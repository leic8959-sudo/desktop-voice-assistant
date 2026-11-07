"""只合成TTS的主动播报；不创建ASR、聊天或VoiceSession。"""
from __future__ import annotations

import asyncio
import json
import logging
import re
import uuid
from types import SimpleNamespace

from chat_pipeline import ChatBackend
from .device_preferences import is_quiet_now

LOGGER = logging.getLogger("assistant.proactive_speech")
SAMPLE_RATE = 24000
BYTES_PER_SECOND = SAMPLE_RATE * 2
MAX_AUDIO_BYTES = min(2 * 1024 * 1024, 30 * BYTES_PER_SECOND)
FRAME_BYTES = BYTES_PER_SECOND // 50


class _Stopped(Exception):
    pass


class _TrackedSocket:
    def __init__(self, socket, collector):
        self.socket, self.collector = socket, collector

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.socket.__anext__()
        if message.type.name == "TEXT":
            header = json.loads(message.data).get("header", {})
            if header.get("event") == "task-finished":
                self.collector.finished = True
        return message

    def __getattr__(self, name):
        return getattr(self.socket, name)


class _TrackedClient:
    def __init__(self, client, collector):
        self.client, self.collector = client, collector

    async def ws_connect(self, url, **kwargs):
        kwargs["max_msg_size"] = min(kwargs.get("max_msg_size", 2 * 1024 * 1024), 2 * 1024 * 1024)
        socket = await self.client.ws_connect(url, **kwargs)
        tracked = _TrackedSocket(socket, self.collector)
        self.collector.socket = tracked
        return tracked


class _TtsCollector(ChatBackend):
    """继承既有TTS协议方法；故意不调用ChatBackend.connect/close。"""

    def __init__(self, config, http):
        super().__init__(SimpleNamespace(config=config), "proactive_tts")
        self.client = _TrackedClient(http, self)
        self._tts_text = asyncio.Queue(maxsize=1)
        self._cancelled = set()
        self.audio = bytearray()
        self.socket = None
        self.finished = False
        self.tasks = []

    def on_tts_audio(self, response_id, pcm):
        if self._closed:
            return
        if len(self.audio) + len(pcm) > MAX_AUDIO_BYTES:
            raise ValueError("主动播报合成超过30秒或2MiB上限，未发送设备")
        self.audio.extend(pcm)

    async def synthesize(self, text):
        task_id = str(uuid.uuid4())
        # _open_tts已包含15秒握手限制；外层覆盖建连、喂文本及读音频全过程。
        async with asyncio.timeout(35):
            socket = await self._open_tts(task_id)
            self._tts_text.put_nowait(None)
            self.tasks = [asyncio.create_task(self._tts_feed(socket, task_id, text)), asyncio.create_task(self._tts_read(socket, task_id))]
            await asyncio.gather(*self.tasks)
        if not self.finished or not self.audio or len(self.audio) % 2:
            raise ValueError("TTS没有返回完整PCM16音频，未发送设备")
        return self.audio

    async def dispose(self):
        self._closed = True
        for task in self.tasks:
            if not task.done():
                task.cancel()
        if self.tasks:
            await asyncio.gather(*self.tasks, return_exceptions=True)
        if self.socket is not None:
            try:
                await asyncio.wait_for(self.socket.close(), timeout=2)
            except Exception:
                LOGGER.exception("proactive_tts_socket_cleanup_failed")
        self.audio.clear()


class ProactiveSpeech:
    """父入口必须在voice_lock内先stop再注册会话；流式发送不持该锁。"""

    def __init__(self, config, runtime, http):
        self.config, self.runtime, self.http = config, runtime, http
        self.enabled = False
        self.kinds = {"summary"}
        self.active = None
        self.closed = False
        self._stop_reason = "cancelled"
        self._stopping = asyncio.Lock()

    async def start(self):
        self.closed = False

    def _preferences(self):
        return self.runtime.device_preferences_store.snapshot()

    def _blocked(self, device=None, during_playback=False, settings_gated=True):
        # settings_gated=False 仅用于用户主动触发的演示类播报：跳过提醒开关与免打扰偏好，仍尊重会话占用与设备状态。
        if self.closed or (settings_gated and not self.enabled):
            return "disabled"
        if self.runtime.voice_sessions or any(item.task is not None and not item.task.done() for item in tuple(self.runtime.native_devices)):
            return "voice_session_active"
        allowed = {"idle", "speaking"} if during_playback else {"idle"}
        if self.runtime.last_device_state not in allowed:
            return "device_busy"
        if not settings_gated:
            return None
        values = self._preferences()
        if values["dnd_enabled"] and is_quiet_now(values):
            return "do_not_disturb"
        if values["reminders_muted"] or values["reminder_volume"] == 0:
            return "reminders_muted"
        # 当前固件TTS使用会话通道；不修改设备音量配置，额外尊重会话静音。
        if values["conversation_muted"] or values["conversation_volume"] == 0:
            return "conversation_output_muted"
        if device is not None and (device.socket.closed or device not in self.runtime.native_devices):
            return "device_offline"
        return None

    def _device(self):
        online = [item for item in tuple(self.runtime.native_devices) if not item.socket.closed]
        if len(online) != 1:
            return None, "no_online_device" if not online else "multiple_devices"
        device = online[0]
        version = re.fullmatch(r"(\d+)\.(\d+)\.(\d+)(?:[-+].*)?", getattr(device, "firmware_version", ""))
        if not version or tuple(map(int, version.groups())) < (0, 3, 5) or not getattr(device, "screen_cards_supported", False):
            return None, "firmware_protocol_unknown"
        return device, None

    async def speak(self, text, *, kind, event_id, fresh=True, settings_gated=True, payload_extra=None):
        if not fresh:
            return {"status": "skipped", "reason": "expired_event"}
        if settings_gated and kind not in self.kinds:
            return {"status": "skipped", "reason": "category_disabled"}
        if not isinstance(text, str) or not text.strip():
            return {"status": "skipped", "reason": "empty_text"}
        if payload_extra is not None and not isinstance(payload_extra, dict):
            return {"status": "skipped", "reason": "invalid_payload_extra"}
        async with self.runtime.voice_lock:
            if self.active is not None and not self.active.done():
                return {"status": "skipped", "reason": "speech_busy"}
            blocked = self._blocked(settings_gated=settings_gated)
            device, device_error = self._device()
            if blocked or device_error:
                return {"status": "skipped", "reason": blocked or device_error}
            required = ("aliyun_api_key", "chat_tts_model", "chat_tts_voice")
            if any(not isinstance(self.config.get(key), str) or not self.config[key] or "REPLACE" in self.config[key] for key in required):
                return {"status": "skipped", "reason": "tts_not_configured"}
            media = getattr(self.runtime, "media_service", None)
            if media is not None:
                await media.before_voice_session()
            music = getattr(self.runtime, "device_music", None)
            if music is not None:
                await music.before_voice_session()
            # 先登记独立子任务，再释放锁；新会话入口可stop并等待清理后取得音频通道。
            self._stop_reason = "cancelled"
            task = asyncio.create_task(self._run(device, text[:120], event_id, len(text) > 120, settings_gated, payload_extra), name="proactive-speech")
            self.active = task
        try:
            return await asyncio.shield(task)
        except asyncio.CancelledError:
            if task.cancelled() and not asyncio.current_task().cancelling():
                return {"status": "cancelled", "reason": self._stop_reason, "device_playback_confirmed": False}
            await self.stop("publisher_closed")
            raise
        finally:
            if self.active is task and task.done():
                self.active = None

    async def stop(self, reason="voice_session_started"):
        # 不获取voice_lock；父NativeDevice.start/open_session可持该锁调用。
        async with self._stopping:
            task = self.active
            if task is not None and not task.done():
                self._stop_reason = reason
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
            if self.active is task:
                self.active = None

    async def close(self):
        self.closed = True
        await self.stop("service_closed")

    async def navigation_step(self, identity, icon):
        # 与新会话争用同一个入口锁；先显示方向，再合成这一句音频。
        async with self.runtime.voice_lock:
            blocked = self._blocked(settings_gated=False)
            device, device_error = self._device()
            if blocked or device_error:
                return {"status": "skipped", "reason": blocked or device_error}
            await asyncio.wait_for(device.send_json({"type": "navigation", "state": "step", "navigation_id": identity, "nav_icon": icon}), timeout=2)
            return {"status": "sent"}

    async def navigation_end(self, identity):
        """只清理本次导航图标，不操作音频或会话状态。"""
        device, device_error = self._device()
        if device_error:
            return {"status": "skipped", "reason": device_error}
        try:
            await asyncio.wait_for(device.send_json({"type": "navigation", "state": "end", "navigation_id": identity}), timeout=2)
            return {"status": "sent"}
        except Exception:
            LOGGER.exception("navigation_indicator_cleanup_failed")
            return {"status": "failed"}

    async def _watch(self, device, playback, settings_gated=True):
        while True:
            await asyncio.sleep(0.05)
            blocked = self._blocked(device, during_playback=playback.is_set(), settings_gated=settings_gated)
            if blocked:
                raise _Stopped(blocked)

    async def _run(self, device, text, event_id, truncated, settings_gated=True, payload_extra=None):
        collector = _TtsCollector(self.config, self.http)
        playback_id = "proactive-" + uuid.uuid4().hex
        playback = asyncio.Event()
        stream = asyncio.create_task(self._produce(collector, device, text, playback_id, playback, settings_gated, payload_extra))
        watcher = asyncio.create_task(self._watch(device, playback, settings_gated))
        result = {"status": "unknown", "playback_id": playback_id, "event_id": event_id, "text_truncated": truncated, "device_playback_confirmed": False}
        try:
            async with asyncio.timeout(70):
                done, _ = await asyncio.wait((stream, watcher), return_when=asyncio.FIRST_COMPLETED)
                if watcher in done:
                    await watcher
                sent = await stream
            result.update(status="sent", **sent, message="音频与结束标记已发送，设备实际播放未确认")
        except _Stopped as error:
            result.update(status="cancelled" if playback.is_set() else "skipped", reason=str(error))
        except asyncio.CancelledError:
            result.update(status="cancelled", reason=self._stop_reason)
        except Exception:
            LOGGER.exception("proactive_speech_failed event_id=%s", event_id)
            result.update(status="unknown" if playback.is_set() else "failed", message="本次合成或发送失败，未自动重试")
        finally:
            started = playback.is_set()
            for task in (stream, watcher):
                if not task.done():
                    task.cancel()
            await asyncio.gather(stream, watcher, return_exceptions=True)
            if started and result["status"] != "sent" and not device.socket.closed:
                try:
                    await asyncio.wait_for(device.send_json({"type": "interrupt", "playback_id": playback_id}), timeout=2)
                except Exception:
                    LOGGER.exception("proactive_speech_interrupt_failed event_id=%s", event_id)
                    result["stop_delivery"] = "unknown"
            await collector.dispose()
        return result

    async def _produce(self, collector, device, text, playback_id, playback, settings_gated=True, payload_extra=None):
        # 导航“直行”被TTS读成zhí háng，合成用无多音歧义的“直走”，屏幕保留原文。
        spoken_text = text.replace("直行", "直走") if payload_extra and payload_extra.get("nav_icon") else text
        pcm = await collector.synthesize(spoken_text)
        blocked = self._blocked(device, settings_gated=settings_gated)
        if blocked:
            raise _Stopped(blocked)
        playback.set()
        start_payload = {"type": "tts", "state": "start", "playback_id": playback_id, "text": text}
        if payload_extra:
            start_payload.update(payload_extra)
        await asyncio.wait_for(device.send_json(start_payload), timeout=2)
        loop, sent = asyncio.get_running_loop(), 0
        deadline = loop.time()
        for offset in range(0, len(pcm), FRAME_BYTES):
            if loop.time() - deadline > 0.12:
                deadline = loop.time()
            delay = deadline - loop.time()
            if delay > 0:
                await asyncio.sleep(delay)
            blocked = self._blocked(device, during_playback=True, settings_gated=settings_gated)
            if blocked:
                raise _Stopped(blocked)
            frame = bytes(pcm[offset:offset + FRAME_BYTES])
            await asyncio.wait_for(device.send_bytes(frame), timeout=2)
            sent += len(frame)
            deadline += len(frame) / BYTES_PER_SECOND
        await asyncio.wait_for(device.send_json({"type": "tts", "state": "stop", "playback_id": playback_id}), timeout=2)
        # 给最后一帧及结束标记一个有界排空窗口；这不是设备播放确认。
        await asyncio.sleep(0.3)
        return {"audio_bytes_sent": sent, "audio_seconds": sent / BYTES_PER_SECOND, "sample_rate": SAMPLE_RATE}

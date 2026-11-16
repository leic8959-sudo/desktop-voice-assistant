"""互联网点歌与设备音轨编排；音乐平台凭据和流地址不交给语音模型。"""

from __future__ import annotations

import asyncio
import json
import logging
import math
import os
import secrets
import tempfile
import time
from collections import OrderedDict
from datetime import datetime, timedelta, timezone
from pathlib import Path

from src.assistant_app.integrations.music_assistant import MusicAssistantClient, MusicAssistantError
from .device_media_stream import DeviceMediaStream
from .errors import LocalToolArgumentsError, LocalToolRejectedError

LOGGER = logging.getLogger("assistant.internet_music")
MAX_QUEUE = 8
MAX_SELECTIONS = 64
SELECTION_TTL_S = 600
MUSIC_SELECTION_DECISION_TOOL = {
    "type": "function", "name": "music_selection_decision",
    "description": "对当前歌曲候选作非播放决策：cancel取消；clarify需要询问；unrelated用户转了话题。不会播放或排队，播放必须调用music_play/queue的真实selection_id。",
    "parameters": {"type": "object", "properties": {"outcome": {"type": "string", "enum": ["cancel", "clarify", "unrelated"]}, "message": {"type": "string", "maxLength": 300}}, "required": ["outcome"], "additionalProperties": False},
}


class InternetMusicService:
    def __init__(self, config, store, runtime, http):
        self.config, self.store, self.runtime, self.http = config, store, runtime, http
        self.client = MusicAssistantClient(config, http)
        self.stream = DeviceMediaStream(runtime, http)
        self.runtime.device_music = self
        self.directory = Path(store.path).parent
        self.state_path = self.directory / "device-music-state.json"
        self.selections = OrderedDict()
        self.queue = []
        self.current = None
        self.bookmark = None
        self.pending = None
        self.last_error = None
        self.closed = True
        self.worker = None
        self.reporter = None
        self.reports = asyncio.Queue(maxsize=4)
        self.lock = asyncio.Lock()
        self.prepare_lock = asyncio.Lock()
        self.preparing_session = None
        self.generated = {}
        self.last_checkpoint = 0.0
        self.generation = 0
        self.platform_session = None
        self.guard_until = 0.0
        string = {"type": "string"}
        definitions = (
            ("music_search", "在已授权曲库搜索歌曲，返回真实selection_id及歌手/专辑。按当前会话上下文理解原版、现场版、随便一个等选择；只有无法确定且用户未授权代选时询问。", {"query": {"type": "string", "maxLength": 200}, "limit": {"type": "integer", "minimum": 1, "maximum": 5}}, ["query"]),
            ("music_play", "用户明确要求后在伊迪斯实体喇叭播放搜索返回的selection_id；只有试听时播放试听并明确告知，scheduled表示已安排，尚未发声。", {"selection_id": string}, ["selection_id"]),
            ("music_queue", "按用户要求将搜索候选加入最多8首设备播放队列。", {"selection_id": string}, ["selection_id"]),
            ("music_noise", "在伊迪斯实体喇叭播放本地生成雨声或白噪音；半小时为30分钟，每次最长30分钟。", {"source": {"type": "string", "enum": ["rain", "white_noise"]}, "minutes": {"type": "integer", "minimum": 1, "maximum": 30}}, ["source"]),
            ("music_pause", "用户明确要求暂停实体设备音乐，保留设备回执中的实际进度。", {}, []),
            ("music_resume", "用户明确要求继续实体设备音乐，从设备已报告进度恢复；进度未知时不能假装续播。", {}, []),
            ("music_stop", "用户明确要求停止实体设备音乐并清空队列。", {}, []),
            ("music_status", "查询实体设备音乐、实际设备进度、音源配置和队列；不返回媒体地址或令牌。", {}, []),
        )
        self.tool_schemas = [{"type": "function", "name": name, "description": description, "parameters": {"type": "object", "properties": properties, "required": required, "additionalProperties": False}} for name, description, properties, required in definitions]

    async def start(self):
        await self.client.start()
        await self._thread(self._load)
        self.closed = False
        self.worker = asyncio.create_task(self._loop(), name="device-music-coordination")
        self.reporter = asyncio.create_task(self._report_loop(), name="music-assistant-device-reports")

    async def close(self):
        self.closed = True
        if self.worker:
            self.worker.cancel()
            await asyncio.gather(self.worker, return_exceptions=True)
        async with self.lock:
            self.pending = None
            await self._pause_locked("service_closed")
        await self.stream.close()
        if self.reporter:
            self.reporter.cancel()
            await asyncio.gather(self.reporter, return_exceptions=True)
        await self.client.close()
        for path in tuple(self.generated.values()):
            await self._thread(path.unlink, missing_ok=True)
        self.generated.clear()

    async def _thread(self, function, *args, **kwargs):
        task = asyncio.create_task(asyncio.to_thread(function, *args, **kwargs))
        try:
            return await asyncio.shield(task)
        except asyncio.CancelledError:
            while not task.done():
                try:
                    await asyncio.shield(task)
                except asyncio.CancelledError:
                    continue
                except Exception:
                    break
            if task.done() and not task.cancelled():
                task.exception()
            raise

    def _device(self):
        devices = [device for device in tuple(self.runtime.native_devices) if not device.socket.closed]
        if len(devices) != 1:
            raise ValueError("请连接一个伊迪斯实体设备后播放" if not devices else "存在多个在线设备，请先选择唯一播放设备")
        device = devices[0]
        if not getattr(device, "media_stream_supported", False):
            raise ValueError("当前设备固件尚未支持音乐通道，请安装媒体固件后再播放")
        return device

    def _busy(self):
        speech = getattr(self.runtime, "proactive_speech", None)
        task = getattr(speech, "active", None)
        return time.monotonic() < self.guard_until or any(not session.closed for session in tuple(self.runtime.voice_sessions)) or task is not None and not task.done()

    @staticmethod
    def _public(item):
        if not item:
            return None
        return {key: item.get(key) for key in ("title", "artists", "album", "duration_s", "source", "minutes", "playback_access") if key in item}

    def status(self):
        device_items = [{"firmware": str(getattr(device, "firmware_version", "unknown")), "online": not device.socket.closed, "supports_music": bool(getattr(device, "media_stream_supported", False))} for device in tuple(self.runtime.native_devices)]
        return {"configuration": self.client.status(), "devices": device_items, "output": "xiaoyanzi_speaker", "current": self._public(self.current), "queue": [self._public(item) for item in self.queue], "queue_limit": MAX_QUEUE, "stream": self.stream.status(), "pending": {"expires_in_s": max(0, round(self.pending["expires"] - time.monotonic())), "ready": self.pending["ready"]} if self.pending else None, "bookmark": {"position_s": self.bookmark.get("position_s"), "position_confirmed": self.bookmark.get("position_confirmed")} if self.bookmark else None, "error": self.last_error}

    async def configure(self, payload):
        async with self.lock:
            if self.current or self.pending:
                raise ValueError("请先停止当前设备播放，再修改音乐服务配置")
            result = await self.client.configure(payload)
            self.selections.clear()
            return result

    async def search(self, query, limit=5, *, voice_session=None):
        if not isinstance(query, str) or not 1 <= len(query.strip()) <= 200 or type(limit) is not int or not 1 <= limit <= 5:
            raise ValueError("请输入1～200字的歌名或歌手，最多返回5个候选")
        result = await self.client.search(query.strip(), limit=limit)
        items = result.get("items", []) if isinstance(result, dict) else result
        if not isinstance(items, list):
            raise RuntimeError("音乐服务返回的候选格式无效")
        now = time.monotonic()
        self.selections = OrderedDict((key, item) for key, item in self.selections.items() if item["expires"] > now)
        public = []
        for item in items[:limit]:
            if not isinstance(item, dict) or not isinstance(item.get("ref"), dict) or not isinstance(item.get("title"), str):
                raise RuntimeError("音乐服务候选缺少可验证引用")
            if item.get("available") is False:
                continue
            selection = secrets.token_urlsafe(18)
            track = {"ref": item["ref"], "title": item["title"][:200], "artists": [str(value)[:100] for value in item.get("artists", [])[:5]], "album": str(item.get("album", ""))[:200], "duration_s": item.get("duration_s"), "source": "music_assistant"}
            self.selections[selection] = {"track": track, "expires": now + SELECTION_TTL_S, "session_id": voice_session.session_id if voice_session else None}
            public.append({"selection_id": selection, **self._public(track)})
            while len(self.selections) > MAX_SELECTIONS:
                self.selections.popitem(last=False)
        return {"items": public, "selection_ttl_s": SELECTION_TTL_S, "source": "music_assistant", "output": "xiaoyanzi_speaker"}

    def _selection(self, identity, voice_session=None):
        item = self.selections.get(identity) if isinstance(identity, str) else None
        if item is None or item["expires"] <= time.monotonic():
            raise LocalToolArgumentsError()
        if voice_session is not None and item.get("session_id") != voice_session.session_id:
            raise LocalToolRejectedError()
        return dict(item["track"])

    async def play(self, selection_id, *, voice_session=None):
        return await self._request(self._selection(selection_id, voice_session), voice_session)

    async def _request(self, track, voice_session=None, position_s=0):
        self._device()
        async with self.lock:
            self.generation += 1
            generation = self.generation
            self.pending = None
            if voice_session is not None:
                if self.closed or voice_session.closed:
                    return {"status": "cancelled", "message": "请求已取消，没有开始播放"}
                self.pending = {"track": track, "position_s": position_s, "session": voice_session, "ready": False, "expires": time.monotonic() + 120, "generation": generation}
        if voice_session is not None:
            try:
                if track.get("source") == "music_assistant":
                    # 提前验证音源；立即释放预检租约，交接时重新取流，避免等待回答导致租约过期。
                    async with self.prepare_lock:
                        source = None
                        try:
                            source = await self.client.resolve_stream(track["ref"], position_s=position_s)
                            track = {**track, "playback_access": source.get("playback_access", "unknown")}
                        finally:
                            if source is not None:
                                await self.client.stop(session_id=source["session_id"])
            except BaseException as error:
                async with self.lock:
                    if generation == self.generation:
                        self.pending = None
                        if isinstance(error, MusicAssistantError):
                            self.last_error = str(error)
                raise
            async with self.lock:
                if generation != self.generation or self.closed or voice_session.closed or self.pending is None:
                    if generation == self.generation:
                        self.pending = None
                    return {"status": "cancelled", "message": "请求已取消，没有开始播放"}
                self.last_error = None
                self.pending["track"] = track
                preview = track.get("playback_access") == "preview"
                return {"status": "scheduled", "output": "xiaoyanzi_speaker", "title": track["title"], "playback_access": track.get("playback_access", "unknown"), "message": "准备播放《" + track["title"] + ("》的试听。" if preview else "》。")}
        return await self._begin(track, position_s, expected_generation=generation)

    async def _begin(self, track, position_s=0, *, expected_generation):
        async with self.prepare_lock:
            self.preparing_session = None
            try:
                result = await self._prepare_track(track, position_s, expected_generation)
                if result.get("state") in {"starting", "playing"}:
                    self.preparing_session = None
                return result
            finally:
                if self.preparing_session:
                    await self.client.stop(session_id=self.preparing_session)
                self.preparing_session = None

    async def _prepare_track(self, track, position_s, generation):
        async with self.runtime.voice_lock:
            if generation != self.generation:
                return {"status": "cancelled", "message": "已有新的播放操作，本次未开始"}
            if self.closed or self._busy():
                return {"status": "blocked", "message": "语音会话正在使用设备，请结束对话后再播放"}
            device = self._device()
        if track.get("source") in {"rain", "white_noise"}:
            source = await self._noise_source(track)
        else:
            source = await self.client.resolve_stream(track["ref"], position_s=position_s)
            self.preparing_session = source.get("session_id")
        preview = source.get("playback_access") == "preview"
        source = {**source, "title": track["title"] + ("（试听）" if preview else "")}
        if track.get("source") == "music_assistant":
            track = {**track, "playback_access": source.get("playback_access", "unknown")}
            if preview:
                track["duration_s"] = source["duration_s"]
            elif track.get("duration_s"):
                source["duration_s"] = track["duration_s"]
        async with self.runtime.voice_lock:
            async with self.lock:
                if generation != self.generation or self.closed or self._busy():
                    return {"status": "cancelled", "message": "播放准备期间发生新操作，本次未开始"}
                desktop = getattr(self.runtime, "media_service", None)
                if desktop is not None:
                    await desktop.before_voice_session()
                await self.stream.stop()
                if source.get("kind") == "trusted_file" and self.platform_session:
                    await self.client.stop(session_id=self.platform_session)
                self.platform_session = source.get("session_id")
                self.current = dict(track)
                self.bookmark = {"position_s": position_s, "position_confirmed": position_s == 0}
                self.last_error = None
                try:
                    result = await self.stream.start(device, source, position_s=position_s, playback_id="music-" + secrets.token_hex(12))
                    await self._save()
                except BaseException:
                    await self.stream.stop()
                    self.last_error = "播放启动或进度持久保存失败，已停止本次设备媒体"
                    raise
                return {**result, "output": "xiaoyanzi_speaker", "title": track["title"]}

    async def _noise_source(self, track):
        source = track["source"]
        desktop = getattr(self.runtime, "media_service", None)
        if desktop is None:
            raise RuntimeError("本地环境声生成器尚未加载")
        if source not in self.generated:
            self.generated[source] = await self._thread(desktop._noise, source)
        return {"kind": "trusted_file", "path": str(self.generated[source]), "root": str(self.directory), "duration_s": track["minutes"] * 60, "loop": True, "title": track["title"]}

    async def action(self, name, arguments=None, *, voice_session=None):
        arguments = arguments or {}
        if not isinstance(arguments, dict):
            raise ValueError("音乐操作参数必须是对象")
        if name in {"pause", "music_pause"}:
            async with self.lock:
                self.generation += 1
                self.pending = None
                return await self._pause_locked("user")
        if name in {"stop", "music_stop"}:
            async with self.lock:
                self.generation += 1
                self.pending = None
                self.current = self.bookmark = None
                self.queue.clear()
                cleanup = asyncio.create_task(self._stop_locked())
                try:
                    return await asyncio.shield(cleanup)
                except asyncio.CancelledError:
                    try:
                        await cleanup
                    except Exception:
                        LOGGER.exception("device_music_stop_cleanup_failed_after_cancel")
                    raise
        if name in {"resume", "music_resume"}:
            if not self.current or not self.bookmark or not self.bookmark.get("position_confirmed"):
                raise ValueError("没有设备确认的续播进度，请重新点播")
            return await self._request(dict(self.current), voice_session, self.bookmark["position_s"])
        if name in {"queue", "music_queue"}:
            track = self._selection(arguments.get("selection_id"), voice_session)
            async with self.lock:
                if len(self.queue) >= MAX_QUEUE:
                    raise ValueError("设备音乐队列已满，最多8首")
                self.queue.append(track)
                await self._save()
            return {"status": "queued", "queue_count": len(self.queue), "output": "xiaoyanzi_speaker"}
        if name in {"noise", "music_noise"}:
            source, minutes = arguments.get("source"), arguments.get("minutes", 30)
            if source not in {"rain", "white_noise"} or type(minutes) is not int or not 1 <= minutes <= 30:
                raise ValueError("请选择雨声或白噪音，时长1～30分钟")
            return await self._request({"source": source, "minutes": minutes, "title": "雨声" if source == "rain" else "白噪音", "duration_s": minutes * 60}, voice_session)
        raise ValueError("未知设备音乐操作")

    async def _stop_locked(self):
        problems = []
        persistence_failed = False
        saved = False
        try:
            try:
                # 先保存取消意图，设备或平台失败不能恢复后续队列和续播资格。
                await self._save()
                saved = True
            except asyncio.CancelledError:
                self.last_error = "停止请求中断，播放取消状态保存未确认"
                raise
            except Exception:
                persistence_failed = True
                self.last_error = "播放取消状态保存失败，重启前请核对本地存储"
                LOGGER.exception("device_music_stop_persistence_failed")
                raise
        finally:
            try:
                try:
                    result = await self.stream.stop()
                    if result.get("state") != "stopped":
                        problems.append("device_stop_unconfirmed")
                except Exception:
                    LOGGER.exception("device_music_stop_failed")
                    problems.append("device_stop_failed")
            finally:
                try:
                    await self.client.stop()
                except Exception:
                    LOGGER.exception("music_assistant_stop_failed")
                    problems.append("platform_stop_failed")
                else:
                    self.platform_session = None
            if problems:
                if saved:
                    prefix = "后续播放已取消；"
                elif persistence_failed:
                    prefix = "播放取消状态保存失败；"
                else:
                    prefix = "播放取消状态保存未确认；"
                self.last_error = prefix + "设备或平台停止结果待核实，不会自动重试"
        if problems:
            return {"status": "unknown", "output": "xiaoyanzi_speaker", "message": self.last_error, "reasons": problems}
        self.last_error = None
        return {"status": "stopped", "output": "xiaoyanzi_speaker"}

    async def _pause_locked(self, reason):
        if self.current:
            before = self.stream.status()
            if before.get("state") not in {"starting", "playing"}:
                if before.get("state") == "error":
                    return {**before, "status": "unknown", "retryable": False, "reason": "pause_unconfirmed", "message": "暂停未确认；最近设备进度不代表已暂停，请先核查实际状态。", "position_s": (self.bookmark or {}).get("position_s")}
                return {"status": before.get("state", "paused"), "reason": reason, "position_s": (self.bookmark or {}).get("position_s")}
            result = await self.stream.pause()
            position = self.stream.status().get("device_position_s")
            self.bookmark = {"position_s": position, "position_confirmed": position is not None}
            await self._save()
            if result.get("state") == "error":
                return {**result, "status": "unknown", "retryable": False, "reason": "pause_unconfirmed", "message": "暂停未确认；最近设备进度不代表已暂停，请先核查实际状态。", "position_s": position}
            return {**result, "status": result.get("state", "paused"), "reason": reason, "position_s": position}
        return {"status": "stopped", "reason": reason}

    async def before_voice_session(self):
        # 父持有voice_lock时可调用；此处不反向获取voice_lock。
        async with self.lock:
            self.generation += 1
            self.guard_until = time.monotonic() + 2
            return await self._pause_locked("voice_session")

    async def cancel_voice_request(self, session):
        async with self.lock:
            if self.pending and self.pending["session"] is session:
                self.pending = None
                self.generation += 1

    async def voice_response_finished(self, session):
        async with self.lock:
            if not self.pending or self.pending["session"] is not session or self.pending["expires"] <= time.monotonic():
                return False
            self.pending["ready"] = True
            return True

    async def on_device_event(self, device, event):
        accepted = await self.stream.on_device_event(device, event)
        if not accepted:
            return
        state = self.stream.status()
        if event.get("playback_id") != state.get("playback_id") or not self.current or self.current.get("source") != "music_assistant":
            return
        phase = event.get("state", event.get("phase"))
        reported = {"start": "playing", "progress": "playing", "pause": "paused", "stop": "stopped", "ended": "finished", "error": "error"}.get(phase)
        if reported:
            item = {"state": reported, "position_s": state.get("device_position_s") or 0, "playback_id": event["playback_id"], "session_id": self.platform_session}
            if self.reports.full():
                self.reports.get_nowait()
                self.reports.task_done()
            self.reports.put_nowait(item)

    async def _report_loop(self):
        while True:
            item = await self.reports.get()
            try:
                if item["playback_id"] == self.stream.status().get("playback_id"):
                    await self.client.report(item["state"], position_s=item["position_s"], session_id=item["session_id"])
            except asyncio.CancelledError:
                raise
            except Exception:
                LOGGER.exception("music_assistant_player_report_failed state=%s", item["state"])
                self.last_error = "设备已处理音频，但音乐平台进度同步失败"
            finally:
                self.reports.task_done()

    async def invoke(self, name, arguments, subject="voice", *, voice_authorized=False, voice_session=None):
        schema = next((tool["parameters"] for tool in self.tool_schemas if tool["name"] == name), None)
        if schema is None or not isinstance(arguments, dict) or set(arguments) - set(schema["properties"]) or any(key not in arguments for key in schema["required"]):
            raise LocalToolArgumentsError()
        if name == "music_status":
            return self.status()
        if name == "music_search":
            return await self.search(arguments.get("query"), arguments.get("limit", 5), voice_session=voice_session)
        if voice_authorized is not True:
            raise PermissionError("需要本轮明确的点歌或播放操作指令")
        if name == "music_play":
            return await self.play(arguments.get("selection_id"), voice_session=voice_session)
        return await self.action(name, arguments, voice_session=voice_session)

    async def _loop(self):
        while not self.closed:
            try:
                request = None
                async with self.lock:
                    if self.pending and self.pending["expires"] <= time.monotonic():
                        self.pending = None
                        self.last_error = "等待本轮回答结束超过120秒，播放请求已取消"
                    if self.pending and self.pending["ready"] and self.pending["session"].closed and not self._busy():
                        request = self.pending
                        self.pending = None
                    state = self.stream.status()
                    if self.current and state.get("state") == "error":
                        self.last_error = "设备媒体流失败，请检查音乐服务和设备连接"
                    if self.current and state.get("state") == "ended":
                        self.current = self.bookmark = None
                        await self._save()
                    if not request and self.current is None and self.queue and not self._busy():
                        self.generation += 1
                        request = {"track": self.queue.pop(0), "position_s": 0, "generation": self.generation}
                        await self._save()
                    if self.current and state.get("device_position_s") is not None and time.monotonic() - self.last_checkpoint >= 5:
                        self.bookmark = {"position_s": state["device_position_s"], "position_confirmed": True}
                        await self._save()
                        self.last_checkpoint = time.monotonic()
                if request:
                    await self._begin(request["track"], request["position_s"], expected_generation=request["generation"])
            except asyncio.CancelledError:
                raise
            except Exception as error:
                LOGGER.exception("device_music_coordination_failed")
                if request is not None and request["generation"] != self.generation:
                    continue
                self.last_error = str(error) if isinstance(error, MusicAssistantError) else "设备点播未完成，请核对音乐服务配置、授权和网络"
                try:
                    await self._device().send_json({"type": "alert", "status": "音乐未能播放", "message": self.last_error, "emotion": "neutral"})
                except Exception:
                    LOGGER.exception("device_music_error_delivery_failed")
            await asyncio.sleep(0.2)

    async def _save(self):
        value = {"version": 1, "expires_at": (datetime.now(timezone.utc) + timedelta(days=7)).isoformat(), "current": self.current, "bookmark": self.bookmark, "queue": self.queue}
        await self._thread(self._write, value)

    def _write(self, value):
        encoded = json.dumps(value, ensure_ascii=False, allow_nan=False)
        if len(encoded.encode("utf-8")) > 65536:
            raise ValueError("设备音乐状态超过64KiB上限")
        descriptor, temporary = tempfile.mkstemp(prefix="device-music-", suffix=".tmp", dir=self.directory)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as target:
                target.write(encoded)
                target.flush()
                os.fsync(target.fileno())
            os.replace(temporary, self.state_path)
        finally:
            Path(temporary).unlink(missing_ok=True)

    def _load(self):
        if not self.state_path.exists():
            return
        if self.state_path.stat().st_size > 65536:
            raise ValueError("设备音乐状态文件超过64KiB")
        value = json.loads(self.state_path.read_text(encoding="utf-8"))
        if not isinstance(value, dict) or value.get("version") != 1 or not isinstance(value.get("queue"), list) or len(value["queue"]) > MAX_QUEUE:
            raise ValueError("设备音乐恢复状态无效")
        expiry = datetime.fromisoformat(value["expires_at"])
        if expiry.tzinfo is None:
            raise ValueError("设备音乐恢复时间缺少时区")
        if expiry <= datetime.now(timezone.utc):
            return
        for item in [*value["queue"], *([value["current"]] if value.get("current") else [])]:
            if not isinstance(item, dict) or not isinstance(item.get("title"), str) or item.get("source") not in {"music_assistant", "rain", "white_noise"}:
                raise ValueError("设备音乐条目格式无效")
            if item["source"] == "music_assistant" and not isinstance(item.get("ref"), dict):
                raise ValueError("设备音乐引用无效")
            if item["source"] == "music_assistant":
                ref = item["ref"]
                if set(ref) - {"uri", "provider", "item_id"} or any(not isinstance(value, str) or not 1 <= len(value) <= 1024 for value in ref.values()) or not ref.get("uri"):
                    raise ValueError("设备音乐恢复引用无效")
            elif type(item.get("minutes")) is not int or not 1 <= item["minutes"] <= 30:
                raise ValueError("设备环境声恢复时长无效")
        bookmark = value.get("bookmark")
        if bookmark is not None and (not isinstance(bookmark, dict) or type(bookmark.get("position_confirmed")) is not bool or bookmark.get("position_confirmed") is True and bookmark.get("position_s") is None or bookmark.get("position_s") is not None and (type(bookmark["position_s"]) not in {int, float} or not math.isfinite(bookmark["position_s"]) or not 0 <= bookmark["position_s"] <= 1800)):
            raise ValueError("设备音乐续播位置无效")
        self.current, self.bookmark, self.queue = value.get("current"), bookmark, value["queue"]

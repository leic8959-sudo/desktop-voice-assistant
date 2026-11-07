"""有界可信音源 -> 隔离解码器 -> 原生设备媒体通道；不使用电脑声卡。"""
from __future__ import annotations

import asyncio
import json
import logging
import math
import re
import subprocess
import sys
import wave
from dataclasses import dataclass, field
from pathlib import Path
from urllib.parse import urlsplit

import aiohttp

LOGGER = logging.getLogger("assistant.device_media_stream")
SAMPLE_RATE = 24000
BYTES_PER_SECOND = SAMPLE_RATE * 2
FRAME_SAMPLES = SAMPLE_RATE // 50
FRAME_BYTES = FRAME_SAMPLES * 2
MAX_TRACK_SECONDS = 1800
MAX_SOURCE_BYTES = 512 * 1024 * 1024
READ_CHUNK_BYTES = 32768
FORMAT_NAMES = {"wav", "flac", "mp3", "ogg", "aac", "s16le"}


class MediaStreamError(RuntimeError):
    pass


def _number(value, name, minimum=0, maximum=MAX_TRACK_SECONDS):
    if type(value) not in {float, int} or not math.isfinite(value) or not minimum <= value <= maximum:
        raise ValueError(name + "超出允许范围")
    return float(value)


def _safe_log(stage, error):
    # 保留完整调用堆栈，用固定安全异常替代可能包含 URL/token 的第三方异常文本。
    safe = MediaStreamError("媒体处理失败，原始异常类型=" + type(error).__name__)
    LOGGER.error("device_media_failed stage=%s", stage, exc_info=(MediaStreamError, safe, error.__traceback__))


@dataclass
class _Playback:
    device: object
    source: dict
    playback_id: str
    origin_samples: int
    state: str = "starting"
    sent_samples: int = 0
    played_samples: int | None = None
    ack: dict | None = None
    error: str | None = None
    start_attempted: bool = False
    start_ack: asyncio.Event = field(default_factory=asyncio.Event)
    end_ack: asyncio.Event = field(default_factory=asyncio.Event)
    control_ack: asyncio.Event = field(default_factory=asyncio.Event)
    task: asyncio.Task | None = None
    process: asyncio.subprocess.Process | None = None
    last_ack_time: float = 0


class DeviceMediaStream:
    """父入口负责 voice_lock 仲裁；本类只取自己的锁，永不反取语音锁。"""

    def __init__(self, runtime, http):
        self.runtime, self.http = runtime, http
        self._lock = asyncio.Lock()
        self._current = None
        self._closed = False
        configured = runtime.config.get("runtime_dir")
        self.runtime_root = Path(configured or Path(__file__).resolve().parents[3] / "runtime").resolve()

    def _source(self, source, position_s):
        if not isinstance(source, dict):
            raise ValueError("媒体音源必须来自受信任适配器")
        title = source.get("title") or "音乐"
        if not isinstance(title, str) or len(title) > 1000 or any(ord(character) < 32 or ord(character) == 127 for character in title):
            raise ValueError("媒体标题无效")
        title = title.strip().encode("utf-8")[:192].decode("utf-8", errors="ignore") or "音乐"
        kind = source.get("kind", source.get("source_kind", "http"))
        if kind == "trusted_file":
            candidate = Path(source.get("path", ""))
            if not candidate.is_absolute():
                raise ValueError("本地音源需要服务端绝对路径")
            path = candidate.resolve(strict=True)
            try:
                relative = path.relative_to(self.runtime_root)
            except ValueError:
                raise ValueError("本地音源不在受限运行目录") from None
            current = self.runtime_root
            for part in relative.parts:
                current = current / part
                if current.is_symlink() or getattr(current, "is_junction", lambda: False)():
                    raise ValueError("本地音源不允许符号链接")
            # 也检查未 resolve 的原始路径，避免链接在规范化后被隐藏。
            current = candidate
            while current != current.parent:
                if current.is_symlink() or getattr(current, "is_junction", lambda: False)():
                    raise ValueError("本地音源不允许符号链接")
                current = current.parent
            if path.suffix.lower() != ".wav" or not path.is_file() or not 44 <= path.stat().st_size <= MAX_SOURCE_BYTES:
                raise ValueError("本地音源必须为受限WAV文件")
            with wave.open(str(path), "rb") as audio:
                if audio.getcomptype() != "NONE" or audio.getsampwidth() != 2 or audio.getnchannels() not in {1, 2} or not 8000 <= audio.getframerate() <= 48000:
                    raise ValueError("本地WAV格式不受支持")
                duration = _number(audio.getnframes() / audio.getframerate(), "本地音轨时长", minimum=0.001)
            loop = source.get("loop", False)
            if type(loop) is not bool:
                raise ValueError("循环标记无效")
            limit = _number(source.get("playback_limit_s", source.get("duration_s", MAX_TRACK_SECONDS) if loop else duration), "本地播放预算", minimum=0.001)
            if position_s >= limit or not loop and position_s >= duration:
                raise ValueError("本地音轨已到结尾")
            return {"kind": kind, "path": path, "format": "wav", "title": title, "duration_s": duration, "loop": loop, "playback_limit_s": limit, "source_position_s": 0, "sample_rate": 0, "channels": 0}
        if kind not in {"http", "music_assistant"} or source.get("trusted_by") != "music_assistant":
            raise ValueError("只接受Music Assistant已验证的媒体URL")
        url, base = source.get("url"), source.get("base_url")
        if not isinstance(url, str) or not isinstance(base, str) or not 1 <= len(url) <= 4096 or not 1 <= len(base) <= 2048:
            raise ValueError("媒体URL验证信息缺失")
        parsed, allowed = urlsplit(url), urlsplit(base)
        try:
            origin = lambda item: (item.scheme, item.hostname, item.port or (443 if item.scheme == "https" else 80))
            valid_origin = origin(parsed) == origin(allowed)
        except ValueError:
            valid_origin = False
        if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password or parsed.fragment or allowed.username or allowed.password or not valid_origin:
            raise ValueError("媒体URL必须使用适配器已验证的同一来源")
        headers = source.get("headers", {})
        if not isinstance(headers, dict) or len(headers) > 16:
            raise ValueError("媒体请求头无效")
        forbidden = {"host", "connection", "proxy-authorization", "proxy-connection", "transfer-encoding", "content-length", "upgrade"}
        if any(not isinstance(key, str) or not re.fullmatch(r"[A-Za-z0-9-]{1,64}", key) or key.lower() in forbidden or not isinstance(value, str) or len(value) > 4096 or any(ord(character) < 32 or ord(character) == 127 for character in value) for key, value in headers.items()):
            raise ValueError("媒体请求头不在安全边界内")
        if sum(len(key) + len(value) for key, value in headers.items()) > 8192:
            raise ValueError("媒体请求头超过容量")
        format_name = source.get("format", "wav")
        if format_name not in FORMAT_NAMES:
            raise ValueError("媒体流容器不受支持")
        source_position = _number(source.get("source_position_s", source.get("start_position_s", 0)), "媒体流起始位置")
        if source_position > position_s + 1 / SAMPLE_RATE:
            raise ValueError("媒体流起始位置晚于目标位置")
        duration = _number(source.get("duration_s") or MAX_TRACK_SECONDS, "音轨时长", minimum=0.001)
        if position_s >= duration:
            raise ValueError("音轨已到结尾")
        rate, channels = source.get("sample_rate", 0), source.get("channels", 0)
        if format_name == "s16le" and (type(rate) is not int or not 8000 <= rate <= 192000 or type(channels) is not int or not 1 <= channels <= 8):
            raise ValueError("原始PCM媒体流需要有效采样率和声道")
        return {"kind": "http", "url": url, "headers": dict(headers), "format": format_name, "title": title, "duration_s": duration, "source_position_s": source_position, "sample_rate": rate if format_name == "s16le" else 0, "channels": channels if format_name == "s16le" else 0}

    def _device_available(self, playback):
        return not playback.device.socket.closed and playback.device in self.runtime.native_devices

    def _voice_active(self):
        return any(not session.closed for session in tuple(self.runtime.voice_sessions))

    def status(self):
        current = self._current
        if current is None:
            return {"state": "stopped", "playback_id": None, "device_position_s": None, "sent_position_s": None, "played_samples": None, "ack": None, "error": None}
        return {"state": current.state, "playback_id": current.playback_id, "device_position_s": round(current.played_samples / SAMPLE_RATE, 3) if current.played_samples is not None else None, "position_s": round(current.played_samples / SAMPLE_RATE, 3) if current.played_samples is not None else None, "played_samples": current.played_samples, "sent_samples": current.sent_samples, "sent_position_s": round(current.sent_samples / SAMPLE_RATE, 3), "position_basis": "device_i2s_ack" if current.played_samples is not None else "unconfirmed", "ack": dict(current.ack) if current.ack else None, "error": current.error}

    async def start(self, device, source, position_s=0, playback_id=None):
        position = _number(position_s, "播放位置", maximum=MAX_TRACK_SECONDS - 1 / SAMPLE_RATE)
        if not isinstance(playback_id, str) or not re.fullmatch(r"[A-Za-z0-9_.:-]{1,63}", playback_id):
            raise ValueError("播放标识无效")
        validated = self._source(source, position)
        async with self._lock:
            if self._closed:
                raise MediaStreamError("设备媒体服务已关闭")
            if not getattr(device, "media_stream_supported", False):
                raise MediaStreamError("设备未声明互联网媒体协议能力，需要先更新固件")
            if device.socket.closed or device not in self.runtime.native_devices:
                raise MediaStreamError("设备离线")
            if self._voice_active():
                raise MediaStreamError("语音会话尚未结束")
            await self._halt("stop", "stopped")
            origin = round(position * SAMPLE_RATE)
            playback = _Playback(device, validated, playback_id, origin, sent_samples=origin)
            self._current = playback
            playback.task = asyncio.create_task(self._run(playback), name="device-media-stream")
            LOGGER.debug("device_media_scheduled playback_id=%s", playback_id)
            return self.status()

    async def pause(self):
        async with self._lock:
            await self._halt("pause", "paused")
            return self.status()

    async def stop(self):
        async with self._lock:
            await self._halt("stop", "stopped")
            return self.status()

    async def close(self):
        self._closed = True
        await self.stop()

    async def _halt(self, command, state):
        playback = self._current
        if playback is None:
            return
        active = playback.task is not None and not playback.task.done()
        previous = playback.state
        if active:
            playback.task.cancel()
            await asyncio.gather(playback.task, return_exceptions=True)
        await self._kill_decoder(playback)
        playback.source = {}
        if not playback.start_attempted:
            playback.state = state
            return
        if not active and previous in {"stopped", "ended"}:
            if command == "stop":
                playback.state = state
            return
        playback.control_ack.clear()
        playback.state = state
        try:
            if not self._device_available(playback):
                raise MediaStreamError("设备已离线")
            await asyncio.wait_for(playback.device.send_json({"type": "media_" + command, "playback_id": playback.playback_id}), timeout=2)
            await asyncio.wait_for(playback.control_ack.wait(), timeout=2)
            if playback.ack is None or playback.ack.get("state") != command:
                raise MediaStreamError("设备未确认媒体控制")
        except Exception as error:
            _safe_log("control_" + command, error)
            playback.state, playback.error = "error", "设备媒体停止确认失败，实际输出状态未知"

    async def on_device_event(self, device, event):
        playback = self._current
        if playback is None or device is not playback.device or not isinstance(event, dict) or event.get("playback_id") != playback.playback_id:
            return False
        if event.get("type") not in {"media_ack", "media_status", "media_progress"}:
            return False
        state = event.get("state", event.get("phase"))
        state = {"started": "start", "playing": "progress", "paused": "pause", "stopped": "stop", "end": "ended"}.get(state, state)
        if state not in {"start", "progress", "pause", "stop", "ended", "error"}:
            return False
        samples = event.get("position_samples", event.get("played_samples"))
        if type(samples) is not int or not playback.origin_samples <= samples <= min(SAMPLE_RATE * MAX_TRACK_SECONDS, playback.sent_samples + FRAME_SAMPLES):
            return False
        if playback.played_samples is not None and samples < playback.played_samples:
            return False
        capacity, used = event.get("queue_capacity"), event.get("queue_used")
        if capacity is not None and (type(capacity) is not int or not 1 <= capacity <= 128):
            return False
        if used is not None and (type(used) is not int or not 0 <= used <= (capacity or 128)):
            return False
        playback.played_samples = samples
        playback.last_ack_time = asyncio.get_running_loop().time()
        playback.ack = {"state": state, "played_samples": samples}
        for key in ("queue_capacity", "queue_used", "underruns"):
            value = event.get(key)
            if type(value) is int and 0 <= value <= 10000000:
                playback.ack[key] = value
        if state == "start":
            if playback.state == "starting":
                playback.state = "playing"
            playback.start_ack.set()
        elif state == "ended":
            playback.state = "ended"
            playback.end_ack.set()
        elif state in {"pause", "stop", "error"}:
            playback.control_ack.set()
            if playback.state != "error":
                playback.state = {"pause": "paused", "stop": "stopped", "error": "error"}[state]
            if state == "error":
                playback.error = "设备拒绝或无法播放本次媒体"
                playback.start_ack.set()
                playback.end_ack.set()
            task = playback.task
            if task is not None and not task.done() and task is not asyncio.current_task():
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)
            if state == "error" and self._device_available(playback):
                try:
                    await asyncio.wait_for(device.send_json({"type": "media_stop", "playback_id": playback.playback_id}), timeout=2)
                except Exception as error:
                    _safe_log("device_error_stop", error)
        return True

    async def _run(self, playback):
        stage = "start"
        try:
            async with asyncio.timeout(MAX_TRACK_SECONDS):
                source = playback.source
                duration = min(MAX_TRACK_SECONDS, math.ceil(source.get("playback_limit_s", source["duration_s"])))
                playback.start_attempted = True
                await asyncio.wait_for(playback.device.send_json({"type": "media_start", "playback_id": playback.playback_id, "format": "pcm_s16le", "sample_rate": SAMPLE_RATE, "channels": 1, "frame_duration_ms": 20, "position_samples": playback.origin_samples, "title": source["title"], "duration_s": duration}), timeout=2)
                await asyncio.wait_for(playback.start_ack.wait(), timeout=3)
                if playback.state not in {"starting", "playing"} or playback.ack is None or playback.ack.get("state") not in {"start", "progress"}:
                    raise MediaStreamError("设备没有接受媒体开始")
                playback.state = "playing"
                LOGGER.debug("device_media_started playback_id=%s", playback.playback_id)
                stage = "decode_send"
                clock = [asyncio.get_running_loop().time()]
                skip = playback.origin_samples / SAMPLE_RATE - source["source_position_s"]
                while True:
                    local_skip = skip % source["duration_s"] if source.get("loop") else skip
                    await self._decode_once(playback, max(0, round(local_skip * SAMPLE_RATE)), clock)
                    if not source.get("loop") or playback.sent_samples >= round(source["playback_limit_s"] * SAMPLE_RATE):
                        break
                    skip = 0
                if playback.sent_samples <= playback.origin_samples:
                    raise MediaStreamError("音源没有可播放音频")
                stage = "end"
                await asyncio.wait_for(playback.device.send_json({"type": "media_end", "playback_id": playback.playback_id}), timeout=2)
                await asyncio.wait_for(playback.end_ack.wait(), timeout=4)
                if playback.ack is None or playback.ack.get("state") != "ended":
                    raise MediaStreamError("设备未确认音轨排空")
                playback.state = "ended"
                LOGGER.debug("device_media_ended playback_id=%s", playback.playback_id)
        except asyncio.CancelledError:
            raise
        except Exception as error:
            _safe_log(stage, error)
            playback.state, playback.error = "error", "媒体网络、解码或设备确认失败，未自动重试"
            if self._device_available(playback):
                try:
                    await asyncio.wait_for(playback.device.send_json({"type": "media_stop", "playback_id": playback.playback_id}), timeout=2)
                except Exception as stop_error:
                    _safe_log("failure_stop", stop_error)
        finally:
            await self._kill_decoder(playback)
            # 仅保留有界状态，立刻释放带临时URL/鉴权头的音源引用。
            playback.source = {}

    async def _kill_decoder(self, playback):
        process = playback.process
        if process is None:
            return
        try:
            if process.returncode is None:
                try:
                    process.kill()
                except ProcessLookupError:
                    pass
            if process.stdin is not None:
                process.stdin.close()
            # kill 后继续排空管道，避免已暂停的stdout transport让wait永久等EOF。
            await process.communicate()
        finally:
            playback.process = None

    async def _decode_once(self, playback, skip_samples, clock):
        source = playback.source
        decoder = Path(__file__).with_name("media_decode.py")
        creation = asyncio.create_task(asyncio.create_subprocess_exec(sys.executable, str(decoder), source["format"], str(skip_samples), str(source["sample_rate"]), str(source["channels"]), stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE, limit=READ_CHUNK_BYTES * 2, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)))
        try:
            process = await asyncio.shield(creation)
        except asyncio.CancelledError:
            # 建进程的短窗也要取得真实句柄，交给_run finally kill/wait。
            while not creation.done():
                try:
                    await asyncio.shield(creation)
                except asyncio.CancelledError:
                    continue
                except Exception:
                    break
            if not creation.cancelled():
                try:
                    playback.process = creation.result()
                except Exception as error:
                    _safe_log("decoder_creation_cancelled", error)
            raise
        playback.process = process
        feeder = asyncio.create_task(self._feed_source(process, source), name="device-media-source")
        sender = asyncio.create_task(self._send_pcm(process, playback, clock), name="device-media-pcm")
        diagnostics = asyncio.create_task(self._decoder_diagnostics(process), name="device-media-decode-trace")
        try:
            done, _ = await asyncio.wait((feeder, sender, diagnostics), return_when=asyncio.FIRST_COMPLETED)
            if diagnostics in done:
                await diagnostics
            # 下载错误必须立即停止；发送完毕时也检查下载结果及子进程真实退出码。
            if feeder in done:
                await feeder
                await sender
            else:
                limited = await sender
                if limited:
                    return
                await feeder
            code = await asyncio.wait_for(process.wait(), timeout=3)
            await diagnostics
            if code:
                raise MediaStreamError("受限音频解码子进程失败")
        finally:
            for task in (feeder, sender, diagnostics):
                if not task.done():
                    task.cancel()
            await asyncio.gather(feeder, sender, diagnostics, return_exceptions=True)
            await self._kill_decoder(playback)

    async def _decoder_diagnostics(self, process):
        output = bytearray()
        while True:
            chunk = await process.stderr.read(2048)
            if not chunk:
                break
            output.extend(chunk)
            if len(output) > 8192:
                raise MediaStreamError("解码器诊断输出超过容量")
        prefix = "MEDIA_DECODE_ERROR="
        for line in output.decode("utf-8", errors="replace").splitlines():
            if not line.startswith(prefix):
                continue
            try:
                diagnostic = json.loads(line[len(prefix):])
                frames = diagnostic.get("frames")
                error_type = diagnostic.get("error_type")
                if not isinstance(error_type, str) or not re.fullmatch(r"[A-Za-z0-9_]{1,128}", error_type) or not isinstance(frames, list) or len(frames) > 64:
                    continue
                formatted = []
                for frame in frames:
                    if not isinstance(frame, list) or len(frame) != 3 or not isinstance(frame[0], str) or type(frame[1]) is not int or not isinstance(frame[2], str) or len(frame[0]) > 512 or len(frame[2]) > 128 or any(ord(character) < 32 for character in frame[0] + frame[2]):
                        raise ValueError("诊断栈帧格式无效")
                    formatted.append('  File "' + frame[0] + '", line ' + str(frame[1]) + ", in " + frame[2])
                LOGGER.error("device_media_decoder_failed type=%s truncated=%s\n%s", error_type, diagnostic.get("truncated") is True, "\n".join(formatted))
            except (ValueError, TypeError, AttributeError) as error:
                # 非固定结构的FFmpeg文本一律丢弃，不回显第三方原文。
                _safe_log("decode_diagnostic_parse", error)
                continue

    async def _feed_source(self, process, source):
        total = 0
        try:
            if source["kind"] == "trusted_file":
                with source["path"].open("rb") as stream:
                    while True:
                        chunk = stream.read(READ_CHUNK_BYTES)
                        if not chunk:
                            break
                        total += len(chunk)
                        if total > MAX_SOURCE_BYTES:
                            raise MediaStreamError("媒体输入超过容量")
                        process.stdin.write(chunk)
                        await process.stdin.drain()
            else:
                timeout = aiohttp.ClientTimeout(total=MAX_TRACK_SECONDS, connect=5, sock_connect=5, sock_read=10)
                async with self.http.get(source["url"], headers=source["headers"], timeout=timeout, allow_redirects=False, auto_decompress=False, read_bufsize=READ_CHUNK_BYTES) as response:
                    if response.status != 200 or response.content_length is not None and response.content_length > MAX_SOURCE_BYTES:
                        raise MediaStreamError("媒体来源拒绝请求、重定向或超过容量")
                    encoding = response.headers.get("Content-Encoding", "identity").lower()
                    if encoding not in {"", "identity"}:
                        raise MediaStreamError("媒体来源返回不受支持的传输压缩")
                    async for chunk in response.content.iter_chunked(READ_CHUNK_BYTES):
                        total += len(chunk)
                        if total > MAX_SOURCE_BYTES:
                            raise MediaStreamError("媒体输入超过容量")
                        process.stdin.write(chunk)
                        await process.stdin.drain()
            if not total:
                raise MediaStreamError("媒体来源为空")
        finally:
            if process.stdin is not None:
                process.stdin.close()

    async def _send_pcm(self, process, playback, clock):
        buffer = bytearray()
        source = playback.source
        limit_samples = round(source.get("playback_limit_s", source["duration_s"]) * SAMPLE_RATE)
        while True:
            if playback.state != "playing" or not self._device_available(playback):
                raise MediaStreamError("设备不可播放")
            if self._voice_active():
                raise MediaStreamError("语音会话占用设备通道")
            if playback.sent_samples >= limit_samples:
                return True
            try:
                chunk = await asyncio.wait_for(process.stdout.read(FRAME_BYTES - len(buffer)), timeout=15)
            except asyncio.TimeoutError:
                raise MediaStreamError("媒体解码输出超时") from None
            if not chunk:
                if buffer:
                    if len(buffer) % 2:
                        raise MediaStreamError("解码输出不是完整PCM16样本")
                    remaining = (limit_samples - playback.sent_samples) * 2
                    await self._send_frame(playback, bytes(buffer[:remaining]), clock)
                return False
            buffer.extend(chunk)
            if len(buffer) == FRAME_BYTES:
                remaining = (limit_samples - playback.sent_samples) * 2
                await self._send_frame(playback, bytes(buffer[:remaining]), clock)
                buffer.clear()

    async def _send_frame(self, playback, frame, clock):
        loop = asyncio.get_running_loop()
        # ACK滞后超过3秒即失败；不把发包数当作设备已消费数。
        if loop.time() - playback.last_ack_time > 3:
            raise MediaStreamError("设备媒体进度回执超时")
        ack = playback.ack or {}
        if ack.get("queue_used", 0) >= max(1, ack.get("queue_capacity", 32) - 4):
            await asyncio.sleep(0.1)
        if loop.time() - clock[0] > 0.12:
            clock[0] = loop.time()
        delay = clock[0] - loop.time()
        if delay > 0:
            await asyncio.sleep(delay)
        if playback.state != "playing" or not self._device_available(playback) or self._voice_active():
            raise MediaStreamError("设备媒体已暂停或通道占用")
        await asyncio.wait_for(playback.device.send_bytes(frame), timeout=2)
        playback.sent_samples += len(frame) // 2
        if playback.sent_samples > SAMPLE_RATE * MAX_TRACK_SECONDS:
            raise MediaStreamError("音频输出超过30分钟")
        clock[0] += len(frame) / BYTES_PER_SECOND

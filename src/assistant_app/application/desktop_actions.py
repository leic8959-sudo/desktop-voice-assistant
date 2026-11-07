"""固定允许列表的 Windows 桌面动作与有界本地媒体播放。"""

from __future__ import annotations

import asyncio
import ctypes
import importlib.util
import json
import logging
import math
import os
import random
import struct
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

from ..integrations.token_refresh import TokenRefreshService
from .desktop_media import WindowsMediaPlayer

LOGGER = logging.getLogger("assistant.desktop_actions")
MAX_QUEUE = 8
MAX_MEDIA_BYTES = 10 * 1024 * 1024
MAX_PCM_BYTES = 90 * 1024 * 1024
MAX_DECODE_SECONDS = 1800
MAX_DECODE_WALL_SECONDS = 45
MEDIA_FORMATS = {".wav": "wav", ".mp3": "mp3", ".ogg": "ogg", ".flac": "flac", ".m4a": "mov"}
ALLOWED_APPS = {"calculator": ("calc.exe", "计算器"), "notepad": ("notepad.exe", "记事本"), "explorer": ("explorer.exe", "资源管理器")}
MEDIA_KEYS = {"play_pause": 0xB3, "next": 0xB0, "previous": 0xB1, "volume_up": 0xAF, "volume_down": 0xAE, "mute": 0xAD}
ACTION_LABELS = {"desktop_open_app": "打开应用", "desktop_media_key": "发送系统媒体键", "desktop_volume": "调节系统音量", "desktop_lock": "锁定电脑", "media_play": "播放本地音源", "media_pause": "暂停本地播放", "media_resume": "继续本地播放", "media_stop": "停止播放并清空队列", "media_queue": "加入播放队列", "media_clear_queue": "清空播放队列", "media_upload": "上传明确选择的本地音源", "oauth_refresh": "向提供商续期令牌"}
ACTION_LABELS["media_resume_podcast"] = "继续上次的播客"
VOICE_MEDIA_ACTIONS = {"media_play", "media_queue", "media_pause", "media_resume", "media_resume_podcast", "media_stop", "media_clear_queue"}
DIRECT_PC_ACTIONS = frozenset({"desktop_open_app", "desktop_media_key", "desktop_volume", "desktop_lock"})
NOISE_SOURCES = {"white_noise": "本地生成白噪音", "rain": "本地合成雨声"}


class DesktopActionsService:
    def __init__(self, config, store, runtime, http):
        self.config, self.store, self.runtime, self.http = config, store, runtime, http
        self.supported = sys.platform == "win32"
        self.directory = Path(config.get("runtime_dir", Path(__file__).resolve().parents[3] / "runtime"))
        self.media_root = (self.directory / "media").resolve()
        self.tokens = TokenRefreshService(config, http, self.directory)
        self.token_refresh = self.tokens
        self._lock = asyncio.Lock()
        self.upload_lock = asyncio.Lock()
        self._queue = []
        self._current = None
        self._state = "stopped"
        self._deadline = 0.0
        self._remaining = 0.0
        self._worker = None
        self._noise_paths = {}
        self._player = WindowsMediaPlayer()
        self._position_ms = 0
        self._duration_ms = 0
        self._last_podcast = None
        self._catalog = {}
        self._pending = None
        self._voice_guard_until = 0.0
        self._last_checkpoint = 0.0
        self.state_path = self.directory / "desktop-media-state.json"
        self._closed = True
        self._error = None
        self.runtime.media_service = self

    @property
    def tool_schemas(self):
        specifications = {
            "desktop_status": ("读取电脑与媒体状态", {}, []),
            "desktop_open_app": ("用户明确要求后直接打开允许列表应用，无需二次确认", {"app": {"type": "string", "enum": list(ALLOWED_APPS)}}, ["app"]),
            "desktop_media_key": ("用户明确要求后直接发送系统媒体键，无需二次确认", {"key": {"type": "string", "enum": list(MEDIA_KEYS)}}, ["key"]),
            "desktop_volume": ("用户明确要求后直接按Windows步长调节音量，无需二次确认", {"direction": {"type": "string", "enum": ["up", "down"]}, "steps": {"type": "integer", "minimum": 1, "maximum": 20}}, ["direction"]),
            "desktop_lock": ("用户明确要求后直接锁定电脑，无需二次确认", {}, []),
            "media_play": ("按用户明确指令在电脑播放：rain为本地雨声、white_noise为白噪音；音乐/播客先读desktop_status选择真实音源ID；minutes是停止计时，噪音循环、音乐播客单次播放。语音回答结束后才开始，不可把scheduled说成已播放", {"source": {"type": "string", "description": "rain、white_noise或desktop_status返回的用户自备音源ID，不能是URL"}, "minutes": {"type": "integer", "minimum": 1, "maximum": 240, "description": "多少分钟后停止，例如半小时=30；默认30"}}, ["source"]),
            "media_queue": ("按用户明确要求加入最多8项的持久队列，参数与media_play相同；不得自行加入推荐内容", {"source": {"type": "string"}, "minutes": {"type": "integer", "minimum": 1, "maximum": 240}}, ["source"]),
        }
        for name in ("media_pause", "media_resume", "media_resume_podcast", "media_stop", "media_clear_queue"):
            specifications[name] = (ACTION_LABELS[name] + "；仅在用户本轮明确要求时执行。继续上次播客恢复保存的实际位置，不重新从头播放", {}, [])
        return [{"type": "function", "name": name, "description": description, "parameters": {"type": "object", "properties": properties, "required": required, "additionalProperties": False}} for name, (description, properties, required) in specifications.items()]

    async def start(self):
        if not self._closed:
            return
        await self.tokens.start()
        await asyncio.to_thread(self._prune_temporary_files)
        await asyncio.to_thread(self._load_state)
        self._closed = False
        self._worker = asyncio.create_task(self._loop(), name="desktop-media-coordination")

    def _prune_temporary_files(self):
        # 崩溃残留的临时音源保留至多24小时；正式上传音源另有数量与总容量上限。
        cutoff = time.time() - 86400
        for directory, pattern in ((self.directory, "desktop-noise-*.wav"), (self.media_root, "*.upload"), (self.media_root, "*.pcm-temp"), (self.media_root, "*.decode-result")):
            if directory.exists():
                for path in directory.glob(pattern):
                    if path.is_file() and path.stat().st_mtime < cutoff:
                        path.unlink()

    async def close(self):
        self._closed = True
        if self._worker:
            self._worker.cancel()
            await asyncio.gather(self._worker, return_exceptions=True)
            self._worker = None
        async with self._lock:
            try:
                await self._checkpoint()
            finally:
                await self._silence()
            self._pending = None
            if self._current:
                self._state = "paused"
            await self._save_state()
        for path in self._noise_paths.values():
            await asyncio.to_thread(path.unlink, missing_ok=True)
        self._noise_paths.clear()

    def _active_voice(self):
        proactive = getattr(self.runtime, "proactive_speech", None)
        task = getattr(proactive, "active", None)
        music = getattr(self.runtime, "device_music", None)
        music_active = music is not None and music.stream.status().get("state") in {"starting", "playing"}
        return music_active or time.monotonic() < self._voice_guard_until or bool(task is not None and not task.done()) or any(not getattr(session, "closed", False) for session in tuple(getattr(self.runtime, "voice_sessions", ())))

    def sources(self):
        result = [{"id": identifier, "name": name, "kind": "noise"} for identifier, name in NOISE_SOURCES.items()]
        if self.media_root.exists():
            count = 0
            for path in self.media_root.iterdir():
                if path.name.endswith(".wav") and path.is_file() and not path.is_symlink() and len(path.stem) == 32:
                    metadata = self._catalog.get(path.stem, {})
                    result.append({"id": path.stem, "name": metadata.get("title", "上传音源 " + path.stem[:8]), "kind": metadata.get("kind", "user_audio")})
                    count += 1
                    if count >= 32:
                        break
        return result

    def status(self):
        remaining = max(0, self._deadline - time.monotonic()) if self._state == "playing" else self._remaining
        pending = {"action": self._pending["action"], "expires_in_s": max(0, round(self._pending["expires"] - time.monotonic())), "response_finished": self._pending["ready"]} if self._pending else None
        return {"available": self.supported, "started": not self._closed, "platform": sys.platform, "message": "Windows允许列表控制；媒体在服务所在电脑默认音频输出播放" if self.supported else "当前运行环境不是Windows，桌面与本地音频不可用", "active_voice": self._active_voice(), "allowed_apps": [{"id": key, "name": value[1]} for key, value in ALLOWED_APPS.items()], "media_keys": list(MEDIA_KEYS), "sources": self.sources(), "media": {"status": self._state, "current": self._current, "position_s": round(self._position_ms / 1000, 1), "duration_s": round(self._duration_ms / 1000, 1), "remaining_s": round(remaining), "queue": list(self._queue), "queue_limit": MAX_QUEUE, "last_podcast": self._last_podcast, "pending": pending, "error": self._error, "coordination": "唤醒/主动播报优先暂停；明确继续时恢复实际位置；语音播放等本轮回答和会话结束，最多等120秒"}, "oauth": [self.tokens.status(provider) for provider in ("google_calendar", "microsoft_calendar")], "instance_scope": "单实例；队列、音源分类和实际位置保存本机；重启后暂停，需明确继续；异常退出位置通常回退约5秒，磁盘或调度延迟可能更久"}

    def normalize(self, name, args):
        if not isinstance(args, dict):
            raise ValueError("动作参数必须为对象")
        expected = {"desktop_open_app": {"app"}, "desktop_media_key": {"key"}, "desktop_volume": {"direction", "steps"}, "media_play": {"source", "minutes"}, "media_queue": {"source", "minutes"}, "media_upload": {"filename", "size", "sha256", "title", "kind"}, "oauth_refresh": {"provider"}}
        if name not in ACTION_LABELS:
            raise KeyError(name)
        if set(args) - expected.get(name, set()):
            raise ValueError("动作包含不允许的参数")
        if name == "desktop_open_app":
            app = args.get("app")
            if app not in ALLOWED_APPS:
                raise ValueError("应用不在允许列表")
            return {"app": app}
        if name == "desktop_media_key":
            key = args.get("key")
            if key not in MEDIA_KEYS:
                raise ValueError("媒体键不在允许列表")
            return {"key": key}
        if name == "desktop_volume":
            steps = args.get("steps", 1)
            if args.get("direction") not in {"up", "down"} or type(steps) is not int or not 1 <= steps <= 20:
                raise ValueError("音量方向应为up/down，步数为1～20")
            return {"direction": args["direction"], "steps": steps}
        if name in {"media_play", "media_queue"}:
            source, minutes = args.get("source"), args.get("minutes", 30)
            if not isinstance(source, str) or source not in {item["id"] for item in self.sources()}:
                raise ValueError("音源必须是rain、white_noise或已上传的真实音源ID，请先读取desktop_status")
            if type(minutes) is not int or not 1 <= minutes <= 240:
                raise ValueError("定时播放需为1～240分钟")
            return {"source": source, "minutes": minutes}
        if name == "oauth_refresh":
            if args.get("provider") not in {"google_calendar", "microsoft_calendar"}:
                raise ValueError("不支持此续期提供商")
            return {"provider": args["provider"]}
        if name == "media_upload":
            import re
            filename, size, digest = args.get("filename"), args.get("size"), args.get("sha256")
            if not isinstance(filename, str) or not 1 <= len(filename) <= 120 or Path(filename).suffix.lower() not in MEDIA_FORMATS or type(size) is not int or not 44 <= size <= MAX_MEDIA_BYTES or not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest):
                raise ValueError("上传需为10MiB以内的WAV/MP3/OGG/FLAC/M4A，并提供有效SHA256摘要")
            if Path(filename).suffix.lower() != ".wav" and (getattr(sys, "frozen", False) or importlib.util.find_spec("av") is None):
                raise RuntimeError("当前环境缺少可用的PyAV解码环境；仍可上传受支持的PCM WAV")
            kind, title = args.get("kind", "music"), args.get("title", Path(filename).stem)
            if kind not in {"music", "podcast"} or not isinstance(title, str) or not title.strip() or len(title) > 100 or any(ord(character) < 32 for character in title):
                raise ValueError("音源分类需为music/podcast，名称需为1～100字符的可读文字")
            return {"filename": filename, "size": size, "sha256": digest, "kind": kind, "title": title.strip()}
        return {}

    def preview(self, name, args):
        parameters = self.normalize(name, args)
        return {"action": name, "label": ACTION_LABELS[name], "parameters": parameters, "available": self.supported if name != "oauth_refresh" else self.tokens.status(parameters["provider"])["available"], "active_voice": self._active_voice(), "effect": "电脑将锁屏，需在本机解锁" if name == "desktop_lock" else "在服务所在电脑请求打开" + ALLOWED_APPS[parameters["app"]][1] if name == "desktop_open_app" else "Windows全局媒体键可能作用于另一个播放器；系统不返回播放/音量读数" if name.startswith("desktop_") else "续期请求会发往官方提供商，保存轮换令牌；不代表日历权限已验证" if name == "oauth_refresh" else "只接收明确选定且摘要匹配的用户自备音源；压缩格式在本机转换为受限WAV，不自动播放" if name == "media_upload" else "声音位于电脑；噪音按定时循环，音乐和播客单次播放至结束或定时到期；继续恢复实际位置。语音优先暂停"}

    async def invoke(self, name, args, subject="voice", *, voice_authorized=False, voice_session=None):
        if name == "desktop_status":
            return self.status()
        preview = self.preview(name, args)
        # 授权只能来自父语音入口核验的本轮用户原话，不能来自模型参数或subject。
        if name in VOICE_MEDIA_ACTIONS | DIRECT_PC_ACTIONS and voice_authorized is True:
            return await self._execute(name, self.normalize(name, args), voice_session=voice_session)
        return {"status": "confirmation_required", "preview": preview, "message": "请在电脑与媒体控制台生成预览并点击确认；语音不能自行确认"}

    async def execute_confirmed(self, name, args, *, confirmation_verified=False):
        if confirmation_verified is not True:
            raise PermissionError("需要API消费有效的一次性管理员确认凭证")
        parameters = self.normalize(name, args)
        return await self._execute(name, parameters)

    async def execute_direct(self, name, args):
        # [人工决策-2026-11-04 23:24:24] 牛牛要求PC控制取消二次确认；仅直接执行既有四类允许动作，不扩展任意命令或账号操作。
        if name not in DIRECT_PC_ACTIONS:
            raise PermissionError("该操作仍需使用对应的授权流程")
        return await self._execute(name, self.normalize(name, args))

    async def _execute(self, name, parameters, voice_session=None):
        if name == "oauth_refresh":
            return await self.tokens.refresh(parameters["provider"])
        if not self.supported or self._closed:
            return {"status": "unsupported", "message": "Windows桌面服务当前不可用，动作没有执行"}
        async with self._lock:
            LOGGER.debug("desktop_action_start action=%s", name)
            if name in {"media_play", "media_resume", "media_resume_podcast", "media_pause", "media_stop"}:
                self._pending = None
            if (name in {"media_play", "media_resume", "media_resume_podcast"} or name == "media_queue" and self._current is None) and voice_session is not None:
                if name == "media_resume" and (not self._current or self._state not in {"paused", "paused_voice"}):
                    raise ValueError("没有可继续的暂停音源")
                if name == "media_resume_podcast" and (not self._last_podcast or self._last_podcast.get("completed")):
                    raise ValueError("没有未播完的历史播客，请先上传并播放标为播客的音源")
                self._pending = {"action": "media_play" if name == "media_queue" else name, "parameters": dict(parameters), "session": voice_session, "ready": False, "expires": time.monotonic() + 120}
                return {"status": "scheduled", "message": "已登记播放请求，等待本轮回答和语音会话结束；最多等待120秒，目前尚未开始播放"}
            if name == "desktop_open_app":
                result = await asyncio.to_thread(self._open_app, parameters["app"])
            elif name == "desktop_lock":
                result = await asyncio.to_thread(self._lock_windows)
            elif name in {"desktop_media_key", "desktop_volume"}:
                key = parameters.get("key") or ("volume_up" if parameters["direction"] == "up" else "volume_down")
                await asyncio.to_thread(self._send_key, key, parameters.get("steps", 1))
                result = {"status": "sent", "message": "已发送系统媒体键；没有播放器或音量读数，实际结果需在电脑确认"}
            elif name == "media_play":
                result = await self._begin(parameters)
            elif name == "media_queue":
                if len(self._queue) >= MAX_QUEUE:
                    raise ValueError("播放队列已满（最多8项）")
                if self._current is None:
                    result = await self._begin(parameters)
                else:
                    self._queue.append(parameters)
                    result = {"status": "queued", "message": "已加入队列；当前音源定时结束后依次播放"}
            elif name == "media_pause":
                await self._pause("paused")
                result = {"status": self._state, "message": "本地音频已暂停；已保存实际位置，继续时从该位置恢复"}
            elif name == "media_resume":
                if self._state not in {"paused", "paused_voice"} or not self._current:
                    raise ValueError("没有可继续的暂停音源")
                result = await self._begin(self._current, remaining=self._remaining, position_ms=self._position_ms)
            elif name == "media_resume_podcast":
                result = await self._resume_podcast()
            elif name == "media_stop":
                await self._checkpoint()
                await self._silence()
                self._current, self._state, self._remaining = None, "stopped", 0
                self._queue.clear()
                result = {"status": "stopped", "message": "本地播放已停止，队列已清空"}
            else:
                self._queue.clear()
                result = {"status": "cleared", "message": "队列已清空"}
            if name in VOICE_MEDIA_ACTIONS:
                await self._save_state()
            LOGGER.debug("desktop_action_end action=%s status=%s", name, result.get("status"))
            return result

    @staticmethod
    def _open_app(app):
        buffer = ctypes.create_unicode_buffer(32768)
        if app == "explorer":
            length = ctypes.windll.kernel32.GetWindowsDirectoryW(buffer, len(buffer))
        else:
            length = ctypes.windll.kernel32.GetSystemDirectoryW(buffer, len(buffer))
        if not length or length >= len(buffer):
            raise RuntimeError("无法读取Windows系统目录")
        path = str(Path(buffer.value) / ALLOWED_APPS[app][0])
        shell = ctypes.windll.shell32.ShellExecuteW
        shell.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_int]
        shell.restype = ctypes.c_void_p
        if (shell(None, "open", path, None, None, 1) or 0) <= 32:
            raise RuntimeError("Windows拒绝打开允许列表应用")
        return {"status": "requested", "message": "Windows已接受打开" + ALLOWED_APPS[app][1] + "的请求；未读取窗口状态"}

    @staticmethod
    def _send_key(key, steps):
        for _ in range(steps):
            ctypes.windll.user32.keybd_event(MEDIA_KEYS[key], 0, 0, 0)
            ctypes.windll.user32.keybd_event(MEDIA_KEYS[key], 0, 2, 0)

    @staticmethod
    def _lock_windows():
        if not ctypes.windll.user32.LockWorkStation():
            raise RuntimeError("Windows拒绝锁屏请求")
        return {"status": "requested", "message": "Windows已接受锁屏请求"}

    @staticmethod
    def validate_wav(path):
        if path.stat().st_size > MAX_PCM_BYTES + 44:
            raise ValueError("PCM输出超过90MiB限制")
        try:
            with wave.open(str(path), "rb") as audio:
                if audio.getcomptype() != "NONE" or audio.getnchannels() not in {1, 2} or audio.getsampwidth() != 2 or not 8000 <= audio.getframerate() <= 48000 or not 0 < audio.getnframes() / audio.getframerate() <= MAX_DECODE_SECONDS:
                    raise ValueError("仅支持1～2声道、16bit PCM、8～48kHz、最长30分钟WAV")
                expected = audio.getnframes() * audio.getnchannels() * audio.getsampwidth()
                if expected > path.stat().st_size:
                    raise ValueError("WAV帧长度无效")
        except (wave.Error, EOFError) as error:
            raise ValueError("WAV文件无效") from error

    async def convert_upload_async(self, source, output, extension):
        # HTTP取消不代表线程/子进程退出；等真实退出后外层才可删除Windows占用文件。
        conversion = asyncio.create_task(asyncio.to_thread(self.convert_upload, source, output, extension))
        try:
            return await asyncio.shield(conversion)
        except asyncio.CancelledError:
            while not conversion.done():
                try:
                    await asyncio.shield(conversion)
                except asyncio.CancelledError:
                    continue
                except Exception:
                    break
            try:
                conversion.result()
            except Exception:
                LOGGER.exception("cancelled_media_conversion_failed")
            raise

    def convert_upload(self, source, output, extension):
        # 固定模块与参数启动隔离解码进程；无shell，无用户命令入口，超时会终止进程。
        result_path = output.with_suffix(".decode-result")
        try:
            subprocess.run([sys.executable, "-m", "src.assistant_app.application.desktop_actions", "--decode-upload", str(source), str(output), extension, str(result_path)], cwd=Path(__file__).resolve().parents[3], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, shell=False, timeout=MAX_DECODE_WALL_SECONDS, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0), check=False)
            if not result_path.exists() or result_path.stat().st_size > 65536:
                raise RuntimeError("本机解码进程未返回有效结果，音源没有保存")
            result = json.loads(result_path.read_text(encoding="utf-8"))
            if isinstance(result, dict) and result.get("diagnostic"):
                LOGGER.error("local_media_decode_worker_failed traceback=%s", result.pop("diagnostic"))
            if not isinstance(result, dict) or result.get("status") != "decoded":
                raise RuntimeError(result.get("message", "音源解码失败") if isinstance(result, dict) else "音源解码失败")
            self.validate_wav(output)
            return result
        except subprocess.TimeoutExpired as error:
            LOGGER.exception("local_media_decode_timeout")
            raise RuntimeError("本机音源转换超过45秒，已终止解码，音源没有保存") from error
        except Exception:
            LOGGER.exception("local_media_decode_failed")
            raise
        finally:
            result_path.unlink(missing_ok=True)

    def _noise(self, source):
        self.directory.mkdir(parents=True, exist_ok=True)
        descriptor, filename = tempfile.mkstemp(prefix="desktop-noise-", suffix=".wav", dir=self.directory)
        os.close(descriptor)
        path = Path(filename)
        generator = random.Random()
        frames = bytearray()
        filtered, drop = 0.0, 0.0
        length = 24000 * 8
        for index in range(length):
            noise = generator.uniform(-1, 1)
            if source == "rain":
                # 本机合成：带宽受限的雨幕叠加稀疏水滴，不访问任何网络音源。
                filtered = filtered * 0.86 + noise * 0.14
                drop = drop * 0.985 + (generator.uniform(0.12, 0.4) if generator.random() < 0.001 else 0)
                sample = (filtered * 6000 + math.sin(index * 0.72) * drop * 1100)
            else:
                sample = noise * 2500
            fade = min(1, index / 480, (length - index - 1) / 480)
            frames.extend(struct.pack("<h", max(-32768, min(32767, int(sample * fade)))))
        try:
            with wave.open(str(path), "wb") as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(24000)
                audio.writeframes(frames)
        except Exception:
            path.unlink(missing_ok=True)
            raise
        return path

    def _source_path(self, source):
        candidate = self.media_root / (source + ".wav")
        path = candidate.resolve()
        if path.parent != self.media_root or candidate.is_symlink() or not path.is_file():
            raise ValueError("上传音源不存在或路径不安全")
        self.validate_wav(path)
        return path

    async def _begin(self, item, remaining=None, position_ms=0):
        if self._active_voice():
            return {"status": "blocked", "message": "语音会话正在使用音频，媒体未开始；请结束会话后重新确认"}
        if item["source"] in NOISE_SOURCES:
            if item["source"] not in self._noise_paths:
                self._noise_paths[item["source"]] = await asyncio.to_thread(self._noise, item["source"])
            path = self._noise_paths[item["source"]]
        else:
            path = await asyncio.to_thread(self._source_path, item["source"])
        # 生成/校验期间可能开始语音；复查后才触碰Windows音轨。
        if self._active_voice():
            return {"status": "blocked", "message": "音源准备期间开始了语音，媒体没有开始播放"}
        await self._checkpoint()
        if remaining is not None and remaining <= 0:
            raise ValueError("播放计时已结束，没有可继续的剩余时长")
        if item["source"] in NOISE_SOURCES and self._duration_ms and position_ms >= self._duration_ms:
            position_ms = 0
        try:
            self._duration_ms = await self._audio_call(self._player.open, path, int(position_ms))
        except BaseException:
            LOGGER.exception("desktop_media_start_failed")
            await self._silence()
            self._state = "paused" if self._current else "error"
            self._error = "Windows未能开始播放；音源没有继续输出，请核对音源与音频设备"
            await self._save_state()
            raise
        self._current, self._state = dict(item), "playing"
        self._position_ms = int(position_ms)
        self._remaining = float(remaining if remaining is not None else item["minutes"] * 60)
        self._deadline = time.monotonic() + self._remaining
        self._error = None
        await self._checkpoint()
        return {"status": "playing", "position_s": round(self._position_ms / 1000, 1), "message": "Windows音轨报告正在播放；已从实际位置开始，声音来自电脑默认输出，未做扬声器物理确认"}

    async def _silence(self):
        if self.supported:
            await self._audio_call(self._player.close)

    async def _audio_call(self, method, *arguments):
        operation = asyncio.create_task(asyncio.to_thread(method, *arguments))
        try:
            return await asyncio.shield(operation)
        except asyncio.CancelledError:
            # MCI打开/关闭必须真实退出，取消请求不能遗留未登记的后台播放。
            while not operation.done():
                try:
                    await asyncio.shield(operation)
                except asyncio.CancelledError:
                    continue
                except Exception:
                    break
            try:
                operation.result()
            except Exception:
                LOGGER.exception("cancelled_windows_audio_operation_failed")
            raise

    async def _pause(self, state):
        if self._state == "playing":
            self._remaining = max(0, self._deadline - time.monotonic())
            try:
                await self._checkpoint()
            finally:
                await self._silence()
                self._state = state
            await self._save_state()

    async def before_voice_session(self):
        # 父入口可持runtime.voice_lock调用；此处只取媒体锁，禁止反向取voice_lock。
        async with self._lock:
            self._voice_guard_until = time.monotonic() + 2
            await self._pause("paused_voice")

    async def voice_response_finished(self, session):
        async with self._lock:
            if not self._pending or self._pending["session"] is not session or time.monotonic() >= self._pending["expires"]:
                return False
            self._pending["ready"] = True
            return True

    async def cancel_voice_request(self, session):
        async with self._lock:
            if self._pending and self._pending["session"] is session:
                self._pending = None

    async def _resume_podcast(self):
        bookmark = self._last_podcast
        if not bookmark or bookmark.get("completed"):
            raise ValueError("没有未播完的历史播客，请先上传并播放标为播客的音源")
        # 副本避免_begin检查点更新当前播客时覆盖待恢复书签。
        bookmark = dict(bookmark)
        return await self._begin({"source": bookmark["source"], "minutes": bookmark["minutes"]}, remaining=bookmark["remaining_s"] or bookmark["minutes"] * 60, position_ms=bookmark["position_ms"])

    async def _checkpoint(self):
        if self._state == "playing" and self._player.opened:
            position = await self._audio_call(self._player.position)
            self._position_ms = position
            self._remaining = max(0, self._deadline - time.monotonic())
        if self._current and self._catalog.get(self._current["source"], {}).get("kind") == "podcast":
            self._last_podcast = {**self._current, "position_ms": self._position_ms, "remaining_s": self._remaining, "completed": self._duration_ms > 0 and self._position_ms >= self._duration_ms}

    async def register_upload(self, source, metadata):
        async with self._lock:
            if source not in self._catalog and len(self._catalog) >= 32:
                raise ValueError("音源元数据已达32项上限，请由管理员维护媒体目录")
            previous = self._catalog.get(source)
            self._catalog[source] = {"title": metadata["title"], "kind": metadata["kind"]}
            try:
                await self._save_state()
            except BaseException:
                if previous is None:
                    self._catalog.pop(source, None)
                else:
                    self._catalog[source] = previous
                raise

    async def _save_state(self):
        remaining = max(0, self._deadline - time.monotonic()) if self._state == "playing" else self._remaining
        data = {"version": 1, "catalog": self._catalog, "queue": self._queue, "current": self._current, "remaining_s": remaining, "position_ms": self._position_ms, "duration_ms": self._duration_ms, "last_podcast": self._last_podcast}
        saving = asyncio.create_task(asyncio.to_thread(self._write_state, data))
        try:
            await asyncio.shield(saving)
        except asyncio.CancelledError:
            # 与上传转换一致：取消后等真实写线程退出，再释放媒体锁。
            while not saving.done():
                try:
                    await asyncio.shield(saving)
                except asyncio.CancelledError:
                    continue
                except Exception:
                    break
            try:
                saving.result()
            except Exception:
                LOGGER.exception("cancelled_media_state_write_failed")
            raise
        self._last_checkpoint = time.monotonic()

    def _write_state(self, data):
        self.directory.mkdir(parents=True, exist_ok=True)
        temporary = self.state_path.with_suffix(".json.tmp")
        try:
            with temporary.open("w", encoding="utf-8") as stream:
                json.dump(data, stream, ensure_ascii=False, allow_nan=False)
                stream.flush()
                os.fsync(stream.fileno())
            temporary.replace(self.state_path)
        finally:
            temporary.unlink(missing_ok=True)

    def _load_state(self):
        if not self.state_path.exists():
            return
        try:
            if self.state_path.stat().st_size > 65536:
                raise ValueError("媒体状态文件超过64KiB")
            data = json.loads(self.state_path.read_text(encoding="utf-8"))
            if not isinstance(data, dict) or data.get("version") != 1:
                raise ValueError("媒体状态版本无效")
            catalog = data.get("catalog", {})
            if not isinstance(catalog, dict) or len(catalog) > 32:
                raise ValueError("音源元数据超限")
            import re
            for source, item in catalog.items():
                if not re.fullmatch(r"[a-f0-9]{32}", source) or not isinstance(item, dict) or item.get("kind") not in {"music", "podcast"} or not isinstance(item.get("title"), str) or not 1 <= len(item["title"]) <= 100:
                    raise ValueError("音源元数据无效")
            self._catalog = catalog
            queue = data.get("queue", [])
            if not isinstance(queue, list) or len(queue) > MAX_QUEUE:
                raise ValueError("持久队列超限")
            loaded_queue = [self.normalize("media_queue", item) for item in queue]
            current = self.normalize("media_play", data["current"]) if data.get("current") else None
            remaining, position, duration = data.get("remaining_s", 0), data.get("position_ms", 0), data.get("duration_ms", 0)
            if type(remaining) not in {float, int} or not math.isfinite(remaining) or not 0 <= remaining <= 14400 or type(position) is not int or type(duration) is not int or not 0 <= position <= duration <= MAX_DECODE_SECONDS * 1000:
                raise ValueError("媒体恢复位置无效")
            bookmark = data.get("last_podcast")
            if bookmark is not None:
                if not isinstance(bookmark, dict):
                    raise ValueError("播客书签无效")
                self.normalize("media_play", {"source": bookmark.get("source"), "minutes": bookmark.get("minutes")})
                if self._catalog.get(bookmark["source"], {}).get("kind") != "podcast" or type(bookmark.get("position_ms")) is not int or not 0 <= bookmark["position_ms"] <= MAX_DECODE_SECONDS * 1000 or type(bookmark.get("remaining_s")) not in {float, int} or not math.isfinite(bookmark["remaining_s"]) or not 0 <= bookmark["remaining_s"] <= 14400 or type(bookmark.get("completed")) is not bool:
                    raise ValueError("播客书签字段无效")
            self._queue, self._current, self._remaining = loaded_queue, current, remaining
            self._position_ms, self._duration_ms, self._last_podcast = position, duration, bookmark
            self._state = "paused" if current else "stopped"
        except Exception:
            LOGGER.exception("desktop_media_state_load_failed")
            self._error = "媒体恢复文件无效，未恢复播放；请检查本机状态文件"

    async def _loop(self):
        while not self._closed:
            try:
                async with self._lock:
                    pending = self._pending
                    if pending and time.monotonic() >= pending["expires"]:
                        self._pending = None
                        self._error = "等待语音结束的播放请求已超时取消（120秒），没有开始播放"
                    elif pending and pending["ready"] and getattr(pending["session"], "closed", False) and not self._active_voice():
                        self._pending = None
                        if pending["action"] == "media_resume_podcast":
                            await self._resume_podcast()
                        elif pending["action"] == "media_resume":
                            await self._begin(self._current, remaining=self._remaining, position_ms=self._position_ms)
                        else:
                            await self._begin(pending["parameters"])
                        await self._save_state()
                    if self._state == "playing" and self._active_voice():
                        await self._pause("paused_voice")
                        LOGGER.debug("desktop_media_paused_for_voice")
                    elif self._state == "playing":
                        mode = await self._audio_call(self._player.mode)
                        ended = mode == "stopped"
                        if ended and self._current["source"] in NOISE_SOURCES and time.monotonic() < self._deadline:
                            await self._audio_call(self._player.restart)
                            ended = False
                        if not ended and time.monotonic() < self._deadline:
                            if time.monotonic() - self._last_checkpoint >= 5:
                                await self._checkpoint()
                                await self._save_state()
                            continue
                        await self._checkpoint()
                        if ended and self._last_podcast and self._current["source"] == self._last_podcast["source"]:
                            self._last_podcast["completed"] = True
                        await self._silence()
                        self._state, self._current, self._remaining = "stopped", None, 0
                        if self._queue:
                            item = self._queue[0]
                            result = await self._begin(item)
                            if result["status"] == "playing":
                                self._queue.pop(0)
                            elif result["status"] == "blocked":
                                self._current, self._state = dict(item), "paused_voice"
                                self._remaining = item["minutes"] * 60
                                self._position_ms, self._duration_ms = 0, 0
                                self._queue.pop(0)
                        await self._save_state()
            except asyncio.CancelledError:
                raise
            except Exception:
                LOGGER.exception("desktop_media_coordination_failed")
                async with self._lock:
                    self._state, self._error = "error", "本地播放失败，请检查音源与Windows音频输出"
                    try:
                        await self._silence()
                    except Exception:
                        LOGGER.exception("desktop_media_emergency_stop_failed")
            finally:
                await asyncio.sleep(0.2)


def _decode_uploaded_audio(source, output, extension):
    # 此函数仅在固定解码子进程内调用；不接受URL，所有二次媒体IO一律拒绝。
    try:
        import av
    except ImportError as error:
        raise RuntimeError("当前环境缺少可用PyAV，不能转换压缩音源；仍可使用PCM WAV") from error
    if extension not in MEDIA_FORMATS or extension == ".wav":
        raise ValueError("不支持的音源解码格式")
    if source.stat().st_size > MAX_MEDIA_BYTES:
        raise ValueError("原始音源超过10MiB")
    av.logging.set_level(av.logging.PANIC)
    started = time.monotonic()
    total_samples, total_packets, total_frames = 0, 0, 0
    allowed_codecs = {".mp3": {"mp3", "mp3float"}, ".ogg": {"vorbis", "opus", "flac"}, ".flac": {"flac"}, ".m4a": {"aac", "alac", "mp3", "mp3float"}}

    def reject_secondary_io(*_arguments):
        raise PermissionError("禁止音源打开任何额外本地文件或网络资源")

    def check_frame(frame):
        if time.monotonic() - started > MAX_DECODE_WALL_SECONDS or not 8000 <= frame.sample_rate <= 48000 or not 1 <= len(frame.layout.channels) <= 2 or not 0 < frame.samples <= 262144:
            raise ValueError("音源解码超过时间、采样率、声道或帧大小限制")

    with source.open("rb") as raw:
        options = {"protocol_whitelist": "file,pipe", "format_whitelist": MEDIA_FORMATS[extension], "enable_drefs": "0", "use_absolute_path": "0", "probesize": "1048576", "analyzeduration": "5000000", "max_streams": "4"}
        with av.open(raw, mode="r", format=MEDIA_FORMATS[extension], options=options, io_open=reject_secondary_io) as container:
            streams = list(container.streams.audio)
            if len(streams) != 1 or len(container.streams) > 4:
                raise ValueError("音源必须仅包含一条音轨，且总流数量不能超过4")
            audio = streams[0]
            context = audio.codec_context
            if context.name not in allowed_codecs[extension]:
                raise ValueError("音源编码与允许格式不匹配")
            if not 8000 <= context.sample_rate <= 48000 or not 1 <= len(context.layout.channels) <= 2:
                raise ValueError("原音源只支持8～48kHz、1～2声道")
            context.thread_count = 1
            if audio.duration is not None and audio.time_base is not None and audio.duration * audio.time_base > MAX_DECODE_SECONDS:
                raise ValueError("音源超过30分钟解码上限")
            resampler = av.AudioResampler(format="s16", layout="mono", rate=24000)
            with wave.open(str(output), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(24000)

                def write_converted(frame):
                    nonlocal total_samples
                    if not 0 < frame.samples <= 262144:
                        raise ValueError("转换后的音频帧超限")
                    count = frame.samples * 2
                    total_samples += frame.samples
                    if total_samples > 24000 * MAX_DECODE_SECONDS or total_samples * 2 > MAX_PCM_BYTES:
                        raise ValueError("解码音频超过30分钟或90MiB输出限制")
                    wav.writeframesraw(bytes(frame.planes[0])[:count])

                for packet in container.demux(audio):
                    total_packets += 1
                    if total_packets > 200000 or packet.size > 1024 * 1024 or time.monotonic() - started > MAX_DECODE_WALL_SECONDS:
                        raise ValueError("音源包数量、单包大小或转换时间超限")
                    for frame in packet.decode():
                        total_frames += 1
                        if total_frames > 200000:
                            raise ValueError("解码音频帧数量超限")
                        check_frame(frame)
                        for converted in resampler.resample(frame):
                            write_converted(converted)
                for converted in resampler.resample(None):
                    write_converted(converted)
                if total_samples == 0:
                    raise ValueError("音源没有可解码的音频样本")
    return {"status": "decoded", "duration_s": round(total_samples / 24000, 3), "sample_rate": 24000, "channels": 1, "pcm_bytes": total_samples * 2}


if __name__ == "__main__":
    # 固定模块入口仅用于管理员已确认的本地上传转换；不读取配置或启动服务。
    if len(sys.argv) != 6 or sys.argv[1] != "--decode-upload":
        raise SystemExit(2)
    source_path, output_path, suffix, result_path = Path(sys.argv[2]), Path(sys.argv[3]), sys.argv[4], Path(sys.argv[5])
    try:
        decoded = _decode_uploaded_audio(source_path, output_path, suffix)
    except Exception as error:
        import traceback
        output_path.unlink(missing_ok=True)
        decoded = {"status": "unavailable" if isinstance(error, RuntimeError) else "failed", "message": str(error) if isinstance(error, (RuntimeError, ValueError, PermissionError)) else "音源无法转换；请核对格式、编码、时长及PyAV环境", "diagnostic": traceback.format_exc()}
    result_path.write_text(json.dumps(decoded, ensure_ascii=False), encoding="utf-8")

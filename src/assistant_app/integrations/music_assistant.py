"""Music Assistant 2.10.9 正式命令适配；不解析 QQ 密码或绕过订阅授权。"""

from __future__ import annotations

import asyncio
import ipaddress
import json
import logging
import re
import sys
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

import aiohttp

from gmail_service import DpapiTokenVault

LOGGER = logging.getLogger("assistant.music_assistant")
PLAYER_ID = "xiaoyanzi_gateway"
CONFIG_FIELDS = {"base_url", "stream_base_url", "access_token"}
MAX_RESPONSE_BYTES = 1024 * 1024


class MusicAssistantError(RuntimeError):
    """安全、可直接向管理员显示的适配错误。"""


def _base_url(value):
    if not isinstance(value, str) or len(value) > 1024:
        raise ValueError("Music Assistant 地址格式无效")
    parsed = urlsplit(value)
    if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in {"", "/"}:
        raise ValueError("地址必须为不带路径、凭据或查询参数的 HTTP(S) 根地址")
    try:
        parsed.port
    except ValueError as exc:
        raise ValueError("Music Assistant 端口无效") from exc
    try:
        local = parsed.hostname == "localhost" or ipaddress.ip_address(parsed.hostname).is_loopback
    except ValueError:
        local = False
    if parsed.scheme == "http" and not local:
        raise ValueError("明文 HTTP 只允许本机 loopback；远程 Music Assistant 必须使用 HTTPS")
    return value.rstrip("/")


class MusicAssistantClient:
    def __init__(self, config, http, vault=None):
        self.config, self.http = config, http
        self.path = Path(config.get("runtime_dir", "runtime")) / "music-assistant.dpapi"
        self.vault = vault
        self.available = sys.platform == "win32" or vault is not None
        self._lock = asyncio.Lock()
        self._play_lock = asyncio.Lock()
        self._storage_error = False
        self._lease = ""
        self._last_check = "not_checked"

    def _vault(self):
        if not self.available:
            raise MusicAssistantError("当前环境没有 Windows DPAPI，不能安全保存音乐服务令牌")
        if self.vault is None:
            self.vault = DpapiTokenVault(self.path)
        return self.vault

    async def start(self):
        if not self.available or (self.vault is None and not self.path.exists()):
            return
        try:
            if self.path.exists() and self.path.stat().st_size > 64 * 1024:
                raise ValueError("音乐安全存储超过上限")
            values = await asyncio.to_thread(self._vault().load) or {}
            if not isinstance(values, dict) or set(values) - CONFIG_FIELDS:
                raise ValueError("音乐安全存储格式无效")
            overrides = self._normalize(values)
            if values.get("access_token") == "":
                overrides["access_token"] = ""
            for field, value in overrides.items():
                key = "music_assistant_" + field
                if self.config.get("_config_sources", {}).get(key) != "environment":
                    self.config[key] = value
        except Exception:
            LOGGER.exception("music_secure_storage_load_failed")
            self._storage_error = True

    async def close(self):
        # 退出前释放本客户端持有的队列；不关闭父应用共享 HTTP session。
        if self._lease:
            try:
                await self.stop()
            except Exception:
                LOGGER.exception("music_queue_release_on_close_failed")

    def status(self):
        managed = any(self.config.get("_config_sources", {}).get("music_assistant_" + field) == "environment" for field in CONFIG_FIELDS)
        base = self.config.get("music_assistant_base_url", "")
        stream = self.config.get("music_assistant_stream_base_url", "http://127.0.0.1:8097")
        configured = bool(base and self.config.get("music_assistant_access_token"))
        return {"provider": "music_assistant", "version": "2.10.9", "configured": configured, "available": self.available and not self._storage_error and not managed, "storage_error": self._storage_error, "environment_managed": managed, "base_url": base, "stream_base_url": stream, "token_configured": bool(self.config.get("music_assistant_access_token")), "player_id": PLAYER_ID, "connection": self._last_check, "message": "安全存储无法读取，请由管理员修复后重启；不会覆盖损坏文件" if self._storage_error else "配置受环境管理，后台不可覆盖" if managed else "已配置，搜索时才读取 Music Assistant；尚未核验音源和实体喇叭" if configured else "请先部署 Music Assistant，在其页面完成 QQ 授权，再保存地址和访问令牌"}

    @staticmethod
    def _normalize(values):
        result = {}
        for field, value in values.items():
            if field not in CONFIG_FIELDS or not isinstance(value, str):
                raise ValueError("不支持的音乐配置字段")
            if field == "access_token":
                if not value:
                    continue
                if not re.fullmatch(r"[^\s\r\n]{8,8192}", value) or value.upper().startswith("REPLACE"):
                    raise ValueError("Music Assistant 访问令牌格式无效")
                result[field] = value
            else:
                result[field] = _base_url(value)
        return result

    async def configure(self, payload):
        if not self.status()["available"]:
            raise MusicAssistantError("音乐配置不可编辑：安全存储不可用或由环境变量管理")
        if not isinstance(payload, dict) or set(payload) - (CONFIG_FIELDS | {"clear_token"}):
            raise ValueError("音乐配置仅支持地址、流地址、访问令牌和清除令牌")
        if "clear_token" in payload and type(payload["clear_token"]) is not bool:
            raise ValueError("clear_token 必须为布尔值")
        values = self._normalize({field: value for field, value in payload.items() if field in CONFIG_FIELDS})
        if payload.get("clear_token"):
            if payload.get("access_token"):
                raise ValueError("替换令牌和清除令牌不能同时使用")
            values["access_token"] = ""
        async with self._lock:
            if self._lease:
                raise ValueError("请先停止当前音乐，再修改连接配置")
            vault = self._vault()
            saved = await asyncio.to_thread(vault.load) or {}
            if not isinstance(saved, dict) or set(saved) - CONFIG_FIELDS:
                raise MusicAssistantError("音乐安全存储格式无效，未覆盖旧文件")
            saved.update(values)
            await asyncio.to_thread(vault.save, saved)
            self.config.update({"music_assistant_" + field: value for field, value in values.items()})
            self._last_check = "not_checked"
        return self.status()

    async def _command(self, command, arguments):
        if self._storage_error:
            raise MusicAssistantError("音乐安全存储无法读取，已停用连接，请先由管理员修复")
        base = _base_url(self.config.get("music_assistant_base_url", ""))
        token = self.config.get("music_assistant_access_token", "")
        if not token:
            raise MusicAssistantError("尚未配置 Music Assistant 访问令牌")
        try:
            async with self.http.post(base + "/api", json={"command": command, "args": arguments}, headers={"Authorization": "Bearer " + token}, timeout=aiohttp.ClientTimeout(total=40, connect=5, sock_read=35), allow_redirects=False, proxy=None) as response:
                if response.status in {401, 403}:
                    raise MusicAssistantError("Music Assistant 令牌失效或缺少音乐/播放器权限，请在其页面重新授权")
                if response.status != 200:
                    raise MusicAssistantError("Music Assistant 命令失败；请核查服务日志、QQ 授权和伊迪斯播放器扩展")
                raw = bytearray()
                async for chunk in response.content.iter_chunked(16384):
                    raw.extend(chunk)
                    if len(raw) > MAX_RESPONSE_BYTES:
                        raise MusicAssistantError("Music Assistant 返回内容超过上限")
                try:
                    result = json.loads(raw)
                except (ValueError, UnicodeDecodeError) as exc:
                    raise MusicAssistantError("Music Assistant 没有返回有效 JSON") from exc
                self._last_check = "reachable"
                return result
        except MusicAssistantError:
            self._last_check = "failed"
            raise
        except (aiohttp.ClientError, asyncio.TimeoutError) as exc:
            self._last_check = "failed"
            # 不记录携带 Bearer 的请求对象或 stream URL。
            LOGGER.exception("music_assistant_request_failed command=%s", command)
            raise MusicAssistantError("无法连接 Music Assistant；请先确认本机服务已启动") from exc

    async def search(self, query, limit=5):
        if not isinstance(query, str) or not 1 <= len(query.strip()) <= 200:
            raise ValueError("请输入1～200字的歌名或歌手")
        if type(limit) is not int or not 1 <= limit <= 10:
            raise ValueError("候选数量应为1～10")
        result = await self._command("music/search", {"search_query": query.strip(), "media_types": ["track"], "limit": limit, "library_only": False})
        if not isinstance(result, dict) or not isinstance(result.get("tracks"), list):
            raise MusicAssistantError("Music Assistant 搜索返回格式不兼容")
        items = []
        for track in result["tracks"][:limit]:
            if not isinstance(track, dict):
                continue
            uri, provider, item_id = track.get("uri"), track.get("provider"), track.get("item_id")
            if not all(isinstance(value, str) and 0 < len(value) <= 1024 for value in (uri, provider, item_id)) or "://track/" not in uri:
                continue
            duration = track.get("duration")
            duration = int(duration) if type(duration) in {int, float} and 0 < duration <= 1800 else None
            artists = track.get("artists", [])
            album = track.get("album")
            mappings = track.get("provider_mappings", [])
            available = track.get("available") is not False and (not isinstance(mappings, list) or not mappings or any(isinstance(mapping, dict) and mapping.get("available") is not False for mapping in mappings))
            items.append({"ref": {"uri": uri, "provider": provider, "item_id": item_id}, "title": str(track.get("name", "未知歌曲"))[:200], "artists": [str(artist.get("name", ""))[:100] for artist in artists[:8] if isinstance(artist, dict)] if isinstance(artists, list) else [], "album": str(album.get("name", ""))[:200] if isinstance(album, dict) else "", "duration_s": duration, "available": available, "availability_note": "账号权限和完整音频在实际播放时由 Music Assistant 验证"})
        return {"items": items}

    async def resolve_stream(self, track_ref, position_s=0):
        if not isinstance(track_ref, dict) or not isinstance(track_ref.get("uri"), str) or "://track/" not in track_ref["uri"]:
            raise ValueError("歌曲选择无效，请重新搜索")
        if type(position_s) not in {int, float} or not 0 <= position_s <= 1800:
            raise ValueError("恢复位置超出30分钟限制")
        async with self._play_lock:
            result = await self._command("xiaoyanzi/resolve", {"uri": track_ref["uri"], "position_s": int(position_s)})
            if not isinstance(result, dict) or not isinstance(result.get("url"), str) or not isinstance(result.get("lease"), str):
                raise MusicAssistantError("伊迪斯播放器扩展未返回可播放音频流，请检查音乐扩展是否已加载更新")
            preview = result.get("playback_access") == "preview"
            duration = result.get("duration_s")
            if preview and (type(duration) not in {int, float} or not 0 < duration <= 30):
                raise MusicAssistantError("试听音源缺少有效时长，没有开始播放")
            parsed = urlsplit(result["url"])
            segments = parsed.path.split("/")
            if parsed.scheme not in {"http", "https"} or parsed.username or parsed.password or parsed.query or parsed.fragment or len(segments) != 6 or segments[1] != "single" or segments[3] != PLAYER_ID or not segments[5].startswith(PLAYER_ID + ".") or not all(re.fullmatch(r"[A-Za-z0-9_-]{1,128}", segments[index]) for index in (2, 4)):
                raise MusicAssistantError("Music Assistant 流地址不符合固定伊迪斯队列合同")
            format_name = segments[5][len(PLAYER_ID) + 1:]
            if format_name not in {"wav", "flac", "mp3", "ogg", "aac"}:
                raise MusicAssistantError("Music Assistant 返回了不支持的音频格式")
            base = _base_url(self.config.get("music_assistant_stream_base_url", "http://127.0.0.1:8097"))
            origin = urlsplit(base)
            api_origin = urlsplit(_base_url(self.config.get("music_assistant_base_url", "")))
            expected_hosts = {origin.hostname, api_origin.hostname}
            if parsed.hostname not in expected_hosts:
                try:
                    address = ipaddress.ip_address(parsed.hostname or "")
                    container_address = address.version == 4 and any(address in ipaddress.ip_network(network) for network in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))
                except ValueError:
                    container_address = False
                if not container_address or parsed.port != (origin.port or (443 if origin.scheme == "https" else 80)):
                    raise MusicAssistantError("Music Assistant 音频源站不属于已配置服务或容器网络")
            url = urlunsplit((origin.scheme, origin.netloc, parsed.path, "", ""))
            self._lease = result["lease"]
            return {"kind": "http", "trusted_by": "music_assistant", "base_url": base, "url": url, "headers": {}, "format": format_name, "source_position_s": int(position_s), "start_position_s": int(position_s), "seek_applied": True, "session_id": self._lease, "playback_access": result.get("playback_access", "unknown"), **({"duration_s": duration} if preview else {})}

    async def report(self, state, position_s=0, *, session_id=None):
        async with self._play_lock:
            return await self._report(state, position_s, session_id=session_id)

    async def _report(self, state, position_s=0, *, session_id=None):
        if not self._lease:
            return {"accepted": False}
        if session_id is not None and session_id != self._lease:
            return {"accepted": False}
        normalized = {"start": "started", "progress": "playing", "pause": "paused", "stop": "stopped", "ended": "finished", "idle": "finished"}.get(state, state)
        if normalized not in {"started", "playing", "paused", "stopped", "finished", "error"}:
            raise ValueError("未知的实体音乐回执")
        lease = self._lease
        result = await self._command("xiaoyanzi/report", {"lease": lease, "state": normalized, "position_s": min(1800, max(0, int(position_s or 0)))})
        if normalized in {"stopped", "finished", "error"} and self._lease == lease:
            self._lease = ""
        return result

    async def stop(self, *, session_id=None):
        async with self._play_lock:
            if not self._lease or session_id is not None and session_id != self._lease:
                return {"accepted": False}
            lease = self._lease
            result = await self._command("xiaoyanzi/stop", {"lease": lease})
            if not isinstance(result, dict) or result.get("accepted") is not True:
                self._last_check = "failed"
                raise MusicAssistantError("音乐平台没有确认停止，租约释放状态待核实；不会自动重试")
            if self._lease == lease:
                self._lease = ""
            return result

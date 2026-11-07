"""Google / Microsoft 的显式令牌续期边界；不创建或伪造 OAuth 授权。"""

from __future__ import annotations

import asyncio
import json
import logging
import re
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import aiohttp

from gmail_service import DpapiTokenVault

LOGGER = logging.getLogger("assistant.token_refresh")
PROVIDERS = {"google_calendar": "Google Calendar", "microsoft_calendar": "Microsoft 365（日历/邮件共享）"}
FIELDS = ("client_id", "client_secret", "refresh_token", "access_token", "expires_at", "tenant_id")


class TokenRefreshService:
    def __init__(self, config, http, directory, vault=None):
        self.config, self.http = config, http
        self.path = Path(directory) / "calendar-oauth.dpapi"
        self.vault = vault
        self.available = sys.platform == "win32" or vault is not None
        self._locks = {provider: asyncio.Lock() for provider in PROVIDERS}
        self._file_lock = asyncio.Lock()
        self._storage_error = False

    def _vault(self):
        if not self.available:
            raise RuntimeError("当前环境不支持 Windows DPAPI，无法安全保存续期凭据")
        if self.vault is None:
            self.vault = DpapiTokenVault(self.path)
        return self.vault

    async def start(self):
        if self.available and (self.vault is not None or self.path.exists()):
            try:
                if self.path.exists() and self.path.stat().st_size > 128 * 1024:
                    raise RuntimeError("日历OAuth安全存储超限")
                saved = await asyncio.to_thread(self._vault().load) or {}
                overrides = {}
                for provider in PROVIDERS:
                    values = saved.get(provider, {})
                    if not isinstance(values, dict):
                        raise RuntimeError("日历 OAuth 安全存储格式无效")
                    for field in FIELDS:
                        if field in values and self.config.get("_config_sources", {}).get(f"{provider}_{field}") != "environment":
                            value = values[field]
                            if not isinstance(value, str) or len(value) > 8192:
                                raise RuntimeError("日历OAuth安全存储字段无效")
                            overrides[f"{provider}_{field}"] = value
                self.config.update(overrides)
                self.config.setdefault("_config_sources", {}).update({key: "calendar_oauth" for key in overrides})
            except Exception:
                LOGGER.exception("oauth_secure_storage_load_failed")
                self._storage_error = True

    @staticmethod
    def _provider(provider):
        if provider not in PROVIDERS:
            raise ValueError("仅支持 Google Calendar 与 Microsoft Calendar 续期")
        return provider

    def status(self, provider):
        self._provider(provider)
        present = lambda field: bool(self.config.get(f"{provider}_{field}"))
        expires = self.config.get(f"{provider}_expires_at")
        expires_value = None
        invalid_expiry = False
        if expires:
            try:
                parsed = datetime.fromisoformat(str(expires).replace("Z", "+00:00"))
                if parsed.tzinfo is None:
                    raise ValueError("缺少时区")
                expires_value = parsed.timestamp()
            except (ValueError, TypeError):
                invalid_expiry = True
        ready = self.available and present("refresh_token") and present("client_id")
        environment_managed = self._storage_error or any(self.config.get("_config_sources", {}).get(f"{provider}_{field}") == "environment" for field in FIELDS)
        ready = ready and not environment_managed and not invalid_expiry
        state = "invalid_expiry" if invalid_expiry else "refresh_ready" if ready else "access_token_only" if present("access_token") else "not_configured"
        return {"provider": provider, "name": PROVIDERS[provider], "status": "storage_unavailable" if self._storage_error else state, "available": self.available and not environment_managed, "refresh_ready": ready, "access_token_configured": present("access_token"), "refresh_token_configured": present("refresh_token"), "client_id_configured": present("client_id"), "client_secret_configured": present("client_secret"), "expires_at": expires or None, "expired": expires_value <= time.time() if expires_value else None, "message": "日历OAuth安全存储无法读取，已停用续期；请由管理员检查，不会覆盖损坏文件" if self._storage_error else "配置由环境变量管理，控制台不会覆盖；请先在维护窗口移除环境覆盖" if environment_managed else "到期时间无效，请保存带时区的ISO 8601时间" if invalid_expiry else "续期参数已配置；尚未向提供商验证" if ready else "需要客户端 ID、Refresh Token 和 Windows DPAPI；仅 Access Token 无法续期", "fields": [{"key": field, "secret": field not in {"expires_at", "tenant_id"}, "configured": present(field), **({"value": self.config.get(f"{provider}_{field}", "")} if field in {"expires_at", "tenant_id"} else {})} for field in FIELDS]}

    async def configure(self, provider, values):
        self._provider(provider)
        if not self.status(provider).get("available"):
            raise RuntimeError("OAuth配置不可编辑：安全存储不可用或由环境变量管理")
        if not isinstance(values, dict) or set(values) - set(FIELDS):
            raise ValueError("包含不支持的续期配置字段")
        validated = {}
        for field, value in values.items():
            if not isinstance(value, str) or len(value) > 8192 or any(character.isspace() for character in value):
                raise ValueError("续期字段必须为无空白的字符串，且不能超过8192字符")
            if not value:
                continue
            if value.upper().startswith("REPLACE"):
                raise ValueError("不能保存示例占位值")
            if field == "tenant_id" and not re.fullmatch(r"[A-Za-z0-9.-]{1,128}", value):
                raise ValueError("Microsoft tenant_id 格式无效")
            if field == "expires_at":
                parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
                if parsed.tzinfo is None:
                    raise ValueError("到期时间必须携带时区")
                value = parsed.astimezone(timezone.utc).isoformat()
            validated[field] = value
        async with self._locks[provider]:
            await self._persist(provider, validated)
        return self.status(provider)

    async def _persist(self, provider, values):
        # 两个提供商共用文件，持有文件锁防止同时续期覆盖另一提供商的新令牌。
        async with self._file_lock:
            vault = self._vault()
            saved = await asyncio.to_thread(vault.load) or {}
            current = dict(saved.get(provider, {}))
            current.update(values)
            saved[provider] = current
            await asyncio.to_thread(vault.save, saved)
            self.config.update({f"{provider}_{field}": value for field, value in values.items()})
            self.config.setdefault("_config_sources", {}).update({f"{provider}_{field}": "calendar_oauth" for field in values})

    async def ensure_access_token(self, provider, *, force=False):
        self._provider(provider)
        async with self._locks[provider]:
            state = self.status(provider)
            token = self.config.get(f"{provider}_access_token", "")
            expires = state.get("expires_at")
            # 兼容现有仅Access Token配置：有效性未知时由原API判定，不伪造续期。
            if not force and token and state["status"] == "access_token_only" and state.get("expired") is not True:
                return token
            if not force and token and expires and state["status"] != "invalid_expiry":
                if datetime.fromisoformat(str(expires).replace("Z", "+00:00")).timestamp() > time.time() + 90:
                    return token
            if not state.get("refresh_ready"):
                raise RuntimeError("尚未配置可用的 OAuth 续期凭据；请完成提供商真实授权")
            get = lambda field: self.config.get(f"{provider}_{field}", "")
            endpoint = "https://oauth2.googleapis.com/token"
            if provider == "microsoft_calendar":
                tenant = get("tenant_id") or "common"
                if not re.fullmatch(r"[A-Za-z0-9.-]{1,128}", tenant):
                    raise ValueError("Microsoft tenant_id 格式无效")
                endpoint = f"https://login.microsoftonline.com/{tenant}/oauth2/v2.0/token"
            payload = {"grant_type": "refresh_token", "client_id": get("client_id"), "refresh_token": get("refresh_token")}
            if get("client_secret"):
                payload["client_secret"] = get("client_secret")
            try:
                async with self.http.post(endpoint, data=payload, timeout=aiohttp.ClientTimeout(total=20), allow_redirects=False) as response:
                    raw = bytearray()
                    async for chunk in response.content.iter_chunked(8192):
                        raw.extend(chunk)
                        if len(raw) > 65536:
                            raise RuntimeError("OAuth 续期响应超限")
                    try:
                        data = json.loads(raw)
                    except (ValueError, UnicodeDecodeError) as error:
                        raise RuntimeError("提供商返回了无效的OAuth响应") from error
                    if response.status != 200 or not isinstance(data, dict):
                        raise RuntimeError("提供商拒绝续期；请核对真实授权与客户端配置")
            except (aiohttp.ClientError, asyncio.TimeoutError):
                LOGGER.exception("oauth_refresh_network_failed provider=%s", provider)
                raise
            access = data.get("access_token")
            seconds = data.get("expires_in")
            if not isinstance(access, str) or not access or len(access) > 8192 or any(character.isspace() for character in access) or type(seconds) not in {int, float} or not 0 < seconds <= 31536000:
                raise RuntimeError("提供商返回的 OAuth 令牌或有效期无效")
            values = {"access_token": access, "expires_at": datetime.fromtimestamp(time.time() + seconds, timezone.utc).isoformat()}
            if data.get("refresh_token"):
                if not isinstance(data["refresh_token"], str) or len(data["refresh_token"]) > 8192 or any(character.isspace() for character in data["refresh_token"]):
                    raise RuntimeError("提供商返回的 Refresh Token 无效")
                values["refresh_token"] = data["refresh_token"]
            # 先持久化轮换后的 Refresh Token，再更新内存；保存失败必须向调用者报告。
            await self._persist(provider, values)
            LOGGER.info("oauth_refresh_completed provider=%s", provider)
            return access

    async def refresh(self, provider):
        await self.ensure_access_token(provider, force=True)
        return {**self.status(provider), "status": "refreshed", "message": "提供商已返回新令牌并安全保存；具体日历/邮件权限需单独检查"}

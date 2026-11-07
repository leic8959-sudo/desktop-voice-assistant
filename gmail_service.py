"""Gmail OAuth2/API service for the local desktop assistant.

The service deliberately keeps sending out of the voice-tool surface.  A panel
must prepare a draft and then confirm the same short-lived nonce and raw hash.
"""
from __future__ import annotations

import asyncio
import base64
import ctypes
import hashlib
import html as html_module
import json
import logging
import os
import re
import secrets
import sys
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from email import policy
from email.message import EmailMessage
from email.parser import BytesParser
from email.utils import getaddresses
from pathlib import Path
from typing import Any, Mapping
from urllib.parse import urlencode, urlparse

import aiohttp

LOGGER = logging.getLogger("assistant.gmail")

AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_URL = "https://oauth2.googleapis.com/token"
GMAIL_API_BASE = "https://gmail.googleapis.com/gmail/v1/users/me"
SCOPES = ("https://www.googleapis.com/auth/gmail.readonly", "https://www.googleapis.com/auth/gmail.compose")
MAX_BODY_CHARS = 12000
STATE_TTL_SECONDS = 300
SEND_CONFIRM_TTL_SECONDS = 120
HTTP_TIMEOUT_SECONDS = 15
SAFE_ID = re.compile(r"^[A-Za-z0-9_-]{1,256}$")


class GmailError(RuntimeError):
    """Safe, user-facing Gmail operation error."""


class TokenVault:
    """Small storage interface to make tests independent of Windows DPAPI."""

    available = True

    def load(self) -> dict[str, Any] | None:
        raise NotImplementedError

    def save(self, value: Mapping[str, Any]) -> None:
        raise NotImplementedError

    def clear(self) -> None:
        raise NotImplementedError


class DpapiTokenVault(TokenVault):
    """Encrypt token JSON with the current Windows user's DPAPI."""

    def __init__(self, path: str | os.PathLike[str]):
        self.path = Path(path)
        self.available = sys.platform == "win32"
        if not self.available:
            raise RuntimeError("Gmail令牌保管仅支持Windows DPAPI")

    @staticmethod
    def _protect(data: bytes, unprotect: bool = False) -> bytes:
        crypt = ctypes.windll.crypt32
        kernel = ctypes.windll.kernel32

        class Blob(ctypes.Structure):
            _fields_ = [("cbData", ctypes.c_uint32), ("pbData", ctypes.POINTER(ctypes.c_ubyte))]

        crypt.CryptProtectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_wchar_p, ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Blob)]
        crypt.CryptProtectData.restype = ctypes.c_int
        crypt.CryptUnprotectData.argtypes = [ctypes.POINTER(Blob), ctypes.c_wchar_p, ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(Blob)]
        crypt.CryptUnprotectData.restype = ctypes.c_int
        kernel.LocalFree.argtypes = [ctypes.c_void_p]
        kernel.LocalFree.restype = ctypes.c_void_p

        source = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
        source_blob = Blob(len(data), source)
        target_blob = Blob()
        function = crypt.CryptUnprotectData if unprotect else crypt.CryptProtectData
        if not function(ctypes.byref(source_blob), None, None, None, None, 1, ctypes.byref(target_blob)):
            raise OSError("Windows DPAPI操作失败")
        try:
            return ctypes.string_at(target_blob.pbData, target_blob.cbData)
        finally:
            kernel.LocalFree(target_blob.pbData)

    def load(self) -> dict[str, Any] | None:
        if not self.path.exists():
            return None
        try:
            payload = self._protect(self.path.read_bytes(), unprotect=True)
            value = json.loads(payload.decode("utf-8"))
            return value if isinstance(value, dict) else None
        except (OSError, ValueError, UnicodeDecodeError, json.JSONDecodeError) as error:
            LOGGER.exception("gmail_token_load_failed")
            raise GmailError("Gmail令牌无法读取") from error

    def save(self, value: Mapping[str, Any]) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        encrypted = self._protect(json.dumps(dict(value), separators=(",", ":")).encode("utf-8"))
        descriptor, temporary = tempfile.mkstemp(prefix=self.path.name + ".", suffix=".tmp", dir=self.path.parent)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(encrypted)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.path)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)

    def clear(self) -> None:
        try:
            self.path.unlink()
        except FileNotFoundError:
            pass


class MemoryTokenVault(TokenVault):
    """Explicitly injectable test vault; never used by production defaults."""

    def __init__(self, value: Mapping[str, Any] | None = None):
        self.value = dict(value) if value else None

    def load(self) -> dict[str, Any] | None:
        return dict(self.value) if self.value else None

    def save(self, value: Mapping[str, Any]) -> None:
        self.value = dict(value)

    def clear(self) -> None:
        self.value = None


@dataclass
class _OAuthState:
    verifier: str
    expires_at: float


@dataclass
class _SendConfirmation:
    draft_id: str
    raw_hash: str
    nonce: str
    expires_at: float


def _safe_redirect_uri(value: Any) -> str | None:
    if not isinstance(value, str) or not value or "\r" in value or "\n" in value:
        return None
    parsed = urlparse(value)
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        return None
    if parsed.scheme == "https" and parsed.hostname:
        return value
    if parsed.scheme == "http" and parsed.hostname in {"127.0.0.1", "localhost", "::1"}:
        return value
    return None


def _header_value(headers: Mapping[str, str], name: str) -> str:
    wanted = name.lower()
    for key, value in headers.items():
        if key.lower() == wanted:
            return value
    return ""


def _decode_b64(value: str) -> bytes:
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def _strip_html(value: str) -> str:
    value = re.sub(r"(?is)<(script|style).*?>.*?</\1>", " ", value)
    value = re.sub(r"(?s)<[^>]+>", " ", value)
    return re.sub(r"\s+", " ", html_module.unescape(value)).strip()


def _split_addresses(value: str) -> str:
    return value or ""


def _validate_header(value: Any, field: str) -> str:
    if not isinstance(value, str) or "\r" in value or "\n" in value:
        raise ValueError(f"{field}含有非法换行")
    return value.strip()


def _validate_addresses(value: Any, field: str, required: bool = False) -> list[str]:
    if value is None:
        values: list[Any] = []
    elif isinstance(value, str):
        values = [item.strip() for item in value.split(",") if item.strip()]
    elif isinstance(value, (list, tuple)):
        values = list(value)
    else:
        raise ValueError(f"{field}格式无效")
    if required and not values:
        raise ValueError("收件人不能为空")
    result = []
    for item in values:
        address = _validate_header(item, field)
        parsed = getaddresses([address])
        if len(parsed) != 1 or not parsed[0][1] or "@" not in parsed[0][1] or any(char.isspace() for char in parsed[0][1]):
            raise ValueError(f"{field}地址无效")
        result.append(address)
    return result


class GmailService:
    def __init__(self, config: Mapping[str, Any], aiohttp_client: Any = None, runtime_dir: str | os.PathLike[str] = ".", token_vault: TokenVault | None = None):
        self.config = config
        self.client = aiohttp_client
        self.runtime_dir = Path(runtime_dir)
        self._session: aiohttp.ClientSession | None = None
        self._token_vault = token_vault
        self._token: dict[str, Any] | None = None
        self._states: dict[str, _OAuthState] = {}
        self._confirmations: dict[str, _SendConfirmation] = {}
        self._lock = asyncio.Lock()
        self._send_lock = asyncio.Lock()
        self._epoch = 0
        self._attempt_path = self.runtime_dir / "gmail_send_attempts.json"
        if self._token_vault is None:
            try:
                self._token_vault = DpapiTokenVault(self.runtime_dir / "gmail_token.dpapi")
            except RuntimeError:
                self._token_vault = None
        if self._token_vault:
            try:
                self._token = self._token_vault.load()
            except GmailError:
                self._token = None

    @property
    def configured(self) -> bool:
        return bool(self.config.get("gmail_client_id") and self.config.get("gmail_client_secret") and _safe_redirect_uri(self.config.get("gmail_redirect_uri")))

    def status(self) -> dict[str, Any]:
        return {
            "configured": self.configured,
            "authorized": bool(self._token and self._token.get("access_token")),
            "connected": bool(self._token and self._token.get("access_token")),
            "email": self._token.get("email") if self._token else None,
            "send_enabled": bool(self.config.get("gmail_send_enabled", self.config.get("send_enabled", False))),
            "redirect_uri": self.config.get("gmail_redirect_uri") if self.configured else None,
            "available": bool(self.configured and self._token_vault is not None),
        }

    async def close(self) -> None:
        if self._session is not None:
            await self._session.close()
            self._session = None

    def start_authorization(self) -> dict[str, str]:
        if not self.configured:
            raise GmailError("Gmail尚未配置")
        now = time.time()
        self._states = {key: item for key, item in self._states.items() if item.expires_at > now}
        verifier = secrets.token_urlsafe(48)
        challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode()).digest()).rstrip(b"=").decode()
        state = secrets.token_urlsafe(32)
        if len(self._states) >= 20:
            oldest = min(self._states, key=lambda key: self._states[key].expires_at)
            self._states.pop(oldest, None)
        self._states[state] = _OAuthState(verifier, time.time() + STATE_TTL_SECONDS)
        query = urlencode({"client_id": self.config["gmail_client_id"], "redirect_uri": self.config["gmail_redirect_uri"], "response_type": "code", "scope": " ".join(SCOPES), "access_type": "offline", "prompt": "consent", "state": state, "code_challenge": challenge, "code_challenge_method": "S256"})
        return {"authorization_url": f"{AUTH_URL}?{query}", "state": state, "expires_in": STATE_TTL_SECONDS}

    async def complete_authorization(self, state: str, code: str) -> dict[str, Any]:
        if not isinstance(state, str) or not isinstance(code, str) or not code or "\r" in code or "\n" in code:
            raise GmailError("授权回调无效")
        oauth_state = self._states.pop(state, None)
        if oauth_state is None or oauth_state.expires_at <= time.time():
            raise GmailError("授权状态已失效")
        epoch = self._epoch
        payload = {"code": code, "client_id": self.config.get("gmail_client_id"), "client_secret": self.config.get("gmail_client_secret"), "redirect_uri": self.config.get("gmail_redirect_uri"), "grant_type": "authorization_code", "code_verifier": oauth_state.verifier}
        token = await self._request_json("POST", TOKEN_URL, data=payload, auth=False)
        if not token.get("access_token"):
            raise GmailError("Gmail授权未完成")
        returned_scopes = set(str(token.get("scope", "")).split())
        if returned_scopes and not set(SCOPES).issubset(returned_scopes):
            raise GmailError("Gmail授权范围不足")
        expires_in = int(token.get("expires_in", 3600))
        if expires_in <= 60:
            raise GmailError("Gmail授权有效期无效")
        token["expires_at"] = time.time() + expires_in
        try:
            profile = await self._request_json("GET", f"{GMAIL_API_BASE}/profile", auth=False, auth_token=token["access_token"])
            if profile.get("emailAddress"):
                token["email"] = profile["emailAddress"]
        except GmailError:
            LOGGER.exception("gmail_profile_read_failed")
        async with self._lock:
            if epoch != self._epoch:
                raise GmailError("授权流程已被重新配置")
            if self._token_vault is None:
                raise GmailError("当前系统不支持安全令牌保管")
            self._token_vault.save(token)
            self._token = token
        return {"authorized": True}

    async def _request_json(self, method: str, url: str, *, params: Mapping[str, Any] | None = None, data: Mapping[str, Any] | None = None, json_payload: Any = None, auth: bool = True, auth_token: str | None = None) -> dict[str, Any]:
        if auth:
            await self._ensure_token()
        headers = {"Accept": "application/json"}
        if auth_token:
            headers["Authorization"] = f"Bearer {auth_token}"
        elif auth and self._token:
            headers["Authorization"] = f"Bearer {self._token['access_token']}"
        client = self.client
        if client is None:
            if self._session is None:
                self._session = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=HTTP_TIMEOUT_SECONDS))
            client = self._session
        kwargs: dict[str, Any] = {"headers": headers, "timeout": HTTP_TIMEOUT_SECONDS}
        proxy = self.config.get("gmail_proxy_url", "")
        if proxy:
            parsed_proxy = urlparse(proxy) if isinstance(proxy, str) else None
            if parsed_proxy is None or parsed_proxy.scheme not in ("http", "https") or not parsed_proxy.hostname or parsed_proxy.username or parsed_proxy.password or parsed_proxy.query or parsed_proxy.fragment:
                raise GmailError("Gmail代理配置无效，请检查本机设置")
            kwargs["proxy"] = proxy
        if params is not None:
            kwargs["params"] = params
        if data is not None:
            kwargs["data"] = data
        if json_payload is not None:
            kwargs["json"] = json_payload
        try:
            kwargs["allow_redirects"] = False
            response_context = client.request(method, url, **kwargs)
            response = await response_context if hasattr(response_context, "__await__") else response_context
            if hasattr(response, "__aenter__"):
                async with response as entered:
                    return await self._read_response(entered)
            return await self._read_response(response)
        except (aiohttp.ClientError, asyncio.TimeoutError) as error:
            LOGGER.exception("gmail_http_failed method=%s", method)
            raise GmailError("无法连接 Google 服务，请检查助手的网络和代理后重新发起授权") from error

    async def _read_response(self, response: Any) -> dict[str, Any]:
        status = getattr(response, "status", 0)
        if getattr(response, "content_length", None) and response.content_length > 4 * 1024 * 1024:
            raise GmailError("Gmail响应过大")
        try:
            if not getattr(response, "content_length", None) and getattr(response, "content", None) is not None:
                raw = await response.content.read(4 * 1024 * 1024 + 1)
                if len(raw) > 4 * 1024 * 1024:
                    raise GmailError("Gmail响应过大")
                payload = json.loads(raw.decode("utf-8")) if raw else {}
            else:
                payload = await response.json(content_type=None)
        except (ValueError, TypeError, json.JSONDecodeError):
            payload = {}
        if status >= 300:
            LOGGER.error("gmail_http_error status=%s", status)
            raise GmailError("Gmail请求未完成")
        if not isinstance(payload, dict):
            raise GmailError("Gmail响应格式无效")
        return payload

    async def _ensure_token(self) -> None:
        if not self._token or not self._token.get("access_token"):
            raise GmailError("Gmail尚未授权")
        if float(self._token.get("expires_at", 0)) > time.time() + 60:
            return
        async with self._lock:
            if self._token and float(self._token.get("expires_at", 0)) > time.time() + 60:
                return
            refresh = self._token.get("refresh_token") if self._token else None
            if not refresh:
                raise GmailError("Gmail授权已过期，请重新登录")
            token = await self._request_json("POST", TOKEN_URL, data={"client_id": self.config.get("gmail_client_id"), "client_secret": self.config.get("gmail_client_secret"), "refresh_token": refresh, "grant_type": "refresh_token"}, auth=False)
            if not token.get("access_token"):
                raise GmailError("Gmail授权已过期，请重新登录")
            token.setdefault("refresh_token", refresh)
            token.setdefault("email", self._token.get("email"))
            token["expires_at"] = time.time() + int(token.get("expires_in", 3600))
            self._token = token
            if self._token_vault:
                self._token_vault.save(token)

    async def search(self, query: str, page_token: str | None = None, limit: int = 20) -> dict[str, Any]:
        if not isinstance(query, str):
            raise ValueError("搜索条件无效")
        limit = max(1, min(int(limit), 20))
        params: dict[str, Any] = {"q": query, "maxResults": limit}
        if page_token:
            params["pageToken"] = page_token
        payload = await self._request_json("GET", f"{GMAIL_API_BASE}/messages", params=params)
        items = payload.get("messages", [])
        details = await asyncio.gather(*(self._message_metadata(item.get("id", "")) for item in items if isinstance(item, dict) and SAFE_ID.fullmatch(str(item.get("id", "")))))
        return {"items": details, "next_page_token": payload.get("nextPageToken")}

    async def _message_metadata(self, message_id: str) -> dict[str, Any]:
        try:
            payload = await self._request_json("GET", f"{GMAIL_API_BASE}/messages/{message_id}", params={"format": "metadata", "metadataHeaders": ["Subject", "From", "To", "Date"]})
            headers = {item.get("name", ""): item.get("value", "") for item in payload.get("payload", {}).get("headers", []) if isinstance(item, dict)}
            internal_date = payload.get("internalDate")
            received_at = datetime.fromtimestamp(int(internal_date) / 1000, timezone.utc).isoformat() if internal_date is not None else None
            return {"id": payload.get("id", message_id), "thread_id": payload.get("threadId"), "subject": _header_value(headers, "Subject"), "from": _header_value(headers, "From"), "to": _header_value(headers, "To"), "date": _header_value(headers, "Date"), "received_at": received_at}
        except GmailError:
            current = time.monotonic()
            if current - getattr(self, "_metadata_error_at", -300) >= 300:
                LOGGER.exception("gmail_message_metadata_failed")
                self._metadata_error_at = current
            return {"id": message_id}

    async def read_message(self, message_id: str) -> dict[str, Any]:
        if not isinstance(message_id, str) or not SAFE_ID.fullmatch(message_id):
            raise ValueError("邮件标识无效")
        payload = await self._request_json("GET", f"{GMAIL_API_BASE}/messages/{message_id}", params={"format": "full"})
        return self._message_summary(payload)

    def _message_summary(self, payload: Mapping[str, Any]) -> dict[str, Any]:
        headers = {item.get("name", ""): item.get("value", "") for item in payload.get("payload", {}).get("headers", []) if isinstance(item, dict)}
        plain, rich, attachments = self._walk_parts(payload.get("payload", {}))
        body = plain if plain else _strip_html(rich)
        return {"id": payload.get("id"), "thread_id": payload.get("threadId"), "subject": _header_value(headers, "Subject"), "from": _header_value(headers, "From"), "to": _header_value(headers, "To"), "cc": _header_value(headers, "Cc"), "bcc": _header_value(headers, "Bcc"), "date": _header_value(headers, "Date"), "body": body[:MAX_BODY_CHARS], "body_truncated": len(body) > MAX_BODY_CHARS, "body_untrusted": True, "body_is_plain": bool(plain), "attachments": attachments}

    def _walk_parts(self, part: Mapping[str, Any]) -> tuple[str, str, list[dict[str, Any]]]:
        plain = ""
        rich = ""
        attachments: list[dict[str, Any]] = []
        filename = part.get("filename")
        body = part.get("body") if isinstance(part.get("body"), dict) else {}
        if filename or body.get("attachmentId"):
            attachments.append({"filename": filename or "", "mime_type": part.get("mimeType", "application/octet-stream"), "size": body.get("size", 0), "attachment_id": body.get("attachmentId")})
        if part.get("mimeType") == "text/plain" and body.get("data"):
            plain = _decode_b64(body["data"]).decode("utf-8", errors="replace")
        elif part.get("mimeType") == "text/html" and body.get("data"):
            rich = _decode_b64(body["data"]).decode("utf-8", errors="replace")
        for child in part.get("parts", []) or []:
            child_plain, child_rich, child_attachments = self._walk_parts(child)
            plain += child_plain
            rich += child_rich
            attachments.extend(child_attachments)
        return plain, rich, attachments

    async def list_drafts(self, page_token: str | None = None, limit: int = 20) -> dict[str, Any]:
        limit = max(1, min(int(limit), 20))
        params: dict[str, Any] = {"maxResults": limit}
        if page_token:
            params["pageToken"] = page_token
        payload = await self._request_json("GET", f"{GMAIL_API_BASE}/drafts", params=params)
        items = payload.get("drafts", [])
        details = await asyncio.gather(*(self._draft_metadata(item.get("id", "")) for item in items if isinstance(item, dict) and SAFE_ID.fullmatch(str(item.get("id", "")))))
        return {"items": details, "next_page_token": payload.get("nextPageToken")}

    async def _draft_metadata(self, draft_id: str) -> dict[str, Any]:
        try:
            draft = await self.get_draft(draft_id)
            return {"id": draft_id, "thread_id": draft.get("thread_id"), "subject": draft.get("subject", ""), "to": draft.get("to", ""), "revision": draft.get("revision")}
        except GmailError:
            return {"id": draft_id}

    async def get_draft(self, draft_id: str, include_raw: bool = False) -> dict[str, Any]:
        if not isinstance(draft_id, str) or not SAFE_ID.fullmatch(draft_id):
            raise ValueError("草稿标识无效")
        payload = await self._request_json("GET", f"{GMAIL_API_BASE}/drafts/{draft_id}", params={"format": "raw"})
        message = payload.get("message", payload)
        raw = message.get("raw", "")
        if raw and not message.get("payload"):
            try:
                parsed = BytesParser(policy=policy.default).parsebytes(_decode_b64(raw))
                body_part = parsed.get_body(preferencelist=("plain", "html"))
                body_text = body_part.get_content() if body_part else ""
                attachments = [{"filename": item.get_filename() or "", "mime_type": item.get_content_type(), "size": len(item.get_payload(decode=True) or b"")} for item in parsed.iter_attachments()]
                summary = {"id": payload.get("id", draft_id), "thread_id": message.get("threadId"), "subject": str(parsed.get("Subject", "")), "from": str(parsed.get("From", "")), "to": str(parsed.get("To", "")), "cc": str(parsed.get("Cc", "")), "bcc": str(parsed.get("Bcc", "")), "date": str(parsed.get("Date", "")), "body": body_text[:MAX_BODY_CHARS], "body_truncated": len(body_text) > MAX_BODY_CHARS, "body_untrusted": True, "body_is_plain": bool(body_part and body_part.get_content_type() == "text/plain"), "attachments": attachments}
            except (ValueError, TypeError):
                summary = {"id": payload.get("id", draft_id), "body": "", "body_untrusted": True, "attachments": []}
        else:
            summary = self._message_summary(message)
            summary["id"] = payload.get("id", draft_id)
        summary["draft_id"] = payload.get("id", draft_id)
        if include_raw:
            summary["raw"] = raw
        summary["revision"] = hashlib.sha256(raw.encode()).hexdigest()
        return summary

    async def _source_reply(self, message_id: str) -> tuple[str | None, str | None, str | None]:
        if not isinstance(message_id, str) or not SAFE_ID.fullmatch(message_id):
            raise ValueError("源邮件标识无效")
        source = await self._request_json("GET", f"{GMAIL_API_BASE}/messages/{message_id}", params={"format": "metadata", "metadataHeaders": ["Message-ID", "References", "Subject"]})
        headers = {item.get("name", ""): item.get("value", "") for item in source.get("payload", {}).get("headers", [])}
        return source.get("threadId"), _header_value(headers, "Message-ID"), _header_value(headers, "References")

    @staticmethod
    def _make_raw(to: list[str], subject: str, body: str, cc: list[str], bcc: list[str], in_reply_to: str | None, references: str | None, sender: str | None = None) -> str:
        message = EmailMessage(policy=policy.SMTP)
        if sender:
            message["From"] = sender
        message["To"] = ", ".join(to)
        if cc:
            message["Cc"] = ", ".join(cc)
        if bcc:
            message["Bcc"] = ", ".join(bcc)
        message["Subject"] = subject
        if in_reply_to:
            message["In-Reply-To"] = in_reply_to
        if references:
            message["References"] = references
        message.set_content(body)
        return base64.urlsafe_b64encode(message.as_bytes()).rstrip(b"=").decode()

    async def _sender_email(self) -> str:
        sender = self._token.get("email") if self._token else None
        if not sender:
            profile = await self._request_json("GET", f"{GMAIL_API_BASE}/profile")
            sender = profile.get("emailAddress")
            if sender and self._token is not None:
                self._token["email"] = sender
        values = _validate_addresses(sender, "发件人", required=True)
        return values[0]

    async def create_draft(self, to: Any, subject: Any, body: Any, cc: Any = None, bcc: Any = None, reply_to_message_id: str | None = None) -> dict[str, Any]:
        recipients = _validate_addresses(to, "收件人", required=True)
        carbon_copy = _validate_addresses(cc, "抄送")
        blind_copy = _validate_addresses(bcc, "密送")
        subject_value = _validate_header(subject, "主题")
        if not isinstance(body, str):
            raise ValueError("正文格式无效")
        thread_id = None
        in_reply_to = references = None
        if reply_to_message_id:
            thread_id, in_reply_to, references = await self._source_reply(reply_to_message_id)
        raw = self._make_raw(recipients, subject_value, body, carbon_copy, blind_copy, in_reply_to, references, await self._sender_email())
        message: dict[str, Any] = {"raw": raw}
        if thread_id:
            message["threadId"] = thread_id
        payload = await self._request_json("POST", f"{GMAIL_API_BASE}/drafts", json_payload={"message": message})
        return await self.get_draft(str(payload.get("id", "")))

    async def update_draft(self, draft_id: str, to: Any, subject: Any, body: Any, cc: Any = None, bcc: Any = None, reply_to_message_id: str | None = None, revision: str | None = None) -> dict[str, Any]:
        if not isinstance(draft_id, str) or not SAFE_ID.fullmatch(draft_id):
            raise ValueError("草稿标识无效")
        async with self._send_lock:
            current = await self.get_draft(draft_id, include_raw=True)
            if revision is not None and not secrets.compare_digest(str(current.get("revision", "")), str(revision)):
                raise GmailError("草稿版本已变化，请先重新读取")
            if current.get("attachments") or current.get("body_truncated") or not current.get("body_is_plain", False):
                raise GmailError("草稿含附件、截断正文或非纯文本，拒绝覆盖")
            if cc is None:
                cc = current.get("cc") or None
            if bcc is None:
                bcc = current.get("bcc") or None
            recipients = _validate_addresses(to, "收件人", required=True)
            carbon_copy = _validate_addresses(cc, "抄送")
            blind_copy = _validate_addresses(bcc, "密送")
            subject_value = _validate_header(subject, "主题")
            if not isinstance(body, str):
                raise ValueError("正文格式无效")
            thread_id = None
            in_reply_to = references = None
            try:
                parsed_current = BytesParser(policy=policy.default).parsebytes(_decode_b64(current.get("raw", "")))
                in_reply_to = str(parsed_current.get("In-Reply-To", "")) or None
                references = str(parsed_current.get("References", "")) or None
                thread_id = current.get("thread_id")
            except (ValueError, TypeError):
                pass
            if reply_to_message_id:
                thread_id, in_reply_to, references = await self._source_reply(reply_to_message_id)
            message = {"raw": self._make_raw(recipients, subject_value, body, carbon_copy, blind_copy, in_reply_to, references, await self._sender_email())}
            if thread_id:
                message["threadId"] = thread_id
            self._confirmations.pop(draft_id, None)
            await self._request_json("PUT", f"{GMAIL_API_BASE}/drafts/{draft_id}", json_payload={"message": message})
            return await self.get_draft(draft_id)

    def _load_attempts(self) -> dict[str, Any]:
        try:
            value = json.loads(self._attempt_path.read_text(encoding="utf-8"))
            if not isinstance(value, dict):
                raise GmailError("发送记录格式无效，已停止发送")
            return value
        except FileNotFoundError:
            return {}
        except (OSError, ValueError, json.JSONDecodeError) as error:
            LOGGER.exception("gmail_attempt_log_load_failed")
            raise GmailError("发送记录无法读取，已停止发送") from error

    def _save_attempts(self, attempts: Mapping[str, Any]) -> None:
        self._attempt_path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self._attempt_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(dict(attempts), ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
        os.replace(temporary, self._attempt_path)

    async def prepare_send(self, draft_id: str) -> dict[str, Any]:
        if not self.config.get("gmail_send_enabled", self.config.get("send_enabled", False)):
            raise GmailError("发送功能尚未启用")
        async with self._send_lock:
            now = time.time()
            self._confirmations = {key: item for key, item in self._confirmations.items() if item.expires_at > now}
            draft = await self.get_draft(draft_id, include_raw=True)
            if draft.get("attachments") or draft.get("body_truncated") or not draft.get("body_is_plain", False):
                raise GmailError("草稿含附件、截断正文或非纯文本，需在 Gmail 中核对后发送")
            raw_hash = hashlib.sha256((draft.get("raw") or "").encode()).hexdigest()
            nonce = secrets.token_urlsafe(32)
            self._confirmations[draft_id] = _SendConfirmation(draft_id, raw_hash, nonce, time.time() + SEND_CONFIRM_TTL_SECONDS)
            return {"draft_id": draft_id, "confirmation_id": nonce, "raw_sha256": raw_hash, "expires_at": time.time() + SEND_CONFIRM_TTL_SECONDS, "draft": {"id": draft_id, "to": draft.get("to", ""), "cc": draft.get("cc", ""), "bcc": draft.get("bcc", ""), "subject": draft.get("subject", ""), "body": draft.get("body", ""), "revision": raw_hash}}

    async def confirm_send(self, draft_id: str, confirmation_id: str, raw_sha256: str | None = None) -> dict[str, Any]:
        async with self._send_lock:
            if not self.config.get("gmail_send_enabled", self.config.get("send_enabled", False)):
                raise GmailError("发送功能尚未启用")
            confirmation = self._confirmations.pop(draft_id, None)
            if confirmation is None or confirmation.expires_at <= time.time() or not secrets.compare_digest(confirmation.nonce, str(confirmation_id)):
                raise GmailError("发送确认已失效")
            draft = await self.get_draft(draft_id, include_raw=True)
            actual_hash = hashlib.sha256((draft.get("raw") or "").encode()).hexdigest()
            if not secrets.compare_digest(actual_hash, confirmation.raw_hash) or (raw_sha256 is not None and not secrets.compare_digest(actual_hash, str(raw_sha256))):
                raise GmailError("草稿已更新，请重新确认")
            attempts = self._load_attempts()
            if len(attempts) >= 1000 and draft_id not in attempts:
                raise GmailError("发送记录达到本地上限，需人工归档")
            existing = attempts.get(draft_id)
            if existing and existing.get("status") in {"SENT", "UNKNOWN", "IN_FLIGHT"}:
                status = "already_sent" if existing.get("status") == "SENT" else "unknown"
                return {**existing, "status": status}
            record = {"draft_id": draft_id, "raw_sha256": actual_hash, "status": "IN_FLIGHT", "attempted_at": time.time()}
            attempts[draft_id] = record
            self._save_attempts(attempts)
            try:
                send_message = {"raw": draft.get("raw", "")}
                if draft.get("thread_id"):
                    send_message["threadId"] = draft["thread_id"]
                response = await self._request_json("POST", f"{GMAIL_API_BASE}/drafts/send", json_payload={"id": draft_id, "message": send_message})
            except (GmailError, asyncio.TimeoutError) as error:
                record["status"] = "UNKNOWN"
                self._save_attempts(attempts)
                if isinstance(error, asyncio.TimeoutError):
                    raise GmailError("发送结果未知，请在 Gmail 中核对") from error
                raise
            record["message_id"] = response.get("id") or response.get("message", {}).get("id")
            if not record["message_id"]:
                record["status"] = "UNKNOWN"
                self._save_attempts(attempts)
                raise GmailError("发送结果未知，请在 Gmail 中核对")
            record["status"] = "SENT"
            self._save_attempts(attempts)
            return {**record, "status": "sent"}

    async def disconnect(self) -> dict[str, bool]:
        async with self._send_lock:
            async with self._lock:
                self._epoch += 1
                self._token = None
                self._states.clear()
                self._confirmations.clear()
                if self._token_vault:
                    self._token_vault.clear()
                return {"authorized": False}

    async def reconfigure(self, settings: Mapping[str, Any]) -> dict[str, Any]:
        async with self._send_lock:
            async with self._lock:
                if self._token_vault:
                    self._token_vault.clear()
                self._epoch += 1
                self._states.clear()
                self._confirmations.clear()
                self._token = None
                self.config = dict(settings)
                return self.status()

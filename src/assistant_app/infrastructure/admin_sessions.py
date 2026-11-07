"""持久管理员会话：仅存摘要，连接按调用开启并关闭。"""

from __future__ import annotations

import hashlib
import logging
import secrets
import sqlite3
import time
from contextlib import closing
from pathlib import Path


LOGGER = logging.getLogger(__name__)
SESSION_TTL_SECONDS = 30 * 24 * 60 * 60
MAX_SESSIONS = 32


class AdminSessionStoreError(RuntimeError):
    pass


class AdminSessionCapacityError(AdminSessionStoreError):
    pass


class PersistentAdminSessions:
    def __init__(self, path: str | Path, credential_binding: str) -> None:
        if not isinstance(credential_binding, str) or not credential_binding:
            raise ValueError("管理员会话需要有效的凭据绑定")
        self.path = Path(path)
        self.credential_binding = hashlib.sha256(credential_binding.encode("utf-8")).hexdigest()

    def _connect(self) -> sqlite3.Connection:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        connection = sqlite3.connect(self.path, timeout=5)
        try:
            connection.execute("PRAGMA synchronous=FULL")
            if connection.execute("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'admin_sessions'").fetchone() is None:
                connection.execute("BEGIN IMMEDIATE")
                connection.execute(
                    "CREATE TABLE IF NOT EXISTS admin_sessions ("
                    "token_hash TEXT PRIMARY KEY NOT NULL CHECK(length(token_hash) = 64), "
                    "credential_binding TEXT NOT NULL CHECK(length(credential_binding) = 64), "
                    "expires_at INTEGER NOT NULL CHECK(expires_at > 0))"
                )
                connection.commit()
        except Exception:
            connection.close()
            raise
        return connection

    @staticmethod
    def _token_hash(raw_token: object) -> str | None:
        if not isinstance(raw_token, str) or len(raw_token) != 43 or any(character not in "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_" for character in raw_token):
            return None
        return hashlib.sha256(raw_token.encode("ascii")).hexdigest()

    def issue(self) -> tuple[str, int]:
        try:
            now = int(time.time())
            expires_at = now + SESSION_TTL_SECONDS
            raw_token = secrets.token_urlsafe(32)
            token_hash = hashlib.sha256(raw_token.encode("ascii")).hexdigest()
            with closing(self._connect()) as connection, connection:
                connection.execute("BEGIN IMMEDIATE")
                connection.execute("DELETE FROM admin_sessions WHERE expires_at <= ? OR credential_binding != ?", (now, self.credential_binding))
                count = connection.execute("SELECT COUNT(*) FROM admin_sessions").fetchone()[0]
                if count >= MAX_SESSIONS:
                    raise AdminSessionCapacityError("本机登录会话已满，请退出旧会话或等待过期")
                connection.execute("INSERT INTO admin_sessions (token_hash, credential_binding, expires_at) VALUES (?, ?, ?)", (token_hash, self.credential_binding, expires_at))
            return raw_token, expires_at
        except AdminSessionCapacityError:
            raise
        except Exception as error:
            LOGGER.exception("admin_session_issue_failed")
            raise AdminSessionStoreError("管理员会话保存失败，未建立登录") from error

    def valid(self, raw_token: object) -> bool:
        token_hash = self._token_hash(raw_token)
        if token_hash is None:
            return False
        try:
            with closing(self._connect()) as connection:
                row = connection.execute("SELECT credential_binding, expires_at FROM admin_sessions WHERE token_hash = ?", (token_hash,)).fetchone()
            return bool(row and row[0] == self.credential_binding and row[1] > time.time())
        except Exception:
            LOGGER.exception("admin_session_validation_failed")
            return False

    def revoke(self, raw_token: object) -> None:
        token_hash = self._token_hash(raw_token)
        if token_hash is None:
            return
        try:
            with closing(self._connect()) as connection, connection:
                connection.execute("BEGIN IMMEDIATE")
                connection.execute("DELETE FROM admin_sessions WHERE token_hash = ? AND credential_binding = ?", (token_hash, self.credential_binding))
        except Exception as error:
            LOGGER.exception("admin_session_revoke_failed")
            raise AdminSessionStoreError("管理员会话注销失败，请稍后重试") from error

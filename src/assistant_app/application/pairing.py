"""Short-lived device pairing sessions.

The pairing code is a one-time bootstrap secret, never a device credential.
The optional atomic snapshot supports a single local process. Multi-instance
deployments require a shared transactional store.
"""

from __future__ import annotations

import secrets
import logging
import json
import os
from copy import deepcopy
from pathlib import Path
from dataclasses import asdict, dataclass
from datetime import datetime, timedelta, timezone
from enum import StrEnum

LOGGER = logging.getLogger("assistant.pairing")


class PairingStatus(StrEnum):
    PENDING = "pending"
    CLAIMED = "claimed"
    EXPIRED = "expired"
    REVOKED = "revoked"


@dataclass
class PairingSession:
    pairing_id: str
    code: str
    device_nonce: str
    created_at: datetime
    expires_at: datetime
    status: PairingStatus = PairingStatus.PENDING
    attempts: int = 0
    max_attempts: int = 5
    device_id: str | None = None
    workspace_id: str | None = None
    device_credential: str | None = None
    delivery_expires_at: datetime | None = None

    def public(self) -> dict[str, object]:
        return {"pairing_id": self.pairing_id, "code": self.code, "created_at": self.created_at.isoformat(), "expires_at": self.expires_at.isoformat(), "status": self.status.value, "attempts_remaining": max(0, self.max_attempts - self.attempts), "device_id": self.device_id, "workspace_id": self.workspace_id}


class PairingManager:
    def __init__(self, ttl_seconds: int = 600, storage_path: str | Path | None = None) -> None:
        self.ttl_seconds = ttl_seconds
        self.storage_path = Path(storage_path) if storage_path else None
        self._sessions: dict[str, PairingSession] = {}
        self._load()
        self._committed_sessions = deepcopy(self._sessions)

    def _load(self) -> None:
        if not self.storage_path or not self.storage_path.is_file(): return
        try:
            rows = json.loads(self.storage_path.read_text(encoding="utf-8"))
            for row in rows:
                row["created_at"] = datetime.fromisoformat(row["created_at"])
                row["expires_at"] = datetime.fromisoformat(row["expires_at"])
                row["status"] = PairingStatus(row["status"])
                delivery = row.get("delivery_expires_at")
                row["delivery_expires_at"] = datetime.fromisoformat(delivery) if delivery else None
                row.setdefault("max_attempts", 5)
                row["attempts"] = row.get("attempts", row.get("max_attempts", 5) - row.get("attempts_remaining", 5))
                self._sessions[row["pairing_id"]] = PairingSession(**{key: row.get(key) for key in ("pairing_id","code","device_nonce","created_at","expires_at","status","attempts","max_attempts","device_id","workspace_id","device_credential","delivery_expires_at")})
        except (OSError, ValueError, TypeError, KeyError):
            LOGGER.exception("pairing_snapshot_load_failed")
            raise

    def _save(self) -> None:
        if not self.storage_path: return
        data = [asdict(session) | {"created_at": session.created_at.isoformat(), "expires_at": session.expires_at.isoformat(), "delivery_expires_at": session.delivery_expires_at.isoformat() if session.delivery_expires_at else None} for session in self._sessions.values()]
        temporary = self.storage_path.with_suffix(".tmp")
        try:
            self.storage_path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps(data, ensure_ascii=False), encoding="utf-8")
            os.replace(temporary, self.storage_path)
        except OSError:
            self._sessions = deepcopy(self._committed_sessions)
            LOGGER.exception("pairing_snapshot_save_failed")
            raise
        self._committed_sessions = deepcopy(self._sessions)

    def create(self, device_nonce: str) -> PairingSession:
        now = datetime.now(timezone.utc)
        if not isinstance(device_nonce, str) or not 16 <= len(device_nonce) <= 128:
            raise ValueError("device_nonce 长度必须为16到128字符")
        for existing in self._sessions.values():
            if existing.status == PairingStatus.PENDING and existing.expires_at > now and secrets.compare_digest(existing.device_nonce, device_nonce):
                return existing
        self._sessions = {key: value for key, value in self._sessions.items() if value.expires_at > now or value.status == PairingStatus.CLAIMED}
        if len(self._sessions) >= 256:
            raise ValueError("配对容量已满，请稍后重试")
        session = PairingSession(pairing_id=secrets.token_urlsafe(18), code=f"{secrets.randbelow(1_000_000):06d}", device_nonce=device_nonce[:128], created_at=now, expires_at=now + timedelta(seconds=self.ttl_seconds))
        active_codes = {value.code for value in self._sessions.values() if value.status == PairingStatus.PENDING}
        while session.code in active_codes:
            session.code = f"{secrets.randbelow(1_000_000):06d}"
        self._sessions[session.pairing_id] = session
        self._save()
        return session

    def claim_code(self, code: str, workspace_id: str) -> PairingSession:
        normalized = code.replace(" ", "").strip()
        if len(normalized) != 6 or not normalized.isascii() or not normalized.isdigit():
            raise ValueError("请输入设备显示的六位配对码")
        for session in tuple(self._sessions.values()):
            if secrets.compare_digest(session.code, normalized) and self.get(session.pairing_id).status == PairingStatus.PENDING:
                return self.claim(session.pairing_id, normalized, workspace_id, "desk-" + secrets.token_hex(8))
        raise ValueError("配对码无效或已过期")

    def get(self, pairing_id: str) -> PairingSession | None:
        session = self._sessions.get(pairing_id)
        if session and session.status == PairingStatus.PENDING and datetime.now(timezone.utc) >= session.expires_at:
            session.status = PairingStatus.EXPIRED
            self._save()
        return session

    def claim(self, pairing_id: str, code: str, workspace_id: str, device_id: str) -> PairingSession:
        if workspace_id != "local" or not isinstance(device_id, str) or not device_id.strip() or len(device_id) > 128:
            raise ValueError("需要有效设备ID，当前仅支持local工作区")
        session = self.get(pairing_id)
        if session is None or session.status != PairingStatus.PENDING:
            raise ValueError("配对会话不存在或已失效")
        session.attempts += 1
        if session.attempts > session.max_attempts:
            session.status = PairingStatus.REVOKED
            self._save()
            raise ValueError("配对尝试次数已用尽")
        if not secrets.compare_digest(session.code, str(code).strip()):
            if session.attempts >= session.max_attempts:
                session.status = PairingStatus.REVOKED
            self._save()
            raise ValueError("配对码无效")
        session.status = PairingStatus.CLAIMED
        session.workspace_id = workspace_id[:128]
        session.device_id = device_id[:128]
        session.device_credential = secrets.token_urlsafe(48)
        session.delivery_expires_at = datetime.now(timezone.utc) + timedelta(minutes=2)
        self._save()
        return session

    def device_result(self, pairing_id: str, code: str, device_nonce: str) -> str | None:
        session = self.get(pairing_id)
        if session is None or datetime.now(timezone.utc) >= (session.delivery_expires_at or session.expires_at) or not secrets.compare_digest(session.device_nonce, device_nonce) or not secrets.compare_digest(session.code, str(code).strip()) or session.status != PairingStatus.CLAIMED:
            return None
        return session.device_credential

    def revoke(self, pairing_id: str) -> None:
        session = self.get(pairing_id)
        if session:
            session.status = PairingStatus.REVOKED
            self._save()

    def authenticate_device(self, credential: str) -> str | None:
        if not credential:
            return None
        for session in self._sessions.values():
            if session.status == PairingStatus.CLAIMED and session.device_credential and secrets.compare_digest(session.device_credential, credential):
                return session.device_id
        return None

    def paired_devices(self) -> list[dict[str, object]]:
        return [{"id": session.device_id, "pairing_id": session.pairing_id, "workspace": session.workspace_id} for session in self._sessions.values() if session.status == PairingStatus.CLAIMED and session.device_id]

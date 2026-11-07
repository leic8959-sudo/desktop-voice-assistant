"""单实例管理员动作确认：短期、绑定参数、单次消费。"""

from __future__ import annotations

import hashlib
import json
import secrets
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Any


@dataclass
class Confirmation:
    subject: str
    action: str
    parameter_hash: str
    expires_at: float
    expires_at_iso: str
    state: str = "pending"
    result: dict[str, Any] | None = None


class ConfirmationStore:
    """进程内存储；单实例限制需在控制台明确展示。"""

    def __init__(self, ttl_seconds: int = 300, max_items: int = 256) -> None:
        self.ttl_seconds = ttl_seconds
        self.max_items = max_items
        self._items: dict[str, Confirmation] = {}

    @staticmethod
    def _hash(parameters: dict[str, Any]) -> str:
        encoded = json.dumps(parameters, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(encoded.encode("utf-8")).hexdigest()

    def _prune(self) -> None:
        now = time.time()
        for token, item in tuple(self._items.items()):
            if item.expires_at <= now:
                self._items.pop(token, None)
        if len(self._items) > self.max_items:
            for token, _ in sorted(self._items.items(), key=lambda pair: pair[1].expires_at)[: len(self._items) - self.max_items]:
                self._items.pop(token, None)

    def prepare(self, subject: str, action: str, parameters: dict[str, Any]) -> tuple[str, Confirmation]:
        self._prune()
        expires = time.time() + self.ttl_seconds
        item = Confirmation(subject, action, self._hash(parameters), expires, datetime.fromtimestamp(expires, timezone.utc).isoformat())
        token = secrets.token_urlsafe(32)
        self._items[token] = item
        return token, item

    def begin(self, token: str, subject: str, action: str, parameters: dict[str, Any]) -> tuple[str, Confirmation]:
        self._prune()
        item = self._items.get(str(token))
        if item is None or item.expires_at <= time.time():
            raise PermissionError("确认凭证不存在或已过期，请重新生成预览")
        if item.subject != subject or item.action != action:
            raise PermissionError("确认凭证与当前管理员或动作不匹配")
        if item.parameter_hash != self._hash(parameters):
            raise PermissionError("参数已变化，请重新生成预览并确认")
        if item.state == "pending":
            item.state = "executing"
            return "execute", item
        if item.state == "executing":
            return "in_progress", item
        return "replay", item

    def finish(self, token: str, state: str, result: dict[str, Any]) -> None:
        item = self._items.get(str(token))
        if item is not None:
            item.state, item.result = state, result

    def invalidate_pending(self) -> None:
        for token, item in tuple(self._items.items()):
            if item.state == "pending":
                self._items.pop(token, None)

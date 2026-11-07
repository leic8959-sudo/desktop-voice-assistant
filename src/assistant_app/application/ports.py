"""应用层端口。适配器实现这些协议，业务用例不依赖供应商 SDK。"""

from __future__ import annotations

from collections.abc import Mapping
from typing import Protocol


class LlmPort(Protocol):
    async def respond(self, messages: list[Mapping[str, object]], *, session_id: str) -> str: ...


class DevicePort(Protocol):
    async def send(self, device_id: str, payload: Mapping[str, object]) -> None: ...


class SecretProvider(Protocol):
    def get(self, name: str) -> str | None: ...

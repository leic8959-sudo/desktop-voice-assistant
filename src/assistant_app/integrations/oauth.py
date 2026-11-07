"""OAuth/token storage boundary; providers must not write tokens directly to connectors."""

from __future__ import annotations

from dataclasses import dataclass
from datetime import datetime
from typing import Protocol


@dataclass(frozen=True)
class OAuthGrant:
    connector_id: str
    subject: str
    access_token: str
    refresh_token: str | None
    expires_at: datetime | None
    scopes: tuple[str, ...]


class OAuthStore(Protocol):
    async def get(self, connector_id: str, subject: str) -> OAuthGrant | None: ...

    async def save(self, grant: OAuthGrant) -> None: ...

    async def revoke(self, connector_id: str, subject: str) -> None: ...

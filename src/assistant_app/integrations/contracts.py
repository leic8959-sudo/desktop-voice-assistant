"""Stable contracts shared by Gmail, calendars, collaboration and device connectors."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import StrEnum
from typing import Any, Protocol


class PermissionScope(StrEnum):
    READ = "read"
    WRITE = "write"
    SEND = "send"
    DEVICE_CONTROL = "device_control"


@dataclass(frozen=True)
class ConnectorAction:
    name: str
    description: str
    scopes: frozenset[PermissionScope] = frozenset({PermissionScope.READ})
    requires_confirmation: bool = False

    @property
    def is_mutating(self) -> bool:
        return bool(self.scopes & {PermissionScope.WRITE, PermissionScope.SEND, PermissionScope.DEVICE_CONTROL})


@dataclass(frozen=True)
class ConnectorManifest:
    connector_id: str
    display_name: str
    version: str
    actions: tuple[ConnectorAction, ...] = ()
    oauth_scopes: tuple[str, ...] = ()


@dataclass
class ConnectorContext:
    subject: str
    approved_scopes: frozenset[PermissionScope] = frozenset()
    metadata: dict[str, Any] = field(default_factory=dict)


class Connector(Protocol):
    manifest: ConnectorManifest

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]: ...


def action_for(manifest: ConnectorManifest, name: str) -> ConnectorAction:
    for action in manifest.actions:
        if action.name == name:
            return action
    raise KeyError(f"unknown connector action: {manifest.connector_id}.{name}")

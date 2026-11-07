"""Connector registration and permission gate."""

from __future__ import annotations

from .contracts import Connector, ConnectorContext, PermissionScope, action_for


class ConnectorRegistry:
    def __init__(self) -> None:
        self._connectors: dict[str, Connector] = {}

    def register(self, connector: Connector) -> None:
        connector_id = connector.manifest.connector_id
        if not connector_id or connector_id in self._connectors:
            raise ValueError(f"connector already registered: {connector_id}")
        self._connectors[connector_id] = connector

    def get(self, connector_id: str) -> Connector:
        try:
            return self._connectors[connector_id]
        except KeyError as error:
            raise KeyError(f"connector not registered: {connector_id}") from error

    def manifests(self) -> tuple[object, ...]:
        return tuple(connector.manifest for connector in self._connectors.values())

    async def invoke(self, connector_id: str, action: str, arguments: dict, context: ConnectorContext) -> dict:
        connector = self.get(connector_id)
        descriptor = action_for(connector.manifest, action)
        if not descriptor.scopes.issubset(context.approved_scopes):
            missing = sorted(scope.value for scope in descriptor.scopes - context.approved_scopes)
            raise PermissionError(f"connector permission not approved: {', '.join(missing)}")
        if descriptor.requires_confirmation and context.metadata.get("confirmation_verified") is not True:
            raise PermissionError(f"server confirmation required: {connector_id}.{action}")
        return await connector.invoke(action, arguments, context)

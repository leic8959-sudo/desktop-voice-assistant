"""Connector registration and permission gate."""

from __future__ import annotations

import hashlib
import json
import time
from datetime import datetime, timezone

import aiohttp

from .contracts import Connector, ConnectorContext, PermissionScope, action_for
from .action_schemas import normalize_arguments, schema_for


class ConnectorRegistry:
    def __init__(self) -> None:
        self._connectors: dict[str, Connector] = {}
        self.config = None
        self._checks = {}

    def configure(self, config):
        disabled = config.get("disabled_connectors", [])
        if not isinstance(disabled, list) or len(disabled) > 32 or any(not isinstance(item, str) for item in disabled):
            raise ValueError("连接器访问许可配置无效")
        approved = config.get("mcp_approved_tools") or {}
        if not isinstance(approved, dict) or len(approved) > 64:
            raise ValueError("外部能力许可配置无效")
        for name, grant in approved.items():
            if not isinstance(name, str) or not 1 <= len(name) <= 128 or not isinstance(grant, dict):
                raise ValueError("外部能力许可条目无效")
            if grant.get("mode") not in ("read", "confirm") or any(not isinstance(grant.get(key), str) or len(grant[key]) != 64 or any(character not in "0123456789abcdef" for character in grant[key]) for key in ("connection", "fingerprint")):
                raise ValueError("外部能力许可指纹或模式无效")
        self.config = config

    def _signature(self, connector_id):
        from ..infrastructure.config_store import CONNECTOR_CONFIG_FIELDS
        keys = {field["key"] for field in CONNECTOR_CONFIG_FIELDS.get(connector_id, ())}
        prefix = "microsoft_calendar" if connector_id == "microsoft_mail" else connector_id
        keys.update(f"{prefix}_{field}" for field in ("refresh_token", "client_id", "expires_at"))
        values = {key: self.config.get(key) for key in keys} if self.config is not None else {}
        return hashlib.sha256(json.dumps(values, sort_keys=True, default=str).encode()).hexdigest()

    def invalidate(self, connector_id):
        self._checks.pop(connector_id, None)

    def record_check(self, connector_id, status="available", message="本次只读检查成功；不代表所有写入权限已授权"):
        self._checks[connector_id] = {"status": status, "message": message, "checked_at": datetime.now(timezone.utc).isoformat(), "checked_epoch": time.time(), "signature": self._signature(connector_id)}

    def status(self, connector_id):
        from ..infrastructure.settings import connector_configuration
        configured = connector_configuration(self.config or {}).get(connector_id, False)
        base = {"configured": configured, "status": "unverified" if configured else "not_configured", "message": "请主动检查连接；保存配置不会验证授权" if configured else "请先配置凭据或完成提供商授权", "checked_at": None}
        if connector_id in (self.config or {}).get("disabled_connectors", []):
            return {**base, "status": "disabled", "message": "已撤销伊迪斯的访问许可；提供商账号授权需在提供商处另行撤销"}
        checked = self._checks.get(connector_id)
        if configured and checked and checked["signature"] == self._signature(connector_id):
            if time.time() - checked["checked_epoch"] < 900:
                base.update({key: checked[key] for key in ("status", "message", "checked_at")})
            else:
                base.update(status="unverified", checked_at=checked["checked_at"], message="检查结果已超过15分钟，请重新检查")
        prefix = "microsoft_calendar" if connector_id == "microsoft_mail" else connector_id
        expires = (self.config or {}).get(f"{prefix}_expires_at")
        if configured and expires:
            try:
                value = datetime.fromisoformat(str(expires).replace("Z", "+00:00"))
                if value.tzinfo is None or value.timestamp() <= time.time():
                    base.update(status="expired_or_denied", message="令牌已到期或到期时间无效，请续期后重新检查")
            except ValueError:
                base.update(status="expired_or_denied", message="到期时间无效，请使用带时区的时间")
        return base

    def require_enabled(self, connector_id):
        if connector_id in (self.config or {}).get("disabled_connectors", []):
            raise PermissionError("连接器已停用，请由管理员重新允许访问")

    def read_tool_schemas(self):
        result = []
        for connector in self._connectors.values():
            connector_id = connector.manifest.connector_id
            if self.status(connector_id)["status"] != "available":
                continue
            for action in connector.manifest.actions:
                if action.needs_confirmation or not action.scopes.issubset({PermissionScope.READ}):
                    continue
                schema = schema_for(connector_id, action)
                if schema is None:
                    schema = {"type": "object", "properties": {}, "additionalProperties": True}
                result.append({"type": "function", "name": f"connector__{connector_id}__{action.name}", "description": f"{connector.manifest.display_name}：{action.description}", "parameters": schema})
        return result

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
        self.require_enabled(connector_id)
        arguments = normalize_arguments(connector_id, descriptor, arguments)
        if not descriptor.scopes.issubset(context.approved_scopes):
            missing = sorted(scope.value for scope in descriptor.scopes - context.approved_scopes)
            raise PermissionError(f"connector permission not approved: {', '.join(missing)}")
        if descriptor.needs_confirmation and context.metadata.get("confirmation_verified") is not True:
            raise PermissionError(f"server confirmation required: {connector_id}.{action}")
        try:
            result = await connector.invoke(action, arguments, context)
        except Exception as error:
            # 已失败的真实请求立即撤销旧可用快照；原异常继续交给既有日志边界。
            if isinstance(error, (RuntimeError, ConnectionError, TimeoutError, aiohttp.ClientError)) or connector_id == "mcp" and isinstance(error, PermissionError):
                denied = getattr(error, "status", None) in {401, 403}
                self.record_check(connector_id, "expired_or_denied" if denied else "connection_error", "权限不足，请重新授权和检查" if denied else "最近请求未成功，请重新检查连接")
            raise
        if len(json.dumps(result, ensure_ascii=False, default=str).encode()) > 262144:
            raise RuntimeError("连接器返回超过256 KiB，请缩小查询范围")
        return result

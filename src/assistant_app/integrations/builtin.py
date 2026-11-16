"""Adapters for services already present in the assistant runtime."""

from __future__ import annotations

from typing import Any

from .contracts import ConnectorContext, ConnectorManifest, ConnectorAction, PermissionScope
from .action_schemas import SCHEMAS


class GmailConnector:
    manifest = ConnectorManifest(
        connector_id="gmail",
        display_name="Gmail",
        version="1.0",
        oauth_scopes=("gmail.readonly", "gmail.compose"),
        actions=(
            ConnectorAction("search", "搜索邮件"),
            ConnectorAction("read_message", "读取邮件"),
            ConnectorAction("list_drafts", "列出草稿"),
            ConnectorAction("create_draft", "创建草稿", frozenset({PermissionScope.WRITE}), True),
            ConnectorAction("send", "发送邮件", frozenset({PermissionScope.SEND}), True),
        ),
    )

    def __init__(self, service: Any) -> None:
        self.service = service

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "search":
            return await self.service.search(arguments.get("query", ""), page_token=arguments.get("page_token"), limit=min(int(arguments.get("limit", 20)), 50))
        if action == "read_message":
            return await self.service.read_message(arguments["message_id"])
        if action == "list_drafts":
            return await self.service.list_drafts(page_token=arguments.get("page_token"), limit=min(int(arguments.get("limit", 20)), 50))
        if action == "create_draft":
            return await self.service.create_draft(arguments.get("to"), arguments.get("subject"), arguments.get("body"), arguments.get("cc"), arguments.get("bcc"), arguments.get("reply_to_message_id"))
        if action == "send":
            prepared = await self.service.prepare_send(arguments["draft_id"])
            return await self.service.confirm_send(arguments["draft_id"], context.metadata["confirmation_id"], prepared.get("raw_sha256"))
        raise KeyError(action)


class HomeAssistantConnector:
    manifest = ConnectorManifest(
        connector_id="home_assistant",
        display_name="Home Assistant",
        version="1.0",
        actions=(
            ConnectorAction("list_devices", "列出已授权设备", input_schema=SCHEMAS[("home_assistant", "list_devices")]),
            ConnectorAction("get_state", "读取设备状态", input_schema=SCHEMAS[("home_assistant", "get_state")]),
            ConnectorAction("execute", "执行设备动作", frozenset({PermissionScope.DEVICE_CONTROL}), True, SCHEMAS[("home_assistant", "execute")]),
        ),
    )

    def __init__(self, controller: Any) -> None:
        self.controller = controller

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "list_devices":
            return await self.controller.list_home_devices(enabled_only=True, query=arguments.get("query", ""), limit=min(int(arguments.get("limit", 30)), 30))
        if action == "get_state":
            return await self.controller.get_home_state(arguments["device_id"])
        if action == "execute":
            return await self.controller.execute_home_action(arguments["device_id"], arguments["action"], arguments.get("parameters", {}))
        raise KeyError(action)

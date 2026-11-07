"""Slack incoming webhook connector."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class SlackConnector:
    manifest = ConnectorManifest(
        connector_id="slack",
        display_name="Slack Webhook",
        version="1.0",
        actions=(ConnectorAction("send_message", "发送 Slack 文本消息", frozenset({PermissionScope.SEND}), True),),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action != "send_message":
            raise KeyError(action)
        webhook = self.config.get("slack_webhook_url", "")
        if not webhook:
            raise RuntimeError("Slack Webhook 尚未配置")
        text = str(arguments.get("text", "")).strip()
        if not text or len(text) > 4000:
            raise ValueError("Slack 消息不能为空且最多4000个字符")
        async with self.client.post(webhook, json={"text": text}, timeout=aiohttp.ClientTimeout(total=15)) as response:
            if response.status >= 400:
                raise RuntimeError(f"Slack 发送失败: {response.status}")
            return {"sent": True, "provider": "slack"}

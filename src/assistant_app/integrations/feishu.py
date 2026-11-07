"""Feishu bot webhook connector for outbound team notifications."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class FeishuConnector:
    manifest = ConnectorManifest(
        connector_id="feishu",
        display_name="飞书机器人",
        version="1.0",
        actions=(ConnectorAction("send_message", "发送飞书文本消息", frozenset({PermissionScope.SEND}), True),),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action != "send_message":
            raise KeyError(action)
        webhook = self.config.get("feishu_webhook_url", "")
        if not webhook:
            raise RuntimeError("飞书 Webhook 尚未配置")
        text = str(arguments.get("text", "")).strip()
        if not text or len(text) > 4000:
            raise ValueError("飞书消息不能为空且最多4000个字符")
        async with self.client.post(webhook, json={"msg_type": "text", "content": {"text": text}}, timeout=aiohttp.ClientTimeout(total=15)) as response:
            data = await response.json(content_type=None)
            if response.status >= 400 or data.get("code", 0) != 0:
                raise RuntimeError(f"飞书发送失败: {response.status}")
            return {"sent": True, "provider": "feishu", "code": data.get("code", 0)}

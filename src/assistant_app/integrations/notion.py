"""Notion API connector for workspace search and page creation."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class NotionConnector:
    manifest = ConnectorManifest(
        connector_id="notion",
        display_name="Notion",
        version="1.0",
        oauth_scopes=("read_content", "insert_content"),
        actions=(ConnectorAction("search", "搜索 Notion 页面"), ConnectorAction("create_page", "创建 Notion 页面", frozenset({PermissionScope.WRITE}), True)),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def _request(self, method: str, path: str, payload: dict[str, Any] | None = None) -> dict[str, Any]:
        token = self.config.get("notion_access_token", "")
        if not token:
            raise RuntimeError("Notion 尚未配置 Access Token")
        headers = {"Authorization": f"Bearer {token}", "Notion-Version": "2022-06-28", "Content-Type": "application/json"}
        async with self.client.request(method, f"https://api.notion.com/v1{path}", headers=headers, json=payload, timeout=aiohttp.ClientTimeout(total=20)) as response:
            data = await response.json(content_type=None)
            if response.status >= 400:
                raise RuntimeError(f"Notion 请求失败: {response.status}")
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "search":
            query = str(arguments.get("query", "")).strip()
            if not query:
                raise ValueError("query 不能为空")
            data = await self._request("POST", "/search", {"query": query[:200], "page_size": min(int(arguments.get("limit", 20)), 100)})
            items = []
            for item in data.get("results", []):
                properties = item.get("properties") or {}
                title_property = next((value for value in properties.values() if isinstance(value, dict) and value.get("type") == "title"), {})
                title = "".join(part.get("plain_text", "") for part in title_property.get("title", []))
                items.append({"id": item.get("id"), "title": title, "url": item.get("url", "")})
            return {"provider": "notion", "items": items, "next_cursor": data.get("next_cursor")}
        if action == "create_page":
            parent_id = str(arguments.get("parent_id", "")).strip()
            title = str(arguments.get("title", "")).strip()
            content = str(arguments.get("content", "")).strip()
            if not parent_id or not title:
                raise ValueError("parent_id 和 title 不能为空")
            parent_type = arguments.get("parent_type", "page")
            if parent_type not in {"page", "database"}:
                raise ValueError("parent_type 必须是 page 或 database")
            parent = {"page_id": parent_id} if parent_type == "page" else {"database_id": parent_id}
            properties = {"title": {"title": [{"text": {"content": title[:200]}}]}}
            return await self._request("POST", "/pages", {"parent": parent, "properties": properties, "children": [{"object": "block", "type": "paragraph", "paragraph": {"rich_text": [{"type": "text", "text": {"content": content[:2000]}}]}}] if content else []})
        raise KeyError(action)

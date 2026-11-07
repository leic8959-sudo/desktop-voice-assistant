"""Todoist REST connector for task lookup and creation."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class TodoistConnector:
    manifest = ConnectorManifest(
        connector_id="todoist",
        display_name="Todoist",
        version="1.0",
        actions=(ConnectorAction("list_tasks", "查询 Todoist 任务"), ConnectorAction("create_task", "创建 Todoist 任务", frozenset({PermissionScope.WRITE}), True), ConnectorAction("complete_task", "完成 Todoist 任务", frozenset({PermissionScope.WRITE}), True)),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def _request(self, method: str, path: str, payload: dict[str, Any] | None = None, params: dict[str, Any] | None = None) -> Any:
        token = self.config.get("todoist_access_token", "")
        if not token:
            raise RuntimeError("Todoist 尚未配置 Access Token")
        async with self.client.request(method, f"https://api.todoist.com/rest/v2{path}", headers={"Authorization": f"Bearer {token}"}, json=payload, params=params, timeout=aiohttp.ClientTimeout(total=20)) as response:
            if response.status >= 400:
                raise RuntimeError(f"Todoist 请求失败: {response.status}")
            if response.status == 204:
                return {}
            data = await response.json(content_type=None)
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "list_tasks":
            limit = int(arguments.get("limit", 50))
            if not 1 <= limit <= 200:
                raise ValueError("limit 必须在1到200之间")
            data = await self._request("GET", "/tasks", params={"filter": str(arguments.get("filter", ""))[:200], "limit": limit})
            return {"provider": "todoist", "items": [{"id": item.get("id"), "content": item.get("content", ""), "description": item.get("description", ""), "due": (item.get("due") or {}).get("string", ""), "priority": item.get("priority"), "url": item.get("url", "")} for item in data]}
        if action == "create_task":
            content = str(arguments.get("content", "")).strip()
            if not content:
                raise ValueError("content 不能为空")
            priority = int(arguments.get("priority", 1))
            if not 1 <= priority <= 4:
                raise ValueError("priority 必须在1到4之间")
            return await self._request("POST", "/tasks", {"content": content[:500], "description": str(arguments.get("description", ""))[:2000], "due_string": str(arguments.get("due_string", ""))[:100], "priority": priority})
        if action == "complete_task":
            task_id = str(arguments.get("task_id", "")).strip()
            if not task_id:
                raise ValueError("task_id 不能为空")
            await self._request("POST", f"/tasks/{task_id}/close")
            return {"provider": "todoist", "completed": True, "task_id": task_id}
        raise KeyError(action)

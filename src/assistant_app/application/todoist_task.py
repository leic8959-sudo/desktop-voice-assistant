"""Create a reviewed Todoist task from a meeting action item."""

from __future__ import annotations

from ..integrations.contracts import ConnectorContext


async def create_todoist_task(registry, *, content: str, due_string: str = "", priority: int = 1, context: ConnectorContext) -> dict:
    if not content.strip():
        raise ValueError("content 不能为空")
    if context.metadata.get("confirmation_verified") is not True:
        raise PermissionError("创建 Todoist 任务需要服务端确认凭证")
    return await registry.invoke("todoist", "create_task", {"content": content, "due_string": due_string, "priority": priority}, context)

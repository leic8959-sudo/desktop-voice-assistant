"""Create a reviewed GitHub issue from a meeting action item."""

from __future__ import annotations

from ..integrations.contracts import ConnectorContext


async def create_action_item(registry, *, repository: str, title: str, body: str, context: ConnectorContext) -> dict:
    if not repository.strip() or not title.strip():
        raise ValueError("repository 和 title 不能为空")
    if context.metadata.get("confirmation_verified") is not True:
        raise PermissionError("创建 GitHub Issue 需要服务端确认凭证")
    return await registry.invoke("github", "create_issue", {"repository": repository, "title": title, "body": body}, context)

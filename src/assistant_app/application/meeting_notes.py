"""Persist a reviewed meeting note into Notion."""

from __future__ import annotations

from ..integrations.contracts import ConnectorContext


async def save_meeting_notes(registry, *, parent_id: str, parent_type: str = "page", title: str, content: str, context: ConnectorContext) -> dict:
    if not parent_id.strip() or not title.strip() or not content.strip():
        raise ValueError("parent_id、title 和 content 不能为空")
    if context.metadata.get("confirmation_verified") is not True:
        raise PermissionError("保存会议纪要需要服务端确认凭证")
    return await registry.invoke("notion", "create_page", {"parent_id": parent_id, "parent_type": parent_type, "title": title, "content": content}, context)

"""Microsoft Graph mail connector with draft-only writes."""

from __future__ import annotations

from datetime import datetime
from typing import Any
from urllib.parse import urlsplit

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope


class MicrosoftMailConnector:
    manifest = ConnectorManifest(
        connector_id="microsoft_mail",
        display_name="Microsoft 365 邮件",
        version="1.0",
        oauth_scopes=("Mail.Read", "Mail.ReadWrite"),
        actions=(ConnectorAction("search", "搜索 Outlook 邮件"), ConnectorAction("notification_messages", "有界读取通知邮件元数据"), ConnectorAction("create_draft", "创建 Outlook 草稿", frozenset({PermissionScope.WRITE}), True)),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any], token_service=None) -> None:
        self.client = client
        self.config = config
        self.token_service = token_service

    async def _request(self, method: str, path: str, payload: dict[str, Any] | None = None, params: dict[str, Any] | None = None) -> dict[str, Any]:
        token = await self.token_service.ensure_access_token("microsoft_calendar") if self.token_service else self.config.get("microsoft_calendar_access_token", "")
        if not token:
            raise RuntimeError("Microsoft 365 尚未完成 OAuth 授权")
        async with self.client.request(method, f"https://graph.microsoft.com/v1.0{path}", headers={"Authorization": f"Bearer {token}"}, json=payload, params=params, timeout=aiohttp.ClientTimeout(total=20)) as response:
            data = await response.json(content_type=None)
            if response.status >= 400:
                raise RuntimeError(f"Microsoft Mail 请求失败: {response.status}")
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "notification_messages":
            lower, upper = str(arguments.get("received_after", "")), str(arguments.get("received_before", ""))
            for value in (lower, upper):
                parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
                if parsed.tzinfo is None:
                    raise ValueError("邮件水位必须带时区")
            limit = int(arguments.get("limit", 20))
            if not 1 <= limit <= 20:
                raise ValueError("邮件分页上限20")
            params = {"$filter": f"receivedDateTime ge {lower} and receivedDateTime le {upper}", "$orderby": "receivedDateTime asc", "$top": limit, "$select": "id,subject,from,receivedDateTime"}
            path = "/me/mailFolders/inbox/messages"
            if arguments.get("page_token"):
                next_url = urlsplit(str(arguments["page_token"]))
                if next_url.scheme != "https" or next_url.netloc != "graph.microsoft.com" or next_url.path != "/v1.0/me/mailFolders/inbox/messages":
                    raise ValueError("邮件分页游标来源无效")
                path, params = next_url.path[len("/v1.0"):] + "?" + next_url.query, None
            data = await self._request("GET", path, params=params)
            return {"provider": "microsoft_mail", "items": [{"id": item.get("id"), "subject": item.get("subject", ""), "sender": ((item.get("from") or {}).get("emailAddress") or {}).get("address", ""), "received_at": item.get("receivedDateTime")} for item in data.get("value", [])], "next_page_token": data.get("@odata.nextLink")}
        if action == "search":
            query = str(arguments.get("query", "")).strip()
            if not query:
                raise ValueError("query 不能为空")
            limit = int(arguments.get("limit", 20))
            if not 1 <= limit <= 50:
                raise ValueError("limit 必须在1到50之间")
            data = await self._request("GET", "/me/messages", params={"$search": f'"{query[:100]}"', "$top": limit, "$select": "id,subject,from,receivedDateTime,webLink,bodyPreview"})
            return {"provider": "microsoft_mail", "items": [{"id": item.get("id"), "subject": item.get("subject", ""), "sender": ((item.get("from") or {}).get("emailAddress") or {}).get("address", ""), "received_at": item.get("receivedDateTime"), "preview": item.get("bodyPreview", ""), "web_link": item.get("webLink", "")} for item in data.get("value", [])]}
        if action == "create_draft":
            to = str(arguments.get("to", "")).strip()
            subject = str(arguments.get("subject", "")).strip()
            if not to or not subject:
                raise ValueError("to 和 subject 不能为空")
            data = await self._request("POST", "/me/messages", {"subject": subject[:250], "body": {"contentType": "Text", "content": str(arguments.get("body", ""))[:10000]}, "toRecipients": [{"emailAddress": {"address": to}}]})
            return {"provider": "microsoft_mail", "draft_id": data.get("id"), "subject": data.get("subject", subject), "web_link": data.get("webLink", ""), "is_draft": True}
        raise KeyError(action)

    async def check_connection(self) -> None:
        # 验证邮件读取权限；不搜索邮件正文，也不创建草稿。
        await self._request("GET", "/me/mailFolders", params={"$top": 1, "$select": "id"})

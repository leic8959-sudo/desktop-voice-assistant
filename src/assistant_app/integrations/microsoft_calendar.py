"""Microsoft Graph calendar connector."""

from __future__ import annotations

from datetime import datetime
from typing import Any
from urllib.parse import urlsplit

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope
from .action_schemas import SCHEMAS
from .http_json import read_json


class MicrosoftCalendarConnector:
    manifest = ConnectorManifest(
        connector_id="microsoft_calendar",
        display_name="Microsoft 365 日历",
        version="1.0",
        oauth_scopes=("Calendars.Read", "Calendars.ReadWrite"),
        actions=(ConnectorAction("list_events", "查询 Outlook 日程", input_schema=SCHEMAS[("microsoft_calendar", "list_events")]), ConnectorAction("create_event", "创建 Outlook 日程", frozenset({PermissionScope.WRITE}), True, SCHEMAS[("microsoft_calendar", "create_event")])),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any], token_service=None) -> None:
        self.client = client
        self.config = config
        self.token_service = token_service

    async def _request(self, method: str, path: str, payload: dict[str, Any] | None = None, params: dict[str, Any] | None = None) -> dict[str, Any]:
        token = await self.token_service.ensure_access_token("microsoft_calendar") if self.token_service else self.config.get("microsoft_calendar_access_token", "")
        if not token:
            raise RuntimeError("Microsoft 365 日历尚未完成 OAuth 授权")
        async with self.client.request(method, f"https://graph.microsoft.com/v1.0{path}", headers={"Authorization": f"Bearer {token}", "Prefer": 'outlook.timezone="UTC"'}, json=payload, params=params, timeout=aiohttp.ClientTimeout(total=20), allow_redirects=False) as response:
            data = await read_json(response, "Microsoft Calendar")
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "list_events":
            start, end = str(arguments.get("time_min", "")).strip(), str(arguments.get("time_max", "")).strip()
            if not start or not end:
                raise ValueError("time_min 和 time_max 不能为空")
            start_value = datetime.fromisoformat(start.replace("Z", "+00:00"))
            end_value = datetime.fromisoformat(end.replace("Z", "+00:00"))
            if start_value.tzinfo is None or end_value.tzinfo is None:
                raise ValueError("日历查询时间必须携带时区")
            if end_value <= start_value:
                raise ValueError("time_max 必须晚于 time_min")
            limit = int(arguments.get("limit", 20))
            if not 1 <= limit <= 100:
                raise ValueError("limit 必须在1到100之间")
            params = {"startDateTime": start, "endDateTime": end, "$top": limit, "$orderby": "start/dateTime"}
            path = "/me/calendarView"
            if arguments.get("metadata_only"):
                params["$select"] = "id,subject,start,isCancelled,isAllDay,lastModifiedDateTime"
            if arguments.get("page_token"):
                next_url = urlsplit(str(arguments["page_token"]))
                if next_url.scheme != "https" or next_url.netloc != "graph.microsoft.com" or next_url.path != "/v1.0/me/calendarView":
                    raise ValueError("日历分页游标来源无效")
                path, params = next_url.path[len("/v1.0"):] + "?" + next_url.query, None
            data = await self._request("GET", path, params=params)
            if not isinstance(data, dict) or not isinstance(data.get("value", []), list) or len(data.get("value", [])) > limit:
                raise RuntimeError("Microsoft Calendar 列表格式无效或超过请求数量")
            if arguments.get("metadata_only"):
                def utc_start(item):
                    value = (item.get("start") or {}).get("dateTime")
                    if not value:
                        return None
                    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
                    if parsed.tzinfo is None and (item.get("start") or {}).get("timeZone") not in {"UTC", "Etc/UTC", "Etc/GMT"}:
                        raise ValueError("日历返回的时区不是已请求的UTC，不能推测会议时间")
                    return value if parsed.tzinfo else value + "Z"
                return {"provider": "microsoft_calendar", "items": [{"id": item.get("id"), "summary": item.get("subject", ""), "cancelled": bool(item.get("isCancelled")), "all_day": bool(item.get("isAllDay")), "start": utc_start(item), "updated_at": item.get("lastModifiedDateTime")} for item in data.get("value", [])], "next_page_token": data.get("@odata.nextLink")}
            return {"provider": "microsoft_calendar", "items": [{"id": item.get("id"), "summary": item.get("subject", ""), "description": item.get("bodyPreview", ""), "start": (item.get("start") or {}).get("dateTime"), "end": (item.get("end") or {}).get("dateTime"), "location": (item.get("location") or {}).get("displayName", ""), "web_link": item.get("webLink", "")} for item in data.get("value", [])], "next_link": data.get("@odata.nextLink")}
        if action == "create_event":
            for key in ("summary", "start", "end"):
                if not str(arguments.get(key, "")).strip():
                    raise ValueError(f"缺少日程字段: {key}")
            start = datetime.fromisoformat(str(arguments["start"]).replace("Z", "+00:00"))
            end = datetime.fromisoformat(str(arguments["end"]).replace("Z", "+00:00"))
            if start.tzinfo is None or end.tzinfo is None:
                raise ValueError("日程时间必须携带时区")
            if end <= start:
                raise ValueError("日程结束时间必须晚于开始时间")
            data = await self._request("POST", "/me/events", payload={"subject": arguments["summary"], "body": {"contentType": "text", "content": arguments.get("description", "")}, "start": {"dateTime": arguments["start"], "timeZone": arguments.get("time_zone", "UTC")}, "end": {"dateTime": arguments["end"], "timeZone": arguments.get("time_zone", "UTC")}, "location": {"displayName": arguments.get("location", "")}})
            return {"provider": "microsoft_calendar", "id": data.get("id"), "summary": data.get("subject", arguments["summary"]), "start": (data.get("start") or {}).get("dateTime", arguments["start"]), "end": (data.get("end") or {}).get("dateTime", arguments["end"]), "location": (data.get("location") or {}).get("displayName", arguments.get("location", "")), "web_link": data.get("webLink", "")}
        raise KeyError(action)

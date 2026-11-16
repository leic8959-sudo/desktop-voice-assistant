"""Google Calendar REST connector using an injected OAuth access token."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope
from .action_schemas import SCHEMAS
from .http_json import read_json


class GoogleCalendarConnector:
    manifest = ConnectorManifest(
        connector_id="google_calendar",
        display_name="Google Calendar",
        version="1.0",
        oauth_scopes=("https://www.googleapis.com/auth/calendar.readonly", "https://www.googleapis.com/auth/calendar.events"),
        actions=(
            ConnectorAction("list_events", "查询日程", input_schema=SCHEMAS[("google_calendar", "list_events")]),
            ConnectorAction("create_event", "创建日程", frozenset({PermissionScope.WRITE}), True, SCHEMAS[("google_calendar", "create_event")]),
        ),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any], token_service=None) -> None:
        self.client = client
        self.config = config
        self.token_service = token_service

    def _token(self) -> str:
        token = self.config.get("google_calendar_access_token", "")
        if not token:
            raise RuntimeError("Google Calendar 尚未完成 OAuth 授权")
        return token

    async def _request(self, method: str, path: str, *, payload: dict[str, Any] | None = None, params: dict[str, Any] | None = None) -> dict[str, Any]:
        token = await self.token_service.ensure_access_token("google_calendar") if self.token_service else self._token()
        headers = {"Authorization": f"Bearer {token}"}
        async with self.client.request(method, f"https://www.googleapis.com/calendar/v3{path}", headers=headers, json=payload, params=params, timeout=aiohttp.ClientTimeout(total=20), allow_redirects=False) as response:
            data = await read_json(response, "Google Calendar")
            return data

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        if action == "list_events":
            limit = int(arguments.get("limit", 20))
            if not 1 <= limit <= 100:
                raise ValueError("limit 必须在1到100之间")
            now = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
            time_min = arguments.get("time_min", now)
            time_max = arguments.get("time_max")
            for value in (time_min, time_max):
                if value:
                    parsed = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
                    if parsed.tzinfo is None:
                        raise ValueError("日历查询时间必须带时区")
            params = {"timeMin": time_min, "timeMax": time_max, "singleEvents": "true", "orderBy": "startTime", "maxResults": limit}
            if arguments.get("page_token"):
                params["pageToken"] = str(arguments["page_token"])[:4096]
            if arguments.get("metadata_only"):
                params["fields"] = "items(id,summary,status,start,end,updated),nextPageToken"
            data = await self._request("GET", "/calendars/primary/events", params=params)
            if not isinstance(data, dict) or not isinstance(data.get("items", []), list) or len(data.get("items", [])) > limit:
                raise RuntimeError("Google Calendar 列表格式无效或超过请求数量")
            if arguments.get("metadata_only"):
                return {"provider": "google_calendar", "items": [{"id": item.get("id"), "summary": item.get("summary", ""), "status": item.get("status"), "cancelled": item.get("status") == "cancelled", "all_day": not bool((item.get("start") or {}).get("dateTime")), "start": (item.get("start") or {}).get("dateTime"), "updated_at": item.get("updated")} for item in data.get("items", [])], "next_page_token": data.get("nextPageToken")}
            return {"provider": "google_calendar", "items": [{"id": item.get("id"), "summary": item.get("summary", ""), "description": item.get("description", ""), "start": (item.get("start") or {}).get("dateTime") or (item.get("start") or {}).get("date"), "end": (item.get("end") or {}).get("dateTime") or (item.get("end") or {}).get("date"), "location": item.get("location", ""), "html_link": item.get("htmlLink", "")} for item in data.get("items", [])], "next_page_token": data.get("nextPageToken")}
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
            data = await self._request("POST", "/calendars/primary/events", payload={"summary": arguments["summary"], "description": arguments.get("description", ""), "start": {"dateTime": arguments["start"]}, "end": {"dateTime": arguments["end"]}, "location": arguments.get("location", "")})
            return {"provider": "google_calendar", "id": data.get("id"), "summary": data.get("summary", arguments["summary"]), "start": (data.get("start") or {}).get("dateTime", arguments["start"]), "end": (data.get("end") or {}).get("dateTime", arguments["end"]), "location": data.get("location", arguments.get("location", "")), "html_link": data.get("htmlLink", "")}
        raise KeyError(action)

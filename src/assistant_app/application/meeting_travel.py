"""Cross-connector meeting and commute workflow."""

from __future__ import annotations

from datetime import datetime

from ..integrations.contracts import ConnectorContext, PermissionScope


async def plan_meeting_travel(registry, *, calendar_connector: str, time_min: str, time_max: str, origin: str, context: ConnectorContext) -> dict:
    if calendar_connector not in {"google_calendar", "microsoft_calendar"}:
        raise ValueError("calendar_connector 只能是 google_calendar 或 microsoft_calendar")
    start = datetime.fromisoformat(time_min.replace("Z", "+00:00"))
    end = datetime.fromisoformat(time_max.replace("Z", "+00:00"))
    if end <= start:
        raise ValueError("time_max 必须晚于 time_min")
    events_result = await registry.invoke(calendar_connector, "list_events", {"time_min": time_min, "time_max": time_max, "limit": 20}, context)
    planned = []
    for event in events_result.get("items", []):
        location = str(event.get("location", "")).strip()
        item = {"event": event, "route": None}
        if location:
            item["route"] = await registry.invoke("amap", "driving_route", {"origin": origin, "destination": location}, context)
        planned.append(item)
    return {"calendar": calendar_connector, "origin": origin, "items": planned}

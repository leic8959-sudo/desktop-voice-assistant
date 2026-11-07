"""Amap web service connector for POI search and weather lookup."""

from __future__ import annotations

from typing import Any

import aiohttp

from .contracts import ConnectorAction, ConnectorContext, ConnectorManifest


class AmapConnector:
    manifest = ConnectorManifest(
        connector_id="amap",
        display_name="高德地图",
        version="1.0",
        actions=(
            ConnectorAction("search_poi", "搜索地点和周边商户"),
            ConnectorAction("weather", "查询城市天气"),
            ConnectorAction("driving_route", "规划驾车路线"),
        ),
    )

    def __init__(self, client: aiohttp.ClientSession, config: dict[str, Any]) -> None:
        self.client = client
        self.config = config

    async def invoke(self, action: str, arguments: dict[str, Any], context: ConnectorContext) -> dict[str, Any]:
        key = self.config.get("amap_web_service_key", "")
        if not key:
            raise RuntimeError("高德 Web 服务 Key 尚未配置")
        if action == "search_poi":
            keywords = str(arguments.get("keywords", "")).strip()
            if not keywords:
                raise ValueError("keywords 不能为空")
            limit = int(arguments.get("limit", 10))
            if not 1 <= limit <= 25:
                raise ValueError("limit 必须在1到25之间")
            params = {"key": key, "keywords": keywords[:100], "city": str(arguments.get("city", ""))[:40], "page_size": limit, "extensions": "all"}
            path = "/v3/place/text"
        elif action == "weather":
            params = {"key": key, "city": str(arguments["city"])[:40], "extensions": "all"}
            path = "/v3/weather/weatherInfo"
        elif action == "driving_route":
            origin = str(arguments.get("origin", "")).strip()
            destination = str(arguments.get("destination", "")).strip()
            if not origin or not destination:
                raise ValueError("origin 和 destination 不能为空")
            origin = await self._geocode(key, origin)
            destination = await self._geocode(key, destination)
            params = {"key": key, "origin": origin, "destination": destination, "strategy": "0", "extensions": "all"}
            path = "/v3/direction/driving"
        else:
            raise KeyError(action)
        async with self.client.get(f"https://restapi.amap.com{path}", params=params, timeout=aiohttp.ClientTimeout(total=15)) as response:
            data = await response.json(content_type=None)
            if response.status >= 400 or data.get("status") != "1":
                reasons = {"10001": "Key 无效，请核对高德 Web 服务 Key", "10002": "Key 无服务权限，请在高德控制台开通对应 Web 服务", "10003": "调用额度已用尽", "10004": "访问频率超限，请稍后重新读取", "10005": "IP 白名单不匹配，请核对高德控制台配置", "10009": "Key 平台类型不匹配；服务端必须使用 Web 服务 Key，地图 JS Key 不适用", "10012": "服务权限不足，请核对高德控制台授权"}
                raise RuntimeError(reasons.get(str(data.get("infocode", "")), f"高德请求未通过（HTTP {response.status}），请核对 Web 服务 Key、服务权限与额度"))
            if action == "driving_route":
                paths = data.get("route", {}).get("paths", [])
                if not paths:
                    return {"provider": "amap", "found": False, "items": []}
                path = paths[0]
                return {"provider": "amap", "found": True, "distance_m": int(path.get("distance", 0)), "duration_s": int(path.get("duration", 0)), "tolls": path.get("tolls"), "steps": [{"instruction": step.get("instruction"), "distance_m": int(step.get("distance", 0) or 0)} for step in path.get("steps", [])]}
            return data

    async def _geocode(self, key: str, address: str) -> str:
        if "," in address and all(part.strip().replace(".", "", 1).isdigit() for part in address.split(",", 1)):
            return address
        async with self.client.get("https://restapi.amap.com/v3/geocode/geo", params={"key": key, "address": address[:100]}, timeout=aiohttp.ClientTimeout(total=15)) as response:
            data = await response.json(content_type=None)
            geocodes = data.get("geocodes", [])
            if response.status >= 400 or data.get("status") != "1" or not geocodes or not geocodes[0].get("location"):
                raise RuntimeError("高德地址解析失败")
            return geocodes[0]["location"]

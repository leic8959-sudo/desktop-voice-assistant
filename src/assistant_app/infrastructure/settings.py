"""应用配置边界。业务层只依赖 Settings，不直接读取 JSON 或环境变量。"""

from __future__ import annotations

import json
import os
from dataclasses import dataclass
from pathlib import Path


class SettingsError(ValueError):
    """配置缺失或格式不正确。"""


@dataclass(frozen=True)
class Settings:
    host: str
    port: int
    public_base_url: str
    device_id: str
    device_token: str
    provisioning_token: str
    max_connections: int = 1
    session_seconds: int = 600
    idle_seconds: int = 30


def connector_configuration(values: dict) -> dict[str, bool]:
    """Return configured status without exposing any credential values."""
    return {
        "gmail": bool(values.get("gmail_client_id") and values.get("gmail_client_secret")),
        "google_calendar": bool(values.get("google_calendar_access_token")),
        "microsoft_calendar": bool(values.get("microsoft_calendar_access_token")),
        "microsoft_mail": bool(values.get("microsoft_calendar_access_token")),
        "notion": bool(values.get("notion_access_token")),
        "github": bool(values.get("github_access_token")),
        "slack": bool(values.get("slack_webhook_url")),
        "todoist": bool(values.get("todoist_access_token")),
        "feishu": bool(values.get("feishu_webhook_url")),
        "amap": bool(values.get("amap_web_service_key")),
        "home_assistant": bool(values.get("home_assistant_url") and values.get("home_assistant_token")),
    }


def _required_string(values: dict, key: str) -> str:
    value = values.get(key)
    if not isinstance(value, str) or not value or "REPLACE" in value:
        raise SettingsError(f"缺少有效配置: {key}")
    return value


def load_settings(path: str | Path) -> Settings:
    """读取非敏感运行参数；敏感令牌优先由环境变量覆盖。"""
    source = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    values = dict(source)
    for key in ("device_token", "provisioning_token"):
        environment_key = f"ASSISTANT_{key.upper()}"
        if os.getenv(environment_key):
            values[key] = os.environ[environment_key]
    device_token = _required_string(values, "device_token")
    provisioning_token = _required_string(values, "provisioning_token")
    if len(device_token) < 24 or len(provisioning_token) < 24:
        raise SettingsError("设备令牌至少需要24个字符")
    device_id = _required_string(values, "device_id")
    port = values.get("port", 8765)
    if not isinstance(port, int) or not 1 <= port <= 65535:
        raise SettingsError("port 必须是1到65535之间的整数")
    return Settings(
        host=str(values.get("host", "127.0.0.1")),
        port=port,
        public_base_url=_required_string(values, "public_base_url"),
        device_id=device_id,
        device_token=device_token,
        provisioning_token=provisioning_token,
        max_connections=int(values.get("max_connections", 1)),
        session_seconds=int(values.get("session_seconds", 600)),
        idle_seconds=int(values.get("idle_seconds", 30)),
    )

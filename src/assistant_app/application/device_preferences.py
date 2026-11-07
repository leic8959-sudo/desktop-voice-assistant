"""设备音频、显示和免打扰偏好；只保存白名单字段，时间按 Asia/Shanghai 解释。"""

from __future__ import annotations

import json
import os
import tempfile
from datetime import datetime, timezone, timedelta
from datetime import time as clock_time
from pathlib import Path


DEFAULT_DEVICE_PREFERENCES = {
    "conversation_volume": 75,
    "reminder_volume": 75,
    "conversation_muted": False,
    "reminders_muted": False,
    "dnd_enabled": False,
    "quiet_start": "22:00",
    "quiet_end": "07:00",
    "brightness": 75,
    "night_brightness": 35,
    "night_mode_enabled": False,
    "night_start": "22:00",
    "night_end": "07:00",
    "screen_off_seconds": 0,
}
PREFERENCE_KEYS = frozenset((*DEFAULT_DEVICE_PREFERENCES, "timezone"))


def _clock(value: object, key: str) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{key} 必须是 HH:MM")
    try:
        datetime.strptime(value, "%H:%M")
    except ValueError as error:
        raise ValueError(f"{key} 必须是 HH:MM") from error
    return value


def validate_device_preferences(values: dict, current: dict | None = None) -> dict:
    if not isinstance(values, dict) or set(values) - PREFERENCE_KEYS:
        raise ValueError("设备偏好包含未允许的字段")
    merged = {**DEFAULT_DEVICE_PREFERENCES, **(current or {}), **values}
    if merged.get("timezone", "Asia/Shanghai") != "Asia/Shanghai":
        raise ValueError("设备偏好只支持 Asia/Shanghai")
    for key in ("conversation_volume", "reminder_volume", "brightness", "night_brightness"):
        if type(merged[key]) is not int or not 0 <= merged[key] <= 100:
            raise ValueError(f"{key} 必须是0到100的整数")
    if type(merged["screen_off_seconds"]) is not int or not 0 <= merged["screen_off_seconds"] <= 86400:
        raise ValueError("screen_off_seconds 必须是0到86400的整数")
    for key in ("conversation_muted", "reminders_muted", "dnd_enabled", "night_mode_enabled"):
        if type(merged[key]) is not bool:
            raise ValueError(f"{key} 必须是布尔值")
    for key in ("quiet_start", "quiet_end", "night_start", "night_end"):
        merged[key] = _clock(merged[key], key)
    merged["timezone"] = "Asia/Shanghai"
    return {key: merged[key] for key in (*DEFAULT_DEVICE_PREFERENCES, "timezone")}


def is_quiet_now(values: dict, now: datetime | None = None) -> bool:
    now = now or datetime.now(timezone.utc).astimezone(timezone(timedelta(hours=8)))
    start = datetime.strptime(values["quiet_start"], "%H:%M").time()
    end = datetime.strptime(values["quiet_end"], "%H:%M").time()
    current = now.timetz().replace(tzinfo=None)
    return (start <= current < end) if start < end else (current >= start or current < end)


def is_night_now(values: dict, now: datetime | None = None) -> bool:
    start = datetime.strptime(values["night_start"], "%H:%M").time()
    end = datetime.strptime(values["night_end"], "%H:%M").time()
    now = now or datetime.now(timezone.utc).astimezone(timezone(timedelta(hours=8)))
    current = now.timetz().replace(tzinfo=None)
    return (start <= current < end) if start < end else (current >= start or current < end)


class DevicePreferences:
    def __init__(self, runtime_dir):
        self.path = Path(runtime_dir) / "device-preferences.json"
        self.values = self._load()

    def _load(self):
        try:
            if self.path.stat().st_size > 4096:
                raise ValueError("设备偏好文件超过限制")
            raw = json.loads(self.path.read_text(encoding="utf-8"))
        except FileNotFoundError:
            return validate_device_preferences({})
        except (OSError, json.JSONDecodeError) as error:
            raise ValueError("设备偏好文件不可读取") from error
        return validate_device_preferences(raw)

    def snapshot(self):
        return dict(self.values)

    def save(self, values):
        merged = validate_device_preferences(values, self.values)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        descriptor, temporary_name = tempfile.mkstemp(prefix="device-preferences.", suffix=".tmp", dir=self.path.parent)
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(merged, stream, ensure_ascii=False, separators=(",", ":"))
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary_name, self.path)
        finally:
            if os.path.exists(temporary_name):
                os.unlink(temporary_name)
        self.values = merged
        return self.snapshot()

"""原生动作的有界参数；旧连接器可逐步补充，不改变其调用协议。"""

from __future__ import annotations

import json
from datetime import datetime
from typing import Any


def text(description, maximum=4096):
    return {"type": "string", "description": description, "minLength": 1, "maxLength": maximum}


def object_schema(properties, required=()):
    return {"type": "object", "properties": properties, "required": list(required), "additionalProperties": False}


TIME = {**text("带时区的 ISO 8601 时间，例如 2026-11-12T09:00:00+08:00", 64), "format": "date-time"}
EVENT = object_schema({"summary": text("日程标题", 500), "start": TIME, "end": TIME, "description": {"type": "string", "maxLength": 4000}, "location": {"type": "string", "maxLength": 500}}, ("summary", "start", "end"))
EVENT_LIST = {"time_min": TIME, "time_max": TIME, "limit": {"type": "integer", "minimum": 1, "maximum": 100}, "page_token": text("上次查询返回的分页游标", 4096), "metadata_only": {"type": "boolean"}}
SCHEMAS = {
    ("google_calendar", "list_events"): object_schema(EVENT_LIST),
    ("google_calendar", "create_event"): EVENT,
    ("microsoft_calendar", "list_events"): object_schema(EVENT_LIST, ("time_min", "time_max")),
    ("microsoft_calendar", "create_event"): object_schema({**EVENT["properties"], "time_zone": {"type": "string", "enum": ["UTC"]}}, EVENT["required"]),
    ("home_assistant", "list_devices"): object_schema({"query": {"type": "string", "maxLength": 200}, "limit": {"type": "integer", "minimum": 1, "maximum": 30}}),
    ("home_assistant", "get_state"): object_schema({"device_id": text("list_devices 返回的已授权真实设备 ID", 256)}, ("device_id",)),
    ("home_assistant", "execute"): object_schema({"device_id": text("已授权真实设备 ID", 256), "action": text("设备实际支持的动作", 128), "parameters": {"type": "object", "maxProperties": 32}}, ("device_id", "action")),
    ("todoist", "list_tasks"): object_schema({"filter": {"type": "string", "maxLength": 200}, "limit": {"type": "integer", "minimum": 1, "maximum": 200}, "cursor": text("上页返回的 next_cursor", 4096)}),
    ("todoist", "create_task"): object_schema({"content": text("任务内容", 500), "description": {"type": "string", "maxLength": 2000}, "due_string": {"type": "string", "maxLength": 100}, "priority": {"type": "integer", "minimum": 1, "maximum": 4}}, ("content",)),
    ("todoist", "complete_task"): object_schema({"task_id": text("查询返回的真实任务 ID", 128)}, ("task_id",)),
}


def schema_for(connector_id, action):
    return action.input_schema or SCHEMAS.get((connector_id, action.name))


def validate_schema(schema, depth=0):
    if not isinstance(schema, dict) or depth > 12:
        raise ValueError("工具参数定义必须为有界对象")
    allowed = {"type", "properties", "required", "additionalProperties", "items", "enum", "description", "title", "default", "format", "minimum", "maximum", "minLength", "maxLength", "maxItems", "minItems", "maxProperties", "minProperties"}
    if set(schema) - allowed:
        raise ValueError("工具包含暂不支持的参数定义关键字，请提供基本对象参数")
    if "type" in schema and schema["type"] not in ("object", "array", "string", "boolean", "integer", "number", "null"):
        raise ValueError("工具参数类型暂不支持")
    properties = schema.get("properties", {})
    required = schema.get("required", [])
    if not isinstance(properties, dict) or len(properties) > 100 or not isinstance(required, list) or any(not isinstance(key, str) or key not in properties for key in required):
        raise ValueError("工具字段或必需字段定义无效")
    for value in properties.values():
        validate_schema(value, depth + 1)
    if "items" in schema:
        validate_schema(schema["items"], depth + 1)
    additional = schema.get("additionalProperties", True)
    if not isinstance(additional, (bool, dict)):
        raise ValueError("工具额外字段定义无效")
    if isinstance(additional, dict):
        validate_schema(additional, depth + 1)
    if "enum" in schema and (not isinstance(schema["enum"], list) or len(schema["enum"]) > 200):
        raise ValueError("工具枚举定义无效")
    for key in ("minLength", "maxLength", "minItems", "maxItems", "minProperties", "maxProperties"):
        if key in schema and (type(schema[key]) is not int or not 0 <= schema[key] <= 65536):
            raise ValueError("工具参数长度定义无效")
    for key in ("minimum", "maximum"):
        if key in schema and type(schema[key]) not in {int, float}:
            raise ValueError("工具数值边界无效")
    for key in ("description", "title", "format"):
        if key in schema and not isinstance(schema[key], str):
            raise ValueError("工具参数说明无效")
    if schema.get("format") not in {None, "date-time"}:
        raise ValueError("工具参数格式暂不支持")


def validate_value(value: Any, schema: dict, path="arguments", depth=0):
    # 首版仅执行 validate_schema 接受的子集，业务语义仍由提供商验证。
    if depth > 16:
        raise ValueError("参数嵌套过深")
    kind = schema.get("type")
    types = {"object": dict, "array": list, "string": str, "boolean": bool, "integer": int, "number": (int, float), "null": type(None)}
    if isinstance(kind, str) and kind in types and (not isinstance(value, types[kind]) or kind in {"integer", "number"} and isinstance(value, bool)):
        raise ValueError(f"{path} 类型必须为 {kind}")
    if "enum" in schema and value not in schema["enum"]:
        raise ValueError(f"{path} 不在允许选项内")
    if isinstance(value, dict):
        properties = schema.get("properties", {})
        if set(schema.get("required", ())) - set(value):
            raise ValueError(f"{path} 缺少必需字段")
        if not schema.get("minProperties", 0) <= len(value) <= min(schema.get("maxProperties", 100), 100):
            raise ValueError(f"{path} 字段数量超限")
        if schema.get("additionalProperties") is False and set(value) - set(properties):
            raise ValueError(f"{path} 含有未允许字段")
        for key, item in value.items():
            additional = schema.get("additionalProperties")
            validate_value(item, properties.get(key, additional if isinstance(additional, dict) else {}), f"{path}.{key}", depth + 1)
    elif isinstance(value, list):
        if not schema.get("minItems", 0) <= len(value) <= min(schema.get("maxItems", 200), 200):
            raise ValueError(f"{path} 数量超限")
        for item in value:
            validate_value(item, schema.get("items", {}), path, depth + 1)
    elif isinstance(value, str):
        if not schema.get("minLength", 0) <= len(value) <= schema.get("maxLength", 32768):
            raise ValueError(f"{path} 文本长度超限或为空")
        if schema.get("format") == "date-time":
            parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
            if parsed.tzinfo is None:
                raise ValueError(f"{path} 必须携带时区")
    elif type(value) in {int, float}:
        if not schema.get("minimum", float("-inf")) <= value <= schema.get("maximum", float("inf")):
            raise ValueError(f"{path} 数值超出范围")


def normalize_arguments(connector_id, action, arguments):
    if not isinstance(arguments, dict):
        raise ValueError("arguments 必须为对象")
    encoded = json.dumps(arguments, ensure_ascii=False, allow_nan=False)
    if len(encoded.encode("utf-8")) > 65536:
        raise ValueError("参数总大小超过64 KiB")
    result = json.loads(encoded)
    schema = schema_for(connector_id, action)
    if (connector_id, action.name) in SCHEMAS:
        result = {key: value for key, value in result.items() if value is not None or key in schema.get("required", ()) or key not in schema.get("properties", {})}
    validate_value(result, schema or {"type": "object"})
    return result

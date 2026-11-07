"""JSON tool contracts and a small dispatch helper for the voice gateway."""

from __future__ import annotations

from typing import Any

from .store import AssistantStore
from .project_tasks import create_project_tasks, update_task_progress


def _object(properties: dict[str, Any], required: list[str] | None = None) -> dict[str, Any]:
    return {"type": "object", "properties": properties, "required": required or [], "additionalProperties": False}


TOOL_SCHEMAS = [
    {"type": "function", "name": "create_project_tasks", "description": "按用户明确要求为同一项目原子记录1至20件事，返回永久不复用的项目内position序号。项目名和事项必须真实来自用户；明确时间带时区，未明确具体时刻先询问。重放同一幂等键只返回当前状态，不覆盖后续进度、完成或改期。", "parameters": _object({"project": {"type": "string", "minLength": 1, "maxLength": 80}, "tasks": {"type": "array", "minItems": 1, "maxItems": 20, "items": _object({"title": {"type": "string", "minLength": 1, "maxLength": 200}, "due_at": {"type": ["string", "null"]}, "progress": {"type": "string", "maxLength": 1000}}, ["title"])}, "idempotency_key": {"type": "string", "minLength": 1, "maxLength": 128}}, ["project", "tasks", "idempotency_key"])},
    {"type": "function", "name": "update_task_progress", "description": "保存用户实际报告的项目任务最新进度（覆盖上一条摘要，空字符串清除）；先list_tasks查询真实task_id和updated_at，按metadata.position识别第几项。只支持未完成任务；提醒正在投递时拒绝，冲突先重新查询，不自动重试写入。不会完成任务或改变提醒时间。", "parameters": _object({"task_id": {"type": "string"}, "expected_updated_at": {"type": "string"}, "progress": {"type": "string", "maxLength": 1000}}, ["task_id", "expected_updated_at", "progress"])},
    {"type": "function", "name": "remember_fact", "description": "仅保存用户明确授权的事实或偏好。", "parameters": _object({"key": {"type": "string", "minLength": 1, "maxLength": 64}, "value": {"type": "string", "minLength": 1, "maxLength": 2048}}, ["key", "value"])},
    {"type": "function", "name": "recall_memory", "description": "读取当前设备用户的记忆。", "parameters": _object({"key": {"type": "string", "minLength": 1, "maxLength": 64}, "limit": {"type": "integer", "minimum": 1, "maximum": 100}, "cursor": {"type": "string"}})},
    {"type": "function", "name": "update_memory", "description": "更新一条已存在且由用户授权的记忆。", "parameters": _object({"memory_id": {"type": "string"}, "key": {"type": "string", "minLength": 1, "maxLength": 64}, "value": {"type": "string", "minLength": 1, "maxLength": 2048}}, ["value"])},
    {"type": "function", "name": "delete_memory", "description": "删除一条记忆。", "parameters": _object({"memory_id": {"type": "string"}, "key": {"type": "string", "minLength": 1, "maxLength": 64}})},
    {"type": "function", "name": "create_task", "description": "创建待办；due_at为空表示无提醒待办，否则必须是带时区的ISO8601。", "parameters": _object({"title": {"type": "string", "minLength": 1, "maxLength": 200}, "due_at": {"type": ["string", "null"]}, "idempotency_key": {"type": "string", "minLength": 1, "maxLength": 128}, "metadata": {"type": ["object", "null"]}}, ["title", "idempotency_key"])},
    {"type": "function", "name": "list_tasks", "description": "按游标分页读取待办；project为精确项目名；项目序号取metadata.position，禁止用当前行号。按提醒日期查询可传due_from含起点、due_before不含终点，均为带时区ISO8601，日期范围不包含未设提醒时间的待办。", "parameters": _object({"status": {"type": "string", "enum": ["pending", "completed", "cancelled"]}, "limit": {"type": "integer", "minimum": 1, "maximum": 100}, "cursor": {"type": "string"}, "due_from": {"type": "string"}, "due_before": {"type": "string"}, "project": {"type": "string", "minLength": 1, "maxLength": 80}})},
    {"type": "function", "name": "complete_task", "description": "完成一条待办。", "parameters": _object({"task_id": {"type": "string"}}, ["task_id"])},
    {"type": "function", "name": "cancel_task", "description": "取消一条待办。", "parameters": _object({"task_id": {"type": "string"}}, ["task_id"])},
    {"type": "function", "name": "rename_task", "description": "按用户明确要求修改未完成待办的标题，仅支持未定时或尚未到期且未进入投递的任务；保持原ID、提醒时间和投递状态，不删除重建。先list_tasks查询真实task_id和updated_at；目标或新标题含糊先问清。已到期、投递中、已完成或已取消不可改名；冲突后刷新核对，不自动重试。", "parameters": _object({"task_id": {"type": "string"}, "expected_updated_at": {"type": "string", "description": "实际记录的updated_at，原样传入。"}, "title": {"type": "string", "minLength": 1, "maxLength": 200}}, ["task_id", "expected_updated_at", "title"])},
    {"type": "function", "name": "snooze_reminder", "description": "用户要求稍后再提醒时，延后已到期且未完成的提醒。先用list_tasks查询真实task_id和updated_at，多个候选先询问；未说明延后多久时默认5分钟。", "parameters": _object({"task_id": {"type": "string"}, "expected_updated_at": {"type": "string", "description": "list_tasks返回的该提醒updated_at，原样传入，禁止编造。"}, "minutes": {"type": "integer", "minimum": 1, "maximum": 60, "default": 5}}, ["task_id", "expected_updated_at"])},
    {"type": "function", "name": "reschedule_task", "description": "按用户明确要求，给已有未定时待办添加未来提醒，或修改尚未到期的单次提醒时间；只处理未完成任务并保留原task_id，不删除重建。先list_tasks查询真实task_id和updated_at；目标或新时间不明确先询问。已到期提醒用snooze_reminder，不能以空时间删除提醒。", "parameters": _object({"task_id": {"type": "string"}, "expected_updated_at": {"type": "string", "description": "实际记录的updated_at，原样传入。"}, "due_at": {"type": "string", "description": "严格晚于当前时刻且带时区的ISO8601。"}}, ["task_id", "expected_updated_at", "due_at"])},
]


def execute_tool(store: AssistantStore, name: str, arguments: dict[str, Any], *, authorized: bool = True) -> dict[str, Any]:
    """Dispatch validated JSON-like arguments; gateway owns auth and error reporting."""
    if not isinstance(arguments, dict):
        raise ValueError("工具参数必须是对象")
    if name == "create_project_tasks":
        return create_project_tasks(store, arguments.get("project"), arguments.get("tasks"), arguments.get("idempotency_key"))
    if name == "update_task_progress":
        return update_task_progress(store, arguments.get("task_id"), arguments.get("expected_updated_at"), arguments.get("progress"))
    if name == "remember_fact":
        return store.remember_fact(arguments.get("key"), arguments.get("value"), authorized=authorized)
    if name == "recall_memory":
        result = store.recall_memory(arguments.get("key"), limit=arguments.get("limit", 10), cursor=arguments.get("cursor"))
        # Keep the old gateway shape available while new callers use memories.
        result["facts"] = {item["key"]: item["value"] for item in result["memories"]}
        result["storage"] = "pc_prototype"
        return result
    if name == "update_memory":
        return store.update_memory(arguments.get("memory_id"), key=arguments.get("key"), value=arguments.get("value"), authorized=authorized)
    if name == "delete_memory":
        return store.delete_memory(arguments.get("memory_id"), key=arguments.get("key"))
    if name == "create_task":
        return store.create_task(arguments.get("title"), arguments.get("due_at"), arguments.get("idempotency_key"), metadata=arguments.get("metadata"))
    if name == "list_tasks":
        return store.list_tasks(arguments.get("status"), limit=arguments.get("limit", 50), cursor=arguments.get("cursor"), due_from=arguments.get("due_from"), due_before=arguments.get("due_before"), project=arguments.get("project"))
    if name == "complete_task":
        return store.complete_task(arguments.get("task_id"))
    if name == "cancel_task":
        return store.cancel_task(arguments.get("task_id"))
    if name == "rename_task":
        return store.rename_task(arguments.get("task_id"), arguments.get("expected_updated_at"), arguments.get("title"))
    if name == "snooze_reminder":
        return store.snooze_reminder(arguments.get("task_id"), arguments.get("expected_updated_at"), minutes=arguments.get("minutes", 5))
    if name == "reschedule_task":
        return store.reschedule_task(arguments.get("task_id"), arguments.get("expected_updated_at"), arguments.get("due_at"))
    raise ValueError("工具不在允许列表")

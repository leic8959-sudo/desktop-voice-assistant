"""项目待办沿用 tasks 和提醒租约；表与索引必须由离线迁移显式准备。"""

from __future__ import annotations

import hashlib
import json
import uuid
from datetime import datetime, timedelta
from typing import Any

from .store import AssistantStore, IdempotencyConflict, NotFoundError, _check_text, _decode_cursor, _encode_cursor, _iso, _next_task_revision, _parse_time, _utc_now

PROJECT_INDEX = "tasks_device_project_due"
PROJECT_EXPRESSION = "json_extract(metadata,'$.project')"

PROJECT_TASK_INSTRUCTIONS = (
    "用户明确要求记录某项目的多件事时，用create_project_tasks一次原子保存1至20项；"
    "项目名及每项内容取自用户，不把记录任务当成执行项目授权。没有给出事项内容时先询问，不能编造三件事。"
    "项目进度承接用list_tasks的project精确查询，按metadata.position识别原始第几项，不能用按提醒时间排序后的行号。"
    "先查询真实task_id和updated_at，再完成、取消、改名、改期或用update_task_progress更新进度。"
    "只记用户实际报告的进度，未完成进度不能标完成；查项目整体进度时不只读取pending。"
    "项目和目标不明、明天下午或周末等时间没有确定具体钟点时先询问，按Asia/Shanghai解析明确时间。"
    "设备提醒包含该项最新保存进度摘要，完整内容仍以查询结果为准；保存不等于已提醒，不能宣称已执行真实项目工作。"
)


class ProjectStorageUnavailable(ValueError):
    """缺少显式离线迁移；此时项目写入完全不执行。"""


def require_project_storage(store: AssistantStore) -> None:
    with store._lock:
        names = {row[0] for row in store.connection.execute(
            "SELECT name FROM sqlite_master WHERE name IN (?,?,?,?) LIMIT 4",
            (PROJECT_INDEX, "tasks_device_project_status_due", "project_task_sequences", "project_task_batches"),
        ).fetchall()}
    if len(names) != 4:
        raise ProjectStorageUnavailable("项目待办存储尚未准备，请先由管理员离线执行项目待办迁移；普通待办仍可使用")


def _child_key(key: str, index: int) -> str:
    return "project-task-" + hashlib.sha256(f"{key}:{index}".encode("utf-8")).hexdigest()


def create_project_tasks(store: AssistantStore, project: str, tasks: list[dict[str, Any]], idempotency_key: str) -> dict[str, Any]:
    project = _check_text(project, "project", 80)
    key = _check_text(idempotency_key, "idempotency_key", 128)
    if not isinstance(tasks, list) or not 1 <= len(tasks) <= 20:
        raise ValueError("项目事项一次需为1至20项")
    normalized = []
    for item in tasks:
        if not isinstance(item, dict) or set(item) - {"title", "due_at", "progress"}:
            raise ValueError("每项只接受title、due_at和progress")
        due = _parse_time(item["due_at"], "due_at") if item.get("due_at") is not None else None
        progress = item.get("progress", "")
        if not isinstance(progress, str) or len(progress.strip()) > 1000:
            raise ValueError("progress 必须是不超过1000字的字符串")
        normalized.append({"title": _check_text(item.get("title"), "title", 200), "due_at": _iso(due) if due else None, "progress": progress.strip()})
    fingerprint = hashlib.sha256(json.dumps([project, normalized], ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")).hexdigest()
    require_project_storage(store)
    with store._transaction(immediate=True) as connection:
        now = _utc_now()
        receipt = connection.execute("SELECT request_hash FROM project_task_batches WHERE device_id=? AND idempotency_key=?", (store.device_id, key)).fetchone()
        child_keys = [_child_key(key, index) for index in range(len(normalized))]
        placeholders = ",".join("?" for _ in child_keys)
        rows = connection.execute("SELECT * FROM tasks WHERE device_id=? AND idempotency_key IN (" + placeholders + ") LIMIT 20", [store.device_id, *child_keys]).fetchall()
        if receipt or rows:
            if receipt and receipt["request_hash"] != fingerprint:
                raise IdempotencyConflict("项目批次幂等键已用于不同请求")
            by_key = {row["idempotency_key"]: row for row in rows}
            if len(rows) != len(normalized) or any(json.loads(row["metadata"] or "{}").get("project_request") != fingerprint for row in rows):
                raise IdempotencyConflict("原项目批次已变化或部分归档，请查询核对；未重新创建")
            return {"project": project, "tasks": [store._task_dict(by_key[child_key]) for child_key in child_keys], "replayed": True}
        # 子任务唯一键在回执180天清理后仍阻止给尚存任务重复建档。
        connection.execute("DELETE FROM project_task_batches WHERE rowid IN (SELECT rowid FROM project_task_batches WHERE device_id=? AND expires_at<=? ORDER BY expires_at LIMIT 100)", (store.device_id, _iso(now)))
        sequence = connection.execute("SELECT next_position FROM project_task_sequences WHERE device_id=? AND project=?", (store.device_id, project)).fetchone()
        position = sequence["next_position"] if sequence else 1
        if sequence:
            connection.execute("UPDATE project_task_sequences SET next_position=? WHERE device_id=? AND project=?", (position + len(normalized), store.device_id, project))
        else:
            connection.execute("INSERT INTO project_task_sequences(device_id,project,next_position) VALUES(?,?,?)", (store.device_id, project, position + len(normalized)))
        created = []
        for index, item in enumerate(normalized):
            task_id = uuid.uuid4().hex
            metadata = {"project": project, "position": position + index, "progress": item["progress"], "project_request": fingerprint}
            metadata_text = json.dumps(metadata, ensure_ascii=False, separators=(",", ":"))
            if len(metadata_text.encode("utf-8")) > 4096:
                raise ValueError("项目任务metadata不能超过4096字节")
            connection.execute("INSERT INTO tasks(task_id,device_id,title,due_at,status,reminder_state,idempotency_key,metadata,created_at,updated_at) VALUES(?,?,?,?,'pending','pending',?,?,?,?)", (task_id, store.device_id, item["title"], item["due_at"], child_keys[index], metadata_text, _iso(now), _iso(now)))
            created.append(store._task_dict(connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()))
        connection.execute("INSERT INTO project_task_batches(device_id,idempotency_key,request_hash,expires_at) VALUES(?,?,?,?)", (store.device_id, key, fingerprint, _iso(now + timedelta(days=180))))
    return {"project": project, "tasks": created, "replayed": False}


def list_project_tasks(store: AssistantStore, project: str, *, status: str | None, limit: int, cursor: str | None, start: datetime | None, end: datetime | None) -> dict[str, Any]:
    project = _check_text(project, "project", 80)
    require_project_storage(store)
    index = "tasks_device_project_status_due" if status else PROJECT_INDEX
    base = ["device_id=?", PROJECT_EXPRESSION + "=?"]
    parameters: list[Any] = [store.device_id, project]
    if status:
        base.append("status=?")
        parameters.append(status)
    ranged = start is not None or end is not None
    boundary = _decode_cursor(cursor) if cursor else None
    if ranged and boundary and boundary[0] == "__NULL__":
        return {"tasks": [], "next_cursor": None}
    rows = []
    with store._lock:
        # 分开读取定时/未定时段，游标直接seek；禁止OR跨段导致扫描已翻过的项目历史。
        if boundary is None or boundary[0] != "__NULL__":
            conditions = [*base, "(due_at IS NULL)=0", "due_at IS NOT NULL"]
            values = list(parameters)
            cursor_after_start = boundary is not None and (start is None or _parse_time(boundary[0], "cursor") >= start)
            if start is not None and not cursor_after_start:
                conditions.append("due_at>=?")
                values.append(_iso(start))
            if end is not None:
                conditions.append("due_at<?")
                values.append(_iso(end))
            if cursor_after_start:
                conditions.append("(due_at,task_id)>(?,?)")
                values.extend(boundary)
            rows = store.connection.execute("SELECT * FROM tasks INDEXED BY " + index + " WHERE " + " AND ".join(conditions) + " ORDER BY due_at,task_id LIMIT ?", [*values, limit + 1]).fetchall()
        if not ranged and len(rows) < limit + 1:
            conditions = [*base, "(due_at IS NULL)=1", "due_at IS NULL"]
            values = list(parameters)
            if boundary and boundary[0] == "__NULL__":
                conditions.append("task_id>?")
                values.append(boundary[1])
            rows += store.connection.execute("SELECT * FROM tasks INDEXED BY " + index + " WHERE " + " AND ".join(conditions) + " ORDER BY task_id LIMIT ?", [*values, limit + 1 - len(rows)]).fetchall()
    page = rows[:limit]
    next_cursor = _encode_cursor((page[-1]["due_at"] or "__NULL__", page[-1]["task_id"])) if len(rows) > limit else None
    return {"tasks": [store._task_dict(row) for row in page], "next_cursor": next_cursor}


def update_task_progress(store: AssistantStore, task_id: str, expected_updated_at: str, progress: str) -> dict[str, Any]:
    task_id = _check_text(task_id, "task_id", 64)
    expected_updated_at = _check_text(expected_updated_at, "expected_updated_at", 64)
    if not isinstance(progress, str) or len(progress.strip()) > 1000:
        raise ValueError("progress 必须是不超过1000字的字符串，空字符串用于清除")
    require_project_storage(store)
    with store._transaction(immediate=True) as connection:
        row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (store.device_id, task_id)).fetchone()
        if row is None:
            raise NotFoundError("项目任务不存在")
        metadata = json.loads(row["metadata"] or "{}")
        if not metadata.get("project"):
            raise ValueError("该任务未关联项目")
        now = _utc_now()
        active_lease = row["reminder_state"] == "leased" and row["lease_until"] and _parse_time(row["lease_until"], "lease_until") > now
        if row["updated_at"] != expected_updated_at or active_lease:
            raise IdempotencyConflict("任务已变化或提醒正在投递，请稍后查询核对；未修改进度")
        if row["status"] != "pending":
            raise ValueError("只能更新未完成项目任务的进度")
        metadata["progress"] = progress.strip()
        metadata_text = json.dumps(metadata, ensure_ascii=False, separators=(",", ":"))
        if len(metadata_text.encode("utf-8")) > 4096:
            raise ValueError("项目任务metadata不能超过4096字节")
        reminder_state = "pending" if row["reminder_state"] == "leased" else row["reminder_state"]
        connection.execute("UPDATE tasks SET metadata=?,updated_at=?,reminder_state=?,lease_token=NULL,lease_until=NULL WHERE device_id=? AND task_id=?", (metadata_text, _next_task_revision(row["updated_at"], now), reminder_state, store.device_id, task_id))
        result = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
    return store._task_dict(result)


def reminder_title(item: dict[str, Any]) -> str:
    metadata = item.get("metadata") or {}
    if not isinstance(metadata, dict) or not isinstance(metadata.get("project"), str) or type(metadata.get("position")) is not int or metadata["position"] < 1:
        return item["title"]
    def brief(value: str, maximum: int) -> str:
        text = " ".join(value.split())
        if len(text.encode("utf-8")) <= maximum:
            return text
        return text.encode("utf-8")[:maximum - 3].decode("utf-8", errors="ignore") + "…"
    # 固件提醒卡标题容量181字节；分别为项目、事项和进度留出空间。
    title = f"{brief(metadata['project'], 24)}·第{metadata['position']}项：{brief(item['title'], 48)}"
    if isinstance(metadata.get("progress"), str) and metadata["progress"]:
        title += f"；上次：{brief(metadata['progress'], 80)}"
    return brief(title, 180)

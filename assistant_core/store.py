"""SQLite backed assistant tasks, reminders, and user memories."""

from __future__ import annotations

import base64
import json
import logging
import secrets
import sqlite3
import threading
import uuid
from contextlib import contextmanager
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Iterator
from zoneinfo import ZoneInfo

LOGGER = logging.getLogger(__name__)
UTC = timezone.utc


class NotFoundError(ValueError):
    """Requested memory, task, or reminder lease does not exist."""


class IdempotencyConflict(ValueError):
    """An idempotency key was reused for a different task."""


class LeaseConflict(ValueError):
    """The reminder lease is missing, expired, or owned by another claimant."""


def _utc_now() -> datetime:
    return datetime.now(UTC)


def _parse_time(value: Any, field: str) -> datetime:
    if isinstance(value, datetime):
        result = value
    elif isinstance(value, str) and 1 <= len(value) <= 80:
        text = value.strip()
        if text.endswith("Z"):
            text = text[:-1] + "+00:00"
        try:
            result = datetime.fromisoformat(text)
        except ValueError as exc:
            raise ValueError(f"{field} 必须是带时区的 ISO8601") from exc
    else:
        raise ValueError(f"{field} 必须是带时区的 ISO8601")
    if result.tzinfo is None or result.utcoffset() is None:
        raise ValueError(f"{field} 必须带时区，禁止使用 naive 时间")
    return result.astimezone(UTC)


def _iso(value: datetime) -> str:
    return value.astimezone(UTC).isoformat(timespec="microseconds").replace("+00:00", "Z")


def _next_task_revision(previous: str, now: datetime) -> str:
    return _iso(max(now, _parse_time(previous, "updated_at") + timedelta(microseconds=1)))


def _check_text(value: Any, field: str, maximum: int) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{field} 必须是字符串")
    value = value.strip()
    if not 1 <= len(value) <= maximum:
        raise ValueError(f"{field} 长度需为1～{maximum}")
    return value


def _check_memory_key(value: Any) -> str:
    key = _check_text(value, "key", 64)
    lowered = key.casefold().replace("-", "_")
    sensitive_terms = ("password", "passwd", "secret", "token", "api_key", "access_key", "private_key")
    if any(term in lowered for term in sensitive_terms):
        raise ValueError("不能把密码、令牌或密钥写入记忆")
    return key


def _encode_cursor(values: tuple[str, str]) -> str:
    payload = json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return base64.urlsafe_b64encode(payload).decode("ascii")


def _decode_cursor(cursor: Any) -> tuple[str, str]:
    if not isinstance(cursor, str) or not cursor:
        raise ValueError("cursor 无效")
    try:
        values = json.loads(base64.urlsafe_b64decode(cursor.encode("ascii")).decode("utf-8"))
    except (ValueError, UnicodeError) as exc:
        raise ValueError("cursor 无效") from exc
    if not isinstance(values, list) or len(values) != 2 or not all(isinstance(item, str) for item in values):
        raise ValueError("cursor 无效")
    return values[0], values[1]


class AssistantStore:
    """Single-device assistant store; each instance is scoped to one device identity."""

    def __init__(
        self,
        path: str | Path,
        device_id: str = "default",
        *,
        business_timezone: str = "Asia/Shanghai",
        max_memories: int = 100,
        memory_ttl_days: int = 180,
    ) -> None:
        self.path = str(path)
        self.device_id = _check_text(device_id, "device_id", 128)
        try:
            self.business_timezone = ZoneInfo(business_timezone)
        except Exception as exc:
            raise ValueError("business_timezone 无效") from exc
        if not isinstance(max_memories, int) or not 1 <= max_memories <= 10000:
            raise ValueError("max_memories 超出范围")
        if not isinstance(memory_ttl_days, int) or not 1 <= memory_ttl_days <= 3650:
            raise ValueError("memory_ttl_days 超出范围")
        self.max_memories = max_memories
        self.memory_ttl_days = memory_ttl_days
        self._lock = threading.RLock()
        self.connection = sqlite3.connect(self.path, timeout=5, check_same_thread=False)
        self.connection.row_factory = sqlite3.Row
        self.connection.execute("PRAGMA foreign_keys = ON")
        self.connection.execute("PRAGMA busy_timeout = 5000")
        self.connection.execute("PRAGMA journal_mode = WAL")
        self._initialize()

    def _initialize(self) -> None:
        with self._lock:
            self.connection.executescript(
                """
                CREATE TABLE IF NOT EXISTS memories (
                    memory_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    key TEXT NOT NULL,
                    value TEXT NOT NULL,
                    authorized INTEGER NOT NULL CHECK (authorized = 1),
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL,
                    expires_at TEXT,
                    UNIQUE(device_id, key)
                );
                CREATE INDEX IF NOT EXISTS memories_device_updated
                    ON memories(device_id, updated_at DESC, memory_id DESC);
                CREATE INDEX IF NOT EXISTS memories_device_expiry
                    ON memories(device_id, expires_at);
                CREATE TABLE IF NOT EXISTS assistant_migrations (
                    device_id TEXT NOT NULL,
                    migration_key TEXT NOT NULL,
                    migrated_at TEXT NOT NULL,
                    PRIMARY KEY(device_id, migration_key)
                );
                CREATE TABLE IF NOT EXISTS tasks (
                    task_id TEXT PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    title TEXT NOT NULL,
                    due_at TEXT,
                    status TEXT NOT NULL CHECK (status IN ('pending', 'completed', 'cancelled')),
                    reminder_state TEXT NOT NULL CHECK (reminder_state IN ('pending', 'leased', 'acked')),
                    lease_token TEXT,
                    lease_until TEXT,
                    idempotency_key TEXT NOT NULL,
                    metadata TEXT,
                    created_at TEXT NOT NULL,
                    updated_at TEXT NOT NULL,
                    completed_at TEXT,
                    cancelled_at TEXT,
                    UNIQUE(device_id, idempotency_key)
                );
                CREATE INDEX IF NOT EXISTS tasks_device_due
                    ON tasks(device_id, due_at, task_id);
                CREATE INDEX IF NOT EXISTS tasks_device_status_due
                    ON tasks(device_id, status, due_at, task_id);
                CREATE INDEX IF NOT EXISTS tasks_device_updated
                    ON tasks(device_id, updated_at, task_id);
                CREATE INDEX IF NOT EXISTS tasks_order_with_nulls
                    ON tasks(device_id, (due_at IS NULL), due_at, task_id);
                CREATE INDEX IF NOT EXISTS tasks_due_claim
                    ON tasks(device_id, status, reminder_state, due_at, lease_until);
                CREATE INDEX IF NOT EXISTS tasks_pending_due
                    ON tasks(device_id, due_at, task_id)
                    WHERE status='pending' AND reminder_state='pending' AND due_at IS NOT NULL;
                CREATE INDEX IF NOT EXISTS tasks_leased_until
                    ON tasks(device_id, lease_until, task_id)
                    WHERE status='pending' AND reminder_state='leased';
                """
            )
            self._migrate_legacy_facts()
            self.connection.commit()

    def _migrate_legacy_facts(self) -> None:
        row = self.connection.execute(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='facts'"
        ).fetchone()
        if not row:
            return
        migrated = self.connection.execute(
            "SELECT 1 FROM assistant_migrations WHERE device_id=? AND migration_key=?",
            (self.device_id, "facts-v1"),
        ).fetchone()
        if migrated:
            return
        columns = {item[1] for item in self.connection.execute("PRAGMA table_info(facts)").fetchall()}
        if not {"device", "key", "value"}.issubset(columns):
            LOGGER.warning("legacy facts table schema not recognized; migration skipped")
            return
        self.connection.execute("CREATE INDEX IF NOT EXISTS facts_device_key ON facts(device, key)")
        now = _iso(_utc_now())
        last_rowid = 0
        while True:
            facts = self.connection.execute(
                "SELECT rowid,device,key,value,updated FROM facts WHERE device=? AND rowid>? ORDER BY rowid LIMIT 100",
                (self.device_id, last_rowid),
            ).fetchall()
            if not facts:
                break
            for fact in facts:
                last_rowid = fact[0]
                _, device, key, value, updated = fact
                if not isinstance(key, str) or not isinstance(value, str):
                    continue
                try:
                    key = _check_memory_key(key)
                    value = _check_text(value, "value", 2048)
                    updated_at = updated if isinstance(updated, str) and updated else now
                    expiry = _iso(_parse_time(updated_at, "updated") + timedelta(days=self.memory_ttl_days))
                except ValueError:
                    continue
                self.connection.execute(
                    """INSERT OR IGNORE INTO memories
                    (memory_id,device_id,key,value,authorized,created_at,updated_at,expires_at)
                    VALUES (?,?,?,?,1,?,?,?)""",
                    (uuid.uuid4().hex, device, key, value, updated_at, updated_at, expiry),
                )
        self.connection.execute(
            "INSERT INTO assistant_migrations(device_id,migration_key,migrated_at) VALUES(?,?,?)",
            (self.device_id, "facts-v1", now),
        )

    @contextmanager
    def _transaction(self, immediate: bool = False) -> Iterator[sqlite3.Connection]:
        with self._lock:
            savepoint = "memory_" + uuid.uuid4().hex if self.connection.in_transaction else None
            try:
                self.connection.execute("SAVEPOINT " + savepoint if savepoint else "BEGIN IMMEDIATE" if immediate else "BEGIN")
                yield self.connection
            except BaseException:
                if savepoint:
                    self.connection.execute("ROLLBACK TO SAVEPOINT " + savepoint)
                    self.connection.execute("RELEASE SAVEPOINT " + savepoint)
                else:
                    self.connection.rollback()
                raise
            else:
                if savepoint:
                    self.connection.execute("RELEASE SAVEPOINT " + savepoint)
                else:
                    self.connection.commit()

    def _prune_memories(self, connection: sqlite3.Connection, now: datetime) -> None:
        connection.execute(
            "DELETE FROM memories WHERE device_id=? AND expires_at IS NOT NULL AND expires_at <= ?",
            (self.device_id, _iso(now)),
        )

    def remember_fact(
        self,
        key: str,
        value: str,
        *,
        authorized: bool = True,
        expires_at: str | datetime | None = None,
    ) -> dict[str, Any]:
        if authorized is not True:
            raise ValueError("保存记忆必须有用户明确授权")
        key = _check_memory_key(key)
        value = _check_text(value, "value", 2048)
        expiry = _parse_time(expires_at, "expires_at") if expires_at is not None else _utc_now() + timedelta(days=self.memory_ttl_days)
        now = _utc_now()
        with self._transaction(immediate=True) as connection:
            self._prune_memories(connection, now)
            existing = connection.execute(
                "SELECT memory_id,created_at FROM memories WHERE device_id=? AND key=?",
                (self.device_id, key),
            ).fetchone()
            if existing is None:
                count = connection.execute("SELECT count(*) FROM memories WHERE device_id=?", (self.device_id,)).fetchone()[0]
                if count >= self.max_memories:
                    raise ValueError("记忆容量已满，请先删除旧记忆")
                memory_id = uuid.uuid4().hex
                created_at = _iso(now)
            else:
                memory_id, created_at = existing[0], existing[1]
            connection.execute(
                """INSERT INTO memories(memory_id,device_id,key,value,authorized,created_at,updated_at,expires_at)
                VALUES(?,?,?,?,1,?,?,?)
                ON CONFLICT(device_id,key) DO UPDATE SET value=excluded.value,
                updated_at=excluded.updated_at,expires_at=excluded.expires_at,authorized=1""",
                (memory_id, self.device_id, key, value, created_at, _iso(now), _iso(expiry)),
            )
        return {"id": memory_id, "memory_id": memory_id, "key": key, "saved": True, "storage": "pc_prototype", "expires_at": _iso(expiry)}

    def recall_memory(self, key: str | None = None, *, limit: int = 10, cursor: str | None = None) -> dict[str, Any]:
        if key is not None:
            key = _check_memory_key(key)
        if not isinstance(limit, int) or not 1 <= limit <= 100:
            raise ValueError("limit 需为1～100")
        now = _utc_now()
        with self._transaction(immediate=True) as connection:
            self._prune_memories(connection, now)
            if key is not None:
                rows = connection.execute(
                    "SELECT memory_id,key,value,updated_at,expires_at FROM memories WHERE device_id=? AND key=? LIMIT 1",
                    (self.device_id, key),
                ).fetchall()
                next_cursor = None
            else:
                params: list[Any] = [self.device_id]
                condition = ""
                if cursor:
                    updated, memory_id = _decode_cursor(cursor)
                    condition = " AND (updated_at < ? OR (updated_at = ? AND memory_id < ?))"
                    params.extend((updated, updated, memory_id))
                params.append(limit)
                rows = connection.execute(
                    "SELECT memory_id,key,value,updated_at,expires_at FROM memories WHERE device_id=?" + condition + " ORDER BY updated_at DESC,memory_id DESC LIMIT ?",
                    params,
                ).fetchall()
                next_cursor = _encode_cursor((rows[-1][3], rows[-1][0])) if len(rows) == limit else None
        memories = []
        for row in rows:
            item = dict(row)
            item["id"] = item["memory_id"]
            memories.append(item)
        return {"memories": memories, "next_cursor": next_cursor}

    def update_memory(
        self,
        memory_id: str | None = None,
        *,
        key: str | None = None,
        value: str,
        expires_at: str | datetime | None = None,
        authorized: bool = True,
    ) -> dict[str, Any]:
        if authorized is not True:
            raise ValueError("更新记忆必须有用户明确授权")
        if (memory_id is None) == (key is None):
            raise ValueError("memory_id 与 key 必须且只能提供一个")
        value = _check_text(value, "value", 2048)
        if key is not None:
            key = _check_memory_key(key)
        expiry = _parse_time(expires_at, "expires_at") if expires_at is not None else _utc_now() + timedelta(days=self.memory_ttl_days)
        now = _utc_now()
        with self._transaction(immediate=True) as connection:
            self._prune_memories(connection, now)
            where, identity = ("memory_id=?", memory_id) if memory_id is not None else ("key=?", key)
            row = connection.execute(f"SELECT memory_id,key FROM memories WHERE device_id=? AND {where}", (self.device_id, identity)).fetchone()
            if row is None:
                raise NotFoundError("记忆不存在")
            connection.execute(f"UPDATE memories SET value=?,updated_at=?,expires_at=? WHERE device_id=? AND {where}", (value, _iso(now), _iso(expiry), self.device_id, identity))
            memory_id, key = row[0], row[1]
        return {"id": memory_id, "memory_id": memory_id, "key": key, "updated": True, "expires_at": _iso(expiry)}

    def delete_memory(self, memory_id: str | None = None, *, key: str | None = None) -> dict[str, Any]:
        if (memory_id is None) == (key is None):
            raise ValueError("memory_id 与 key 必须且只能提供一个")
        if key is not None:
            key = _check_memory_key(key)
        with self._transaction(immediate=True) as connection:
            where, identity = ("memory_id=?", memory_id) if memory_id is not None else ("key=?", key)
            cursor = connection.execute(f"DELETE FROM memories WHERE device_id=? AND {where}", (self.device_id, identity))
            if cursor.rowcount != 1:
                raise NotFoundError("记忆不存在")
        return {"deleted": True, "id": memory_id, "memory_id": memory_id, "key": key}

    def create_task(
        self,
        title: str,
        due_at: str | datetime | None,
        idempotency_key: str,
        *,
        metadata: dict[str, Any] | None = None,
    ) -> dict[str, Any]:
        title = _check_text(title, "title", 200)
        idempotency_key = _check_text(idempotency_key, "idempotency_key", 128)
        due = _parse_time(due_at, "due_at") if due_at is not None else None
        if metadata is not None and not isinstance(metadata, dict):
            raise ValueError("metadata 必须是对象")
        if metadata is not None and {"project", "position", "progress", "project_request"}.intersection(metadata):
            raise ValueError("项目关联与序号由create_project_tasks统一创建，普通待办不能传入项目保留字段")
        metadata_text = json.dumps(metadata, ensure_ascii=False, separators=(",", ":")) if metadata is not None else None
        if metadata_text is not None and len(metadata_text.encode("utf-8")) > 4096:
            raise ValueError("metadata 长度不能超过4096字节")
        now_text = _iso(_utc_now())
        with self._transaction(immediate=True) as connection:
            existing = connection.execute(
                "SELECT * FROM tasks WHERE device_id=? AND idempotency_key=?",
                (self.device_id, idempotency_key),
            ).fetchone()
            if existing:
                if existing["title"] != title or existing["due_at"] != (_iso(due) if due else None) or existing["metadata"] != metadata_text:
                    raise IdempotencyConflict("idempotency_key 已用于不同任务")
                return self._task_dict(existing)
            task_id = uuid.uuid4().hex
            connection.execute(
                """INSERT INTO tasks(task_id,device_id,title,due_at,status,reminder_state,lease_token,lease_until,idempotency_key,metadata,created_at,updated_at)
                VALUES(?,?,?,?, 'pending','pending',NULL,NULL,?,?,?,?)""",
                (task_id, self.device_id, title, _iso(due) if due else None, idempotency_key, metadata_text, now_text, now_text),
            )
            row = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        return self._task_dict(row)

    @staticmethod
    def _task_dict(row: sqlite3.Row | None) -> dict[str, Any]:
        if row is None:
            raise NotFoundError("任务不存在")
        result = dict(row)
        result["id"] = result["task_id"]
        if result.get("metadata"):
            result["metadata"] = json.loads(result["metadata"])
        else:
            result["metadata"] = None
        return result

    def list_tasks(self, status: str | None = None, *, limit: int = 50, cursor: str | None = None, due_from: str | datetime | None = None, due_before: str | datetime | None = None, project: str | None = None) -> dict[str, Any]:
        if status is not None and status not in {"pending", "completed", "cancelled"}:
            raise ValueError("status 无效")
        if not isinstance(limit, int) or not 1 <= limit <= 100:
            raise ValueError("limit 需为1～100")
        start = _parse_time(due_from, "due_from") if due_from is not None else None
        end = _parse_time(due_before, "due_before") if due_before is not None else None
        if start is not None and end is not None and start >= end:
            raise ValueError("due_from 必须早于 due_before")
        if project is not None:
            from .project_tasks import list_project_tasks
            return list_project_tasks(self, project, status=status, limit=limit, cursor=cursor, start=start, end=end)
        ranged = start is not None or end is not None
        params: list[Any] = [self.device_id]
        conditions = ["device_id=?"]
        if status:
            conditions.append("status=?")
            params.append(status)
        if ranged:
            conditions.append("due_at IS NOT NULL")
        if start is not None:
            conditions.append("due_at >= ?")
            params.append(_iso(start))
        if end is not None:
            conditions.append("due_at < ?")
            params.append(_iso(end))
        if cursor:
            due, task_id = _decode_cursor(cursor)
            if due == "__NULL__":
                if ranged:
                    return {"tasks": [], "next_cursor": None}
                conditions.append("due_at IS NULL AND task_id > ?")
                params.append(task_id)
            else:
                conditions.append("(due_at,task_id) > (?,?)" if ranged else "(due_at > ? OR (due_at = ? AND task_id > ?) OR due_at IS NULL)")
                params.extend((due, task_id) if ranged else (due, due, task_id))
        params.append(limit + 1)
        ordering = "due_at,task_id" if ranged else "due_at IS NULL,due_at,task_id"
        with self._lock:
            rows = self.connection.execute(
                "SELECT * FROM tasks WHERE " + " AND ".join(conditions) + " ORDER BY " + ordering + " LIMIT ?",
                params,
            ).fetchall()
        page = rows[:limit]
        next_cursor = _encode_cursor((page[-1]["due_at"] or "__NULL__", page[-1]["task_id"])) if len(rows) > limit else None
        return {"tasks": [self._task_dict(row) for row in page], "next_cursor": next_cursor}

    def snooze_reminder(self, task_id: str, expected_updated_at: str, *, minutes: int = 5) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        expected_updated_at = _check_text(expected_updated_at, "updated_at", 64)
        if type(minutes) is not int or not 1 <= minutes <= 60:
            raise ValueError("稍后提醒需为1～60分钟")
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("提醒不存在")
            if row["status"] != "pending" or row["due_at"] is None:
                raise ValueError("只有未完成的定时提醒可稍后提醒")
            if row["updated_at"] != expected_updated_at:
                raise IdempotencyConflict("提醒状态已变化，请刷新；未重复延后")
            now = _utc_now()
            if _parse_time(row["due_at"], "due_at") > now:
                raise ValueError("提醒尚未到期，无需稍后提醒")
            connection.execute("UPDATE tasks SET due_at=?,reminder_state='pending',lease_token=NULL,lease_until=NULL,updated_at=? WHERE device_id=? AND task_id=?", (_iso(now + timedelta(minutes=minutes)), _next_task_revision(row["updated_at"], now), self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        return self._task_dict(result)

    def reschedule_task(self, task_id: str, expected_updated_at: str, due_at: str | datetime) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        expected_updated_at = _check_text(expected_updated_at, "expected_updated_at", 64)
        due = _parse_time(due_at, "due_at")
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("提醒不存在")
            now = _utc_now()
            if due <= now:
                raise ValueError("新提醒时间必须晚于当前时间")
            if row["status"] != "pending":
                raise ValueError("只有未完成的待办或提醒可设置提醒时间")
            if row["updated_at"] != expected_updated_at:
                raise IdempotencyConflict("提醒状态已变化，请刷新；未重复改期")
            if row["due_at"] is not None and _parse_time(row["due_at"], "due_at") <= now:
                raise ValueError("提醒已到期，请使用稍后提醒")
            # 版本必须严格前进；同一时刻或系统时间回退不能令旧请求再次匹配。
            updated_at = _next_task_revision(row["updated_at"], now)
            connection.execute("UPDATE tasks SET due_at=?,reminder_state='pending',lease_token=NULL,lease_until=NULL,updated_at=? WHERE device_id=? AND task_id=?", (_iso(due), updated_at, self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
        return self._task_dict(result)

    def rename_task(self, task_id: str, expected_updated_at: str, title: str) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        expected_updated_at = _check_text(expected_updated_at, "expected_updated_at", 64)
        title = _check_text(title, "title", 200)
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("待办不存在")
            now = _utc_now()
            if row["updated_at"] != expected_updated_at or row["status"] != "pending":
                raise IdempotencyConflict("待办状态已变化，请刷新核对；未修改标题")
            if row["reminder_state"] == "leased" or row["due_at"] is not None and _parse_time(row["due_at"], "due_at") <= now:
                raise IdempotencyConflict("提醒已到期或正在投递，暂不能修改标题；提醒状态未改变")
            connection.execute("UPDATE tasks SET title=?,updated_at=? WHERE device_id=? AND task_id=?", (title, _next_task_revision(row["updated_at"], now), self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
        return self._task_dict(result)

    def screen_tasks(self, day_end: str | datetime, *, limit: int = 5) -> dict[str, Any]:
        boundary = _iso(_parse_time(day_end, "day_end"))
        if type(limit) is not int or not 1 <= limit <= 10:
            raise ValueError("屏幕摘要最多10条")
        with self._lock:
            rows = self.connection.execute("SELECT * FROM tasks WHERE device_id=? AND status='pending' AND due_at < ? ORDER BY due_at,task_id LIMIT ?", (self.device_id, boundary, limit + 1)).fetchall()
            if len(rows) <= limit:
                rows += self.connection.execute("SELECT * FROM tasks WHERE device_id=? AND status='pending' AND due_at IS NULL ORDER BY task_id LIMIT ?", (self.device_id, limit + 1 - len(rows))).fetchall()
        return {"tasks": [self._task_dict(row) for row in rows[:limit]], "truncated": len(rows) > limit}

    def due_reminders(self, *, limit: int = 10) -> list[dict[str, Any]]:
        if type(limit) is not int or not 1 <= limit <= 10:
            raise ValueError("到期提醒最多10条")
        with self._lock:
            rows = self.connection.execute("SELECT * FROM tasks WHERE device_id=? AND status='pending' AND due_at IS NOT NULL AND due_at <= ? ORDER BY due_at,task_id LIMIT ?", (self.device_id, _iso(_utc_now()), limit)).fetchall()
        return [self._task_dict(row) for row in rows]

    def complete_task(self, task_id: str) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        now = _iso(_utc_now())
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("任务不存在")
            if row["status"] == "cancelled":
                raise ValueError("已取消任务不能完成")
            connection.execute("UPDATE tasks SET status='completed',reminder_state='acked',lease_token=NULL,lease_until=NULL,completed_at=?,updated_at=? WHERE device_id=? AND task_id=?", (now, now, self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        return self._task_dict(result)

    def cancel_task(self, task_id: str) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        now = _iso(_utc_now())
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("任务不存在")
            if row["status"] == "completed":
                raise ValueError("已完成任务不能取消")
            connection.execute("UPDATE tasks SET status='cancelled',reminder_state='acked',lease_token=NULL,lease_until=NULL,cancelled_at=?,updated_at=? WHERE device_id=? AND task_id=?", (now, now, self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        return self._task_dict(result)

    def claim_due_reminders(self, now: str | datetime | None = None, *, lease_seconds: int = 60, limit: int = 20) -> list[dict[str, Any]]:
        explicit_now = _parse_time(now, "now") if now is not None else None
        if not isinstance(lease_seconds, int) or not 1 <= lease_seconds <= 86400:
            raise ValueError("lease_seconds 超出范围")
        if not isinstance(limit, int) or not 1 <= limit <= 100:
            raise ValueError("limit 需为1～100")
        claimed: list[dict[str, Any]] = []
        with self._transaction(immediate=True) as connection:
            current = explicit_now if explicit_now is not None else _utc_now()
            lease_until = current + timedelta(seconds=lease_seconds)
            rows = connection.execute(
                """SELECT * FROM tasks WHERE device_id=? AND status='pending' AND due_at IS NOT NULL AND due_at <= ?
                AND (reminder_state='pending' OR (reminder_state='leased' AND lease_until <= ?))
                ORDER BY due_at,task_id LIMIT ?""",
                (self.device_id, _iso(current), _iso(current), limit),
            ).fetchall()
            for row in rows:
                token = secrets.token_urlsafe(24)
                updated_at = _next_task_revision(row["updated_at"], current)
                connection.execute("UPDATE tasks SET reminder_state='leased',lease_token=?,lease_until=?,updated_at=? WHERE task_id=?", (token, _iso(lease_until), updated_at, row["task_id"]))
                result = dict(row)
                result["id"] = result["task_id"]
                result.update({"reminder_state": "leased", "lease_token": token, "lease_until": _iso(lease_until), "updated_at": updated_at})
                if result.get("metadata"):
                    result["metadata"] = json.loads(result["metadata"])
                else:
                    result["metadata"] = None
                claimed.append(result)
        return claimed

    def valid_reminder_leases(self, candidates: list[dict[str, Any]]) -> list[dict[str, Any]]:
        if not isinstance(candidates, (list, tuple)) or len(candidates) > 100:
            raise ValueError("提醒租约复核最多100条")
        if not candidates:
            return []
        identities = list({_check_text(item.get("task_id", item.get("id")), "task_id", 64) for item in candidates})
        snapshots = {(item.get("task_id", item.get("id")), item["lease_token"], item["updated_at"]) for item in candidates}
        placeholders = ",".join("?" for _ in identities)
        with self._lock:
            now = _utc_now()
            # 强制按候选主键查找，避免优化器改走设备到期索引扫描整个积压。
            rows = self.connection.execute(
                "SELECT * FROM tasks INDEXED BY sqlite_autoindex_tasks_1 WHERE task_id IN (" + placeholders + ") AND device_id=? LIMIT ?",
                [*identities, self.device_id, len(identities)],
            ).fetchall()
        valid = []
        for row in rows:
            if row["status"] != "pending" or row["reminder_state"] != "leased" or not row["due_at"] or not row["lease_until"]:
                continue
            if (row["task_id"], row["lease_token"], row["updated_at"]) not in snapshots or _parse_time(row["due_at"], "due_at") > now or _parse_time(row["lease_until"], "lease_until") <= now:
                continue
            valid.append(self._task_dict(row))
        return valid

    def prune_tasks(self, now: str | datetime | None = None, *, retention_days: int = 90, limit: int = 100) -> int:
        """Delete a bounded batch of old terminal/acknowledged tasks."""
        current = _parse_time(now, "now") if now is not None else _utc_now()
        if not isinstance(retention_days, int) or not 1 <= retention_days <= 3650:
            raise ValueError("retention_days 超出范围")
        if not isinstance(limit, int) or not 1 <= limit <= 1000:
            raise ValueError("limit 需为1～1000")
        cutoff = _iso(current - timedelta(days=retention_days))
        with self._transaction(immediate=True) as connection:
            cursor = connection.execute(
                """DELETE FROM tasks WHERE task_id IN (
                    SELECT task_id FROM tasks
                    WHERE device_id=? AND updated_at < ?
                      AND status IN ('completed','cancelled')
                    ORDER BY updated_at,task_id LIMIT ?
                )""",
                (self.device_id, cutoff, limit),
            )
            return cursor.rowcount

    def ack_reminder(self, task_id: str, lease_token: str) -> dict[str, Any]:
        task_id = _check_text(task_id, "task_id", 64)
        lease_token = _check_text(lease_token, "lease_token", 200)
        with self._transaction(immediate=True) as connection:
            row = connection.execute("SELECT * FROM tasks WHERE device_id=? AND task_id=?", (self.device_id, task_id)).fetchone()
            if row is None:
                raise NotFoundError("任务不存在")
            now = _utc_now()
            if row["reminder_state"] != "leased" or row["lease_token"] != lease_token or not row["lease_until"] or _parse_time(row["lease_until"], "lease_until") <= now:
                raise LeaseConflict("提醒租约无效或已过期")
            connection.execute("UPDATE tasks SET reminder_state='acked',lease_token=NULL,lease_until=NULL,updated_at=? WHERE device_id=? AND task_id=?", (_next_task_revision(row["updated_at"], now), self.device_id, task_id))
            result = connection.execute("SELECT * FROM tasks WHERE task_id=?", (task_id,)).fetchone()
        return self._task_dict(result)

    def close(self) -> None:
        with self._lock:
            self.connection.close()

    def __enter__(self) -> "AssistantStore":
        return self

    def __exit__(self, *_: Any) -> None:
        self.close()

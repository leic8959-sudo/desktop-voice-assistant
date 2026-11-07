"""仅临时SQLite与假设备：项目生命周期、并发、租约和工具身份。"""

import asyncio
import json
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

from assistant_core import AssistantStore, IdempotencyConflict
from assistant_core.project_tasks import ProjectStorageUnavailable, create_project_tasks, reminder_title, update_task_progress
from assistant_core.tools import execute_tool
from assistant_core.store import _encode_cursor
from assistant_runtime import AssistantRuntime, NativeDevice
from chat_pipeline import ChatBackend
from gateway import CONFIG_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession
from tests.test_tool_activity import FakeCloud

MIGRATION = Path(__file__).resolve().parents[1] / "tools/migrations/project_tasks_20261107.sql"
NOW = datetime(2035, 11, 7, 1, tzinfo=timezone.utc)


class ProjectTaskTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "assistant.sqlite3"
        self.store = AssistantStore(self.path, "offline-project")
        self.store.connection.executescript(MIGRATION.read_text(encoding="utf-8"))
        self.clock = patch("assistant_core.project_tasks._utc_now", return_value=NOW)
        self.store_clock = patch("assistant_core.store._utc_now", return_value=NOW)
        self.clock.start()
        self.store_clock.start()

    def tearDown(self):
        self.clock.stop()
        self.store_clock.stop()
        self.store.close()
        self.directory.cleanup()

    def create(self, key="plan", project="小燕子", tasks=None):
        tasks = tasks or [{"title": "确认唤醒边界", "progress": "已定位尾音", "due_at": "2035-11-08T15:00:00+08:00"}, {"title": "整理测试记录"}, {"title": "整理项目交付"}]
        return execute_tool(self.store, "create_project_tasks", {"project": project, "tasks": tasks, "idempotency_key": key})

    def test_full_project_loop_and_replay_after_modifications(self):
        first, second, third = self.create()["tasks"]
        progress = update_task_progress(self.store, first["id"], first["updated_at"], "唤醒边界已确认，等待实机")
        self.store.complete_task(second["id"])
        moved = self.store.reschedule_task(third["id"], third["updated_at"], "2035-11-10T10:00:00+08:00")
        replay = self.create()
        self.assertTrue(replay["replayed"])
        self.assertEqual([item["id"] for item in replay["tasks"]], [first["id"], second["id"], third["id"]])
        self.assertEqual(replay["tasks"][0]["metadata"]["progress"], progress["metadata"]["progress"])
        self.assertEqual(replay["tasks"][1]["status"], "completed")
        self.assertEqual(replay["tasks"][2]["due_at"], moved["due_at"])
        self.assertEqual([item["metadata"]["position"] for item in replay["tasks"]], [1, 2, 3])
        self.assertEqual([item["id"] for item in self.store.list_tasks(project="小燕子", status="completed")["tasks"]], [second["id"]])
        with self.assertRaises(IdempotencyConflict):
            self.create(tasks=[{"title": "不同的三件事"}])
        self.store.cancel_task(third["id"])
        claims = self.store.claim_due_reminders("2035-11-11T00:00:00Z")
        self.assertEqual([item["id"] for item in claims], [first["id"]])
        self.assertEqual(claims[0]["metadata"]["progress"], "唤醒边界已确认，等待实机")

    def test_project_and_device_filter_with_bounded_pages(self):
        expected = {item["id"] for item in self.create()["tasks"]}
        self.create("other", "小燕子二期")
        with AssistantStore(self.path, "another-device") as other:
            create_project_tasks(other, "小燕子", [{"title": "其他设备"}], "private")
        items, cursor = [], None
        while True:
            page = execute_tool(self.store, "list_tasks", {"project": "小燕子", "limit": 1, **({"cursor": cursor} if cursor else {})})
            items.extend(page["tasks"])
            cursor = page["next_cursor"]
            if not cursor:
                break
        self.assertEqual({item["id"] for item in items}, expected)
        self.assertEqual(len(items), 3)
        self.assertEqual(self.store.list_tasks(project="小燕子", due_from="2035-11-08T00:00:00+08:00", due_before="2035-11-09T00:00:00+08:00")["tasks"][0]["metadata"]["position"], 1)
        plan = self.store.connection.execute("EXPLAIN QUERY PLAN SELECT * FROM tasks INDEXED BY tasks_device_project_due WHERE device_id=? AND json_extract(metadata,'$.project')=? ORDER BY due_at IS NULL,due_at,task_id LIMIT 2", (self.store.device_id, "小燕子")).fetchall()
        self.assertTrue(any("SEARCH tasks USING INDEX tasks_device_project_due" in row[3] for row in plan))
        for status in (None, "pending"):
            index = "tasks_device_project_status_due" if status else "tasks_device_project_due"
            plan = self.store.connection.execute("EXPLAIN QUERY PLAN SELECT * FROM tasks INDEXED BY " + index + " WHERE device_id=? AND json_extract(metadata,'$.project')=? " + ("AND status=? " if status else "") + "AND due_at IS NOT NULL AND (due_at IS NULL)=0 AND due_at>=? AND due_at<? ORDER BY due_at,task_id LIMIT 2", [self.store.device_id, "小燕子", *([status] if status else []), "2035-11-08T00:00:00Z", "2035-11-09T00:00:00Z"]).fetchall()
            self.assertFalse(any("TEMP B-TREE" in row[3] for row in plan), [row[3] for row in plan])
            self.assertTrue(any("due_at>? AND due_at<?" in row[3] for row in plan), [row[3] for row in plan])

    def test_atomic_batch_validation_and_metadata_size(self):
        with self.assertRaises(ValueError):
            self.create(tasks=[{"title": "有效"}, {"title": ""}])
        with self.assertRaises(ValueError):
            self.create(tasks=[{"title": "先写的也需回滚"}, {"title": "超长字节", "progress": "😀" * 1000}])
        self.assertEqual(self.store.list_tasks()["tasks"], [])
        self.assertEqual(self.store.connection.execute("SELECT count(*) FROM project_task_sequences").fetchone()[0], 0)
        self.assertEqual(self.create()["tasks"][0]["metadata"]["position"], 1)

    def test_deep_project_cursor_seeks_both_segments_with_bounded_work(self):
        rows = []
        for index in range(8000):
            timed = index < 4000
            identity = f"{'timed' if timed else 'untimed'}-{index % 4000:05d}"
            metadata = json.dumps({"project": "规模项目", "position": index + 1})
            rows.append((identity, self.store.device_id, identity, "2035-11-08T07:00:00.000000Z" if timed else None, identity, metadata, NOW.isoformat(), NOW.isoformat()))
        self.store.connection.executemany("INSERT INTO tasks(task_id,device_id,title,due_at,status,reminder_state,idempotency_key,metadata,created_at,updated_at) VALUES(?,?,?,?,'pending','pending',?,?,?,?)", rows)
        self.store.connection.commit()
        for status in (None, "pending"):
            for due, identity, date_range in (("2035-11-08T07:00:00.000000Z", "timed-03000", False), ("2035-11-08T07:00:00.000000Z", "timed-03000", True), ("__NULL__", "untimed-03000", False), ("2035-11-08T07:00:00.000000Z", "timed-03998", False)):
                with self.subTest(status=status, identity=identity, date_range=date_range):
                    statements, instructions = [], [0]
                    def count():
                        instructions[0] += 1
                        return int(instructions[0] > 1500)
                    self.store.connection.set_trace_callback(lambda statement: statements.append(statement) if statement.startswith("SELECT * FROM tasks INDEXED") else None)
                    self.store.connection.set_progress_handler(count, 1)
                    try:
                        page = self.store.list_tasks(project="规模项目", status=status, limit=3, cursor=_encode_cursor((due, identity)), **({"due_from": "2035-11-08T00:00:00Z", "due_before": "2035-11-09T00:00:00Z"} if date_range else {}))
                    finally:
                        self.store.connection.set_progress_handler(None, 0)
                        self.store.connection.set_trace_callback(None)
                    self.assertLess(instructions[0], 1500)
                    if identity == "timed-03998":
                        self.assertEqual([item["id"] for item in page["tasks"]], ["timed-03999", "untimed-00000", "untimed-00001"])
                    else:
                        prefix = "untimed" if due == "__NULL__" else "timed"
                        self.assertEqual([item["id"] for item in page["tasks"]], [f"{prefix}-{index:05d}" for index in (3001, 3002, 3003)])
                    self.assertLessEqual(len(statements), 2)
                    for statement in statements:
                        plan = [row[3] for row in self.store.connection.execute("EXPLAIN QUERY PLAN " + statement)]
                        self.assertFalse(any("TEMP B-TREE" in row for row in plan), plan)
                        self.assertTrue(any("SEARCH tasks USING INDEX tasks_device_project" in row for row in plan), plan)

    def test_concurrent_creation_never_reuses_positions_after_pruning(self):
        with AssistantStore(self.path, self.store.device_id) as other:
            with ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(create_project_tasks, store, "并发项目", [{"title": "记录"}], key) for store, key in ((self.store, "a"), (other, "b"))]
                tasks = [future.result()["tasks"][0] for future in futures]
        self.assertEqual({task["metadata"]["position"] for task in tasks}, {1, 2})
        for task in tasks:
            self.store.complete_task(task["id"])
        self.assertEqual(self.store.prune_tasks(NOW + timedelta(days=91)), 2)
        new = self.create("later", "并发项目", [{"title": "后续任务"}])["tasks"][0]
        self.assertEqual(new["metadata"]["position"], 3)
        with self.assertRaises(IdempotencyConflict):
            create_project_tasks(self.store, "并发项目", [{"title": "记录"}], "a")

    def test_concurrent_same_batch_and_receipt_cleanup_do_not_duplicate_pending(self):
        with AssistantStore(self.path, self.store.device_id) as other:
            with ThreadPoolExecutor(max_workers=2) as pool:
                futures = [pool.submit(create_project_tasks, store, "相同项目", [{"title": "保留待办"}], "same") for store in (self.store, other)]
                results = [future.result() for future in futures]
        self.assertEqual(results[0]["tasks"][0]["id"], results[1]["tasks"][0]["id"])
        with patch("assistant_core.project_tasks._utc_now", return_value=NOW + timedelta(days=181)):
            self.create("trigger-prune", "清理触发", [{"title": "触发有界回执清理"}])
        self.assertIsNone(self.store.connection.execute("SELECT 1 FROM project_task_batches WHERE idempotency_key='same'").fetchone())
        replay = create_project_tasks(self.store, "相同项目", [{"title": "保留待办"}], "same")
        self.assertTrue(replay["replayed"])
        self.assertEqual(replay["tasks"][0]["id"], results[0]["tasks"][0]["id"])
        with self.assertRaises(IdempotencyConflict):
            create_project_tasks(self.store, "相同项目", [{"title": "保留待办"}, {"title": "不能扩容原批次"}], "same")

    def test_progress_revision_active_and_expired_reminder_lease(self):
        task = self.create(tasks=[{"title": "到期事项", "due_at": "2035-11-07T00:59:00Z"}])["tasks"][0]
        claim = self.store.claim_due_reminders(lease_seconds=30)[0]
        with self.assertRaises(IdempotencyConflict):
            update_task_progress(self.store, task["id"], claim["updated_at"], "不能在投递中修改")
        with patch("assistant_core.project_tasks._utc_now", return_value=NOW + timedelta(seconds=31)), patch("assistant_core.store._utc_now", return_value=NOW + timedelta(seconds=31)):
            changed = update_task_progress(self.store, task["id"], claim["updated_at"], "离线期间已完成一半")
            self.assertEqual(self.store.valid_reminder_leases([claim]), [])
            new_claim = self.store.claim_due_reminders()[0]
            self.assertNotEqual(new_claim["lease_token"], claim["lease_token"])
            self.assertEqual(new_claim["metadata"]["progress"], "离线期间已完成一半")
        self.assertEqual(changed["reminder_state"], "pending")
        with self.assertRaises(IdempotencyConflict):
            update_task_progress(self.store, task["id"], task["updated_at"], "过期版本")

    def test_progress_after_ack_does_not_repeat_reminder_and_terminal_is_rejected(self):
        task = self.create(tasks=[{"title": "已显示但未完成", "due_at": "2035-11-07T00:59:00Z"}])["tasks"][0]
        claim = self.store.claim_due_reminders()[0]
        acknowledged = self.store.ack_reminder(task["id"], claim["lease_token"])
        changed = update_task_progress(self.store, task["id"], acknowledged["updated_at"], "继续执行中")
        self.assertEqual(changed["reminder_state"], "acked")
        self.assertEqual(self.store.claim_due_reminders(), [])
        completed = self.store.complete_task(task["id"])
        with self.assertRaises(ValueError):
            update_task_progress(self.store, task["id"], completed["updated_at"], "不能更新已完成")
        third = self.create("cancelled")["tasks"][2]
        cancelled = self.store.cancel_task(third["id"])
        with self.assertRaises(ValueError):
            update_task_progress(self.store, third["id"], cancelled["updated_at"], "不能更新已取消")

    def test_ordinary_tasks_cannot_forge_project_metadata(self):
        for field in ("project", "position", "progress", "project_request"):
            with self.assertRaises(ValueError):
                self.store.create_task("不能伪造", None, field, metadata={field: "fake"})
        plain = self.store.create_task("普通待办", None, "ordinary", metadata={"tag": "keep"})
        self.assertEqual(reminder_title(plain), plain["title"])

    def test_missing_migration_keeps_ordinary_tasks_available(self):
        with AssistantStore(Path(self.directory.name) / "unmigrated.sqlite3") as plain:
            self.assertEqual(plain.create_task("已有功能", None, "old")["status"], "pending")
            with self.assertRaises(ProjectStorageUnavailable):
                create_project_tasks(plain, "小燕子", [{"title": "不可写"}], "new")
            with self.assertRaises(ProjectStorageUnavailable):
                plain.list_tasks(project="小燕子")
            self.assertEqual(len(plain.list_tasks()["tasks"]), 1)


class ProjectReminderTests(unittest.IsolatedAsyncioTestCase):
    async def test_realtime_and_chat_use_server_batch_identity(self):
        for provider in ("realtime", "chat"):
            with self.subTest(provider=provider), tempfile.TemporaryDirectory() as directory:
                with AssistantStore(Path(directory) / "tools.sqlite3", provider) as store:
                    store.connection.executescript(MIGRATION.read_text(encoding="utf-8"))
                    runtime = SimpleNamespace(notifications={}, memory_service=None, media_service=None, device_music=None, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
                    voice = VoiceSession({CONFIG_KEY: {}, STORE_KEY: store, RUNTIME_KEY: runtime}, SimpleNamespace(send_json=AsyncMock(), closed=False), browser=True)
                    if provider == "realtime":
                        backend = RealtimeBackend(voice)
                        backend.cloud = FakeCloud()
                    else:
                        backend = ChatBackend(voice, "qwen_chat")
                    voice.backend = backend
                    await voice.observe_user_text("记录小燕子的一项进度", source="text")
                    arguments = json.dumps({"project": "小燕子", "tasks": [{"title": "确认进度"}], "idempotency_key": "model-must-not-own-key"})
                    call = {"name": "create_project_tasks", "arguments": arguments}
                    for _ in range(2):
                        if provider == "realtime":
                            await voice.process_tools([{**call, "call_id": "create-1"}], voice.generation, response_id="response-1", input_epoch=voice.accepted_input_epoch)
                        else:
                            result = await backend._execute_tool({**call, "id": "create-1", "response_id": "response-1"})
                            self.assertEqual(result["project"], "小燕子")
                    items = store.list_tasks(project="小燕子")["tasks"]
                    self.assertEqual(len(items), 1)
                    receipt = store.connection.execute("SELECT idempotency_key FROM project_task_batches").fetchone()[0]
                    self.assertTrue(receipt.startswith("realtime-tool-" if provider == "realtime" else "chat-tool-"))
                    self.assertNotEqual(receipt, "model-must-not-own-key")
                    await backend.close()

    async def test_actual_notification_payload_is_bounded_and_keeps_identity(self):
        revision = datetime.now(timezone.utc).isoformat()
        item = {"id": "item", "status": "pending_delivery", "lease_token": "token", "updated_at": revision, "lease_until": (datetime.now(timezone.utc) + timedelta(minutes=1)).isoformat(), "title": "事项标题" * 60, "metadata": {"project": "小燕子", "position": 3, "progress": "最新：已定位边界，等待验收" * 70}}
        device = NativeDevice.__new__(NativeDevice)
        device.runtime = SimpleNamespace(notifications={"item": item})
        device.socket = SimpleNamespace(closed=False, send_json=AsyncMock())
        device.send_lock = asyncio.Lock()
        await device.send_notification(item)
        payload = device.socket.send_json.await_args.args[0]["notification"]
        self.assertLessEqual(len(payload["title"].encode("utf-8")), 180)
        self.assertIn("第3项", payload["title"])
        self.assertIn("上次：最新", payload["title"])
        self.assertEqual(payload["lease_token"], item["lease_token"])
        self.assertEqual(payload["updated_at"], revision)
        self.assertEqual(item["title"], "事项标题" * 60)

    async def test_invalidated_cached_notification_does_not_send(self):
        runtime = AssistantRuntime.__new__(AssistantRuntime)
        runtime.store = SimpleNamespace(valid_reminder_leases=lambda candidates: [])
        device = SimpleNamespace(send_notification=AsyncMock())
        await runtime._send_notification_batch(device, [{"id": "stale", "status": "pending_delivery", "lease_token": "old", "updated_at": "old"}])
        device.send_notification.assert_not_awaited()


if __name__ == "__main__":
    unittest.main()

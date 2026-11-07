"""未来单次提醒改期：真实临时SQLite、并发连接和本机API，禁止外网。"""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import ipaddress
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp
from aiohttp.test_utils import TestClient, TestServer

from assistant_core import AssistantStore, IdempotencyConflict, LeaseConflict, NotFoundError
from assistant_core.tools import execute_tool
from gateway import RUNTIME_KEY, TOOL_SCHEMAS, VoiceSession, create_app
from llm_providers import chat_tool_schemas
from src.assistant_app.application.screen_feedback import TOOLS, api_operation, feedback_card

NOW = datetime(2026, 11, 5, 2, 0, tzinfo=timezone.utc)


class TaskRescheduleTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "assistant.sqlite3"
        self.store = AssistantStore(self.path, "offline-reschedule")
        self.sequence = 0

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def task(self, due=None, **kwargs):
        self.sequence += 1
        with patch("assistant_core.store._utc_now", return_value=NOW):
            return self.store.create_task("未来提醒", due or (NOW + timedelta(minutes=10)).isoformat(), f"offline-task-{self.sequence}", **kwargs)

    def arguments(self, task, due=None):
        return {"task_id": task["task_id"], "expected_updated_at": task["updated_at"], "due_at": due or (NOW + timedelta(hours=1)).isoformat()}

    def test_tool_reschedule_stores_utc_and_preserves_task_identity(self):
        task = self.task(metadata={"source": "offline", "tag": 3})
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = execute_tool(self.store, "reschedule_task", self.arguments(task, "2026-11-05T13:00:00+08:00"))
        self.assertEqual(result["due_at"], "2026-11-05T05:00:00.000000Z")
        self.assertEqual(result["status"], "pending")
        for field in ("task_id", "title", "idempotency_key", "metadata", "created_at", "device_id"):
            self.assertEqual(result[field], task[field])
        with AssistantStore(self.path, "offline-reschedule") as reopened:
            self.assertEqual(reopened.list_tasks()["tasks"][0], result)

    def test_versions_strictly_advance_during_same_time_and_clock_rollback(self):
        task = self.task()
        with patch("assistant_core.store._utc_now", return_value=NOW):
            first = execute_tool(self.store, "reschedule_task", self.arguments(task))
            with self.assertRaises(IdempotencyConflict):
                execute_tool(self.store, "reschedule_task", self.arguments(task))
        with patch("assistant_core.store._utc_now", return_value=NOW - timedelta(hours=1)):
            second = execute_tool(self.store, "reschedule_task", self.arguments(first))
            with self.assertRaises(IdempotencyConflict):
                execute_tool(self.store, "reschedule_task", self.arguments(first))
        self.assertGreater(first["updated_at"], task["updated_at"])
        self.assertGreater(second["updated_at"], first["updated_at"])

    def test_future_validation_reads_clock_after_write_transaction_is_acquired(self):
        for original_minutes, new_minutes in ((10, 60), (60, 10)):
            with self.subTest(original_minutes=original_minutes, new_minutes=new_minutes):
                task = self.task((NOW + timedelta(minutes=original_minutes)).isoformat())

                def clock_after_lock():
                    self.assertTrue(self.store.connection.in_transaction, "验证时间必须在取得写事务后读取")
                    return NOW + timedelta(minutes=20)

                with patch("assistant_core.store._utc_now", side_effect=clock_after_lock), self.assertRaises(ValueError):
                    self.store.reschedule_task(task["task_id"], task["updated_at"], NOW + timedelta(minutes=new_minutes))
                current = next(item for item in self.store.list_tasks()["tasks"] if item["task_id"] == task["task_id"])
                self.assertEqual(current, task)

    def test_two_sqlite_connections_only_accept_one_matching_version(self):
        task = self.task()
        other = AssistantStore(self.path, "offline-reschedule")
        barrier = threading.Barrier(2)

        def change(store, minutes):
            barrier.wait(timeout=2)
            try:
                return store.reschedule_task(task["task_id"], task["updated_at"], NOW + timedelta(minutes=minutes))
            except IdempotencyConflict:
                return "conflict"

        try:
            with patch("assistant_core.store._utc_now", return_value=NOW), ThreadPoolExecutor(max_workers=2) as executor:
                futures = [executor.submit(change, store, minutes) for store, minutes in ((self.store, 30), (other, 40))]
                results = [future.result(timeout=5) for future in futures]
            self.assertEqual(sum(isinstance(result, dict) for result in results), 1)
            self.assertEqual(results.count("conflict"), 1)
            accepted = next(result for result in results if isinstance(result, dict))
            self.assertEqual(self.store.list_tasks()["tasks"][0], accepted)
        finally:
            other.close()

    def test_reschedule_clears_old_lease_but_does_not_touch_another_reminder(self):
        target, untouched = self.task(), self.task()
        self.store.claim_due_reminders(NOW + timedelta(minutes=11), lease_seconds=60)
        claimed = {task["task_id"]: task for task in self.store.list_tasks()["tasks"]}
        target = claimed[target["task_id"]]
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = execute_tool(self.store, "reschedule_task", self.arguments(target))
            with self.assertRaises(LeaseConflict):
                self.store.ack_reminder(target["task_id"], target["lease_token"])
        self.assertIsNone(result["lease_token"])
        self.assertIsNone(result["lease_until"])
        self.assertEqual(result["reminder_state"], "pending")
        current = {task["task_id"]: task for task in self.store.list_tasks()["tasks"]}
        self.assertEqual(current[untouched["task_id"]], claimed[untouched["task_id"]])

    def test_device_scope_rejects_foreign_task_without_changes(self):
        task = self.task()
        with AssistantStore(self.path, "another-device") as other, patch("assistant_core.store._utc_now", return_value=NOW):
            with self.assertRaises(NotFoundError):
                other.reschedule_task(task["task_id"], task["updated_at"], NOW + timedelta(hours=1))
        self.assertEqual(self.store.list_tasks()["tasks"][0], task)

    def test_invalid_new_time_is_rejected_without_changes(self):
        task = self.task()
        for due in (None, "2026-11-05T13:00:00", NOW.isoformat(), (NOW - timedelta(seconds=1)).isoformat(), "tomorrow", True):
            with self.subTest(due=due), patch("assistant_core.store._utc_now", return_value=NOW), self.assertRaises(ValueError):
                self.store.reschedule_task(task["task_id"], task["updated_at"], due)
        self.assertEqual(self.store.list_tasks()["tasks"][0], task)

    def test_due_completed_and_cancelled_tasks_are_not_rescheduled(self):
        for state in ("due", "completed", "cancelled"):
            with self.subTest(state=state):
                task = self.task(NOW.isoformat() if state == "due" else None)
                if state == "completed":
                    task = self.store.complete_task(task["task_id"])
                elif state == "cancelled":
                    task = self.store.cancel_task(task["task_id"])
                with patch("assistant_core.store._utc_now", return_value=NOW), self.assertRaises(ValueError):
                    execute_tool(self.store, "reschedule_task", self.arguments(task))
                current = next(item for item in self.store.list_tasks()["tasks"] if item["task_id"] == task["task_id"])
                self.assertEqual(current, task)

    def test_realtime_chat_and_api_share_strict_contract_and_actual_due_feedback(self):
        realtime = next(tool for tool in TOOL_SCHEMAS if tool["name"] == "reschedule_task")
        chat = next(tool["function"] for tool in chat_tool_schemas(TOOL_SCHEMAS) if tool["function"]["name"] == "reschedule_task")
        for tool in (realtime, chat):
            self.assertEqual(set(tool["parameters"]["required"]), {"task_id", "expected_updated_at", "due_at"})
            self.assertFalse(tool["parameters"]["additionalProperties"])
        operation = api_operation("POST", "/api/tasks/offline/reschedule", None)
        self.assertEqual(operation, TOOLS["reschedule_task"])
        task = self.task()
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = execute_tool(self.store, "reschedule_task", self.arguments(task))
        card = feedback_card(operation, result)
        self.assertEqual(card["title"], "修改提醒时间")
        self.assertEqual(card["status"], "saved")
        self.assertIn(result["due_at"], card["detail"])
        self.assertIn("仍未完成", card["detail"])
        self.assertEqual(card, feedback_card(operation, {"item": result}))


class RescheduleApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.outside_attempts = []
        original = aiohttp.TCPConnector._resolve_host

        async def loopback_only(connector, host, port, *args, **kwargs):
            try:
                allowed = host == "localhost" or ipaddress.ip_address(host).is_loopback
            except ValueError:
                allowed = False
            if not allowed:
                self.outside_attempts.append(host)
                raise AssertionError("外网请求未隔离")
            return await original(connector, host, port, *args, **kwargs)

        self.guard = patch.object(aiohttp.TCPConnector, "_resolve_host", loopback_only)
        self.guard.start()
        self.addCleanup(self.guard.stop)
        self.token = "offline-task-reschedule-token-0000"
        self.app = create_app({"runtime_dir": self.directory.name, "device_id": "offline-api", "device_token": self.token, "aliyun_model": "offline-model", "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.runtime = self.app[RUNTIME_KEY]
        self.headers = {"Authorization": "Bearer " + self.token}
        self.due = (datetime.now(timezone.utc) + timedelta(days=2)).isoformat()

    async def asyncTearDown(self):
        try:
            await self.client.close()
        finally:
            self.guard.stop()
            self.directory.cleanup()
        self.assertEqual(self.outside_attempts, [])

    def task(self):
        return self.runtime.store.create_task("未来API提醒", (datetime.now(timezone.utc) + timedelta(days=1)).isoformat(), "offline-api-task")

    async def test_api_updates_real_record_and_rejects_stale_revision(self):
        task = self.task()
        self.runtime.notifications = {task["task_id"]: {"old": True}, "other": {"keep": True}}
        payload = {"expected_updated_at": task["updated_at"], "due_at": self.due}
        url = f"/api/tasks/{task['task_id']}/reschedule"
        response = await self.client.post(url, headers=self.headers, json=payload)
        self.assertEqual(response.status, 200)
        result = (await response.json())["item"]
        self.assertEqual(result["status"], "pending")
        self.assertEqual(result["due_at"], datetime.fromisoformat(self.due).isoformat(timespec="microseconds").replace("+00:00", "Z"))
        self.assertNotIn(task["task_id"], self.runtime.notifications)
        self.assertEqual(self.runtime.notifications["other"], {"keep": True})
        self.runtime.notifications[task["task_id"]] = {"fresh": True}
        replay = await self.client.post(url, headers=self.headers, json=payload)
        self.assertEqual(replay.status, 409)
        self.assertEqual(self.runtime.notifications[task["task_id"]], {"fresh": True})
        stored = self.runtime.store.list_tasks()["tasks"][0]
        self.assertEqual(stored["due_at"], result["due_at"])
        self.assertEqual(stored["updated_at"], result["updated_at"])

    async def test_status_advertises_loaded_reschedule_capability(self):
        response = await self.client.get('/api/status', headers=self.headers)
        self.assertEqual(response.status, 200)
        self.assertIs((await response.json())["capabilities"]["task_reschedule"], True)

    async def test_api_rejects_anonymous_missing_and_extra_fields_without_writing(self):
        task = self.task()
        url = f"/api/tasks/{task['task_id']}/reschedule"
        payload = {"expected_updated_at": task["updated_at"], "due_at": self.due}
        self.assertEqual((await self.client.post(url, json=payload)).status, 401)
        for invalid in ({"due_at": self.due}, {"expected_updated_at": task["updated_at"]}, {**payload, "title": "不应修改"}, {**payload, "due_at": "2026-11-07T12:00:00"}):
            with self.subTest(payload=invalid):
                response = await self.client.post(url, headers=self.headers, json=invalid)
                self.assertEqual(response.status, 400)
        self.assertEqual(self.runtime.store.list_tasks()["tasks"][0], task)

    async def test_voice_dispatch_clears_only_target_and_preserves_conflict_notification(self):
        task = self.task()
        voice = VoiceSession(self.app, None, browser=True)
        self.runtime.operation_screen_feedback = AsyncMock()
        self.runtime.notifications = {task["task_id"]: {"old": True}, "other": {"keep": True}}
        arguments = {"task_id": task["task_id"], "expected_updated_at": task["updated_at"], "due_at": self.due}
        result = await voice.execute_tool("reschedule_task", arguments)
        self.assertNotIn(task["task_id"], self.runtime.notifications)
        self.assertEqual(self.runtime.notifications["other"], {"keep": True})
        operation, returned = self.runtime.operation_screen_feedback.call_args.args
        card = feedback_card(operation, returned)
        self.assertEqual(card["status"], "saved")
        self.assertIn(result["due_at"], card["detail"])
        self.runtime.notifications[task["task_id"]] = {"fresh": True}
        with self.assertRaises(IdempotencyConflict):
            await voice.execute_tool("reschedule_task", arguments)
        self.assertEqual(self.runtime.notifications[task["task_id"]], {"fresh": True})


if __name__ == "__main__":
    unittest.main()

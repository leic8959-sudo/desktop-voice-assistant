"""语音稍后提醒闭环：真实临时SQLite和假网关运行时，不接云端或设备。"""

import tempfile
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from assistant_core import AssistantStore, IdempotencyConflict
from assistant_core.tools import execute_tool
from gateway import CONFIG_KEY, RUNTIME_KEY, STORE_KEY, TOOL_SCHEMAS, VoiceSession
from llm_providers import chat_tool_schemas
from src.assistant_app.application.screen_feedback import TOOLS, api_operation, feedback_card

NOW = datetime(2026, 11, 5, 2, 0, tzinfo=timezone.utc)


class VoiceSnoozeTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "assistant.sqlite3"
        self.store = AssistantStore(self.path, "offline-voice")
        self.runtime = SimpleNamespace(notifications={}, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        self.voice = VoiceSession({CONFIG_KEY: {}, RUNTIME_KEY: self.runtime, STORE_KEY: self.store}, None, browser=True)

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def create_task(self, *, due_at=None):
        due_at = due_at or (NOW - timedelta(minutes=1)).isoformat()
        with patch("assistant_core.store._utc_now", return_value=NOW - timedelta(minutes=2)):
            return self.store.create_task("离线测试提醒", due_at, "offline-voice-snooze")

    def arguments(self, task, **extra):
        return {"task_id": task["task_id"], "expected_updated_at": task["updated_at"], **extra}

    def test_custom_minutes_persist_and_stale_replay_does_not_extend_again(self):
        task = self.create_task()
        arguments = self.arguments(task, minutes=7)
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = execute_tool(self.store, "snooze_reminder", arguments)
            with self.assertRaises(IdempotencyConflict):
                execute_tool(self.store, "snooze_reminder", arguments)
        with AssistantStore(self.path, "offline-voice") as reopened:
            persisted = reopened.list_tasks()["tasks"][0]
        self.assertEqual(persisted["due_at"], "2026-11-05T02:07:00.000000Z")
        self.assertEqual(persisted, result)
        self.assertEqual(result["status"], "pending")
        self.assertEqual(result["reminder_state"], "pending")

    def test_missing_minutes_uses_existing_five_minute_default(self):
        task = self.create_task()
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = execute_tool(self.store, "snooze_reminder", self.arguments(task))
        self.assertEqual(result["due_at"], "2026-11-05T02:05:00.000000Z")

    def test_invalid_minutes_do_not_change_record(self):
        task = self.create_task()
        with patch("assistant_core.store._utc_now", return_value=NOW):
            for minutes in (0, 61, True, 1.5, "7"):
                with self.subTest(minutes=minutes), self.assertRaises(ValueError):
                    execute_tool(self.store, "snooze_reminder", self.arguments(task, minutes=minutes))
        self.assertEqual(self.store.list_tasks()["tasks"][0], task)

    def test_future_and_completed_reminders_cannot_be_snoozed(self):
        for completed in (False, True):
            with self.subTest(completed=completed):
                task = self.create_task(due_at=(NOW + timedelta(minutes=10)).isoformat())
                if completed:
                    task = self.store.complete_task(task["task_id"])
                with patch("assistant_core.store._utc_now", return_value=NOW), self.assertRaises(ValueError):
                    execute_tool(self.store, "snooze_reminder", self.arguments(task))
                self.assertEqual(self.store.list_tasks()["tasks"][0], task)

    async def test_voice_success_clears_only_old_notification_and_reports_real_due(self):
        task = self.create_task()
        self.runtime.notifications = {task["task_id"]: {"old": True}, "unrelated": {"keep": True}}
        with patch("assistant_core.store._utc_now", return_value=NOW):
            result = await self.voice.execute_tool("snooze_reminder", self.arguments(task, minutes=12))
        self.assertNotIn(task["task_id"], self.runtime.notifications)
        self.assertEqual(self.runtime.notifications["unrelated"], {"keep": True})
        self.assertEqual(result["due_at"], "2026-11-05T02:12:00.000000Z")
        operation, returned = self.runtime.operation_screen_feedback.call_args.args
        card = feedback_card(operation, returned)
        self.assertEqual(card["status"], "saved")
        self.assertIn(result["due_at"], card["detail"])
        self.assertIn("UTC", card["detail"])
        self.assertIn("仍未完成", card["detail"])
        self.assertNotIn("5分钟", card["detail"])

    async def test_voice_conflict_preserves_notification_and_reports_failure(self):
        task = self.create_task()
        arguments = self.arguments(task)
        with patch("assistant_core.store._utc_now", return_value=NOW):
            execute_tool(self.store, "snooze_reminder", arguments)
            self.runtime.notifications[task["task_id"]] = {"new": True}
            with self.assertRaises(IdempotencyConflict):
                await self.voice.execute_tool("snooze_reminder", arguments)
        self.assertEqual(self.runtime.notifications[task["task_id"]], {"new": True})
        failure = self.runtime.operation_screen_feedback.call_args.kwargs["error"]
        self.assertIsInstance(failure, IdempotencyConflict)
        self.assertEqual(feedback_card(TOOLS["snooze_reminder"], error=failure)["status"], "error")

    def test_realtime_and_chat_contracts_require_real_record_revision(self):
        realtime = next(tool for tool in TOOL_SCHEMAS if tool["name"] == "snooze_reminder")
        chat = next(tool["function"] for tool in chat_tool_schemas(TOOL_SCHEMAS) if tool["function"]["name"] == "snooze_reminder")
        for tool in (realtime, chat):
            self.assertEqual(set(tool["parameters"]["required"]), {"task_id", "expected_updated_at"})
            self.assertFalse(tool["parameters"]["additionalProperties"])
            self.assertEqual(tool["parameters"]["properties"]["minutes"]["maximum"], 60)

    def test_api_and_voice_share_real_due_feedback(self):
        operation = api_operation("POST", "/api/tasks/offline-id/snooze", None)
        self.assertEqual(operation, TOOLS["snooze_reminder"])
        task = {"status": "pending", "due_at": "2026-11-05T02:17:00.000000Z"}
        api_card = feedback_card(operation, {"item": task})
        self.assertEqual(api_card, feedback_card(operation, task))
        self.assertIn(task["due_at"], api_card["detail"])
        self.assertEqual(feedback_card(operation, {})["status"], "unknown")


if __name__ == "__main__":
    unittest.main()

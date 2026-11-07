"""屏幕反馈发布检查：隔离网关、假设备，不访问真实账号或硬件。"""

import json
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from aiohttp.test_utils import TestClient, TestServer

from aiohttp import WSMsgType

from assistant_runtime import NativeDevice, RUNTIME_KEY
from gateway import VoiceSession, create_app
from src.assistant_app.application.screen_feedback import ScreenOperation, feedback_card
from src.assistant_app.application.errors import LocalToolRejectedError


class FeedbackTests(unittest.TestCase):
    def test_unknown_failed_and_empty_do_not_become_success(self):
        operation = ScreenOperation("外部操作", True)
        cases = [({"status": "failed"}, "error"), ({"status": "unknown"}, "unknown"), ({"status": "completed", "result": {"status": "unknown"}}, "unknown"), ({"items": []}, "empty")]
        for result, expected in cases:
            with self.subTest(expected=expected):
                self.assertEqual(feedback_card(operation, result)["status"], expected)
        self.assertEqual(feedback_card(operation, error=PermissionError("fake private secret"))["status"], "unknown")
        self.assertEqual(feedback_card(operation, error=LocalToolRejectedError())["status"], "blocked")
        self.assertEqual(feedback_card(operation, error=TimeoutError())["status"], "unknown")

    def test_saved_oauth_and_confirmation_require_explicit_result(self):
        self.assertEqual(feedback_card(ScreenOperation("配置", outcome="configuration"), {})["status"], "unknown")
        self.assertEqual(feedback_card(ScreenOperation("配置", outcome="configuration"), {"saved": True})["status"], "saved")
        preview = ScreenOperation("预览", outcome="confirmation")
        self.assertEqual(feedback_card(preview, {})["status"], "unknown")
        self.assertEqual(feedback_card(preview, {"confirmation_id": "fake-nonce"})["status"], "pending_confirmation")
        self.assertEqual(feedback_card(ScreenOperation("授权", outcome="oauth"), {"authorization_url": "fake private url"})["status"], "pending_authorization")

    def test_private_fields_and_errors_are_not_copied(self):
        private = "private-test-body-token-url"
        for card in [feedback_card(ScreenOperation("读取邮件"), {"items": [{"body": private}], "access_token": private}), feedback_card(ScreenOperation("发送", True), error=RuntimeError(private)), feedback_card(ScreenOperation("预览", outcome="confirmation"), {"confirmation_id": private})]:
            self.assertNotIn(private, str(card))


class FeedbackApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.token = "offline-screen-feedback-token-0000"
        self.app = create_app({"runtime_dir": self.directory.name, "device_token": self.token, "device_id": "offline", "aliyun_model": "offline-model", "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.runtime = self.app[RUNTIME_KEY]
        self.messages = []
        # Native devices live in a set; use a small identity object rather than unhashable namespace.
        class Device:
            socket = SimpleNamespace(closed=False)
            screen_cards_supported = True
            async def send_json(inner, payload): self.messages.append(payload)
        self.device = Device()
        self.runtime.native_devices.add(self.device)
        self.headers = {"Authorization": "Bearer " + self.token}

    async def asyncTearDown(self):
        self.runtime.native_devices.discard(self.device)
        await self.client.close()
        self.directory.cleanup()

    async def test_anonymous_and_polling_do_not_send_cards(self):
        response = await self.client.post("/api/tasks", json={"title": "fake"})
        self.assertEqual(response.status, 401)
        for path in ("/api/status", "/api/connectors", "/api/screen/state"):
            self.assertEqual((await self.client.get(path, headers=self.headers)).status, 200)
        self.assertEqual(self.messages, [])

    async def test_api_empty_result_and_preview_do_not_send_generic_cards(self):
        with patch.object(self.runtime.connector_registry, "invoke", new=AsyncMock(return_value={"items": []})):
            response = await self.client.post("/api/connectors/notion/search", headers=self.headers, json={"arguments": {"query": "fake"}})
            self.assertEqual(response.status, 200)
        self.assertEqual(self.messages, [])
        self.messages.clear()
        response = await self.client.post("/api/workflows/github-issue/prepare", headers=self.headers, json={"repository": "fake/repo", "title": "fake"})
        data = await response.json()
        self.assertEqual(self.messages, [])
        self.assertNotIn(data["confirmation_id"], str(self.messages))

    async def test_voice_and_api_failures_do_not_send_generic_cards(self):
        voice = VoiceSession(self.app, None, browser=True)
        with patch.object(self.runtime.gmail, "search", new=AsyncMock(return_value={"items": []})):
            self.assertEqual(await voice.execute_tool("gmail_search", {"query": "fake"}), {"items": []})
        self.assertEqual(self.messages, [])
        self.messages.clear()
        with patch.object(self.runtime.connector_registry, "invoke", new=AsyncMock(side_effect=TimeoutError("private fake failure"))):
            response = await self.client.post("/api/connectors/notion/search", headers=self.headers, json={"arguments": {"query": "fake"}})
            self.assertEqual(response.status, 502)
        self.assertEqual(self.messages, [])
        self.assertNotIn("private fake failure", str(self.messages))

    async def test_feedback_failure_does_not_change_business_result(self):
        with patch.object(self.runtime, "send_screen_card", new=AsyncMock(side_effect=OSError("offline fake display failure"))):
            response = await self.client.post("/api/tasks", headers=self.headers, json={"title": "offline", "due_at": None, "idempotency_key": "offline-feedback-task"})
        self.assertEqual(response.status, 201)
        self.assertEqual((await response.json())["item"]["status"], "pending")

    async def test_all_operation_feedback_phases_keep_device_clear(self):
        operation = ScreenOperation("外部操作", True)
        for result in ({"status": "completed"}, {"status": "unknown"}, {"status": "failed"}, {"saved": True}):
            await self.runtime.operation_screen_feedback(operation, phase="start", feedback_id="operation-1")
            await self.runtime.operation_screen_feedback(operation, result, feedback_id="operation-1")
        await self.runtime.operation_screen_feedback(operation, error=RuntimeError("private test error"))
        self.assertEqual(self.messages, [])
        self.assertTrue(await self.runtime.completion_snapshot())

    async def test_dedicated_timer_and_reminder_cards_still_work(self):
        await self.runtime.send_timer_screen_card({"id": "timer-1", "revision": 1, "title": "倒计时", "status": "running", "remaining_s": 60})
        await self.runtime.send_task_screen_card({"title": "吃药", "due_at": "2030-01-01T00:00:00+00:00"})
        self.assertEqual([message["kind"] for message in self.messages], ["timer", "task"])

    async def test_send_boundary_blocks_all_generic_result_statuses(self):
        statuses = ("completed", "processing", "error", "unknown", "blocked", "saved", "finished", "observed", "unconfirmed", "verified", "empty", "pending_confirmation", "pending_authorization", "authorized", "unconfigured", "running", "paused", "cancelled", "future_status")
        with self.assertLogs("assistant", level="INFO") as logs:
            for kind in ("task", "home", "weather", "today", "route", "power"):
                for status in statuses:
                    with self.subTest(kind=kind, status=status):
                        delivery = await self.runtime.send_screen_card({"kind": kind, "status": status, "title": "private title", "detail": "private detail"})
                        self.assertEqual(delivery["status"], "disabled")
                        self.assertEqual(delivery["sent_devices"], 0)
        self.assertEqual(self.messages, [])
        self.assertNotIn("private title", "\n".join(logs.output))
        self.assertNotIn("private detail", "\n".join(logs.output))

    async def test_specialized_cards_and_clear_still_reach_device(self):
        cases = [(kind, "") for kind in ("task", "home", "weather", "today", "route", "power")]
        cases += [("timer", status) for status in ("running", "paused", "finished", "cancelled")]
        cases += [("reminder", ""), ("task", "clear"), ("home", "clear")]
        for kind, status in cases:
            with self.subTest(kind=kind, status=status):
                delivery = await self.runtime.send_screen_card({"kind": kind, "status": status, "title": "private title", "detail": "private detail"})
                self.assertEqual(delivery["status"], "sent")
                self.assertEqual(delivery["sent_devices"], 1)
        self.assertEqual([(message["kind"], message["status"]) for message in self.messages], cases)
        self.assertEqual(set(self.device.last_screen_card), {"card_id", "kind", "status", "source"})

    async def test_receipt_metadata_matches_only_latest_card(self):
        device = NativeDevice(SimpleNamespace(closed=False, send_json=AsyncMock()), self.runtime, None)
        device.screen_cards_supported = True
        await self.runtime.send_timer_screen_card({"id": "timer-1", "revision": 1, "title": "private title", "status": "running", "remaining_s": 60}, target=device)
        async def receipt(card_id):
            await device.handle(SimpleNamespace(type=WSMsgType.TEXT, data=json.dumps({"type": "screen_card_ack", "card_id": card_id, "status": "shown"})))
        with self.assertLogs("assistant", level="INFO") as logs:
            await receipt("timer-timer-1-1")
            self.assertEqual(device.last_screen_receipt["kind"], "timer")
            self.assertEqual(device.last_screen_receipt["card_status"], "running")
            self.assertEqual(device.last_screen_receipt["source"], "send_timer_screen_card")
            await receipt("old-unmatched-card")
            self.assertEqual(device.last_screen_receipt["kind"], "unknown")
            self.assertEqual(device.last_screen_receipt["source"], "unknown")
        self.assertNotIn("private title", "\n".join(logs.output))

    async def test_meeting_empty_result_stays_successful_without_card(self):
        result = {"items": []}
        with patch("control_api.plan_meeting_travel", new=AsyncMock(return_value=result)):
            response = await self.client.post("/api/workflows/meeting-travel", headers=self.headers, json={"calendar_connector": "fake", "time_min": "2030-01-01T00:00:00Z", "time_max": "2030-01-02T00:00:00Z", "origin": "fake origin"})
        self.assertEqual(response.status, 200)
        self.assertEqual(await response.json(), result)
        self.assertEqual(self.messages, [])

    async def test_home_api_and_voice_results_never_send_operation_cards(self):
        for result in ({"target_observed": True, "reported_state": "off"}, {"target_observed": False, "reported_state": "off"}):
            self.messages.clear()
            with patch.object(self.runtime.home, "execute_home_action", new=AsyncMock(return_value=result)) as execute:
                response = await self.client.post("/api/home/offline-ac/actions", headers=self.headers, json={"action": "climate.offline:turn_off", "parameters": {}})
                self.assertEqual(response.status, 200)
                self.assertEqual(await response.json(), result)
                execute.assert_awaited_once()
            self.assertEqual(self.messages, [])
            voice = VoiceSession(self.app, None, browser=True)
            with patch.object(voice, "_execute_tool", new=AsyncMock(return_value=result)) as execute:
                self.assertEqual(await voice.execute_tool("execute_home_action", {"device_id": "offline-ac", "action": "turn_off", "parameters": {}}), result)
                execute.assert_awaited_once()
            self.assertEqual(self.messages, [])


if __name__ == "__main__": unittest.main()

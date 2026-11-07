"""OTA 发布前的桌面计时/协议检查；仅临时目录和隔离随机端口。"""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from aiohttp.test_utils import TestClient, TestServer

from assistant_runtime import RUNTIME_KEY
from assistant_core.store import IdempotencyConflict
from gateway import create_app
from src.assistant_app.application.desktop_timer import DesktopTimer


class TimerTests(unittest.TestCase):
    def test_restore_pause_resume_finish_and_duplicate(self):
        with tempfile.TemporaryDirectory() as directory, patch("src.assistant_app.application.desktop_timer.time.time", return_value=1000) as clock:
            timer = DesktopTimer(directory)
            item = timer.create("发布前模拟", 60, "offline-request-1")
            self.assertEqual(timer.create("发布前模拟", 60, "offline-request-1")["id"], item["id"])
            clock.return_value = 1010
            paused = timer.action("pause", item["id"], 1)
            self.assertEqual(paused["remaining_s"], 50)
            with self.assertRaises(IdempotencyConflict): timer.action("pause", item["id"], 1)
            restored = DesktopTimer(directory)
            clock.return_value = 1100
            resumed = restored.action("resume", item["id"], paused["revision"])
            self.assertEqual(resumed["deadline"], 1150)
            clock.return_value = 1160
            self.assertEqual(DesktopTimer(directory).snapshot()["status"], "finished")

    def test_save_failure_keeps_previous_state(self):
        with tempfile.TemporaryDirectory() as directory:
            timer = DesktopTimer(directory)
            item = timer.create("模拟", 60, "offline-request-2")
            previous = timer.path.read_bytes()
            with patch("src.assistant_app.application.desktop_timer.os.replace", side_effect=OSError("offline failure")):
                with self.assertRaises(OSError): timer.action("cancel", item["id"], item["revision"])
            self.assertEqual(timer.state["status"], "running")
            self.assertEqual(timer.path.read_bytes(), previous)


class DesktopApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.token = "offline-desktop-token-00000000"
        self.app = create_app({"runtime_dir": self.directory.name, "device_token": self.token, "device_id": "offline", "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {"Authorization": "Bearer " + self.token}

    async def asyncTearDown(self):
        await self.client.close()
        self.directory.cleanup()

    async def test_auth_timer_and_unknown_firmware_are_truthful(self):
        self.assertEqual((await self.client.get("/api/screen/state")).status, 401)
        response = await self.client.post("/api/screen/timer", json={"title": "模拟", "seconds": 60, "request_id": "offline-api-1"}, headers=self.headers)
        self.assertEqual(response.status, 200)
        data = await response.json()
        self.assertEqual(data["delivery"]["status"], "no_device")
        timer = data["timer"]
        response = await self.client.post("/api/screen/timer/pause", json={"timer_id": timer["id"], "revision": timer["revision"]}, headers=self.headers)
        self.assertEqual((await response.json())["timer"]["status"], "paused")
        self.assertEqual((await self.client.post("/api/screen/timer/pause", json={"timer_id": timer["id"], "revision": timer["revision"]}, headers=self.headers)).status, 409)

    async def test_reminder_snooze_is_single_change(self):
        runtime = self.app[RUNTIME_KEY]
        item = runtime.store.create_task("离线提醒", "2020-01-01T00:00:00Z", "offline-reminder")
        response = await self.client.post(f"/api/tasks/{item['id']}/snooze", headers=self.headers, json={"updated_at": item["updated_at"]})
        self.assertEqual(response.status, 200)
        self.assertEqual((await response.json())["item"]["status"], "pending")
        repeated = await self.client.post(f"/api/tasks/{item['id']}/snooze", headers=self.headers, json={"updated_at": item["updated_at"]})
        self.assertEqual(repeated.status, 409)


if __name__ == "__main__": unittest.main()

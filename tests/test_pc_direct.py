"""PC控制直接执行回归，所有系统动作在初始化前替换，不触碰真实桌面。"""

import tempfile
import unittest
from unittest.mock import AsyncMock, Mock, patch

from aiohttp.test_utils import TestClient, TestServer

from gateway import VoiceSession, create_app
from src.assistant_app.api.desktop_actions import DESKTOP_ACTIONS_KEY


class DirectPcApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.patches = [
            patch("src.assistant_app.application.desktop_actions.WindowsMediaPlayer", return_value=Mock(opened=False)),
            patch("src.assistant_app.application.desktop_actions.DesktopActionsService._open_app", return_value={"status": "requested"}),
            patch("src.assistant_app.application.desktop_actions.DesktopActionsService._lock_windows", return_value={"status": "requested"}),
            patch("src.assistant_app.application.desktop_actions.DesktopActionsService._send_key"),
        ]
        self.player, self.open_app, self.lock_windows, self.send_key = [item.start() for item in self.patches]
        self.token = "local-direct-pc-test-credential"
        self.app = create_app({"runtime_dir": self.directory.name, "device_id": "offline", "device_token": self.token, "aliyun_voice": "Tina", "aliyun_model": "mock", "enable_search": False, "home_entities": {}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {"Authorization": "Bearer " + self.token}

    async def asyncTearDown(self):
        await self.client.close()
        for item in reversed(self.patches):
            item.stop()
        self.directory.cleanup()

    async def test_authenticated_pc_controls_execute_without_confirmation_token(self):
        for action, arguments in (("desktop_open_app", {"app": "calculator"}), ("desktop_lock", {}), ("desktop_volume", {"direction": "down", "steps": 2}), ("desktop_media_key", {"key": "next"})):
            response = await self.client.post("/api/computer/actions/" + action, headers=self.headers, json={"arguments": arguments})
            self.assertEqual(response.status, 200, await response.text())
            self.assertNotEqual((await response.json()).get("status"), "confirmation_required")
        self.open_app.assert_called_once_with("calculator")
        self.lock_windows.assert_called_once_with()
        self.assertEqual(self.send_key.call_count, 2)

    async def test_auth_and_allowlist_remain_required(self):
        response = await self.client.post("/api/computer/actions/desktop_lock", json={"arguments": {}})
        self.assertEqual(response.status, 401)
        response = await self.client.post("/api/computer/actions/desktop_open_app", headers=self.headers, json={"arguments": {"app": "cmd.exe"}})
        self.assertEqual(response.status, 400)
        self.lock_windows.assert_not_called()
        self.open_app.assert_not_called()
        with self.assertRaises(PermissionError):
            await self.app[DESKTOP_ACTIONS_KEY].execute_direct("oauth_refresh", {"provider": "google_calendar"})

    async def test_voice_uses_explicit_model_tool_with_real_allowlist_without_replanning(self):
        session = VoiceSession(self.app, Mock(send_json=AsyncMock()), browser=True)
        with patch("gateway.plan_voice_control", new=AsyncMock(side_effect=AssertionError("不得二次规划"))) as planner:
            with self.assertRaises(PermissionError):
                await session._execute_tool("desktop_open_app", {"app": "calculator"})
            await session.observe_user_text("打开那个算数的", refresh=False, source="text")
            await session._execute_tool("desktop_open_app", {"app": "calculator"})
            self.open_app.assert_called_once_with("calculator")
            with self.assertRaises(ValueError):
                await session._execute_tool("desktop_open_app", {"app": "cmd.exe"})
            with self.assertRaises(ValueError):
                await session._execute_tool("desktop_volume", {"direction": "invalid"})
            planner.assert_not_awaited()
        self.lock_windows.assert_not_called()
        self.send_key.assert_not_called()


if __name__ == "__main__":
    unittest.main()

"""Gmail 面板认证、回调和原生工具边界；不请求 Google。"""

import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock

from aiohttp.test_utils import TestClient, TestServer

from assistant_runtime import RUNTIME_KEY
from gateway import create_app, session_settings, VoiceSession
from gmail_api import _validate_settings
from gmail_service import DpapiTokenVault, GmailError
from gmail_tools import TOOL_SCHEMAS


class GmailApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.token = "gmail-api-test-device-token-0000000"
        self.config = {"device_token": self.token, "device_id": "gmail-test-device", "runtime_dir": self.directory.name, "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}}
        self.client = TestClient(TestServer(create_app(self.config)))
        await self.client.start_server()
        self.headers = {"Authorization": "Bearer " + self.token}

    async def asyncTearDown(self):
        await self.client.close()
        self.directory.cleanup()

    async def test_missing_credentials_is_safe_and_status_requires_authentication(self):
        response = await self.client.get("/api/gmail/status")
        self.assertEqual(response.status, 401)
        response = await self.client.get("/api/gmail/status", headers=self.headers)
        payload = await response.json()
        self.assertFalse(payload["configured"])
        self.assertFalse(payload["connected"])
        self.assertFalse(payload["send_enabled"])
        self.assertNotIn("client_secret", payload)
        response = await self.client.post("/api/gmail/oauth/start", json={}, headers=self.headers)
        self.assertEqual(response.status, 409)

    async def test_secret_configuration_blocked_through_tunnel_and_stored_encrypted_locally(self):
        payload = {"client_id": "mock-client.apps.googleusercontent.com", "client_secret": "mock-not-a-real-secret", "redirect_uri": "http://127.0.0.1:8765/gmail/oauth/callback", "send_enabled": False}
        response = await self.client.post("/api/gmail/config", headers={**self.headers, "X-Forwarded-For": "203.0.113.2"}, json=payload)
        self.assertEqual(response.status, 403)
        self.assertFalse((Path(self.directory.name) / "gmail_settings.dpapi").exists())
        response = await self.client.post("/api/gmail/config", headers=self.headers, json=payload)
        self.assertEqual(response.status, 200)
        result = await response.json()
        self.assertTrue(result["configured"])
        self.assertNotIn("mock-not-a-real-secret", json.dumps(result))
        encrypted = (Path(self.directory.name) / "gmail_settings.dpapi").read_bytes()
        self.assertNotIn(b"mock-not-a-real-secret", encrypted)
        self.assertEqual(DpapiTokenVault(Path(self.directory.name) / "gmail_settings.dpapi").load()["gmail_client_id"], payload["client_id"])

    async def test_oauth_callback_is_state_protected_and_does_not_echo_code(self):
        response = await self.client.get("/gmail/oauth/callback?state=invalid&code=private-code")
        self.assertEqual(response.status, 400)
        body = await response.text()
        self.assertNotIn("private-code", body)
        self.assertEqual(response.headers["Referrer-Policy"], "no-referrer")
        self.assertEqual(response.headers["Cache-Control"], "no-store")

    async def test_callback_network_error_is_not_reported_as_expired_and_is_escaped(self):
        service = self.client.app[RUNTIME_KEY].gmail
        service.complete_authorization = AsyncMock(side_effect=GmailError("无法连接 Google 服务，检查代理 <test>"))
        response = await self.client.get("/gmail/oauth/callback?state=mock-state&code=private-code")
        self.assertEqual(response.status, 400)
        body = await response.text()
        self.assertIn("检查代理", body)
        self.assertIn("&lt;test&gt;", body)
        self.assertNotIn("已过期", body)
        self.assertNotIn("private-code", body)

    async def test_panel_create_update_and_send_contract(self):
        service = self.client.app[RUNTIME_KEY].gmail
        service.create_draft = AsyncMock(return_value={"id": "draft-1", "to": "test@example.com", "subject": "Test", "body": "Body", "revision": "v1"})
        service.update_draft = AsyncMock(return_value={"id": "draft-1", "revision": "v2"})
        service.prepare_send = AsyncMock(return_value={"confirmation_id": "one-use", "draft": {"id": "draft-1"}})
        service.confirm_send = AsyncMock(return_value={"status": "unknown"})
        data = {"to": "test@example.com", "subject": "Test", "body": "Body"}
        response = await self.client.post("/api/gmail/drafts", headers=self.headers, json=data)
        self.assertEqual((await response.json())["id"], "draft-1")
        service.create_draft.assert_awaited_once()
        response = await self.client.put("/api/gmail/drafts/draft-1", headers=self.headers, json={**data, "revision": "v1"})
        self.assertEqual(response.status, 200)
        self.assertEqual(service.update_draft.await_args.kwargs["revision"], "v1")
        response = await self.client.post("/api/gmail/drafts/draft-1/prepare-send", headers=self.headers, json={})
        self.assertEqual((await response.json())["confirmation_id"], "one-use")
        response = await self.client.post("/api/gmail/drafts/draft-1/send", headers=self.headers, json={"confirmation_id": "one-use"})
        self.assertEqual((await response.json())["status"], "unknown")
        service.confirm_send.assert_awaited_once_with("draft-1", "one-use")

    async def test_model_cannot_prepare_or_send_and_has_valid_tool_shapes(self):
        session = VoiceSession(self.client.app, None, browser=True)
        for name in ("gmail_send", "gmail_prepare_send", "gmail_send_draft"):
            with self.assertRaises(ValueError):
                await session.execute_tool(name, {})
        for item in TOOL_SCHEMAS:
            self.assertEqual(item["type"], "function")
            self.assertFalse(item["parameters"]["additionalProperties"])
        settings = session_settings(self.config, "")
        self.assertTrue(any(item["name"] == "gmail_search" for item in settings["session"]["tools"]))
        self.assertIn("不可信", settings["session"]["instructions"])

    def test_callback_and_send_setting_validation(self):
        initial = {"gmail_client_id": "test", "gmail_client_secret": "secret"}
        for uri in ("http://example.com/gmail/oauth/callback", "https://example.com/another", "https://user:password@example.com/gmail/oauth/callback", "https://example.com/gmail/oauth/callback?q=1"):
            with self.assertRaises(ValueError):
                _validate_settings({"redirect_uri": uri}, initial)
        with self.assertRaises(ValueError):
            _validate_settings({"send_enabled": "true"}, initial)


if __name__ == "__main__":
    unittest.main()

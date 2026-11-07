"""管理员会话跨网关重建仍有效，不触碰真实设备和账号。"""

import tempfile
import unittest

from aiohttp.test_utils import TestClient, TestServer

from gateway import create_app


class AdminSessionApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.config = {"device_token": "offline-admin-token-for-persistence", "device_id": "offline-admin", "runtime_dir": self.directory.name, "aliyun_voice": "Tina", "aliyun_model": "mock", "enable_search": False, "home_entities": {}}
        self.headers = {"Host": "127.0.0.1:8765", "Origin": "http://127.0.0.1:8765"}
        await self.start_client()

    async def start_client(self):
        self.client = TestClient(TestServer(create_app(self.config)))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        self.directory.cleanup()

    async def test_cookie_survives_gateway_recreate_and_logout_revokes(self):
        response = await self.client.post("/api/auth/local-login", headers=self.headers, json={})
        self.assertEqual(response.status, 200)
        cookie = response.cookies["assistant_admin"]
        self.assertEqual(cookie["path"], "/")
        self.assertEqual(int(cookie["max-age"]), 30 * 86400)
        self.assertTrue(cookie["httponly"])
        authenticated = {**self.headers, "Cookie": "assistant_admin=" + cookie.value}
        await self.client.close()
        await self.start_client()
        response = await self.client.get("/api/status", headers=authenticated)
        self.assertEqual(response.status, 200)
        response = await self.client.post("/api/auth/local-login", headers=authenticated, json={})
        self.assertTrue((await response.json())["restored"])
        response = await self.client.post("/api/auth/logout", headers=authenticated, json={})
        self.assertEqual(response.status, 200)
        self.assertEqual((await self.client.get("/api/status", headers=authenticated)).status, 401)

    async def test_rotated_credential_invalidates_previous_cookie(self):
        response = await self.client.post("/api/auth/local-login", headers=self.headers, json={})
        token = response.cookies["assistant_admin"].value
        await self.client.close()
        self.config["device_token"] = "rotated-offline-admin-for-persistence"
        await self.start_client()
        response = await self.client.get("/api/status", headers={**self.headers, "Cookie": "assistant_admin=" + token})
        self.assertEqual(response.status, 401)


if __name__ == "__main__":
    unittest.main()

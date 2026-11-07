"""扫地机器人边界验证：只访问本地模拟平台，不调用真实米家或语音模型。"""

import asyncio
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from assistant_runtime import RUNTIME_KEY
from gateway import create_app, VoiceSession
from home_control import HomeController
from tests.home_fixture import register_home_api


class VacuumTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.writes = []
        self.state = "docked"
        self.features = 8192 | 4 | 16
        self.status = 200
        self.update_state = True

        async def read(request):
            return web.json_response({"state": self.state, "attributes": {"supported_features": self.features, "battery_level": 85}})

        async def write(request):
            self.writes.append((request.match_info["action"], await request.json()))
            if self.status == 200 and self.update_state:
                self.state = {"start": "cleaning", "pause": "paused", "return_to_base": "returning"}[request.match_info["action"]]
            return web.json_response([], status=self.status)

        app = web.Application()
        app.router.add_get("/api/states/vacuum.m40", read)
        app.router.add_post("/api/services/vacuum/{action}", write)
        register_home_api(app, lambda: [{"entity_id": "vacuum.m40", "state": self.state, "attributes": {"supported_features": self.features, "battery_level": 85}}], entity_ids=("vacuum.m40",), domain_services={"vacuum": ("start", "pause", "return_to_base")})
        self.server = TestServer(app)
        await self.server.start_server()
        self.http = aiohttp.ClientSession()
        self.temp = tempfile.TemporaryDirectory()
        self.home_config = {"home_assistant_url": str(self.server.make_url("")), "home_assistant_token": "mock-ha-token", "home_entities": {"M40": "vacuum.m40"}, "runtime_dir": self.temp.name}
        self.controller = HomeController(self.home_config, self.http)

    async def asyncTearDown(self):
        await self.http.close()
        await self.server.close()
        self.temp.cleanup()

    async def test_read_is_read_only_and_returns_actual_capabilities(self):
        result = await self.controller.read("M40")
        self.assertEqual(result["state"], "docked")
        self.assertEqual(result["battery_level"], 85)
        self.assertEqual(set(result["supported_actions"]), {"start", "pause", "return_to_base"})
        self.assertEqual(self.writes, [])

    async def test_each_action_writes_once_and_returns_reported_state(self):
        for action, state in (("start", "cleaning"), ("pause", "paused"), ("return_to_base", "returning")):
            result = await self.controller.command_vacuum("M40", action)
            self.assertEqual(result["reported_state"], state)
            self.assertTrue(result["target_observed"])
            self.assertEqual(result["evidence"], "home_assistant_reported_state")
        self.assertEqual(self.writes, [(action, {"entity_id": "vacuum.m40"}) for action in ("start", "pause", "return_to_base")])

    async def test_unknown_entity_invalid_action_and_binary_switch_are_rejected(self):
        for alias, action in (("other", "start"), ("M40", "toggle"), ("M40", [])):
            with self.assertRaises(ValueError):
                await self.controller.command_vacuum(alias, action)
        with self.assertRaises(ValueError):
            await self.controller.set("M40", "on")
        self.assertEqual(self.writes, [])

    async def test_offline_or_unsupported_action_never_writes(self):
        self.features = 4
        with self.assertRaises(ValueError):
            await self.controller.command_vacuum("M40", "start")
        self.state = "unavailable"
        with self.assertRaises(ValueError):
            await self.controller.command_vacuum("M40", "pause")
        self.assertEqual(self.writes, [])

    async def test_upstream_failure_and_timeout_do_not_retry(self):
        self.status = 504
        with self.assertRaises(aiohttp.ClientResponseError):
            await self.controller.command_vacuum("M40", "start")
        self.assertEqual(len(self.writes), 1)
        with patch.object(self.http, "post", side_effect=asyncio.TimeoutError) as post:
            with self.assertRaises(asyncio.TimeoutError):
                await self.controller.command_vacuum("M40", "pause")
            self.assertEqual(post.call_count, 1)

    async def test_accepted_command_without_state_change_is_unconfirmed(self):
        self.update_state = False
        result = await self.controller.command_vacuum("M40", "start")
        self.assertTrue(result["request_accepted"])
        self.assertFalse(result["target_observed"])
        self.assertEqual(result["reported_state"], "docked")
        self.assertEqual(len(self.writes), 1)

    async def test_api_auth_and_voice_dispatch_use_vacuum_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            token = "mock-assistant-token-at-least-24-characters"
            config = {**self.home_config, "device_token": token, "device_id": "vacuum-test", "runtime_dir": directory, "aliyun_voice": "Tina", "enable_search": False}
            client = TestClient(TestServer(create_app(config)))
            await client.start_server()
            try:
                response = await client.post("/api/home/M40", json={"action": "start"})
                self.assertEqual(response.status, 401)
                self.assertEqual(self.writes, [])
                headers = {"Authorization": "Bearer " + token}
                response = await client.post("/api/home/M40", json={"action": "pause"}, headers=headers)
                self.assertEqual(response.status, 200)
                self.assertEqual((await response.json())["reported_state"], "paused")
                response = await client.post("/api/home/M40", json={"state": "on"}, headers=headers)
                self.assertEqual(response.status, 400)
                session = VoiceSession(client.app, None, browser=True)
                result = await session.execute_tool("control_vacuum", {"entity": "M40", "action": "return_to_base"})
                self.assertEqual(result["reported_state"], "returning")
                self.assertEqual(len(self.writes), 2)
                self.assertTrue(client.app[RUNTIME_KEY].home.configured)
            finally:
                await client.close()


if __name__ == "__main__":
    unittest.main()

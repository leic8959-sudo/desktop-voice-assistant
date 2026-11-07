"""空调控制使用本地模拟平台；不改变真实空调状态。"""

import asyncio
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gateway import create_app, VoiceSession
from home_control import HomeController
from tests.home_fixture import register_home_api


class ClimateTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.state = "off"
        self.attributes = {"hvac_modes": ["cool", "heat", "off"], "supported_features": 429, "current_temperature": 22.9, "temperature": 27, "min_temp": 16, "max_temp": 31, "target_temp_step": 0.5}
        self.writes = []

        async def read(request):
            return web.json_response({"state": self.state, "attributes": self.attributes})

        async def write(request):
            payload = await request.json()
            self.writes.append((request.match_info["action"], payload))
            # 模拟真实 HA：turn_on 恢复到非 off 模式，turn_off 回到 off。
            if request.match_info["action"] == "turn_on":
                self.state = "cool"
            elif request.match_info["action"] == "turn_off":
                self.state = "off"
            if "hvac_mode" in payload:
                self.state = payload["hvac_mode"]
            if "temperature" in payload:
                self.attributes["temperature"] = payload["temperature"]
            return web.json_response([])

        app = web.Application()
        app.router.add_get("/api/states/climate.air", read)
        app.router.add_post("/api/services/climate/{action}", write)
        register_home_api(app, lambda: [{"entity_id": "climate.air", "state": self.state, "attributes": self.attributes}], entity_ids=("climate.air",), domain_services={"climate": ("set_hvac_mode", "set_temperature", "turn_on", "turn_off")})
        self.server = TestServer(app)
        await self.server.start_server()
        self.http = aiohttp.ClientSession()
        self.temp = tempfile.TemporaryDirectory()
        self.config = {"home_assistant_url": str(self.server.make_url("")), "home_assistant_token": "mock", "home_entities": {"空调": "climate.air"}, "runtime_dir": self.temp.name}
        self.controller = HomeController(self.config, self.http)

    async def asyncTearDown(self):
        await self.http.close()
        await self.server.close()
        self.temp.cleanup()

    async def test_read_returns_modes_and_temperatures_without_writing(self):
        result = await self.controller.read("空调")
        self.assertEqual(result["current_temperature"], 22.9)
        self.assertEqual(result["temperature"], 27)
        self.assertEqual(result["hvac_modes"], ["cool", "heat", "off"])
        # 特性429含TURN_ON/TURN_OFF，能力目录按真实设备特性补齐开关机。
        self.assertEqual(result["supported_actions"], ["set_hvac_mode", "turn_on", "turn_off", "set_temperature"])
        self.assertEqual(self.writes, [])

    async def test_explicit_mode_writes_once_and_confirms_report(self):
        result = await self.controller.command_climate("空调", "set_hvac_mode", hvac_mode="cool")
        self.assertTrue(result["target_observed"])
        self.assertEqual(result["reported_state"], "cool")
        self.assertEqual(self.writes, [("set_hvac_mode", {"entity_id": "climate.air", "hvac_mode": "cool"})])

    async def test_setting_temperature_does_not_select_mode_or_change_room_temperature(self):
        self.state = "cool"
        result = await self.controller.command_climate("空调", "set_temperature", temperature=24.5)
        self.assertTrue(result["target_observed"])
        self.assertEqual(result["reported_state"], "cool")
        self.assertEqual(result["current_temperature"], 22.9)
        self.assertEqual(self.writes, [("set_temperature", {"entity_id": "climate.air", "temperature": 24.5})])

    async def test_temperature_while_powered_off_requires_power_on_first(self):
        # 关机调温在本地返回前置条件错误，不写服务；先开机（回读到非off）后调温才走通。
        with self.assertRaises(ValueError):
            await self.controller.command_climate("空调", "set_temperature", temperature=24.5)
        self.assertEqual(self.writes, [])
        result = await self.controller.execute_home_action("空调", "turn_on", {})
        self.assertEqual(result["reported_state"], "cool")
        self.assertTrue(result["target_observed"])
        result = await self.controller.command_climate("空调", "set_temperature", temperature=24.5)
        self.assertTrue(result["target_observed"])
        self.assertEqual(self.writes, [("turn_on", {"entity_id": "climate.air"}), ("set_temperature", {"entity_id": "climate.air", "temperature": 24.5})])

    async def test_missing_or_unsupported_mode_and_invalid_temperatures_never_write(self):
        for mode in (None, "auto", "on", []):
            with self.assertRaises(ValueError):
                await self.controller.command_climate("空调", "set_hvac_mode", hvac_mode=mode)
        for temperature in (None, True, "24", 15, 32, 24.1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                await self.controller.command_climate("空调", "set_temperature", temperature=temperature)
        self.assertEqual(self.writes, [])

    async def test_unavailable_or_unsupported_temperature_control_never_writes(self):
        self.attributes["supported_features"] = 0
        with self.assertRaises(ValueError):
            await self.controller.command_climate("空调", "set_temperature", temperature=24)
        self.state = "unavailable"
        with self.assertRaises(ValueError):
            await self.controller.command_climate("空调", "set_hvac_mode", hvac_mode="cool")
        self.assertEqual(self.writes, [])

    async def test_write_timeout_is_not_retried(self):
        self.state = "cool"
        with patch.object(self.http, "post", side_effect=asyncio.TimeoutError) as post:
            with self.assertRaises(asyncio.TimeoutError):
                await self.controller.command_climate("空调", "set_temperature", temperature=24)
            self.assertEqual(post.call_count, 1)

    async def test_api_and_voice_dispatch_enforce_climate_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            token = "local-climate-test-token-more-than-24"
            config = {**self.config, "device_token": token, "device_id": "climate-test", "runtime_dir": directory, "aliyun_voice": "Tina", "enable_search": False}
            client = TestClient(TestServer(create_app(config)))
            await client.start_server()
            try:
                payload = {"action": "set_hvac_mode", "hvac_mode": "cool"}
                response = await client.post("/api/home/空调", json=payload)
                self.assertEqual(response.status, 401)
                self.assertEqual(self.writes, [])
                headers = {"Authorization": "Bearer " + token}
                response = await client.post("/api/home/空调", json=payload, headers=headers)
                self.assertEqual(response.status, 200)
                self.assertTrue((await response.json())["target_observed"])
                session = VoiceSession(client.app, None, browser=True)
                result = await session.execute_tool("control_climate", {"entity": "空调", "action": "set_temperature", "temperature": 24})
                self.assertEqual(result["temperature"], 24)
                self.assertEqual(len(self.writes), 2)
            finally:
                await client.close()


if __name__ == "__main__":
    unittest.main()

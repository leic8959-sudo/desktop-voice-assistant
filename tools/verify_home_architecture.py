"""主代理独立黑盒验收：模拟HA正式接口，无真实家电/云模型调用。"""

import asyncio
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from urllib.parse import quote

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from gateway import create_app
from home_control import HomeController


class IndependentHomeAcceptance(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.writes = []
        self.discovery_requests = 0
        self.fail_discovery = False
        self.write_status = 200
        self.fail_confirmation = False
        self.chunked_discovery = False
        self.registry_devices = []
        self.entities = []
        self.states = []
        self.areas = [{"area_id": "living", "name": "客厅"}, {"area_id": "bedroom", "name": "卧室"}]
        self.services = {
            "vacuum": {name: {"name": name, "target": {"entity": [{"domain": "vacuum"}]}, "fields": {}} for name in ("start", "pause", "return_to_base")},
            "climate": {
                "set_hvac_mode": {"name": "Set mode", "target": {"entity": [{"domain": "climate"}]}, "fields": {"hvac_mode": {"required": True, "selector": {"select": {"options": ["off", "cool", "heat"]}}}}},
                "set_temperature": {"name": "Set temperature", "target": {"entity": [{"domain": "climate"}]}, "fields": {"temperature": {"required": True, "selector": {"number": {"min": 0, "max": 100, "step": 0.1}}}}},
            },
            "light": {name: {"name": name, "target": {"entity": [{"domain": "light"}]}, "fields": {}} for name in ("turn_on", "turn_off")},
            "fan": {name: {"name": name, "target": {"entity": [{"domain": "fan"}]}, "fields": {}} for name in ("turn_on", "turn_off")},
            "lock": {"unlock": {"name": "Unlock", "fields": {}}},
            "button": {"press": {"name": "Press", "fields": {}}},
            "homeassistant": {"restart": {"name": "Restart", "fields": {}}},
        }
        self.add_device("vac_a", "扫地机器人", "vacuum.original", "vendor.old-vacuum", "docked", {"supported_features": 8192 | 16})
        self.add_device("ac_a", "空调", "climate.air", "vendor.air-v1", "off", {"supported_features": 1, "hvac_modes": ["off", "cool", "heat"], "temperature": 24, "current_temperature": 22, "min_temp": 16, "max_temp": 31, "target_temp_step": 0.5})
        self.add_device("combo", "风扇灯", "light.combo", "vendor.combo", "off", {"supported_color_modes": ["onoff"]})
        self.add_entity("combo", "fan.combo", "off", {"supported_features": 16 | 32})
        self.add_device("heater", "电暖器", "climate.heater", "vendor.heater", "off", {"supported_features": 1, "hvac_modes": ["off", "heat"], "temperature": 20, "min_temp": 16, "max_temp": 30, "target_temp_step": 1})
        self.add_device("lock", "门锁", "lock.front", "vendor.lock", "locked", {})
        self.add_entity("vac_a", "button.factory_reset", "unknown", {}, category="config")
        for index in range(45):
            self.add_entity("vac_a", f"sensor.vacuum_{index}", str(index), {"unit_of_measurement": "%"})
        app = web.Application()
        app.router.add_get("/api/websocket", self.websocket)
        app.router.add_get("/api/states", self.all_states)
        app.router.add_get("/api/states/{entity_id}", self.one_state)
        app.router.add_get("/api/services", self.all_services)
        app.router.add_post("/api/services/{domain}/{service}", self.write)
        self.ha = TestServer(app)
        await self.ha.start_server()
        self.token = "independent-home-acceptance-token-0000"
        self.headers = {"Authorization": "Bearer " + self.token}
        self.config = {"device_token": self.token, "device_id": "independent-acceptance", "runtime_dir": self.folder.name, "aliyun_voice": "Tina", "enable_search": False, "home_assistant_url": str(self.ha.make_url("")), "home_assistant_token": "fake-ha-token", "home_entities": {"M40": "vacuum.original", "空调": "climate.air"}}
        self.client = TestClient(TestServer(create_app(copy.deepcopy(self.config))))
        await self.client.start_server()

    def add_device(self, identifier, name, entity_id, model, state, attributes):
        self.registry_devices.append({"id": identifier, "name": name, "name_by_user": None, "manufacturer": "fixture", "model": model, "area_id": "living", "disabled_by": None, "config_entries": ["xiaomi"], "primary_config_entry": "xiaomi"})
        self.add_entity(identifier, entity_id, state, attributes)

    def add_entity(self, device_id, entity_id, state, attributes, category=None):
        self.entities.append({"id": "registry-" + entity_id, "entity_id": entity_id, "unique_id": "unique-" + entity_id, "device_id": device_id, "area_id": None, "name": None, "original_name": entity_id, "disabled_by": None, "hidden_by": None, "entity_category": category, "config_entry_id": "xiaomi", "platform": "xiaomi_home"})
        self.states.append({"entity_id": entity_id, "state": state, "attributes": {"friendly_name": entity_id, **attributes}})

    async def websocket(self, request):
        socket = web.WebSocketResponse()
        await socket.prepare(request)
        await socket.send_json({"type": "auth_required", "ha_version": "2026.10.4"})
        authentication = await socket.receive_json()
        if authentication.get("access_token") != "fake-ha-token":
            await socket.send_json({"type": "auth_invalid"})
            return socket
        await socket.send_json({"type": "auth_ok", "ha_version": "2026.10.4"})
        async for message in socket:
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            command = json.loads(message.data)
            values = {"config/device_registry/list": self.registry_devices, "config/entity_registry/list": self.entities, "config/area_registry/list": self.areas, "get_states": self.states, "get_services": self.services, "subscribe_events": None, "unsubscribe_events": None}
            if command["type"] not in values or self.fail_discovery:
                await socket.send_json({"id": command["id"], "type": "result", "success": False, "error": {"code": "fixture_failure", "message": "Independent discovery failure"}})
            else:
                self.discovery_requests += command["type"] not in ("subscribe_events", "unsubscribe_events")
                await socket.send_json({"id": command["id"], "type": "result", "success": True, "result": values[command["type"]]})
        return socket

    async def all_states(self, request):
        self.discovery_requests += 1
        if self.chunked_discovery and not self.fail_discovery:
            response = web.StreamResponse(headers={"Content-Type": "application/json"})
            await response.prepare(request)
            data = json.dumps(self.states).encode("utf-8")
            for offset in range(0, len(data), 512):
                await response.write(data[offset:offset + 512])
                await asyncio.sleep(0.002)
            await response.write_eof()
            return response
        return web.json_response(self.states, status=503 if self.fail_discovery else 200)

    async def all_services(self, request):
        self.discovery_requests += 1
        return web.json_response([{"domain": key, "services": value} for key, value in self.services.items()], status=503 if self.fail_discovery else 200)

    async def one_state(self, request):
        if self.fail_confirmation and self.writes:
            return web.json_response({"error": "confirmation unavailable"}, status=503)
        for state in self.states:
            if state["entity_id"] == request.match_info["entity_id"]:
                return web.json_response(state)
        raise web.HTTPNotFound()

    async def write(self, request):
        payload = await request.json()
        self.writes.append({"domain": request.match_info["domain"], "service": request.match_info["service"], "payload": payload})
        if self.write_status != 200:
            return web.json_response({"error": "fixture upstream failure"}, status=self.write_status)
        for state in self.states:
            if state["entity_id"] == payload.get("entity_id"):
                if "hvac_mode" in payload:
                    state["state"] = payload["hvac_mode"]
                elif "temperature" in payload:
                    state["attributes"]["temperature"] = payload["temperature"]
                else:
                    state["state"] = {"turn_on": "on", "turn_off": "off", "start": "cleaning", "pause": "paused", "return_to_base": "returning"}.get(request.match_info["service"], state["state"])
        return web.json_response([])

    async def asyncTearDown(self):
        await self.client.close()
        await self.ha.close()
        self.folder.cleanup()

    async def devices(self, refresh=False, enabled_only=False):
        response = await self.client.get(f"/api/home?enabled_only={str(enabled_only).lower()}&refresh={str(refresh).lower()}&limit=30", headers=self.headers)
        self.assertEqual(response.status, 200, await response.text())
        return (await response.json())["items"]

    async def authorize(self, identifier, enabled):
        return await self.client.post(f"/api/home/{quote(identifier, safe='')}/authorization", headers=self.headers, json={"enabled": enabled})

    async def execute(self, identifier, action, parameters=None):
        return await self.client.post(f"/api/home/{quote(identifier, safe='')}/actions", headers=self.headers, json={"action": action, "parameters": parameters or {}})

    def find_action(self, device, needle):
        return next(action for action in device["actions"] if needle in action["id"])

    @staticmethod
    def fixed_parameters(action):
        schema = action["parameters"]
        return {key: schema["properties"][key]["enum"][0] for key in schema.get("required", []) if len(schema["properties"][key].get("enum", [])) == 1}

    async def test_01_discovery_groups_entities_without_authorizing_every_device(self):
        devices = await self.devices(refresh=True)
        self.assertGreaterEqual(len(devices), 4)
        self.assertLessEqual(len(devices), 5)
        self.assertEqual(len({item["device_id"] for item in devices}), len(devices))
        enabled = [item for item in devices if item["enabled"]]
        self.assertEqual(len(enabled), 2)
        self.assertEqual(len(await self.devices(enabled_only=True)), 2)
        for item in devices:
            self.assertIn("area", item)
            for action in item["actions"]:
                self.assertIn("parameters", action)
                self.assertNotIn("factory_reset", action["id"])
                self.assertNotIn("unlock", action["id"])
        self.assertEqual(self.writes, [])

    async def test_02_new_model_appears_and_is_enabled_without_configuration_edit(self):
        await self.devices(refresh=True)
        self.add_device("vac_b", "扫地机器人", "vacuum.new_vendor", "entirely.new.model", "docked", {"supported_features": 8192 | 16})
        devices = await self.devices(refresh=True)
        new = next(item for item in devices if "vacuum.new_vendor" in item["entity_ids"])
        self.assertFalse(new["enabled"])
        self.assertTrue(any("start" in action["id"] for action in new["actions"]))
        response = await self.authorize(new["device_id"], True)
        self.assertEqual(response.status, 200, await response.text())
        self.assertIn(new["device_id"], {item["device_id"] for item in await self.devices(enabled_only=True)})
        self.assertEqual(self.config["home_entities"], {"M40": "vacuum.original", "空调": "climate.air"})
        self.assertEqual(self.writes, [])

    async def test_03_unauthorized_device_and_arbitrary_service_never_write(self):
        devices = await self.devices(refresh=True)
        combo = next(item for item in devices if "light.combo" in item["entity_ids"])
        action = self.find_action(combo, "turn_on")
        response = await self.execute(combo["device_id"], action["id"])
        self.assertIn(response.status, (400, 403))
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        response = await self.execute(vacuum["device_id"], "homeassistant.restart")
        self.assertIn(response.status, (400, 403))
        self.assertEqual(self.writes, [])

    async def test_04_revocation_survives_restart_without_seed_resurrection(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        response = await self.authorize(vacuum["device_id"], False)
        self.assertEqual(response.status, 200, await response.text())
        await self.client.close()
        self.client = TestClient(TestServer(create_app(copy.deepcopy(self.config))))
        await self.client.start_server()
        devices = await self.devices(refresh=True)
        self.assertFalse(next(item for item in devices if item["device_id"] == vacuum["device_id"])["enabled"])
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        self.assertIn(response.status, (400, 403))
        self.assertEqual(self.writes, [])

    async def test_05_invalid_enum_range_and_type_are_rejected_before_ha_write(self):
        devices = await self.devices(refresh=True)
        air = next(item for item in devices if "climate.air" in item["entity_ids"])
        temperature = self.find_action(air, "set_temperature")
        mode = self.find_action(air, "set_hvac_mode")
        for parameters in ({"temperature": 32}, {"temperature": 24.1}, {"temperature": True}, {"temperature": 24, "entity_id": "climate.heater"}):
            response = await self.execute(air["device_id"], temperature["id"], parameters)
            self.assertIn(response.status, (400, 403), await response.text())
        response = await self.execute(air["device_id"], mode["id"], {"hvac_mode": "unsupported"})
        self.assertIn(response.status, (400, 403))
        self.assertEqual(self.writes, [])

    async def test_06_valid_generic_command_targets_one_entity_once(self):
        devices = await self.devices(refresh=True)
        air = next(item for item in devices if "climate.air" in item["entity_ids"])
        action = self.find_action(air, "set_temperature")
        response = await self.execute(air["device_id"], action["id"], {"temperature": 24.5})
        self.assertEqual(response.status, 200, await response.text())
        self.assertEqual(self.writes, [{"domain": "climate", "service": "set_temperature", "payload": {"entity_id": "climate.air", "temperature": 24.5}}])
        self.assertEqual(next(state for state in self.states if state["entity_id"] == "climate.air")["state"], "off")

    async def test_07_cached_pagination_does_not_repeat_full_discovery(self):
        await self.devices(refresh=True)
        before = self.discovery_requests
        seen = set()
        cursor = ""
        for _ in range(6):
            response = await self.client.get('/api/home?enabled_only=false&limit=1&cursor=' + quote(cursor, safe=''), headers=self.headers)
            self.assertEqual(response.status, 200, await response.text())
            result = await response.json()
            self.assertLessEqual(len(result["items"]), 1)
            for item in result["items"]:
                self.assertNotIn(item["device_id"], seen)
                seen.add(item["device_id"])
            cursor = result.get("next_cursor")
            if not cursor:
                break
        self.assertGreaterEqual(len(seen), 4)
        self.assertEqual(self.discovery_requests, before)

    async def test_08_discovery_failure_is_explicit_and_does_not_erase_permissions(self):
        original = await self.devices(refresh=True)
        self.fail_discovery = True
        response = await self.client.get('/api/home?enabled_only=false&refresh=true', headers=self.headers)
        result = await response.json()
        if response.status == 200:
            self.assertTrue(result.get("discovery_error"), result)
            self.assertEqual({item["device_id"] for item in result["items"]}, {item["device_id"] for item in original})
        else:
            self.assertGreaterEqual(response.status, 400)
        self.fail_discovery = False
        self.assertEqual(len(await self.devices(refresh=True, enabled_only=True)), 2)
        self.assertEqual(self.writes, [])

    async def test_09_upstream_failure_is_not_retried(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        self.write_status = 504
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        self.assertGreaterEqual(response.status, 400)
        self.assertEqual(len(self.writes), 1)

    async def test_10_composite_device_keeps_each_control_and_targets_correct_entity(self):
        devices = await self.devices(refresh=True)
        combo = next(item for item in devices if "light.combo" in item["entity_ids"])
        starts = [action for action in combo["actions"] if "turn_on" in action["id"]]
        self.assertEqual(len(starts), 2)
        self.assertEqual(len({action["id"] for action in starts}), 2)
        response = await self.authorize(combo["device_id"], True)
        self.assertEqual(response.status, 200, await response.text())
        for action in starts:
            response = await self.execute(combo["device_id"], action["id"], self.fixed_parameters(action))
            self.assertEqual(response.status, 200, await response.text())
        self.assertEqual({item["payload"]["entity_id"] for item in self.writes}, {"light.combo", "fan.combo"})

    async def test_11_current_offline_state_blocks_cached_action(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        next(state for state in self.states if state["entity_id"] == "vacuum.original")["state"] = "unavailable"
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        self.assertGreaterEqual(response.status, 400, await response.text())
        self.assertEqual(self.writes, [])

    async def test_12_current_features_block_action_that_was_previously_supported(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        next(state for state in self.states if state["entity_id"] == "vacuum.original")["attributes"]["supported_features"] = 0
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        self.assertGreaterEqual(response.status, 400, await response.text())
        self.assertEqual(self.writes, [])

    async def test_13_revoked_alias_cannot_bypass_failed_discovery_after_restart(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        response = await self.authorize(vacuum["device_id"], False)
        self.assertEqual(response.status, 200)
        await self.client.close()
        self.fail_discovery = True
        self.client = TestClient(TestServer(create_app(copy.deepcopy(self.config))))
        await self.client.start_server()
        response = await self.execute("M40", self.find_action(vacuum, "start")["id"])
        self.assertGreaterEqual(response.status, 400, await response.text())
        self.assertEqual(self.writes, [])

    async def test_14_corrupt_authorization_file_cannot_restore_seed_permissions(self):
        await self.devices(refresh=True)
        path = Path(self.folder.name) / "home-authorizations.json"
        async with aiohttp.ClientSession() as http:
            for corrupted in ('{"devices":', '[]', 'null', '{}', '{"version":2,"devices":{},"seeds":{}}', '{"version":true,"devices":{},"seeds":{}}'):
                path.write_text(corrupted, encoding="utf-8")
                try:
                    controller = HomeController(copy.deepcopy(self.config), http)
                    result = await controller.list_home_devices(enabled_only=True, force=True)
                except (ValueError, OSError, RuntimeError):
                    pass
                else:
                    self.assertFalse(result["devices"], "Corrupt authorization file must fail closed")
                self.assertEqual(path.read_text(encoding="utf-8"), corrupted)
        self.assertEqual(self.writes, [])

    async def test_15_confirmation_failure_does_not_report_cached_state_as_success(self):
        state = next(state for state in self.states if state["entity_id"] == "vacuum.original")
        state["state"] = "cleaning"
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        self.fail_confirmation = True
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        if response.status == 200:
            self.assertFalse((await response.json()).get("target_observed"))
        else:
            self.assertGreaterEqual(response.status, 400)
        self.assertEqual(len(self.writes), 1)

    async def test_16_platform_attributes_cannot_override_execution_envelope(self):
        state = next(state for state in self.states if state["entity_id"] == "vacuum.original")
        state["attributes"].update({"device_id": "forged", "action": "homeassistant.restart", "target_observed": False, "request_accepted": False})
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        action = self.find_action(vacuum, "start")
        response = await self.execute(vacuum["device_id"], action["id"])
        self.assertEqual(response.status, 200, await response.text())
        result = await response.json()
        self.assertEqual(result["device_id"], vacuum["device_id"])
        self.assertEqual(result["action"], action["id"])
        self.assertTrue(result["request_accepted"])
        self.assertTrue(result["target_observed"])

    async def test_17_capability_is_not_exposed_when_platform_service_is_absent(self):
        del self.services["vacuum"]["start"]
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        self.assertFalse(any("start" in action["id"] for action in vacuum["actions"]))
        self.assertEqual(self.writes, [])

    async def test_18_command_refreshes_only_its_target_not_full_house_catalog(self):
        devices = await self.devices(refresh=True)
        air = next(item for item in devices if "climate.air" in item["entity_ids"])
        before = self.discovery_requests
        response = await self.execute(air["device_id"], self.find_action(air, "set_temperature")["id"], {"temperature": 24.5})
        self.assertEqual(response.status, 200, await response.text())
        self.assertEqual(self.discovery_requests, before)

    async def test_19_duplicate_device_name_requires_stable_identifier(self):
        self.add_device("vac_b", "扫地机器人", "vacuum.new_vendor", "entirely.new.model", "docked", {"supported_features": 8192 | 16})
        await self.devices(refresh=True)
        response = await self.execute("扫地机器人", "start")
        self.assertGreaterEqual(response.status, 400)
        self.assertEqual(self.writes, [])

    async def test_20_malformed_parameter_container_is_not_coerced_to_empty_object(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        action = self.find_action(vacuum, "start")["id"]
        for parameters in ([], False, ""):
            response = await self.client.post(f"/api/home/{vacuum['device_id']}/actions", headers=self.headers, json={"action": action, "parameters": parameters})
            self.assertIn(response.status, (400, 403), await response.text())
        self.assertEqual(self.writes, [])

    async def test_21_removed_entity_is_rejected_before_write(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        self.states = [state for state in self.states if state["entity_id"] != "vacuum.original"]
        response = await self.execute(vacuum["device_id"], self.find_action(vacuum, "start")["id"])
        self.assertGreaterEqual(response.status, 400)
        self.assertEqual(self.writes, [])

    async def test_22_seed_migration_is_not_reapplied_to_replacement_physical_device(self):
        await self.devices(refresh=True)
        next(device for device in self.registry_devices if device["id"] == "vac_a")["id"] = "replacement-vacuum"
        for entity in self.entities:
            if entity.get("device_id") == "vac_a":
                entity["device_id"] = "replacement-vacuum"
        devices = await self.devices(refresh=True)
        replacement = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        self.assertEqual(replacement["device_id"], "replacement-vacuum")
        self.assertFalse(replacement["enabled"], "A replacement physical device needs fresh authorization")
        self.assertEqual(self.writes, [])

    async def test_23_chunked_platform_json_is_read_to_eof(self):
        self.chunked_discovery = True
        devices = await self.devices(refresh=True)
        self.assertGreaterEqual(len(devices), 4)
        self.assertEqual(sum(device["enabled"] for device in devices), 2)
        self.assertEqual(self.writes, [])

    async def test_24_failed_refresh_blocks_cached_execution_until_recovery(self):
        devices = await self.devices(refresh=True)
        vacuum = next(item for item in devices if "vacuum.original" in item["entity_ids"])
        action = self.find_action(vacuum, "start")["id"]
        self.fail_discovery = True
        response = await self.client.get('/api/home?enabled_only=false&refresh=true', headers=self.headers)
        if response.status == 200:
            self.assertTrue((await response.json()).get("discovery_error"))
        response = await self.execute(vacuum["device_id"], action)
        self.assertGreaterEqual(response.status, 400, await response.text())
        self.assertEqual(self.writes, [])
        self.fail_discovery = False
        await self.devices(refresh=True)
        response = await self.execute(vacuum["device_id"], action)
        self.assertEqual(response.status, 200, await response.text())
        self.assertEqual(len(self.writes), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)

"""连接中心离线回归；只使用临时目录、虚构凭据与 loopback MCP 服务。"""

import asyncio
import json
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from aiohttp import ClientSession, web
from aiohttp.test_utils import TestServer

from gateway import VoiceSession
from src.assistant_app.integrations.action_schemas import normalize_arguments
from src.assistant_app.integrations.contracts import ConnectorAction, ConnectorContext, ConnectorManifest, PermissionScope
from src.assistant_app.integrations.mcp_http import MCPConnector, MCPError, MAX_BYTES, action_name
from src.assistant_app.integrations.registry import ConnectorRegistry
from tests import test_console_configuration as console_tests


class ConnectionCenterTests(unittest.IsolatedAsyncioTestCase):
    asyncSetUp = console_tests.ConsoleConfigurationTests.asyncSetUp
    asyncTearDown = console_tests.ConsoleConfigurationTests.asyncTearDown
    save = console_tests.ConsoleConfigurationTests.save

    async def state(self, connector_id):
        result = await (await self.client.get("/api/connectors", headers=self.headers)).json()
        return next(item for item in result["items"] if item["id"] == connector_id)

    async def test_configured_check_ttl_and_configuration_change_are_distinct(self):
        await self.save("todoist", {"todoist_access_token": "fake-todoist-token"})
        self.assertEqual((await self.state("todoist"))["status"], "unverified")
        registry = self.runtime.connector_registry
        self.assertFalse(registry.read_tool_schemas())
        connector = registry.get("todoist")
        with patch.object(connector, "invoke", new=AsyncMock(return_value={"items": []})):
            response = await self.client.post("/api/connectors/todoist/check", headers=self.headers, json={})
            self.assertEqual(response.status, 200)
        self.assertEqual((await self.state("todoist"))["status"], "available")
        self.assertTrue(any(item["name"] == "connector__todoist__list_tasks" for item in registry.read_tool_schemas()))
        registry._checks["todoist"]["checked_epoch"] -= 901
        self.assertEqual((await self.state("todoist"))["status"], "unverified")
        registry.record_check("todoist")
        await self.save("todoist", {"todoist_access_token": "fake-replacement-token"})
        self.assertEqual((await self.state("todoist"))["status"], "unverified")

    async def test_check_is_serialized_with_config_save(self):
        await self.save("todoist", {"todoist_access_token": "fake-original-token"})
        entered, release = asyncio.Event(), asyncio.Event()
        async def read(*args):
            entered.set()
            await release.wait()
            self.assertEqual(self.config["todoist_access_token"], "fake-original-token")
            return {"items": []}
        with patch.object(self.runtime.connector_registry.get("todoist"), "invoke", side_effect=read):
            checking = asyncio.create_task(self.client.post("/api/connectors/todoist/check", headers=self.headers, json={}))
            await entered.wait()
            saving = asyncio.create_task(self.save("todoist", {"todoist_access_token": "fake-new-token"}))
            await asyncio.sleep(0)
            self.assertFalse(saving.done())
            release.set()
            self.assertEqual((await checking).status, 200)
            self.assertEqual((await saving).status, 200)
        self.assertEqual((await self.state("todoist"))["status"], "unverified")

    async def test_failed_check_revokes_available_state(self):
        await self.save("todoist", {"todoist_access_token": "fake-todoist-token"})
        registry = self.runtime.connector_registry
        registry.record_check("todoist")
        with patch.object(registry.get("todoist"), "invoke", new=AsyncMock(side_effect=RuntimeError("Todoist 请求失败: 403"))):
            self.assertEqual((await self.client.post("/api/connectors/todoist/check", headers=self.headers, json={})).status, 502)
        self.assertEqual((await self.state("todoist"))["status"], "expired_or_denied")
        self.assertFalse(registry.read_tool_schemas())

    async def test_revoke_is_persistent_invalidates_preview_and_blocks_old_voice(self):
        await self.save("todoist", {"todoist_access_token": "fake-todoist-token"})
        registry = self.runtime.connector_registry
        registry.record_check("todoist")
        preview = await (await self.client.post("/api/connectors/todoist/create_task/prepare", headers=self.headers, json={"arguments": {"content": "offline task"}})).json()
        response = await self.client.post("/api/connectors/todoist/access", headers=self.headers, json={"enabled": False})
        self.assertEqual(response.status, 200)
        self.assertIn("todoist", self.vault.load()["disabled_connectors"])
        self.assertEqual((await self.state("todoist"))["status"], "disabled")
        session = VoiceSession(self.application, None, browser=True)
        with self.assertRaises(Exception):
            await session._execute_tool("connector__todoist__list_tasks", {})
        await self.client.post("/api/connectors/todoist/access", headers=self.headers, json={"enabled": True})
        with patch.object(registry, "invoke", new=AsyncMock()) as invoke:
            response = await self.client.post("/api/connectors/todoist/create_task", headers=self.headers, json={"arguments": preview["preview"], "confirmation_id": preview["confirmation_id"]})
            self.assertEqual(response.status, 403)
            invoke.assert_not_awaited()

    async def test_native_preview_validates_before_confirm_and_replay_never_reexecutes(self):
        for connector, arguments in (("google_calendar", {"summary": "bad", "start": "2026-11-12T09:00:00", "end": "2026-11-12T10:00:00"}), ("todoist", {"content": "task", "priority": True})):
            action = "create_event" if connector == "google_calendar" else "create_task"
            self.assertEqual((await self.client.post(f"/api/connectors/{connector}/{action}/prepare", headers=self.headers, json={"arguments": arguments})).status, 400)
        parameters = {"content": "offline task", "priority": 2}
        preview = await (await self.client.post("/api/connectors/todoist/create_task/prepare", headers=self.headers, json={"arguments": parameters})).json()
        with patch.object(self.runtime.connector_registry.get("todoist"), "invoke", new=AsyncMock(return_value={"id": "fake-task"})) as invoke:
            changed = await self.client.post("/api/connectors/todoist/create_task", headers=self.headers, json={"arguments": {**parameters, "content": "changed"}, "confirmation_id": preview["confirmation_id"]})
            self.assertEqual(changed.status, 403)
            for _ in range(2):
                response = await self.client.post("/api/connectors/todoist/create_task", headers=self.headers, json={"arguments": parameters, "confirmation_id": preview["confirmation_id"]})
                self.assertEqual((await response.json())["status"], "completed")
            invoke.assert_awaited_once()

    async def test_mutating_descriptor_cannot_opt_out_of_confirmation(self):
        class UnsafeConnector:
            manifest = ConnectorManifest("unsafe", "unsafe", "1", (ConnectorAction("write", "write", frozenset({PermissionScope.WRITE}), False),))
            invoke = AsyncMock()
        connector = UnsafeConnector()
        registry = ConnectorRegistry()
        registry.register(connector)
        with self.assertRaises(PermissionError):
            await registry.invoke("unsafe", "write", {}, ConnectorContext("voice", frozenset(PermissionScope), {"confirmation_id": "invented"}))
        connector.invoke.assert_not_awaited()

    async def test_existing_home_and_gmail_routes_respect_revoke(self):
        for connector, route in (("home_assistant", "/api/home"), ("gmail", "/api/gmail/messages")):
            await self.client.post(f"/api/connectors/{connector}/access", headers=self.headers, json={"enabled": False})
            response = await self.client.get(route, headers=self.headers)
            self.assertEqual(response.status, 403)

    async def test_mcp_discovery_approval_revision_and_confirmed_call_use_one_permission_path(self):
        fake = SimpleNamespace(calls=[], sse=False, version="2025-06-18", call_count=0, tools=[{"name": "external-switch", "description": "offline switch", "inputSchema": {"type": "object", "properties": {"enabled": {"type": "boolean"}}, "required": ["enabled"], "additionalProperties": False}, "annotations": {"readOnlyHint": True}}])
        application = web.Application()
        async def handle(request):
            return await MCPTransportTests.handle(fake, request)
        application.router.add_route("*", "/mcp", handle)
        server = TestServer(application)
        await server.start_server()
        try:
            self.assertEqual((await self.save("mcp", {"mcp_url": str(server.make_url("/mcp")), "mcp_token": "fake-mcp-token"})).status, 200)
            self.assertEqual((await self.client.post("/api/connectors/mcp/check", headers=self.headers, json={})).status, 200)
            first = await self.state("mcp")
            self.assertEqual(first["actions"], [])
            self.assertEqual(first["tools"][0]["mode"], "blocked")
            self.assertNotIn("fake-mcp-token", json.dumps(first))
            await self.client.post("/api/connectors/mcp/check", headers=self.headers, json={})
            second = await self.state("mcp")
            stale = await self.client.post("/api/connectors/mcp/permissions", headers=self.headers, json={"tools": {"external-switch": "read"}, "revision": first["tool_revision"]})
            self.assertEqual(stale.status, 400)
            granted = await self.client.post("/api/connectors/mcp/permissions", headers=self.headers, json={"tools": {"external-switch": "confirm"}, "revision": second["tool_revision"]})
            self.assertEqual(granted.status, 200)
            action = action_name("external-switch")
            path = f"/api/connectors/mcp/{action}"
            forged = await self.client.post(path, headers=self.headers, json={"arguments": {"enabled": False}, "confirmation_id": "invented"})
            self.assertEqual(forged.status, 403)
            self.assertEqual(fake.call_count, 0)
            preview = await (await self.client.post(path + "/prepare", headers=self.headers, json={"arguments": {"enabled": False}})).json()
            for _ in range(2):
                executed = await self.client.post(path, headers=self.headers, json={"arguments": preview["preview"], "confirmation_id": preview["confirmation_id"]})
                self.assertEqual((await executed.json())["status"], "completed")
            self.assertEqual(fake.call_count, 1)
            await self.client.post("/api/connectors/mcp/access", headers=self.headers, json={"enabled": False})
            self.assertEqual(self.vault.load()["mcp_approved_tools"], {})
            self.assertEqual((await self.state("mcp"))["status"], "disabled")
        finally:
            await server.close()


class MCPTransportTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.calls = []
        self.tools = [{"name": "switch.with-hints", "description": "may change a device", "inputSchema": {"type": "object", "properties": {"enabled": {"type": "boolean"}}, "required": ["enabled"], "additionalProperties": False}, "annotations": {"readOnlyHint": True}}]
        self.sse = False
        self.version = "2025-06-18"
        self.call_count = 0
        application = web.Application()
        application.router.add_route("*", "/mcp", self.handle)
        self.server = TestServer(application)
        await self.server.start_server()
        self.http = ClientSession()
        self.config = {"mcp_url": str(self.server.make_url("/mcp")), "mcp_token": "fake-mcp-token"}
        self.connector = MCPConnector(self.http, self.config)

    async def asyncTearDown(self):
        await self.http.close()
        await self.server.close()

    async def handle(self, request):
        if request.method == "DELETE":
            self.calls.append(("DELETE", dict(request.headers)))
            return web.Response(status=204)
        payload = await request.json()
        method = payload["method"]
        self.calls.append((method, dict(request.headers)))
        if method == "initialize":
            return web.json_response({"jsonrpc": "2.0", "id": payload["id"], "result": {"protocolVersion": self.version, "capabilities": {"tools": {}}, "serverInfo": {"name": "offline", "version": "1"}}}, headers={"Mcp-Session-Id": "offline-session"})
        if method == "notifications/initialized":
            return web.Response(status=202)
        if method == "tools/list":
            result = {"tools": self.tools}
        else:
            self.call_count += 1
            result = {"content": [{"type": "text", "text": "offline result"}]}
        response = {"jsonrpc": "2.0", "id": payload["id"], "result": result}
        if self.sse:
            server_request = {"jsonrpc": "2.0", "id": payload["id"], "method": "unsupported/server-request"}
            return web.Response(text="data: " + json.dumps(server_request) + "\n\ndata: " + json.dumps(response) + "\n\n", content_type="text/event-stream")
        return web.json_response(response)

    async def approve(self, mode):
        from src.assistant_app.integrations.mcp_http import connection_fingerprint, fingerprint
        tool = self.tools[0]
        self.config["mcp_approved_tools"] = {tool["name"]: {"mode": mode, "fingerprint": fingerprint(tool), "connection": connection_fingerprint(self.config)}}
        self.connector.apply_approvals()

    async def test_initialize_headers_cleanup_and_no_automatic_hint_approval(self):
        await self.connector.check_connection()
        self.assertEqual(self.connector.manifest.actions, ())
        self.assertEqual(self.connector.public_tools()[0]["mode"], "blocked")
        self.assertEqual([method for method, _ in self.calls], ["initialize", "notifications/initialized", "tools/list", "DELETE"])
        for method, headers in self.calls:
            self.assertIn("application/json", headers["Accept"])
            self.assertIn("text/event-stream", headers["Accept"])
            if method != "initialize":
                self.assertEqual(headers["Mcp-Session-Id"], "offline-session")
                self.assertEqual(headers["MCP-Protocol-Version"], "2025-06-18")

    async def test_sse_and_single_confirmed_call_then_changed_schema_is_blocked(self):
        self.sse = True
        await self.connector.check_connection()
        await self.approve("confirm")
        action = action_name(self.tools[0]["name"])
        with self.assertRaises(PermissionError):
            await self.connector.invoke(action, {"enabled": False}, ConnectorContext("admin"))
        self.assertEqual(self.call_count, 0)
        context = ConnectorContext("admin", frozenset(PermissionScope), {"confirmation_verified": True})
        result = await self.connector.invoke(action, {"enabled": False}, context)
        self.assertTrue(result["untrusted_external_content"])
        self.assertEqual(self.call_count, 1)
        self.tools[0] = {**self.tools[0], "description": "changed semantics"}
        with self.assertRaises(PermissionError):
            await self.connector.invoke(action, {"enabled": False}, context)
        self.assertEqual(self.call_count, 1)

    async def test_versions_limits_invalid_schema_and_credentials_changed(self):
        self.version = "unsupported"
        with self.assertRaises(MCPError):
            await self.connector.check_connection()
        self.assertEqual(self.calls[-1][0], "DELETE")
        self.version = "2025-06-18"
        self.tools = [dict(self.tools[0], name=f"tool-{i}") for i in range(65)]
        with self.assertRaises(MCPError):
            await self.connector.check_connection()
        self.tools = [{"name": "bad", "inputSchema": {"type": "object", "properties": []}}]
        with self.assertRaises(MCPError):
            await self.connector.check_connection()
        self.tools = [{"name": "huge", "description": "x" * (MAX_BYTES + 1), "inputSchema": {"type": "object"}}]
        with self.assertRaises(MCPError):
            await self.connector.check_connection()
        self.tools = [{"name": "ok", "inputSchema": {"type": "object"}}]
        await self.connector.check_connection()
        await self.approve("read")
        self.config["mcp_token"] = "fake-new-account-token"
        with self.assertRaises(PermissionError):
            await self.connector.invoke(action_name("ok"), {}, ConnectorContext("voice"))
        self.assertEqual(self.call_count, 0)


class NativeSchemaTests(unittest.TestCase):
    def test_internal_optional_none_cursor_remains_compatible(self):
        from src.assistant_app.integrations.google_calendar import GoogleCalendarConnector
        descriptor = GoogleCalendarConnector.manifest.actions[0]
        result = normalize_arguments("google_calendar", descriptor, {"page_token": None, "limit": 1})
        self.assertEqual(result, {"limit": 1})


if __name__ == "__main__":
    unittest.main()

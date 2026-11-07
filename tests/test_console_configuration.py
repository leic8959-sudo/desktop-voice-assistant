"""后台配置闭环离线检查：仅临时目录、模拟提供商、随机 loopback 端口。"""

import asyncio
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import AsyncMock, patch

from aiohttp import ClientSession, web
from aiohttp.test_utils import TestClient, TestServer, make_mocked_request

from assistant_runtime import RUNTIME_KEY
from control_api import CONFIG_STORE_KEY, CONFIRMATIONS_KEY, _require_config_transport
from gateway import VoiceSession, create_app
from gmail_api import create_gmail_service
from gmail_service import DpapiTokenVault, MemoryTokenVault
from src.assistant_app.infrastructure.config_store import SecureConfigStore, apply_configuration_overrides
from src.assistant_app.api.desktop_actions import DESKTOP_ACTIONS_KEY


class ConsoleConfigurationTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.config = {"device_token": "offline-admin-token-0000000000000", "provisioning_token": "offline-provisioning-token-000000", "device_id": "offline-device", "host": "127.0.0.1", "port": 8765, "public_base_url": "http://127.0.0.1:8765", "runtime_dir": self.directory.name, "aliyun_model": "mock-realtime", "aliyun_voice": "Tina", "enable_search": False, "home_entities": {}}
        self.application = create_app(self.config)
        self.vault = MemoryTokenVault()
        self.application[CONFIG_STORE_KEY] = SecureConfigStore(Path(self.directory.name) / "test.dpapi", self.vault)
        self.client = TestClient(TestServer(self.application))
        await self.client.start_server()
        self.runtime = self.application[RUNTIME_KEY]
        self.headers = {"Authorization": "Bearer " + self.config["device_token"]}

    async def asyncTearDown(self):
        await self.client.close()
        self.directory.cleanup()

    async def save(self, connector, values=None, clear=None):
        return await self.client.post(f"/api/connectors/{connector}/config", headers=self.headers, json={"values": values or {}, "clear_fields": clear or []})

    async def test_anonymous_and_cross_origin_cannot_read_or_save(self):
        for method, path in (("get", "/api/connectors"), ("get", "/api/settings"), ("post", "/api/connectors/amap/config"), ("post", "/api/settings/models")):
            response = await getattr(self.client, method)(path, json={})
            self.assertEqual(response.status, 401)
        response = await self.client.post("/api/connectors/amap/config", headers={**self.headers, "Origin": "https://foreign.invalid"}, json={"values": {"amap_web_service_key": "a" * 32}})
        self.assertEqual(response.status, 403)
        self.assertIsNone(self.vault.value)

    async def test_registry_exposes_eleven_real_forms_without_secret_values(self):
        response = await self.client.get("/api/connectors", headers=self.headers)
        data = await response.json()
        self.assertEqual(len(data["items"]), 11)
        self.assertTrue(all(item["config_fields"] for item in data["items"]))
        self.assertNotIn(self.config["device_token"], json.dumps(data))

    async def test_all_token_adapters_and_shared_microsoft_configuration(self):
        cases = {"amap": {"amap_web_service_key": "b" * 32}, "google_calendar": {"google_calendar_access_token": "fake-google-token"}, "microsoft_calendar": {"microsoft_calendar_access_token": "fake-graph-token"}, "notion": {"notion_access_token": "fake-notion-token"}, "github": {"github_access_token": "fake-github-token"}, "todoist": {"todoist_access_token": "fake-todoist-token"}, "slack": {"slack_webhook_url": "https://hooks.slack.com/services/TEST/FAKE/SECRET"}, "feishu": {"feishu_webhook_url": "https://open.feishu.cn/open-apis/bot/v2/hook/fake-secret"}, "home_assistant": {"home_assistant_url": "http://home.invalid:8123", "home_assistant_token": "fake-home-token"}}
        for connector, values in cases.items():
            response = await self.save(connector, values)
            if connector in {"google_calendar", "microsoft_calendar"}:
                self.assertEqual(response.status, 400)
                fields = (await (await self.client.get(f"/api/connectors/{connector}/config", headers=self.headers)).json())["fields"]
                self.assertTrue(all(field["managed_by"] == "calendar_oauth" and field["editable"] is False for field in fields))
                continue
            self.assertEqual(response.status, 200, await response.text())
            text = await response.text()
            for key, value in values.items():
                if key != "home_assistant_url":
                    self.assertNotIn(value, text)
                self.assertEqual(self.config[key], value)
        response = await self.save("microsoft_mail", {"microsoft_calendar_access_token": "fake-shared-replacement"})
        self.assertEqual(response.status, 400)
        tokens = self.application[DESKTOP_ACTIONS_KEY].token_refresh
        tokens.vault = MemoryTokenVault()
        response = await self.client.post("/api/computer/oauth/microsoft_calendar/config", headers=self.headers, json={"values": {"access_token": "fake-shared-replacement"}})
        self.assertEqual(response.status, 200, await response.text())
        self.assertNotIn("fake-shared-replacement", await response.text())
        self.assertEqual(self.runtime.connector_registry.get("microsoft_calendar").config["microsoft_calendar_access_token"], "fake-shared-replacement")
        self.assertEqual(self.runtime.connector_registry.get("microsoft_mail").config["microsoft_calendar_access_token"], "fake-shared-replacement")

    async def test_white_list_formats_and_transport_before_persistence(self):
        for values in ({"device_token": "malicious"}, {"amap_web_service_key": "invalid"}, {"amap_web_service_key": "a\nb"}, {"amap_web_service_key": 123}, {"unknown": "fake"}):
            self.assertEqual((await self.save("amap", values)).status, 400)
        self.assertEqual((await self.save("github", {}, ["device_token"])).status, 400)
        self.assertEqual((await self.save("slack", {"slack_webhook_url": "https://attacker.invalid/secret"})).status, 400)
        self.assertEqual((await self.save("home_assistant", {"home_assistant_url": "http://user:secret@home.invalid/"})).status, 400)
        self.assertEqual((await self.save("home_assistant", {"home_entities": {"new-alias": "light.new"}})).status, 400)
        response = await self.client.post("/api/connectors/amap/config", headers={**self.headers, "X-Forwarded-Proto": "https"}, json={"values": {"amap_web_service_key": "a" * 32}})
        self.assertEqual(response.status, 403)
        self.assertIsNone(self.vault.value)

    async def test_empty_secret_preserves_and_explicit_clear_survives_recovery(self):
        self.config["github_access_token"] = "fake-file-token"
        await self.save("github", {"github_access_token": "fake-saved-token"})
        await self.save("github", {"github_access_token": ""})
        self.assertEqual(self.config["github_access_token"], "fake-saved-token")
        await self.save("github", {}, ["github_access_token"])
        restored = {"github_access_token": "fake-file-token"}
        with patch("src.assistant_app.infrastructure.config_store.load_secure_overrides", return_value=self.vault.load()):
            apply_configuration_overrides(restored, self.directory.name, {})
        self.assertEqual(restored["github_access_token"], "")

    async def test_environment_precedence_and_no_form_override(self):
        self.config.update({"github_access_token": "fake-env-token", "_config_sources": {"github_access_token": "environment"}})
        self.assertEqual((await self.save("github", {"github_access_token": "fake-form-token"})).status, 400)
        self.assertEqual((await self.save("github", {}, ["github_access_token"])).status, 400)
        restored = {"github_access_token": "fake-file-token"}
        with patch("src.assistant_app.infrastructure.config_store.load_secure_overrides", return_value={"github_access_token": "fake-saved-token"}):
            apply_configuration_overrides(restored, self.directory.name, {"ASSISTANT_GITHUB_ACCESS_TOKEN": "fake-env-token"})
        self.assertEqual(restored["github_access_token"], "fake-env-token")

    async def test_failed_save_preserves_runtime_and_store(self):
        await self.save("github", {"github_access_token": "fake-original-token"})
        with patch.object(self.vault, "save", side_effect=OSError("simulated disk failure")):
            response = await self.save("github", {"github_access_token": "fake-failed-token"})
        self.assertNotEqual(response.status, 200)
        self.assertEqual(self.config["github_access_token"], "fake-original-token")
        self.assertEqual(self.vault.load()["github_access_token"], "fake-original-token")
        self.assertNotIn("fake-failed-token", await response.text())

    async def test_parallel_saves_do_not_lose_unrelated_fields(self):
        first, second = await asyncio.gather(self.save("github", {"github_access_token": "fake-github-token"}), self.save("todoist", {"todoist_access_token": "fake-todoist-token"}))
        self.assertEqual((first.status, second.status), (200, 200))
        self.assertEqual(set(self.vault.load()), {"github_access_token", "todoist_access_token"})

    async def test_home_hot_update_waits_for_active_read_preserves_authorizations(self):
        await self.save("home_assistant", {"home_assistant_url": "http://home.invalid", "home_assistant_token": "fake-home-old-token"})
        home = self.runtime.home
        home._seed_entities = {"retained": "light.retained"}
        home._authorizations = {"retained-device": False}
        home._catalog = {"fake": True}
        entered, release = asyncio.Event(), asyncio.Event()
        async def read(*args, **kwargs):
            entered.set(); await release.wait()
            self.assertEqual(home.token, "fake-home-old-token")
            return {"message": "API running."}
        with patch.object(home, "_get_json", side_effect=read):
            reading = asyncio.create_task(home.check_connection())
            await entered.wait()
            saving = asyncio.create_task(self.save("home_assistant", {"home_assistant_token": "fake-home-new-token"}))
            await asyncio.sleep(0)
            self.assertFalse(saving.done())
            release.set(); await reading
            self.assertEqual((await saving).status, 200)
        self.assertEqual(home.token, "fake-home-new-token")
        self.assertEqual(home._authorizations, {"retained-device": False})
        self.assertEqual(home._seed_entities, {"retained": "light.retained"})
        self.assertIsNone(home._catalog)
        self.assertEqual((await self.save("home_assistant", {"home_assistant_url": "http://other.invalid"})).status, 400)

    async def test_checks_are_read_only_bounded_and_webhooks_never_send(self):
        registry = self.runtime.connector_registry
        with patch.object(registry, "invoke", new=AsyncMock(return_value={"items": [{"private": "business-data"}]})) as invoke:
            response = await self.client.post("/api/connectors/amap/check", headers=self.headers, json={"arguments": {"city": "110000"}})
            self.assertEqual(response.status, 200)
            self.assertNotIn("business-data", await response.text())
            self.assertEqual(invoke.await_args.args[:2], ("amap", "weather"))
            response = await self.client.post("/api/connectors/microsoft_calendar/check", headers=self.headers, json={"arguments": {}})
            self.assertEqual(response.status, 200)
            self.assertIn("time_min", invoke.await_args.args[2])
            for connector in ("slack", "feishu"):
                self.assertEqual((await self.client.post(f"/api/connectors/{connector}/check", headers=self.headers, json={})).status, 400)
            self.assertEqual(invoke.await_count, 2)

    async def test_configuration_invalidates_pending_but_does_not_bypass_confirmation(self):
        response = await self.client.post("/api/workflows/github-issue/prepare", headers=self.headers, json={"repository": "owner/repo", "title": "offline"})
        token = (await response.json())["confirmation_id"]
        await self.save("github", {"github_access_token": "fake-github-token"})
        with patch.object(self.runtime.connector_registry, "invoke", new=AsyncMock()) as invoke:
            response = await self.client.post("/api/workflows/github-issue", headers=self.headers, json={"repository": "owner/repo", "title": "offline", "confirmation_id": token})
            self.assertEqual(response.status, 403)
            response = await self.client.post("/api/connectors/github/create_issue", headers=self.headers, json={"arguments": {"repository": "owner/repo", "title": "offline"}, "confirmation_id": "invented"})
            self.assertEqual(response.status, 403)
            invoke.assert_not_awaited()

    async def test_search_save_failure_recovery_and_maintenance_white_list(self):
        with patch("control_api.save_runtime_preferences", side_effect=OSError("mock failure")):
            response = await self.client.put("/api/settings", headers=self.headers, json={"enable_search": True})
        self.assertNotEqual(response.status, 200)
        self.assertFalse(self.config["enable_search"])
        self.assertEqual((await self.client.put("/api/settings", headers=self.headers, json={"enable_search": True})).status, 200)
        restored = {}
        apply_configuration_overrides(restored, self.directory.name, {})
        self.assertTrue(restored["enable_search"])
        self.assertEqual((await self.client.put("/api/settings", headers=self.headers, json={"device_token": "new"})).status, 400)
        self.assertEqual((await self.client.put("/api/settings", headers=self.headers, json={"enable_search": "false"})).status, 400)

    async def test_models_saved_and_current_session_snapshot_is_stable(self):
        session = VoiceSession(self.application, None, browser=True)
        response = await self.client.post("/api/settings/models", headers=self.headers, json={"values": {"aliyun_api_key": "fake-aliyun-key", "aliyun_workspace_id": "fake-workspace", "deepseek_api_key": "fake-deepseek-key", "chat_tts_voice": "longwan_v2"}})
        self.assertEqual(response.status, 200)
        self.assertNotIn("fake-aliyun-key", await response.text())
        self.assertNotIn("aliyun_api_key", session.config)
        for path, body in (("/api/voice", {"voice": "Serena"}), ("/api/llm", {"provider": "deepseek"})):
            self.assertEqual((await self.client.put(path, headers=self.headers, json=body)).status, 200)
        response = await self.client.post("/api/settings/models", headers=self.headers, json={"values": {"qwen_chat_thinking": "null", "deepseek_thinking": "enabled", "chat_max_tokens": 1024, "chat_turn_silence_ms": 800}})
        self.assertEqual(response.status, 200)
        self.assertIsNone(self.config["qwen_chat_thinking"])
        response = await self.client.post("/api/settings/models", headers=self.headers, json={"values": {"chat_max_tokens": "1024"}})
        self.assertEqual(response.status, 400)
        response = await self.client.post("/api/settings/models", headers=self.headers, json={"values": {}, "clear_fields": ["deepseek_api_key"]})
        self.assertEqual(response.status, 400)

    async def test_microsoft_mail_check_uses_read_only_folder_api(self):
        connector = self.runtime.connector_registry.get("microsoft_mail")
        with patch.object(connector, "_request", new=AsyncMock(return_value={"value": []})) as request:
            response = await self.client.post("/api/connectors/microsoft_mail/check", headers=self.headers, json={"arguments": {}})
            self.assertEqual(response.status, 200)
            request.assert_awaited_once_with("GET", "/me/mailFolders", params={"$top": 1, "$select": "id"})

    async def test_sensitive_fields_do_not_enter_audit_or_metadata(self):
        value = "fake-secret-should-not-appear"
        await self.save("github", {"github_access_token": value})
        for path in ("/api/connectors", "/api/connectors/github/config", "/api/settings", "/api/settings/models"):
            response = await self.client.get(path, headers=self.headers)
            self.assertNotIn(value, await response.text())
            self.assertNotIn(self.config["device_token"], await response.text())
        self.assertNotIn(value, (Path(self.directory.name) / "audit.log").read_text(encoding="utf-8"))

    async def test_actual_startup_restores_secure_models_and_search(self):
        if sys.platform != "win32": self.skipTest("DPAPI is Windows-only")
        from gateway import load_config
        base = {**self.config, "max_connections": 1, "session_seconds": 600, "idle_seconds": 30, "memory_backend": "pc_prototype"}
        config_path = Path(self.directory.name) / "fake-config.json"
        config_path.write_text(json.dumps(base), encoding="utf-8")
        SecureConfigStore(Path(self.directory.name) / "connector-config.dpapi").save({"amap_web_service_key": "d" * 32, "deepseek_api_key": "fake-restored-key"})
        with patch.dict("os.environ", {}, clear=True):
            restored = load_config(config_path)
        self.assertEqual(restored["amap_web_service_key"], "d" * 32)
        self.assertEqual(restored["deepseek_api_key"], "fake-restored-key")

    async def test_non_windows_store_is_read_only_without_plaintext_fallback(self):
        with patch("src.assistant_app.infrastructure.config_store.sys.platform", "linux"):
            store = SecureConfigStore(Path(self.directory.name) / "no-plaintext.dpapi")
            with self.assertRaises(ValueError):
                store.save({"github_access_token": "fake-secret"})
            self.assertFalse(store.path.exists())

    async def test_real_dpapi_encrypts_and_restarts_without_plaintext(self):
        if sys.platform != "win32": self.skipTest("DPAPI is Windows-only")
        store = SecureConfigStore(Path(self.directory.name) / "connector-config.dpapi")
        store.save({"github_access_token": "fake-only-for-dpapi-test"})
        self.assertNotIn(b"fake-only-for-dpapi-test", store.path.read_bytes())
        restored = {}
        apply_configuration_overrides(restored, self.directory.name, {})
        self.assertEqual(restored["github_access_token"], "fake-only-for-dpapi-test")
        with patch("gmail_service.os.replace", side_effect=OSError("mock replace failure")):
            with self.assertRaises(ValueError): store.save({"github_access_token": "fake-failed-token"})
        self.assertEqual(store.load()["github_access_token"], "fake-only-for-dpapi-test")

    async def test_gmail_existing_config_flow_and_environment_precedence(self):
        if sys.platform != "win32": self.skipTest("existing Gmail config uses DPAPI")
        payload = {"client_id": "fake.apps.googleusercontent.com", "client_secret": "fake-gmail-secret", "redirect_uri": "http://127.0.0.1:8765/gmail/oauth/callback", "send_enabled": False}
        response = await self.client.post("/api/gmail/config", headers=self.headers, json=payload)
        self.assertEqual(response.status, 200)
        self.assertNotIn("fake-gmail-secret", await response.text())
        self.assertTrue(self.runtime.gmail.configured)
        self.assertFalse(self.runtime.gmail.status()["authorized"])
        config = {"gmail_client_id": "fake-env-client", "gmail_client_secret": "fake-env-secret", "_config_sources": {"gmail_client_id": "environment", "gmail_client_secret": "environment"}}
        restored = create_gmail_service(config, None, self.directory.name)
        self.assertEqual(restored.config["gmail_client_secret"], "fake-env-secret")
        self.config["_config_sources"] = {"gmail_client_secret": "environment"}
        response = await self.client.post("/api/gmail/config", headers=self.headers, json=payload)
        self.assertEqual(response.status, 400)

    async def test_gmail_apply_failure_rolls_back_existing_saved_settings(self):
        if sys.platform != "win32": self.skipTest("existing Gmail config uses DPAPI")
        payload = {"client_id": "fake.apps.googleusercontent.com", "client_secret": "fake-original-gmail-secret", "redirect_uri": "http://127.0.0.1:8765/gmail/oauth/callback"}
        self.assertEqual((await self.client.post("/api/gmail/config", headers=self.headers, json=payload)).status, 200)
        with patch.object(self.runtime.gmail, "reconfigure", new=AsyncMock(side_effect=RuntimeError("mock apply failure"))):
            response = await self.client.post("/api/gmail/config", headers=self.headers, json={**payload, "client_secret": "fake-failed-secret"})
            self.assertNotEqual(response.status, 200)
        self.assertEqual(self.runtime.gmail.config["gmail_client_secret"], "fake-original-gmail-secret")
        stored = DpapiTokenVault(Path(self.directory.name) / "gmail_settings.dpapi").load()
        self.assertEqual(stored["gmail_client_secret"], "fake-original-gmail-secret")

    async def test_amap_js_key_error_is_not_reported_as_http_success(self):
        connector = self.runtime.connector_registry.get("amap")
        self.config["amap_web_service_key"] = "a" * 32
        class Response:
            status = 200
            async def __aenter__(self): return self
            async def __aexit__(self, *args): return None
            async def json(self, **kwargs): return {"status": "0", "infocode": "10009"}
        class Client:
            def get(self, *args, **kwargs): return Response()
        connector.client = Client()
        response = await self.client.post("/api/connectors/amap/check", headers=self.headers, json={"arguments": {"city": "110000"}})
        self.assertEqual(response.status, 502)
        self.assertIn("地图 JS Key 不适用", (await response.json())["message"])


class ConfigurationTransportTests(unittest.TestCase):
    def test_remote_requires_actual_https_and_does_not_trust_forwarded_proto(self):
        for url, peer, headers, permitted in (("http://console.invalid/api/settings", "203.0.113.2", {}, False), ("http://console.invalid/api/settings", "127.0.0.1", {"X-Forwarded-Proto": "https"}, False), ("https://console.invalid/api/settings", "203.0.113.2", {}, True)):
            request = make_mocked_request("POST", url, headers={"Host": "console.invalid", **headers})
            request._cache["remote"] = peer
            if permitted: _require_config_transport(request)
            else:
                with self.assertRaises(web.HTTPForbidden): _require_config_transport(request)


if __name__ == "__main__": unittest.main()

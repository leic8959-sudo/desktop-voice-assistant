"""搜索开关与主会话分派边界；不连接真实云端。"""

import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from gateway import CONFIG_KEY, HTTP_KEY, RUNTIME_KEY, TOOL_SCHEMAS, VoiceSession, session_settings


class SearchDispatchTests(unittest.IsolatedAsyncioTestCase):
    @staticmethod
    def config(enabled):
        return {"enable_search": enabled, "aliyun_voice": "Tina", "session_seconds": 600, "idle_seconds": 30}

    @staticmethod
    def runtime():
        # 按真实运行时合同补齐工具反馈依赖；search_web 路径只用到这里列出的属性。
        return SimpleNamespace(media_service=None, device_music=None, memory_service=None, connector_registry=SimpleNamespace(manifests=lambda: ()), operation_screen_feedback=AsyncMock())

    def test_session_settings_filters_search_and_disables_provider_search(self):
        disabled = session_settings(self.config(False), "[]")["session"]
        enabled = session_settings(self.config(True), "[]")["session"]
        self.assertFalse(disabled["enable_search"])
        self.assertFalse(enabled["enable_search"])
        self.assertNotIn("search_web", {tool["name"] for tool in disabled["tools"]})
        self.assertIn("search_web", {tool["name"] for tool in enabled["tools"]})

    def test_search_tool_is_registered_with_function_schema(self):
        search_tools = [tool for tool in TOOL_SCHEMAS if tool["name"] == "search_web"]
        self.assertEqual(len(search_tools), 1)
        self.assertEqual(search_tools[0]["type"], "function")
        self.assertEqual(search_tools[0]["parameters"]["required"], ["query"])

    async def test_execute_tool_dispatches_only_when_enabled(self):
        client = object()
        enabled_app = {CONFIG_KEY: self.config(True), HTTP_KEY: client, RUNTIME_KEY: self.runtime()}
        enabled_session = VoiceSession(enabled_app, None, raw_pcm=True)
        with patch("gateway.search_web", new_callable=AsyncMock, return_value={"ok": True}) as search:
            result = await enabled_session.execute_tool("search_web", {"query": "北京天气"})
        self.assertEqual(result, {"ok": True})
        search.assert_awaited_once_with(enabled_app[CONFIG_KEY], client, "北京天气", None)

        disabled_app = {CONFIG_KEY: self.config(False), HTTP_KEY: client, RUNTIME_KEY: self.runtime()}
        disabled_session = VoiceSession(disabled_app, None, raw_pcm=True)
        with patch("gateway.search_web", new_callable=AsyncMock) as disabled_search:
            result = await disabled_session.execute_tool("search_web", {"query": "北京天气"})
        self.assertFalse(result["ok"])
        self.assertFalse(result["search_confirmed"])
        disabled_search.assert_not_awaited()


if __name__ == "__main__":
    unittest.main()

"""独立联网搜索会话的本地协议测试，不连接真实云端。"""

import asyncio
import json
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import aiohttp

import web_search


CONFIG = {
    "aliyun_api_key": "test-key",
    "aliyun_workspace_id": "test-workspace",
    "aliyun_model": "qwen3.8-omni-flash-realtime",
}


def text_message(event):
    return SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(event, ensure_ascii=False))


class FakeWebSocket:
    def __init__(self, events=None, receive_error=None, receive_delay=0):
        self.messages = [text_message(event) for event in (events or [])]
        self.sent = []
        self.receive_error = receive_error
        self.receive_delay = receive_delay

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_args):
        return False

    async def send_json(self, payload):
        self.sent.append(payload)

    async def receive(self):
        if self.receive_delay:
            await asyncio.sleep(self.receive_delay)
        if self.receive_error:
            raise self.receive_error
        if not self.messages:
            return SimpleNamespace(type=aiohttp.WSMsgType.CLOSED, data=None)
        return self.messages.pop(0)


class FakeClient:
    def __init__(self, socket=None, error=None):
        self.socket = socket
        self.error = error
        self.calls = []

    def ws_connect(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        if self.error:
            raise self.error
        return self.socket


def response_events(usage=None, status="completed", text="检索结果"):
    return [
        {"type": "session.created"},
        {"type": "session.updated"},
        {"type": "response.text.delta", "delta": text},
        {"type": "response.text.done", "text": text},
        {
            "type": "response.done",
            "response": {
                "status": status,
                "output": [{"content": [{"text": text}]}] if text else [],
                "usage": usage or {},
            },
        },
    ]


class WebSearchTests(unittest.IsolatedAsyncioTestCase):
    async def test_success_uses_text_only_search_session_and_count(self):
        socket = FakeWebSocket(response_events({"plugins": {"search": {"count": 1, "strategy": "agent"}}}))
        result = await web_search.search_web(CONFIG, FakeClient(socket), "北京今天的天气", "ws://local/realtime")

        self.assertTrue(result["ok"])
        self.assertTrue(result["search_confirmed"])
        self.assertEqual(result["search"], {"count": 1, "strategy": "agent"})
        self.assertEqual(result["text"], "检索结果")
        session = socket.sent[0]["session"]
        self.assertEqual(session["modalities"], ["text"])
        self.assertNotIn("audio", session)
        self.assertEqual(session["tools"], [])
        self.assertTrue(session["enable_search"])
        self.assertTrue(session["search_options"]["enable_source"])
        self.assertEqual(socket.sent[1]["item"]["content"][0]["type"], "input_text")
        self.assertEqual(socket.sent[2]["type"], "response.create")

    async def test_missing_or_zero_count_is_not_confirmed(self):
        for usage in ({}, {"plugins": {"search": {"count": 0, "strategy": "agent"}}}):
            socket = FakeWebSocket(response_events(usage))
            result = await web_search.search_web(CONFIG, FakeClient(socket), "查询", "ws://local/realtime")
            self.assertFalse(result["ok"])
            self.assertFalse(result["search_confirmed"])
            self.assertEqual(result["error"]["code"], "search_not_confirmed")

    async def test_failed_response_status_is_structured_error(self):
        socket = FakeWebSocket(response_events({"plugins": {"search": {"count": 1}}}, status="failed"))
        result = await web_search.search_web(CONFIG, FakeClient(socket), "查询", "ws://local/realtime")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "provider_incomplete")

    async def test_empty_response_is_structured_error(self):
        socket = FakeWebSocket(response_events({"plugins": {"search": {"count": 1}}}, text=""))
        result = await web_search.search_web(CONFIG, FakeClient(socket), "查询", "ws://local/realtime")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "empty_response")

    async def test_query_limit_rejects_before_connection(self):
        client = FakeClient(FakeWebSocket())
        result = await web_search.search_web(CONFIG, client, "x" * 501, "ws://local/realtime")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "query_too_large")
        self.assertEqual(client.calls, [])

    async def test_client_exception_is_structured_without_secret(self):
        client = FakeClient(error=RuntimeError("local fake failure"))
        result = await web_search.search_web(CONFIG, client, "查询", "ws://local/realtime")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "unexpected")
        self.assertNotIn(CONFIG["aliyun_api_key"], json.dumps(result, ensure_ascii=False))

    async def test_timeout_is_structured(self):
        socket = FakeWebSocket(receive_delay=0.05)
        with patch.object(web_search, "SEARCH_TIMEOUT_SECONDS", 0.01):
            result = await web_search.search_web(CONFIG, FakeClient(socket), "查询", "ws://local/realtime")
        self.assertFalse(result["ok"])
        self.assertEqual(result["error"]["code"], "timeout")


if __name__ == "__main__":
    unittest.main()

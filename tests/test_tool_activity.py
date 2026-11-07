"""工具执行期间的 idle watchdog 与 generation 防回填本地测试。"""

import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession


class StopWatchdog(Exception):
    pass


class FakeSocket:
    def __init__(self):
        self.messages = []

    async def send_json(self, payload):
        self.messages.append(payload)


class FakeCloud:
    def __init__(self):
        self.closed = False
        self.messages = []

    async def send_json(self, payload):
        self.messages.append(payload)

    async def close(self):
        self.closed = True


class FakeBackend:
    """按 VoiceSession 的 backend 接口包装 FakeCloud，记录云端下行。"""

    def __init__(self, cloud):
        self.cloud = cloud
        self.name = "qwen_realtime"

    @property
    def connected(self):
        return not self.cloud.closed

    async def cancel_response(self):
        await self.cloud.send_json({"type": "response.cancel"})

    async def send_tool_output(self, call_id, output):
        await self.cloud.send_json({"type": "conversation.item.create", "item": {"type": "function_call_output", "call_id": call_id, "output": output}})

    async def request_response(self):
        await self.cloud.send_json({"type": "response.create"})


def make_session(idle_seconds=10):
    app = {CONFIG_KEY: {"session_seconds": 600, "idle_seconds": idle_seconds}, RUNTIME_KEY: SimpleNamespace(media_service=None, device_music=None)}
    session = VoiceSession(app, FakeSocket(), raw_pcm=True)
    session.backend = FakeBackend(FakeCloud())
    session.state = AsyncMock()
    return session


class ToolActivityTests(unittest.IsolatedAsyncioTestCase):
    async def test_watchdog_does_not_kill_session_while_tool_is_running(self):
        session = make_session()
        session.created = 0
        session.last_activity = 0
        session.executing_tools = 1
        sleeps = 0

        async def sleep_once(_seconds):
            nonlocal sleeps
            sleeps += 1
            if sleeps > 1:
                raise StopWatchdog

        with patch("gateway.time.monotonic", return_value=100), patch("gateway.asyncio.sleep", sleep_once):
            with self.assertRaises(StopWatchdog):
                await session.watchdog()
        session.state.assert_not_awaited()

    async def test_watchdog_kills_idle_session_after_tool_finishes(self):
        session = make_session()
        session.created = 0
        session.last_activity = 0
        session.executing_tools = 0

        async def sleep_once(_seconds):
            return None

        with patch("gateway.time.monotonic", return_value=100), patch("gateway.asyncio.sleep", sleep_once):
            await session.watchdog()
        session.state.assert_awaited_once_with("idle", "说“你好，小燕子”可继续")

    async def test_generation_change_drops_tool_result_and_does_not_resume_response(self):
        session = make_session()
        session.state = AsyncMock()

        async def execute_tool(_name, _args):
            session.generation += 1
            return {"ok": True, "value": "late"}

        session.execute_tool = execute_tool
        await session.process_tools([{"call_id": "call-1", "name": "search_web", "arguments": "{}"}], 0, response_id='offline-response', input_epoch=session.accepted_input_epoch)

        self.assertEqual(session.tool_calls[(session.accepted_input_epoch, "call-1")]["result"], {"ok": True, "value": "late"})
        self.assertEqual(session.backend.cloud.messages, [])

    async def test_unknown_tool_rejection_does_not_pause_tool_chain(self):
        # 未注册工具从未执行：确定性拒绝不能把同回合后续合法工具打成“待核实”。
        runtime = SimpleNamespace(media_service=None, device_music=None, memory_service=None, connector_registry=SimpleNamespace(manifests=lambda: ()), operation_screen_feedback=AsyncMock(), timer_snapshot=AsyncMock(return_value={"status": "idle"}))
        app = {CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30}, RUNTIME_KEY: runtime}
        session = VoiceSession(app, FakeSocket(), browser=True)
        session.backend = FakeBackend(FakeCloud())
        session.state = AsyncMock()
        with self.assertRaises(ValueError):
            await session.execute_tool("gmail_send", {})
        self.assertFalse(session.tools_paused)
        result = await session.execute_tool("desktop_timer_status", {})
        self.assertEqual(result, {"timer": {"status": "idle"}})
        self.assertFalse(session.tools_paused)


if __name__ == "__main__":
    unittest.main()

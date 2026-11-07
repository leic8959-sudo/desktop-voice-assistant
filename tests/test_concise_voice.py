"""精简设备状态和纯唤醒分流；不连接模型或设备。"""

import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession
from tests.test_voice_control import session


class ConciseVoiceTests(unittest.IsolatedAsyncioTestCase):
    async def test_native_tool_chain_shares_one_processing_state(self):
        socket = SimpleNamespace(send_json=AsyncMock())
        runtime = SimpleNamespace(connector_registry=SimpleNamespace(manifests=lambda: []))
        voice = VoiceSession({CONFIG_KEY: {}, RUNTIME_KEY: runtime}, socket, raw_pcm=True)
        for value in ("listening", "listening", "thinking", "thinking", "executing", "thinking", "executing", "thinking", "speaking", "listening"):
            await voice.state(value, "music_search" if value == "executing" else "")
        states = [call.args[0]["payload"]["state"] for call in socket.send_json.await_args_list]
        self.assertEqual(states, ["listening", "thinking", "speaking", "listening"])

    async def test_wake_only_never_runs_control_fallback(self):
        voice, _ = session()
        for text in ("你好，小燕子。", "你好小燕子！", "你好，小燕子。你好，小燕子。"):
            voice.last_user_text = text
            with patch("gateway.plan_voice_control", new_callable=AsyncMock) as planner:
                await voice.finish_unhandled_control(voice.control_turn, voice.generation)
            planner.assert_not_awaited()
        voice.backend.request_response.assert_not_awaited()

    async def test_wake_with_request_keeps_control_fallback(self):
        voice, _ = session()
        voice.last_user_text = "你好小燕子，打开计算器"
        with patch("gateway.plan_voice_control", new_callable=AsyncMock, return_value={"status": "none", "actions": []}) as planner:
            await voice.finish_unhandled_control(voice.control_turn, voice.generation)
        planner.assert_awaited_once()


if __name__ == "__main__":
    unittest.main()

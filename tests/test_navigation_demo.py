"""导航演示语音工具、会话交接和取消；不连接云端或实体设备。"""

import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession
from src.assistant_app.application.errors import LocalToolRejectedError
from src.assistant_app.application.navigation_demo import NavigationDemoService, NAVIGATION_DEMO_STEPS
from src.assistant_app.application.proactive_speech import ProactiveSpeech


class Device:
    socket = SimpleNamespace(closed=False)
    task = None


class NavigationDemoTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.device = Device()
        self.speech = SimpleNamespace(closed=False, _device=lambda: (self.device, None), speak=AsyncMock(return_value={"status": "sent"}), navigation_step=AsyncMock(return_value={"status": "sent"}), navigation_end=AsyncMock(return_value={"status": "sent"}))
        self.runtime = SimpleNamespace(config={"aliyun_api_key": "fake", "chat_tts_model": "fake", "chat_tts_voice": "fake"}, proactive_speech=self.speech, native_devices={self.device}, voice_sessions=set())
        self.service = self.runtime.navigation_demo = NavigationDemoService(self.runtime)
        self.voice = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: self.runtime}, None, browser=True)
        self.runtime.voice_sessions.add(self.voice)

    async def asyncTearDown(self):
        await self.service.close()

    async def test_voice_registers_and_schedules_actual_demo_without_destination(self):
        self.assertIn("start_navigation_demo", [tool["name"] for tool in self.voice.active_tool_schemas()])
        self.voice.last_user_text = "呃，演示一下导航。"
        result = await self.voice._execute_tool("start_navigation_demo", {})
        self.assertEqual(result["status"], "scheduled")
        self.speech.speak.assert_not_awaited()
        await self.voice._execute_tool("start_navigation_demo", {})
        self.assertIs(self.service.pending["session"], self.voice)
        self.voice.last_user_text = "能不能演示一下导航？"
        self.assertEqual((await self.voice._execute_tool("start_navigation_demo", {}))["status"], "scheduled")

    async def test_model_navigation_tool_requires_user_context_and_no_extra_arguments(self):
        self.voice.last_user_text = ""
        with self.assertRaises(LocalToolRejectedError):
            await self.voice._execute_tool("start_navigation_demo", {})
        self.voice.last_user_text = "按刚才的路线演一遍吧"
        with self.assertRaises(LocalToolRejectedError):
            await self.voice._execute_tool("start_navigation_demo", {"destination": "unknown"})
        self.assertIsNone(self.service.pending)
        self.assertEqual((await self.voice._execute_tool("start_navigation_demo", {}))["status"], "scheduled")

    async def test_verified_audio_schedules_even_when_model_never_calls_tool(self):
        await self.voice.observe_user_text("模拟导航。", refresh=False, source="verified_audio")
        self.assertIs(self.service.pending["session"], self.voice)
        self.assertTrue(self.voice.control_response_reported)
        self.service._run = AsyncMock(return_value={"status": "completed"})
        self.voice.response_completed = True
        self.voice.state = AsyncMock()
        async def no_sleep(_seconds):
            pass
        with patch("gateway.asyncio.sleep", no_sleep):
            await self.voice.watchdog()
        self.voice.closed = True
        self.runtime.voice_sessions.clear()
        await asyncio.wait_for(self.service.task, 1)
        self.service._run.assert_awaited_once()

    async def test_direct_dispatch_and_model_tool_share_one_request(self):
        await self.voice.observe_user_text("演示一下导航。", refresh=False, source="verified_audio")
        pending = self.service.pending
        await self.voice._execute_tool("start_navigation_demo", {})
        self.assertIs(self.service.pending, pending)

    async def test_verified_negative_or_quoted_audio_does_not_schedule(self):
        for text in ("不要模拟导航", "为什么模拟导航没有开始", "他说模拟导航", "翻译模拟导航", "导航去最近的加油站"):
            await self.voice.observe_user_text(text, refresh=False, source="verified_audio")
            self.assertIsNone(self.service.pending)

    async def test_new_verified_input_cancels_the_previous_request(self):
        await self.voice.observe_user_text("模拟导航", refresh=False, source="verified_audio")
        await self.voice.observe_user_text("先不要了", refresh=False, source="verified_audio")
        self.assertIsNone(self.service.pending)

    async def test_handoff_waits_for_reply_and_native_session_cleanup(self):
        await self.service.schedule(self.voice)
        self.service._run = AsyncMock(return_value={"status": "completed"})
        await self.service.voice_response_finished(self.voice)
        await asyncio.sleep(0.01)
        self.service._run.assert_not_awaited()
        self.voice.closed = True
        await asyncio.sleep(0.01)
        self.service._run.assert_not_awaited()
        self.runtime.voice_sessions.clear()
        await asyncio.wait_for(self.service.task, 1)
        self.service._run.assert_awaited_once()
        self.assertIsNone(self.service.pending)

    async def test_new_input_cancels_pending_handoff(self):
        await self.service.schedule(self.voice)
        self.service._run = AsyncMock()
        await self.service.voice_response_finished(self.voice)
        await self.service.cancel_voice_request(self.voice)
        self.voice.closed = True
        self.runtime.voice_sessions.clear()
        self.assertIsNone(self.service.pending)
        self.service._run.assert_not_awaited()

    async def test_api_uses_same_seven_steps_and_stops_after_failure(self):
        async def no_sleep(_seconds):
            pass
        with patch("src.assistant_app.application.navigation_demo.asyncio.sleep", no_sleep):
            result = await self.service.run()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(self.speech.speak.await_count, 7)
        self.assertEqual([call.kwargs["payload_extra"]["nav_icon"] for call in self.speech.speak.await_args_list], [icon for icon, _ in NAVIGATION_DEMO_STEPS])
        self.speech.speak.reset_mock()
        self.speech.speak.side_effect = [{"status": "sent"}, {"status": "cancelled", "reason": "voice_session_active"}]
        with patch("src.assistant_app.application.navigation_demo.asyncio.sleep", no_sleep):
            result = await self.service.run()
        self.assertEqual(result["status"], "aborted")
        self.assertEqual(result["failed_step"], 2)
        self.assertEqual(self.speech.speak.await_count, 2)

    async def test_indicator_starts_before_speech_and_always_ends_once(self):
        events = []
        async def step(identity, icon):
            events.append(("step", identity, icon))
            return {"status": "sent"}
        async def speak(*args, **kwargs):
            events.append(("speech", kwargs["payload_extra"]["nav_icon"]))
            return {"status": "sent"}
        self.speech.navigation_step.side_effect = step
        self.speech.speak.side_effect = speak
        async def no_sleep(_seconds):
            pass
        with patch("src.assistant_app.application.navigation_demo.asyncio.sleep", no_sleep):
            result = await self.service.run()
        self.assertEqual(result["status"], "completed")
        self.assertEqual([event[0] for event in events], ["step", "speech"] * 7)
        identities = {event[1] for event in events if event[0] == "step"}
        self.assertEqual(len(identities), 1)
        self.speech.navigation_end.assert_awaited_once_with(next(iter(identities)))

    async def test_actual_indicator_sender_emits_seven_steps_then_matching_end(self):
        self.runtime.voice_lock = asyncio.Lock()
        self.device.send_json = AsyncMock()
        speaker = ProactiveSpeech({}, self.runtime, None)
        speaker._blocked = lambda **kwargs: None
        speaker._device = lambda: (self.device, None)
        speaker.speak = AsyncMock(return_value={"status": "sent"})
        self.runtime.proactive_speech = speaker
        async def no_sleep(_seconds):
            pass
        with patch("src.assistant_app.application.navigation_demo.asyncio.sleep", no_sleep):
            result = await self.service.run()
        self.assertEqual(result["status"], "completed")
        messages = [call.args[0] for call in self.device.send_json.await_args_list]
        self.assertEqual([message["state"] for message in messages], ["step"] * 7 + ["end"])
        self.assertEqual([message["nav_icon"] for message in messages[:-1]], [icon for icon, _ in NAVIGATION_DEMO_STEPS])
        self.assertEqual(len({message["navigation_id"] for message in messages}), 1)
        self.assertTrue(all(message["type"] == "navigation" for message in messages))

    async def test_indicator_failure_never_starts_speech_and_still_cleans(self):
        self.speech.navigation_step.return_value = {"status": "skipped", "reason": "voice_session_active"}
        result = await self.service.run()
        self.assertEqual(result["status"], "aborted")
        self.speech.speak.assert_not_awaited()
        self.speech.navigation_end.assert_awaited_once()

    async def test_cancelling_between_steps_ends_the_navigation(self):
        started = asyncio.Event()
        async def speak(*args, **kwargs):
            started.set()
            return {"status": "sent"}
        self.speech.speak.side_effect = speak
        task = asyncio.create_task(self.service.run())
        await asyncio.wait_for(started.wait(), 1)
        await self.service.stop()
        await asyncio.gather(task, return_exceptions=True)
        self.speech.navigation_end.assert_awaited_once()

    async def test_watchdog_hands_off_only_when_reply_has_finished(self):
        await self.service.schedule(self.voice)
        self.voice.state = AsyncMock()
        self.voice.response_completed = True
        self.service.voice_response_finished = AsyncMock(return_value=True)
        async def no_sleep(_seconds):
            pass
        with patch("gateway.asyncio.sleep", no_sleep):
            await self.voice.watchdog()
        self.service.voice_response_finished.assert_awaited_once_with(self.voice)
        self.voice.state.assert_awaited_once_with("idle")


if __name__ == "__main__":
    unittest.main()

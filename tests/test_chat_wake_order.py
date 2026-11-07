"""真实Chat回合首句唤醒顺序：模型、记忆与电脑服务使用离线替身。"""

import asyncio
import copy
import json
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock, patch

from chat_pipeline import ChatBackend, _Chunk
from gateway import CONFIG_KEY, HTTP_KEY, RUNTIME_KEY, VoiceSession, WakePhraseRejected
from tests.test_input_observation import MessageSocket
from tests.test_voice_control import media_service


class ChatWakeOrderTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.device = MessageSocket()
        self.memory = SimpleNamespace(context=AsyncMock(return_value=[{"key": "测试偏好", "value": "verified-memory-context"}]), submit=Mock())
        self.media = media_service()
        self.runtime = SimpleNamespace(memory_service=self.memory, media_service=self.media, device_music=None, home=SimpleNamespace(configured=False), connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        self.session = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: self.runtime, HTTP_KEY: object()}, self.device, browser=True)
        self.session.require_wake_phrase = True
        self.session.wake_phrase_verified = False
        self.session.observe_user_text = AsyncMock(wraps=self.session.observe_user_text)
        self.backend = ChatBackend(self.session, "qwen_chat")
        self.session.backend = self.backend
        self.backend._history = []
        self.backend._instructions = "offline-before-verified-context"
        self.backend._events = asyncio.Queue()
        self.backend._cancelled = set()
        self.backend._response_sequence = self.backend._turn_generation = 0
        self.backend._active_response_id = self.backend._pending_history = None
        self.backend._tts_text = None
        self.backend._finals = []
        self.backend._last_partial = ""
        self.backend._asr_audio = asyncio.Queue(maxsize=4)
        self.model_calls = []
        self.tasks = []

        async def model(messages):
            self.model_calls.append({"verified": self.session.wake_phrase_verified, "control_turn": self.session.control_turn, "last_user_text": self.session.last_user_text, "messages": copy.deepcopy(messages)})
            if len(self.model_calls) == 1:
                yield _Chunk(tool_delta=[{"index": 0, "id": "offline-open", "function": {"name": "desktop_open_app", "arguments": '{"app":"calculator"}'}}])
            else:
                yield _Chunk()

        self.backend._request = model

    async def asyncTearDown(self):
        await self.backend.close()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        await asyncio.gather(*self.session.tasks, return_exceptions=True)

    def cloud(self):
        task = asyncio.create_task(self.session.cloud_output())
        self.tasks.append(task)
        return task

    async def drain(self):
        task = self.backend._turn_drain_task
        if task is not None:
            await asyncio.wait_for(asyncio.shield(task), 2)

    def empty_model(self):
        async def model(messages):
            self.model_calls.append({"verified": self.session.wake_phrase_verified, "control_turn": self.session.control_turn, "last_user_text": self.session.last_user_text, "messages": copy.deepcopy(messages)})
            yield _Chunk()
        self.backend._request = model

    async def consumed_partial(self):
        consumed = asyncio.Event()
        original = self.backend.events

        async def events():
            async for event in original():
                if event.get("type") == "conversation.item.input_audio_transcription.completed" and event.get("transcript") == "你好":
                    consumed.set()
                yield event

        self.backend.events = events
        self.cloud()
        await self.backend._start_user_turn("你好")
        await self.drain()
        await asyncio.wait_for(consumed.wait(), 2)
        return self.tasks[-1]

    async def test_first_wake_command_is_observed_before_model_and_control_tool(self):
        self.cloud()
        text = "你好，小燕子，打开电脑的计算器。"
        plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "calculator"}}]}
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value=plan)) as planner:
            await self.backend._start_user_turn(text)
            await self.drain()
            await self.device.wait(lambda: any(item.get("type") == "stt" and item.get("text") == text for item in self.device.json))
        self.assertTrue(self.session.wake_phrase_verified)
        self.assertEqual(self.session.last_user_text, text, "唤醒确认后首句命令必须进入真实用户控制上下文")
        self.assertEqual(self.session.control_turn, 1)
        self.assertEqual([text for _seen, text in self.session.control_fragments], [text])
        self.assertEqual(self.session.observe_user_text.await_count, 1)
        self.memory.context.assert_awaited_once_with(text)
        self.memory.submit.assert_called_once()
        self.assertTrue(self.model_calls[0]["verified"], "不得先请求模型再由输出消费端补做唤醒验证")
        self.assertEqual(self.model_calls[0]["control_turn"], 1)
        self.assertIn("verified-memory-context", self.model_calls[0]["messages"][0]["content"])
        planner.assert_not_awaited()
        self.media.invoke.assert_awaited_once()
        self.assertEqual(self.media.invoke.await_args.args, ("desktop_open_app", {"app": "calculator"}))

    async def test_missing_wake_phrase_never_reaches_model_or_tool(self):
        cloud = self.cloud()
        with patch("gateway.plan_voice_control", new=AsyncMock()) as planner:
            await self.backend._start_user_turn("打开电脑的计算器。")
            await self.drain()
            with self.assertRaises(WakePhraseRejected):
                await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertEqual(self.model_calls, [], "无唤醒首句必须在模型与工具之前拒绝，不能只挡设备音频")
        self.media.invoke.assert_not_awaited()
        planner.assert_not_awaited()
        self.memory.context.assert_not_awaited()
        self.memory.submit.assert_not_called()
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.session.control_turn, 0)
        self.assertEqual(self.session.control_fragments, [])

    async def test_actual_execute_tool_gate_still_rejects_unverified_session(self):
        with self.assertRaisesRegex(ValueError, "唤醒词尚未确认"):
            await self.session.execute_tool("desktop_open_app", {"app": "calculator"})
        self.media.invoke.assert_not_awaited()
        self.runtime.operation_screen_feedback.assert_not_awaited()

    async def test_split_wake_prefix_is_combined_before_single_observation_and_model(self):
        self.empty_model()
        await self.consumed_partial()
        deadline = self.session.wake_deadline
        self.assertEqual(self.model_calls, [])
        self.session.observe_user_text.assert_not_awaited()
        await self.backend._start_user_turn("小燕子，打开计算器。")
        await self.drain()
        await self.device.wait(lambda: any(item.get("type") == "stt" for item in self.device.json))
        self.assertTrue(self.session.wake_phrase_verified)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.assertEqual(self.session.last_user_text, "你好小燕子，打开计算器。")
        self.assertEqual(self.session.control_turn, 1)
        self.assertEqual(len(self.model_calls), 1)
        self.assertEqual(self.model_calls[0]["last_user_text"], "你好小燕子，打开计算器。")
        self.memory.context.assert_awaited_once()

    async def test_cancelled_partial_cannot_be_reauthorized_by_output_gate_prefix(self):
        cloud = await self.consumed_partial()
        deadline = self.session.wake_deadline
        await self.backend.cancel_response()
        self.assertEqual(self.backend._pending_wake, {})
        await self.backend._start_user_turn("小燕子打开计算器。")
        await self.drain()
        with self.assertRaises(WakePhraseRejected):
            await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.assertEqual(self.model_calls, [])
        self.media.invoke.assert_not_awaited()
        self.session.observe_user_text.assert_not_awaited()

    async def test_device_abort_discards_partial_without_renewing_wake_deadline(self):
        cloud = await self.consumed_partial()
        deadline = self.session.wake_deadline
        self.device.receive_event({"type": "abort"})
        self.device.incoming.put_nowait(None)
        await self.session.device_input()
        self.assertEqual(self.backend._pending_wake, {})
        await self.backend._start_user_turn("小燕子打开计算器。")
        await self.drain()
        with self.assertRaises(WakePhraseRejected):
            await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.assertEqual(self.model_calls, [])
        self.media.invoke.assert_not_awaited()
        self.session.observe_user_text.assert_not_awaited()

    async def test_late_complete_phrase_is_rejected_before_unscheduled_output_consumer(self):
        self.session.wake_deadline = asyncio.get_running_loop().time() - 1
        deadline = self.session.wake_deadline
        await self.backend._start_user_turn("你好小燕子打开计算器。")
        await self.drain()
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.model_calls, [])
        self.media.invoke.assert_not_awaited()
        self.session.observe_user_text.assert_not_awaited()
        cloud = self.cloud()
        with self.assertRaisesRegex(WakePhraseRejected, "timeout"):
            await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.assertEqual(self.session.wake_rejection, "timeout")
        await self.backend.cancel_response()
        await self.backend.clear_audio()
        await self.backend._start_user_turn("你好小燕子再次打开计算器。")
        await self.drain()
        self.assertEqual(self.model_calls, [])
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.assertEqual(self.session.wake_rejection, "timeout")

    async def test_manual_input_and_already_verified_followup_keep_existing_behavior(self):
        self.empty_model()
        self.session.require_wake_phrase = False
        await self.backend._start_user_turn("手动输入不要求唤醒词")
        await self.drain()
        self.assertEqual(len(self.model_calls), 1)
        self.assertEqual(self.session.control_turn, 1)
        self.session.require_wake_phrase = True
        self.session.wake_phrase_verified = True
        self.session.wake_deadline = asyncio.get_running_loop().time() - 1
        await self.backend._start_user_turn("已验证的正常接话")
        await self.drain()
        self.assertEqual(len(self.model_calls), 2)
        self.assertEqual(self.session.control_turn, 2)

    async def test_wake_verification_finishes_before_slow_memory_observation(self):
        self.empty_model()
        observing, release = asyncio.Event(), asyncio.Event()

        async def context(_query):
            observing.set()
            await release.wait()
            return []

        self.memory.context.side_effect = context
        self.cloud()
        try:
            await self.backend._start_user_turn("你好，小燕子。")
            await asyncio.wait_for(observing.wait(), 2)
            await self.device.wait(lambda: any(item.get("type") == "stt" for item in self.device.json))
            self.assertTrue(self.session.wake_phrase_verified)
            self.assertEqual(self.model_calls, [])
            self.assertEqual(self.session.control_turn, 1)
            release.set()
            await self.drain()
            self.assertEqual(len(self.model_calls), 1)
            self.memory.context.assert_awaited_once()
        finally:
            release.set()

    async def test_closed_pending_wake_cannot_start_a_later_model(self):
        await self.consumed_partial()
        deadline = self.session.wake_deadline
        await self.backend.close()
        self.assertEqual(self.backend._pending_wake, {})
        await self.backend._start_user_turn("小燕子打开计算器。")
        self.assertEqual(self.model_calls, [])
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.session.wake_deadline, deadline)
        self.media.invoke.assert_not_awaited()

    async def test_wake_capacity_rejection_cannot_be_bypassed_by_complete_phrase(self):
        with patch("gateway.WAKE_MAX_EVENTS", 1):
            cloud = await self.consumed_partial()
            await self.backend._start_user_turn("小燕子打开计算器。")
            await self.drain()
            with self.assertRaisesRegex(WakePhraseRejected, "buffer_limit"):
                await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertFalse(self.session.wake_phrase_verified)
        self.assertEqual(self.model_calls, [])
        self.media.invoke.assert_not_awaited()

    async def test_deepseek_native_first_wake_command_is_observed_and_authorized_once(self):
        self.backend.name = "deepseek"
        self.session.browser = False
        self.session.raw_pcm = True
        self.session.manual = False
        self.session.memory_audio_allowed = False
        self.session.command_audio_allowed = False
        self.cloud()
        text = "你好，小燕子，打开电脑的计算器。"
        plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "calculator"}}]}
        with patch("gateway.plan_voice_control", new=AsyncMock(return_value=plan)) as planner:
            await self.backend._start_user_turn(text)
            await self.drain()
            await self.device.wait(lambda: any(item.get("type") == "stt" and item.get("text") == text for item in self.device.json))
        self.assertTrue(self.session.wake_phrase_verified)
        self.assertEqual(self.session.last_user_text, text)
        self.assertEqual(self.session.control_turn, 1)
        self.assertEqual([value for _seen, value in self.session.control_fragments], [text])
        self.assertEqual(self.session.observe_user_text.await_count, 1)
        self.memory.context.assert_awaited_once_with(text)
        self.assertTrue(self.model_calls[0]["verified"])
        self.assertEqual(self.model_calls[0]["control_turn"], 1)
        self.assertIn("verified-memory-context", self.model_calls[0]["messages"][0]["content"])
        planner.assert_not_awaited()
        self.media.invoke.assert_awaited_once()
        self.assertEqual(self.media.invoke.await_args.args, ("desktop_open_app", {"app": "calculator"}))

    async def test_complete_phrase_exceeding_byte_budget_never_reaches_model_without_consumer(self):
        with patch("gateway.WAKE_MAX_BYTES", 32):
            await self.backend._start_user_turn("你好小燕子打开计算器。")
            await self.drain()
            self.assertEqual(self.backend._pending_wake.get("rejection"), "buffer_limit")
            self.assertFalse(self.session.wake_phrase_verified)
            self.assertEqual(self.model_calls, [])
            self.session.observe_user_text.assert_not_awaited()
            self.memory.context.assert_not_awaited()
            self.media.invoke.assert_not_awaited()
            cloud = self.cloud()
            with self.assertRaisesRegex(WakePhraseRejected, "buffer_limit"):
                await asyncio.wait_for(asyncio.shield(cloud), 2)

    async def test_output_byte_rejection_is_terminal_before_session_cleanup_can_run(self):
        partial = "，"
        complete = "你好小燕子打开计算器。"
        event = {"type": "conversation.item.input_audio_transcription.completed", "transcript": partial}
        plain_bytes = len(json.dumps(event, ensure_ascii=False).encode("utf-8"))
        output_bytes = len(json.dumps({**event, "wake_verification": "pending"}, ensure_ascii=False).encode("utf-8"))
        complete_bytes = len(json.dumps({**event, "transcript": complete}, ensure_ascii=False).encode("utf-8"))
        count = 6
        budget = count * plain_bytes + complete_bytes
        self.assertGreater(count * output_bytes, budget)
        cloud = self.cloud()
        plan = {"status": "execute", "actions": [{"tool": "desktop_open_app", "arguments": {"app": "calculator"}}]}
        with patch("gateway.WAKE_MAX_BYTES", budget), patch("gateway.plan_voice_control", new=AsyncMock(return_value=plan)) as planner:
            for _ in range(count):
                await self.backend._start_user_turn(partial)
                await self.drain()
            with self.assertRaisesRegex(WakePhraseRejected, "buffer_limit"):
                await asyncio.wait_for(asyncio.shield(cloud), 2)
            self.assertFalse(self.backend._closed, "保留清理尚未调度的真实窗口")
            self.assertEqual(self.session.wake_rejection, "buffer_limit")
            deadline = self.session.wake_deadline
            await self.backend.cancel_response()
            await self.backend.clear_audio()
            self.assertEqual(self.backend._pending_wake, {})
            await self.backend._start_user_turn(complete)
            await self.drain()
        self.assertEqual(self.model_calls, [], "输出门禁已经拒绝后不能在清理调度前重新授权模型")
        self.assertFalse(self.session.wake_phrase_verified)
        self.session.observe_user_text.assert_not_awaited()
        self.memory.context.assert_not_awaited()
        self.media.invoke.assert_not_awaited()
        planner.assert_not_awaited()
        self.assertEqual(self.session.wake_rejection, "buffer_limit")
        self.assertEqual(self.session.wake_deadline, deadline)


if __name__ == "__main__":
    unittest.main()

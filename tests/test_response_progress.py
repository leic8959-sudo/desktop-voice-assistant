"""响应活动、绝对期限和输入归属；云、工具、时钟均为隔离替身。"""
import asyncio
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession


class StopLoop(Exception):
    pass


class ResponseProgressTests(unittest.IsolatedAsyncioTestCase):
    def voice(self):
        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=None, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        voice = VoiceSession({CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30, "enable_search": False}, RUNTIME_KEY: runtime}, SimpleNamespace(send_json=AsyncMock(), closed=False), browser=True)
        voice.state = AsyncMock()
        voice.created = voice.last_activity = 0
        voice.backend = SimpleNamespace(name="qwen_realtime", connected=True, cancel_response=AsyncMock(), update_instructions=AsyncMock(), request_response=AsyncMock())
        voice.refresh_memory_context = AsyncMock()
        return voice

    async def cycle(self, voice, now):
        count = 0

        async def sleep(_seconds):
            nonlocal count
            count += 1
            if count > 1:
                raise StopLoop()

        with patch("gateway.time.monotonic", return_value=now), patch("gateway.asyncio.sleep", sleep):
            with self.assertRaises(StopLoop):
                await voice.watchdog()

    async def test_idle_never_closes_active_response_or_queued_audio(self):
        voice = self.voice()
        voice.current_response = "active"
        voice.response_started_at = 80
        voice.response_progress_at = 95
        await self.cycle(voice, 100)
        voice.state.assert_not_awaited()
        voice.current_response = None
        voice.response_requested_at = 90
        await self.cycle(voice, 100)
        voice.state.assert_not_awaited()
        voice.response_requested_at = None
        voice.playback.put_nowait((0, "pcm"))
        await self.cycle(voice, 100)
        voice.state.assert_not_awaited()

    async def test_stalled_response_and_trickling_absolute_limit_raise_visible_error(self):
        for start, progress, now, phase in ((10, 20, 70, "response_stalled"), (10, 129, 131, "response_total")):
            voice = self.voice()
            voice.current_response = "active"
            voice.response_started_at, voice.response_progress_at = start, progress
            with patch("gateway.time.monotonic", return_value=now), patch("gateway.asyncio.sleep", new=AsyncMock()):
                with self.assertLogs("assistant", level="WARNING") as logs:
                    with self.assertRaisesRegex(RuntimeError, "等待超时"):
                        await voice.watchdog()
            self.assertIn(phase, " ".join(logs.output))
            voice.state.assert_not_awaited()

    async def test_missing_response_created_has_its_own_deadline(self):
        voice = self.voice()
        voice.response_requested_at = 1
        with patch("gateway.time.monotonic", return_value=47), patch("gateway.asyncio.sleep", new=AsyncMock()):
            with self.assertRaisesRegex(RuntimeError, "等待超时"):
                await voice.watchdog()

    async def output(self, voice, events):
        async def stream():
            for event in events:
                yield event
        voice.backend.events = stream
        with self.assertRaisesRegex(RuntimeError, "云端连接已结束"):
            await voice.cloud_output()
        for task in voice.tasks:
            await task

    async def test_same_input_late_asr_can_bind_tool_but_new_input_cannot(self):
        def created(identity):
            return {"type": "response.created", "response": {"id": identity}}

        def done(identity):
            return {"type": "response.done", "response": {"id": identity, "status": "completed", "output": [{"type": "function_call", "name": "music_play", "call_id": "track-call", "arguments": '{"selection_id":"actual"}'}]}}

        voice = self.voice()
        voice.process_tools = AsyncMock()
        await self.output(voice, [{"type": "input_audio_buffer.speech_started"}, created("same-input"), {"type": "conversation.item.input_audio_transcription.completed", "transcript": "原版"}, done("same-input")])
        voice.process_tools.assert_awaited_once()
        self.assertEqual(voice.process_tools.await_args.kwargs["input_epoch"], voice.accepted_input_epoch)

        voice = self.voice()
        voice.process_tools = AsyncMock()
        await self.output(voice, [{"type": "input_audio_buffer.speech_started"}, created("old-input"), {"type": "input_audio_buffer.speech_started"}, {"type": "conversation.item.input_audio_transcription.completed", "transcript": "先不要"}, done("old-input")])
        voice.process_tools.assert_not_awaited()

    async def test_argument_progress_is_observable_without_logging_contents(self):
        voice = self.voice()
        events = [{"type": "response.created", "response": {"id": "safe-id"}}, {"type": "response.function_call_arguments.done", "response_id": "safe-id", "name": "music_play", "arguments": '{"secret":"never-log-me"}'}]
        with self.assertLogs("assistant", level="INFO") as logs:
            await self.output(voice, events)
        output = " ".join(logs.output)
        self.assertIn("music_play", output)
        self.assertIn("argument_bytes=", output)
        self.assertNotIn("never-log-me", output)

    async def test_incomplete_response_never_executes_partial_tool_call(self):
        voice = self.voice()
        voice.process_tools = AsyncMock()
        await self.output(voice, [{"type": "response.created", "response": {"id": "partial"}}, {"type": "response.done", "response": {"id": "partial", "status": "incomplete", "output": [{"type": "function_call", "name": "music_play", "call_id": "partial-call", "arguments": "{}"}]}}])
        voice.process_tools.assert_not_awaited()
        self.assertTrue(any(call.args[0].get("type") == "alert" for call in voice.socket.send_json.await_args_list))

    async def test_missing_created_identity_never_authorizes_tool_from_done_only(self):
        voice = self.voice()
        voice.process_tools = AsyncMock()
        await self.output(voice, [{"type": "response.done", "response": {"id": "orphan", "status": "completed", "output": [{"type": "function_call", "name": "music_play", "call_id": "orphan-call", "arguments": "{}"}]}}])
        voice.process_tools.assert_not_awaited()

    async def test_tool_deadline_retains_unknown_result_and_pauses_followup(self):
        voice = self.voice()

        async def stuck(*_args):
            await asyncio.Event().wait()

        voice._execute_tool = stuck
        with patch("gateway.TOOL_EXECUTION_SECONDS", 0.01):
            with self.assertRaises(TimeoutError):
                await voice.execute_tool("music_play", {"selection_id": "actual"})
        self.assertTrue(voice.tools_paused)
        self.assertTrue(any(call.args[0].get("type") == "alert" for call in voice.socket.send_json.await_args_list))

    async def test_generic_connector_keeps_read_scope_even_with_model_confirmation_id(self):
        from src.assistant_app.integrations.contracts import ConnectorAction, ConnectorManifest, PermissionScope
        from src.assistant_app.application.errors import LocalToolRejectedError
        voice = self.voice()
        registry = voice.app[RUNTIME_KEY].connector_registry
        registry.get = lambda _: SimpleNamespace(manifest=ConnectorManifest("gmail", "Gmail", "1", (ConnectorAction("send", "send", frozenset({PermissionScope.SEND}), True),)))
        registry.invoke = AsyncMock()
        with self.assertRaises(LocalToolRejectedError):
            await voice.execute_tool("invoke_connector", {"connector_id": "gmail", "action": "send", "arguments": {}, "confirmation_id": "model-cannot-confirm"})
        registry.invoke.assert_not_awaited()
        self.assertFalse(voice.tools_paused)
        with self.assertRaises(LocalToolRejectedError):
            await voice.execute_tool("invoke_workflow", {"workflow_id": "github-issue", "arguments": {}})
        self.assertFalse(voice.tools_paused)

    async def test_external_connector_permission_failure_remains_unknown(self):
        from src.assistant_app.integrations.contracts import ConnectorAction, ConnectorManifest
        voice = self.voice()
        registry = voice.app[RUNTIME_KEY].connector_registry
        registry.get = lambda _: SimpleNamespace(manifest=ConnectorManifest("known", "Known", "1", (ConnectorAction("read", "read"),)))
        registry.invoke = AsyncMock(side_effect=PermissionError("external-provider-may-have-acted"))
        with self.assertRaises(PermissionError):
            await voice.execute_tool("invoke_connector", {"connector_id": "known", "action": "read", "arguments": {}})
        self.assertTrue(voice.tools_paused)
        registry.invoke.assert_awaited_once()

    async def test_slow_memory_refresh_cannot_write_old_input_context(self):
        voice = self.voice()
        entered, release = asyncio.Event(), asyncio.Event()

        async def context(_query):
            entered.set()
            await release.wait()
            return {}

        voice.app[RUNTIME_KEY].memory_service = SimpleNamespace(context=context)
        refresh = asyncio.create_task(VoiceSession.refresh_memory_context(voice))
        await asyncio.wait_for(entered.wait(), 1)
        voice.accepted_input_epoch += 1
        release.set()
        await refresh
        voice.backend.update_instructions.assert_not_awaited()

"""同一模型的有限候选选择合同；全部模型、设备与音乐输出均为替身。"""
import asyncio
import base64
import copy
import json
import time
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

import aiohttp

from chat_pipeline import ChatBackend, _Chunk
from gateway import CONFIG_KEY, RUNTIME_KEY, RealtimeBackend, VoiceSession
from src.assistant_app.application.internet_music import MUSIC_SELECTION_DECISION_TOOL


SCHEMAS = [{"type": "function", "name": name, "description": "fixed", "parameters": {"type": "object", "properties": {"selection_id": {"type": "string"}}, "required": ["selection_id"]}} for name in ("music_play", "music_queue")]


class Cloud:
    closed = False

    def __init__(self):
        self.queue = asyncio.Queue()

    def __aiter__(self):
        return self

    async def __anext__(self):
        event = await self.queue.get()
        if event is None:
            raise StopAsyncIteration
        return SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(event))

    async def close(self):
        self.closed = True
        self.queue.put_nowait(None)


class MusicChoiceContractTests(unittest.IsolatedAsyncioTestCase):
    def voice(self):
        self.effects = []
        self.settled = asyncio.Event()
        self.final = asyncio.Event()

        async def invoke(name, args, **_kwargs):
            if name == "music_search":
                return {"items": [{"selection_id": "real-original", "title": "晴天", "album": "叶惠美"}, {"selection_id": "real-live", "title": "晴天现场"}], "selection_ttl_s": 600}
            self.effects.append((name, args))
            self.settled.set()
            return {"status": "scheduled"}

        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=SimpleNamespace(invoke=invoke, cancel_voice_request=AsyncMock()), connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        socket = SimpleNamespace(closed=False, send_json=AsyncMock())
        voice = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: runtime}, socket, browser=True)
        voice.refresh_memory_context = AsyncMock()
        voice.active_tool_schemas = lambda: copy.deepcopy(SCHEMAS)
        return voice

    async def prepare(self, voice):
        await voice.observe_user_text("先列版本不要播", refresh=False, source="text")
        await voice._execute_tool("music_search", {"query": "晴天"})
        self.assertFalse(voice.needs_music_choice())

    async def realtime(self, outcome="play", unsupported=False):
        voice = self.voice()
        backend = voice.backend = RealtimeBackend(voice)
        backend.cloud = Cloud()
        backend.current_instructions = "original-instructions"
        self.requests = []

        async def send(payload):
            self.requests.append(payload)
            if payload["type"] == "session.update":
                backend.cloud.queue.put_nowait({"type": "session.updated", "session": payload["session"]})
            elif payload["type"] == "response.create":
                state = backend.music_choice_session
                if state and state["phase"] == "active":
                    name = "music_play" if outcome == "play" else "music_selection_decision"
                    args = {"selection_id": "real-original"} if outcome == "play" else {"outcome": outcome}
                    calls = [] if unsupported else [{"type": "function_call", "name": name, "call_id": "choice-call", "arguments": json.dumps(args)}]
                    backend.cloud.queue.put_nowait({"type": "response.created", "response": {"id": "choice-response"}})
                    backend.cloud.queue.put_nowait({"type": "response.done", "response": {"id": "choice-response", "status": "completed", "output": calls}})
                else:
                    self.final.set()

        backend._send = AsyncMock(side_effect=send)
        await self.prepare(voice)
        return voice, backend

    async def draft(self, voice, backend, *, transcript_first=True):
        if transcript_first:
            await voice.observe_user_text("随便来一个放就行了", refresh=False, source="text")
        else:
            backend.cloud.queue.put_nowait({"type": "input_audio_buffer.speech_started"})
        backend.cloud.queue.put_nowait({"type": "response.created", "response": {"id": "draft"}})
        backend.cloud.queue.put_nowait({"type": "response.audio.delta", "response_id": "draft", "delta": base64.b64encode(b"\x01\0" * 480).decode()})
        backend.cloud.queue.put_nowait({"type": "response.audio_transcript.done", "response_id": "draft", "transcript": "我已经播放了"})
        backend.cloud.queue.put_nowait({"type": "response.done", "response": {"id": "draft", "status": "completed", "output": []}})

    async def test_realtime_late_asr_forces_once_waits_acks_restores_then_plays_once(self):
        voice, backend = await self.realtime()
        task = asyncio.create_task(voice.cloud_output())
        try:
            await self.draft(voice, backend, transcript_first=False)
            await asyncio.sleep(0.01)
            self.assertEqual(self.requests, [], "不能在真实转写绑定前决定播放")
            backend.cloud.queue.put_nowait({"type": "conversation.item.input_audio_transcription.completed", "transcript": "随便来一个放就行了"})
            await asyncio.wait_for(self.final.wait(), 1)
            self.assertEqual(self.effects, [("music_play", {"selection_id": "real-original"})])
            updates = [item["session"] for item in self.requests if item["type"] == "session.update"]
            self.assertEqual([item["tool_choice"] for item in updates], ["required", "auto"])
            self.assertEqual(updates[-1]["instructions"], "original-instructions")
            self.assertIsNone(backend.music_choice_session)
            self.assertTrue(voice.playback.empty())
            self.assertFalse(any("我已经播放了" in str(call.args[0]) for call in voice.socket.send_json.await_args_list))
        finally:
            task.cancel(); await asyncio.gather(task, return_exceptions=True)
            await asyncio.gather(*voice.tasks, return_exceptions=True)
            await backend.close()

    async def test_cancel_and_unrelated_decisions_never_play(self):
        for outcome in ("cancel", "unrelated"):
            voice, backend = await self.realtime(outcome)
            task = asyncio.create_task(voice.cloud_output())
            try:
                await self.draft(voice, backend)
                await asyncio.wait_for(self.final.wait(), 1)
                self.assertEqual(self.effects, [])
                self.assertIsNone(voice.music_choices)
                self.assertIsNone(backend.music_choice_session)
            finally:
                task.cancel(); await asyncio.gather(task, return_exceptions=True)
                await asyncio.gather(*voice.tasks, return_exceptions=True)
                await backend.close()

    async def test_required_without_call_completes_truthful_no_action_after_restore(self):
        voice, backend = await self.realtime(unsupported=True)
        task = asyncio.create_task(voice.cloud_output())
        await self.draft(voice, backend)
        try:
            async with asyncio.timeout(1):
                while not voice.response_completed:
                    await asyncio.sleep(0.001)
            self.assertEqual(self.effects, [])
            self.assertEqual(sum(item["type"] == "response.create" for item in self.requests), 1)
            self.assertIsNone(backend.music_choice_session)
            self.assertTrue(any(call.args[0].get("status") == "未播放" for call in voice.socket.send_json.await_args_list))
            with self.assertRaises(PermissionError):
                await voice.execute_tool("desktop_open_app", {"app": "calculator"})
            with self.assertRaises(PermissionError):
                await voice.execute_tool("music_play", {"selection_id": "real-original"})
            self.assertFalse(voice.tools_paused, "确定零执行不能标作结果未知")
        finally:
            task.cancel(); await asyncio.gather(task, return_exceptions=True)
            await backend.close()

    async def test_ack_mismatch_and_new_input_cannot_authorize_old_choice(self):
        voice, backend = await self.realtime()
        await voice.observe_user_text("原版", refresh=False, source="text")
        await backend.request_music_choice()
        self.requests.clear()
        await backend.music_choice_updated({"type": "session.updated", "session": {"tool_choice": "auto", "tools": SCHEMAS}})
        self.assertEqual(self.requests, [])
        await voice.observe_user_text("先不要了", refresh=False, source="text")
        with self.assertRaisesRegex(RuntimeError, "请求已更新"):
            await backend.music_choice_updated({"type": "session.updated", "session": {"tool_choice": "required", "tools": voice.music_choice_tools(), "instructions": backend.music_choice_session["start_instructions"]}})
        self.assertEqual(self.effects, [])
        await backend.close()

    async def test_chat_same_model_required_round_suppresses_unexecuted_promise(self):
        voice = self.voice()
        backend = voice.backend = ChatBackend(voice, "qwen_chat")
        backend._history, backend._instructions, backend._events = [], "original", asyncio.Queue()
        backend._cancelled = set()
        backend._response_sequence, backend._active_response_id = 0, None
        await self.prepare(voice)
        await voice.observe_user_text("随便来一个放就行了", refresh=False, source="text")
        rounds = []

        async def model(messages):
            rounds.append(copy.deepcopy(backend._forced_music_tools))
            if len(rounds) == 1:
                yield _Chunk(content="我已经播放了。")
            elif len(rounds) == 2:
                yield _Chunk(tool_delta=[{"index": 0, "id": "play-call", "function": {"name": "music_play", "arguments": '{"selection_id":"real-original"}'}}])
            else:
                yield _Chunk(content="准备播放。")

        backend._request = model
        backend._tts_turn = AsyncMock()
        try:
            await backend._run_turn("随便来一个放就行了")
            self.assertEqual(self.effects, [("music_play", {"selection_id": "real-original"})])
            self.assertEqual(len(rounds), 3)
            self.assertIsNone(rounds[0]); self.assertIsNotNone(rounds[1]); self.assertIsNone(rounds[2])
            backend._tts_turn.assert_awaited_once()
            transcript = [item.get("transcript") for item in backend._events._queue if item["type"] == "response.audio_transcript.done"]
            self.assertEqual(transcript, ["准备播放。"])
        finally:
            await backend.close()

    async def test_initial_search_reply_and_expired_candidates_do_not_force_playback(self):
        voice, backend = await self.realtime()
        events = voice.music_selection_events()
        backend.cloud.queue.put_nowait({"type": "response.created", "response": {"id": "search-reply"}})
        backend.cloud.queue.put_nowait({"type": "response.text.done", "response_id": "search-reply", "text": "请选择版本"})
        self.assertEqual((await anext(events))["type"], "response.created")
        self.assertEqual((await anext(events))["text"], "请选择版本")
        self.assertEqual(self.requests, [])
        voice.music_choices["expires"] = time.monotonic() - 1
        await voice.observe_user_text("原版", refresh=False, source="text")
        self.assertFalse(voice.needs_music_choice())
        await events.aclose(); await backend.close()

    async def test_preview_suppression_has_a_hard_byte_limit(self):
        voice, backend = await self.realtime()
        await voice.observe_user_text("原版", refresh=False, source="text")
        task = asyncio.create_task(voice.cloud_output())
        backend.cloud.queue.put_nowait({"type": "response.created", "response": {"id": "large-draft"}})
        backend.cloud.queue.put_nowait({"type": "response.text.done", "response_id": "large-draft", "text": "x" * (2 * 1024 * 1024)})
        try:
            with self.assertRaisesRegex(RuntimeError, "超过缓冲上限"):
                await asyncio.wait_for(task, 1)
            self.assertEqual(self.effects, [])
            self.assertTrue(voice.playback.empty())
        finally:
            await backend.close()

    async def test_forced_mode_chat_payload_is_required_for_both_provider_shapes(self):
        for provider in ("qwen_chat", "deepseek"):
            voice = self.voice()
            backend = ChatBackend(voice, provider)
            backend.config.update({"chat_max_tokens": 256, "qwen_chat_thinking": None, "deepseek_thinking": "unset"})
            backend._chat_model = "fixed-model"
            backend._tools = []
            backend._forced_music_tools = [{"type": "function", "function": {"name": "music_selection_decision"}}]
            payload = backend._chat_payload([], stream=True)
            self.assertEqual(payload["tool_choice"], "required")
            self.assertEqual(payload["tools"], backend._forced_music_tools)

    async def test_required_mixed_cancel_and_play_rejected_before_any_effect(self):
        voice, backend = await self.realtime()
        original = backend._send.side_effect

        async def contradictory(payload):
            if payload["type"] == "response.create" and backend.music_choice_session and backend.music_choice_session["phase"] == "active":
                self.requests.append(payload)
                backend.cloud.queue.put_nowait({"type": "response.created", "response": {"id": "mixed"}})
                backend.cloud.queue.put_nowait({"type": "response.done", "response": {"id": "mixed", "status": "completed", "output": [{"type": "function_call", "name": "music_play", "call_id": "play", "arguments": '{"selection_id":"real-original"}'}, {"type": "function_call", "name": "music_selection_decision", "call_id": "cancel", "arguments": '{"outcome":"cancel"}'}]}})
            else:
                await original(payload)

        backend._send.side_effect = contradictory
        task = asyncio.create_task(voice.cloud_output())
        await self.draft(voice, backend)
        try:
            with self.assertRaises(PermissionError):
                await asyncio.wait_for(task, 1)
            self.assertEqual(self.effects, [])
        finally:
            await backend.close()

    async def test_chat_missing_decision_finishes_without_third_model_round_or_tts(self):
        voice = self.voice()
        backend = voice.backend = ChatBackend(voice, "qwen_chat")
        backend._history, backend._instructions, backend._events = [], "original", asyncio.Queue()
        backend._cancelled = set()
        backend._response_sequence, backend._active_response_id = 0, None
        await self.prepare(voice)
        await voice.observe_user_text("先都不要播放", refresh=False, source="text")
        rounds = []

        async def model(_messages):
            rounds.append(backend._forced_music_tools is not None)
            yield _Chunk(content="未形成工具调用的普通回答。")

        backend._request = model
        backend._tts_turn = AsyncMock()
        try:
            await backend._run_turn("先都不要播放")
            self.assertEqual(rounds, [False, True])
            self.assertEqual(self.effects, [])
            backend._tts_turn.assert_not_awaited()
            self.assertIsNone(voice.music_choices)
            self.assertFalse(voice.tools_paused)
            with self.assertRaises(PermissionError):
                await voice.execute_tool("gmail_create_draft", {"to": "nobody@example.invalid", "subject": "fixed", "body": "fixed"})
            await voice.observe_user_text("新问题", refresh=False, source="text")
            self.assertNotEqual(voice.music_no_action_turn, voice.input_turn_serial)
        finally:
            await backend.close()

"""真实输入入口与后端的观察归属，网络与记忆服务均为受控替身。"""

import asyncio
import json
import time
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock

import aiohttp

from chat_pipeline import ChatBackend, _Chunk
from gateway import CONFIG_KEY, RUNTIME_KEY, RealtimeBackend, VoiceSession, WakePhraseRejected
from tests.test_chat_playback_handoff import RecordingDevice


class MessageSocket(RecordingDevice):
    def __init__(self):
        super().__init__()
        self.incoming = asyncio.Queue()

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.incoming.get()
        if message is None:
            raise StopAsyncIteration
        return message

    def receive_event(self, event):
        self.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(event)))


class InputObservationTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.sessions = []
        self.tasks = []

    async def asyncTearDown(self):
        for session in self.sessions:
            await asyncio.gather(*session.tasks, return_exceptions=True)
            await session.backend.close()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)

    def session(self, provider):
        memory = SimpleNamespace(context=AsyncMock(return_value=[]), submit=Mock())
        runtime = SimpleNamespace(memory_service=memory, media_service=SimpleNamespace(cancel_voice_request=AsyncMock()), device_music=SimpleNamespace(cancel_voice_request=AsyncMock()), connector_registry=SimpleNamespace(manifests=lambda: []))
        device = MessageSocket()
        session = VoiceSession({CONFIG_KEY: {"enable_search": False}, RUNTIME_KEY: runtime}, device, browser=True)
        session.observe_user_text = AsyncMock(wraps=session.observe_user_text)
        if provider == "qwen_realtime":
            backend = RealtimeBackend(session)
            backend.cloud = MessageSocket()
        else:
            backend = ChatBackend(session, provider)
            backend._history = []
            backend._instructions = "offline"
            backend._events = asyncio.Queue()
            backend._cancelled = set()
            backend._response_sequence = backend._turn_generation = 0
            backend._active_response_id = backend._pending_history = None
            backend._tts_text = None

            async def empty_response(_messages):
                yield _Chunk()

            backend._request = empty_response
        session.backend = backend
        self.sessions.append(session)
        return session, runtime, device

    def launch_cloud(self, session):
        task = asyncio.create_task(session.cloud_output())
        self.tasks.append(task)
        return task

    async def typed_input(self, session, device, text):
        device.receive_event({"type": "text", "text": text})
        device.incoming.put_nowait(None)
        await session.device_input()
        if isinstance(session.backend, ChatBackend):
            drain = session.backend._turn_drain_task
            if drain is not None:
                await asyncio.wait_for(asyncio.shield(drain), 2)

    async def transcription_consumed(self, device, text):
        await device.wait(lambda: any(item.get("type") == "stt" and item.get("text") == text for item in device.json))

    def assert_once(self, session, runtime, text):
        actual = {
            "observations": session.observe_user_text.await_count,
            "control_turn": session.control_turn,
            "fragments": [fragment for _seen, fragment in session.control_fragments],
            "media_cancellations": runtime.media_service.cancel_voice_request.await_count,
            "music_cancellations": runtime.device_music.cancel_voice_request.await_count,
            "memory_refreshes": runtime.memory_service.context.await_count,
            "memory_submissions": runtime.memory_service.submit.call_count,
        }
        self.assertEqual(actual, {"observations": 1, "control_turn": 1, "fragments": [text], "media_cancellations": 1, "music_cancellations": 1, "memory_refreshes": 1, "memory_submissions": 1})

    async def test_chat_typed_input_and_synthetic_transcription_observe_once(self):
        for provider in ("qwen_chat", "deepseek"):
            with self.subTest(provider=provider):
                session, runtime, device = self.session(provider)
                self.launch_cloud(session)
                text = "我喜欢清淡饮食"
                await self.typed_input(session, device, text)
                await self.transcription_consumed(device, text)
                self.assert_once(session, runtime, text)

    async def test_realtime_typed_input_preserves_one_gateway_observation(self):
        session, runtime, device = self.session("qwen_realtime")
        text = "我喜欢清淡饮食"
        await self.typed_input(session, device, text)
        self.assert_once(session, runtime, text)
        items = [item for item in session.backend.cloud.json if item.get("type") == "conversation.item.create"]
        self.assertEqual(items[0]["item"]["content"], [{"type": "input_text", "text": text}])

    async def test_closed_chat_backend_does_not_observe_new_typed_input(self):
        session, runtime, device = self.session("qwen_chat")
        await session.backend.close()
        await self.typed_input(session, device, "关闭后不应观察")
        session.observe_user_text.assert_not_awaited()
        self.assertEqual(session.control_turn, 0)
        self.assertEqual(session.control_fragments, [])
        runtime.memory_service.context.assert_not_awaited()
        runtime.memory_service.submit.assert_not_called()
        runtime.media_service.cancel_voice_request.assert_not_awaited()
        runtime.device_music.cancel_voice_request.assert_not_awaited()

    async def test_verified_realtime_audio_keeps_control_authorization_without_memory(self):
        session, runtime, device = self.session("qwen_realtime")
        session.memory_audio_allowed = False
        session.command_audio_allowed = True
        session.speaking = True
        session.playback_ended_at = time.monotonic()
        text = "打开电脑的计算器"
        self.launch_cloud(session)
        session.backend.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "transcript": text})
        await self.transcription_consumed(device, text)
        self.assertEqual(session.control_turn, 1)
        self.assertEqual(session.last_user_text, text)
        self.assertTrue(session.command_audio_allowed)
        self.assertEqual(session.observe_user_text.await_args.kwargs, {"refresh": False, "source": "verified_audio"})
        runtime.memory_service.submit.assert_not_called()

    async def test_unverified_realtime_audio_cannot_bypass_wake_gate(self):
        session, runtime, _device = self.session("qwen_realtime")
        session.require_wake_phrase = True
        cloud = self.launch_cloud(session)
        session.backend.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "transcript": "打开客厅灯"})
        with self.assertRaises(WakePhraseRejected):
            await asyncio.wait_for(asyncio.shield(cloud), 2)
        session.observe_user_text.assert_not_awaited()
        self.assertEqual(session.control_turn, 0)
        runtime.memory_service.submit.assert_not_called()

    async def test_verified_wake_phrase_observes_realtime_transcript_once(self):
        session, runtime, device = self.session("qwen_realtime")
        session.require_wake_phrase = True
        self.launch_cloud(session)
        text = "你好，小燕子！"
        session.backend.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "transcript": text})
        await self.transcription_consumed(device, text)
        self.assertTrue(session.wake_phrase_verified)
        self.assertEqual(session.observe_user_text.await_count, 1)
        self.assertEqual(session.control_turn, 1)
        runtime.memory_service.submit.assert_called_once()


if __name__ == "__main__":
    unittest.main()

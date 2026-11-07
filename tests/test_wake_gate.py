import asyncio
from pathlib import Path
from types import SimpleNamespace
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from assistant_runtime import NativeDevice
from gateway import CONFIG_KEY, WakePhraseRejected, VoiceSession


class FakeSocket:
    def __init__(self):
        self.sent = []
        self.closed = False

    async def send_json(self, payload):
        self.sent.append(payload)

    async def send_bytes(self, payload):
        self.sent.append(payload)

    async def close(self, **_kwargs):
        self.closed = True


class FakeBackend:
    name = "fake"
    connected = True

    def __init__(self, events=()):
        self.events_list = list(events)
        self.cancel_calls = 0

    async def events(self):
        for event in self.events_list:
            yield event

    async def cancel_response(self):
        self.cancel_calls += 1


def make_session(events=()):
    app = {CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30}}
    session = VoiceSession(app, FakeSocket(), browser=False, raw_pcm=True)
    session.backend = FakeBackend(events)
    session.require_wake_phrase = True
    return session


async def collect_verified(session):
    return [event async for event in session.verified_backend_events()]


class WakeGateTests(unittest.IsolatedAsyncioTestCase):
    async def test_ordinary_first_sentence_is_rejected(self):
        session = make_session([{
            "type": "conversation.item.input_audio_transcription.completed",
            "transcript": "打开客厅灯",
        }])
        with self.assertRaises(WakePhraseRejected):
            await collect_verified(session)

    async def test_complete_phrase_with_punctuation_is_accepted(self):
        session = make_session([{
            "type": "conversation.item.input_audio_transcription.completed",
            "transcript": "你好，小燕子！",
        }, {"type": "response.done", "response": {"id": "r1"}}])
        events = await collect_verified(session)
        self.assertTrue(session.wake_phrase_verified)
        self.assertEqual(events[-1]["type"], "response.done")

    async def test_phrase_split_across_asr_segments_is_accepted(self):
        session = make_session([
            {"type": "conversation.item.input_audio_transcription.completed", "transcript": "你好"},
            {"type": "conversation.item.input_audio_transcription.completed", "transcript": "小燕子"},
        ])
        await collect_verified(session)
        self.assertTrue(session.wake_phrase_verified)

    async def test_pre_asr_output_and_tool_events_are_buffered(self):
        release = asyncio.Event()

        class GatedBackend(FakeBackend):
            async def events(self):
                yield {"type": "response.audio.delta", "delta": "pre-asr"}
                yield {"type": "response.done", "response": {"id": "early"}, "output": [{"type": "function_call", "name": "create_task"}]}
                await release.wait()
                yield {"type": "conversation.item.input_audio_transcription.completed", "transcript": "你好小燕子"}

        session = make_session()
        session.backend = GatedBackend()
        task = asyncio.create_task(collect_verified(session))
        await asyncio.sleep(0)
        self.assertFalse(task.done())
        self.assertEqual(session.socket.sent, [])
        release.set()
        events = await task
        self.assertEqual(events[0]["type"], "response.audio.delta")
        self.assertEqual(events[1]["type"], "response.done")

    async def test_timeout_rejects_without_asr(self):
        class SlowBackend(FakeBackend):
            async def events(self):
                await asyncio.sleep(1)
                yield {"type": "response.done"}

        session = make_session()
        session.backend = SlowBackend()
        with patch("gateway.WAKE_VERIFICATION_SECONDS", 0.01):
            with self.assertRaisesRegex(WakePhraseRejected, "timeout"):
                await collect_verified(session)

    async def test_unverified_tool_is_blocked(self):
        session = make_session()
        with self.assertRaisesRegex(ValueError, "唤醒词尚未确认"):
            await session.execute_tool("create_task", {"title": "不应执行", "idempotency_key": "gate-1"})

    async def test_manual_native_start_bypasses_gate_and_auto_requires_it(self):
        runtime = SimpleNamespace(config={"max_connections": 2}, voice_sessions=set(), voice_lock=asyncio.Lock())
        socket = FakeSocket()
        created = []
        stopped = asyncio.Event()

        class Session:
            def __init__(self, conversation):
                self.session_id = "fake-session"
                self.require_wake_phrase = None
                created.append(self)

            async def run(self):
                await stopped.wait()

        device = NativeDevice(socket, runtime, Session)
        await device.start("auto")
        self.assertTrue(created[-1].require_wake_phrase)
        await device.end_conversation()
        stopped.set()
        await device.start("manual")
        self.assertFalse(created[-1].require_wake_phrase)
        await device.end_conversation()

    async def test_verified_session_continues_without_repeating_phrase(self):
        session = make_session()
        session.wake_phrase_verified = True
        session.backend = FakeBackend([{"type": "response.done", "response": {"id": "next"}}])
        events = await collect_verified(session)
        self.assertEqual(events, [{"type": "response.done", "response": {"id": "next"}}])


if __name__ == "__main__":
    unittest.main()

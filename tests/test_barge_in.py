"""两阶段自然插话协议的异步本地测试，不连接真实云端。"""

import asyncio
import json
import unittest

from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession


class FakeSocket:
    def __init__(self):
        self.json = []
        self.binary = []
        self.closed = False

    async def send_json(self, payload):
        self.json.append(payload)

    async def send_bytes(self, payload):
        self.binary.append(payload)

    async def close(self, **kwargs):
        self.closed = True

    def exception(self):
        return None


class FakeCloud:
    def __init__(self):
        self.closed = False
        self.events = []

    async def send_json(self, payload):
        self.events.append(payload)


class EventCloud(FakeCloud):
    def __init__(self, events):
        super().__init__()
        self.messages = [{"type": "TEXT", "data": event} for event in events]

    def __aiter__(self):
        self._iterator = iter(self.messages)
        return self

    async def __anext__(self):
        try:
            item = next(self._iterator)
        except StopIteration:
            raise StopAsyncIteration
        return type("Message", (), {"type": 1, "data": item["data"]})()


class FakeBackend:
    """按 VoiceSession 的 backend 接口包装 FakeCloud，记录云端下行。"""

    def __init__(self, cloud):
        self.cloud = cloud
        self.name = "qwen_realtime"

    @property
    def connected(self):
        return not self.cloud.closed

    async def events(self):
        async for message in self.cloud:
            yield json.loads(message.data)

    async def cancel_response(self):
        await self.cloud.send_json({"type": "response.cancel"})

    async def send_tool_output(self, call_id, output):
        await self.cloud.send_json({"type": "conversation.item.create", "item": {"type": "function_call_output", "call_id": call_id, "output": output}})

    async def request_response(self):
        await self.cloud.send_json({"type": "response.create"})


class FakeRuntime:
    last_device_state = "idle"
    last_latency = None
    last_usage = None


def make_session():
    app = {
        CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30, "aliyun_voice": "Tina"},
        RUNTIME_KEY: FakeRuntime(),
    }
    socket = FakeSocket()
    session = VoiceSession(app, socket, raw_pcm=True)
    session.backend = FakeBackend(FakeCloud())
    session.current_response = "response-1"
    session.current_playback_id = "playback-1"
    session.playback_response_ids["playback-1"] = "response-1"
    session.speaking = True
    return session, socket


class BargeInTests(unittest.IsolatedAsyncioTestCase):
    async def test_candidate_pause_resume_keeps_pending_chunk(self):
        session, socket = make_session()
        session.playback.put_nowait((session.generation, "playback-1", b"\x01\x00" * 480))
        session.playback.put_nowait((session.generation, "playback-1", None))
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 7, "playback_id": "playback-1", "played_samples": 0})
        task = asyncio.create_task(session.play_output())
        await asyncio.sleep(0.03)
        self.assertEqual(socket.binary, [])
        await session.handle_barge_in({"phase": "resume", "candidate_id": 7, "playback_id": "playback-1", "played_samples": 0})
        async with asyncio.timeout(1):
            while not socket.binary:
                await asyncio.sleep(0.01)
        self.assertEqual(socket.binary[0], b"\x01\x00" * 480)
        task.cancel()
        await asyncio.gather(task, return_exceptions=True)
        acks = [item for item in socket.json if item.get("type") == "barge_in_ack"]
        self.assertEqual([item["phase"] for item in acks], ["candidate", "resume"])

    async def test_confirmed_cancels_after_cloud_done_and_drops_queue(self):
        session, socket = make_session()
        session.current_response = None
        session.completed_responses.add("response-1")
        session.playback.put_nowait((session.generation, "playback-1", b"\x01\x00" * 480))
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 8, "playback_id": "playback-1", "played_samples": 120})
        await session.handle_barge_in({"phase": "confirmed", "candidate_id": 8, "playback_id": "playback-1", "played_samples": 120})
        self.assertEqual(session.playback.qsize(), 0)
        self.assertIn("response-1", session.cancelled_responses)
        self.assertNotIn({"type": "response.cancel"}, session.backend.cloud.events)
        self.assertFalse(any(item.get("type") == "input_audio_buffer.clear" for item in session.backend.cloud.events))
        self.assertIn({"type": "interrupt", "playback_id": "playback-1", "session_id": session.session_id}, socket.json)

    async def test_duplicate_and_stale_candidates_have_no_state_effect(self):
        session, socket = make_session()
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 9, "playback_id": "playback-1", "played_samples": 0})
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 9, "playback_id": "playback-1", "played_samples": 0})
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 10, "playback_id": "old-playback", "played_samples": 0})
        self.assertEqual(session.barge_candidate["candidate_id"], 9)
        self.assertFalse(session.barge_pause.is_set())
        self.assertEqual(len([item for item in socket.json if item.get("type") == "barge_in_ack"]), 2)
        await session._cancel_barge_timeout()

    async def test_candidate_timeout_resumes_and_notifies(self):
        session, socket = make_session()
        await session.handle_barge_in({"phase": "candidate", "candidate_id": 11, "playback_id": "playback-1", "played_samples": 0})
        await asyncio.sleep(1.25)
        self.assertIsNone(session.barge_candidate)
        self.assertTrue(session.barge_pause.is_set())
        self.assertTrue(any(item.get("type") == "barge_in_ack" and item.get("phase") == "resume" and item.get("reason") == "timeout" for item in socket.json))

    async def test_late_delta_after_done_is_discarded(self):
        session, _ = make_session()
        session.speaking = False
        session.backend = FakeBackend(EventCloud([
            '{"type":"response.created","response":{"id":"response-late"}}',
            '{"type":"response.done","response":{"id":"response-late","output":[],"usage":{}}}',
            '{"type":"response.audio.delta","response_id":"response-late","delta":"AQAA"}',
        ]))
        with self.assertRaises(RuntimeError):
            await session.cloud_output()
        self.assertEqual(session.playback.qsize(), 0)
        self.assertIn("response-late", session.completed_responses)

    async def test_confirmation_cancels_active_response_once_without_clearing_input(self):
        session, socket = make_session()
        event = {"phase": "candidate", "candidate_id": 21, "playback_id": "playback-1", "played_samples": 2400}
        await session.handle_barge_in(event)
        confirmed = {**event, "phase": "confirmed", "played_samples": 2880}
        await session.handle_barge_in(confirmed)
        await session.handle_barge_in(confirmed)
        self.assertEqual(session.backend.cloud.events.count({"type": "response.cancel"}), 1)
        self.assertFalse(any(item.get("type") == "input_audio_buffer.clear" for item in session.backend.cloud.events))
        acks = [item for item in socket.json if item.get("phase") == "confirmed"]
        self.assertEqual(len(acks), 1)
        self.assertEqual(acks[0]["played_samples"], 2880)

    async def test_cancelled_response_late_audio_cannot_restart_playback(self):
        session, _ = make_session()
        session.backend = FakeBackend(EventCloud([
            '{"type":"response.audio.delta","response_id":"response-1","delta":"AQAA"}',
            '{"type":"response.done","response":{"id":"response-1","output":[],"usage":{}}}',
        ]))
        event = {"phase": "candidate", "candidate_id": 22, "playback_id": "playback-1", "played_samples": 2880}
        await session.handle_barge_in(event)
        await session.handle_barge_in({**event, "phase": "confirmed"})
        with self.assertRaises(RuntimeError):
            await session.cloud_output()
        self.assertEqual(session.playback.qsize(), 0)
        self.assertFalse(session.speaking)


if __name__ == "__main__":
    unittest.main()

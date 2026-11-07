"""自问自答熔断与 STT/TTS 文本日志的单元测试（假后端/假会话，不接云端）。"""

import asyncio
import sys
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from gateway import CONFIG_KEY, RUNTIME_KEY, SELF_TURN_INTERVAL_MS, SELF_TURN_SPEECH_MS, VoiceSession


class FakeSocket:
    def __init__(self):
        self.sent = []
        self.closed = False

    async def send_json(self, payload):
        self.sent.append(payload)

    async def close(self, **_kwargs):
        self.closed = True


class FakeBackend:
    """按脚本回放云端事件，并记录 response.cancel 调用。"""

    name = "fake"

    def __init__(self, events):
        self._events = list(events)
        self.cancel_calls = 0
        self.connected = True

    async def events(self):
        for event in self._events:
            yield event

    async def cancel_response(self):
        self.cancel_calls += 1


def make_session(events=()):
    app = {CONFIG_KEY: {"session_seconds": 600, "idle_seconds": 30}, RUNTIME_KEY: SimpleNamespace(last_usage=None)}
    socket = FakeSocket()
    session = VoiceSession(app, socket, browser=True)
    session.backend = FakeBackend(events)
    return session, socket


def custom_states(socket):
    return [item["payload"]["state"] for item in socket.sent if item.get("type") == "custom"]


class SelfTurnGuardTests(unittest.IsolatedAsyncioTestCase):
    def test_short_speech_after_playback_is_suspicious_and_long_turn_resets(self):
        session, _ = make_session()
        session.playback_ended_at = 100.0
        session._note_speech_started(100.5, {})
        self.assertTrue(session._note_speech_stopped(100.6, {}))
        self.assertTrue(session.self_turn_pending)
        self.assertLess(session.vad_speech_ms, SELF_TURN_INTERVAL_MS)

        # 真实用户回合：事件间隔够长（含 600ms 静音），重置连续计数。
        session.self_turn_streak = 1
        session.playback_ended_at = 100.0
        session._note_speech_started(101.0, {})
        self.assertFalse(session._note_speech_stopped(102.2, {}))
        self.assertFalse(session.self_turn_pending)
        self.assertEqual(session.self_turn_streak, 0)

        # 短语音但离播放结束超过1.2s：真实回合，不算自触发。
        session.self_turn_streak = 1
        session.playback_ended_at = 100.0
        session._note_speech_started(105.0, {})
        self.assertFalse(session._note_speech_stopped(105.1, {}))
        self.assertEqual(session.self_turn_streak, 0)

    def test_audio_timestamps_measure_real_speech_not_silence(self):
        session, _ = make_session()
        session.playback_ended_at = 100.0
        # 云端给了 audio_start_ms/audio_end_ms：200ms 真实尾音 → 疑似。
        session._note_speech_started(100.5, {"audio_start_ms": 1000})
        self.assertTrue(session._note_speech_stopped(101.5, {"audio_end_ms": 1200}))
        self.assertEqual(session.vad_speech_ms, 200)
        # 同样的短事件间隔，但真实人声 900ms → 真实用户，不取消。
        session.self_turn_streak = 1
        session.playback_ended_at = 100.0
        session._note_speech_started(100.5, {"audio_start_ms": 2000})
        self.assertFalse(session._note_speech_stopped(101.5, {"audio_end_ms": 2900}))
        self.assertEqual(session.vad_speech_ms, 900)
        self.assertEqual(session.self_turn_streak, 0)

    async def test_two_suspicious_responses_cancel_and_trip_guard(self):
        events = [
            {"type": "input_audio_buffer.speech_started"},
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r1"}},
            {"type": "input_audio_buffer.speech_started"},
            {"type": "input_audio_buffer.speech_stopped"},
            {"type": "response.created", "response": {"id": "r2"}},
        ]
        session, socket = make_session(events)
        session.playback_ended_at = time.monotonic()
        with self.assertRaises(RuntimeError):
            await session.cloud_output()
        self.assertEqual(session.backend.cancel_calls, 2)
        self.assertTrue(session.guard_tripped)
        self.assertEqual(session.cancelled_responses, {"r1", "r2"})
        self.assertIsNone(session.current_response)
        # 疑似轮不分配播放ID、不向设备发 TTS，只回到 listening；
        # 相同显示状态合并（speech_started 时上一条已是 listening，不再重发）。
        self.assertEqual(session.response_playback_ids, {})
        self.assertNotIn("tts", [item.get("type") for item in socket.sent])
        self.assertEqual(custom_states(socket).count("listening"), 3)

    async def test_real_user_response_is_not_cancelled(self):
        events = [{"type": "response.created", "response": {"id": "r1"}}]
        session, _ = make_session(events)
        with self.assertRaises(RuntimeError):
            await session.cloud_output()
        self.assertEqual(session.backend.cancel_calls, 0)
        self.assertEqual(session.current_response, "r1")
        self.assertFalse(session.guard_tripped)
        self.assertIn("r1", session.response_playback_ids)

    async def test_tts_and_stt_text_logged_and_truncated(self):
        long_text = "很长的句子" * 100
        events = [
            {"type": "response.audio_transcript.done", "transcript": long_text},
            {"type": "conversation.item.input_audio_transcription.completed", "transcript": "帮我开灯"},
        ]
        session, _ = make_session(events)
        with self.assertLogs("assistant", level="INFO") as captured:
            with self.assertRaises(RuntimeError):
                await session.cloud_output()
        tts_logs = [record.getMessage() for record in captured.records if "tts_sentence" in record.getMessage()]
        stt_logs = [record.getMessage() for record in captured.records if "stt_text" in record.getMessage()]
        self.assertEqual(len(tts_logs), 1)
        self.assertIn(long_text[:200], tts_logs[0])
        self.assertNotIn(long_text[:201], tts_logs[0])
        self.assertEqual(len(stt_logs), 1)
        self.assertIn("帮我开灯", stt_logs[0])

    async def test_watchdog_ends_session_gracefully_when_guard_tripped(self):
        session, socket = make_session()
        session.guard_tripped = True
        await asyncio.wait_for(session.watchdog(), timeout=3)
        idle = [item for item in socket.sent if item.get("type") == "custom" and item["payload"]["state"] == "idle"]
        self.assertEqual(len(idle), 1)
        self.assertIn("自问自答", idle[0]["payload"]["detail"])

    async def test_play_output_records_playback_end_time(self):
        session, socket = make_session()
        session.current_playback_id = "p1"
        session.playback.put_nowait((session.generation, "p1", None))
        task = asyncio.create_task(session.play_output())
        try:
            async with asyncio.timeout(3):
                while session.playback_ended_at is None:
                    await asyncio.sleep(0.01)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        self.assertIsNotNone(session.playback_ended_at)
        self.assertIn("listening", custom_states(socket))
        self.assertTrue(any(item.get("type") == "tts" and item.get("state") == "stop" for item in socket.sent))


if __name__ == "__main__":
    unittest.main()

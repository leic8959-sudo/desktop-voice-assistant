"""固定唤醒回答的门禁、首轮路由及连续接话回归，不连接真实云端。"""

import base64
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

from gateway import CONFIG_KEY, RUNTIME_KEY, RealtimeBackend, VoiceSession, WakePhraseRejected
from src.assistant_app.application.wake_ack import load_wake_ack
from tests.test_wake_gate import FakeSocket


class AckCloud:
    def __init__(self):
        self.items = []

    def __aiter__(self):
        return self

    async def __anext__(self):
        if not self.items:
            raise StopAsyncIteration
        return SimpleNamespace(type=1, data=json.dumps(self.items.pop(0)))


class ScriptedRealtime(RealtimeBackend):
    def __init__(self, session, events):
        super().__init__(session)
        self.wake_ack_pcm = b"\x01\x00" * 480
        self.script = events
        self.cloud = AckCloud()

        async def accept_item(event):
            if event["type"] == "session.update":
                self.cloud.items.append({"type": "session.updated", "session": event["session"]})

        self._send = AsyncMock(side_effect=accept_item)

    async def events(self):
        for event in self.script:
            yield event


def make_voice(text="你好，小燕子。", *, split=False, extra=()):
    voice = VoiceSession({CONFIG_KEY: {}, RUNTIME_KEY: SimpleNamespace(last_usage=None)}, FakeSocket(), raw_pcm=True)
    voice.require_wake_phrase = True
    voice.observe_user_text = AsyncMock()
    voice.refresh_memory_context = AsyncMock()
    voice.finish_unhandled_control = AsyncMock()
    parts = ["你好", "小燕子。"] if split else [text]
    events = [{"type": "input_audio_buffer.speech_started"}, {"type": "input_audio_buffer.speech_stopped"}]
    events.extend({"type": "conversation.item.input_audio_transcription.completed", "transcript": part} for part in parts)
    events.extend(extra)
    voice.backend = ScriptedRealtime(voice, events)
    return voice


class WakeAckTests(unittest.IsolatedAsyncioTestCase):
    async def test_repeated_complete_wake_phrase_uses_cache_once(self):
        voice = make_voice("你好，小燕子。你好，小燕子。")
        await self.run_output(voice)
        self.assertTrue(voice.wake_phrase_verified)
        self.assertEqual([call.args[0]["type"] for call in voice.backend._send.await_args_list], ["session.update"])
        sentences = [item["text"] for item in voice.socket.sent if item.get("type") == "tts" and item.get("state") == "sentence_start"]
        self.assertEqual(sentences, ["我在。"])

    async def test_repeated_wake_with_request_or_partial_tail_keeps_model_path(self):
        for text in ("你好小燕子你好小燕子打开计算器", "你好小燕子你好", "你好小燕子不要回答", "你好小燕子2"):
            voice = make_voice(text)
            events = [event async for event in voice.verified_backend_events()]
            self.assertFalse(any(event.get("cached_wake") for event in events))
            self.assertEqual([call.args[0]["type"] for call in voice.backend._send.await_args_list], ["session.update", "response.create"])

    async def run_output(self, voice):
        # 有限事件流结束按生产合同抛连接结束；断言前收回观察任务。
        with self.assertRaisesRegex(RuntimeError, "云端连接已结束"):
            await voice.cloud_output()
        for task in voice.tasks:
            await task

    async def test_verified_pure_wake_uses_one_cached_response_without_cloud_request(self):
        voice = make_voice()
        await self.run_output(voice)
        self.assertTrue(voice.wake_phrase_verified)
        messages = [call.args[0] for call in voice.backend._send.await_args_list]
        self.assertEqual([message["type"] for message in messages], ["session.update"])
        self.assertIn("设备独立播放", messages[0]["session"]["instructions"])
        self.assertTrue(messages[0]["session"]["turn_detection"]["create_response"])
        self.assertEqual([item["text"] for item in voice.socket.sent if item.get("type") == "tts" and item.get("state") == "sentence_start"], ["我在。"])
        queued = list(voice.playback._queue)
        self.assertEqual(b"".join(item[2] for item in queued if item[2] is not None), b"\x01\x00" * 480)
        self.assertIsNone(queued[-1][2])
        self.assertTrue(voice.response_completed)
        # 继续接话与旧回合隔离；取消后旧缓存不再排队。
        await voice.interrupt(cancel_cloud=False)
        self.assertTrue(voice.playback.empty())

    async def test_wake_with_command_requests_model_once_and_never_injects_ack(self):
        voice = make_voice("你好小燕子，打开计算器")
        events = [event async for event in voice.verified_backend_events()]
        self.assertFalse(any(event.get("cached_wake") for event in events))
        self.assertEqual([call.args[0]["type"] for call in voice.backend._send.await_args_list], ["session.update", "response.create"])

    async def test_split_wake_uses_cache_and_followup_transcript_is_preserved(self):
        voice = make_voice(split=True, extra=[{"type": "input_audio_buffer.speech_started"}, {"type": "conversation.item.input_audio_transcription.completed", "transcript": "打开计算器"}])
        events = [event async for event in voice.verified_backend_events()]
        self.assertEqual(sum(event.get("type") == "response.created" and bool(event.get("cached_wake")) for event in events), 1)
        self.assertEqual(events[-1]["transcript"], "打开计算器")
        self.assertEqual(voice.backend._send.await_count, 1)

    async def test_missing_phrase_never_plays_or_restores_automatic_response(self):
        voice = make_voice("打开计算器")
        with self.assertRaises(WakePhraseRejected):
            await voice.cloud_output()
        voice.backend._send.assert_not_awaited()
        self.assertTrue(voice.playback.empty())

    async def test_cache_absent_keeps_existing_cloud_flow(self):
        voice = make_voice()
        voice.backend.wake_ack_pcm = None
        events = [event async for event in voice.verified_backend_events()]
        self.assertFalse(any(event.get("cached_wake") for event in events))
        voice.backend._send.assert_not_awaited()

    async def test_interrupting_local_ack_never_cancels_nonexistent_cloud_response(self):
        voice = make_voice()
        voice.current_response = voice.wake_ack_response_id = "wake-ack-local"
        voice.current_playback_id = "local-playback"
        voice.speaking = True
        await voice.interrupt()
        voice.backend._send.assert_not_awaited()
        self.assertIsNone(voice.current_response)

    async def test_followup_audio_reaches_device_after_cached_ack(self):
        voice = make_voice(extra=[{"type": "input_audio_buffer.speech_started"}, {"type": "input_audio_buffer.speech_stopped"}, {"type": "response.created", "response": {"id": "followup"}}, {"type": "response.audio.delta", "response_id": "followup", "delta": base64.b64encode(b"\x02\x00" * 480).decode()}, {"type": "response.done", "response": {"id": "followup", "status": "completed", "output": []}}])
        await self.run_output(voice)
        self.assertEqual(voice.current_playback_id, voice.response_playback_ids["followup"])
        self.assertEqual(b"".join(item[2] for item in voice.playback._queue if item[2] is not None), b"\x02\x00" * 480)

    async def test_voice_model_or_hash_mismatch_falls_back(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            pcm = b"\x01\x00" * 480
            metadata = {"model": "model", "voice": "voice", "text": "我在。", "format": "pcm_s16le_24000_mono", "sha256": hashlib.sha256(pcm).hexdigest()}
            (directory / "ack.pcm").write_bytes(pcm)
            (directory / "manifest.json").write_text(json.dumps(metadata), encoding="utf-8")
            config = {"aliyun_model": "model", "aliyun_voice": "voice"}
            self.assertEqual(load_wake_ack(directory, config), pcm)
            self.assertIsNone(load_wake_ack(directory, {**config, "aliyun_voice": "other"}))
            self.assertIsNone(load_wake_ack(directory, {**config, "aliyun_model": "other"}))
            (directory / "ack.pcm").write_bytes(b"\x00\x00")
            with self.assertLogs("assistant", level="ERROR"):
                self.assertIsNone(load_wake_ack(directory, config))

"""端云协商、播放期接话和旧回答隔离；使用假设备和假云，不调用真实服务。"""

import asyncio
import base64
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, Mock

from gateway import CONFIG_KEY, RUNTIME_KEY, REALTIME_TURN_DETECTION, RealtimeBackend, VoiceSession
from tests.test_input_observation import MessageSocket


class NaturalBargeInTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        memory = SimpleNamespace(context=AsyncMock(return_value=[]), submit=Mock())
        runtime = SimpleNamespace(memory_service=memory, media_service=None, device_music=None, connector_registry=SimpleNamespace(manifests=lambda: []), last_usage=None)
        self.device = MessageSocket()
        self.voice = VoiceSession({CONFIG_KEY: {"aliyun_model": "qwen3.8-omni-flash-realtime", "enable_search": False}, RUNTIME_KEY: runtime}, self.device, raw_pcm=True)
        self.voice.natural_barge_in_supported = True
        self.backend = self.voice.backend = RealtimeBackend(self.voice)
        self.backend.cloud = MessageSocket()
        self.backend._send = AsyncMock()

    async def asyncTearDown(self):
        for task in tuple(self.voice.tasks):
            task.cancel()
        await asyncio.gather(*self.voice.tasks, return_exceptions=True)

    def confirm_cloud(self):
        self.backend.confirm_turn_detection({"session": {"turn_detection": {"type": "semantic_vad", "interrupt_response": True}}})

    async def acknowledge(self, **fields):
        self.device.receive_event({"type": "conversation_mode_ack", "session_id": self.voice.session_id, "natural_barge_in": True, "aec_ready": True, **fields})
        self.device.incoming.put_nowait(None)
        await self.voice.device_input()

    def start_old_playback(self, *, completed=False):
        self.voice.current_response = None if completed else "old-response"
        self.voice.current_playback_id = "old-playback"
        self.voice.playback_response_ids["old-playback"] = "old-response"
        self.voice.speaking = True
        self.voice.playback.put_nowait((self.voice.generation, "old-playback", b"old-pcm"))
        if completed:
            self.voice.completed_responses.add("old-response")

    async def test_device_and_model_must_both_support_semantic_mode(self):
        self.assertEqual(self.backend.turn_detection()["type"], "semantic_vad")
        self.voice.natural_barge_in_supported = False
        self.assertEqual(self.backend.turn_detection(), REALTIME_TURN_DETECTION)
        self.voice.natural_barge_in_supported = True
        self.voice.config["aliyun_model"] = "other-realtime-model"
        self.assertEqual(self.backend.turn_detection(), REALTIME_TURN_DETECTION)

    async def test_cloud_confirmation_and_initial_wake_gate_precede_device_enable(self):
        self.voice.require_wake_phrase = True
        await self.voice.enable_natural_barge_in()
        self.confirm_cloud()
        await self.voice.enable_natural_barge_in()
        self.assertEqual(self.device.json, [])
        self.voice.wake_phrase_verified = True
        await self.voice.enable_natural_barge_in()
        self.assertEqual(self.device.json[-1]["type"], "conversation_mode")
        self.assertFalse(self.voice.natural_barge_in_enabled)
        await self.acknowledge()
        self.assertTrue(self.voice.natural_barge_in_enabled)

    async def test_missing_cloud_automatic_cancellation_is_not_accepted(self):
        with self.assertRaisesRegex(RuntimeError, "未确认语义打断"):
            self.backend.confirm_turn_detection({"session": {"turn_detection": {"type": "semantic_vad"}}})
        await self.voice.enable_natural_barge_in()
        self.assertFalse(self.voice.natural_barge_in_requested)

    async def test_wake_verification_confirms_semantic_mode_before_replaying_audio(self):
        self.backend.wake_clip_active = True
        self.backend.wake_continuation.extend(b"\x01\x00" * 160)
        self.backend.cloud.receive_event({"type": "session.updated", "session": {"turn_detection": {"type": "server_vad"}}})

        async def reply(event):
            if event["type"] == "session.update":
                self.backend.cloud.receive_event({"type": "session.updated", "session": {"turn_detection": {**event["session"]["turn_detection"], "interrupt_response": True}}})
            elif event["type"] == "input_audio_buffer.append":
                self.assertTrue(self.backend.natural_vad_confirmed)

        self.backend._send.side_effect = reply
        await self.backend.finish_wake_verification(False)
        self.assertTrue(self.backend.natural_vad_confirmed)
        self.assertFalse(self.backend.wake_clip_active)
        self.assertEqual(self.backend.wake_continuation, b"")
        appends = [call.args[0] for call in self.backend._send.await_args_list if call.args[0]["type"] == "input_audio_buffer.append"]
        self.assertEqual(base64.b64decode(appends[0]["audio"]), b"\x01\x00" * 160)

    async def test_cached_wake_switch_keeps_timing_locked_after_audio_append(self):
        self.backend.wake_clip_active = True
        self.backend.wake_ack_pcm = b"\x01\x00" * 480
        self.backend.current_instructions = "offline wake instructions"

        async def reply(event):
            if event["type"] != "session.update":
                return
            detection = event["session"]["turn_detection"]
            if any(field in detection for field in ("silence_duration_ms", "prefix_padding_ms", "threshold")):
                self.backend.cloud.receive_event({"type": "error", "error": {"type": "invalid_request_error", "param": "session.turn_detection.silence_duration_ms", "message": "turn_detection.silence_duration_ms cannot change after audio append"}})
                return
            self.backend.cloud.receive_event({"type": "session.updated", "session": {**event["session"], "turn_detection": {**detection, "silence_duration_ms": 800, "interrupt_response": True}}})

        self.backend._send.side_effect = reply
        acknowledgment = await self.backend.finish_wake_verification(True)
        self.assertEqual(acknowledgment, b"\x01\x00" * 480)
        self.assertTrue(self.backend.natural_vad_confirmed)
        update = self.backend._send.await_args_list[0].args[0]
        self.assertEqual(update["session"]["turn_detection"], {"type": "semantic_vad", "create_response": True})

    async def test_stale_ack_or_missing_aec_cannot_enable_audio(self):
        self.confirm_cloud()
        await self.voice.enable_natural_barge_in()
        await self.acknowledge(session_id="old-session")
        self.assertFalse(self.voice.natural_barge_in_enabled)
        await self.acknowledge(aec_ready=False)
        self.assertFalse(self.voice.natural_barge_in_enabled)

    async def test_interruption_discards_old_audio_and_preserves_short_followup(self):
        self.confirm_cloud()
        await self.voice.enable_natural_barge_in()
        await self.acknowledge()
        self.start_old_playback()
        previous_epoch = self.voice.accepted_input_epoch
        await self.voice.start_input_turn({"audio_start_ms": 2000})
        self.assertGreater(self.voice.accepted_input_epoch, previous_epoch)
        self.assertTrue(self.voice.playback.empty())
        self.assertIn("old-response", self.voice.cancelled_responses)
        self.assertTrue(any(event.get("type") == "interrupt" and event.get("playback_id") == "old-playback" for event in self.device.json))
        self.assertFalse(any(call.args[0].get("type") == "response.cancel" for call in self.backend._send.await_args_list))
        self.voice.playback_ended_at = self.voice.speech_started_at
        self.assertFalse(self.voice._note_speech_stopped(self.voice.speech_started_at + 0.2, {"audio_end_ms": 2200}))
        self.assertTrue(self.voice.command_audio_allowed)
        await self.voice.observe_user_text("换一个", refresh=False, source="verified_audio")
        self.assertEqual(self.voice.last_user_text, "换一个")

    async def test_completed_cloud_response_still_has_interruptible_local_audio(self):
        self.voice.natural_barge_in_enabled = True
        self.start_old_playback(completed=True)
        await self.voice.start_input_turn({"audio_start_ms": 1000})
        self.assertTrue(self.voice.playback.empty())
        self.assertIn("old-response", self.voice.cancelled_responses)
        self.backend._send.assert_not_awaited()

    async def test_late_old_cloud_audio_and_tools_cannot_restart_playback(self):
        self.voice.natural_barge_in_enabled = True
        self.start_old_playback()
        await self.voice.start_input_turn({"audio_start_ms": 1000})
        self.voice.process_tools = AsyncMock()
        for event in (
            {"type": "response.created", "response": {"id": "old-response"}},
            {"type": "response.audio.delta", "response_id": "old-response", "delta": base64.b64encode(b"\x01\x00" * 480).decode()},
            {"type": "response.done", "response": {"id": "old-response", "status": "completed", "output": [{"type": "function_call", "name": "execute_home_action"}]}},
        ):
            self.backend.cloud.receive_event(event)
        self.backend.cloud.incoming.put_nowait(None)
        with self.assertRaisesRegex(RuntimeError, "云端连接已结束"):
            await self.voice.cloud_output()
        self.assertTrue(self.voice.playback.empty())
        self.assertIsNone(self.voice.current_response)
        self.voice.process_tools.assert_not_awaited()

    async def test_legacy_session_keeps_playback_voice_restriction(self):
        self.start_old_playback()
        await self.voice.start_input_turn({"audio_start_ms": 1000})
        self.voice._note_speech_stopped(self.voice.speech_started_at + 1, {"audio_end_ms": 2000})
        self.assertFalse(self.voice.command_audio_allowed)
        self.assertTrue(any(call.args[0]["type"] == "response.cancel" for call in self.backend._send.await_args_list))

    async def test_manual_recording_never_enables_continuous_playback_capture(self):
        self.confirm_cloud()
        self.voice.manual = True
        await self.voice.enable_natural_barge_in()
        self.assertFalse(self.voice.natural_barge_in_requested)
        self.assertEqual(self.device.json, [])

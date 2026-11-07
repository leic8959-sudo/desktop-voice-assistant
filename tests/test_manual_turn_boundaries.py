"""真实网关输入/云事件生命周期；只用受控队列，无外网、设备和工具动作。"""

import asyncio
import base64
import json
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp

from gateway import CONFIG_KEY, HTTP_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession, WAKE_ACK_CONTEXT
from tests.test_input_observation import MessageSocket
from chat_pipeline import ChatBackend


class ControlledCloud(MessageSocket):
    def __init__(self):
        super().__init__()
        self.append_started = asyncio.Event()
        self.append_release = asyncio.Event()
        self.append_release.set()

    async def send_json(self, event):
        if event["type"] == "input_audio_buffer.append":
            self.append_started.set()
            await self.append_release.wait()
        await super().send_json(event)


class ManualTurnBoundaryTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.config = {"aliyun_api_key": "offline", "aliyun_workspace_id": "offline", "aliyun_voice": "offline", "aliyun_model": "offline", "enable_search": False}
        self.runtime = SimpleNamespace(last_cloud_ready=None, memory_service=None)
        self.device = MessageSocket()
        self.voice = VoiceSession({CONFIG_KEY: self.config, RUNTIME_KEY: self.runtime, STORE_KEY: SimpleNamespace(recall_memory=lambda **_: [])}, self.device, raw_pcm=True)
        self.cloud = ControlledCloud()
        self.backend = self.voice.backend = RealtimeBackend(self.voice)
        self.backend.cloud = self.cloud
        self.voice.manual_input_ready_supported = True
        self.tasks = []

    async def asyncTearDown(self):
        self.cloud.append_release.set()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        await self.backend.close()

    def launch(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    def start(self, manual_id):
        self.device.receive_event({"type": "listen", "state": "start", "mode": "manual", "manual_id": manual_id})

    def audio(self, pcm):
        self.device.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=pcm))

    def stop(self, manual_id, byte_count):
        self.device.receive_event({"type": "listen", "state": "stop", "manual_id": manual_id, "audio_bytes": byte_count})

    async def test_initial_manual_config_requires_disabled_vad_ack(self):
        for settings, accepted in (({}, True), ({"turn_detection": None}, True), ({"turn_detection": {"type": "server_vad"}}, False)):
            self.voice.manual = True
            self.cloud.receive_event({"type": "session.created"})
            self.cloud.receive_event({"type": "session.updated", "session": settings})
            self.voice.app[HTTP_KEY] = SimpleNamespace(ws_connect=AsyncMock(return_value=self.cloud))
            if accepted:
                await self.backend.connect()
            else:
                with self.assertRaisesRegex(RuntimeError, "未确认手动收音"):
                    await self.backend.connect()
            self.assertIsNone(self.cloud.json[-1]["session"]["turn_detection"])

    async def test_mode_ack_and_slow_tail_precede_ready_and_single_commit(self):
        self.launch(self.voice.device_input())
        self.launch(self.voice.cloud_output())
        self.cloud.append_release.clear()
        self.start("turn-a")
        pcm = b"\1\0" * 4800
        self.audio(pcm)
        self.stop("turn-a", len(pcm))
        self.stop("turn-a", len(pcm))
        await self.cloud.wait(lambda: bool(self.cloud.json))
        self.assertEqual([item["type"] for item in self.cloud.json], ["session.update"])
        self.assertFalse(any(item["type"] == "manual_input_ready" for item in self.device.json))
        self.cloud.receive_event({"type": "session.updated", "session": {}})
        await asyncio.wait_for(self.cloud.append_started.wait(), 1)
        self.assertTrue(any(item["type"] == "manual_input_ready" and item["manual_id"] == "turn-a" for item in self.device.json))
        self.assertFalse(any(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json))
        self.cloud.append_release.set()
        await self.cloud.wait(lambda: any(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json))
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "input-a"})
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "input-a"})
        await self.cloud.wait(lambda: any(item["type"] == "response.create" for item in self.cloud.json))
        self.assertEqual([item["type"] for item in self.cloud.json], ["session.update", "input_audio_buffer.clear", "input_audio_buffer.append", "input_audio_buffer.commit", "response.create"])
        self.assertEqual(base64.b64decode(self.cloud.json[2]["audio"]), pcm)
        self.assertEqual(self.voice.latest_input_item_id, "input-a")

    async def test_abort_discards_old_audio_and_stale_stop_before_new_turn(self):
        self.voice.manual = True
        self.launch(self.voice.device_input())
        self.start("old")
        self.audio(b"\1\0" * 320)
        self.device.receive_event({"type": "abort"})
        self.stop("old", 640)
        self.start("new")
        self.audio(b"\2\0" * 4800)
        self.stop("old", 640)
        self.stop("new", 9600)
        await self.cloud.wait(lambda: any(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json))
        self.assertEqual([item["type"] for item in self.cloud.json], ["input_audio_buffer.append", "input_audio_buffer.clear", "input_audio_buffer.append", "input_audio_buffer.commit"])
        self.assertEqual(self.voice.manual_pending_commit["id"], "new")
        self.assertEqual(self.voice.manual_pending_commit["audio_bytes"], 9600)

    async def test_cloud_short_rejection_is_local_but_other_errors_are_fatal(self):
        self.voice.manual = True
        self.launch(self.voice.device_input())
        output = self.launch(self.voice.cloud_output())
        self.start("rejected")
        self.audio(b"\1\0" * 4800)
        self.stop("rejected", 9600)
        await self.cloud.wait(lambda: any(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json))
        self.cloud.receive_event({"type": "error", "error": {"type": "invalid_request_error", "message": "Error committing input audio buffer: buffer too small, or have no audio."}})
        await self.device.wait(lambda: any(item.get("status") == "请重新录音" for item in self.device.json))
        self.assertFalse(output.done())
        self.assertIsNone(self.voice.manual_pending_commit)
        self.assertFalse(any(item["type"] == "response.create" for item in self.cloud.json))
        self.cloud.receive_event({"type": "error", "error": {"type": "invalid_request_error", "code": "authentication_failed", "message": "other failure"}})
        with self.assertRaisesRegex(RuntimeError, "authentication_failed"):
            await asyncio.wait_for(output, 1)

    async def test_cache_context_ack_gates_followup_and_preserves_input_item(self):
        self.backend.wake_ack_pcm = b"\1\0" * 480
        self.backend.wake_clip_active = True
        self.backend.wake_clip_boundary = ("wake", 6400, "silence")
        self.backend.wake_continuation.extend(b"\2\0" * 480)
        finishing = self.launch(self.backend.finish_wake_verification(True))
        await self.cloud.wait(lambda: bool(self.cloud.json))
        instructions = self.cloud.json[0]["session"]["instructions"]
        self.assertIn("设备独立播放", instructions)
        live = self.launch(self.backend.send_user_audio(b"\3\0" * 480))
        self.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "item_id": "followup-item", "transcript": "周杰伦的晴天"})
        self.assertFalse(finishing.done())
        self.assertFalse(live.done())
        self.cloud.receive_event({"type": "session.updated", "session": {"instructions": instructions}})
        await asyncio.wait_for(asyncio.gather(finishing, live), 1)
        chunks = [base64.b64decode(item["audio"]) for item in self.cloud.json if item["type"] == "input_audio_buffer.append"]
        self.assertEqual(chunks, [b"\2\0" * 480, b"\3\0" * 480])
        events = self.backend.events()
        self.assertEqual((await anext(events))["item_id"], "followup-item")
        await events.aclose()
        await self.backend.update_instructions("refreshed memory")
        self.assertEqual(self.backend.current_instructions, "refreshed memory" + WAKE_ACK_CONTEXT)
        self.assertFalse(any(item["type"] in {"conversation.item.create", "response.create"} for item in self.cloud.json))

    async def test_manual_start_clears_previous_control_and_response_scope(self):
        self.voice.manual = True
        self.voice.last_user_text = "old control"
        self.voice.control_plan = {"status": "execute", "actions": ["old"]}
        self.voice.control_results = [{"old": True}]
        self.voice.current_response = "old-response"
        self.voice.response_completed = True
        old_turn = self.voice.input_turn_serial
        await self.voice.begin_manual_turn({"manual_id": "fresh"})
        self.assertEqual(self.voice.input_turn_serial, old_turn + 1)
        self.assertEqual(self.voice.last_user_text, "")
        self.assertIsNone(self.voice.control_plan)
        self.assertIsNone(self.voice.control_results)
        self.assertIsNone(self.voice.current_response)
        self.assertFalse(self.voice.response_completed)
        self.assertTrue(self.voice.command_audio_allowed)
        self.assertIn("old-response", self.voice.cancelled_responses)
        self.assertTrue(any(item["type"] == "response.cancel" for item in self.cloud.json))

    async def test_native_manual_chat_provider_fails_before_connect_or_ready(self):
        self.voice.manual = True
        self.voice.config["llm_provider"] = "qwen_chat"
        self.voice.app[HTTP_KEY] = SimpleNamespace(ws_connect=AsyncMock(side_effect=AssertionError("must not connect")))
        with self.assertRaisesRegex(RuntimeError, "不支持按键手动录音"):
            await self.voice.connect()
        self.voice.app[HTTP_KEY].ws_connect.assert_not_awaited()
        self.assertFalse(any(item["type"] == "manual_input_ready" for item in self.device.json))
        self.voice.manual = False
        self.voice.backend = ChatBackend(self.voice, "qwen_chat")
        self.start("chat-rejected")
        with self.assertRaisesRegex(RuntimeError, "不支持按键手动录音"):
            await self.voice.device_input()
        self.assertIsNone(self.voice.manual_pending_commit)
        self.assertFalse(any(item["type"] == "manual_input_ready" for item in self.device.json))

    async def test_previous_instructions_ack_cannot_confirm_new_manual_mode(self):
        self.launch(self.voice.device_input())
        self.launch(self.voice.cloud_output())
        await self.backend.update_instructions("previous context")
        self.start("wait-own-mode")
        self.audio(b"\1\0" * 4800)
        await self.cloud.wait(lambda: len(self.cloud.json) == 2)
        self.cloud.receive_event({"type": "session.updated", "session": {"instructions": "previous context"}})
        await asyncio.sleep(0)
        await asyncio.sleep(0)
        self.assertFalse(any(item["type"] == "manual_input_ready" for item in self.device.json))
        self.assertFalse(any(item["type"] == "input_audio_buffer.append" for item in self.cloud.json))
        self.cloud.receive_event({"type": "session.updated", "session": {}})
        await self.cloud.wait(lambda: any(item["type"] == "input_audio_buffer.append" for item in self.cloud.json))

    async def test_old_commit_and_unattributed_error_cannot_claim_next_pending_turn(self):
        self.voice.manual = True
        self.launch(self.voice.device_input())
        output = self.launch(self.voice.cloud_output())
        self.start("first")
        self.audio(b"\1\0" * 4800)
        self.stop("first", 9600)
        await self.cloud.wait(lambda: any(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json))
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "first-item"})
        self.cloud.receive_event({"type": "response.created", "response": {"id": "first-response"}})
        await self.cloud.wait(lambda: any(item["type"] == "response.create" for item in self.cloud.json))
        self.start("second")
        self.audio(b"\2\0" * 4800)
        self.stop("second", 9600)
        await self.cloud.wait(lambda: sum(item["type"] == "input_audio_buffer.commit" for item in self.cloud.json) == 2)
        pending = self.voice.manual_pending_commit
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "first-item"})
        self.cloud.receive_event({"type": "error", "error": {"type": "invalid_request_error", "message": "Error committing input audio buffer: buffer too small, or have no audio."}})
        with self.assertRaisesRegex(RuntimeError, "云端接口返回错误"):
            await asyncio.wait_for(output, 1)
        self.assertIs(self.voice.manual_pending_commit, pending)
        self.assertEqual(pending["id"], "second")
        self.assertEqual(sum(item["type"] == "response.create" for item in self.cloud.json), 1)

    async def test_cancel_while_cache_context_ack_pending_never_flushes_followup(self):
        self.backend.wake_ack_pcm = b"\1\0" * 480
        self.backend.wake_clip_active = True
        self.backend.wake_continuation.extend(b"\2\0" * 480)
        finishing = self.launch(self.backend.finish_wake_verification(True))
        await self.cloud.wait(lambda: bool(self.cloud.json))
        finishing.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await finishing
        self.assertFalse(any(item["type"] == "input_audio_buffer.append" for item in self.cloud.json))
        self.assertIsNone(self.backend.wake_ack_pcm)

    async def test_abort_with_unknown_commit_ends_input_before_next_manual_ready(self):
        self.voice.manual = True
        self.start("unconfirmed")
        self.audio(b"\1\0" * 4800)
        self.stop("unconfirmed", 9600)
        self.device.receive_event({"type": "abort"})
        self.start("must-not-start")
        with self.assertRaisesRegex(RuntimeError, "提交尚未确认"):
            await self.voice.device_input()
        self.assertIsNone(self.voice.manual_pending_commit)
        self.assertIsNone(self.voice.manual_turn)
        self.assertFalse(self.voice.uploading)
        self.assertFalse(any(item.get("manual_id") == "must-not-start" for item in self.device.json))

    async def test_abort_invalidates_pending_before_output_supervisor_cancels(self):
        self.voice.manual = True
        output = self.launch(self.voice.cloud_output())
        self.start("abort-window")
        self.audio(b"\1\0" * 4800)
        self.stop("abort-window", 9600)
        self.device.receive_event({"type": "abort"})
        with self.assertRaisesRegex(RuntimeError, "提交尚未确认"):
            await self.voice.device_input()
        self.assertFalse(output.done())
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "late-after-abort"})
        self.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "transcript": ""})
        await self.device.wait(lambda: any(item["type"] == "stt" for item in self.device.json))
        self.assertFalse(any(item["type"] == "response.create" for item in self.cloud.json))

    async def test_long_recording_stop_refreshes_idle_and_commit_timeout_invalidates_pending(self):
        self.voice.manual = True
        self.voice.created = 100
        with patch("gateway.time.monotonic", return_value=100):
            await self.voice.begin_manual_turn({"manual_id": "long"})
        self.voice.manual_turn["audio_bytes"] = 16000 * 2 * 29
        with patch("gateway.time.monotonic", return_value=129):
            await self.voice.end_manual_turn({"manual_id": "long", "audio_bytes": 16000 * 2 * 29})
        self.assertEqual(self.voice.last_activity, 129)
        self.assertEqual(self.voice.manual_pending_commit["committed_at"], 129)

        class StopAfterTick(Exception):
            pass

        ticks = 0

        async def one_tick(_):
            nonlocal ticks
            ticks += 1
            if ticks > 1:
                raise StopAfterTick

        with patch("gateway.time.monotonic", return_value=131), patch("gateway.asyncio.sleep", side_effect=one_tick):
            with self.assertRaises(StopAfterTick):
                await self.voice.watchdog()
        with patch("gateway.time.monotonic", return_value=140), patch("gateway.asyncio.sleep", new=AsyncMock()):
            with self.assertRaisesRegex(RuntimeError, "提交确认超时"):
                await self.voice.watchdog()
        self.assertIsNone(self.voice.manual_pending_commit)
        self.assertIsNone(self.voice.manual_turn)
        self.assertFalse(self.voice.uploading)
        self.launch(self.voice.cloud_output())
        self.cloud.receive_event({"type": "input_audio_buffer.committed", "item_id": "late-after-timeout"})
        self.cloud.receive_event({"type": "conversation.item.input_audio_transcription.completed", "transcript": ""})
        await self.device.wait(lambda: any(item["type"] == "stt" for item in self.device.json))
        self.assertFalse(any(item["type"] == "response.create" for item in self.cloud.json))

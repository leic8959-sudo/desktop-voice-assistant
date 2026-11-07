"""有限首句的提交、完整口令门禁、接话保留与取消边界；不接真实设备或云端。"""

import asyncio
import base64
import json
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp

from gateway import CONFIG_KEY, HTTP_KEY, RUNTIME_KEY, LocalToolRejectedError, RealtimeBackend, VoiceSession, WAKE_CONTINUATION_MAX_BYTES, WakePhraseRejected
from assistant_runtime import ConversationSocket, NativeDevice
from tests.test_wake_gate import FakeSocket


class Cloud:
    def __init__(self, events=()):
        self.items = list(events)
        self.closed = False

    def __aiter__(self):
        return self

    async def __anext__(self):
        if not self.items:
            raise StopAsyncIteration
        return SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(self.items.pop(0)))


def make_voice(transcript="你好小燕子", *, reason="silence"):
    config = {"aliyun_api_key": "fixture-key", "aliyun_workspace_id": "fixture-space", "aliyun_model": "fixture-model", "aliyun_voice": "fixture-voice", "enable_search": False}
    runtime = SimpleNamespace(last_usage=None, memory_service=SimpleNamespace(context=AsyncMock(return_value={})))
    voice = VoiceSession({CONFIG_KEY: config, RUNTIME_KEY: runtime}, FakeSocket(), raw_pcm=True)
    voice.require_wake_phrase = voice.wake_clip_supported = True
    voice.wake_id = "wake-test"
    voice.active_tool_schemas = lambda: []
    voice.observe_user_text = AsyncMock()
    voice.refresh_memory_context = AsyncMock()
    voice.finish_unhandled_control = AsyncMock()
    backend = voice.backend = RealtimeBackend(voice)
    backend.wake_clip_active = True
    backend.wake_ack_pcm = b"\x01\x00" * 480
    backend._send = AsyncMock()
    backend.cloud = Cloud([{"type": "input_audio_buffer.committed", "item_id": "input-test"}, {"type": "conversation.item.input_audio_transcription.completed", "item_id": "input-test", "transcript": transcript}])
    async def accept_item(event):
        if event["type"] == "session.update":
            backend.cloud.items.append({"type": "session.updated", "session": event["session"]})

    backend._send.side_effect = accept_item
    return voice, backend


class WakeClipTests(unittest.IsolatedAsyncioTestCase):
    async def test_input_ready_upload_precedes_slow_cloud_connect_and_commit_keeps_order(self):
        connect_started, release_connect, first_audio = asyncio.Event(), asyncio.Event(), asyncio.Event()

        class SlowCloud:
            closed = False

            def __init__(self):
                self.events = asyncio.Queue()
                self.events.put_nowait({"type": "session.created"})
                self.audio = []
                self.commits = 0

            def __aiter__(self):
                return self

            async def __anext__(self):
                event = await self.events.get()
                if event is None:
                    raise StopAsyncIteration
                return SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(event))

            async def send_json(self, event):
                if event["type"] == "session.update":
                    self.events.put_nowait({"type": "session.updated", "session": event["session"]})
                elif event["type"] == "input_audio_buffer.append":
                    self.audio.append(base64.b64decode(event["audio"]))
                elif event["type"] == "input_audio_buffer.commit":
                    self.commits += 1
                    self.events.put_nowait({"type": "input_audio_buffer.committed", "item_id": "early-input"})
                    self.events.put_nowait({"type": "conversation.item.input_audio_transcription.completed", "item_id": "early-input", "transcript": "你好小燕子"})

            async def close(self):
                self.closed = True
                self.events.put_nowait(None)

        class Socket(FakeSocket):
            async def send_bytes(self, payload):
                await super().send_bytes(payload)
                first_audio.set()

        cloud = SlowCloud()

        async def slow_connect(*args, **kwargs):
            connect_started.set()
            await release_connect.wait()
            return cloud

        voice, _ = make_voice()
        config = {**voice.config, "llm_provider": "qwen_realtime", "session_seconds": 600, "idle_seconds": 600}
        runtime = SimpleNamespace(config=config, voice_lock=asyncio.Lock(), voice_sessions=set(), last_cloud_ready=None, memory_service=SimpleNamespace(context=AsyncMock(return_value={}), submit=lambda *args, **kwargs: None))
        app = {CONFIG_KEY: config, RUNTIME_KEY: runtime, HTTP_KEY: SimpleNamespace(ws_connect=slow_connect)}
        socket = Socket()
        device = NativeDevice(socket, runtime, lambda proxy: VoiceSession(app, proxy, raw_pcm=True))

        async def message(payload):
            await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(payload)))

        with patch("gateway.load_wake_ack", return_value=b"\x01\x00" * 960):
            try:
                await message({"type": "hello", "features": {"wake_clip_v1": True, "wake_input_ready_v1": True}})
                await message({"type": "listen", "state": "detect", "wake_id": "early-wake"})
                await message({"type": "listen", "state": "start", "mode": "realtime"})
                await asyncio.wait_for(connect_started.wait(), 1)
                self.assertIn({"type": "wake_input_ready", "wake_id": "early-wake"}, socket.sent)
                prefix, suffix = b"\x02\x00" * 1920, b"\x03\x00" * 1280
                for pcm in (prefix, suffix):
                    await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=pcm))
                await message({"type": "wake_clip_end", "wake_id": "early-wake", "audio_bytes": len(prefix) + len(suffix), "reason": "silence"})
                self.assertEqual(device.conversation.pending_audio_bytes, len(prefix) + len(suffix))
                self.assertIsNone(runtime.last_cloud_ready)
                self.assertIsNone(device._session.wake_deadline)
                self.assertFalse(device._session.wake_phrase_verified)
                self.assertEqual(cloud.commits, 0)
                release_connect.set()
                await asyncio.wait_for(first_audio.wait(), 2)
                self.assertEqual(cloud.audio[:2], [prefix, suffix])
                self.assertEqual(cloud.commits, 1)
                self.assertTrue(device._session.wake_phrase_verified)
                self.assertEqual(device.conversation.pending_audio_bytes, 0)
            finally:
                release_connect.set()
                await device.end_conversation()
        self.assertTrue(cloud.closed)

    async def test_early_queue_rejects_message_byte_and_time_limits(self):
        def socket():
            result = ConversationSocket(SimpleNamespace())
            result.early_wake_input = True
            return result

        message = lambda count: SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=b"\0" * count)
        queue = socket()
        with self.assertRaises(ValueError):
            queue.feed(message(3842))
        for _ in range(66):
            queue.feed(message(3840))
        with self.assertRaises(ValueError):
            queue.feed(message(3840))
        queue = socket()
        for _ in range(150):
            queue.feed(message(2))
        with self.assertRaises(ValueError):
            queue.feed(message(2))
        queue = socket()
        queue.created_at -= 4.1
        with self.assertRaises(ValueError):
            queue.feed(message(2))

    async def test_early_connect_deadline_fires_without_further_pcm(self):
        voice, _ = make_voice()
        socket = ConversationSocket(SimpleNamespace())
        socket.early_wake_input = True
        socket.created_at -= 3.95
        voice.socket = socket
        voice.backend = None
        entered = asyncio.Event()

        async def blocked_connect():
            entered.set()
            await asyncio.Event().wait()

        voice.connect = blocked_connect
        with self.assertLogs("assistant", level="INFO") as logs:
            await asyncio.wait_for(voice.run(), 0.5)
        self.assertTrue(entered.is_set())
        self.assertTrue(voice.closed)
        self.assertIn("early_input_timeout", " ".join(logs.output))

    async def test_early_input_does_not_fall_back_to_incompatible_chat_backend(self):
        voice, _ = make_voice()
        voice.socket.early_wake_input = True
        with patch("gateway.provider_chain", return_value=["deepseek"]):
            with self.assertRaisesRegex(WakePhraseRejected, "unsupported_early_backend"):
                await voice.connect()

    async def test_buffer_overflow_closes_conversation_and_keeps_device_connection(self):
        entered = asyncio.Event()
        runtime = SimpleNamespace(config={"llm_provider": "qwen_realtime"}, voice_lock=asyncio.Lock(), voice_sessions=set())

        class Session:
            def __init__(self, proxy):
                self.config = runtime.config
                self.session_id = "bounded-input"
                self.browser = False
                self.closed = False
                self.wake_id = None
                self.proxy = proxy

            async def run(self):
                entered.set()
                await asyncio.Event().wait()

        socket = FakeSocket()
        device = NativeDevice(socket, runtime, Session)
        device.wake_clip_supported = device.wake_input_ready_supported = True
        device.last_wake_detected_at = asyncio.get_running_loop().time()
        device.last_wake_id = "bounded-wake"
        await device.start("realtime")
        await asyncio.wait_for(entered.wait(), 1)
        binary = SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=b"\0" * 3840)
        for _ in range(66):
            await device.handle(binary)
        with self.assertLogs("assistant", level="ERROR"):
            await device.handle(binary)
        self.assertTrue(device.conversation.closed)
        self.assertEqual(device.conversation.pending_audio_bytes, 0)
        self.assertIsNone(device.task)
        self.assertFalse(socket.closed)

    async def test_actual_native_entry_negotiates_and_delivers_cached_pcm(self):
        class StreamingCloud(Cloud):
            def __init__(self):
                self.queue = asyncio.Queue()
                self.queue.put_nowait({"type": "session.created"})
                self.closed = False
                self.commits = 0
                self.settings = None

            async def __anext__(self):
                event = await self.queue.get()
                if event is None:
                    raise StopAsyncIteration
                return SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(event))

            async def send_json(self, event):
                if event["type"] == "session.update":
                    self.settings = event["session"]
                    self.queue.put_nowait({"type": "session.updated", "session": event["session"]})
                elif event["type"] == "input_audio_buffer.commit":
                    self.commits += 1
                    self.queue.put_nowait({"type": "input_audio_buffer.committed", "item_id": "native-input"})
                    self.queue.put_nowait({"type": "conversation.item.input_audio_transcription.completed", "item_id": "native-input", "transcript": "你好小燕子"})

            async def close(self):
                self.closed = True
                self.queue.put_nowait(None)

        class DeviceSocket(FakeSocket):
            def __init__(self):
                super().__init__()
                self.audio_sent = asyncio.Event()

            async def send_bytes(self, data):
                await super().send_bytes(data)
                self.audio_sent.set()

        voice, _ = make_voice()
        config = {**voice.config, "llm_provider": "qwen_realtime", "session_seconds": 600, "idle_seconds": 600}
        runtime = SimpleNamespace(config=config, voice_lock=asyncio.Lock(), voice_sessions=set(), memory_service=SimpleNamespace(context=AsyncMock(return_value={}), submit=lambda *args, **kwargs: None))
        cloud = StreamingCloud()
        app = {CONFIG_KEY: config, RUNTIME_KEY: runtime, HTTP_KEY: SimpleNamespace(ws_connect=AsyncMock(return_value=cloud))}
        socket = DeviceSocket()
        device = NativeDevice(socket, runtime, lambda proxy: VoiceSession(app, proxy, raw_pcm=True))

        async def message(payload):
            await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(payload)))

        with patch("gateway.load_wake_ack", return_value=b"\x01\x00" * 960):
            try:
                await message({"type": "hello", "features": {"wake_clip_v1": True}})
                await message({"type": "listen", "state": "detect", "wake_id": "native-wake"})
                await message({"type": "listen", "state": "start", "mode": "realtime"})
                active = device._session
                await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=b"\x02\x00" * 3200))
                await message({"type": "wake_clip_end", "wake_id": "native-wake", "audio_bytes": 6400, "reason": "silence"})
                await asyncio.wait_for(socket.audio_sent.wait(), timeout=2)
                self.assertEqual(cloud.commits, 1)
                self.assertTrue(active.wake_phrase_verified)
                self.assertIsNotNone(active.wake_ack_response_id)
                self.assertEqual(active.wake_id, "native-wake")
                self.assertTrue(any(item.get("type") == "metric" and item.get("wake_to_first_audio_ms") is not None for item in socket.sent if isinstance(item, dict)))
            finally:
                await device.end_conversation()
        self.assertTrue(cloud.closed)
        self.assertFalse(runtime.voice_sessions)

    async def submit(self, voice, backend, reason="silence"):
        audio = b"\x02\x00" * 3200
        await backend.send_user_audio(audio)
        boundary = {"wake_id": voice.wake_id, "audio_bytes": len(audio), "reason": reason}
        await backend.end_wake_clip(boundary)
        return boundary

    async def output(self, voice):
        with self.assertRaisesRegex(RuntimeError, "云端连接已结束"):
            await voice.cloud_output()
        for task in voice.tasks:
            await task

    async def test_first_clip_has_no_model_request_until_explicit_boundary(self):
        voice, backend = make_voice()
        await backend.send_user_audio(b"\x00\x00" * 3200)
        self.assertEqual([call.args[0]["type"] for call in backend._send.await_args_list], ["input_audio_buffer.append"])
        await backend.end_wake_clip({"wake_id": voice.wake_id, "audio_bytes": 6400, "reason": "silence"})
        self.assertEqual(backend._send.await_args.args[0]["type"], "input_audio_buffer.commit")
        await self.output(voice)
        self.assertEqual(voice.speech_end, backend.wake_clip_committed_at)
        types = [call.args[0]["type"] for call in backend._send.await_args_list]
        self.assertNotIn("response.create", types)
        self.assertTrue(voice.wake_phrase_verified)
        self.assertEqual([item["text"] for item in voice.socket.sent if item.get("state") == "sentence_start"], ["我在。"])

    async def test_inline_command_preserved_and_waiting_audio_replayed_in_order(self):
        voice, backend = make_voice("你好小燕子，打开计算器")
        await self.submit(voice, backend)
        continuation = b"\x03\x00" * 900
        await backend.send_user_audio(continuation)
        self.assertEqual(bytes(backend.wake_continuation), continuation)
        await self.output(voice)
        voice.observe_user_text.assert_awaited_once_with("你好小燕子，打开计算器", refresh=False, source="verified_audio")
        payloads = [call.args[0] for call in backend._send.await_args_list]
        self.assertEqual([item["type"] for item in payloads], ["input_audio_buffer.append", "input_audio_buffer.commit", "session.update", "response.create", "input_audio_buffer.append"])
        self.assertEqual(base64.b64decode(payloads[-1]["audio"]), continuation)
        self.assertEqual(bytes(backend.wake_continuation), b"")
        self.assertFalse(backend.wake_clip_active)
        self.assertFalse(any(item.get("state") == "sentence_start" for item in voice.socket.sent))

    async def test_wrong_phrase_never_restores_vad_or_replays_followup(self):
        voice, backend = make_voice("打开计算器")
        await self.submit(voice, backend)
        await backend.send_user_audio(b"\x03\x00" * 480)
        with self.assertRaises(WakePhraseRejected):
            await voice.cloud_output()
        self.assertEqual(backend._send.await_count, 2)
        voice.observe_user_text.assert_not_awaited()
        self.assertTrue(voice.playback.empty())

    async def test_finite_final_empty_or_partial_phrase_does_not_wait_for_more_audio(self):
        for transcript, reason in (("", "empty_transcript"), ("。", "empty_transcript"), ("你好", "phrase_missing")):
            voice, backend = make_voice(transcript)
            await self.submit(voice, backend)
            class WaitingCloud:
                def __init__(self, source):
                    self.source = source

                def __aiter__(self):
                    return self

                async def __anext__(self):
                    if self.source.items:
                        return await self.source.__anext__()
                    await asyncio.Event().wait()

            # 最终结果之后云连接仍开放，避免测试只靠流结束来拒绝。
            backend.cloud = WaitingCloud(backend.cloud)
            with self.assertRaisesRegex(WakePhraseRejected, reason):
                await asyncio.wait_for(voice.cloud_output(), timeout=0.5)
            self.assertFalse(voice.wake_phrase_verified)
            voice.observe_user_text.assert_not_awaited()
            self.assertTrue(voice.playback.empty())
            self.assertEqual(backend._send.await_count, 2)

    async def test_buffered_clip_keeps_turn_semantics_without_replaying_display_states(self):
        voice, backend = make_voice()
        await self.submit(voice, backend)
        voice.state = AsyncMock()
        await self.output(voice)
        self.assertEqual(voice.control_turn, 1)
        states = [call.args[0] for call in voice.state.await_args_list]
        self.assertEqual(states, ["speaking"])
        self.assertTrue(voice.wake_phrase_verified)

    async def test_device_playback_timing_is_optional_bounded_and_logged_without_extra_fields(self):
        runtime = SimpleNamespace(config={})
        device = NativeDevice(FakeSocket(), runtime, lambda _: None)
        payload = {"type": "audio_stats", "frames": 56, "underruns": 0, "overflows": 0, "elapsed_ms": 1120, "first_write_after_tts_ms": 95, "first_write_after_receive_ms": 44, "first_display_ms": 10, "unrelated": "private"}
        with self.assertLogs("assistant", level="INFO") as logs:
            await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps(payload)))
        self.assertIn('"first_write_after_receive_ms": 44', logs.output[0])
        self.assertNotIn("private", logs.output[0])
        with self.assertLogs("assistant", level="INFO") as logs:
            await device.handle(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps({**payload, "first_write_after_tts_ms": True, "first_write_after_receive_ms": 60001, "first_display_ms": "private"})))
        self.assertNotIn("first_", logs.output[0])

    async def test_boundary_id_size_type_and_reason_must_match(self):
        for change in ({"wake_id": "old-wake"}, {"audio_bytes": 6402}, {"audio_bytes": True}, {"reason": "unknown"}):
            voice, backend = make_voice()
            await backend.send_user_audio(b"\x01\x00" * 3200)
            with self.assertRaises(ValueError):
                await backend.end_wake_clip({"wake_id": voice.wake_id, "audio_bytes": 6400, "reason": "silence", **change})
            self.assertIsNone(backend.wake_clip_boundary)
            self.assertEqual(backend._send.await_count, 1)

    async def test_duplicate_boundary_commits_once_and_conflicting_one_rejected(self):
        voice, backend = make_voice()
        boundary = await self.submit(voice, backend)
        await backend.end_wake_clip(boundary)
        self.assertEqual(backend._send.await_count, 2)
        with self.assertRaises(ValueError):
            await backend.end_wake_clip({**boundary, "audio_bytes": 6402})

    async def test_transcript_must_belong_to_explicitly_committed_clip(self):
        for premature in (True, False):
            voice, backend = make_voice()
            if not premature:
                await self.submit(voice, backend)
            backend.cloud = Cloud(([{"type": "input_audio_buffer.committed", "item_id": "actual"}] if not premature else []) + [{"type": "conversation.item.input_audio_transcription.completed", "item_id": "other", "transcript": "你好小燕子打开计算器"}])
            with self.assertRaises(WakePhraseRejected):
                await voice.cloud_output()
            voice.observe_user_text.assert_not_awaited()
            self.assertFalse(voice.wake_phrase_verified)

    async def test_long_first_utterance_never_observed_or_executed_as_command(self):
        voice, backend = make_voice("你好小燕子打开计算器")
        await self.submit(voice, backend, "limit")
        await self.output(voice)
        voice.observe_user_text.assert_not_awaited()
        with self.assertRaises(LocalToolRejectedError):
            await voice.execute_tool("desktop_open_app", {"app": "calculator"})
        requests = [call.args[0] for call in backend._send.await_args_list if call.args[0]["type"] == "response.create"]
        self.assertEqual(len(requests), 1)
        self.assertEqual(requests[0]["response"]["tools"], [])
        self.assertIn("首句太长", requests[0]["response"]["instructions"])

    async def test_followup_audio_cannot_overtake_buffer_during_restore(self):
        voice, backend = make_voice("你好小燕子打开计算器")
        await self.submit(voice, backend)
        await backend.send_user_audio(b"\x03\x00" * 480)
        entered, release = asyncio.Event(), asyncio.Event()
        sent = []

        async def send(payload):
            sent.append(payload)
            if payload["type"] == "session.update":
                entered.set()
                await release.wait()

        backend._send = send
        restore = asyncio.create_task(backend.finish_wake_verification(False))
        await entered.wait()
        live = asyncio.create_task(backend.send_user_audio(b"\x04\x00" * 480))
        await asyncio.sleep(0)
        self.assertFalse(live.done())
        release.set()
        await asyncio.gather(restore, live)
        chunks = [base64.b64decode(item["audio"]) for item in sent if item["type"] == "input_audio_buffer.append"]
        self.assertEqual(chunks, [b"\x03\x00" * 480, b"\x04\x00" * 480])

    async def test_waiting_buffer_is_bounded_and_abort_cannot_commit_stale_clip(self):
        voice, backend = make_voice()
        await self.submit(voice, backend)
        await backend.send_user_audio(b"\0" * WAKE_CONTINUATION_MAX_BYTES)
        with self.assertRaises(ValueError):
            await backend.send_user_audio(b"\0\0")
        self.assertEqual(len(backend.wake_continuation), WAKE_CONTINUATION_MAX_BYTES)
        with self.assertRaises(WakePhraseRejected):
            await backend.clear_audio()

    async def test_connect_negotiates_manual_finite_clip_without_cache(self):
        for effective in ({"turn_detection": None}, {}):
            voice, backend = make_voice()
            cloud = Cloud([{"type": "session.created"}, {"type": "session.updated", "session": effective}])
            voice.app[HTTP_KEY] = SimpleNamespace(ws_connect=AsyncMock(return_value=cloud))
            backend.wake_ack_pcm = None
            with patch("gateway.load_wake_ack", return_value=None):
                await backend.connect()
            self.assertTrue(backend.wake_clip_active)
            settings = backend._send.await_args.args[0]
            self.assertIsNone(settings["session"]["turn_detection"])

    async def test_connect_requires_server_ack_and_legacy_peer_keeps_vad(self):
        for supported in (True, False):
            voice, backend = make_voice()
            voice.wake_clip_supported = supported
            cloud = Cloud([{"type": "session.created"}, {"type": "session.updated", "session": {"turn_detection": {"type": "server_vad"}}}])
            voice.app[HTTP_KEY] = SimpleNamespace(ws_connect=AsyncMock(return_value=cloud))
            with patch("gateway.load_wake_ack", return_value=None):
                if supported:
                    with self.assertRaisesRegex(RuntimeError, "云端未确认"):
                        await backend.connect()
                else:
                    await backend.connect()
                    self.assertFalse(backend.wake_clip_active)
                    self.assertEqual(backend._send.await_args.args[0]["session"]["turn_detection"]["type"], "server_vad")

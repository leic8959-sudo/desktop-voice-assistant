"""语音识别双向连接的离线故障回归，不访问真实云端。"""

import asyncio
import json
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

import aiohttp

from chat_pipeline import ChatBackend, _AsrPump
from gateway import CONFIG_KEY, RUNTIME_KEY, HTTP_KEY, VoiceSession
from llm_providers import apply_defaults


class ControlledAsrSocket:
    def __init__(self, *, auto_ready=True, send_error=None, send_release=None, close_release=None):
        self.messages = asyncio.Queue()
        self.closed = False
        self.auto_ready = auto_ready
        self.send_error = send_error
        self.send_release = send_release
        self.close_release = close_release
        self.handshaking = asyncio.Event()
        self.sending = asyncio.Event()
        self.closing = asyncio.Event()
        self.sent = []

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.messages.get()
        if message is None:
            raise StopAsyncIteration
        return message

    def event(self, name, **payload):
        self.messages.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps({"header": {"event": name}, "payload": payload})))

    async def send_json(self, _payload):
        self.handshaking.set()
        if self.auto_ready:
            self.event("task-started")

    async def send_bytes(self, frame):
        self.sending.set()
        if self.send_release is not None:
            await self.send_release.wait()
        if self.send_error:
            raise self.send_error
        self.sent.append(frame)

    async def close(self):
        self.closing.set()
        if self.close_release is not None:
            await self.close_release.wait()
        self.closed = True
        self.messages.put_nowait(None)


def pump_backend(sockets):
    return SimpleNamespace(
        client=SimpleNamespace(ws_connect=AsyncMock(side_effect=list(sockets))),
        config={"dashscope_ws_url": "https://offline.invalid/inference", "chat_asr_model": "offline-asr"},
        _closed=False, _asr_audio=asyncio.Queue(maxsize=4), dashscope_headers=lambda: {},
        _emit=Mock(), on_asr_result=Mock(), _reset_accumulators=Mock(),
    )


def connect_session(socket):
    config = {"enable_search": False, "aliyun_api_key": "offline-placeholder", "aliyun_workspace_id": "offline-space", "dashscope_ws_url": "https://offline.invalid/inference"}
    apply_defaults(config)
    return SimpleNamespace(config=config, session_id="offline-asr-connect", http=SimpleNamespace(ws_connect=AsyncMock(return_value=socket)), memory_service=SimpleNamespace(context=AsyncMock(return_value=[])), active_tool_schemas=lambda: [])


class AsrSocket:
    def __init__(self):
        self.messages = asyncio.Queue()
        self.closed = False
        self.send_failed = asyncio.Event()

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.messages.get()
        if message is None:
            raise StopAsyncIteration
        return message

    async def send_json(self, _payload):
        self.messages.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps({"header": {"event": "task-started"}})))

    async def send_bytes(self, _frame):
        self.send_failed.set()
        raise OSError("synthetic ASR upload failure")

    async def close(self):
        self.closed = True
        self.messages.put_nowait(None)


class AsrLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_close_is_safe_before_connect_has_initialized_resources(self):
        backend = ChatBackend(SimpleNamespace(config={}, session_id="offline-unconnected"), "qwen_chat")
        await backend.close()
        self.assertFalse(backend.connected)

    async def test_upload_failure_is_supervised_even_when_reader_stays_connected(self):
        socket = AsrSocket()
        audio = asyncio.Queue(maxsize=2)
        audio.put_nowait(b"\x00\x00" * 320)
        backend = SimpleNamespace(
            client=SimpleNamespace(ws_connect=AsyncMock(return_value=socket)),
            config={"dashscope_ws_url": "https://offline.invalid/inference", "chat_asr_model": "offline-asr"},
            _closed=False, _asr_audio=audio, dashscope_headers=lambda: {},
            _emit=Mock(), on_asr_result=Mock(),
            _reset_accumulators=Mock(),
        )
        pump = _AsrPump(backend)
        with patch("chat_pipeline.ASR_RECONNECT_LIMIT", 0), self.assertLogs("assistant.chat_pipeline", level="ERROR") as logs:
            task = asyncio.create_task(pump.run())
            try:
                await asyncio.wait_for(socket.send_failed.wait(), 1)
                await asyncio.wait_for(asyncio.shield(task), 1)
                backend._emit.assert_called_once_with({"type": "error", "error": {"code": "asr_unavailable"}})
                self.assertTrue(socket.closed)
                backend._reset_accumulators.assert_called()
                self.assertIn("synthetic ASR upload failure", "\n".join(logs.output))
            finally:
                backend._closed = True
                if not task.done():
                    task.cancel()
                await asyncio.gather(task, return_exceptions=True)

    async def test_handshake_and_upload_timeouts_close_the_owned_socket(self):
        for phase in ("handshake", "upload"):
            with self.subTest(phase=phase):
                socket = ControlledAsrSocket(auto_ready=phase != "handshake", send_release=asyncio.Event() if phase == "upload" else None)
                backend = pump_backend([socket])
                backend._asr_audio.put_nowait(b"\x00\x00")
                pump = _AsrPump(backend)
                with patch("chat_pipeline.ASR_RECONNECT_LIMIT", 0), patch("chat_pipeline.ASR_HANDSHAKE_TIMEOUT", 0.01), patch("chat_pipeline.ASR_SEND_TIMEOUT", 0.01), self.assertLogs("assistant.chat_pipeline", level="ERROR"):
                    await asyncio.wait_for(pump.run(), 1)
                self.assertTrue(socket.closed)
                self.assertFalse(pump.ready.is_set())
                backend._emit.assert_called_once_with({"type": "error", "error": {"code": "asr_unavailable"}})

    async def test_restart_is_folded_and_new_connection_waits_for_old_close(self):
        close_release = asyncio.Event()
        first, second = ControlledAsrSocket(close_release=close_release), ControlledAsrSocket()
        backend = pump_backend([first, second])
        pump = _AsrPump(backend)
        pump.start()
        try:
            await asyncio.wait_for(pump.ready.wait(), 1)
            pump.restart()
            await asyncio.wait_for(first.closing.wait(), 1)
            for _ in range(50):
                pump.restart()
            self.assertEqual(backend.client.ws_connect.await_count, 1)
            owned = [task for task in asyncio.all_tasks() if task.get_coro().__name__ in {"_serve", "_feed", "_read"}]
            self.assertEqual(len(owned), 1)
            close_release.set()
            await asyncio.wait_for(second.handshaking.wait(), 1)
            self.assertTrue(first.closed)
            self.assertEqual(backend.client.ws_connect.await_count, 2)
        finally:
            close_release.set()
            backend._closed = True
            pump.task.cancel()
            await asyncio.gather(pump.task, return_exceptions=True)
        self.assertTrue(second.closed)

    async def test_successful_handshake_and_restart_do_not_reset_error_budget(self):
        first, restarted, last = ControlledAsrSocket(), ControlledAsrSocket(), ControlledAsrSocket()
        backend = pump_backend([first, restarted, last])
        pump = _AsrPump(backend)
        with patch("chat_pipeline.ASR_RECONNECT_LIMIT", 1), self.assertLogs("assistant.chat_pipeline", level="ERROR"):
            pump.start()
            try:
                await asyncio.wait_for(first.handshaking.wait(), 1)
                first.event("task-failed")
                await asyncio.wait_for(restarted.handshaking.wait(), 2)
                await asyncio.wait_for(pump.ready.wait(), 1)
                pump.restart()
                await asyncio.wait_for(last.handshaking.wait(), 2)
                await asyncio.wait_for(pump.ready.wait(), 1)
                last.event("task-failed")
                await asyncio.wait_for(asyncio.shield(pump.task), 2)
                self.assertEqual(backend.client.ws_connect.await_count, 3)
                backend._emit.assert_called_once_with({"type": "error", "error": {"code": "asr_unavailable"}})
            finally:
                backend._closed = True
                if not pump.task.done():
                    pump.task.cancel()
                await asyncio.gather(pump.task, return_exceptions=True)

    async def test_simultaneous_failure_and_restart_still_consumes_error_budget(self):
        backend = pump_backend([])
        pump = _AsrPump(backend)
        failure_entered, release_failure = asyncio.Event(), asyncio.Event()

        async def failing_connection():
            failure_entered.set()
            await release_failure.wait()
            pump.restart()
            raise OSError("simultaneous synthetic upload failure")

        pump._serve = failing_connection
        with patch("chat_pipeline.ASR_RECONNECT_LIMIT", 0), self.assertLogs("assistant.chat_pipeline", level="ERROR") as logs:
            pump.start()
            await asyncio.wait_for(failure_entered.wait(), 1)
            release_failure.set()
            await asyncio.wait_for(asyncio.shield(pump.task), 1)
        backend._emit.assert_called_once_with({"type": "error", "error": {"code": "asr_unavailable"}})
        self.assertIn("simultaneous synthetic upload failure", "\n".join(logs.output))

    async def test_connect_waits_for_real_task_started_and_close_before_connect_is_safe(self):
        socket = ControlledAsrSocket(auto_ready=False)
        backend = ChatBackend(connect_session(socket), "qwen_chat")
        task = asyncio.create_task(backend.connect())
        try:
            await asyncio.wait_for(socket.handshaking.wait(), 1)
            self.assertFalse(task.done())
            socket.event("task-started")
            await asyncio.wait_for(task, 1)
            self.assertTrue(backend.asr.ready.is_set())
        finally:
            await backend.close()
        self.assertTrue(socket.closed)

    async def test_startup_total_timeout_cleans_connection_before_connect_fails(self):
        socket = ControlledAsrSocket(auto_ready=False)
        backend = ChatBackend(connect_session(socket), "qwen_chat")
        with patch("chat_pipeline.ASR_STARTUP_TIMEOUT", 0.01):
            with self.assertRaises(TimeoutError):
                await backend.connect()
        self.assertTrue(socket.closed)
        self.assertTrue(backend.asr.task.done())
        self.assertFalse(backend.connected)

    async def test_cancelled_connect_waits_for_slow_socket_close(self):
        release_close = asyncio.Event()
        socket = ControlledAsrSocket(auto_ready=False, close_release=release_close)
        backend = ChatBackend(connect_session(socket), "qwen_chat")
        task = asyncio.create_task(backend.connect())
        try:
            await asyncio.wait_for(socket.handshaking.wait(), 1)
            task.cancel()
            await asyncio.wait_for(socket.closing.wait(), 1)
            self.assertFalse(task.done())
            release_close.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 1)
            self.assertTrue(socket.closed)
            self.assertTrue(backend.asr.task.done())
        finally:
            release_close.set()
            await asyncio.gather(task, return_exceptions=True)

    async def test_connection_failure_discards_pending_transcript_before_slow_close(self):
        release_close = asyncio.Event()
        first, second = ControlledAsrSocket(close_release=release_close), ControlledAsrSocket()
        session = connect_session(first)
        session.config["chat_turn_silence_ms"] = 0
        backend = ChatBackend(session, "qwen_chat")
        backend._events = asyncio.Queue()
        backend._asr_audio = asyncio.Queue(maxsize=4)
        backend._finals = []
        backend._last_partial = ""
        backend._turn_generation = 0
        new_turn = asyncio.Event()
        backend._start_user_turn = AsyncMock(side_effect=lambda _text: new_turn.set())
        backend.client = SimpleNamespace(ws_connect=AsyncMock(side_effect=[first, second]))
        pump = _AsrPump(backend)
        backend.asr = pump
        pump.start()
        old_timer_cancelled = asyncio.Event()

        async def old_timer():
            try:
                await asyncio.Event().wait()
            finally:
                old_timer_cancelled.set()

        try:
            await asyncio.wait_for(pump.ready.wait(), 1)
            backend._finals = ["旧句"]
            backend._last_partial = "旧半句"
            backend._timer_task = asyncio.create_task(old_timer())
            timer_started = asyncio.Event()
            asyncio.get_running_loop().call_soon(timer_started.set)
            await asyncio.wait_for(timer_started.wait(), 1)
            first.event("task-failed")
            with self.assertLogs("assistant.chat_pipeline", level="ERROR"):
                await asyncio.wait_for(first.closing.wait(), 1)
                self.assertEqual(backend._finals, [])
                self.assertEqual(backend._last_partial, "")
                await asyncio.wait_for(old_timer_cancelled.wait(), 1)
                backend._start_user_turn.assert_not_awaited()
                release_close.set()
                await asyncio.wait_for(second.handshaking.wait(), 2)
            second.event("result-generated", output={"sentence": {"text": "新句", "sentence_end": True}})
            await asyncio.wait_for(new_turn.wait(), 1)
            backend._start_user_turn.assert_awaited_once_with("新句")
        finally:
            release_close.set()
            await backend.close()

    async def test_failed_asr_startup_closes_before_provider_fallback(self):
        socket = ControlledAsrSocket(auto_ready=False)
        template = connect_session(socket)
        runtime = SimpleNamespace(memory_service=template.memory_service, last_provider=None, last_cloud_ready=None)
        device = SimpleNamespace(send_json=AsyncMock(), closed=False)
        session = VoiceSession({CONFIG_KEY: template.config, RUNTIME_KEY: runtime, HTTP_KEY: template.http}, device, browser=True)
        attempted = []

        def chat_factory(actual_session, name):
            backend = ChatBackend(actual_session, name)
            attempted.append(backend)
            return backend

        async def fallback_connect():
            self.assertTrue(socket.closed)
            self.assertTrue(attempted[0].asr.task.done())

        fallback = SimpleNamespace(name="qwen_realtime", connect=AsyncMock(side_effect=fallback_connect), close=AsyncMock())
        with patch("gateway.provider_chain", return_value=["qwen_chat", "qwen_realtime"]), patch("gateway.ChatBackend", side_effect=chat_factory), patch("gateway.RealtimeBackend", return_value=fallback), patch("chat_pipeline.ASR_RECONNECT_LIMIT", 0), patch("chat_pipeline.ASR_HANDSHAKE_TIMEOUT", 0.01), self.assertLogs(level="ERROR"):
            await asyncio.wait_for(session.connect(), 1)
        self.assertIs(session.backend, fallback)
        fallback.connect.assert_awaited_once()
        self.assertEqual(runtime.last_provider, "qwen_realtime")

    async def test_unconfirmed_close_deadline_prevents_reconnect(self):
        release_close = asyncio.Event()
        socket = ControlledAsrSocket(close_release=release_close)
        backend = pump_backend([socket, ControlledAsrSocket()])
        pump = _AsrPump(backend)
        with patch("chat_pipeline.ASR_CLOSE_TIMEOUT", 0.01), self.assertLogs("assistant.chat_pipeline", level="ERROR"):
            pump.start()
            try:
                await asyncio.wait_for(pump.ready.wait(), 1)
                socket.event("task-failed")
                await asyncio.wait_for(socket.closing.wait(), 1)
                await asyncio.wait_for(asyncio.shield(pump.task), 1)
                self.assertEqual(backend.client.ws_connect.await_count, 1)
                self.assertFalse(socket.closed)
                self.assertFalse(pump.ready.is_set())
                backend._emit.assert_called_once_with({"type": "error", "error": {"code": "asr_unavailable"}})
            finally:
                release_close.set()
                backend._closed = True
                if not pump.task.done():
                    pump.task.cancel()
                await asyncio.gather(pump.task, return_exceptions=True)
                await socket.close()

    async def test_silent_audio_queue_wait_does_not_consume_send_deadline(self):
        socket = ControlledAsrSocket()
        backend = pump_backend([socket])
        pump = _AsrPump(backend)
        quiet_window_ended = asyncio.Event()
        timer = None
        with patch("chat_pipeline.ASR_SEND_TIMEOUT", 0.01):
            pump.start()
            try:
                await asyncio.wait_for(pump.ready.wait(), 1)
                # 专门等待超过发送期限，证明期限不包正常静音的queue.get等待。
                timer = asyncio.get_running_loop().call_later(0.03, quiet_window_ended.set)
                await asyncio.wait_for(quiet_window_ended.wait(), 1)
                self.assertFalse(pump.task.done())
                self.assertTrue(pump.ready.is_set())
                self.assertFalse(socket.sending.is_set())
                backend._emit.assert_not_called()
                frame = b"\x02\x00" * 320
                backend._asr_audio.put_nowait(frame)
                await asyncio.wait_for(socket.sending.wait(), 1)
                self.assertEqual(socket.sent, [frame])
                self.assertFalse(pump.task.done())
                backend._emit.assert_not_called()
            finally:
                if timer is not None:
                    timer.cancel()
                backend._closed = True
                pump.task.cancel()
                await asyncio.gather(pump.task, return_exceptions=True)


if __name__ == "__main__":
    unittest.main()

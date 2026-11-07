"""TTS生命周期的离线故障回归；不启动服务、不使用真实凭据或云端。"""

import asyncio
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

import aiohttp

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from chat_pipeline import ChatBackend, _Chunk
from src.assistant_app.application.proactive_speech import _TtsCollector


class FakeTtsSocket:
    def __init__(self, handshake="started", fail_action=None):
        self.handshake = handshake
        self.fail_action = fail_action
        self.messages = asyncio.Queue()
        self.sent = []
        self.closed = False
        self.started = asyncio.Event()
        self.text_sent = asyncio.Event()

    def __aiter__(self):
        return self

    async def __anext__(self):
        message = await self.messages.get()
        if message is None:
            raise StopAsyncIteration
        return message

    def event(self, name):
        self.messages.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.TEXT, data=json.dumps({"header": {"event": name}})))

    async def send_json(self, payload):
        action = payload["header"]["action"]
        self.sent.append(payload)
        if action == self.fail_action:
            raise RuntimeError("synthetic send failure")
        if action == "run-task":
            self.started.set()
            if self.handshake == "started":
                self.event("task-started")
            elif self.handshake == "rejected":
                self.event("task-failed")
            elif self.handshake == "eof":
                self.messages.put_nowait(None)
        elif action == "continue-task":
            self.text_sent.set()
            self.messages.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=b"\x01\x00" * 48))
        elif action == "finish-task":
            self.event("task-finished")

    async def close(self):
        self.closed = True
        self.messages.put_nowait(None)


def make_backend(sockets):
    config = {"dashscope_ws_url": "https://offline.invalid/inference", "aliyun_api_key": "offline-placeholder", "chat_tts_model": "offline-tts", "chat_tts_voice": "offline-voice"}
    backend = ChatBackend(SimpleNamespace(config=config, session_id="offline-tts-session"), "qwen_chat")
    backend.client = SimpleNamespace(ws_connect=AsyncMock(side_effect=list(sockets)))
    backend._response_sequence = 0
    backend._cancelled = set()
    backend._events = asyncio.Queue()
    backend._tts_text = asyncio.Queue()
    backend._tts_task = backend._turn_task = backend._timer_task = None
    return backend


class ChatTtsLifecycleTests(unittest.IsolatedAsyncioTestCase):
    async def test_request_failure_closes_tts_before_round_returns(self):
        socket = FakeTtsSocket()
        backend = make_backend([socket])

        async def chunks(_messages):
            yield _Chunk(content="第一句。")
            await socket.text_sent.wait()
            raise RuntimeError("synthetic stream failure")

        backend._request = chunks
        with self.assertRaisesRegex(RuntimeError, "synthetic stream failure"):
            await asyncio.wait_for(backend._chat_round([]), 1)
        self.assertTrue(socket.closed)
        self.assertIsNone(backend._tts_task)
        self.assertFalse(any(item["type"] == "response.done" for item in list(backend._events._queue)))

    async def test_cancelled_round_cannot_consume_next_round_text(self):
        old_socket, new_socket = FakeTtsSocket(), FakeTtsSocket()
        backend = make_backend([old_socket, new_socket])

        async def old_chunks(_messages):
            yield _Chunk(content="旧回答。")
            await asyncio.Event().wait()

        backend._request = old_chunks
        old_round = asyncio.create_task(backend._chat_round([]))
        await asyncio.wait_for(old_socket.text_sent.wait(), 1)
        old_tts = backend._tts_task
        old_round.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await old_round
        self.assertTrue(old_socket.closed)
        self.assertTrue(old_tts.done())

        async def new_chunks(_messages):
            yield _Chunk(content="新回答。")

        backend._request = new_chunks
        await asyncio.wait_for(backend._chat_round([]), 1)
        old_text = [item["payload"]["input"]["text"] for item in old_socket.sent if item["header"]["action"] == "continue-task"]
        new_text = [item["payload"]["input"]["text"] for item in new_socket.sent if item["header"]["action"] == "continue-task"]
        self.assertEqual(old_text, ["旧回答。"])
        self.assertEqual(new_text, ["新回答。"])
        self.assertTrue(new_socket.closed)

    async def test_close_cancels_tts_even_without_active_turn(self):
        backend = make_backend([])
        task = asyncio.create_task(asyncio.Event().wait())
        backend._tts_task = task
        await backend.close()
        self.assertTrue(task.cancelled())

    async def test_handshake_failure_closes_owned_socket(self):
        for handshake, fail_action in (("rejected", None), ("eof", None), ("started", "run-task")):
            with self.subTest(handshake=handshake, fail_action=fail_action):
                socket = FakeTtsSocket(handshake, fail_action)
                backend = make_backend([socket])
                with self.assertRaises(RuntimeError):
                    await backend._open_tts("offline-task")
                self.assertTrue(socket.closed)

    async def test_cancelled_handshake_closes_owned_socket(self):
        socket = FakeTtsSocket("hang")
        backend = make_backend([socket])
        task = asyncio.create_task(backend._open_tts("offline-task"))
        await asyncio.wait_for(socket.started.wait(), 1)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(socket.closed)

    async def test_handshake_timeout_closes_owned_socket(self):
        socket = FakeTtsSocket("hang")
        backend = make_backend([socket])
        with patch("chat_pipeline.TTS_HANDSHAKE_TIMEOUT", 0.01):
            with self.assertRaises(TimeoutError):
                await backend._open_tts("offline-task")
        self.assertTrue(socket.closed)

    async def test_tts_eof_without_finished_is_failure(self):
        socket = FakeTtsSocket()
        socket.messages.put_nowait(None)
        backend = make_backend([])
        with self.assertRaisesRegex(RuntimeError, "合成完成前结束"):
            await backend._tts_read(socket, "offline-response")

    async def test_tts_handshake_starts_before_first_sentence(self):
        # 首音优化：TTS 握手必须与 LLM 流式并行预热，而不是等首句到达才开始建连。
        socket = FakeTtsSocket()
        backend = make_backend([socket])
        text_queue = asyncio.Queue()
        task = asyncio.create_task(backend._tts_turn("offline-response", text_queue))
        await asyncio.wait_for(socket.started.wait(), 1)
        text_queue.put_nowait("第一句。")
        text_queue.put_nowait(None)
        await asyncio.wait_for(task, 1)
        self.assertTrue(socket.closed)

    async def test_cancelled_turn_closes_prewarmed_socket(self):
        socket = FakeTtsSocket()
        backend = make_backend([socket])
        text_queue = asyncio.Queue()
        task = asyncio.create_task(backend._tts_turn("offline-response", text_queue))
        await asyncio.wait_for(socket.started.wait(), 1)
        task.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await task
        self.assertTrue(socket.closed)

    async def test_handshake_failure_without_text_does_not_fail_turn(self):
        # 无文本可合成时，预热握手失败只丢弃连接，不影响回合收尾。
        backend = make_backend([])
        text_queue = asyncio.Queue()
        text_queue.put_nowait(None)
        await asyncio.wait_for(backend._tts_turn("offline-response", text_queue), 1)

    async def test_send_failure_cancels_reader_and_closes_socket(self):
        socket = FakeTtsSocket(fail_action="continue-task")
        backend = make_backend([socket])
        text_queue = asyncio.Queue()
        text_queue.put_nowait("第一句。")
        with self.assertRaisesRegex(RuntimeError, "synthetic send failure"):
            await asyncio.wait_for(backend._tts_turn("offline-response", text_queue), 1)
        self.assertTrue(socket.closed)

    async def test_finish_timeout_starts_after_llm_text_is_complete(self):
        socket = FakeTtsSocket()
        backend = make_backend([socket])
        text_queue = asyncio.Queue()
        text_queue.put_nowait("第一句。")
        with patch("chat_pipeline.TTS_FINISH_TIMEOUT", 0.01):
            task = asyncio.create_task(backend._tts_turn("offline-response", text_queue))
            try:
                await asyncio.wait_for(socket.text_sent.wait(), 1)
                await asyncio.sleep(0.03)
                self.assertFalse(task.done())
                text_queue.put_nowait("第二句。")
                text_queue.put_nowait(None)
                await asyncio.wait_for(task, 1)
            finally:
                if not task.done():
                    task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        self.assertTrue(socket.closed)

    async def test_missing_finished_times_out_and_closes_socket(self):
        socket = FakeTtsSocket()
        original_send = socket.send_json

        async def send_without_finish(payload):
            if payload["header"]["action"] != "finish-task":
                await original_send(payload)

        socket.send_json = send_without_finish
        backend = make_backend([socket])
        text_queue = asyncio.Queue()
        text_queue.put_nowait("第一句。")
        text_queue.put_nowait(None)
        with patch("chat_pipeline.TTS_FINISH_TIMEOUT", 0.01):
            with self.assertRaises(TimeoutError):
                await backend._tts_turn("offline-response", text_queue)
        self.assertTrue(socket.closed)

    async def test_proactive_collector_retains_shared_tts_protocol(self):
        socket = FakeTtsSocket()
        backend = make_backend([socket])
        collector = _TtsCollector(backend.config, backend.client)
        try:
            audio = await asyncio.wait_for(collector.synthesize("该喝水了。"), 1)
            self.assertEqual(audio, b"\x01\x00" * 48)
            self.assertTrue(collector.finished)
        finally:
            await collector.dispose()
        self.assertTrue(socket.closed)


if __name__ == "__main__":
    unittest.main()

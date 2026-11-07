"""真实ChatBackend到VoiceSession的播放交接；仅模型流和TTS网络为离线替身。"""

import asyncio
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

import aiohttp

from chat_pipeline import ChatBackend, _Chunk
from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession
from tests.test_chat_tts_lifecycle import FakeTtsSocket


class RecordingDevice:
    def __init__(self):
        self.json = []
        self.audio = []
        self.trace = []
        self.playback_id = None
        self.changed = asyncio.Condition()
        self.closed = False

    async def send_json(self, payload):
        self.json.append(dict(payload))
        self.trace.append(dict(payload))
        if payload.get("type") == "tts" and payload.get("state") == "start":
            self.playback_id = payload.get("playback_id")
        elif payload.get("type") == "interrupt" or payload.get("type") == "tts" and payload.get("state") == "stop":
            if payload.get("playback_id") == self.playback_id:
                self.playback_id = None
        async with self.changed:
            self.changed.notify_all()

    async def send_bytes(self, payload):
        self.audio.append((self.playback_id, payload))
        self.trace.append((self.playback_id, payload))
        async with self.changed:
            self.changed.notify_all()

    async def close(self):
        self.closed = True

    async def wait(self, predicate):
        async with asyncio.timeout(2):
            async with self.changed:
                await self.changed.wait_for(predicate)

    def tts(self, state):
        return [item for item in self.json if item.get("type") == "tts" and item.get("state") == state]

    def usage_count(self):
        return sum(item.get("type") == "usage" for item in self.json)


class MarkedTtsSocket(FakeTtsSocket):
    def __init__(self, pcm):
        super().__init__()
        self.pcm = pcm

    async def send_json(self, payload):
        if payload["header"]["action"] == "continue-task":
            self.sent.append(payload)
            self.text_sent.set()
            self.messages.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY, data=self.pcm))
            return
        await super().send_json(payload)


class ChatPlaybackHandoffTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        config = {"enable_search": False, "dashscope_ws_url": "https://offline.invalid/inference", "aliyun_api_key": "offline-placeholder", "chat_tts_model": "offline-tts", "chat_tts_voice": "offline-voice"}
        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=None, connector_registry=SimpleNamespace(manifests=lambda: []), last_latency=None, last_usage=None)
        self.device = RecordingDevice()
        self.session = VoiceSession({CONFIG_KEY: config, RUNTIME_KEY: runtime}, self.device, browser=True)
        self.backend = ChatBackend(self.session, "qwen_chat")
        self.session.backend = self.backend
        self.backend._history = []
        self.backend._instructions = "offline"
        self.backend._events = asyncio.Queue()
        self.backend._cancelled = set()
        self.backend._response_sequence = self.backend._turn_generation = 0
        self.backend._turn_task = self.backend._timer_task = self.backend._tts_task = None
        self.backend._active_response_id = self.backend._pending_history = None
        self.backend._tts_text = None
        self.scripts = []
        self.events = asyncio.Queue()
        self.tts_sockets = []
        self.tasks = []
        original_emit = self.backend._emit

        def emit(event):
            self.events.put_nowait(event)
            original_emit(event)

        self.backend._emit = emit

        def websocket(*_args, **_kwargs):
            socket = MarkedTtsSocket(bytes([len(self.tts_sockets) + 1, 0]) * 48)
            self.tts_sockets.append(socket)
            return socket

        async def request(messages):
            script = self.scripts.pop(0)
            async for chunk in script(messages):
                yield chunk

        self.backend.client = SimpleNamespace(ws_connect=AsyncMock(side_effect=websocket))
        self.backend._request = request

    async def asyncTearDown(self):
        await self.backend.close()
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)

    def launch(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        self.session.tasks.add(task)
        return task

    @staticmethod
    def text(text, *, before=None, after=None):
        async def script(_messages):
            if before is not None:
                await before.wait()
            yield _Chunk(content=text)
            if after is not None:
                await after.wait()
        return script

    async def wait_drain(self):
        drain = self.backend._turn_drain_task
        if drain is not None:
            await asyncio.wait_for(asyncio.shield(drain), 2)

    async def backend_event(self, predicate):
        async with asyncio.timeout(2):
            while True:
                event = await self.events.get()
                if predicate(event):
                    return event

    async def cloud_barrier(self):
        self.backend._emit({"type": "response.text.done", "text": "离线同步点"})
        await self.device.wait(lambda: any(item.get("text") == "离线同步点" for item in self.device.json))

    async def test_completed_cloud_reply_still_buffered_gets_new_playback_start(self):
        self.scripts = [self.text("旧回答。"), self.text("新回答。")]
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: self.device.usage_count() == 1)
        await self.wait_drain()
        old_playback = self.session.current_playback_id
        self.assertTrue(self.session.speaking)
        self.assertIsNone(self.session.current_response)
        self.assertFalse(self.session.playback.empty())

        await self.backend.send_user_text("第二问")
        await self.device.wait(lambda: self.device.usage_count() == 2)
        new_playback = self.session.current_playback_id
        starts = [item["playback_id"] for item in self.device.tts("start")]
        self.assertEqual(starts, [old_playback, new_playback], "新回答覆盖播放ID但没有匹配的tts start，设备仍持有旧播放ID")
        self.assertIn(old_playback, [item.get("playback_id") for item in self.device.json if item.get("type") == "interrupt"])
        self.launch(self.session.play_output())
        await self.device.wait(lambda: any(item.get("playback_id") == new_playback for item in self.device.tts("stop")))
        self.assertEqual(self.device.audio, [(new_playback, b"\x02\x00" * 48)])

    async def test_new_user_input_cancels_stream_and_starts_matching_new_playback(self):
        unfinished = asyncio.Event()
        self.scripts = [self.text("旧回答。", after=unfinished), self.text("新回答。")]
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: len(self.device.tts("start")) == 1)
        old_playback = self.session.current_playback_id
        old_owner = self.backend._turn_task
        old_tts = self.tts_sockets[0]

        await self.backend.send_user_text("换个问题")
        await self.device.wait(lambda: self.device.usage_count() == 1)
        new_playback = self.session.current_playback_id
        self.assertTrue(old_owner.done())
        self.assertTrue(old_tts.closed)
        starts = [item["playback_id"] for item in self.device.tts("start")]
        self.assertEqual(starts, [old_playback, new_playback], "旧云回合已取消，但新设备播放缺少匹配start")
        self.assertIn(old_playback, [item.get("playback_id") for item in self.device.json if item.get("type") == "interrupt"])
        self.launch(self.session.play_output())
        await self.device.wait(lambda: any(item.get("playback_id") == new_playback for item in self.device.tts("stop")))
        self.assertEqual(self.device.audio, [(new_playback, b"\x02\x00" * 48)])

    async def test_tool_followup_is_a_continuation_without_user_interrupt(self):
        continue_answer = asyncio.Event()

        async def tool_round(_messages):
            yield _Chunk(content="我来查询。")
            yield _Chunk(tool_delta=[{"index": 0, "id": "offline-call", "function": {"name": "list_tasks", "arguments": "{}"}}])

        self.scripts = [tool_round, self.text("查询完成。", before=continue_answer)]
        self.session.execute_tool = AsyncMock(return_value={"tasks": []})
        self.launch(self.session.cloud_output())
        self.launch(self.session.play_output())
        await self.backend.send_user_text("查一下待办")
        await self.device.wait(lambda: len(self.device.audio) == 1)
        continue_answer.set()
        await self.device.wait(lambda: len(self.device.tts("stop")) == 1)
        self.assertEqual(len(self.device.tts("start")), 1)
        self.assertFalse(any(item.get("type") == "interrupt" for item in self.device.json))
        self.session.execute_tool.assert_awaited_once_with("list_tasks", {})
        self.assertEqual([audio for _playback, audio in self.device.audio], [b"\x01\x00" * 48, b"\x02\x00" * 48])

    async def test_late_old_audio_done_and_turn_done_cannot_finish_new_playback(self):
        unfinished = asyncio.Event()
        self.scripts = [self.text("旧回答。"), self.text("新回答。", after=unfinished)]
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: self.device.usage_count() == 1)
        await self.wait_drain()
        old_turn = (await self.backend_event(lambda event: event["type"] == "conversation.user_turn.started"))["user_turn_id"]
        old_response = next(iter(self.session.response_playback_ids))
        await self.backend.send_user_text("第二问")
        await self.device.wait(lambda: len(self.device.tts("start")) == 2)
        new_response, new_playback = self.session.current_response, self.session.current_playback_id
        await self.backend_event(lambda event: event["type"] == "response.audio.delta" and event["response_id"] == new_response)
        self.backend._emit({"type": "response.audio.delta", "response_id": old_response, "delta": "AwADAA=="})
        self.backend._emit({"type": "response.done", "response": {"id": old_response, "status": "completed"}})
        self.backend._emit({"type": "conversation.user_turn.done", "user_turn_id": old_turn, "status": "completed"})
        await self.cloud_barrier()
        self.assertEqual(self.session.current_response, new_response)
        self.assertEqual(self.session.current_playback_id, new_playback)
        self.assertFalse(self.session.response_completed)
        self.assertEqual(self.session.output_bytes, 96)
        unfinished.set()
        await self.wait_drain()
        self.launch(self.session.play_output())
        await self.device.wait(lambda: any(item.get("playback_id") == new_playback for item in self.device.tts("stop")))
        self.assertEqual(self.device.audio, [(new_playback, b"\x02\x00" * 48)])

    async def test_tool_only_turn_has_no_fake_audio_start_or_stop(self):
        async def tool_round(_messages):
            yield _Chunk(tool_delta=[{"index": 0, "id": "offline-call", "function": {"name": "list_tasks", "arguments": "{}"}}])

        self.scripts = [tool_round, self.text("")]
        self.session.execute_tool = AsyncMock(return_value={"tasks": []})
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("查一下待办")
        await self.wait_drain()
        await self.cloud_barrier()
        self.assertEqual(self.device.tts("start"), [])
        self.assertEqual(self.device.tts("stop"), [])
        self.assertFalse(self.session.speaking)
        self.assertTrue(self.session.response_completed)
        self.assertIsNone(self.session.current_chat_turn)

    async def test_tool_round_limit_stops_prefix_without_media_handoff(self):
        async def tool_round(_messages):
            yield _Chunk(content="还在查询。")
            yield _Chunk(tool_delta=[{"index": 0, "id": "offline-call", "function": {"name": "list_tasks", "arguments": "{}"}}])

        self.scripts = [tool_round] * 4
        self.session.execute_tool = AsyncMock(return_value={"tasks": []})
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("查一下待办")
        await self.wait_drain()
        await self.cloud_barrier()
        self.assertFalse(self.session.response_completed)
        self.assertFalse(self.session.speaking)
        self.assertTrue(self.session.playback.empty())
        self.assertEqual(len(self.device.tts("start")), 1)
        self.assertEqual(len(self.device.tts("stop")), 1)
        self.assertTrue(any(item.get("type") == "interrupt" for item in self.device.json))
        self.assertTrue(any("达到上限" in item.get("message", "") for item in self.device.json if item.get("type") == "alert"))

    async def test_failed_turn_stops_device_playback_before_cloud_error(self):
        async def failing(_messages):
            yield _Chunk(content="旧回答。")
            await self.device.wait(lambda: len(self.device.tts("start")) == 1)
            raise RuntimeError("offline stream failure")

        self.scripts = [failing]
        cloud = self.launch(self.session.cloud_output())
        with self.assertLogs("assistant.chat_pipeline", level="ERROR"):
            await self.backend.send_user_text("第一问")
            await self.wait_drain()
        with self.assertRaisesRegex(RuntimeError, "chat_turn_failed"):
            await asyncio.wait_for(asyncio.shield(cloud), 2)
        self.assertFalse(self.session.speaking)
        self.assertFalse(self.session.response_completed)
        self.assertTrue(self.session.playback.empty())
        self.assertEqual(len(self.device.tts("stop")), 1)
        self.assertTrue(any(item.get("type") == "interrupt" for item in self.device.json))

    async def test_cancelled_turn_clears_buffered_audio_without_new_response(self):
        unfinished = asyncio.Event()
        self.scripts = [self.text("旧回答。", after=unfinished)]
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: len(self.device.tts("start")) == 1)
        await self.backend.cancel_response()
        await self.cloud_barrier()
        self.assertFalse(self.session.speaking)
        self.assertTrue(self.session.playback.empty())
        self.assertEqual(len(self.device.tts("stop")), 1)
        self.assertFalse(self.session.response_completed)

    async def test_close_resources_stops_device_after_output_task_is_cancelled(self):
        unfinished = asyncio.Event()
        self.scripts = [self.text("旧回答。", after=unfinished)]
        cloud = self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: len(self.device.tts("start")) == 1)
        self.session.closed = True
        await self.session.close_resources()
        self.assertTrue(cloud.done())
        self.assertTrue(self.device.closed)
        self.assertFalse(self.session.speaking)
        self.assertEqual(len(self.device.tts("stop")), 1)
        self.assertTrue(any(item.get("type") == "interrupt" for item in self.device.json))

    async def test_confirmed_barge_in_cancels_active_tool_chain_after_prefix_done(self):
        followup_entered, release = asyncio.Event(), asyncio.Event()

        async def tool_round(_messages):
            yield _Chunk(content="我来查询。")
            yield _Chunk(tool_delta=[{"index": 0, "id": "offline-call", "function": {"name": "list_tasks", "arguments": "{}"}}])

        async def followup(_messages):
            followup_entered.set()
            await release.wait()
            yield _Chunk(content="不应继续播出。")

        self.scripts = [tool_round, followup]
        self.session.execute_tool = AsyncMock(return_value={"tasks": []})
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("查一下待办")
        await asyncio.wait_for(followup_entered.wait(), 2)
        await self.device.wait(lambda: self.device.usage_count() == 1)
        playback = self.session.current_playback_id
        owner = self.backend._turn_task
        self.assertIsNotNone(self.session.current_response)
        self.assertEqual(self.session.current_response, self.backend._active_response_id)
        await self.session.handle_barge_in({"phase": "candidate", "candidate_id": 7, "playback_id": playback, "played_samples": 0})
        await self.session.handle_barge_in({"phase": "confirmed", "candidate_id": 7, "playback_id": playback, "played_samples": 0})
        await self.wait_drain()
        release.set()
        await self.cloud_barrier()
        self.assertTrue(owner.done())
        self.assertTrue(self.session.playback.empty())
        self.assertFalse(self.session.response_completed)
        self.assertIsNone(self.session.current_chat_turn)
        self.assertIsNone(self.session.current_response)
        self.assertEqual(len(self.device.tts("start")), 1)
        self.assertEqual(len(self.device.tts("stop")), 1)

    async def test_fast_tool_followup_preserves_buffered_prefix_in_one_playback(self):
        self.session.browser = False
        self.session.raw_pcm = True
        async def tool_round(_messages):
            yield _Chunk(content="我来查询。")
            yield _Chunk(tool_delta=[{"index": 0, "id": "offline-call", "function": {"name": "list_tasks", "arguments": "{}"}}])

        self.scripts = [tool_round, self.text("查询完成。")]
        self.session.execute_tool = AsyncMock(return_value={"tasks": []})
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("查一下待办")
        await self.device.wait(lambda: self.device.usage_count() == 2)
        await self.wait_drain()
        initial_playback = self.device.tts("start")[0]["playback_id"]
        self.launch(self.session.play_output())
        await self.device.wait(lambda: len(self.device.tts("stop")) >= 1)
        self.assertEqual(self.device.audio, [(initial_playback, b"\x01\x00" * 48), (initial_playback, b"\x02\x00" * 48)], "快工具续轮不能覆盖前段PCM或在中间关闭播放")
        self.assertEqual([item["playback_id"] for item in self.device.tts("stop")], [initial_playback])
        self.assertEqual(len(self.device.tts("start")), 1)
        self.assertFalse(any(item.get("type") == "interrupt" for item in self.device.json))

    async def test_interrupt_waits_inflight_pcm_then_blocks_late_old_audio(self):
        unfinished, sending, release = asyncio.Event(), asyncio.Event(), asyncio.Event()
        self.scripts = [self.text("旧回答。", after=unfinished), self.text("新回答。")]
        original_send = self.device.send_bytes

        async def blocked_send(payload):
            if payload == b"\x01\x00" * 48:
                sending.set()
                await release.wait()
            await original_send(payload)

        self.device.send_bytes = blocked_send
        self.launch(self.session.cloud_output())
        self.launch(self.session.play_output())
        try:
            await self.backend.send_user_text("第一问")
            await asyncio.wait_for(sending.wait(), 2)
            old_playback = self.session.current_playback_id
            await self.backend.send_user_text("第二问")
            await self.backend_event(lambda event: event["type"] == "conversation.user_turn.done" and event["status"] == "completed")
            self.assertEqual(self.device.tts("stop"), [])
            release.set()
            await self.device.wait(lambda: len(self.device.tts("start")) == 2)
            new_playback = self.session.current_playback_id
            await self.device.wait(lambda: any(item.get("playback_id") == new_playback for item in self.device.tts("stop")))
            old_audio = self.device.trace.index((old_playback, b"\x01\x00" * 48))
            interrupt = next(index for index, item in enumerate(self.device.trace) if isinstance(item, dict) and item.get("type") == "interrupt" and item.get("playback_id") == old_playback)
            new_audio = self.device.trace.index((new_playback, b"\x02\x00" * 48))
            self.assertLess(old_audio, interrupt)
            self.assertLess(interrupt, new_audio)
            self.assertEqual(self.device.audio, [(old_playback, b"\x01\x00" * 48), (new_playback, b"\x02\x00" * 48)])
        finally:
            release.set()

    async def test_confirmed_barge_after_cloud_completion_does_not_reuse_cancelled_playback(self):
        self.scripts = [self.text("旧回答。"), self.text("新回答。")]
        self.launch(self.session.cloud_output())
        await self.backend.send_user_text("第一问")
        await self.device.wait(lambda: self.device.usage_count() == 1)
        await self.wait_drain()
        old_playback = self.session.current_playback_id
        await self.session.handle_barge_in({"phase": "candidate", "candidate_id": 9, "playback_id": old_playback, "played_samples": 0})
        await self.session.handle_barge_in({"phase": "confirmed", "candidate_id": 9, "playback_id": old_playback, "played_samples": 0})
        await self.backend.send_user_text("第二问")
        await self.device.wait(lambda: self.device.usage_count() == 2)
        await self.wait_drain()
        new_playback = self.session.current_playback_id
        self.assertNotEqual(old_playback, new_playback)
        self.launch(self.session.play_output())
        await self.device.wait(lambda: any(item.get("playback_id") == new_playback for item in self.device.tts("stop")))
        self.assertEqual(self.device.audio, [(new_playback, b"\x02\x00" * 48)])


if __name__ == "__main__":
    unittest.main()

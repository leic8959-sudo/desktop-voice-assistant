"""媒体语音授权与最终回答交接回归；不调用真实音频或云服务。"""

import asyncio
import json
import time
import unittest
from types import SimpleNamespace
from unittest.mock import AsyncMock

from assistant_runtime import RUNTIME_KEY
from chat_pipeline import ChatBackend, _Chunk
from gateway import VoiceSession
from src.assistant_app.integrations.music_assistant import MusicAssistantError


class MediaGatewayTests(unittest.IsolatedAsyncioTestCase):
    def session(self, utterance):
        media = SimpleNamespace(invoke=AsyncMock(return_value={"status": "scheduled"}), voice_response_finished=AsyncMock(return_value=True))
        subscription = SimpleNamespace(tool_schemas=[{"name": "subscription_action"}, {"name": "subscribe"}], invoke=AsyncMock(return_value={"status": "saved"}))
        session = object.__new__(VoiceSession)
        session.app = {RUNTIME_KEY: SimpleNamespace(media_service=media, capability_services=[subscription])}
        session.last_user_text = utterance
        session.backend = None
        return session, media, subscription

    async def test_rain_and_podcast_requests_receive_verified_user_authorization(self):
        for utterance, name, arguments in [("播放雨声半小时", "media_play", {"source": "rain", "minutes": 30}), ("继续上次的播客", "media_resume_podcast", {})]:
            session, media, _ = self.session(utterance)
            self.assertEqual((await session._execute_tool(name, arguments))["status"], "scheduled")
            self.assertTrue(media.invoke.call_args.kwargs["voice_authorized"])
            self.assertIs(media.invoke.call_args.kwargs["voice_session"], session)

    async def test_missing_user_context_cannot_change_media_or_subscriptions(self):
        for name, arguments in (("media_play", {"source": "rain"}), ("subscription_action", {"action": "delete"}), ("subscribe", {"kind": "mail"})):
            session, media, subscription = self.session("")
            with self.assertRaises(PermissionError):
                await session._execute_tool(name, arguments)
            media.invoke.assert_not_awaited()
            subscription.invoke.assert_not_awaited()

    async def test_subscription_action_matches_actual_requested_action(self):
        session, _, subscription = self.session("暂停指定邮件通知")
        await session._execute_tool("subscription_action", {"action": "pause"})
        subscription.invoke.assert_awaited_once()

    async def test_known_preview_rejection_reaches_voice_tool_result(self):
        session, _, _ = self.session("来一首晴天")
        session.session_id = "preview-regression"
        session.generation = 1
        session.accepted_input_epoch = 0
        session.tools_paused = False
        session.tool_calls = {}
        session.executing_tools = 0
        session.state = AsyncMock()
        session.execute_tool = AsyncMock(side_effect=MusicAssistantError("当前仅有试听音源，无法按整曲播放"))
        session.backend = SimpleNamespace(send_tool_output=AsyncMock(), request_response=AsyncMock())
        await session.process_tools([{"name": "music_play", "call_id": "preview-call", "arguments": "{}"}], 1, response_id='offline-response', input_epoch=session.accepted_input_epoch)
        output = json.loads(session.backend.send_tool_output.await_args.args[1])
        self.assertEqual(output["status"], "unavailable")
        self.assertIn("试听", output["error"])

    async def test_internet_song_requests_accept_natural_words_and_selected_candidate(self):
        for utterance in ("原版", "随便来一个放就行了。", "Play whichever version fits", "给我放晴天", "我想听周杰伦的歌", "第一首", "就这首吧", "小燕子，帮我来一首周杰伦的晴天。", "小燕，小燕子，帮我来一首送别。", "呃，来首朴树的吧。"):
            with self.subTest(utterance=utterance):
                session, _, _ = self.session(utterance)
                music = SimpleNamespace(invoke=AsyncMock(return_value={"status": "scheduled"}))
                session.app[RUNTIME_KEY].device_music = music
                await session._execute_tool("music_play", {"selection_id": "server-issued-candidate"})
                self.assertTrue(music.invoke.await_args.kwargs["voice_authorized"])
    async def test_watchdog_waits_until_final_response_and_audio_are_drained(self):
        session, media, _ = self.session("播放雨声半小时")
        session.guard_tripped = False
        session.manual_pending_commit = None
        session.response_completed = False
        session.speaking = False
        session.current_response = None
        session.executing_tools = 0
        session.playback = asyncio.Queue()
        session.pending_output = None
        session.created = session.last_activity = time.monotonic()
        session.config = {"session_seconds": 600, "idle_seconds": 600}
        session.state = AsyncMock()
        task = asyncio.create_task(session.watchdog())
        try:
            await asyncio.sleep(1.05)
            media.voice_response_finished.assert_not_awaited()
            session.response_completed = True
            session.playback.put_nowait((0, "pending-audio"))
            await asyncio.sleep(1.05)
            media.voice_response_finished.assert_not_awaited()
            session.playback.get_nowait()
            await asyncio.wait_for(task, timeout=2)
            media.voice_response_finished.assert_awaited_once_with(session)
            self.assertEqual(session.state.await_args.args[0], "idle")
        finally:
            if not task.done():
                task.cancel()
                await asyncio.gather(task, return_exceptions=True)

    async def test_chat_text_plus_tool_round_is_not_final_media_handoff(self):
        backend = ChatBackend(SimpleNamespace(config={}, session_id="synthetic-media"), "qwen_chat")
        backend._response_sequence = 0
        backend._cancelled = set()
        backend._events = asyncio.Queue()
        async def chunks(_messages):
            yield _Chunk(content="我来安排播放。")
            yield _Chunk(tool_delta=[{"index": 0, "id": "call-media", "function": {"name": "media_play", "arguments": '{"source":"rain","minutes":30}'}}])
        async def silent_tts(_response_id, text_queue):
            while await text_queue.get() is not None:
                pass
        backend._request = chunks
        backend._tts_turn = silent_tts
        _, calls = await backend._chat_round([])
        self.assertTrue(calls)
        events = []
        while not backend._events.empty():
            events.append(backend._events.get_nowait())
        done = next(item for item in events if item["type"] == "response.done")
        self.assertFalse(done["response"]["handoff_final"])


if __name__ == "__main__":
    unittest.main()

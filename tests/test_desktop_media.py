"""媒体本地模拟：播放器全替身，不访问 Windows 音频或第三方账户。"""

import json
import tempfile
import time
import unittest
import wave
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch

from src.assistant_app.application import desktop_actions
from src.assistant_app.application.desktop_actions import DesktopActionsService, MAX_QUEUE


class DesktopMediaTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.player_patch = patch.object(desktop_actions, "WindowsMediaPlayer", side_effect=self.mock_player)
        self.player_patch.start()
        self.addCleanup(self.player_patch.stop)
        self.runtime = SimpleNamespace(voice_sessions=[], proactive_speech=None)
        self.service = self.make_service()
        self.podcast_id = "a" * 32
        self.music_id = "b" * 32
        self.service.media_root.mkdir(parents=True)
        for identifier in (self.podcast_id, self.music_id):
            with wave.open(str(self.service.media_root / (identifier + ".wav")), "wb") as audio:
                audio.setnchannels(1)
                audio.setsampwidth(2)
                audio.setframerate(8000)
                audio.writeframes(b"\0\0" * 8000 * 60)
        self.service._catalog = {self.podcast_id: {"title": "本地播客第1集", "kind": "podcast"}, self.music_id: {"title": "自备音乐", "kind": "music"}}
        self.service._noise_paths["rain"] = Path(self.temporary.name) / "mock-rain.wav"
        self.service._noise_paths["white_noise"] = Path(self.temporary.name) / "mock-white.wav"

    def mock_player(self):
        player = Mock()
        player.opened = False
        player.position_ms = 0

        def open_audio(path, position_ms=0):
            player.opened = True
            player.position_ms = position_ms
            return 60000

        def close_audio():
            player.opened = False

        player.open.side_effect = open_audio
        player.close.side_effect = close_audio
        player.position.side_effect = lambda: player.position_ms
        player.mode.side_effect = lambda: "playing" if player.opened else "closed"
        player.restart.side_effect = lambda: setattr(player, "position_ms", 0)
        return player

    def make_service(self):
        service = DesktopActionsService({"runtime_dir": self.temporary.name}, None, self.runtime, None)
        service.supported = True
        service._closed = False
        return service

    async def cycle(self, service=None):
        service = service or self.service

        async def finish_cycle(_delay):
            service._closed = True

        with patch.object(desktop_actions.asyncio, "sleep", new=finish_cycle):
            await service._loop()
        service._closed = False

    async def play(self, source=None, minutes=30):
        return await self.service.execute_confirmed("media_play", {"source": source or self.podcast_id, "minutes": minutes}, confirmation_verified=True)

    async def test_rain_source_and_timer_are_explicit_and_bounded(self):
        self.assertEqual(self.service.normalize("media_play", {"source": "rain", "minutes": 30}), {"source": "rain", "minutes": 30})
        self.assertEqual(next(item for item in self.service.sources() if item["id"] == "rain")["kind"], "noise")
        for arguments in ({"source": "https://example.com/rain.mp3"}, {"source": "rain", "minutes": 241}, {"source": "rain", "minutes": True}):
            with self.assertRaises(ValueError):
                self.service.normalize("media_play", arguments)
        path = self.service._noise("rain")
        with wave.open(str(path), "rb") as audio:
            self.assertEqual(audio.getnframes(), 24000 * 8)
            self.assertEqual(audio.getsampwidth(), 2)
            self.assertNotEqual(audio.readframes(audio.getnframes()), b"\0" * (24000 * 8 * 2))

    async def test_voice_waits_for_final_response_session_close_and_other_voices(self):
        session = SimpleNamespace(closed=False)
        other = SimpleNamespace(closed=False)
        self.runtime.voice_sessions = [session, other]
        result = await self.service.invoke("media_play", {"source": "rain", "minutes": 30}, voice_authorized=True, voice_session=session)
        self.assertEqual(result["status"], "scheduled")
        await self.cycle()
        self.service._player.open.assert_not_called()
        self.assertTrue(await self.service.voice_response_finished(session))
        await self.cycle()
        self.service._player.open.assert_not_called()
        session.closed = True
        await self.cycle()
        self.service._player.open.assert_not_called()
        other.closed = True
        await self.cycle()
        self.service._player.open.assert_called_once_with(self.service._noise_paths["rain"], 0)
        self.assertEqual(self.service._state, "playing")
        self.assertIsNone(self.service._pending)

    async def test_cancel_and_timeout_never_start_audio(self):
        session = SimpleNamespace(closed=False)
        for cancelled in (True, False):
            await self.service.invoke("media_play", {"source": "rain"}, voice_authorized=True, voice_session=session)
            await self.service.voice_response_finished(session)
            if cancelled:
                await self.service.cancel_voice_request(session)
            else:
                self.service._pending["expires"] = time.monotonic() - 1
            session.closed = True
            await self.cycle()
            self.assertIsNone(self.service._pending)
        self.service._player.open.assert_not_called()
        self.assertIn("超时", self.service._error)

    async def test_wake_pauses_and_resume_seeks_to_reported_position(self):
        await self.play()
        self.service._player.position_ms = 12345
        await self.service.before_voice_session()
        self.assertEqual(self.service._state, "paused_voice")
        self.assertEqual(self.service._position_ms, 12345)
        self.assertEqual(self.service._last_podcast["position_ms"], 12345)
        self.service._player.close.assert_called_once()
        self.service._voice_guard_until = 0
        result = await self.service.execute_confirmed("media_resume", {}, confirmation_verified=True)
        self.assertEqual(result["status"], "playing")
        self.service._player.open.assert_called_with(self.service.media_root / (self.podcast_id + ".wav"), 12345)

    async def test_timer_expiry_keeps_podcast_bookmark_and_renews_budget(self):
        await self.play(minutes=1)
        self.service._player.position_ms = 25000
        self.service._deadline = time.monotonic() - 1
        await self.cycle()
        self.assertEqual(self.service._state, "stopped")
        self.assertFalse(self.service._last_podcast["completed"])
        self.assertEqual(self.service._last_podcast["remaining_s"], 0)
        result = await self.service.execute_confirmed("media_resume_podcast", {}, confirmation_verified=True)
        self.assertEqual(result["status"], "playing")
        self.assertGreater(self.service._remaining, 59)
        self.service._player.open.assert_called_with(self.service.media_root / (self.podcast_id + ".wav"), 25000)

    async def test_restart_restores_catalog_bounded_queue_and_bookmark_paused(self):
        await self.play()
        self.service._player.position_ms = 12345
        for _ in range(MAX_QUEUE):
            await self.service.execute_confirmed("media_queue", {"source": self.music_id, "minutes": 10}, confirmation_verified=True)
        with self.assertRaises(ValueError):
            await self.service.execute_confirmed("media_queue", {"source": self.music_id}, confirmation_verified=True)
        await self.service.before_voice_session()
        restored = self.make_service()
        restored._load_state()
        self.assertEqual(restored._state, "paused")
        self.assertEqual(restored._queue, self.service._queue)
        self.assertEqual(len(restored._queue), MAX_QUEUE)
        self.assertEqual(restored._catalog, self.service._catalog)
        self.assertEqual(restored._last_podcast["position_ms"], 12345)
        self.assertEqual(restored._position_ms, 12345)
        restored._player.open.assert_not_called()
        self.assertLess(restored.state_path.stat().st_size, 65536)

    async def test_voice_without_trusted_authorization_is_blocked(self):
        result = await self.service.invoke("media_play", {"source": "rain"}, subject="voice")
        self.assertEqual(result["status"], "confirmation_required")
        result = await self.service.invoke("desktop_lock", {}, subject="voice")
        self.assertEqual(result["status"], "confirmation_required")
        self.service._player.open.assert_not_called()
        self.assertIsNone(self.service._pending)

    async def test_invalid_persisted_queue_is_not_loaded(self):
        await self.service._save_state()
        data = json.loads(self.service.state_path.read_text(encoding="utf-8"))
        data["queue"] = [{"source": "rain", "minutes": 30}] * (MAX_QUEUE + 1)
        self.service.state_path.write_text(json.dumps(data), encoding="utf-8")
        restored = self.make_service()
        with self.assertLogs(desktop_actions.LOGGER, level="ERROR"):
            restored._load_state()
        self.assertEqual(restored._queue, [])
        self.assertIn("无效", restored._error)
        restored._player.open.assert_not_called()

    async def test_upload_registration_failure_rolls_back_metadata(self):
        with patch.object(self.service, "_save_state", new=AsyncMock(side_effect=OSError("模拟磁盘写失败"))):
            with self.assertRaises(OSError):
                await self.service.register_upload("c" * 32, {"title": "不能保存的播客", "kind": "podcast"})
        self.assertNotIn("c" * 32, self.service._catalog)


if __name__ == "__main__":
    unittest.main()

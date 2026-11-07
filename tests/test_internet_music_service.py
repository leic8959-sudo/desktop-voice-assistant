"""互联网音乐编排隔离模拟；MA、媒体流和设备均为替身。"""

import asyncio
import json
import tempfile
import time
import unittest
from datetime import datetime, timedelta, timezone
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, patch

from src.assistant_app.application.internet_music import InternetMusicService
from src.assistant_app.integrations.music_assistant import MusicAssistantClient, MusicAssistantError, PLAYER_ID
from tests.test_music_assistant import FakeHttp, FakeVault


class FakeMusicClient:
    def __init__(self, *_arguments):
        self.start = AsyncMock()
        self.close = AsyncMock()
        self.stop = AsyncMock(return_value={"accepted": True})
        self.report = AsyncMock(return_value={"accepted": True})
        self.configure = AsyncMock(return_value={"configured": True})
        self.search = AsyncMock(return_value={"items": []})
        self.resolve_stream = AsyncMock(side_effect=self.resolve)
        self.lease = None

    async def resolve(self, ref, position_s=0):
        self.lease = "lease-" + ref["item_id"]
        return {"kind": "http", "trusted_by": "music_assistant", "url": "http://127.0.0.1:8097/single/fake.wav", "base_url": "http://127.0.0.1:8097", "session_id": self.lease, "source_position_s": int(position_s), "seek_applied": True}

    def status(self):
        return {"configured": True}


class FakeMediaStream:
    def __init__(self, *_arguments):
        self.value = {"state": "stopped", "playback_id": None, "device_position_s": None}
        self.calls = []
        self.accept_event = True
        self.pause_event = None

    def status(self):
        return dict(self.value)

    async def start(self, device, source, position_s=0, playback_id=None):
        self.calls.append(("start", source, position_s, playback_id))
        self.value = {"state": "starting", "playback_id": playback_id, "device_position_s": None}
        return self.status()

    async def pause(self):
        self.calls.append(("pause",))
        if self.pause_event is not None:
            await self.pause_event()
        self.value["state"] = "paused"
        return self.status()

    async def stop(self):
        self.calls.append(("stop",))
        self.value["state"] = "stopped"
        return self.status()

    async def close(self):
        await self.stop()

    async def on_device_event(self, _device, event):
        if not self.accept_event or event.get("playback_id") != self.value.get("playback_id"):
            return False
        self.value["state"] = {"start": "playing", "pause": "paused", "stop": "stopped", "ended": "ended", "error": "error", "progress": self.value["state"]}.get(event.get("state"), self.value["state"])
        if "played_samples" in event:
            self.value["device_position_s"] = event["played_samples"] / 24000
        return True


class InternetMusicServiceTests(unittest.IsolatedAsyncioTestCase):
    async def test_selection_is_real_fresh_and_scoped_to_searching_voice_session(self):
        self.client.search.return_value = {"items": [{"title": "晴天", "ref": {"item_id": "original"}, "album": "叶惠美"}]}
        owner = SimpleNamespace(session_id="owner", closed=False)
        other = SimpleNamespace(session_id="other", closed=False)
        result = await self.service.invoke("music_search", {"query": "晴天"}, voice_session=owner)
        identity = result["items"][0]["selection_id"]
        self.service._request = AsyncMock(return_value={"status": "scheduled"})
        with self.assertRaises(PermissionError):
            await self.service.invoke("music_play", {"selection_id": identity}, voice_authorized=True, voice_session=other)
        with self.assertRaises(ValueError):
            await self.service.invoke("music_play", {"selection_id": "model-invented"}, voice_authorized=True, voice_session=owner)
        with self.assertRaises(ValueError):
            await self.service.invoke("music_play", {"selection_id": identity, "url": "https://untrusted.invalid"}, voice_authorized=True, voice_session=owner)
        self.assertEqual((await self.service.invoke("music_play", {"selection_id": identity}, voice_authorized=True, voice_session=owner))["status"], "scheduled")
        self.service.selections[identity]["expires"] = time.monotonic() - 1
        with self.assertRaises(ValueError):
            await self.service.invoke("music_play", {"selection_id": identity}, voice_authorized=True, voice_session=owner)
        self.service._request.assert_awaited_once()

    async def asyncSetUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.device = SimpleNamespace(socket=SimpleNamespace(closed=False), media_stream_supported=True)
        self.runtime = SimpleNamespace(config={"runtime_dir": str(self.directory)}, native_devices=[self.device], voice_sessions=[], voice_lock=asyncio.Lock(), proactive_speech=None)
        self.store = SimpleNamespace(path=str(self.directory / "store.sqlite"))
        self.client_patch = patch("src.assistant_app.application.internet_music.MusicAssistantClient", FakeMusicClient)
        self.stream_patch = patch("src.assistant_app.application.internet_music.DeviceMediaStream", FakeMediaStream)
        self.client_patch.start()
        self.stream_patch.start()
        self.service = InternetMusicService(self.runtime.config, self.store, self.runtime, None)
        self.service.closed = False
        self.client, self.stream = self.service.client, self.service.stream
        self.tasks = []

    async def asyncTearDown(self):
        self.service.closed = True
        for task in self.tasks:
            task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        self.stream_patch.stop()
        self.client_patch.stop()
        self.temporary.cleanup()

    @staticmethod
    def track(identity="one", duration=200):
        return {"source": "music_assistant", "ref": {"uri": "qqmusic://track/" + identity, "provider": "qqmusic", "item_id": identity}, "title": "歌曲-" + identity, "duration_s": duration, "artists": ["歌手"]}

    async def wait_for(self, predicate, timeout=2):
        async with asyncio.timeout(timeout):
            while not predicate():
                await asyncio.sleep(0.01)

    def launch(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    async def test_seek_duration_and_reference_persistence(self):
        track = self.track()
        result = await self.service._request(track, position_s=41.25)
        self.assertEqual(result["state"], "starting")
        self.client.resolve_stream.assert_awaited_once_with(track["ref"], position_s=41.25)
        _kind, source, position, _identity = self.stream.calls[-1]
        self.assertEqual(position, 41.25)
        self.assertEqual(source["source_position_s"], 41)
        self.assertEqual(source["duration_s"], 200)
        self.assertEqual(source["title"], track["title"])
        saved = self.service.state_path.read_text(encoding="utf-8")
        self.assertNotIn("http://", saved)
        self.assertNotIn("lease-", saved)
        self.assertFalse(self.service.bookmark["position_confirmed"])

    async def test_start_ack_report_is_async_and_pause_does_not_deadlock(self):
        await self.service._request(self.track())
        playback = self.stream.status()["playback_id"]
        report_started, release = asyncio.Event(), asyncio.Event()

        async def delayed_report(*_arguments, **_keywords):
            report_started.set()
            await release.wait()

        self.client.report.side_effect = delayed_report
        self.launch(self.service._report_loop())
        await asyncio.wait_for(self.service.on_device_event(self.device, {"state": "start", "playback_id": playback, "played_samples": 0}), 0.2)
        await asyncio.wait_for(report_started.wait(), 0.2)

        async def pause_receipt():
            await self.service.on_device_event(self.device, {"state": "pause", "playback_id": playback, "played_samples": 36000})

        self.stream.pause_event = pause_receipt
        result = await asyncio.wait_for(self.service.action("pause"), 0.5)
        self.assertEqual(result["state"], "paused")
        self.assertEqual(self.service.bookmark, {"position_s": 1.5, "position_confirmed": True})
        release.set()
        await asyncio.wait_for(self.service.reports.join(), 0.5)

    async def test_voice_handoff_waits_ready_and_closed_session(self):
        session = SimpleNamespace(closed=False)
        self.runtime.voice_sessions = [session]
        result = await self.service._request(self.track(), session)
        self.assertEqual(result["status"], "scheduled")
        self.assertEqual(self.client.resolve_stream.await_count, 1)
        self.client.stop.assert_awaited_once_with(session_id="lease-one")
        self.assertFalse(any(call[0] == "start" for call in self.stream.calls))
        worker = self.launch(self.service._loop())
        await asyncio.sleep(0.05)
        self.assertEqual(self.client.resolve_stream.await_count, 1)
        self.assertTrue(await self.service.voice_response_finished(session))
        await asyncio.sleep(0.05)
        self.assertEqual(self.client.resolve_stream.await_count, 1)
        session.closed = True
        await self.wait_for(lambda: self.client.resolve_stream.await_count == 2)
        self.assertIsNone(self.service.pending)
        worker.cancel()

    async def test_preview_handoff_announces_preview_and_uses_30_second_budget(self):
        async def resolve(ref, position_s=0):
            return {**await self.client.resolve(ref, position_s), "playback_access": "preview", "duration_s": 30}

        self.client.resolve_stream.side_effect = resolve
        session = SimpleNamespace(closed=False)
        result = await self.service._request(self.track(duration=269), session)
        self.assertEqual(result["playback_access"], "preview")
        self.assertIn("试听", result["message"])
        self.assertEqual(self.service.pending["track"]["duration_s"], 269)
        await self.service.voice_response_finished(session)
        session.closed = True
        self.launch(self.service._loop())
        await self.wait_for(lambda: any(call[0] == "start" for call in self.stream.calls))
        source = next(call[1] for call in self.stream.calls if call[0] == "start")
        self.assertEqual(source["duration_s"], 30)
        self.assertIn("试听", source["title"])
        self.assertEqual(self.service.status()["current"]["playback_access"], "preview")
        self.assertEqual(self.service.current["duration_s"], 30)

    async def test_cancel_voice_request_and_expiry_never_start(self):
        session = SimpleNamespace(closed=False)
        await self.service._request(self.track(), session)
        await self.service.cancel_voice_request(session)
        self.assertIsNone(self.service.pending)
        await self.service._request(self.track(), session)
        self.service.pending["expires"] = time.monotonic() - 1
        self.launch(self.service._loop())
        await self.wait_for(lambda: self.service.pending is None)
        self.assertEqual(self.client.resolve_stream.await_count, 2)
        self.assertEqual(self.client.stop.await_count, 2)
        self.assertFalse(any(call[0] == "start" for call in self.stream.calls))

    async def test_preview_failure_returns_to_active_voice_without_scheduling(self):
        self.client.resolve_stream.side_effect = MusicAssistantError("音乐服务仅返回试听音源")
        session = SimpleNamespace(closed=False)
        with self.assertRaisesRegex(MusicAssistantError, "仅返回试听"):
            await self.service._request(self.track(), session)
        self.assertFalse(session.closed)
        self.assertIsNone(self.service.pending)
        self.assertEqual(self.service.last_error, "音乐服务仅返回试听音源")
        self.assertFalse(any(call[0] == "start" for call in self.stream.calls))

    async def test_new_user_input_during_preflight_cancels_and_releases_source(self):
        resolving, release = asyncio.Event(), asyncio.Event()

        async def resolve(ref, position_s=0):
            resolving.set()
            await release.wait()
            return await self.client.resolve(ref, position_s)

        self.client.resolve_stream.side_effect = resolve
        session = SimpleNamespace(closed=False)
        request = self.launch(self.service._request(self.track(), session))
        await resolving.wait()
        await self.service.cancel_voice_request(session)
        release.set()
        self.assertEqual((await request)["status"], "cancelled")
        self.assertIsNone(self.service.pending)
        self.client.stop.assert_awaited_once_with(session_id="lease-one")
        self.assertFalse(any(call[0] == "start" for call in self.stream.calls))

    async def test_cancelled_preflight_stop_still_clears_pending(self):
        entered = asyncio.Event()

        async def stop(**kwargs):
            entered.set()
            await asyncio.Future()

        self.client.stop.side_effect = stop
        request = self.launch(self.service._request(self.track(), SimpleNamespace(closed=False)))
        await entered.wait()
        request.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await request
        self.assertIsNone(self.service.pending)

    async def test_handoff_failure_reports_actual_reason_to_device(self):
        session = SimpleNamespace(closed=False)
        await self.service._request(self.track(), session)
        await self.service.voice_response_finished(session)
        session.closed = True
        self.device.send_json = AsyncMock()
        self.client.resolve_stream.side_effect = MusicAssistantError("该歌曲整曲权限已失效")
        self.launch(self.service._loop())
        await self.wait_for(lambda: self.device.send_json.await_count == 1)
        self.assertEqual(self.device.send_json.await_args.args[0]["message"], "该歌曲整曲权限已失效")
        self.assertIsNone(self.service.pending)

    async def test_superseded_handoff_failure_does_not_interrupt_new_request(self):
        session = SimpleNamespace(closed=False)
        await self.service._request(self.track(), session)
        await self.service.voice_response_finished(session)
        session.closed = True
        resolving, release = asyncio.Event(), asyncio.Event()
        self.device.send_json = AsyncMock()

        async def resolve(*args, **kwargs):
            resolving.set()
            await release.wait()
            raise MusicAssistantError("旧歌曲失败")

        self.client.resolve_stream.side_effect = resolve
        self.launch(self.service._loop())
        await resolving.wait()
        self.service.generation += 1
        self.service.last_error = "新请求状态"
        release.set()
        await asyncio.sleep(0.05)
        self.device.send_json.assert_not_awaited()
        self.assertEqual(self.service.last_error, "新请求状态")

    async def test_bookmark_restart_does_not_auto_play(self):
        self.service.current = self.track()
        self.service.bookmark = {"position_s": 42.5, "position_confirmed": True}
        self.service.queue = [self.track("two")]
        await self.service._save()
        restored = InternetMusicService(self.runtime.config, self.store, self.runtime, None)
        restored._load()
        self.assertEqual(restored.bookmark["position_s"], 42.5)
        restored.stream.calls.clear()
        self.assertEqual(restored.stream.status()["state"], "stopped")
        restored.client.resolve_stream.assert_not_awaited()

    async def test_rejected_engine_receipt_is_not_reported_to_platform(self):
        await self.service._request(self.track())
        self.stream.accept_event = False
        await self.service.on_device_event(self.device, {"state": "start", "playback_id": self.stream.status()["playback_id"], "played_samples": 99999999})
        self.assertTrue(self.service.reports.empty())

    async def test_resolve_cancelled_by_voice_releases_its_platform_lease(self):
        resolving, release = asyncio.Event(), asyncio.Event()

        async def resolve(ref, position_s=0):
            resolving.set()
            await release.wait()
            return await self.client.resolve(ref, position_s)

        self.client.resolve_stream.side_effect = resolve
        task = self.launch(self.service._request(self.track()))
        await resolving.wait()
        await self.service.before_voice_session()
        release.set()
        result = await task
        self.assertEqual(result["status"], "cancelled")
        self.assertFalse(any(call[0] == "start" for call in self.stream.calls))
        self.assertTrue(self.client.stop.await_count or self.client.report.await_count, "取消后未释放已解析的MA租约")

    async def test_persisted_confirmed_bookmark_requires_actual_finite_position(self):
        value = {"version": 1, "expires_at": (datetime.now(timezone.utc) + timedelta(days=1)).isoformat(), "current": self.track(), "queue": [], "bookmark": {"position_s": None, "position_confirmed": True}}
        self.service.state_path.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaises(ValueError):
            self.service._load()

    async def test_newer_request_wins_when_old_resolution_finishes_later(self):
        first_resolving, release_first = asyncio.Event(), asyncio.Event()

        async def resolve(ref, position_s=0):
            if ref["item_id"] == "old":
                first_resolving.set()
                await release_first.wait()
            return await self.client.resolve(ref, position_s)

        self.client.resolve_stream.side_effect = resolve
        first = self.launch(self.service._request(self.track("old")))
        await first_resolving.wait()
        second = self.launch(self.service._request(self.track("new")))
        await self.wait_for(lambda: self.service.generation == 2)
        release_first.set()
        old_result, new_result = await asyncio.gather(first, second)
        self.assertEqual(old_result["status"], "cancelled")
        self.assertEqual(new_result["state"], "starting")
        starts = [call for call in self.stream.calls if call[0] == "start"]
        self.assertEqual(len(starts), 1)
        self.assertEqual(starts[0][1]["session_id"], "lease-new")
        self.assertEqual(self.service.current["ref"]["item_id"], "new")

    async def test_device_error_stop_and_end_have_distinct_platform_reports(self):
        await self.service._request(self.track())
        playback = self.stream.status()["playback_id"]
        for phase, expected in (("error", "error"), ("stop", "stopped"), ("ended", "finished")):
            with self.subTest(phase=phase):
                await self.service.on_device_event(self.device, {"state": phase, "playback_id": playback, "played_samples": 24000})
                item = self.service.reports.get_nowait()
                self.service.reports.task_done()
                self.assertEqual(item["state"], expected)
                self.assertEqual(item["session_id"], "lease-one")

    async def test_stale_playback_report_is_not_sent_for_new_track(self):
        await self.service._request(self.track("one"))
        old_playback = self.stream.status()["playback_id"]
        await self.service.on_device_event(self.device, {"state": "start", "playback_id": old_playback, "played_samples": 0})
        await self.service._request(self.track("two"))
        self.launch(self.service._report_loop())
        await asyncio.wait_for(self.service.reports.join(), 0.5)
        self.client.report.assert_not_awaited()

    async def test_generated_noise_uses_device_source_and_preserves_loop_budget(self):
        path = self.directory / "noise.wav"
        self.runtime.media_service = SimpleNamespace(_noise=lambda _source: path, before_voice_session=AsyncMock())
        await self.service.action("noise", {"source": "rain", "minutes": 30})
        self.client.resolve_stream.assert_not_awaited()
        _kind, source, _position, _playback = self.stream.calls[-1]
        self.assertEqual(source["kind"], "trusted_file")
        self.assertTrue(source["loop"])
        self.assertEqual(source["duration_s"], 1800)
        self.assertIsNone(self.service.platform_session)

    async def test_switching_platform_track_to_noise_releases_old_lease(self):
        await self.service._request(self.track())
        self.runtime.media_service = SimpleNamespace(_noise=lambda _source: self.directory / "noise.wav", before_voice_session=AsyncMock())
        await self.service.action("noise", {"source": "white_noise", "minutes": 1})
        self.assertIsNone(self.service.platform_session)
        self.assertTrue(self.client.stop.await_count or self.client.report.await_count, "MA切换本地音源时未释放旧租约")
        if self.client.stop.await_count:
            self.assertEqual(self.client.stop.await_args.kwargs.get("session_id"), "lease-one")

    async def test_ended_track_advances_queue_once_but_error_does_not(self):
        await self.service._request(self.track("one"))
        self.service.queue = [self.track("two")]
        self.stream.value["state"] = "error"
        self.launch(self.service._loop())
        await asyncio.sleep(0.05)
        self.assertEqual(self.service.current["ref"]["item_id"], "one")
        self.assertEqual(len(self.service.queue), 1)
        self.stream.value["state"] = "ended"
        await self.wait_for(lambda: self.service.current is not None and self.service.current["ref"]["item_id"] == "two")
        self.assertEqual(self.service.queue, [])
        self.assertEqual(self.client.resolve_stream.await_count, 2)
        await asyncio.sleep(0.05)
        self.assertEqual(self.client.resolve_stream.await_count, 2)

    async def test_persistence_failure_after_start_silences_device(self):
        self.service._save = AsyncMock(side_effect=OSError("simulated full disk"))
        with self.assertRaises(OSError):
            await self.service._request(self.track())
        self.assertNotIn(self.stream.status()["state"], {"starting", "playing"}, "持久化失败后设备音流仍在运行")
        self.assertTrue(self.client.stop.await_count or self.client.report.await_count)


    async def test_stop_without_device_confirmation_reports_unknown(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]

        async def unconfirmed_stop():
            self.stream.value["state"] = "error"
            self.stream.value["error"] = "设备媒体停止确认失败，实际输出状态未知"
            return self.stream.status()

        self.stream.stop = unconfirmed_stop
        result = await self.service.action("stop")
        self.assertEqual(result["status"], "unknown")
        self.assertEqual(self.service.queue, [])
        self.assertIsNone(self.service.pending)
        self.client.stop.assert_awaited_once()

    async def test_platform_stop_failure_still_persists_queue_cancellation(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        await self.service._save()
        self.client.stop.side_effect = RuntimeError("synthetic platform stop failure")
        try:
            result = await self.service.action("stop")
        except RuntimeError:
            pass
        else:
            self.assertEqual(result["status"], "unknown")
        self.assertEqual(self.service.queue, [])
        self.assertIsNone(self.service.bookmark)
        persisted = json.loads(self.service.state_path.read_text(encoding="utf-8"))
        self.assertEqual(persisted["queue"], [])
        self.assertIsNone(persisted["bookmark"])

    async def test_confirmed_stop_clears_queue_and_preserves_success_contract(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        result = await self.service.action("stop")
        self.assertEqual(result["status"], "stopped")
        self.assertEqual(self.service.queue, [])
        self.assertIsNone(self.service.current)
        self.assertIsNone(self.service.bookmark)

    async def test_stop_save_failure_still_attempts_both_outputs_and_propagates(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        self.stream.calls.clear()
        self.service._save = AsyncMock(side_effect=OSError("synthetic stop disk failure"))
        with self.assertLogs("assistant.internet_music", level="ERROR"):
            with self.assertRaisesRegex(OSError, "synthetic stop disk failure"):
                await self.service.action("stop")
        self.assertIn(("stop",), self.stream.calls)
        self.client.stop.assert_awaited_once()
        self.service._save.assert_awaited_once()
        self.assertEqual(self.service.queue, [])
        self.assertIsNone(self.service.bookmark)
        self.assertIn("保存失败", self.service.last_error)

    async def test_device_stop_exception_still_releases_platform_and_persists(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        self.stream.stop = AsyncMock(side_effect=RuntimeError("synthetic private stop detail"))
        with self.assertLogs("assistant.internet_music", level="ERROR"):
            result = await self.service.action("stop")
        self.assertEqual(result["status"], "unknown")
        self.assertIn("device_stop_failed", result["reasons"])
        self.assertNotIn("synthetic private stop detail", str(result))
        self.client.stop.assert_awaited_once()
        self.assertIsNone(self.service.platform_session)
        persisted = json.loads(self.service.state_path.read_text(encoding="utf-8"))
        self.assertEqual(persisted["queue"], [])
        self.assertIsNone(persisted["current"])
        self.assertIsNone(persisted["bookmark"])

    async def test_cancelled_stop_caller_waits_for_persistence_and_output_cleanup(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        saving, release = asyncio.Event(), asyncio.Event()
        original_save = self.service._save

        async def save_after_release():
            saving.set()
            await release.wait()
            await original_save()

        self.service._save = AsyncMock(side_effect=save_after_release)
        task = self.launch(self.service.action("stop"))
        try:
            await asyncio.wait_for(saving.wait(), 1)
            task.cancel()
            cancellation_dispatched = asyncio.Event()
            asyncio.get_running_loop().call_soon(cancellation_dispatched.set)
            await asyncio.wait_for(cancellation_dispatched.wait(), 1)
            self.assertFalse(task.done())
            self.assertTrue(self.service.lock.locked())
            release.set()
            with self.assertRaises(asyncio.CancelledError):
                await asyncio.wait_for(task, 1)
            self.client.stop.assert_awaited_once()
            self.service._save.assert_awaited_once()
            self.assertEqual(self.stream.status()["state"], "stopped")
            self.assertFalse(self.service.lock.locked())
            persisted = json.loads(self.service.state_path.read_text(encoding="utf-8"))
            self.assertEqual(persisted["queue"], [])
            self.assertIsNone(persisted["bookmark"])
        finally:
            release.set()

    async def test_cancelled_persistence_still_attempts_stop_without_success(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        self.service._save = AsyncMock(side_effect=asyncio.CancelledError())
        self.stream.calls.clear()
        with self.assertRaises(asyncio.CancelledError):
            await self.service.action("stop")
        self.assertIn(("stop",), self.stream.calls)
        self.client.stop.assert_awaited_once()
        self.assertEqual(self.service.queue, [])
        self.assertIsNone(self.service.bookmark)
        self.assertIn("中断", self.service.last_error)

    async def test_cancelled_save_and_unconfirmed_device_do_not_claim_persistence(self):
        await self.service._request(self.track())
        self.service._save = AsyncMock(side_effect=asyncio.CancelledError())
        self.stream.stop = AsyncMock(return_value={"state": "error"})
        with self.assertRaises(asyncio.CancelledError):
            await self.service.action("stop")
        self.assertIn("保存未确认", self.service.last_error)
        self.assertNotIn("后续播放已取消", self.service.last_error)
        self.client.stop.assert_awaited_once()

    async def test_real_client_rejected_stop_reports_unknown_after_queue_persistence(self):
        await self.service._request(self.track())
        self.service.queue = [self.track("next")]
        http = FakeHttp()
        config = {**self.runtime.config, "music_assistant_base_url": "http://127.0.0.1:8095", "music_assistant_stream_base_url": "http://127.0.0.1:8097", "music_assistant_access_token": "offline-music-stop-token"}
        client = MusicAssistantClient(config, http, FakeVault())
        http.add({"url": f"http://127.0.0.1:8097/single/session_1/{PLAYER_ID}/queueitem_1/{PLAYER_ID}.flac", "lease": "lease-one", "source_position_s": 0})
        await client.resolve_stream(self.track()["ref"])
        self.service.client = client
        http.add({"accepted": False})
        with self.assertLogs("assistant.internet_music", level="ERROR"):
            result = await self.service.action("stop")
        self.assertEqual(result["status"], "unknown")
        self.assertIn("platform_stop_failed", result["reasons"])
        self.assertEqual(self.service.platform_session, "lease-one")
        self.assertEqual(client._lease, "lease-one")
        self.assertEqual(len(http.calls), 2)
        persisted = json.loads(self.service.state_path.read_text(encoding="utf-8"))
        self.assertEqual(persisted["queue"], [])
        self.assertIsNone(persisted["current"])
        self.assertIsNone(persisted["bookmark"])


if __name__ == "__main__":
    unittest.main()

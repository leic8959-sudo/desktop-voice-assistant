"""官方 MA 2.10.9 JSON/流合同的离线边界验证，不连接真实音源。"""

import asyncio
import copy
import json
import tempfile
import unittest

from src.assistant_app.integrations.music_assistant import MusicAssistantClient, MusicAssistantError, PLAYER_ID


TOKEN = "fake-ma-secret-not-a-real-account"
TRACK = {"uri": "qqmusic://track/song-1", "provider": "qqmusic", "item_id": "song-1"}
STREAM_PATH = f"/single/session_1/{PLAYER_ID}/queueitem_1/{PLAYER_ID}.flac"


class FakeVault:
    def __init__(self, saved=None):
        self.saved = copy.deepcopy(saved or {})
        self.save_error = None
        self.load_error = None

    def load(self):
        if self.load_error:
            raise self.load_error
        return copy.deepcopy(self.saved)

    def save(self, values):
        if self.save_error:
            raise self.save_error
        self.saved = copy.deepcopy(values)


class FakeContent:
    def __init__(self, value):
        self.raw = value if isinstance(value, bytes) else json.dumps(value).encode()

    async def iter_chunked(self, size):
        for offset in range(0, len(self.raw), size):
            yield self.raw[offset:offset + size]


class FakeResponse:
    def __init__(self, value, status=200, entered=None, release=None):
        self.status = status
        self.content = FakeContent(value)
        self.entered = entered
        self.release = release

    async def __aenter__(self):
        if self.entered:
            self.entered.set()
        if self.release:
            await self.release.wait()
        return self

    async def __aexit__(self, *arguments):
        return False


class FakeHttp:
    def __init__(self):
        self.responses = []
        self.calls = []

    def add(self, value, **kwargs):
        self.responses.append(FakeResponse(value, **kwargs))

    def post(self, url, **kwargs):
        self.calls.append({"url": url, **kwargs})
        if not self.responses:
            raise AssertionError("Unexpected simulated MA command")
        return self.responses.pop(0)


class MusicAssistantTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.config = {"runtime_dir": self.directory.name, "music_assistant_base_url": "http://127.0.0.1:8095", "music_assistant_stream_base_url": "http://127.0.0.1:8097", "music_assistant_access_token": TOKEN}
        self.http = FakeHttp()
        self.vault = FakeVault()
        self.client = MusicAssistantClient(self.config, self.http, self.vault)

    def stream_response(self, lease="lease-one", url=None, **kwargs):
        self.http.add({"url": url or "http://172.19.0.2:8097" + STREAM_PATH, "lease": lease, "source_position_s": 0}, **kwargs)

    async def test_search_uses_official_command_and_keeps_refs_internal(self):
        self.http.add({"tracks": [{**TRACK, "name": "晴天", "artists": [{"name": "周杰伦"}], "album": {"name": "叶惠美"}, "duration": 269, "provider_mappings": [{"available": True}]}]})
        result = await self.client.search("  晴天 周杰伦  ", limit=3)
        item = result["items"][0]
        self.assertEqual(item["ref"], TRACK)
        self.assertEqual((item["title"], item["artists"], item["album"], item["duration_s"]), ("晴天", ["周杰伦"], "叶惠美", 269))
        self.assertTrue(item["available"])
        call = self.http.calls[0]
        self.assertEqual(call["url"], "http://127.0.0.1:8095/api")
        self.assertEqual(call["json"], {"command": "music/search", "args": {"search_query": "晴天 周杰伦", "media_types": ["track"], "limit": 3, "library_only": False}})
        self.assertEqual(call["headers"], {"Authorization": "Bearer " + TOKEN})
        self.assertFalse(call["allow_redirects"])
        self.assertIsNone(call["proxy"])
        self.assertLessEqual(call["timeout"].total, 40)
        self.assertNotIn(TOKEN, call["url"] + json.dumps(call["json"]) + json.dumps(result))

    async def test_search_preserves_unavailable_and_filters_invalid_media(self):
        self.http.add({"tracks": [{**TRACK, "name": "无权限", "duration": 250, "provider_mappings": [{"available": False}]}, {**TRACK, "uri": "https://foreign.example/audio.mp3"}, {"name": "没有身份"}, "invalid"]})
        result = await self.client.search("歌曲")
        self.assertEqual(len(result["items"]), 1)
        self.assertFalse(result["items"][0]["available"])
        with self.assertRaises(ValueError):
            await self.client.search("歌曲", limit=1000)

    async def test_config_secret_never_echoes_and_blank_preserves(self):
        result = await self.client.configure({"base_url": "http://localhost:8095", "stream_base_url": "http://localhost:8097", "access_token": "new-fake-ma-token"})
        self.assertEqual(self.vault.saved["access_token"], "new-fake-ma-token")
        self.assertNotIn("new-fake-ma-token", json.dumps(result))
        self.assertTrue(result["token_configured"])
        await self.client.configure({"access_token": ""})
        self.assertEqual(self.vault.saved["access_token"], "new-fake-ma-token")
        self.assertFalse(self.http.calls)

    async def test_explicit_token_clear_survives_restart_over_old_config(self):
        await self.client.configure({"clear_token": True})
        restored_config = {**self.config, "music_assistant_access_token": TOKEN}
        restored = MusicAssistantClient(restored_config, self.http, self.vault)
        await restored.start()
        self.assertEqual(restored_config["music_assistant_access_token"], "")
        self.assertFalse(restored.status()["token_configured"])

    async def test_failed_vault_save_keeps_effective_configuration(self):
        before = copy.deepcopy(self.config)
        self.vault.save_error = OSError("simulated disk write failure")
        with self.assertRaises(OSError):
            await self.client.configure({"access_token": "new-fake-token"})
        self.assertEqual(self.config, before)
        self.assertEqual(self.vault.saved, {})

    async def test_corrupt_vault_fails_closed_without_request(self):
        self.vault.load_error = OSError("simulated corrupt vault")
        with self.assertLogs("assistant.music_assistant", level="ERROR"):
            await self.client.start()
        self.assertTrue(self.client.status()["storage_error"])
        with self.assertRaises(MusicAssistantError):
            await self.client.search("歌曲")
        self.assertFalse(self.http.calls)

    async def test_environment_managed_configuration_is_not_overwritten(self):
        self.config["_config_sources"] = {"music_assistant_access_token": "environment"}
        with self.assertRaises(MusicAssistantError):
            await self.client.configure({"access_token": "another-fake-token"})
        self.assertEqual(self.config["music_assistant_access_token"], TOKEN)
        self.assertEqual(self.vault.saved, {})

    async def test_resolve_accepts_official_container_single_stream_and_seek(self):
        self.stream_response()
        source = await self.client.resolve_stream(TRACK, position_s=63)
        self.assertEqual(self.http.calls[0]["json"], {"command": "xiaoyanzi/resolve", "args": {"uri": TRACK["uri"], "position_s": 63}})
        self.assertEqual(source["url"], "http://127.0.0.1:8097" + STREAM_PATH)
        self.assertEqual(source["base_url"], "http://127.0.0.1:8097")
        self.assertEqual(source["trusted_by"], "music_assistant")
        self.assertEqual(source["format"], "flac")
        self.assertEqual(source["headers"], {})
        self.assertTrue(source["seek_applied"])
        self.assertEqual(source["source_position_s"], 63)
        self.assertEqual(source["session_id"], "lease-one")
        self.assertNotIn(TOKEN, json.dumps(source))

    async def test_preview_metadata_without_audio_url_cannot_be_played(self):
        self.http.add({"playback_access": "preview", "duration_s": 30, "queue_item_id": "sample"})
        with self.assertRaisesRegex(MusicAssistantError, "未返回可播放音频流"):
            await self.client.resolve_stream(TRACK)
        self.assertEqual(len(self.http.calls), 1)
        self.assertFalse((await self.client.report("started"))["accepted"])

    async def test_preview_returns_verified_stream_and_real_duration(self):
        self.http.add({"url": "http://172.19.0.2:8097" + STREAM_PATH, "lease": "preview-lease", "playback_access": "preview", "duration_s": 30})
        source = await self.client.resolve_stream(TRACK)
        self.assertEqual(source["playback_access"], "preview")
        self.assertEqual(source["duration_s"], 30)
        self.assertEqual(source["session_id"], "preview-lease")
        self.assertEqual(source["url"], "http://127.0.0.1:8097" + STREAM_PATH)

    async def test_preview_with_catalog_duration_or_invalid_url_is_rejected(self):
        for duration in (None, 269, 0, True, float("inf")):
            self.http.add({"url": "http://172.19.0.2:8097" + STREAM_PATH, "lease": "preview-lease", "playback_access": "preview", "duration_s": duration})
            with self.assertRaises(MusicAssistantError):
                await self.client.resolve_stream(TRACK)
        self.http.add({"url": "https://foreign.example" + STREAM_PATH, "lease": "preview-lease", "playback_access": "preview", "duration_s": 30})
        with self.assertRaises(MusicAssistantError):
            await self.client.resolve_stream(TRACK)

    async def test_rejects_preview_foreign_origin_wrong_queue_and_unsafe_format(self):
        urls = [
            "http://127.0.0.1:8095/preview?token=sample-only",
            "http://foreign.example:8097" + STREAM_PATH,
            "http://172.19.0.2:8080" + STREAM_PATH,
            "http://172.19.0.2:8097" + STREAM_PATH.replace("/xiaoyanzi_gateway/", "/other_queue/"),
            "http://172.19.0.2:8097" + STREAM_PATH.replace("/xiaoyanzi_gateway.flac", "/another_player.flac"),
            "http://172.19.0.2:8097" + STREAM_PATH + "?token=secret",
            "http://172.19.0.2:8097" + STREAM_PATH.replace(".flac", ".exe"),
        ]
        for url in urls:
            with self.subTest(url=url):
                self.stream_response(url=url)
                with self.assertRaises(MusicAssistantError):
                    await self.client.resolve_stream(TRACK)
        self.assertTrue(all(call["url"] == "http://127.0.0.1:8095/api" for call in self.http.calls))

    async def test_redirect_response_is_not_followed_or_treated_as_audio(self):
        self.http.add({}, status=302)
        with self.assertRaises(MusicAssistantError):
            await self.client.search("歌曲")
        self.assertEqual(len(self.http.calls), 1)
        self.assertFalse(self.http.calls[0]["allow_redirects"])
        self.assertEqual(self.client.status()["connection"], "failed")

    async def test_response_budget_and_json_contract_fail_closed(self):
        for value in (b"not-json", {"result": {"tracks": []}}, b"x" * (1024 * 1024 + 1)):
            self.http.add(value)
            with self.assertRaises(MusicAssistantError):
                await self.client.search("歌曲")

    async def test_session_isolation_ignores_old_device_report(self):
        self.stream_response("new-session")
        await self.client.resolve_stream(TRACK)
        result = await self.client.report("started", 2, session_id="old-session")
        self.assertFalse(result["accepted"])
        self.assertEqual(len(self.http.calls), 1)
        self.http.add({"accepted": True})
        await self.client.report("progress", 5, session_id="new-session")
        self.assertEqual(self.http.calls[-1]["json"], {"command": "xiaoyanzi/report", "args": {"lease": "new-session", "state": "playing", "position_s": 5}})

    async def test_old_completion_cannot_clear_new_session_after_await(self):
        self.stream_response("old-session")
        await self.client.resolve_stream(TRACK)
        entered, release = asyncio.Event(), asyncio.Event()
        self.http.add({"accepted": True}, entered=entered, release=release)
        old_report = asyncio.create_task(self.client.report("finished", 100))
        await asyncio.wait_for(entered.wait(), 1)
        self.stream_response("new-session")
        new_resolve = asyncio.create_task(self.client.resolve_stream(TRACK))
        await asyncio.sleep(0)
        self.assertFalse(new_resolve.done())
        release.set()
        await old_report
        await asyncio.wait_for(new_resolve, 1)
        self.http.add({"accepted": True})
        await self.client.stop()
        self.assertEqual(self.http.calls[-1]["json"], {"command": "xiaoyanzi/stop", "args": {"lease": "new-session"}})

    async def test_stop_waits_for_resolve_and_releases_its_actual_lease(self):
        entered, release = asyncio.Event(), asyncio.Event()
        self.stream_response("prepared-session", entered=entered, release=release)
        resolve = asyncio.create_task(self.client.resolve_stream(TRACK))
        await asyncio.wait_for(entered.wait(), 1)
        self.http.add({"accepted": True})
        stop = asyncio.create_task(self.client.stop())
        await asyncio.sleep(0)
        self.assertEqual(len(self.http.calls), 1)
        self.assertFalse(stop.done())
        release.set()
        await resolve
        await stop
        self.assertEqual(self.http.calls[-1]["json"], {"command": "xiaoyanzi/stop", "args": {"lease": "prepared-session"}})
        self.assertFalse((await self.client.report("started"))["accepted"])

    async def test_old_stop_cannot_release_latest_resolved_session(self):
        self.stream_response("old-session")
        await self.client.resolve_stream(TRACK)
        entered, release = asyncio.Event(), asyncio.Event()
        self.stream_response("new-session", entered=entered, release=release)
        resolving = asyncio.create_task(self.client.resolve_stream(TRACK))
        await asyncio.wait_for(entered.wait(), 1)
        old_stop = asyncio.create_task(self.client.stop(session_id="old-session"))
        await asyncio.sleep(0)
        self.assertFalse(old_stop.done())
        release.set()
        await resolving
        self.assertFalse((await old_stop)["accepted"])
        self.assertEqual(len(self.http.calls), 2)
        self.http.add({"accepted": True})
        self.assertTrue((await self.client.stop(session_id="new-session"))["accepted"])
        self.assertEqual(self.http.calls[-1]["json"], {"command": "xiaoyanzi/stop", "args": {"lease": "new-session"}})

    async def test_cancelled_resolve_does_not_leave_lock_or_block_latest_stop(self):
        entered, release = asyncio.Event(), asyncio.Event()
        self.stream_response("cancelled-session", entered=entered, release=release)
        first = asyncio.create_task(self.client.resolve_stream(TRACK))
        await asyncio.wait_for(entered.wait(), 1)
        first.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await first
        self.stream_response("latest-session")
        await asyncio.wait_for(self.client.resolve_stream(TRACK), 1)
        self.http.add({"accepted": True})
        await asyncio.wait_for(self.client.stop(), 1)
        self.assertEqual(self.http.calls[-1]["json"]["args"]["lease"], "latest-session")

    async def test_remote_stop_requires_true_receipt_and_preserves_unconfirmed_lease(self):
        for receipt in ({"accepted": False, "error": TOKEN}, {}, {"accepted": 1}, [], None):
            with self.subTest(receipt=receipt):
                self.stream_response("unconfirmed-session")
                await self.client.resolve_stream(TRACK)
                before = len(self.http.calls)
                self.http.add(receipt)
                with self.assertRaises(MusicAssistantError) as raised:
                    await self.client.stop()
                self.assertEqual(len(self.http.calls), before + 1)
                self.assertEqual(self.client._lease, "unconfirmed-session")
                self.assertNotIn(TOKEN, str(raised.exception))
                self.assertEqual(self.client.status()["connection"], "failed")
                self.http.add({"accepted": True})
                self.assertTrue((await self.client.stop())["accepted"])
                self.assertEqual(self.http.calls[-1]["json"]["args"]["lease"], "unconfirmed-session")

    async def test_stop_without_local_lease_remains_a_network_free_noop(self):
        self.assertEqual(await self.client.stop(), {"accepted": False})
        self.assertEqual(self.http.calls, [])


if __name__ == "__main__":
    unittest.main()

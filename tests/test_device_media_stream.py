"""隔离设备媒体模拟：真实PyAV子进程，假HTTP/设备，不访问声卡或外网。"""

import asyncio
import io
import struct
import tempfile
import unittest
import wave
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from src.assistant_app.application.device_media_stream import DeviceMediaStream, FRAME_BYTES, MediaStreamError, SAMPLE_RATE


def wav_bytes(seconds):
    samples = b"".join(struct.pack("<h", index % 30000) for index in range(round(seconds * SAMPLE_RATE)))
    output = io.BytesIO()
    with wave.open(output, "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(SAMPLE_RATE)
        audio.writeframes(samples)
    return output.getvalue(), samples


class FakeResponse:
    def __init__(self, data, status=200):
        self.data, self.status = data, status
        self.content_length = len(data)
        self.headers = {}
        self.content = self
        self.closed = False

    async def __aenter__(self):
        return self

    async def __aexit__(self, *_arguments):
        self.closed = True

    async def iter_chunked(self, size):
        for offset in range(0, len(self.data), size):
            yield self.data[offset:offset + size]
            await asyncio.sleep(0)


class FakeHttp:
    def __init__(self, data=b"", status=200):
        self.data, self.status = data, status
        self.requests, self.responses = [], []

    def get(self, url, **arguments):
        self.requests.append((url, arguments))
        response = FakeResponse(self.data, self.status)
        self.responses.append(response)
        return response


class FakeDevice:
    def __init__(self):
        self.socket = SimpleNamespace(closed=False)
        self.media_stream_supported = True
        self.engine = None
        self.messages, self.pcm = [], []
        self.started = asyncio.Event()
        self.auto_start_ack = True
        self.start_acked = False
        self.queue = asyncio.Queue(maxsize=32)
        self.consumer = self.progress = None
        self.playback_id, self.played = None, 0
        self.receipts = []

    async def ack(self, state, **values):
        self.receipts.append((state, self.played, self.queue.qsize()))
        if state == "start":
            self.start_acked = True
        return await self.engine.on_device_event(self, {"type": "media_ack", "state": state, "phase": state, "playback_id": self.playback_id, "played_samples": self.played, "position_samples": self.played, "queue_used": self.queue.qsize(), "queue_capacity": 32, "underruns": 0, **values})

    async def send_json(self, event):
        self.messages.append(dict(event))
        kind = event["type"]
        if kind == "media_start":
            self.playback_id = event["playback_id"]
            self.played = event["position_samples"]
            self.started.set()
            self.consumer = asyncio.create_task(self.consume())
            self.progress = asyncio.create_task(self.report_progress())
            if self.auto_start_ack:
                await self.ack("start")
        elif kind in {"media_pause", "media_stop"}:
            await self.halt_output()
            await self.ack("pause" if kind == "media_pause" else "stop")
        elif kind == "media_end":
            await self.queue.join()
            await self.halt_output()
            await self.ack("ended")

    async def send_bytes(self, frame):
        if not self.start_acked:
            raise AssertionError("开始回执前收到PCM")
        if not frame or len(frame) > FRAME_BYTES or len(frame) % 2:
            raise AssertionError("PCM帧不符合960B/20ms上限")
        self.pcm.append(frame)
        await self.queue.put(frame)

    async def consume(self):
        while True:
            frame = await self.queue.get()
            try:
                await asyncio.sleep(len(frame) / (SAMPLE_RATE * 2))
                self.played += len(frame) // 2
            finally:
                self.queue.task_done()

    async def report_progress(self):
        while True:
            await asyncio.sleep(1)
            await self.ack("progress")

    async def halt_output(self):
        tasks = [task for task in (self.consumer, self.progress) if task is not None and task is not asyncio.current_task()]
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        self.consumer = self.progress = None
        while not self.queue.empty():
            self.queue.get_nowait()
            self.queue.task_done()


class DeviceMediaStreamTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name)
        self.http = FakeHttp()
        self.device = FakeDevice()
        self.runtime = SimpleNamespace(config={"runtime_dir": str(self.directory)}, native_devices={self.device}, voice_sessions=set())
        self.engine = DeviceMediaStream(self.runtime, self.http)
        self.device.engine = self.engine

    async def asyncTearDown(self):
        await self.engine.close()
        await self.device.halt_output()
        self.temporary.cleanup()

    def local(self, seconds=0.12, **options):
        data, samples = wav_bytes(seconds)
        path = self.directory / "source.wav"
        path.write_bytes(data)
        return {"kind": "trusted_file", "path": str(path), "title": "临时模拟音轨", **options}, samples

    def remote(self, data, duration_s, **options):
        self.http.data = data
        return {"kind": "http", "trusted_by": "music_assistant", "base_url": "http://127.0.0.1:8097", "url": "http://127.0.0.1:8097/single/fake.wav?token=secret-for-test", "headers": {"X-Media-Test": "private-header"}, "format": "wav", "duration_s": duration_s, **options}

    async def wait_for(self, predicate, timeout=8):
        async with asyncio.timeout(timeout):
            while not predicate():
                await asyncio.sleep(0.01)

    async def wait_terminal(self):
        await self.wait_for(lambda: self.engine.status()["state"] in {"ended", "error"})
        task = self.engine._current.task
        await asyncio.gather(task, return_exceptions=True)
        return self.engine.status()

    async def test_real_decoder_waits_for_start_ack_and_reports_only_device_position(self):
        source, samples = self.local()
        self.device.auto_start_ack = False
        initial = await self.engine.start(self.device, source, playback_id="check-start")
        self.assertEqual(initial["state"], "starting")
        self.assertIsNone(initial["device_position_s"])
        await asyncio.wait_for(self.device.started.wait(), 1)
        await asyncio.sleep(0.05)
        self.assertEqual(self.device.pcm, [])
        await self.device.ack("start")
        state = await self.wait_terminal()
        self.assertEqual(state["state"], "ended")
        self.assertEqual(b"".join(self.device.pcm), samples)
        self.assertEqual(state["played_samples"], len(samples) // 2)
        self.assertEqual(state["device_position_s"], 0.12)
        self.assertEqual(self.device.messages[-1]["type"], "media_end")
        self.assertLessEqual(max(map(len, self.device.pcm)), 960)
        self.assertIsNone(self.engine._current.process)

    async def test_ma_seek_offset_is_not_decoded_twice(self):
        data, samples = wav_bytes(0.2)
        source = self.remote(data, 0.6, start_position_s=0.4, seek_applied=True)
        await self.engine.start(self.device, source, position_s=0.4, playback_id="check-seek")
        state = await self.wait_terminal()
        self.assertEqual(state["state"], "ended")
        self.assertEqual(b"".join(self.device.pcm), samples)
        self.assertEqual(state["played_samples"], round(0.6 * SAMPLE_RATE))
        self.assertEqual(state["device_position_s"], 0.6)
        arguments = self.http.requests[0][1]
        self.assertFalse(arguments["allow_redirects"])
        self.assertEqual(arguments["timeout"].sock_read, 10)
        self.assertTrue(self.http.responses[0].closed)

    async def test_local_seek_discards_actual_pcm_before_target(self):
        source, samples = self.local(0.3)
        await self.engine.start(self.device, source, position_s=0.1, playback_id="check-local-seek")
        state = await self.wait_terminal()
        self.assertEqual(state["state"], "ended")
        self.assertEqual(b"".join(self.device.pcm), samples[round(0.1 * SAMPLE_RATE) * 2:])
        self.assertEqual(state["device_position_s"], 0.3)

    async def test_pause_waits_stable_device_position_and_reaps_real_decoder(self):
        source, _samples = self.local(10)
        await self.engine.start(self.device, source, playback_id="check-pause")
        await self.wait_for(lambda: len(self.device.pcm) >= 3)
        process = self.engine._current.process
        self.assertIsNotNone(process)
        self.assertIsNone(process.returncode)
        state = await self.engine.pause()
        self.assertEqual(state["state"], "paused")
        stable = state["played_samples"]
        sent = len(self.device.pcm)
        await asyncio.sleep(0.08)
        self.assertEqual(self.device.played, stable)
        self.assertEqual(len(self.device.pcm), sent)
        self.assertIsNotNone(process.returncode)
        self.assertIsNone(self.engine._current.process)
        self.assertEqual(self.device.queue.qsize(), 0)

    async def test_disconnect_stops_stream_and_reaps_decoder(self):
        source, _samples = self.local(10)
        await self.engine.start(self.device, source, playback_id="check-disconnect")
        await self.wait_for(lambda: len(self.device.pcm) >= 3)
        process = self.engine._current.process
        self.device.socket.closed = True
        with self.assertLogs("assistant.device_media_stream", level="ERROR"):
            state = await self.wait_terminal()
        self.assertEqual(state["state"], "error")
        self.assertIsNotNone(process.returncode)
        self.assertIsNone(self.engine._current.process)
        self.device.socket.closed = False

    async def test_looped_local_source_obeys_total_budget(self):
        # 完整首遍后截断第二遍；保留真实解码与8秒期限，避免用4次进程启动衡量0.2秒音频预算。
        source, samples = self.local(0.15, loop=True, duration_s=0.2)
        await self.engine.start(self.device, source, playback_id="check-loop")
        state = await self.wait_terminal()
        self.assertEqual(state["state"], "ended")
        expected = (samples * 2)[:round(0.2 * SAMPLE_RATE) * 2]
        self.assertEqual(b"".join(self.device.pcm), expected)
        self.assertEqual(state["device_position_s"], 0.2)
        self.assertEqual(sum(event["type"] == "media_start" for event in self.device.messages), 1)

    async def test_old_firmware_untrusted_origin_and_outside_file_are_rejected(self):
        source, _samples = self.local()
        self.device.media_stream_supported = False
        with self.assertRaises(MediaStreamError):
            await self.engine.start(self.device, source, playback_id="old-firmware")
        self.device.media_stream_supported = True
        data, _samples = wav_bytes(0.1)
        remote = self.remote(data, 0.1)
        for changes in ({"trusted_by": "model"}, {"url": "http://evil.invalid/song.wav"}, {"headers": {"Host": "evil.invalid"}}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                await self.engine.start(self.device, {**remote, **changes}, playback_id="bad-source")
        with tempfile.TemporaryDirectory() as outside:
            path = Path(outside) / "outside.wav"
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                await self.engine.start(self.device, {"kind": "trusted_file", "path": str(path)}, playback_id="outside")
        self.assertEqual(self.device.messages, [])
        self.assertEqual(self.http.requests, [])

    async def test_redirect_is_rejected_without_token_in_error_log(self):
        data, _samples = wav_bytes(0.1)
        self.http.status = 302
        source = self.remote(data, 0.1)
        with self.assertLogs("assistant.device_media_stream", level="ERROR") as logged:
            await self.engine.start(self.device, source, playback_id="check-redirect")
            state = await self.wait_terminal()
        self.assertEqual(state["state"], "error")
        self.assertEqual(self.device.pcm, [])
        self.assertEqual(len(self.http.requests), 1)
        self.assertNotIn("secret-for-test", "\n".join(logged.output))
        self.assertNotIn("private-header", "\n".join(logged.output))

    async def test_unknown_receipts_cannot_claim_actual_position(self):
        source, _samples = self.local()
        self.device.auto_start_ack = False
        await self.engine.start(self.device, source, playback_id="known")
        await asyncio.wait_for(self.device.started.wait(), 1)
        result = await self.engine.on_device_event(self.device, {"type": "media_ack", "state": "progress", "playback_id": "unknown", "played_samples": 2400})
        self.assertFalse(result)
        self.assertIsNone(self.engine.status()["device_position_s"])
        result = await self.device.ack("progress", played_samples=1000000, position_samples=1000000)
        self.assertFalse(result)
        self.assertIsNone(self.engine.status()["device_position_s"])

    async def test_stop_before_start_task_runs_does_not_send_unknown_control(self):
        source, _samples = self.local()
        await self.engine.start(self.device, source, playback_id="immediate-stop")
        state = await self.engine.stop()
        self.assertEqual(state["state"], "stopped")
        self.assertIsNone(state["device_position_s"])
        self.assertEqual(self.device.messages, [])
        self.assertEqual(self.engine._current.source, {})

    async def test_periodic_i2s_progress_and_closed_voice_handoff(self):
        source, _samples = self.local(1.2)
        self.runtime.voice_sessions = [SimpleNamespace(closed=True)]
        await self.engine.start(self.device, source, playback_id="check-progress")
        # 开始解码前的周期回执允许0；等待真正消费音频后的进度，而不是只等第一条心跳。
        await self.wait_for(lambda: any(state == "progress" and samples > 0 for state, samples, _queue in self.device.receipts))
        during = self.engine.status()
        self.assertGreater(during["played_samples"], 0)
        self.assertLessEqual(during["played_samples"], during["sent_samples"])
        self.assertIn("queue_used", during["ack"])
        state = await self.wait_terminal()
        self.assertEqual(state["state"], "ended")
        self.assertEqual(state["device_position_s"], 1.2)

    async def test_missing_start_ack_never_downloads_or_sends_pcm(self):
        data, _samples = wav_bytes(0.1)
        source = self.remote(data, 0.1)
        self.device.auto_start_ack = False
        with self.assertLogs("assistant.device_media_stream", level="ERROR"):
            await self.engine.start(self.device, source, playback_id="check-no-start-ack")
            state = await self.wait_terminal()
        self.assertEqual(state["state"], "error")
        self.assertEqual(self.http.requests, [])
        self.assertEqual(self.device.pcm, [])
        self.assertIsNone(self.engine._current.process)

    async def test_decode_failure_records_safe_child_stack(self):
        source = self.remote(b"not-a-valid-wave-file", 1)
        with self.assertLogs("assistant.device_media_stream", level="ERROR") as logged:
            await self.engine.start(self.device, source, playback_id="check-decode-error")
            state = await self.wait_terminal()
        self.assertEqual(state["state"], "error")
        self.assertEqual(self.device.pcm, [])
        self.assertIn("device_media_decoder_failed", "\n".join(logged.output))
        self.assertIn("media_decode.py", "\n".join(logged.output))
        self.assertNotIn("secret-for-test", "\n".join(logged.output))
        self.assertIsNone(self.engine._current.process)

    async def test_network_exception_text_cannot_leak_url_or_headers(self):
        data, _samples = wav_bytes(0.1)
        source = self.remote(data, 0.1)
        failure = TimeoutError(source["url"] + " Bearer private-header")
        with patch.object(self.http, "get", side_effect=failure), self.assertLogs("assistant.device_media_stream", level="ERROR") as logged:
            await self.engine.start(self.device, source, playback_id="check-secret-error")
            state = await self.wait_terminal()
        self.assertEqual(state["state"], "error")
        self.assertNotIn("secret-for-test", "\n".join(logged.output))
        self.assertNotIn("private-header", "\n".join(logged.output))
        self.assertIn("TimeoutError", "\n".join(logged.output))


if __name__ == "__main__":
    unittest.main()

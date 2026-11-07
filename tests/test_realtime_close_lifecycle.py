"""Realtime关闭及恢复：真实VoiceSession/NativeDevice，仅本机aiohttp及受控替身。"""

import asyncio
from contextlib import asynccontextmanager
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestServer

from assistant_runtime import NativeDevice
from gateway import CONFIG_KEY, HTTP_KEY, REALTIME_URL_KEY, RUNTIME_KEY, RealtimeBackend, VoiceSession
from tests.test_wake_gate import FakeSocket


class DeviceSocket(FakeSocket):
    def __init__(self, events=None, close_error=None):
        super().__init__()
        self.events = events if events is not None else []
        self.close_error = close_error
        self.listening = asyncio.Event()

    async def send_json(self, payload):
        await super().send_json(payload)
        if payload.get("type") == "custom" and payload.get("payload", {}).get("state") == "listening":
            self.listening.set()

    async def close(self, **kwargs):
        self.events.append("device_close")
        if self.close_error is not None:
            raise self.close_error
        await super().close(**kwargs)


class ControlledCloud:
    def __init__(self, *, close_error=None, closes=True, release=None):
        self.closed = False
        self.close_error = close_error
        self.closes = closes
        self.release = release
        self.entered = asyncio.Event()
        self.cancelled = False

    async def close(self):
        self.entered.set()
        try:
            if self.release is not None:
                await self.release.wait()
        except asyncio.CancelledError:
            self.cancelled = True
            raise
        if self.close_error is not None:
            raise self.close_error
        self.closed = self.closes


class FallbackBackend:
    name = "deepseek"

    def __init__(self):
        self.connected = False
        self.connect = AsyncMock(side_effect=self._connect)
        self.close = AsyncMock(side_effect=self._close)
        self.stop = asyncio.Event()

    async def _connect(self):
        self.connected = True

    async def _close(self):
        self.connected = False
        self.stop.set()

    async def events(self):
        await self.stop.wait()
        if False:
            yield None


class RealtimeCloseLifecycleTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.runtime = SimpleNamespace(
            config={"max_connections": 1}, voice_lock=asyncio.Lock(), voice_sessions=set(),
            last_device_state="idle", last_provider=None, last_cloud_ready=None,
        )
        self.config = {
            "aliyun_api_key": "local-fixture-key", "aliyun_workspace_id": "local-fixture-space",
            "aliyun_model": "local-fixture-model", "enable_search": False,
            "session_seconds": 600, "idle_seconds": 600,
        }
        self.app = {CONFIG_KEY: self.config, RUNTIME_KEY: self.runtime}
        self.tasks = []
        original = aiohttp.TCPConnector._resolve_host

        async def loopback_only(connector, host, port, *args, **kwargs):
            if host not in {"127.0.0.1", "::1", "localhost"}:
                raise AssertionError("Realtime关闭回归禁止非本机连接")
            return await original(connector, host, port, *args, **kwargs)

        guard = patch.object(aiohttp.TCPConnector, "_resolve_host", loopback_only)
        guard.start()
        self.addCleanup(guard.stop)

    async def asyncTearDown(self):
        for task in self.tasks:
            if not task.done():
                task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)

    def launch(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    def voice(self, socket=None):
        return VoiceSession(self.app, socket or DeviceSocket(), raw_pcm=True)

    def failed_backend(self, voice, cloud):
        backend = RealtimeBackend(voice)
        backend.cloud = cloud
        backend.connect = AsyncMock(side_effect=RuntimeError("controlled configuration rejection"))
        return backend

    @asynccontextmanager
    async def local_peer(self, *, reply_close):
        close_seen, release = asyncio.Event(), asyncio.Event()
        transports = []

        async def handler(request):
            socket = web.WebSocketResponse(autoclose=reply_close)
            await socket.prepare(request)
            transports.append(request.transport)
            await socket.send_json({"type": "error", "error": {"code": "controlled_rejection"}})
            try:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.CLOSE:
                    close_seen.set()
                if not reply_close:
                    await release.wait()
                return socket
            finally:
                await socket.close()

        app = web.Application()
        app.router.add_get("/realtime", handler)
        server = TestServer(app)
        await server.start_server()
        client = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20, sock_connect=10))
        try:
            yield SimpleNamespace(client=client, url=str(server.make_url("/realtime")), close_seen=close_seen, transports=transports)
        finally:
            release.set()
            await client.close()
            await server.close()

    async def test_close_raises_original_error_and_keeps_cloud_reference(self):
        error = OSError("controlled close failure")
        cloud = ControlledCloud(close_error=error)
        backend = RealtimeBackend(self.voice())
        backend.cloud = cloud
        with self.assertLogs("assistant", level="ERROR") as logs:
            with self.assertRaises(OSError) as raised:
                await backend.close()
        self.assertIs(raised.exception, error)
        self.assertIs(backend.cloud, cloud)
        self.assertFalse(cloud.closed)
        self.assertTrue(any(record.exc_info for record in logs.records))

    async def test_returned_open_connection_prevents_fallback(self):
        voice = self.voice()
        cloud = ControlledCloud(closes=False)
        backend, fallback = self.failed_backend(voice, cloud), FallbackBackend()
        with patch("gateway.provider_chain", return_value=["qwen_realtime", "deepseek"]), patch("gateway.RealtimeBackend", return_value=backend), patch("gateway.ChatBackend", return_value=fallback):
            with self.assertRaises(RuntimeError):
                await voice.connect()
        fallback.connect.assert_not_awaited()
        self.assertIsNone(voice.backend)
        self.assertFalse(cloud.closed)

    async def test_raised_close_error_prevents_fallback(self):
        voice = self.voice()
        error = OSError("controlled unresolved cloud")
        cloud = ControlledCloud(close_error=error)
        backend, fallback = self.failed_backend(voice, cloud), FallbackBackend()
        with patch("gateway.provider_chain", return_value=["qwen_realtime", "deepseek"]), patch("gateway.RealtimeBackend", return_value=backend), patch("gateway.ChatBackend", return_value=fallback):
            with self.assertRaises(OSError) as raised:
                await voice.connect()
        self.assertIs(raised.exception, error)
        fallback.connect.assert_not_awaited()
        self.assertIsNone(voice.backend)

    async def test_failed_connect_waits_for_completed_close_before_fallback(self):
        voice = self.voice()
        release = asyncio.Event()
        cloud = ControlledCloud(release=release)
        backend, fallback = self.failed_backend(voice, cloud), FallbackBackend()

        async def recovered():
            self.assertTrue(cloud.closed)
            fallback.connected = True

        fallback.connect.side_effect = recovered
        with patch("gateway.provider_chain", return_value=["qwen_realtime", "deepseek"]), patch("gateway.RealtimeBackend", return_value=backend), patch("gateway.ChatBackend", return_value=fallback):
            connecting = self.launch(voice.connect())
            try:
                await asyncio.wait_for(cloud.entered.wait(), 1)
                self.assertFalse(connecting.done())
                fallback.connect.assert_not_awaited()
                release.set()
                await asyncio.wait_for(connecting, 1)
            finally:
                release.set()
                await asyncio.gather(connecting, return_exceptions=True)
        self.assertIs(voice.backend, fallback)
        fallback.connect.assert_awaited_once()

    async def test_total_close_deadline_covers_wait_before_close_returns(self):
        release = asyncio.Event()
        cloud = ControlledCloud(release=release)
        backend = RealtimeBackend(self.voice())
        backend.cloud = cloud
        original_timeout, timeouts = asyncio.timeout, []

        def record_timeout(delay):
            timeout = original_timeout(delay)
            timeouts.append((delay, timeout))
            return timeout

        with patch("gateway.asyncio.timeout", side_effect=record_timeout):
            closing = self.launch(backend.close())
            try:
                await asyncio.wait_for(cloud.entered.wait(), 1)
                self.assertEqual([delay for delay, _timeout in timeouts], [3])
                timeouts[0][1].reschedule(asyncio.get_running_loop().time())
                with self.assertRaises(TimeoutError):
                    await asyncio.wait_for(closing, 1)
                self.assertTrue(cloud.cancelled)
                self.assertTrue(timeouts[0][1].expired())
                self.assertIs(backend.cloud, cloud)
            finally:
                release.set()
                await asyncio.gather(closing, return_exceptions=True)

    async def test_external_close_cancellation_is_propagated(self):
        release = asyncio.Event()
        cloud = ControlledCloud(release=release)
        backend = RealtimeBackend(self.voice())
        backend.cloud = cloud
        closing = self.launch(backend.close())
        try:
            await asyncio.wait_for(cloud.entered.wait(), 1)
            closing.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await closing
            self.assertTrue(cloud.cancelled)
            self.assertIs(backend.cloud, cloud)
        finally:
            release.set()

    async def test_backend_cleanup_failure_still_closes_device_and_clears_run_task(self):
        voice = self.voice()
        error = OSError("controlled backend cleanup")
        voice.backend = SimpleNamespace(close=AsyncMock(side_effect=error))
        voice.run_task = object()
        with self.assertLogs("assistant", level="ERROR") as logs:
            with self.assertRaises(OSError) as raised:
                await voice.close_resources()
        self.assertIs(raised.exception, error)
        self.assertTrue(voice.socket.closed)
        self.assertIsNone(voice.run_task)
        self.assertTrue(any("session_cleanup_failed" in record.getMessage() and record.exc_info for record in logs.records))
        self.assertFalse(any("session_closed " in record.getMessage() for record in logs.records))

    async def test_interrupt_cleanup_failure_still_closes_backend_device_and_clears_run_task(self):
        voice = self.voice()
        error = RuntimeError("controlled interrupt cleanup")
        voice.speaking = True
        voice.interrupt = AsyncMock(side_effect=error)
        voice.backend = SimpleNamespace(close=AsyncMock())
        voice.run_task = object()
        with self.assertLogs("assistant", level="ERROR") as logs:
            with self.assertRaises(RuntimeError) as raised:
                await voice.close_resources()
        self.assertIs(raised.exception, error)
        voice.backend.close.assert_awaited_once()
        self.assertTrue(voice.socket.closed)
        self.assertIsNone(voice.run_task)
        self.assertTrue(any("session_cleanup_failed" in record.getMessage() and record.exc_info for record in logs.records))

    async def test_double_cleanup_failure_preserves_exception_chain_and_clears_run_task(self):
        backend_error, device_error = OSError("controlled backend failure"), RuntimeError("controlled device failure")
        socket = DeviceSocket(close_error=device_error)
        voice = self.voice(socket)
        voice.backend = SimpleNamespace(close=AsyncMock(side_effect=backend_error))
        voice.run_task = object()
        with self.assertRaises(RuntimeError) as raised:
            await voice.close_resources()
        self.assertIs(raised.exception, device_error)
        self.assertIs(raised.exception.__context__, backend_error)
        self.assertEqual(socket.events, ["device_close"])
        self.assertIsNone(voice.run_task)

    async def test_backend_cleanup_cancellation_still_releases_device_and_run_task(self):
        voice = self.voice()
        voice.backend = SimpleNamespace(close=AsyncMock(side_effect=asyncio.CancelledError()))
        voice.run_task = object()
        with self.assertRaises(asyncio.CancelledError):
            await voice.close_resources()
        self.assertTrue(voice.socket.closed)
        self.assertIsNone(voice.run_task)

    async def test_successful_cleanup_keeps_interrupt_backend_device_order_and_success_log(self):
        events = []
        voice = self.voice(DeviceSocket(events))
        voice.speaking = True
        voice.run_task = object()
        voice.interrupt = AsyncMock(side_effect=lambda **_kwargs: events.append("interrupt"))
        voice.backend = SimpleNamespace(close=AsyncMock(side_effect=lambda: events.append("backend_close")))
        with self.assertLogs("assistant", level="INFO") as logs:
            await voice.close_resources()
        self.assertEqual(events, ["interrupt", "backend_close", "device_close"])
        self.assertIsNone(voice.run_task)
        self.assertTrue(any("session_closed " in record.getMessage() for record in logs.records))
        self.assertFalse(any("session_cleanup_failed" in record.getMessage() for record in logs.records))

    async def test_real_local_peer_without_close_reply_times_out_and_native_can_restart(self):
        async with self.local_peer(reply_close=False) as peer:
            self.app.update({HTTP_KEY: peer.client, REALTIME_URL_KEY: peer.url})
            sessions, backends, timeouts = [], [], []
            fallback, original_timeout = FallbackBackend(), asyncio.timeout

            def factory(conversation):
                voice = self.voice(conversation)
                sessions.append(voice)
                return voice

            realtime_class = RealtimeBackend

            def real_backend(voice):
                backend = realtime_class(voice)
                backends.append(backend)
                return backend

            def record_timeout(delay):
                timeout = original_timeout(delay)
                timeouts.append((delay, timeout, asyncio.current_task()))
                return timeout

            socket = DeviceSocket()
            device = NativeDevice(socket, self.runtime, factory)
            with patch("gateway.provider_chain", return_value=["qwen_realtime", "deepseek"]), patch("gateway.RealtimeBackend", side_effect=real_backend), patch("gateway.ChatBackend", return_value=fallback), patch("gateway.asyncio.timeout", side_effect=record_timeout):
                try:
                    await device.start("auto")
                    owner = device.task
                    await asyncio.wait_for(peer.close_seen.wait(), 1)
                    close_deadlines = [timeout for delay, timeout, task in timeouts if delay == 3 and task is owner]
                    self.assertEqual(len(close_deadlines), 1, "真实关闭必须有覆盖完整close的3秒总期限")
                    fallback.connect.assert_not_awaited()
                    close_deadlines[0].reschedule(asyncio.get_running_loop().time())
                    with self.assertLogs("assistant", level="ERROR") as logs:
                        await asyncio.wait_for(owner, 1)
                    self.assertTrue(close_deadlines[0].expired())
                    self.assertIsInstance(backends[0].cloud, aiohttp.ClientWebSocketResponse)
                    self.assertTrue(backends[0].cloud.closed)
                    self.assertEqual(backends[0].cloud.close_code, aiohttp.WSCloseCode.ABNORMAL_CLOSURE)

                    async def transport_closed():
                        while not peer.transports[0].is_closing():
                            await asyncio.sleep(0)

                    await asyncio.wait_for(transport_closed(), 1)
                    fallback.connect.assert_not_awaited()
                    self.assertIsNone(sessions[0].backend)
                    self.assertIsNone(sessions[0].run_task)
                    self.assertTrue(sessions[0].socket.closed)
                    self.assertEqual(self.runtime.voice_sessions, set())
                    self.assertEqual(self.runtime.last_device_state, "idle")
                    self.assertTrue(any(record.exc_info and isinstance(record.exc_info[1], TimeoutError) for record in logs.records))
                    self.assertFalse(peer.client.closed, "不能通过关闭共享HTTP client清理单个Realtime连接")
                    with patch("gateway.provider_chain", return_value=["deepseek"]):
                        await device.start("auto")
                        await asyncio.wait_for(socket.listening.wait(), 1)
                        self.assertEqual(len(sessions), 2)
                        self.assertIs(sessions[1].backend, fallback)
                        self.assertEqual(self.runtime.last_device_state, "listening")
                        fallback.connect.assert_awaited_once()
                finally:
                    await device.end_conversation()

    async def test_real_local_normal_close_completes_before_fallback(self):
        async with self.local_peer(reply_close=True) as peer:
            self.app.update({HTTP_KEY: peer.client, REALTIME_URL_KEY: peer.url})
            voice, fallback, backends = self.voice(), FallbackBackend(), []
            realtime_class = RealtimeBackend

            def real_backend(session):
                backend = realtime_class(session)
                backends.append(backend)
                return backend

            async def recovered():
                self.assertTrue(backends[0].cloud.closed)
                self.assertFalse(peer.client.closed)
                fallback.connected = True

            fallback.connect.side_effect = recovered
            with patch("gateway.provider_chain", return_value=["qwen_realtime", "deepseek"]), patch("gateway.RealtimeBackend", side_effect=real_backend), patch("gateway.ChatBackend", return_value=fallback):
                await asyncio.wait_for(voice.connect(), 1)
            await asyncio.wait_for(peer.close_seen.wait(), 1)
            self.assertIsInstance(backends[0].cloud, aiohttp.ClientWebSocketResponse)
            self.assertIs(voice.backend, fallback)
            fallback.connect.assert_awaited_once()
            await voice.close_resources()


if __name__ == "__main__":
    unittest.main()

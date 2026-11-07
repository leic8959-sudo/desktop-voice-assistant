"""Realtime发送所有权与背压：受控Event、真实本机writer路径；无真实云/设备动作。"""

import asyncio
from contextlib import contextmanager
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestServer

from assistant_runtime import NativeDevice
from gateway import CONFIG_KEY, HTTP_KEY, REALTIME_URL_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession
from tests.test_input_observation import MessageSocket
from tests.test_realtime_close_lifecycle import DeviceSocket, FallbackBackend
from tests.test_voice_control import media_service


class SendingCloud(MessageSocket):
    def __init__(self, blocked_type=None, error=None):
        super().__init__()
        self.blocked_type = blocked_type
        self.error = error
        self.entered = asyncio.Event()
        self.release = asyncio.Event()
        self.attempts = []
        self.cancelled = 0
        self.active = 0
        self.maximum_active = 0

    async def send_json(self, event):
        self.attempts.append(event)
        self.active += 1
        self.maximum_active = max(self.maximum_active, self.active)
        try:
            if event.get('type') == self.blocked_type:
                self.entered.set()
                try:
                    await self.release.wait()
                except asyncio.CancelledError:
                    self.cancelled += 1
                    raise
            if self.error is not None:
                raise self.error
            await super().send_json(event)
        finally:
            self.active -= 1


class RealtimeSendLifecycleTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.runtime = SimpleNamespace(
            config={'max_connections':1}, voice_lock=asyncio.Lock(), voice_sessions=set(),
            last_device_state='idle', last_provider=None, last_cloud_ready=None,
            memory_service=None, media_service=None, device_music=None,
            connector_registry=SimpleNamespace(manifests=lambda:[]), operation_screen_feedback=AsyncMock(),
        )
        self.config = {'aliyun_api_key':'offline-fixture-key','aliyun_workspace_id':'offline-fixture-space','aliyun_model':'offline','aliyun_voice':'Tina','enable_search':False,'session_seconds':600,'idle_seconds':600}
        self.app = {CONFIG_KEY:self.config,RUNTIME_KEY:self.runtime,STORE_KEY:SimpleNamespace(recall_memory=lambda **_kwargs:[])}
        self.voice = VoiceSession(self.app, MessageSocket(), raw_pcm=True)
        self.voice.require_wake_phrase = True
        self.backend = RealtimeBackend(self.voice)
        self.cloud = SendingCloud('input_audio_buffer.append')
        self.backend.cloud = self.cloud
        self.voice.backend = self.backend
        self.tasks = []
        self.backends = [self.backend]
        original = aiohttp.TCPConnector._resolve_host

        async def loopback_only(connector, host, port, *args, **kwargs):
            if host not in {'127.0.0.1','::1','localhost'}:
                raise AssertionError('发送回归禁止非本机连接')
            return await original(connector, host, port, *args, **kwargs)

        guard = patch.object(aiohttp.TCPConnector,'_resolve_host',loopback_only)
        guard.start()
        self.addCleanup(guard.stop)

    async def asyncTearDown(self):
        self.cloud.release.set()
        for task in self.tasks:
            if not task.done():
                task.cancel()
        await asyncio.gather(*self.tasks, return_exceptions=True)
        for backend in self.backends:
            await backend.close()

    def launch(self, coroutine):
        task = asyncio.create_task(coroutine)
        self.tasks.append(task)
        return task

    @contextmanager
    def deadlines(self):
        recorded = []
        original = asyncio.timeout

        def record(delay):
            timeout = original(delay)
            recorded.append((asyncio.current_task(),delay,timeout))
            return timeout

        with patch('gateway.asyncio.timeout', side_effect=record):
            yield recorded

    def send_deadline(self, recorded, task):
        deadlines = [timeout for owner,delay,timeout in recorded if owner is task and delay == 5]
        self.assertEqual(len(deadlines), 1, '每次发送必须有覆盖等锁和socket的同一个5秒期限')
        return deadlines[0]

    async def boundary(self):
        crossed = asyncio.Event()
        asyncio.get_running_loop().call_soon(crossed.set)
        await crossed.wait()

    async def transport_error(self):
        self.cloud.blocked_type = None
        self.cloud.error = OSError('private-token-and-payload-must-not-reach-device')
        with self.assertRaises(RuntimeError) as raised:
            await self.backend.clear_audio()
        self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
        return raised.exception

    async def test_blocked_audio_uses_total_deadline_and_is_not_successful_append(self):
        with self.deadlines() as recorded:
            sending = self.launch(self.backend.send_user_audio(b'\1\0' * 320))
            await asyncio.wait_for(self.cloud.entered.wait(), 1)
            deadline = self.send_deadline(recorded, sending)
            deadline.reschedule(asyncio.get_running_loop().time())
            with self.assertRaises(RuntimeError) as raised:
                await asyncio.wait_for(sending, 1)
        self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
        self.assertTrue(deadline.expired())
        self.assertTrue(self.backend.send_failed)
        self.assertFalse(self.backend.connected)
        self.assertEqual(self.cloud.cancelled, 1)
        self.assertEqual(self.voice._wake_diagnostic['realtime_append_messages'], 0)
        with self.assertRaises(RuntimeError):
            await self.backend.request_response()
        self.assertEqual(len(self.cloud.attempts), 1)

    async def test_send_exception_preserves_cause_but_exposes_fixed_safe_error(self):
        error = await self.transport_error()
        self.assertIs(error.__cause__, self.cloud.error)
        self.assertNotIn('private-token', str(error))
        self.assertTrue(self.backend.send_failed)
        with self.assertRaises(RuntimeError):
            await self.backend.commit_audio()
        self.assertEqual(len(self.cloud.attempts), 1)

    async def test_cancelled_writer_poison_is_visible_before_next_waiter_can_write(self):
        sending = self.launch(self.backend.send_user_audio(b'\1\0'))
        await asyncio.wait_for(self.cloud.entered.wait(), 1)
        waiting = self.launch(self.backend.request_response())
        await self.boundary()
        self.assertEqual(len(self.cloud.attempts), 1)
        sending.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await sending
        with self.assertRaises(RuntimeError):
            await asyncio.wait_for(waiting, 1)
        self.assertTrue(self.backend.send_failed)
        self.assertEqual(len(self.cloud.attempts), 1)

    async def test_cancelled_lock_waiter_does_not_poison_or_cancel_healthy_writer(self):
        owner = self.launch(self.backend.send_user_audio(b'\1\0'))
        await asyncio.wait_for(self.cloud.entered.wait(), 1)
        waiting = self.launch(self.backend.request_response())
        await self.boundary()
        self.assertEqual(len(self.cloud.attempts), 1)
        waiting.cancel()
        with self.assertRaises(asyncio.CancelledError):
            await waiting
        self.assertFalse(self.backend.send_failed)
        self.assertFalse(owner.done())
        self.cloud.release.set()
        await asyncio.wait_for(owner, 1)
        await self.backend.request_response()
        self.assertTrue(self.backend.connected)
        self.assertEqual(self.cloud.maximum_active, 1)
        self.assertEqual(self.voice._wake_diagnostic['realtime_append_messages'], 1)

    async def test_lock_wait_consumes_same_budget_without_claiming_owner_frame_failed(self):
        with self.deadlines() as recorded:
            owner = self.launch(self.backend.send_user_audio(b'\1\0'))
            await asyncio.wait_for(self.cloud.entered.wait(), 1)
            waiting = self.launch(self.backend.request_response())
            await self.boundary()
            deadline = self.send_deadline(recorded, waiting)
            self.assertEqual(len(self.cloud.attempts), 1)
            deadline.reschedule(asyncio.get_running_loop().time())
            with self.assertRaises(RuntimeError):
                await asyncio.wait_for(waiting, 1)
            self.assertTrue(deadline.expired())
            self.assertFalse(self.backend.send_failed, '未写帧的等待者超时不能假称owner损坏')
            self.assertFalse(owner.done())
            self.cloud.release.set()
            await asyncio.wait_for(owner, 1)
        await self.backend.request_response()
        self.assertTrue(self.backend.connected)

    async def test_completed_action_with_blocked_receipt_is_not_repeated_or_followed_by_next_action(self):
        self.voice.require_wake_phrase = False
        self.cloud.blocked_type = 'conversation.item.create'
        self.voice.execute_tool = AsyncMock(return_value={'status':'pending','title':'local-result'})
        calls = [{'call_id':'first','name':'offline_action','arguments':'{}'},{'call_id':'second','name':'offline_action','arguments':'{}'}]
        with self.deadlines() as recorded:
            task = self.launch(self.voice.process_tools(calls, self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch))
            await asyncio.wait_for(self.cloud.entered.wait(), 1)
            self.send_deadline(recorded, task).reschedule(asyncio.get_running_loop().time())
            with self.assertRaises(RuntimeError):
                await asyncio.wait_for(task, 1)
        self.voice.execute_tool.assert_awaited_once()
        self.assertEqual(self.voice.tool_calls[(0,'first')]['state'], 'completed')
        self.assertEqual(self.voice.tool_calls[(0,'second')]['state'], 'aborted')
        self.assertFalse(any(event['type'] == 'response.create' for event in self.cloud.attempts))
        await self.voice.process_tools(calls, self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch)
        self.voice.execute_tool.assert_awaited_once()
        self.assertEqual(len(self.cloud.attempts), 1)

    async def test_process_tool_inner_transport_exception_escapes_without_unknown_receipt(self):
        error = await self.transport_error()
        self.voice.require_wake_phrase = False
        self.voice.execute_tool = AsyncMock(side_effect=error)
        self.voice._pause_tool_chain = AsyncMock()
        self.backend.send_tool_output = AsyncMock()
        with self.assertRaises(RuntimeError) as raised:
            await self.voice.process_tools([{'call_id':'first','name':'offline_action','arguments':'{}'}],0,response_id='offline-response',input_epoch=0)
        self.assertIs(raised.exception, error)
        self.voice._pause_tool_chain.assert_not_awaited()
        self.backend.send_tool_output.assert_not_awaited()

    async def test_execute_tool_transport_error_is_not_reported_as_local_business_unknown(self):
        error = await self.transport_error()
        self.voice.require_wake_phrase = False
        self.voice.backend = SimpleNamespace(send_failed=False)
        self.voice._execute_tool = AsyncMock(side_effect=error)
        self.voice._pause_tool_chain = AsyncMock()
        with self.assertRaises(RuntimeError) as raised:
            await self.voice.execute_tool('offline_action', {})
        self.assertIs(raised.exception, error)
        self.voice._pause_tool_chain.assert_not_awaited()
        self.runtime.operation_screen_feedback.assert_awaited_once()
        self.assertEqual(self.runtime.operation_screen_feedback.call_args.kwargs['phase'],'start')

    async def test_failed_transport_blocks_new_tool_before_effect_and_processing_feedback(self):
        await self.transport_error()
        self.voice.require_wake_phrase = False
        self.voice._execute_tool = AsyncMock(return_value={'status':'pending'})
        with self.assertRaises(RuntimeError) as raised:
            await self.voice.execute_tool('offline_action', {})
        self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
        self.voice._execute_tool.assert_not_awaited()
        self.runtime.operation_screen_feedback.assert_not_awaited()

    async def test_transport_failure_during_processing_feedback_prevents_effect_after_wait(self):
        self.voice.require_wake_phrase = False
        entered, release = asyncio.Event(), asyncio.Event()

        async def slow_start(_operation, **kwargs):
            self.assertEqual(kwargs['phase'], 'start')
            entered.set()
            await release.wait()

        self.runtime.operation_screen_feedback.side_effect = slow_start
        self.voice._execute_tool = AsyncMock(return_value={'status':'pending'})
        executing = self.launch(self.voice.execute_tool('offline_action', {}))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            await self.transport_error()
            release.set()
            with self.assertRaises(RuntimeError) as raised:
                await asyncio.wait_for(executing, 1)
            self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
            self.voice._execute_tool.assert_not_awaited()
            self.runtime.operation_screen_feedback.assert_awaited_once()
            self.assertEqual(self.runtime.operation_screen_feedback.call_args.kwargs['phase'], 'start')
        finally:
            release.set()
            await asyncio.gather(executing, return_exceptions=True)

    async def test_unhandled_control_primary_and_fallback_paths_propagate_transport_failure(self):
        error = await self.transport_error()
        for stage in ('primary','fallback'):
            with self.subTest(stage=stage):
                self.voice.control_response_reported = False
                self.voice.control_results = None
                self.voice.control_plan = {'status':'execute','actions':[]}
                original_plan = self.voice.control_plan
                self.voice.execute_control_plan = AsyncMock(return_value={'status':'verified'})
                self.voice.refresh_memory_context = AsyncMock(side_effect=error if stage == 'primary' else [ValueError('offline planner failure'),error])
                self.backend.request_response = AsyncMock()
                with self.assertRaises(RuntimeError) as raised:
                    await self.voice.finish_unhandled_control(self.voice.control_turn,self.voice.generation)
                self.assertIs(raised.exception,error)
                self.assertEqual(self.voice.refresh_memory_context.await_count,1 if stage == 'primary' else 2)
                self.backend.request_response.assert_not_awaited()
                if stage == 'primary':
                    self.assertIs(self.voice.control_plan,original_plan,'传输失败不能改写成解析失败/操作未执行')
                self.assertEqual(self.voice.executing_tools,0)

    async def test_failed_transport_blocks_authorized_direct_control_plan_before_invoke(self):
        self.runtime.media_service = media_service()
        self.runtime.home = SimpleNamespace(configured=False)
        self.voice.require_wake_phrase = True
        self.voice.wake_phrase_verified = True
        self.voice.last_user_text = '打开计算器'
        plan = {'status':'execute','actions':[{'tool':'desktop_open_app','arguments':{'app':'calculator'}}]}
        self.voice.control_plan = plan
        self.voice.state = AsyncMock()
        await self.transport_error()
        with self.assertRaises(RuntimeError) as raised:
            await self.voice.execute_control_plan()
        self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
        self.runtime.media_service.invoke.assert_not_awaited()
        self.voice.state.assert_not_awaited()
        self.assertIs(self.voice.control_plan, plan)
        self.assertIsNone(self.voice.control_results)

    async def test_transport_failure_during_control_executing_state_prevents_direct_effect(self):
        self.runtime.media_service = media_service()
        self.runtime.home = SimpleNamespace(configured=False)
        self.voice.require_wake_phrase = True
        self.voice.wake_phrase_verified = True
        self.voice.last_user_text = '打开计算器'
        plan = {'status':'execute','actions':[{'tool':'desktop_open_app','arguments':{'app':'calculator'}}]}
        self.voice.control_plan = plan
        entered, release = asyncio.Event(), asyncio.Event()

        async def slow_executing(state, name):
            self.assertEqual((state, name), ('executing','desktop_open_app'))
            entered.set()
            await release.wait()

        self.voice.state = AsyncMock(side_effect=slow_executing)
        executing = self.launch(self.voice.execute_control_plan())
        try:
            await asyncio.wait_for(entered.wait(),1)
            await self.transport_error()
            release.set()
            with self.assertRaises(RuntimeError) as raised:
                await asyncio.wait_for(executing,1)
            self.assertEqual(type(raised.exception).__name__, 'RealtimeTransportError')
            self.runtime.media_service.invoke.assert_not_awaited()
            self.voice.state.assert_awaited_once()
            self.assertIs(self.voice.control_plan, plan)
            self.assertEqual(self.voice.control_results, [])
        finally:
            release.set()
            await asyncio.gather(executing,return_exceptions=True)

    async def test_settings_send_deadline_prevents_ready_and_provider_fallback(self):
        cloud = SendingCloud('session.update')
        cloud.receive_event({'type':'session.created'})
        self.app[HTTP_KEY] = SimpleNamespace(ws_connect=AsyncMock(return_value=cloud))
        self.app[REALTIME_URL_KEY] = 'ws://127.0.0.1/offline-fixture'
        voice = VoiceSession(self.app, MessageSocket(), raw_pcm=True)
        fallback = FallbackBackend()
        with patch('gateway.provider_chain',return_value=['qwen_realtime','deepseek']), patch('gateway.ChatBackend',return_value=fallback), self.deadlines() as recorded:
            task = self.launch(voice.connect())
            try:
                await asyncio.wait_for(cloud.entered.wait(),1)
                self.send_deadline(recorded,task).reschedule(asyncio.get_running_loop().time())
                with self.assertRaises(RuntimeError):
                    await asyncio.wait_for(task,1)
                self.assertIsNone(voice.backend)
                self.assertIsNone(voice._wake_ready_at)
                self.assertIsNone(self.runtime.last_cloud_ready)
                fallback.connect.assert_not_awaited()
                self.assertTrue(cloud.closed)
                self.assertEqual(len(cloud.attempts),1)
            finally:
                cloud.release.set()
                if not task.done():task.cancel()
                await asyncio.gather(task,return_exceptions=True)
                await cloud.close()

    async def test_run_detects_failed_backend_even_when_separate_sender_cancel_was_consumed(self):
        self.voice.require_wake_phrase = False
        entered = asyncio.Event()

        async def ready():
            entered.set()

        self.voice.connect = ready
        running = self.launch(self.voice.run())
        await asyncio.wait_for(entered.wait(),1)
        sending = self.launch(self.backend.send_user_audio(b'\1\0'))
        await asyncio.wait_for(self.cloud.entered.wait(),1)
        sending.cancel()
        with self.assertRaises(asyncio.CancelledError):await sending
        await asyncio.wait_for(running,1)
        self.assertTrue(self.voice.closed)
        self.assertIsNone(self.voice.run_task)
        self.assertTrue(self.voice.socket.closed)
        self.assertTrue(self.cloud.closed)

    async def test_native_run_send_error_releases_owner_and_allows_new_session_without_private_alert(self):
        socket = DeviceSocket()
        sessions, backends = [], []

        def factory(conversation):
            voice = VoiceSession(self.app,conversation,raw_pcm=True)
            voice.require_wake_phrase = False
            backend = RealtimeBackend(voice)
            cloud = SendingCloud(error=OSError('private-provider-token-and-payload')) if not sessions else SendingCloud()
            backend.cloud = cloud
            voice.backend = backend
            async def ready():await voice.state('listening')
            voice.connect = ready
            sessions.append(voice);backends.append(backend)
            return voice

        device = NativeDevice(socket,self.runtime,factory)
        try:
            await device.start('manual')
            owner = device.task
            await asyncio.wait_for(socket.listening.wait(),1)
            device.conversation.queue.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY,data=b'\1\0'))
            await asyncio.wait_for(owner,1)
            self.assertEqual(self.runtime.voice_sessions,set())
            self.assertEqual(self.runtime.last_device_state,'idle')
            self.assertTrue(sessions[0].socket.closed)
            self.assertTrue(backends[0].cloud.closed)
            alerts = [event for event in socket.sent if event.get('type') == 'alert']
            self.assertTrue(alerts)
            self.assertNotIn('private-provider',str(alerts))
            socket.listening.clear()
            await device.start('manual')
            await asyncio.wait_for(socket.listening.wait(),1)
            self.assertEqual(len(sessions),2)
            self.assertTrue(backends[1].connected)
        finally:
            await device.end_conversation()

    async def test_real_aiohttp_writer_drain_path_timeout_is_terminal_and_closes_only_its_socket(self):
        server_release = asyncio.Event()
        async def peer(request):
            socket = web.WebSocketResponse()
            await socket.prepare(request)
            try:
                async for _message in socket:pass
                return socket
            finally:
                server_release.set()
                await socket.close()

        app = web.Application();app.router.add_get('/realtime',peer)
        server = TestServer(app);await server.start_server()
        client = aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20,sock_connect=10))
        cloud = await client.ws_connect(server.make_url('/realtime'))
        self.backend.cloud = cloud
        entered, release = asyncio.Event(), asyncio.Event()
        protocol = cloud._writer.protocol
        original_drain = type(protocol)._drain_helper

        async def controlled_drain(instance):
            if instance is protocol:
                entered.set()
                await release.wait()
                return
            await original_drain(instance)

        # 仅本机测试定点注入已安装aiohttp的真实writer drain路径，不模拟自然TCP耗尽。
        protocol.pause_writing()
        try:
            with patch.object(type(protocol),'_drain_helper',controlled_drain), self.deadlines() as recorded:
                sending = self.launch(self.backend.send_user_audio(b'\0\0' * 32768))
                await asyncio.wait_for(entered.wait(),1)
                self.send_deadline(recorded,sending).reschedule(asyncio.get_running_loop().time())
                with self.assertRaises(RuntimeError):await asyncio.wait_for(sending,1)
                self.assertTrue(self.backend.send_failed)
                self.assertFalse(self.backend.connected)
                with self.assertRaises(RuntimeError):await self.backend.request_response()
                protocol.resume_writing()
                await self.backend.close()
                self.assertTrue(cloud.closed)
                self.assertFalse(client.closed)
                await asyncio.wait_for(server_release.wait(),1)
        finally:
            release.set()
            if protocol._paused:protocol.resume_writing()
            await cloud.close()
            await client.close()
            await server.close()


if __name__ == '__main__':
    unittest.main()

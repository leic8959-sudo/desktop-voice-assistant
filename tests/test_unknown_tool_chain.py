"""结果未知后停止本轮自动工具链；真实Chat编排和Realtime会话，动作仅替身。"""

import asyncio
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock
from unittest.mock import patch

from chat_pipeline import ChatBackend, _Chunk
from assistant_core import AssistantStore
from gateway import CONFIG_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession
from tests.test_tool_activity import FakeCloud
from tests.test_chat_playback_handoff import RecordingDevice, MarkedTtsSocket


def chat_call(index, identity):
    return {'index':index, 'id':identity, 'function':{'name':'music_queue', 'arguments':'{"selection_id":"server-track"}'}}


def realtime_call(identity):
    return {'call_id':identity, 'name':'music_queue', 'arguments':'{"selection_id":"server-track"}'}


class UnknownToolChainTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.effects = []
        self.requests = []
        self.scripts = []
        self.backends = []
        self.directory = tempfile.TemporaryDirectory()
        self.store = AssistantStore(Path(self.directory.name) / 'unknown.sqlite3', 'offline-unknown')

    async def asyncTearDown(self):
        for backend in self.backends:
            await backend.close()
        self.store.close()
        self.directory.cleanup()

    async def partially_acted(self, *args, **kwargs):
        self.effects.append((copy.deepcopy(args), copy.copy(kwargs)))
        if len(self.effects) == 1:
            raise TimeoutError('private-provider-detail')
        return {'status':'queued'}

    def chat(self):
        session = SimpleNamespace(config={}, session_id='offline-unknown-chain', state=AsyncMock(), execute_tool=AsyncMock(side_effect=self.partially_acted), executing_tools=0)
        backend = ChatBackend(session, 'qwen_chat')
        backend._history = []
        backend._instructions = 'offline'
        backend._events = asyncio.Queue()
        backend._cancelled = set()
        backend._response_sequence = 0
        backend._active_response_id = backend._pending_history = None

        async def model(messages):
            self.requests.append({'response_id':backend._active_response_id, 'messages':copy.deepcopy(messages)})
            script = self.scripts.pop(0)
            yield _Chunk(tool_delta=script) if script else _Chunk()

        backend._request = model
        self.backends.append(backend)
        return backend

    async def realtime(self):
        music = SimpleNamespace(invoke=AsyncMock(side_effect=self.partially_acted), cancel_voice_request=AsyncMock())
        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=music, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock(), send_home_screen_card=AsyncMock())
        voice = VoiceSession({CONFIG_KEY:{}, RUNTIME_KEY:runtime, STORE_KEY:self.store}, SimpleNamespace(send_json=AsyncMock()), browser=True)
        backend = RealtimeBackend(voice)
        backend.cloud = FakeCloud()
        voice.backend = backend
        self.backends.append(backend)
        await voice.observe_user_text('把这首加入队列', source='text')
        return voice

    async def test_chat_unknown_stops_new_response_new_call_id_retry_in_same_user_turn(self):
        backend = self.chat()
        self.scripts = [[chat_call(0, 'first-write')], [chat_call(0, 'new-response-new-id')], []]
        with self.assertLogs('assistant.chat_pipeline', level='ERROR'):
            await backend._run_turn('把这首加入队列')
        self.assertEqual(len(self.effects), 1, '模型换response和call-id不能自动重试结果未知的动作')
        self.assertNotEqual(self.requests[0]['response_id'], self.requests[1]['response_id'])

    async def test_chat_unknown_stops_remaining_tools_in_the_same_response(self):
        backend = self.chat()
        self.scripts = [[chat_call(0, 'first-write'), chat_call(1, 'second-write')], []]
        with self.assertLogs('assistant.chat_pipeline', level='ERROR'):
            await backend._run_turn('模拟本轮队列操作')
        self.assertEqual(len(self.effects), 1)
        messages = self.requests[1]['messages']
        assistant = next(message for message in reversed(messages) if message['role'] == 'assistant')
        outputs = [message for message in messages if message['role'] == 'tool']
        self.assertEqual({item['id'] for item in assistant['tool_calls']}, {item['tool_call_id'] for item in outputs})
        self.assertEqual(len(outputs), 2)
        self.assertEqual(json.loads(outputs[0]['content'])['status'], 'unknown')
        self.assertNotEqual(json.loads(outputs[1]['content']).get('status'), 'queued')

    async def test_realtime_unknown_stops_new_call_id_until_real_new_user_input(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        await voice.backend.request_response()
        await voice.process_tools([realtime_call('provider-retry')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 1, '普通response请求不能解除unknown状态')
        await voice.observe_user_text('再把这首加入队列', source='text')
        await voice.process_tools([realtime_call('new-user-request')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 2, '真实新输入可开启独立请求，不能永久锁会话')

    async def test_realtime_unknown_stops_remaining_tools_in_same_batch(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write'), realtime_call('second-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 1)
        outputs = [json.loads(item['item']['output']) for item in voice.backend.cloud.messages if item.get('type') == 'conversation.item.create']
        self.assertEqual(len(outputs), 2)
        self.assertEqual(outputs[0]['status'], 'unknown')
        self.assertNotEqual(outputs[1].get('status'), 'queued')

    async def test_known_local_rejection_does_not_trip_unknown_stop(self):
        voice = await self.realtime()
        voice._execute_tool = AsyncMock(side_effect=[{'ok':False, 'status':'conflict'}, {'status':'queued'}])
        await voice.process_tools([realtime_call('known-conflict'), realtime_call('next-valid')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertEqual(voice._execute_tool.await_count, 2)

    async def test_paused_turn_blocks_direct_host_tool_and_semantic_control_fallback(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        result = await voice.execute_tool('create_task', {'title':'主机旁路不得写入', 'idempotency_key':'host-bypass'})
        self.assertEqual(result['code'], 'tool_chain_paused')
        self.assertEqual(self.store.list_tasks()['tasks'], [])
        voice.execute_control_plan = AsyncMock(return_value={'status':'none'})
        await voice.finish_unhandled_control(voice.control_turn, voice.generation)
        voice.execute_control_plan.assert_not_awaited()
        direct = [item for item in voice.backend.cloud.messages if item.get('type') == 'response.create']
        self.assertEqual(len(direct), 1)
        self.assertEqual(direct[0]['response']['tool_choice'], 'none')
        self.assertEqual(direct[0]['response']['tools'], [])

    async def test_only_unknown_contracts_pause_and_clear_this_session_pending(self):
        for result in ({'status':'unknown'}, {'status':'unconfirmed'}, {'status':'completed', 'result':{'status':'unknown'}}, {'request_accepted':True, 'target_observed':False}):
            with self.subTest(result=result):
                self.effects.clear()
                voice = await self.realtime()
                runtime = voice.app[RUNTIME_KEY]
                pending = {'music':voice, 'media':voice}
                async def cancel_music(session):
                    if pending['music'] is session:
                        pending['music'] = None
                async def cancel_media(session):
                    if pending['media'] is session:
                        pending['media'] = None
                runtime.device_music.cancel_voice_request = AsyncMock(side_effect=cancel_music)
                runtime.media_service = SimpleNamespace(cancel_voice_request=AsyncMock(side_effect=cancel_media))
                voice._execute_tool = AsyncMock(return_value=result)
                await voice.process_tools([realtime_call('result-write'), realtime_call('must-not-execute')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
                self.assertTrue(voice.tools_paused)
                self.assertEqual(voice._execute_tool.await_count, 1)
                self.assertEqual(pending, {'music':None, 'media':None})
                outputs = [json.loads(item['item']['output']) for item in voice.backend.cloud.messages if item.get('type') == 'conversation.item.create']
                self.assertEqual(outputs[0], result)
        voice = await self.realtime()
        voice._execute_tool = AsyncMock(side_effect=[{'status':[]}, {'status':'queued'}])
        await voice.process_tools([realtime_call('nonstandard-status'), realtime_call('next-after-nonstandard')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertFalse(voice.tools_paused)
        self.assertEqual(voice._execute_tool.await_count, 2)

    async def test_ignored_realtime_tool_disable_does_not_request_unbounded_final_responses(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        for index in range(5):
            await voice.process_tools([realtime_call(f'ignored-none-{index}')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 1)
        self.assertEqual(sum(item.get('type') == 'response.create' for item in voice.backend.cloud.messages), 1)

    async def test_new_input_waiting_for_media_cleanup_keeps_tools_paused_until_context_replaced(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        previous_epoch = voice.accepted_input_epoch
        entered, release = asyncio.Event(), asyncio.Event()

        async def slow_cancel(_session):
            entered.set()
            await release.wait()

        voice.app[RUNTIME_KEY].media_service = SimpleNamespace(cancel_voice_request=AsyncMock(side_effect=slow_cancel))
        observing = asyncio.create_task(voice.observe_user_text('把新的那首加入队列', source='text'))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            self.assertEqual(voice.accepted_input_epoch, previous_epoch)
            self.assertTrue(voice.tools_paused)
            result = await voice.execute_tool('create_task', {'title':'等待新上下文不能执行', 'idempotency_key':'not-yet-accepted'})
            self.assertEqual(result['code'], 'tool_chain_paused')
            self.assertEqual(self.store.list_tasks()['tasks'], [])
            release.set()
            await asyncio.wait_for(observing, 1)
            self.assertGreater(voice.accepted_input_epoch, previous_epoch)
            self.assertFalse(voice.tools_paused)
            self.assertEqual(voice.last_user_text, '把新的那首加入队列')
            await voice.process_tools([realtime_call('after-accepted')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
            self.assertEqual(len(self.effects), 2)
        finally:
            release.set()
            await asyncio.gather(observing, return_exceptions=True)

    async def test_old_control_lock_waiter_does_not_plan_or_modify_new_input_context(self):
        voice = await self.realtime()
        await voice.control_lock.acquire()
        scheduled = asyncio.Event()

        async def old_control():
            scheduled.set()
            return await voice.execute_control_plan()

        waiter = asyncio.create_task(old_control())
        try:
            await asyncio.wait_for(scheduled.wait(), 1)
            await voice.observe_user_text('把另一首加入队列', source='text')
            new_plan = {'status':'none', 'actions':[]}
            new_results = [{'new-input':'preserve'}]
            voice.control_plan, voice.control_results = new_plan, new_results
            with patch('gateway.plan_voice_control', new=AsyncMock()) as planner:
                voice.control_lock.release()
                result = await asyncio.wait_for(waiter, 1)
            self.assertEqual(result['status'], 'superseded')
            planner.assert_not_awaited()
            self.assertIs(voice.control_plan, new_plan)
            self.assertIs(voice.control_results, new_results)
        finally:
            if voice.control_lock.locked():
                voice.control_lock.release()
            if not waiter.done():
                waiter.cancel()
            await asyncio.gather(waiter, return_exceptions=True)

    async def test_chat_final_explanation_finishes_audio_without_media_handoff(self):
        device = RecordingDevice()
        media = SimpleNamespace(pending=None, queue=['keep-media'], voice_response_finished=AsyncMock())
        music = SimpleNamespace(pending=None, queue=['keep-music'], voice_response_finished=AsyncMock())

        async def cancel_media(session):
            if media.pending is session:
                media.pending = None

        async def cancel_music(session):
            if music.pending is session:
                music.pending = None

        media.cancel_voice_request = AsyncMock(side_effect=cancel_media)
        music.cancel_voice_request = AsyncMock(side_effect=cancel_music)
        runtime = SimpleNamespace(media_service=media, device_music=music, memory_service=None, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        config = {'enable_search':False, 'chat_tts_model':'offline-tts', 'chat_tts_voice':'offline-voice', 'dashscope_ws_url':'https://offline.invalid/inference', 'aliyun_api_key':'offline-placeholder', 'idle_seconds':600, 'session_seconds':600}
        voice = VoiceSession({CONFIG_KEY:config, RUNTIME_KEY:runtime}, device, browser=True)
        backend = ChatBackend(voice, 'qwen_chat')
        voice.backend = backend
        self.backends.append(backend)
        backend._history = []
        backend._instructions = 'offline'
        backend._events = asyncio.Queue()
        backend._cancelled = set()
        backend._response_sequence = backend._turn_generation = 0
        backend._pending_history = backend._active_response_id = None
        socket = MarkedTtsSocket(b'\x03\x00' * 48)
        backend.client = SimpleNamespace(ws_connect=AsyncMock(return_value=socket))
        rounds = 0

        async def model(_messages):
            nonlocal rounds
            rounds += 1
            if rounds == 1:
                yield _Chunk(tool_delta=[chat_call(0, 'first-write')])
            else:
                yield _Chunk(content='操作结果待核实，请先查询实际状态。')

        async def uncertain(*_args, **_kwargs):
            self.effects.append('possibly-queued')
            media.pending = music.pending = voice
            raise TimeoutError('private-provider-detail')

        music.invoke = AsyncMock(side_effect=uncertain)
        backend._request = model
        cloud = asyncio.create_task(voice.cloud_output())
        playback = asyncio.create_task(voice.play_output())
        try:
            with self.assertLogs('assistant', level='ERROR'):
                await backend.send_user_text('把这首加入队列')
                await device.wait(lambda: len(device.tts('stop')) == 1)
            drain = backend._turn_drain_task
            if drain is not None:
                await asyncio.wait_for(asyncio.shield(drain), 1)
            self.assertEqual(len(device.tts('start')), 1)
            self.assertEqual(len(device.audio), 1)
            self.assertEqual(device.audio[0][1], b'\x03\x00' * 48)
            self.assertTrue(any('待核实' in item.get('message', '') for item in device.json if item.get('type') == 'alert'))
            self.assertFalse(voice.response_completed)
            self.assertIsNone(media.pending)
            self.assertIsNone(music.pending)
            self.assertEqual((media.queue, music.queue), (['keep-media'], ['keep-music']))

            class StopWatchdog(Exception):
                pass
            cycles = 0
            async def tick(_seconds):
                nonlocal cycles
                cycles += 1
                if cycles > 1:
                    raise StopWatchdog
            with patch('gateway.asyncio.sleep', tick), self.assertRaises(StopWatchdog):
                await voice.watchdog()
            media.voice_response_finished.assert_not_awaited()
            music.voice_response_finished.assert_not_awaited()
        finally:
            cloud.cancel(); playback.cancel()
            await asyncio.gather(cloud, playback, return_exceptions=True)

    async def test_chat_unknown_final_stream_fallback_cannot_restore_tools(self):
        backend = self.chat()
        backend.config.update({'chat_max_tokens':128, 'qwen_chat_thinking':None})
        backend._tools = [{'type':'function', 'function':{'name':'music_queue'}}]
        backend._chat_model = 'offline-model'
        backend._chat_key = 'offline-placeholder'
        backend._chat_url = 'https://offline.invalid/chat/completions'
        backend._streaming = True
        calls = []

        class Response:
            def __init__(self, status, chunks=(), payload=None):
                self.status = status
                self.lines = list(chunks)
                self.payload = payload
                self.content = self
            async def __aenter__(self): return self
            async def __aexit__(self, *_args): return False
            async def readline(self): return self.lines.pop(0) if self.lines else b''
            async def text(self): return 'synthetic stream rejection'
            async def json(self, **_kwargs): return self.payload

        delta = {'choices':[{'delta':{'tool_calls':[chat_call(0, 'first-write')]}}]}
        responses = [Response(200, [b'data: ' + json.dumps(delta).encode() + b'\n', b'data: [DONE]\n']), Response(400), Response(200, payload={'choices':[{'message':{'content':''}}]})]
        def post(_url, **options):
            calls.append(copy.deepcopy(options['json']))
            return responses.pop(0)

        backend.client = SimpleNamespace(post=post)
        backend._request = ChatBackend._request.__get__(backend, ChatBackend)
        with self.assertLogs('assistant.chat_pipeline', level='WARNING'):
            await backend._run_turn('模拟队列操作')
        self.assertEqual(len(self.effects), 1)
        self.assertEqual(len(calls), 3)
        for payload in calls[1:]:
            self.assertEqual(payload['tool_choice'], 'none')
            self.assertNotIn('tools', payload)
        self.assertEqual(calls[2]['stream'], False)

    async def test_realtime_vad_without_accepted_transcript_cannot_unlock_unknown_turn(self):
        voice = await self.realtime()
        with self.assertLogs('assistant', level='ERROR'):
            await voice.process_tools([realtime_call('first-write')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
        consumed = asyncio.Event()

        async def events():
            yield {'type':'input_audio_buffer.speech_started'}
            yield {'type':'response.text.done', 'text':'VAD synchronization'}
            await asyncio.Event().wait()

        async def send(payload):
            if payload.get('text') == 'VAD synchronization':
                consumed.set()

        voice.backend.events = events
        voice.socket.send_json.side_effect = send
        cloud = asyncio.create_task(voice.cloud_output())
        try:
            await asyncio.wait_for(consumed.wait(), 1)
            await voice.process_tools([{'call_id':'vad-only-retry', 'name':'create_task', 'arguments':'{"title":"VAD不能授权新写入"}'}], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
            self.assertEqual(self.store.list_tasks()['tasks'], [], 'VAD开始不是新的已接受用户授权，不能写入真实SQLite')
        finally:
            cloud.cancel()
            await asyncio.gather(cloud, return_exceptions=True)

    async def test_old_tool_unknown_after_new_input_does_not_pause_or_reply_in_new_epoch(self):
        voice = await self.realtime()
        entered, release = asyncio.Event(), asyncio.Event()

        async def delayed(*args, **kwargs):
            self.effects.append('possible-action')
            if len(self.effects) == 1:
                entered.set()
                await release.wait()
                raise TimeoutError('old-private-detail')
            return {'status':'queued'}

        voice.app[RUNTIME_KEY].device_music.invoke.side_effect = delayed
        old = asyncio.create_task(voice.process_tools([realtime_call('old-user-call')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            await voice.observe_user_text('把新的这首加入队列', source='text')
            await voice.process_tools([realtime_call('new-user-call')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
            responses_before = sum(item.get('type') == 'response.create' for item in voice.backend.cloud.messages)
            release.set()
            with self.assertLogs('assistant', level='ERROR'):
                await asyncio.wait_for(old, 1)
            self.assertEqual(sum(item.get('type') == 'response.create' for item in voice.backend.cloud.messages), responses_before, '旧工具不得请求新输入后的旧回答')
            outputs = [item['item']['call_id'] for item in voice.backend.cloud.messages if item.get('type') == 'conversation.item.create']
            self.assertNotIn('old-user-call', outputs)
            await voice.process_tools([realtime_call('new-user-second-call')], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
            self.assertEqual(len(self.effects), 3, '旧unknown不能暂停已开启的新用户epoch')
        finally:
            release.set()
            await asyncio.gather(old, return_exceptions=True)

    async def test_cancellation_still_propagates_without_replaying_action(self):
        backend = self.chat()
        entered = asyncio.Event()

        async def awaiting_receipt(*args, **kwargs):
            self.effects.append('possibly-acted')
            entered.set()
            await asyncio.Event().wait()

        backend.session.execute_tool.side_effect = awaiting_receipt
        self.scripts = [[chat_call(0, 'first-write'), chat_call(1, 'second-write')], []]
        task = asyncio.create_task(backend._run_turn('模拟队列操作'))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            task.cancel()
            with self.assertRaises(asyncio.CancelledError):
                await task
            self.assertEqual(self.effects, ['possibly-acted'])
            self.assertEqual(backend.session.executing_tools, 0)
        finally:
            if not task.done():
                task.cancel()
            await asyncio.gather(task, return_exceptions=True)


if __name__ == '__main__':
    unittest.main()

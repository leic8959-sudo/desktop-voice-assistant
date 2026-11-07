"""本地冲突/拒绝与未知副作用：真实会话及临时SQLite，不调用真实工具。"""

import asyncio
from datetime import datetime, timedelta, timezone
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

from assistant_core import AssistantStore, IdempotencyConflict, LeaseConflict, NotFoundError
from chat_pipeline import ChatBackend
from gateway import CONFIG_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession
from src.assistant_app.application.screen_feedback import feedback_card
from tests.test_tool_activity import FakeCloud


class ToolOutcomeTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.store = AssistantStore(Path(self.directory.name) / 'tools.sqlite3', 'offline-tool-outcome')
        self.sequence = 0

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def session(self, provider):
        cards = []

        async def feedback(operation, result=None, **kwargs):
            if operation is not None:
                cards.append(feedback_card(operation, result, phase=kwargs.get('phase', 'result'), error=kwargs.get('error')))

        runtime = SimpleNamespace(notifications={}, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=feedback, media_service=None, device_music=SimpleNamespace(invoke=AsyncMock(return_value={'status':'requested'})))
        voice = VoiceSession({CONFIG_KEY:{}, STORE_KEY:self.store, RUNTIME_KEY:runtime}, SimpleNamespace(send_json=AsyncMock()), browser=True)
        if provider == 'realtime':
            backend = RealtimeBackend(voice)
            backend.cloud = FakeCloud()
        else:
            backend = ChatBackend(voice, 'qwen_chat')
        voice.backend = backend
        return voice, runtime, cards

    def stale_arguments(self):
        self.sequence += 1
        now = datetime.now(timezone.utc)
        item = self.store.create_task('离线改期', now + timedelta(days=1), f'outcome-{self.sequence}')
        args = {'task_id':item['task_id'], 'expected_updated_at':item['updated_at'], 'due_at':(now + timedelta(days=2)).isoformat()}
        self.store.reschedule_task(item['task_id'], item['updated_at'], now + timedelta(days=3))
        return args

    async def result(self, voice, provider, name, arguments, call_id='offline-call'):
        if provider == 'realtime':
            await voice.process_tools([{'call_id':call_id, 'name':name, 'arguments':json.dumps(arguments)}], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
            outputs = [event['item']['output'] for event in voice.backend.cloud.messages if event.get('type') == 'conversation.item.create']
            return json.loads(outputs[-1])
        return await voice.backend._execute_tool({'id':call_id, 'name':name, 'arguments':json.dumps(arguments)})

    async def test_stale_reschedule_is_conflict_for_both_providers_and_call_is_not_repeated(self):
        for provider in ('realtime', 'chat'):
            with self.subTest(provider=provider):
                voice, _runtime, cards = self.session(provider)
                arguments = self.stale_arguments()
                saved = next(item for item in self.store.list_tasks()['tasks'] if item['task_id'] == arguments['task_id'])
                original = voice._execute_tool
                voice._execute_tool = AsyncMock(wraps=original)
                with self.assertLogs(level='ERROR'):
                    first = await self.result(voice, provider, 'reschedule_task', arguments)
                    second = await self.result(voice, provider, 'reschedule_task', arguments) if provider == 'realtime' else first
                self.assertEqual(first.get('status'), 'conflict')
                self.assertEqual(first.get('code'), 'local_record_conflict')
                self.assertIs(first.get('retryable'), False)
                self.assertEqual(second, first)
                self.assertEqual(voice._execute_tool.await_count, 1)
                self.assertEqual(cards[-1]['status'], 'error')
                self.assertIn('记录已变化', cards[-1]['detail'])
                current = next(item for item in self.store.list_tasks()['tasks'] if item['task_id'] == arguments['task_id'])
                self.assertEqual(current, saved)

    async def test_music_requires_user_context_before_invoking_music_service(self):
        for provider in ('realtime', 'chat'):
            with self.subTest(provider=provider):
                voice, runtime, _cards = self.session(provider)
                voice.last_user_text = ''
                original = voice._execute_tool
                voice._execute_tool = AsyncMock(wraps=original)
                with self.assertLogs(level='ERROR'):
                    first = await self.result(voice, provider, 'music_play', {'selection_id':'server-selection'})
                    second = await self.result(voice, provider, 'music_play', {'selection_id':'server-selection'}) if provider == 'realtime' else first
                self.assertEqual(first.get('status'), 'blocked')
                self.assertEqual(first.get('code'), 'local_tool_rejected')
                self.assertIs(first.get('retryable'), False)
                self.assertEqual(first, second)
                self.assertEqual(voice._execute_tool.await_count, 1)
                runtime.device_music.invoke.assert_not_awaited()
                self.assertNotIn("selection_id", str(first))

    async def test_external_permission_or_timeout_after_side_effect_remains_unknown(self):
        private = 'https://private.invalid/action?token=do-not-echo'
        for provider in ('realtime', 'chat'):
            for error_type in (PermissionError, TimeoutError, ValueError, IdempotencyConflict, LeaseConflict, NotFoundError):
                with self.subTest(provider=provider, error=error_type.__name__):
                    voice, runtime, _cards = self.session(provider)
                    voice.last_user_text = '播放歌曲'
                    side_effects = []

                    async def external(*_args, **_kwargs):
                        side_effects.append('provider-may-have-acted')
                        raise error_type(private)

                    runtime.device_music.invoke = AsyncMock(side_effect=external)
                    with self.assertLogs(level='ERROR'):
                        first = await self.result(voice, provider, 'music_play', {'selection_id':'server-selection'})
                        second = await self.result(voice, provider, 'music_play', {'selection_id':'server-selection'}) if provider == 'realtime' else first
                    self.assertEqual(first.get('status'), 'unknown')
                    self.assertEqual(first.get('code'), 'tool_result_unknown')
                    self.assertEqual(first, second)
                    self.assertNotIn(private, str(first))
                    self.assertEqual(side_effects, ['provider-may-have-acted'])

    async def test_missing_local_record_is_not_found_and_preserves_domain_exception_contract(self):
        for provider in ('realtime', 'chat'):
            with self.subTest(provider=provider):
                voice, _runtime, cards = self.session(provider)
                arguments = {'task_id':'missing-local-record', 'expected_updated_at':'offline-version', 'due_at':'2035-11-05T03:00:00Z'}
                with self.assertRaises(NotFoundError):
                    await voice.execute_tool('reschedule_task', arguments)
                with self.assertLogs(level='ERROR'):
                    result = await self.result(voice, provider, 'reschedule_task', arguments)
                self.assertEqual(result.get('status'), 'not_found')
                self.assertEqual(result.get('code'), 'local_record_not_found')
                self.assertEqual(cards[-1]['status'], 'error')
                self.assertNotIn('missing-local-record', str(result))

    async def test_external_permission_is_not_a_local_screen_block(self):
        voice, _runtime, cards = self.session('chat')
        effects = []

        async def partially_acted(_name, _args):
            effects.append('already-submitted')
            raise PermissionError('private-provider-detail')

        voice._execute_tool = partially_acted
        with self.assertLogs(level='ERROR'):
            result = await self.result(voice, 'chat', 'reschedule_task', {})
        self.assertEqual(result.get('status'), 'unknown')
        self.assertEqual(cards[-1]['status'], 'unknown')
        self.assertEqual(effects, ['already-submitted'])

    async def test_cancellation_propagates_without_result_cache_or_response_restart(self):
        for provider in ('realtime', 'chat'):
            with self.subTest(provider=provider):
                voice, runtime, _cards = self.session(provider)
                voice.last_user_text = '播放歌曲'
                runtime.device_music.invoke = AsyncMock(side_effect=asyncio.CancelledError())
                with self.assertRaises(asyncio.CancelledError):
                    await self.result(voice, provider, 'music_play', {'selection_id':'server-selection'})
                if provider == 'realtime':
                    record = voice.tool_calls[(voice.accepted_input_epoch, 'offline-call')]
                    self.assertEqual(record['state'], 'aborted')
                    self.assertIsNone(record['result'])
                else:
                    self.assertNotIn('offline-call', voice.tool_calls)
                self.assertEqual(voice.executing_tools, 0)
                if provider == 'realtime':
                    self.assertFalse(any(event.get('type') == 'response.create' for event in voice.backend.cloud.messages))

    async def test_invalid_json_or_nonobject_arguments_are_consistent_before_tool_execution(self):
        expected = {'ok':False, 'status':'invalid_arguments', 'code':'invalid_tool_arguments', 'retryable':False, 'error':'工具参数无效，请补齐或修正后再请求'}
        for raw in ('{"private-token-url":', '[]', '', None):
            for provider in ('realtime', 'chat'):
                with self.subTest(provider=provider, raw=raw):
                    voice, _runtime, _cards = self.session(provider)
                    voice.execute_tool = AsyncMock()
                    with self.assertLogs(level='ERROR') as logs:
                        if provider == 'realtime':
                            call = {'call_id':'invalid-call', 'name':'music_play', 'arguments':raw}
                            await voice.process_tools([call, call], voice.generation, response_id='offline-response', input_epoch=voice.accepted_input_epoch)
                            outputs = [event['item']['output'] for event in voice.backend.cloud.messages if event.get('type') == 'conversation.item.create']
                            result = json.loads(outputs[-1])
                        else:
                            result = await voice.backend._execute_tool({'id':'invalid-call', 'name':'music_play', 'arguments':raw})
                    self.assertEqual(result, expected)
                    voice.execute_tool.assert_not_awaited()
                    self.assertEqual(voice.executing_tools, 0)
                    self.assertNotIn('private-token-url', str(result))
                    self.assertIn('Traceback', '\n'.join(logs.output))


if __name__ == '__main__':
    unittest.main()

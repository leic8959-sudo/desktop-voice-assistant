"""Realtime工具身份并发：真实VoiceSession/RealtimeBackend，动作与云端均为替身。"""

import asyncio
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

from assistant_core import AssistantStore
from gateway import CONFIG_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, VoiceSession
from tests.test_tool_activity import FakeCloud


def tool(identity='call-1', *, name='music_queue', selection='first', response='response-stable'):
    return {'call_id':identity, 'name':name, 'arguments':json.dumps({'selection_id':selection}), 'response_id':response}


class RealtimeToolIdentityTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.effects = []

        async def execute(*args, **kwargs):
            self.effects.append((args, kwargs))
            return {'status':'queued', 'sequence':len(self.effects)}

        self.music = SimpleNamespace(invoke=AsyncMock(side_effect=execute), cancel_voice_request=AsyncMock())
        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=self.music, connector_registry=SimpleNamespace(manifests=lambda: []), operation_screen_feedback=AsyncMock())
        self.voice = VoiceSession({CONFIG_KEY:{}, RUNTIME_KEY:runtime}, SimpleNamespace(send_json=AsyncMock(), closed=False), browser=True)
        self.backend = RealtimeBackend(self.voice); self.backend.cloud = FakeCloud(); self.voice.backend = self.backend
        await self.voice.observe_user_text('把这首加入队列', source='text')

    async def asyncTearDown(self):
        await self.backend.close()

    def outputs(self):
        return [json.loads(event['item']['output']) for event in self.backend.cloud.messages if event.get('type') == 'conversation.item.create']

    async def entered_boundary(self):
        boundary = asyncio.Event()
        asyncio.get_running_loop().call_soon(boundary.set)
        await boundary.wait()

    async def test_concurrent_same_identity_same_arguments_executes_once(self):
        entered, release = asyncio.Event(), asyncio.Event()
        async def slow(*args, **kwargs):
            self.effects.append((args, kwargs)); entered.set(); await release.wait()
            return {'status':'queued', 'sequence':len(self.effects)}
        self.music.invoke.side_effect = slow
        first = asyncio.create_task(self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch))
        second = None
        try:
            await asyncio.wait_for(entered.wait(), 1)
            second = asyncio.create_task(self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch))
            await self.entered_boundary()
            self.assertEqual(len(self.effects), 1, '第二协程不能在第一结果写缓存前再次执行')
            release.set(); await asyncio.wait_for(asyncio.gather(first, second), 1)
            self.assertTrue(self.outputs())
            self.assertEqual(len(self.outputs()), 1)
            self.assertEqual(sum(event.get('type') == 'response.create' for event in self.backend.cloud.messages), 1)
            self.assertTrue(all(result == self.outputs()[0] for result in self.outputs()))
        finally:
            release.set(); await asyncio.gather(*(task for task in (first, second) if task is not None), return_exceptions=True)

    async def test_same_identity_changed_arguments_or_name_is_not_old_success(self):
        await self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch)
        for conflicting in (tool(selection='different'), tool(name='music_stop')):
            with self.subTest(conflicting=conflicting):
                await self.voice.process_tools([conflicting], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch)
                self.assertEqual(len(self.effects), 1)
                self.assertEqual(self.outputs()[-1].get('status'), 'invalid_arguments')

    async def test_late_conflict_in_batch_rejects_before_any_side_effect(self):
        for conflicting in (tool('duplicate', selection='different'), tool('duplicate', name='music_stop')):
            with self.subTest(conflicting=conflicting):
                self.effects.clear()
                await self.voice.process_tools([tool('unrelated-valid'), tool('duplicate'), conflicting], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch)
                self.assertEqual(self.effects, [], '后置冲突也必须在首个副作用之前拒绝整批')

    async def test_new_accepted_input_reused_call_id_does_not_get_old_result(self):
        await self.voice.process_tools([tool(response='first-response')], self.voice.generation, response_id='first-response', input_epoch=self.voice.accepted_input_epoch)
        await self.voice.observe_user_text('把另一首加入队列', source='text')
        await self.voice.process_tools([tool(selection='new-user-track', response='new-user-response')], self.voice.generation, response_id='new-user-response', input_epoch=self.voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 2)
        self.assertEqual(self.effects[1][0][1]['selection_id'], 'new-user-track')
        self.assertEqual(self.outputs()[-1].get('sequence'), 2)

    async def test_duplicate_has_no_waiter_and_cancelled_owner_identity_cannot_replay(self):
        entered, release = asyncio.Event(), asyncio.Event()
        async def slow(*args, **kwargs):
            self.effects.append((args, kwargs)); entered.set(); await release.wait(); return {'status':'queued'}
        self.music.invoke.side_effect = slow
        owner = asyncio.create_task(self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch))
        waiter = None
        try:
            await asyncio.wait_for(entered.wait(), 1)
            waiter = asyncio.create_task(self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch))
            await asyncio.wait_for(waiter, 1)
            self.assertFalse(owner.done())
            self.assertEqual(len(self.effects), 1)
            self.assertEqual(self.backend.cloud.messages, [])
            owner.cancel()
            with self.assertRaises(asyncio.CancelledError): await owner
            await self.voice.process_tools([tool()], self.voice.generation, response_id='offline-response', input_epoch=self.voice.accepted_input_epoch)
            self.assertEqual(len(self.effects), 1)
            self.assertEqual(self.backend.cloud.messages, [])
            record = self.voice.tool_calls[(self.voice.accepted_input_epoch, 'call-1')]
            self.assertEqual(record['state'], 'aborted')
            self.assertIsNone(record['result'])
        finally:
            release.set(); await asyncio.gather(*(task for task in (owner, waiter) if task is not None), return_exceptions=True)

    async def test_mixed_new_and_inflight_identity_batch_cannot_execute_new_call(self):
        entered, release = asyncio.Event(), asyncio.Event()
        async def slow(*args, **kwargs):
            self.effects.append((args, kwargs)); entered.set(); await release.wait(); return {'status':'queued'}
        self.music.invoke.side_effect = slow
        owner = asyncio.create_task(self.voice.process_tools([tool()], self.voice.generation, response_id='response-one', input_epoch=self.voice.accepted_input_epoch))
        try:
            await asyncio.wait_for(entered.wait(), 1)
            with self.assertLogs('assistant', level='ERROR'):
                await self.voice.process_tools([tool(), tool('fresh')], self.voice.generation, response_id='response-one', input_epoch=self.voice.accepted_input_epoch)
            self.assertEqual(len(self.effects), 1)
            self.assertTrue(all(result['status'] == 'invalid_arguments' for result in self.outputs()))
        finally:
            release.set(); await asyncio.gather(owner, return_exceptions=True)

    async def test_same_epoch_cross_response_call_id_reuse_is_rejected(self):
        await self.voice.process_tools([tool()], self.voice.generation, response_id='response-one', input_epoch=self.voice.accepted_input_epoch)
        with self.assertLogs('assistant', level='ERROR'):
            await self.voice.process_tools([tool()], self.voice.generation, response_id='response-two', input_epoch=self.voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 1)
        self.assertEqual(self.outputs()[-1]['status'], 'invalid_arguments')

    async def test_new_input_reused_long_call_id_creates_two_real_local_tasks(self):
        with tempfile.TemporaryDirectory() as directory, AssistantStore(Path(directory) / 'identity.sqlite3', 'offline-realtime') as store:
            self.voice.app[STORE_KEY] = store
            identity = 'vendor-' + 'x' * 200
            first = {'call_id':identity, 'name':'create_task', 'arguments':'{"title":"第一条"}'}
            await self.voice.process_tools([first], self.voice.generation, response_id='first-response', input_epoch=self.voice.accepted_input_epoch)
            await self.voice.observe_user_text('请创建第二条待办', source='text')
            second = {**first, 'arguments':'{"title":"第二条"}'}
            await self.voice.process_tools([second], self.voice.generation, response_id='second-response', input_epoch=self.voice.accepted_input_epoch)
            rows = store.list_tasks()['tasks']
            self.assertEqual({row['title'] for row in rows}, {'第一条','第二条'})
            self.assertEqual(len({row['idempotency_key'] for row in rows}), 2)
            self.assertTrue(all(len(row['idempotency_key'].encode()) <= 128 for row in rows))

    async def test_real_cloud_done_scope_is_captured_before_blocked_usage_send(self):
        usage_entered, release, sync = asyncio.Event(), asyncio.Event(), asyncio.Event()
        async def events():
            yield {'type':'response.done','response':{'id':'real-old-response','status':'completed','output':[{**tool(), 'type':'function_call'}]}}
            yield {'type':'response.text.done','text':'scope-synchronized'}
            await asyncio.Event().wait()
        async def send(event):
            if event.get('type') == 'usage':
                usage_entered.set(); await release.wait()
            elif event.get('text') == 'scope-synchronized': sync.set()
        self.backend.events = events
        self.voice.socket.send_json.side_effect = send
        cloud = asyncio.create_task(self.voice.cloud_output())
        try:
            await asyncio.wait_for(usage_entered.wait(), 1)
            await self.voice.observe_user_text('把另一首加入队列', source='text')
            control_turn = self.voice.control_turn
            self.voice.response_completed = True
            self.voice.current_response = 'new-user-response'
            self.voice.current_playback_id = 'new-user-playback'
            marker = (self.voice.generation, 'new-user-playback', b'new-user-pcm')
            self.voice.playback.put_nowait(marker)
            release.set(); await asyncio.wait_for(sync.wait(), 1)
            await asyncio.gather(*self.voice.tasks)
            self.assertEqual(self.effects, [])
            self.assertEqual(self.outputs(), [])
            self.assertEqual(self.voice.control_turn, control_turn)
            self.assertTrue(self.voice.response_completed)
            self.assertEqual(self.voice.current_response, 'new-user-response')
            self.assertEqual(self.voice.playback.qsize(), 1)
            self.assertEqual(self.voice.playback.get_nowait(), marker)
        finally:
            release.set(); cloud.cancel(); await asyncio.gather(cloud, return_exceptions=True)

    async def test_session_identity_capacity_and_batch_bounds_fail_before_new_actions(self):
        for index in range(100):
            await self.voice.process_tools([tool(f'call-{index}')], self.voice.generation, response_id='response-capacity', input_epoch=self.voice.accepted_input_epoch)
        with self.assertLogs('assistant', level='ERROR'):
            await self.voice.process_tools([tool('call-101')], self.voice.generation, response_id='response-capacity', input_epoch=self.voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 100)
        self.assertEqual(len(self.voice.tool_calls), 100)
        self.assertEqual(self.outputs()[-1]['status'], 'invalid_arguments')

    async def test_bad_late_arguments_reject_whole_batch_before_first_action(self):
        for invalid in ('[]', '{"private":NaN}', '{"value":"' + 'x' * 16384 + '"}'):
            with self.subTest(invalid=invalid):
                batch = [tool('first-valid'), {**tool('bad'), 'arguments':invalid}]
                with self.assertLogs('assistant', level='ERROR'):
                    await self.voice.process_tools(batch, self.voice.generation, response_id='response-invalid', input_epoch=self.voice.accepted_input_epoch)
                self.assertEqual(self.effects, [])

    async def test_same_batch_equivalent_json_ids_are_merged_with_one_output(self):
        first = tool()
        first['arguments'] = '{"selection_id":"first","options":{"a":1,"b":2}}'
        duplicate = {**first,'arguments':' { "options": {"b":2,"a":1}, "selection_id":"first" } '}
        await self.voice.process_tools([first, duplicate], self.voice.generation, response_id='same-batch', input_epoch=self.voice.accepted_input_epoch)
        self.assertEqual(len(self.effects), 1)
        self.assertEqual(len(self.outputs()), 1)
        self.assertEqual(sum(item.get('type') == 'response.create' for item in self.backend.cloud.messages), 1)

    async def test_oversized_batch_and_invalid_scope_never_emit_per_call_cloud_messages(self):
        await self.voice.process_tools([tool(f'huge-{index}') for index in range(1000)], self.voice.generation, response_id='oversized-response', input_epoch=self.voice.accepted_input_epoch)
        await self.voice.process_tools([tool()], True, response_id='invalid-generation', input_epoch=self.voice.accepted_input_epoch)
        await self.voice.process_tools([tool()], self.voice.generation, response_id='', input_epoch=self.voice.accepted_input_epoch)
        await self.voice.process_tools([tool()], self.voice.generation, response_id='invalid-epoch', input_epoch=True)
        self.assertEqual(self.effects, [])
        self.assertEqual(self.backend.cloud.messages, [])
        self.assertEqual(self.voice.tool_calls, {})
        self.assertEqual(self.voice.socket.send_json.await_count, 4)

    async def test_invalid_batch_stops_old_outputs_after_new_input_during_first_send(self):
        entered, release = asyncio.Event(), asyncio.Event()
        original = self.backend.send_tool_output
        async def blocked_output(call_id, output):
            await original(call_id, output)
            entered.set(); await release.wait()
        self.backend.send_tool_output = blocked_output
        batch = [tool('invalid-first'), tool('duplicate'), tool('duplicate', selection='changed')]
        with self.assertLogs('assistant', level='ERROR'):
            old = asyncio.create_task(self.voice.process_tools(batch, self.voice.generation, response_id='old-invalid-response', input_epoch=self.voice.accepted_input_epoch))
            try:
                await asyncio.wait_for(entered.wait(), 1)
                await self.voice.observe_user_text('把另一首加入队列', source='text')
                release.set(); await asyncio.wait_for(old, 1)
                outputs = [item['item']['call_id'] for item in self.backend.cloud.messages if item.get('type') == 'conversation.item.create']
                self.assertEqual(outputs, ['invalid-first'])
                self.assertFalse(any(item.get('type') == 'response.create' for item in self.backend.cloud.messages))
                self.assertEqual(self.effects, [])
            finally:
                release.set(); await asyncio.gather(old, return_exceptions=True)


if __name__ == '__main__': unittest.main()

"""待办改标题：真实SQLite/CAS、本机API及语音分派，不访问云端或在用设备。"""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta, timezone
import ipaddress
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp
from aiohttp.test_utils import TestClient, TestServer

from assistant_core import AssistantStore, IdempotencyConflict, NotFoundError
from assistant_core.tools import execute_tool
from gateway import RUNTIME_KEY, TOOL_SCHEMAS, VoiceSession, create_app
from llm_providers import build_instructions, chat_tool_schemas
from src.assistant_app.application.screen_feedback import TOOLS, api_operation, feedback_card


NOW = datetime(2035, 11, 5, 2, 0, tzinfo=timezone.utc)


class TaskRenameTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'rename.sqlite3'
        self.store = AssistantStore(self.path, 'offline-rename')
        self.sequence = 0

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def task(self, due=None):
        self.sequence += 1
        with patch('assistant_core.store._utc_now', return_value=NOW):
            return self.store.create_task('原待办', due, f'rename-{self.sequence}', metadata={'source':'offline','keep':[1,'中文']})

    def current(self, identity):
        return self.store._task_dict(self.store.connection.execute('SELECT * FROM tasks WHERE task_id=?', (identity,)).fetchone())

    def rename(self, item, title='新待办'):
        return execute_tool(self.store, 'rename_task', {'task_id':item['id'],'expected_updated_at':item['updated_at'],'title':title})

    def test_untimed_and_future_rename_only_change_title_and_revision(self):
        for due in (None, NOW + timedelta(hours=1)):
            with self.subTest(due=due):
                item = self.task(due)
                raw_before = dict(self.store.connection.execute('SELECT * FROM tasks WHERE task_id=?', (item['id'],)).fetchone())
                with patch('assistant_core.store._utc_now', return_value=NOW):
                    result = self.rename(item, '  新标题中文  ')
                self.assertEqual(result['title'], '新标题中文')
                self.assertGreater(result['updated_at'], item['updated_at'])
                raw_after = dict(self.store.connection.execute('SELECT * FROM tasks WHERE task_id=?', (item['id'],)).fetchone())
                self.assertEqual({key:value for key,value in raw_after.items() if key not in {'title','updated_at'}}, {key:value for key,value in raw_before.items() if key not in {'title','updated_at'}})
                with AssistantStore(self.path, 'offline-rename') as reopened:
                    stored = reopened._task_dict(reopened.connection.execute('SELECT * FROM tasks WHERE task_id=?', (item['id'],)).fetchone())
                    self.assertEqual(stored, result)
                with self.assertRaises(IdempotencyConflict):
                    self.store.create_task(item['title'], item['due_at'], item['idempotency_key'], metadata=item['metadata'])
        self.assertEqual(len(self.store.list_tasks()['tasks']), 2)

    def test_same_title_fresh_cas_advances_with_same_clock_and_clock_rollback(self):
        item = self.task()
        with patch('assistant_core.store._utc_now', return_value=NOW):
            first = self.rename(item, item['title'])
            with self.assertRaises(IdempotencyConflict):
                self.rename(item, item['title'])
        with patch('assistant_core.store._utc_now', return_value=NOW - timedelta(hours=1)):
            second = self.rename(first, first['title'])
            with self.assertRaises(IdempotencyConflict):
                self.rename(first, first['title'])
        self.assertGreater(first['updated_at'], item['updated_at'])
        self.assertGreater(second['updated_at'], first['updated_at'])
        self.assertEqual(item['title'], second['title'])

    def test_two_real_sqlite_connections_only_accept_one_matching_version(self):
        item = self.task(NOW + timedelta(hours=1))
        other = AssistantStore(self.path, 'offline-rename')
        barrier = threading.Barrier(2)

        def change(store, title):
            barrier.wait(timeout=2)
            try:
                return store.rename_task(item['id'], item['updated_at'], title)
            except IdempotencyConflict:
                return 'conflict'

        try:
            with patch('assistant_core.store._utc_now', return_value=NOW), ThreadPoolExecutor(max_workers=2) as executor:
                futures = [executor.submit(change, store, title) for store,title in ((self.store,'标题甲'),(other,'标题乙'))]
                results = [future.result(timeout=5) for future in futures]
            self.assertEqual(results.count('conflict'), 1)
            winner = next(result for result in results if isinstance(result, dict))
            self.assertEqual(self.current(item['id']), winner)
        finally:
            other.close()

    def test_title_validation_and_device_scope_do_not_write(self):
        item = self.task()
        for title in ('', ' \n\t ', '中' * 201, '🐦' * 201, None, 42, True):
            with self.subTest(title=title), self.assertRaises(ValueError):
                self.rename(item, title)
        with self.assertRaises(NotFoundError):
            self.store.rename_task('missing-task', item['updated_at'], '新标题')
        with AssistantStore(self.path, 'other-device') as other, self.assertRaises(NotFoundError):
            other.rename_task(item['id'], item['updated_at'], '越权标题')
        self.assertEqual(self.current(item['id']), item)
        with patch('assistant_core.store._utc_now', return_value=NOW):
            result = self.rename(item, '中' * 200)
        self.assertEqual(result['title'], '中' * 200)
        with patch('assistant_core.store._utc_now', return_value=NOW):
            emoji = self.rename(result, '🐦' * 200)
        self.assertEqual(emoji['title'], '🐦' * 200)

    def test_terminal_and_due_tasks_are_known_conflicts_without_any_change(self):
        for state in ('completed','cancelled','due','acked'):
            with self.subTest(state=state):
                item = self.task(NOW - timedelta(minutes=1) if state in {'due','acked'} else None)
                with patch('assistant_core.store._utc_now', return_value=NOW):
                    if state == 'completed':
                        item = self.store.complete_task(item['id'])
                    elif state == 'cancelled':
                        item = self.store.cancel_task(item['id'])
                    elif state == 'acked':
                        lease = next(row for row in self.store.claim_due_reminders() if row['id'] == item['id'])
                        item = self.store.ack_reminder(item['id'], lease['lease_token'])
                    with self.assertRaises(IdempotencyConflict):
                        self.rename(item)
                self.assertEqual(self.current(item['id']), item)

    def test_clock_is_read_inside_write_transaction_and_due_boundary_is_rechecked(self):
        item = self.task(NOW + timedelta(minutes=1))

        def after_lock():
            self.assertTrue(self.store.connection.in_transaction)
            return NOW + timedelta(minutes=1)

        with patch('assistant_core.store._utc_now', side_effect=after_lock), self.assertRaises(IdempotencyConflict):
            self.rename(item)
        self.assertEqual(self.current(item['id']), item)

    def test_claim_before_rename_preserves_lease_and_valid_snapshot(self):
        item = self.task(NOW + timedelta(minutes=1))
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(minutes=2)):
            lease = self.store.claim_due_reminders()[0]
            for snapshot in (item, lease):
                with self.assertRaises(IdempotencyConflict):
                    self.rename(snapshot)
            self.assertEqual(self.current(item['id']), lease)
            self.assertEqual(self.store.valid_reminder_leases([lease]), [lease])

    def test_clock_rollback_does_not_make_leased_reminder_renameable(self):
        item = self.task(NOW + timedelta(minutes=1))
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(minutes=2)):
            lease = self.store.claim_due_reminders()[0]
        with patch('assistant_core.store._utc_now', return_value=NOW), self.assertRaises(IdempotencyConflict):
            self.rename(lease)
        self.assertEqual(self.current(item['id']), lease)

    def test_clock_rollback_keeps_existing_ack_when_due_is_future_again(self):
        item = self.task(NOW + timedelta(minutes=1))
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(minutes=2)):
            lease = self.store.claim_due_reminders()[0]
            acknowledged = self.store.ack_reminder(item['id'], lease['lease_token'])
        with patch('assistant_core.store._utc_now', return_value=NOW):
            result = self.rename(acknowledged)
        self.assertEqual(result['reminder_state'], 'acked')
        self.assertIsNone(result['lease_token'])
        self.assertIsNone(result['lease_until'])
        self.assertEqual(result['due_at'], acknowledged['due_at'])

    def test_rename_before_due_claims_one_original_id_with_new_title(self):
        item = self.task(NOW + timedelta(minutes=1))
        with patch('assistant_core.store._utc_now', return_value=NOW):
            renamed = self.rename(item)
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(minutes=2)):
            claimed = self.store.claim_due_reminders()
            self.assertEqual(len(claimed), 1)
            self.assertEqual(claimed[0]['id'], item['id'])
            self.assertEqual(claimed[0]['title'], renamed['title'])
            self.assertEqual(self.store.valid_reminder_leases(claimed), claimed)
            self.assertEqual(self.store.claim_due_reminders(), [])

    def test_realtime_chat_schema_prompt_and_screen_require_actual_rename_receipt(self):
        realtime = next(tool for tool in TOOL_SCHEMAS if tool['name'] == 'rename_task')
        chat = next(tool['function'] for tool in chat_tool_schemas(TOOL_SCHEMAS) if tool['function']['name'] == 'rename_task')
        for schema in (realtime, chat):
            self.assertEqual(set(schema['parameters']['required']), {'task_id','expected_updated_at','title'})
            self.assertFalse(schema['parameters']['additionalProperties'])
        self.assertIn('rename_task', build_instructions({'enable_search':False}, '[]'))
        operation = api_operation('POST', '/api/tasks/offline/rename', None)
        self.assertEqual(operation, TOOLS['rename_task'])
        item = self.task()
        with patch('assistant_core.store._utc_now', return_value=NOW):
            result = self.rename(item)
        for receipt in (result, {'item':result}):
            card = feedback_card(operation, receipt)
            self.assertEqual(card['status'], 'saved')
            self.assertIn('标题已保存，提醒时间不变', card['detail'])
        for invalid in ({}, {'status':'pending'}, {'item':{'status':'pending','title':'  ','updated_at':result['updated_at']}}, {'item':{**result,'status':'completed'}}):
            self.assertEqual(feedback_card(operation, invalid)['status'], 'unknown')


class TaskRenameApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.outside = []
        original = aiohttp.TCPConnector._resolve_host

        async def loopback_only(connector, host, port, *args, **kwargs):
            try:
                allowed = host == 'localhost' or ipaddress.ip_address(host).is_loopback
            except ValueError:
                allowed = False
            if not allowed:
                self.outside.append(host)
                raise AssertionError('改名回归禁止非本机连接')
            return await original(connector, host, port, *args, **kwargs)

        self.guard = patch.object(aiohttp.TCPConnector, '_resolve_host', loopback_only)
        self.guard.start()
        self.addCleanup(self.guard.stop)
        self.token = 'offline-task-rename-token-00000'
        self.app = create_app({'runtime_dir':self.directory.name,'device_id':'offline-rename-api','device_token':self.token,'aliyun_model':'offline','aliyun_voice':'Tina','enable_search':False,'home_entities':{}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.runtime = self.app[RUNTIME_KEY]
        self.headers = {'Authorization':'Bearer ' + self.token}
        self.item = self.runtime.store.create_task('API原待办', None, 'api-rename-original', metadata={'keep':True})

    async def asyncTearDown(self):
        try:
            await self.client.close()
        finally:
            self.guard.stop()
            self.directory.cleanup()
        self.assertEqual(self.outside, [])

    async def test_api_returns_sanitized_receipt_and_does_not_pop_notification_cache(self):
        cache = {self.item['id']:{'keep':'target'},'other':{'keep':'other'}}
        self.runtime.notifications = cache.copy()
        url = f"/api/tasks/{self.item['id']}/rename"
        payload = {'title':'  API新标题  ','expected_updated_at':self.item['updated_at']}
        response = await self.client.post(url, headers=self.headers, json=payload)
        self.assertEqual(response.status, 200)
        item = (await response.json())['item']
        self.assertEqual(item['id'], self.item['id'])
        self.assertEqual(item['title'], 'API新标题')
        self.assertIsNone(item['due_at'])
        self.assertNotIn('lease_token', item)
        self.assertNotIn('idempotency_key', item)
        self.assertNotIn('device_id', item)
        for identity, snapshot in cache.items():
            self.assertIs(self.runtime.notifications[identity], snapshot)
        self.assertEqual((await self.client.post(url, headers=self.headers, json=payload)).status, 409)
        status = await self.client.get('/api/status', headers=self.headers)
        self.assertIs((await status.json())['capabilities']['task_rename'], True)

    async def test_api_auth_strict_fields_title_validation_and_missing_id(self):
        url = f"/api/tasks/{self.item['id']}/rename"
        payload = {'title':'API新标题','expected_updated_at':self.item['updated_at']}
        self.assertEqual((await self.client.post(url, json=payload)).status, 401)
        for invalid in ({'title':'新标题'},{'expected_updated_at':self.item['updated_at']},{**payload,'due_at':None},{**payload,'title':' \n '},{**payload,'title':'中'*201},{**payload,'title':True}):
            self.assertEqual((await self.client.post(url, headers=self.headers, json=invalid)).status, 400)
        self.assertEqual((await self.client.post('/api/tasks/missing-id/rename', headers=self.headers, json=payload)).status, 404)
        self.assertEqual(self.runtime.store.list_tasks()['tasks'], [self.item])

    async def test_api_lease_conflict_keeps_real_lease_and_cache_owner(self):
        with patch('assistant_core.store._utc_now', return_value=NOW):
            item = self.runtime.store.create_task('未来投递快照', NOW + timedelta(minutes=1), 'api-rename-lease')
        with patch('assistant_core.store._utc_now', return_value=NOW + timedelta(minutes=2)):
            lease = self.runtime.store.claim_due_reminders()[0]
        snapshot = {**lease,'status':'pending_delivery'}
        self.runtime.notifications[item['id']] = snapshot
        response = await self.client.post(f"/api/tasks/{item['id']}/rename", headers=self.headers, json={'title':'拒绝新标题','expected_updated_at':lease['updated_at']})
        self.assertEqual(response.status, 409)
        self.assertEqual((await response.json())['error']['code'], 'conflict')
        self.assertIs(self.runtime.notifications[item['id']], snapshot)
        stored = self.runtime.store._task_dict(self.runtime.store.connection.execute('SELECT * FROM tasks WHERE task_id=?',(item['id'],)).fetchone())
        self.assertEqual(stored, lease)

    async def test_voice_rename_and_conflict_never_pop_existing_cache(self):
        voice = VoiceSession(self.app, None, browser=True)
        self.runtime.operation_screen_feedback = AsyncMock()
        snapshot, other = {'keep':'target'}, {'keep':'other'}
        self.runtime.notifications = {self.item['id']:snapshot,'other':other}
        arguments = {'task_id':self.item['id'],'expected_updated_at':self.item['updated_at'],'title':'语音新标题'}
        result = await voice.execute_tool('rename_task', arguments)
        self.assertEqual(result['title'], '语音新标题')
        operation, receipt = self.runtime.operation_screen_feedback.call_args.args
        self.assertEqual(feedback_card(operation, receipt)['status'], 'saved')
        with self.assertRaises(IdempotencyConflict):
            await voice.execute_tool('rename_task', arguments)
        self.assertIs(self.runtime.notifications[self.item['id']], snapshot)
        self.assertIs(self.runtime.notifications['other'], other)


if __name__ == '__main__':
    unittest.main()

"""提醒due日期范围：真实SQLite、同源API、统一工具合同，不访问外网。"""

import ipaddress
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import aiohttp
from aiohttp.test_utils import TestClient, TestServer

from assistant_core import AssistantStore
from assistant_core.tools import execute_tool, TOOL_SCHEMAS
from gateway import create_app, RUNTIME_KEY
from llm_providers import chat_tool_schemas

START = '2035-11-05T00:00:00+08:00'
END = '2035-11-06T00:00:00+08:00'


class TaskDateFilterTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'date.sqlite3'
        self.store = AssistantStore(self.path, 'offline-date')
        self.sequence = 0

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def add(self, due):
        self.sequence += 1
        return self.store.create_task(f'日期模拟{self.sequence}', due, f'date-{self.sequence}')

    def test_shanghai_day_is_start_inclusive_end_exclusive_and_excludes_untimed(self):
        before = self.add('2035-11-04T15:59:59.999999Z')
        first = self.add('2035-11-04T16:00:00Z')
        last = self.add('2035-11-05T15:59:59.999999Z')
        after = self.add('2035-11-05T16:00:00Z')
        untimed = self.add(None)
        result = self.store.list_tasks(due_from=START, due_before=END)
        self.assertEqual([item['id'] for item in result['tasks']], [first['id'], last['id']])
        self.assertEqual(len(self.store.list_tasks()['tasks']), 5)
        self.assertNotIn(untimed['id'], {item['id'] for item in result['tasks']})
        self.assertEqual({item['id'] for item in self.store.list_tasks(due_before=END)['tasks']}, {before['id'], first['id'], last['id']})
        self.assertEqual({item['id'] for item in self.store.list_tasks(due_from=START)['tasks']}, {first['id'], last['id'], after['id']})

    def test_range_cursor_same_due_exact_end_and_device_status_scope(self):
        items = [self.add('2035-11-05T03:00:00Z') for _ in range(61)]
        self.add(None)
        self.store.complete_task(items[0]['id'])
        with AssistantStore(self.path, 'other-device') as other:
            other.create_task('其他设备', '2035-11-05T03:00:00Z', 'other')
        seen = []
        cursor = None
        for index in range(2):
            page = self.store.list_tasks('pending', due_from=START, due_before=END, limit=30, cursor=cursor)
            seen.extend(item['id'] for item in page['tasks'])
            cursor = page['next_cursor']
            self.assertEqual(len(page['tasks']), 30)
            self.assertEqual(cursor is None, index == 1)
        self.assertEqual(len(set(seen)), 60)
        self.assertNotIn(items[0]['id'], seen)

    def test_invalid_or_unzoned_range_is_rejected(self):
        for args in ({'due_from':'2035-11-05T00:00:00'}, {'due_before':''}, {'due_from':END, 'due_before':START}, {'due_from':START, 'due_before':START}):
            with self.subTest(args=args), self.assertRaises(ValueError):
                self.store.list_tasks(**args)

    def test_null_cursor_with_range_never_falls_back_to_all_tasks(self):
        self.add(None); self.add(None)
        cursor = self.store.list_tasks(limit=1)['next_cursor']
        self.add('2035-11-05T03:00:00Z')
        result = self.store.list_tasks(due_from=START, due_before=END, cursor=cursor)
        self.assertEqual(result, {'tasks':[], 'next_cursor':None})

    def test_range_sql_uses_time_index_without_expression_sort(self):
        self.add('2035-11-05T03:00:00Z')
        queries = []
        self.store.connection.set_trace_callback(queries.append)
        try:
            self.store.list_tasks(due_from=START, due_before=END, limit=30)
            self.store.list_tasks('pending', due_from=START, due_before=END, limit=30)
        finally:
            self.store.connection.set_trace_callback(None)
        selects = [query for query in queries if query.startswith('SELECT * FROM tasks')]
        self.assertEqual(len(selects), 2)
        for query in selects:
            self.assertIn('ORDER BY due_at,task_id LIMIT 31', query)
            self.assertNotIn('ORDER BY due_at IS NULL', query)
            plan = ' '.join(str(row['detail']) for row in self.store.connection.execute('EXPLAIN QUERY PLAN ' + query))
            self.assertIn('INDEX', plan)
            self.assertIn('due_at', plan)
            self.assertNotIn('TEMP B-TREE', plan)

    def test_tool_schemas_and_dispatch_preserve_range(self):
        inside = self.add('2035-11-05T03:00:00Z'); self.add(None)
        realtime = next(tool for tool in TOOL_SCHEMAS if tool['name'] == 'list_tasks')
        chat = next(tool['function'] for tool in chat_tool_schemas(TOOL_SCHEMAS) if tool['function']['name'] == 'list_tasks')
        for tool in (realtime, chat):
            self.assertEqual(tool['parameters']['properties']['due_from']['type'], 'string')
            self.assertEqual(tool['parameters']['properties']['due_before']['type'], 'string')
        result = execute_tool(self.store, 'list_tasks', {'due_from':START, 'due_before':END, 'limit':1})
        self.assertEqual([item['id'] for item in result['tasks']], [inside['id']])


class TaskDateApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.outside = []
        original = aiohttp.TCPConnector._resolve_host
        async def loopback(connector, host, port, *args, **kwargs):
            try: allowed = host == 'localhost' or ipaddress.ip_address(host).is_loopback
            except ValueError: allowed = False
            if not allowed:
                self.outside.append(host); raise AssertionError('外网请求未隔离')
            return await original(connector, host, port, *args, **kwargs)
        self.guard = patch.object(aiohttp.TCPConnector, '_resolve_host', loopback)
        self.guard.start(); self.addCleanup(self.guard.stop)
        self.token = 'offline-task-date-token-00000'
        self.app = create_app({'runtime_dir':self.directory.name, 'device_id':'offline-date-api', 'device_token':self.token, 'aliyun_model':'offline', 'aliyun_voice':'Tina', 'home_entities':{}, 'enable_search':False})
        self.client = TestClient(TestServer(self.app)); await self.client.start_server()
        self.headers = {'Authorization':'Bearer ' + self.token}

    async def asyncTearDown(self):
        try: await self.client.close()
        finally: self.guard.stop(); self.directory.cleanup()
        self.assertEqual(self.outside, [])

    async def test_api_range_and_loaded_capability(self):
        store = self.app[RUNTIME_KEY].store
        inside = store.create_task('日期内', '2035-11-05T03:00:00Z', 'inside')
        store.create_task('日期外', '2035-11-05T16:00:00Z', 'outside'); store.create_task('无时间', None, 'untimed')
        response = await self.client.get('/api/tasks', headers=self.headers, params={'due_from':START, 'due_before':END, 'limit':'30'})
        self.assertEqual(response.status, 200)
        self.assertEqual([item['id'] for item in (await response.json())['items']], [inside['id']])
        status = await self.client.get('/api/status', headers=self.headers)
        self.assertIs((await status.json())['capabilities']['task_date_filter'], True)

    async def test_api_rejects_invalid_ranges_and_keeps_unfiltered_contract(self):
        store = self.app[RUNTIME_KEY].store
        store.create_task('无时间', None, 'untimed')
        for params in ({'due_from':'2035-11-05'}, {'due_before':'invalid'}, {'due_from':END, 'due_before':START}, {'due_from':START, 'due_before':START}):
            with self.subTest(params=params):
                response = await self.client.get('/api/tasks', headers=self.headers, params=params)
                self.assertEqual(response.status, 400)
        response = await self.client.get('/api/tasks?limit=30', headers=self.headers)
        self.assertEqual(len((await response.json())['items']), 1)


if __name__ == '__main__': unittest.main()

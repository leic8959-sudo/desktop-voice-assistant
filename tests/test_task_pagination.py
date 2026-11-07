"""任务游标分页：真实SQLite和本机API，不访问在用网关或外网。"""

import ipaddress
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import aiohttp
from aiohttp.test_utils import TestClient, TestServer

from assistant_core import AssistantStore
from gateway import RUNTIME_KEY, create_app


class TaskPaginationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / 'tasks.sqlite3'
        self.store = AssistantStore(self.path, 'offline-pages')

    def tearDown(self):
        self.store.close()
        self.directory.cleanup()

    def populate(self, timed, untimed=0):
        return [self.store.create_task(f'模拟任务{i}', '2035-11-05T03:00:00Z' if i < timed else None, f'offline-page-{i}') for i in range(timed + untimed)]

    def test_same_due_and_null_pages_have_no_duplicates_and_stop_on_exact_last_page(self):
        items = self.populate(45, 45)
        expected = sorted(items, key=lambda item: (item['due_at'] is None, item['due_at'] or '', item['task_id']))
        cursor = None
        seen = []
        for index in range(3):
            page = self.store.list_tasks(limit=30, cursor=cursor)
            self.assertEqual(len(page['tasks']), 30)
            seen.extend(item['task_id'] for item in page['tasks'])
            cursor = page['next_cursor']
            self.assertEqual(cursor is None, index == 2)
        self.assertEqual(seen, [item['task_id'] for item in expected])
        self.assertEqual(len(set(seen)), 90)

    def test_empty_short_and_exact_limit_do_not_advertise_phantom_next_page(self):
        self.assertIsNone(self.store.list_tasks(limit=30)['next_cursor'])
        for index in range(60):
            self.store.create_task(f'模拟{index}', None, f'exact-{index}')
            if index in (0, 28, 29, 59):
                limit = 60 if index == 59 else 30
                page = self.store.list_tasks(limit=limit)
                self.assertEqual(len(page['tasks']), index + 1)
                self.assertIsNone(page['next_cursor'])

    def test_status_and_device_scope_apply_to_extra_row_and_cursor(self):
        items = self.populate(31)
        self.store.complete_task(items[0]['task_id'])
        pending = self.store.list_tasks(status='pending', limit=30)
        self.assertEqual(len(pending['tasks']), 30)
        self.assertIsNone(pending['next_cursor'])
        self.assertTrue(all(item['status'] == 'pending' for item in pending['tasks']))
        with AssistantStore(self.path, 'other-device') as other:
            other.create_task('其他设备', None, 'other')
            self.assertEqual(len(other.list_tasks(limit=30)['tasks']), 1)
            cursor = self.store.list_tasks(limit=1)['next_cursor']
            self.assertTrue(all(item['device_id'] == 'other-device' for item in other.list_tasks(limit=30, cursor=cursor)['tasks']))


class TaskPaginationApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.outside = []
        original = aiohttp.TCPConnector._resolve_host

        async def local_only(connector, host, port, *args, **kwargs):
            try:
                local = host == 'localhost' or ipaddress.ip_address(host).is_loopback
            except ValueError:
                local = False
            if not local:
                self.outside.append(host)
                raise AssertionError('外网请求未隔离')
            return await original(connector, host, port, *args, **kwargs)

        self.guard = patch.object(aiohttp.TCPConnector, '_resolve_host', local_only)
        self.guard.start()
        self.addCleanup(self.guard.stop)
        self.token = 'offline-task-page-token-0000000'
        self.app = create_app({'runtime_dir':self.directory.name, 'device_id':'offline-page-api', 'device_token':self.token, 'aliyun_model':'offline', 'aliyun_voice':'Tina', 'enable_search':False, 'home_entities':{}})
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {'Authorization':'Bearer ' + self.token}
        self.store = self.app[RUNTIME_KEY].store

    async def asyncTearDown(self):
        try:
            await self.client.close()
        finally:
            self.guard.stop()
            self.directory.cleanup()
        self.assertEqual(self.outside, [])

    async def test_api_cursor_replaces_pages_and_exact_last_page_has_no_next(self):
        for index in range(60):
            self.store.create_task(f'模拟API任务{index}', '2035-11-05T03:00:00Z' if index < 35 else None, f'api-page-{index}')
        first = await self.client.get('/api/tasks?limit=30', headers=self.headers)
        self.assertEqual(first.status, 200)
        first = await first.json()
        second = await self.client.get('/api/tasks', headers=self.headers, params={'limit':'30', 'cursor':first['next_cursor']})
        self.assertEqual(second.status, 200)
        second = await second.json()
        self.assertEqual(len(first['items']), 30)
        self.assertEqual(len(second['items']), 30)
        self.assertIsNone(second['next_cursor'])
        self.assertFalse({item['id'] for item in first['items']} & {item['id'] for item in second['items']})

    async def test_api_enforces_page_limit_without_count_or_lease_fields(self):
        for index in range(101):
            self.store.create_task(f'模拟上限{index}', None, f'api-limit-{index}')
        response = await self.client.get('/api/tasks?limit=100', headers=self.headers)
        self.assertEqual(response.status, 200)
        data = await response.json()
        self.assertEqual(len(data['items']), 100)
        self.assertTrue(data['next_cursor'])
        self.assertNotIn('total', data)
        self.assertTrue(all('lease_token' not in item for item in data['items']))
        for limit in ('0', '101', '-1', 'invalid'):
            with self.subTest(limit=limit):
                rejected = await self.client.get('/api/tasks', headers=self.headers, params={'limit':limit})
                self.assertEqual(rejected.status, 400)


if __name__ == '__main__':
    unittest.main()

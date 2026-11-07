"""已有未定时待办添加未来提醒：真实SQLite、HTTP与工具，不连接外网。"""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime,timedelta,timezone
import ipaddress
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import AsyncMock,patch

import aiohttp
from aiohttp.test_utils import TestClient,TestServer

from assistant_core import AssistantStore,IdempotencyConflict,NotFoundError
from assistant_core.tools import execute_tool,TOOL_SCHEMAS
from gateway import create_app,RUNTIME_KEY,VoiceSession
from llm_providers import chat_tool_schemas,build_instructions
from src.assistant_app.application.screen_feedback import feedback_card,TOOLS

NOW=datetime(2026,11,5,7,0,tzinfo=timezone.utc)
FUTURE='2035-11-05T11:20:00+08:00'


class TaskAddReminderTests(unittest.TestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.path=Path(self.directory.name)/'add.sqlite3'
        self.store=AssistantStore(self.path,'offline-add')
        with patch('assistant_core.store._utc_now',return_value=NOW):
            self.item=self.store.create_task('保留原有待办',None,'original-task',metadata={'tag':'keep'})

    def tearDown(self):
        self.store.close();self.directory.cleanup()

    def args(self,**changes):
        return {'task_id':self.item['id'],'expected_updated_at':self.item['updated_at'],'due_at':FUTURE,**changes}

    def test_tool_attaches_future_time_to_same_task_and_preserves_identity_content(self):
        with patch('assistant_core.store._utc_now',return_value=NOW):
            result=execute_tool(self.store,'reschedule_task',self.args())
        self.assertEqual(result['due_at'],'2035-11-05T03:20:00.000000Z')
        self.assertEqual(result['status'],'pending')
        self.assertGreater(result['updated_at'],self.item['updated_at'])
        for key in ('id','task_id','title','metadata','idempotency_key','created_at','device_id'):
            self.assertEqual(result[key],self.item[key])
        self.assertIsNone(result['lease_token']);self.assertIsNone(result['lease_until'])
        self.assertEqual(result['reminder_state'],'pending')
        with AssistantStore(self.path,'offline-add') as reopened:
            rows=reopened.list_tasks()['tasks']
            self.assertEqual(rows,[result])

    def test_two_connections_same_version_only_one_attachment_succeeds(self):
        other=AssistantStore(self.path,'offline-add');barrier=threading.Barrier(2)
        def write(store,due):
            barrier.wait(timeout=2)
            try:return store.reschedule_task(self.item['id'],self.item['updated_at'],due)
            except IdempotencyConflict:return 'conflict'
        try:
            with patch('assistant_core.store._utc_now',return_value=NOW),ThreadPoolExecutor(max_workers=2) as pool:
                futures=[pool.submit(write,store,due) for store,due in ((self.store,FUTURE),(other,'2035-11-06T11:20:00+08:00'))]
                results=[future.result(timeout=5) for future in futures]
            self.assertEqual(results.count('conflict'),1)
            accepted=next(result for result in results if isinstance(result,dict))
            self.assertEqual(self.store.list_tasks()['tasks'],[accepted])
        finally:other.close()

    def test_fixed_or_rollback_clock_stale_versions_cannot_repeat(self):
        with patch('assistant_core.store._utc_now',return_value=NOW-timedelta(days=1)):
            first=execute_tool(self.store,'reschedule_task',self.args())
            self.assertGreater(first['updated_at'],self.item['updated_at'])
            with self.assertRaises(IdempotencyConflict):execute_tool(self.store,'reschedule_task',self.args())
            second=self.store.reschedule_task(first['id'],first['updated_at'],'2035-11-07T11:20:00+08:00')
        self.assertGreater(second['updated_at'],first['updated_at'])

    def test_null_empty_unzoned_and_expired_new_times_never_remove_or_add_reminder(self):
        for due in (None,'','2035-11-05T11:20:00',NOW.isoformat(),(NOW-timedelta(seconds=1)).isoformat()):
            with self.subTest(due=due),patch('assistant_core.store._utc_now',return_value=NOW),self.assertRaises(ValueError):
                execute_tool(self.store,'reschedule_task',self.args(due_at=due))
        self.assertEqual(self.store.list_tasks()['tasks'],[self.item])
        with patch('assistant_core.store._utc_now',return_value=NOW):
            timed=execute_tool(self.store,'reschedule_task',self.args())
        with self.assertRaises(ValueError):self.store.reschedule_task(timed['id'],timed['updated_at'],None)
        self.assertEqual(self.store.list_tasks()['tasks'],[timed])

    def test_closed_due_and_foreign_tasks_remain_rejected(self):
        for status in ('completed','cancelled'):
            with self.subTest(status=status):
                item=self.store.create_task(status,None,'state-'+status)
                item=getattr(self.store,'complete_task' if status=='completed' else 'cancel_task')(item['id'])
                with patch('assistant_core.store._utc_now',return_value=NOW),self.assertRaises(ValueError):
                    self.store.reschedule_task(item['id'],item['updated_at'],FUTURE)
        due=self.store.create_task('已到期','2020-01-01T00:00:00Z','due')
        with patch('assistant_core.store._utc_now',return_value=NOW),self.assertRaisesRegex(ValueError,'稍后提醒'):
            self.store.reschedule_task(due['id'],due['updated_at'],FUTURE)
        with AssistantStore(self.path,'other-device') as other,self.assertRaises(NotFoundError):
            other.reschedule_task(self.item['id'],self.item['updated_at'],FUTURE)

    def test_time_is_read_after_transaction_and_schemas_require_nonnull_due(self):
        def clock():
            self.assertTrue(self.store.connection.in_transaction)
            return NOW
        with patch('assistant_core.store._utc_now',side_effect=clock) as clock_mock:
            result=execute_tool(self.store,'reschedule_task',self.args())
        clock_mock.assert_called_once()
        for schema in (next(tool for tool in TOOL_SCHEMAS if tool['name']=='reschedule_task'),next(tool['function'] for tool in chat_tool_schemas(TOOL_SCHEMAS) if tool['function']['name']=='reschedule_task')):
            self.assertEqual(schema['parameters']['properties']['due_at']['type'],'string')
            self.assertIn('expected_updated_at',schema['parameters']['required'])
            self.assertIn('保留原task_id',schema['description'])
        instructions=build_instructions({'enable_search':False},'[]')
        self.assertIn('已有待办添加提醒使用reschedule_task',instructions)
        card=feedback_card(TOOLS['reschedule_task'],result)
        self.assertEqual(card['status'],'saved');self.assertIn(result['due_at'],card['detail']);self.assertIn('仍未完成',card['detail'])


class TaskAddReminderApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.directory=tempfile.TemporaryDirectory();self.outside=[]
        original=aiohttp.TCPConnector._resolve_host
        async def local_only(connector,host,port,*args,**kwargs):
            try:allowed=host=='localhost' or ipaddress.ip_address(host).is_loopback
            except ValueError:allowed=False
            if not allowed:self.outside.append(host);raise AssertionError('外网请求未隔离')
            return await original(connector,host,port,*args,**kwargs)
        self.guard=patch.object(aiohttp.TCPConnector,'_resolve_host',local_only);self.guard.start();self.addCleanup(self.guard.stop)
        self.token='offline-add-reminder-token-0000'
        self.app=create_app({'runtime_dir':self.directory.name,'device_id':'offline-api','device_token':self.token,'aliyun_model':'offline','aliyun_voice':'Tina','enable_search':False,'home_entities':{}})
        self.client=TestClient(TestServer(self.app));await self.client.start_server()
        self.headers={'Authorization':'Bearer '+self.token};self.runtime=self.app[RUNTIME_KEY]
        self.item=self.runtime.store.create_task('API原待办',None,'api-original',metadata={'keep':True})

    async def asyncTearDown(self):
        try:await self.client.close()
        finally:self.guard.stop();self.directory.cleanup()
        self.assertEqual(self.outside,[])

    async def test_same_route_adds_to_existing_task_and_advertises_loaded_capability(self):
        payload={'expected_updated_at':self.item['updated_at'],'due_at':FUTURE}
        response=await self.client.post(f"/api/tasks/{self.item['id']}/reschedule",headers=self.headers,json=payload)
        self.assertEqual(response.status,200);result=(await response.json())['item']
        self.assertEqual(result['id'],self.item['id']);self.assertEqual(result['status'],'pending')
        self.assertEqual(result['due_at'],'2035-11-05T03:20:00.000000Z')
        self.assertEqual(len(self.runtime.store.list_tasks()['tasks']),1)
        self.assertEqual(self.runtime.store.list_tasks()['tasks'][0]['metadata'],{'keep':True})
        replay=await self.client.post(f"/api/tasks/{self.item['id']}/reschedule",headers=self.headers,json=payload)
        self.assertEqual(replay.status,409)
        status=await self.client.get('/api/status',headers=self.headers)
        self.assertIs((await status.json())['capabilities']['task_add_reminder'],True)

    async def test_api_auth_strict_body_and_null_deletion_rejected(self):
        url=f"/api/tasks/{self.item['id']}/reschedule";payload={'expected_updated_at':self.item['updated_at'],'due_at':FUTURE}
        self.assertEqual((await self.client.post(url,json=payload)).status,401)
        for invalid in ({'due_at':FUTURE},{**payload,'unexpected':True},{**payload,'due_at':None},{**payload,'due_at':'2035-11-05T11:20:00'}):
            with self.subTest(invalid=invalid):
                self.assertEqual((await self.client.post(url,headers=self.headers,json=invalid)).status,400)
        self.assertEqual(self.runtime.store.list_tasks()['tasks'],[self.item])

    async def test_voice_uses_existing_task_and_only_clears_target_notification(self):
        voice=VoiceSession(self.app,None,browser=True);self.runtime.operation_screen_feedback=AsyncMock()
        self.runtime.notifications={self.item['id']:{'stale':True},'other':{'keep':True}}
        result=await voice.execute_tool('reschedule_task',{'task_id':self.item['id'],'expected_updated_at':self.item['updated_at'],'due_at':FUTURE})
        self.assertEqual(result['task_id'],self.item['id'])
        self.assertEqual(self.runtime.notifications,{'other':{'keep':True}})
        self.assertEqual(len(self.runtime.store.list_tasks()['tasks']),1)


if __name__=='__main__':unittest.main()

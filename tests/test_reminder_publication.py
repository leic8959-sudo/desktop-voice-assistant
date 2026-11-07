"""提醒快照失效与慢连接：真实Store/Runtime/NativeDevice，只有设备为本机替身。"""

import asyncio
from datetime import datetime,timedelta,timezone
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock,patch

from assistant_core import AssistantStore
from assistant_runtime import AssistantRuntime,NativeDevice


class ControlledStore(AssistantStore):
    claim_entered=None
    claim_release=None
    validation_entered=None
    validation_release=None
    def claim_due_reminders(self,*args,**kwargs):
        rows=super().claim_due_reminders(*args,**kwargs)
        if rows and self.claim_entered is not None:
            self.claim_entered.set();self.claim_release.wait(timeout=2)
            self.claim_entered=None
        return rows
    def valid_reminder_leases(self,candidates):
        rows=super().valid_reminder_leases(candidates)
        if self.validation_entered is not None:
            self.validation_entered.set();self.validation_release.wait(timeout=2)
            self.validation_entered=None
        return rows


class DeviceSocket:
    def __init__(self):
        self.closed=False;self.messages=[];self.notifications=asyncio.Queue();self.incoming=asyncio.Queue()
        self.blocked_id=None;self.block_entered=asyncio.Event();self.block_release=asyncio.Event()
    async def send_json(self,payload):
        if payload.get('type')=='notification':
            self.messages.append(payload)
            if payload['notification']['id']==self.blocked_id:
                self.block_entered.set();await self.block_release.wait()
            self.notifications.put_nowait(payload['notification'])
        else:self.messages.append(payload)
    async def send_bytes(self,_data):pass
    def __aiter__(self):return self
    async def __anext__(self):
        value=await self.incoming.get()
        if value is None:raise StopAsyncIteration
        return value


class ReminderPublicationTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory();self.store=ControlledStore(Path(self.directory.name)/'reminders.sqlite3','offline-publication')
        self.runtime=AssistantRuntime({'runtime_dir':self.directory.name,'device_id':'offline-publication','aliyun_model':'offline','aliyun_voice':'Tina'},self.store,SimpleNamespace(configured=False))
        self.tasks=[];self.sockets=[];self.sequence=0
    async def asyncTearDown(self):
        for event in (self.store.claim_release,self.store.validation_release):
            if event is not None:event.set()
        for socket in self.sockets:socket.block_release.set();socket.incoming.put_nowait(None)
        for task in self.tasks:
            if not task.done():task.cancel()
        await asyncio.gather(*self.tasks,return_exceptions=True)
        self.store.close();self.directory.cleanup()
    def due(self):
        self.sequence+=1
        return self.store.create_task(f'模拟提醒{self.sequence}',(datetime.now(timezone.utc)-timedelta(minutes=1)).isoformat(),f'publication-{self.sequence}')
    def device(self):
        socket=DeviceSocket();self.sockets.append(socket)
        device=NativeDevice(socket,self.runtime,lambda _socket:None);self.runtime.native_devices.add(device)
        return device,socket
    def launch(self,coroutine):
        task=asyncio.create_task(coroutine);self.tasks.append(task);return task
    async def thread_event(self,event):
        self.assertTrue(await asyncio.to_thread(event.wait,0.5),'预期的真实存储等待窗口未进入')
    async def test_completed_after_claim_commit_before_return_is_not_cached_or_sent(self):
        item=self.due();_device,socket=self.device()
        self.store.claim_entered=threading.Event();self.store.claim_release=threading.Event()
        worker=self.launch(self.runtime.notifications_loop())
        await self.thread_event(self.store.claim_entered)
        self.store.complete_task(item['id']);self.runtime.notifications.pop(item['id'],None)
        self.store.claim_release.set()
        # 待本轮走到timer意味着领取候选已经处理；不靠固定sleep判断是否发送。
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        await asyncio.wait_for(timer_seen.wait(),0.5)
        self.assertEqual(socket.notifications.qsize(),0)
        self.assertNotIn(item['id'],self.runtime.notifications)
        self.assertEqual(self.store.list_tasks()['tasks'][0]['status'],'completed')
        worker.cancel()

    async def test_completion_after_validation_read_cannot_revive_removed_candidate(self):
        item=self.due();_device,socket=self.device()
        self.store.validation_entered=threading.Event();self.store.validation_release=threading.Event()
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        self.launch(self.runtime.notifications_loop())
        await self.thread_event(self.store.validation_entered)
        self.store.complete_task(item['id']);self.runtime.notifications.pop(item['id'],None)
        self.store.validation_release.set();await asyncio.wait_for(timer_seen.wait(),0.5)
        self.assertEqual(socket.notifications.qsize(),0)
        self.assertNotIn(item['id'],self.runtime.notifications)

    async def test_slow_first_device_is_bounded_and_other_device_and_timer_continue(self):
        item=self.due();_first,slow=self.device();_second,fast=self.device();slow.blocked_id=item['id']
        self.runtime.native_devices=[_first,_second]
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        with patch('assistant_runtime.NOTIFICATION_SEND_TIMEOUT',0.01,create=True):
            self.launch(self.runtime.notifications_loop())
            await asyncio.wait_for(slow.block_entered.wait(),0.5)
            sent=await asyncio.wait_for(fast.notifications.get(),0.5)
            await asyncio.wait_for(timer_seen.wait(),0.5)
        self.assertEqual(sent['id'],item['id'])
        self.assertIn(item['id'],self.runtime.notifications)
        self.assertEqual(self.store.list_tasks()['tasks'][0]['reminder_state'],'leased')

    async def test_snooze_during_first_device_send_prevents_old_lease_at_second_device(self):
        item=self.due();first,slow=self.device();second,fast=self.device();self.runtime.native_devices=[first,second];slow.blocked_id=item['id']
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        self.launch(self.runtime.notifications_loop());await asyncio.wait_for(slow.block_entered.wait(),0.5)
        leased=self.store.list_tasks()['tasks'][0]
        self.store.snooze_reminder(item['id'],leased['updated_at']);self.runtime.notifications.pop(item['id'],None)
        slow.block_release.set();await asyncio.wait_for(timer_seen.wait(),0.5)
        self.assertEqual(fast.notifications.qsize(),0)
        self.assertNotIn(item['id'],self.runtime.notifications)

    async def test_reconnect_snapshot_skips_acknowledged_item_after_prior_send_wait(self):
        first_item,second_item=self.due(),self.due()
        claimed=self.store.claim_due_reminders();first=next(item for item in claimed if item['id']==first_item['id']);second=next(item for item in claimed if item['id']==second_item['id'])
        self.runtime.notifications={first['id']:{**first,'status':'pending_delivery'},second['id']:{**second,'status':'pending_delivery'}}
        device,socket=self.device();socket.blocked_id=first['id']
        self.runtime.device_preferences=AsyncMock(return_value={})
        self.runtime.send_screen_card=AsyncMock();self.runtime.timer_snapshot=AsyncMock(return_value=None);self.runtime.send_today_screen_card=AsyncMock()
        running=self.launch(device.run());await asyncio.wait_for(socket.block_entered.wait(),0.5)
        acknowledged=await self.runtime.acknowledge(second['id'],second['lease_token'])
        self.assertEqual(acknowledged['status'],'pending')
        self.assertEqual(acknowledged['reminder_state'],'acked')
        socket.block_release.set();socket.incoming.put_nowait(None);await asyncio.wait_for(running,0.5)
        self.assertEqual([message['notification']['id'] for message in socket.messages if message.get('type')=='notification'],[first['id']])

    async def test_ack_completion_only_removes_the_snapshot_it_started_with(self):
        item=self.due();claimed=self.store.claim_due_reminders()[0];old={**claimed,'status':'pending_delivery'};self.runtime.notifications[item['id']]=old
        entered,release=threading.Event(),threading.Event();original=self.store.ack_reminder
        def delayed_ack(*args):
            result=original(*args);entered.set();release.wait(timeout=2);return result
        with patch.object(self.store,'ack_reminder',side_effect=delayed_ack):
            task=self.launch(self.runtime.acknowledge(item['id'],claimed['lease_token']))
            await self.thread_event(entered)
            newer={**old,'lease_token':'newer-lease-token','updated_at':'2035-01-01T00:00:00.000000Z'}
            self.runtime.notifications[item['id']]=newer
            release.set();await asyncio.wait_for(task,0.5)
        self.assertIs(self.runtime.notifications.get(item['id']),newer)

    async def test_validating_candidates_are_not_public_or_acknowledgeable(self):
        item=self.due();claimed=self.store.claim_due_reminders()[0];self.runtime.notifications[item['id']]={**claimed,'status':'validating'}
        self.assertEqual(self.runtime.status()['notifications'],[])
        with self.assertRaises((KeyError,ValueError)):
            await self.runtime.acknowledge(item['id'],claimed['lease_token'])
        self.assertEqual(self.store.list_tasks()['tasks'][0]['reminder_state'],'leased')

    async def test_target_batch_deadline_cancels_second_send_before_its_individual_deadline(self):
        self.due();self.due();self.due()
        first,slow=self.device();second,fast=self.device();self.runtime.native_devices=[first,second]
        second_entered=asyncio.Event();second_release=asyncio.Event();cancelled=asyncio.Event();calls=0;cancelled_snapshot=None
        original=slow.send_json;original_timeout=asyncio.timeout;timeouts=[]
        def record_timeout(delay):
            timeout=original_timeout(delay);timeouts.append((delay,timeout));return timeout
        async def cumulative(payload):
            nonlocal calls,cancelled_snapshot
            if payload.get('type')=='notification':
                calls+=1
                if calls==2:
                    second_entered.set()
                    try:await second_release.wait()
                    except asyncio.CancelledError:
                        cancelled_snapshot=(calls,second_release.is_set());cancelled.set()
                        raise
            await original(payload)
        slow.send_json=cumulative
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        try:
            with patch('assistant_runtime.asyncio.timeout',side_effect=record_timeout):
                worker=self.launch(self.runtime.notifications_loop())
                await asyncio.wait_for(second_entered.wait(),0.5)
                self.assertEqual(len(timeouts),3,'当前目标只有一个整批预算及前两项各自的发送预算')
                self.assertEqual([delay for delay,_timeout in timeouts],[3,3,3])
                outer,first_inner,second_inner=[timeout for _delay,timeout in timeouts]
                second_deadline=second_inner.when()
                self.assertFalse(first_inner.expired())
                self.assertFalse(second_inner.expired())
                self.assertGreater(second_inner.when(),asyncio.get_running_loop().time())
                outer.reschedule(asyncio.get_running_loop().time())
                await asyncio.wait_for(cancelled.wait(),0.5)
                self.assertIsNotNone(cancelled_snapshot)
                self.assertEqual(cancelled_snapshot[0],2,'整批预算必须在第二项等待时取消，而非首项或后续目标')
                self.assertLess(cancelled_snapshot[0],3,'单目标整批预算必须阻止逐项慢发送累积成无界阻塞')
                self.assertFalse(cancelled_snapshot[1],'整批期限必须取消仍未完成且单项期限尚未到的第二项')
                self.assertTrue(outer.expired())
                self.assertFalse(second_inner.expired(),'取消必须来自整批期限而非单项期限')
                self.assertEqual(second_inner.when(),second_deadline,'触发整批期限不能修改单项期限')
                await asyncio.wait_for(fast.notifications.get(),0.5)
                await asyncio.wait_for(timer_seen.wait(),0.5)
                self.assertEqual(calls,2,'整批取消后不能继续第三项发送')
                worker.cancel()
        finally:
            second_release.set()

    async def test_lease_expired_while_waiting_for_prior_target_is_not_sent_to_next_target(self):
        now=datetime.now(timezone.utc);clock={'now':now}
        class Clock(datetime):
            @classmethod
            def now(cls,tz=None):return clock['now'].astimezone(tz) if tz else clock['now'].replace(tzinfo=None)
        item=self.due();first,slow=self.device();second,fast=self.device();self.runtime.native_devices=[first,second];slow.blocked_id=item['id']
        timer_seen=asyncio.Event();self.runtime.timer_snapshot=AsyncMock(side_effect=lambda:timer_seen.set())
        with patch('assistant_core.store._utc_now',side_effect=lambda:clock['now']),patch('assistant_runtime.datetime',Clock):
            self.launch(self.runtime.notifications_loop());await asyncio.wait_for(slow.block_entered.wait(),0.5)
            clock['now']=now+timedelta(seconds=61);slow.block_release.set()
            await asyncio.wait_for(timer_seen.wait(),0.5)
        self.assertEqual(fast.notifications.qsize(),0)

    async def test_lease_validation_is_a_bounded_primary_key_lookup_and_scope_checked(self):
        items=[self.due() for _ in range(25)]
        claimed=self.store.claim_due_reminders(limit=25)
        selected=[claimed[0],claimed[-1]]
        queries=[];self.store.connection.set_trace_callback(queries.append)
        try:valid=self.store.valid_reminder_leases(selected)
        finally:self.store.connection.set_trace_callback(None)
        self.assertEqual({row['id'] for row in valid},{row['id'] for row in selected})
        selects=[query for query in queries if query.startswith('SELECT * FROM tasks')]
        self.assertEqual(len(selects),1)
        self.assertIn('INDEXED BY sqlite_autoindex_tasks_1',selects[0])
        self.assertIn("device_id='offline-publication'",selects[0])
        self.assertTrue(selects[0].endswith('LIMIT 2'))
        plan=' '.join(row['detail'] for row in self.store.connection.execute('EXPLAIN QUERY PLAN '+selects[0]))
        self.assertIn('SEARCH tasks USING INDEX sqlite_autoindex_tasks_1 (task_id=?)',plan)
        self.assertNotIn('tasks_due_claim',plan)
        with AssistantStore(Path(self.directory.name)/'reminders.sqlite3','other-device') as other:
            self.assertEqual(other.valid_reminder_leases(selected),[])
        changed={**selected[0],'updated_at':'2035-01-01T00:00:00.000000Z'}
        self.assertEqual(self.store.valid_reminder_leases([changed]),[])
        with self.assertRaises(ValueError):self.store.valid_reminder_leases([selected[0]]*101)
        self.assertEqual(len(items),25)

    async def test_validation_failure_cleans_only_its_staged_candidates(self):
        first,unrelated=self.due(),self.due();claimed=self.store.claim_due_reminders()
        staged=next(item for item in claimed if item['id']==first['id'])
        retained={**next(item for item in claimed if item['id']==unrelated['id']),'status':'pending_delivery'}
        self.runtime.notifications[unrelated['id']]=retained
        with patch.object(self.store,'valid_reminder_leases',side_effect=OSError('synthetic validation read failure')):
            with self.assertRaises(OSError):await self.runtime._register_claimed_notifications([staged])
        self.assertNotIn(first['id'],self.runtime.notifications)
        self.assertIs(self.runtime.notifications[unrelated['id']],retained)

    async def test_empty_claim_skips_validation(self):
        with patch.object(self.store,'valid_reminder_leases') as validation:
            self.assertEqual(await self.runtime._register_claimed_notifications([]),[])
            validation.assert_not_called()

    async def test_validation_read_finishes_before_network_budget_starts(self):
        item=self.due();claimed=self.store.claim_due_reminders()[0];ready={**claimed,'status':'pending_delivery'}
        self.runtime.notifications[item['id']]=ready;device,socket=self.device()
        self.store.validation_entered=threading.Event();self.store.validation_release=threading.Event()
        budget_elapsed=asyncio.Event();timer=None
        try:
            with patch('assistant_runtime.NOTIFICATION_SEND_TIMEOUT',0.01,create=True),patch.object(self.store,'valid_reminder_leases',wraps=self.store.valid_reminder_leases) as validation:
                sending=self.launch(self.runtime._send_notification_batch(device,[ready]))
                await self.thread_event(self.store.validation_entered)
                timer=asyncio.get_running_loop().call_later(0.03,budget_elapsed.set)
                await asyncio.wait_for(budget_elapsed.wait(),0.5)
                self.assertFalse(sending.done(),'网络预算不能中断尚未返回的数据库复核等待')
                self.assertEqual(validation.call_count,1)
                self.assertEqual(socket.notifications.qsize(),0)
                self.store.validation_release.set();await asyncio.wait_for(sending,0.5)
                self.assertEqual(validation.call_count,1)
                self.assertEqual(socket.notifications.get_nowait()['id'],item['id'])
        finally:
            if timer is not None:timer.cancel()
            self.store.validation_release.set()

    async def test_cached_owner_is_rechecked_after_waiting_for_device_send_lock(self):
        item=self.due();claimed=self.store.claim_due_reminders()[0];ready={**claimed,'status':'pending_delivery'}
        self.runtime.notifications[item['id']]=ready;device,socket=self.device()
        await device.send_lock.acquire();entered=asyncio.Event();original=device.send_notification
        async def send(candidate):
            entered.set();await original(candidate)
        device.send_notification=send
        sending=self.launch(self.runtime._send_notification_batch(device,[ready]))
        try:
            await asyncio.wait_for(entered.wait(),0.5)
            self.store.snooze_reminder(item['id'],claimed['updated_at']);self.runtime.notifications.pop(item['id'],None)
            device.send_lock.release();await asyncio.wait_for(sending,0.5)
            self.assertEqual(socket.notifications.qsize(),0)
        finally:
            if device.send_lock.locked():device.send_lock.release()


if __name__=='__main__':unittest.main()

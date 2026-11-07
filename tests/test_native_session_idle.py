"""原生会话结束状态与所有权：真实NativeDevice，不访问账号、云端或硬件。"""

import asyncio
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock

from assistant_runtime import NativeDevice
from gateway import CONFIG_KEY, RUNTIME_KEY, VoiceSession
from tests.test_wake_gate import FakeSocket


class NativeSessionIdleTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.runtime = SimpleNamespace(config={'max_connections':4}, voice_lock=asyncio.Lock(), voice_sessions=set(), last_device_state='idle')
        self.socket = FakeSocket()
        self.sessions = []
        self.entered = asyncio.Event()
        self.release = asyncio.Event()

        outer = self
        class Session:
            def __init__(inner, conversation):
                inner.session_id = f'offline-native-{len(outer.sessions)}'
                inner.conversation = conversation
                inner.browser = False
                inner.closed = False
                outer.sessions.append(inner)
            async def run(inner):
                outer.runtime.last_device_state = 'listening'
                outer.entered.set()
                try:
                    await outer.release.wait()
                finally:
                    inner.closed = True

        self.device = NativeDevice(self.socket, self.runtime, Session)

    async def asyncTearDown(self):
        self.release.set()
        await self.device.end_conversation()

    async def start(self):
        await self.device.start('auto')
        await asyncio.wait_for(self.entered.wait(),1)
        return self.device.task

    def idles(self):
        return [item for item in self.socket.sent if item.get('type')=='custom' and item.get('payload',{}).get('state')=='idle']

    async def test_normal_completion_updates_runtime_and_sends_one_idle(self):
        owner=await self.start()
        self.release.set();await asyncio.wait_for(owner,1)
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(len(self.idles()),1)
        self.assertEqual(self.runtime.voice_sessions,set())

    async def test_explicit_cancellation_finishes_owner_before_releasing_task_reference(self):
        owner=await self.start()
        await asyncio.wait_for(self.device.end_conversation(),1)
        self.assertTrue(owner.done())
        self.assertIsNone(self.device.task)
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(len(self.idles()),1)

    async def test_disconnected_socket_gets_runtime_idle_without_device_send(self):
        owner=await self.start()
        self.socket.closed=True
        await asyncio.wait_for(self.device.end_conversation(),1)
        self.assertTrue(owner.done())
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(self.idles(),[])

    async def test_other_browser_session_does_not_block_native_idle_state(self):
        owner=await self.start()
        class Browser:
            pass
        browser=Browser();browser.browser=True;browser.closed=False
        self.runtime.voice_sessions.add(browser)
        self.release.set();await asyncio.wait_for(owner,1)
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(self.runtime.voice_sessions,{browser})
        self.assertEqual(len(self.idles()),1)

    async def test_other_active_native_state_is_not_overwritten(self):
        class OtherNative:
            browser=False
            closed=False
        for active_state in ('speaking','connecting'):
            with self.subTest(active_state=active_state):
                self.entered.clear();self.release.clear()
                owner=await self.start()
                other=OtherNative();self.runtime.voice_sessions.add(other)
                self.runtime.last_device_state=active_state
                self.release.set();await asyncio.wait_for(owner,1)
                self.assertEqual(self.runtime.last_device_state,active_state)
                self.runtime.voice_sessions.discard(other)

    async def test_idle_send_failure_keeps_local_idle_and_propagates(self):
        owner=await self.start()
        self.socket.send_json=AsyncMock(side_effect=OSError('offline idle send failed'))
        self.release.set()
        with self.assertRaisesRegex(OSError,'offline idle send failed'):
            await asyncio.wait_for(owner,1)
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(self.runtime.voice_sessions,set())
        await self.device.end_conversation()
        self.assertIsNone(self.device.task)

    async def test_immediate_end_before_task_first_runs_releases_capacity_and_can_restart(self):
        self.runtime.config['max_connections']=1
        self.runtime.last_device_state='connecting'
        await self.device.start('auto')
        first=self.device.task
        await self.device.end_conversation()
        self.assertTrue(first.done())
        self.assertEqual(self.runtime.voice_sessions,set(),'未首次调度的取消也必须释放会话容量')
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(len(self.idles()),1)
        self.assertIsNone(self.device.task)
        self.entered.clear()
        await self.device.start('auto')
        self.assertIsNotNone(self.device.task)
        self.assertFalse(self.device.task.done())
        await asyncio.wait_for(self.entered.wait(),1)

    async def test_cleanup_inflight_blocks_new_start_until_old_idle_is_sent(self):
        await self.start()
        idle_entered,release_idle=asyncio.Event(),asyncio.Event()
        original=self.socket.send_json
        async def delayed_idle(payload):
            if payload.get('type')=='custom' and payload.get('payload',{}).get('state')=='idle':
                idle_entered.set();await release_idle.wait()
            await original(payload)
        self.socket.send_json=delayed_idle
        ending=asyncio.create_task(self.device.end_conversation())
        try:
            await asyncio.wait_for(idle_entered.wait(),1)
            await asyncio.wait_for(self.device.start('auto'),1)
            self.assertEqual(len(self.sessions),1,'旧任务还在收尾时不能创建新回合再被旧idle覆盖')
            self.assertIsNotNone(self.device.task)
            release_idle.set();await asyncio.wait_for(ending,1)
            self.entered.clear();self.release.clear()
            new_owner=await self.start()
            self.assertEqual(len(self.sessions),2)
            self.assertEqual(self.runtime.last_device_state,'listening')
            self.release.set();await asyncio.wait_for(new_owner,1)
        finally:
            release_idle.set();await asyncio.gather(ending,return_exceptions=True)

    async def test_replaced_owner_waiting_send_lock_cannot_publish_idle_to_new_session(self):
        owner=await self.start()
        await self.device.send_lock.acquire()
        self.release.set()
        blocked=asyncio.Event()
        asyncio.get_running_loop().call_soon(blocked.set)
        await asyncio.wait_for(blocked.wait(),1)
        replacement=asyncio.create_task(asyncio.Event().wait())
        self.device.task=replacement
        self.runtime.last_device_state='speaking'
        try:
            self.device.send_lock.release()
            await asyncio.wait_for(owner,1)
            self.assertEqual(self.idles(),[],'即使未来交接替换owner，旧finally也须在锁后复核身份')
            self.assertEqual(self.runtime.last_device_state,'speaking')
        finally:
            if self.device.send_lock.locked():self.device.send_lock.release()
            replacement.cancel();await asyncio.gather(replacement,return_exceptions=True)
            self.device.task=None

    async def test_actual_voice_session_wake_rejection_ends_as_native_idle(self):
        outer=self
        class Backend:
            connected=True
            name='offline-realtime'
            async def events(inner):
                yield {'type':'conversation.item.input_audio_transcription.completed','transcript':'未包含唤醒前缀'}
                await asyncio.Event().wait()
            async def close(inner):inner.connected=False

        def factory(conversation):
            voice=VoiceSession({CONFIG_KEY:{'enable_search':False,'session_seconds':600,'idle_seconds':600},RUNTIME_KEY:outer.runtime},conversation,raw_pcm=True)
            voice.backend=Backend()
            async def connect():
                await voice.state('listening')
                outer.entered.set()
            voice.connect=connect
            return voice

        self.device.session_factory=factory
        owner=await self.start()
        await asyncio.wait_for(owner,1)
        self.assertEqual(self.runtime.last_device_state,'idle')
        self.assertEqual(self.runtime.voice_sessions,set())
        self.assertEqual(len(self.idles()),1)


if __name__=='__main__':unittest.main()

"""暂停未确认结果合同：真实媒体_halt、服务与会话，设备及平台均为替身。"""

import asyncio
import json
from pathlib import Path
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock,patch

from aiohttp import web
from aiohttp.test_utils import TestClient,TestServer

from chat_pipeline import ChatBackend,_Chunk
from gateway import CONFIG_KEY,RUNTIME_KEY,RealtimeBackend,VoiceSession
from src.assistant_app.application.device_media_stream import DeviceMediaStream,_Playback,SAMPLE_RATE
from src.assistant_app.application.internet_music import InternetMusicService
from src.assistant_app.application.errors import tool_result_unconfirmed
from src.assistant_app.api.internet_music import action as music_action,INTERNET_MUSIC_KEY
from tests.test_internet_music_service import FakeMusicClient
from tests.test_tool_activity import FakeCloud


class PauseDevice:
    def __init__(self):
        self.socket=SimpleNamespace(closed=False)
        self.media_stream_supported=True
        self.messages=[]
        self.engine=None
        self.ack_pause=False
        self.position=50*SAMPLE_RATE

    async def send_json(self,payload):
        self.messages.append(payload)
        if payload.get('type')=='media_pause' and self.ack_pause:
            await self.engine.on_device_event(self,{'type':'media_ack','state':'pause','playback_id':payload['playback_id'],'played_samples':self.position})


class MusicPauseUnknownTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory=tempfile.TemporaryDirectory()
        self.device=PauseDevice()
        self.runtime=SimpleNamespace(config={'runtime_dir':self.directory.name},native_devices=[self.device],voice_sessions=[],voice_lock=asyncio.Lock(),proactive_speech=None,media_service=None,memory_service=None,connector_registry=SimpleNamespace(manifests=lambda:[]),operation_screen_feedback=AsyncMock())
        self.client_patch=patch('src.assistant_app.application.internet_music.MusicAssistantClient',FakeMusicClient)
        self.client_patch.start()
        self.service=InternetMusicService(self.runtime.config,SimpleNamespace(path=str(Path(self.directory.name)/'state.sqlite3')),self.runtime,None)
        self.service.closed=False
        self.assertIsInstance(self.service.stream,DeviceMediaStream)
        self.device.engine=self.service.stream
        self.track={'source':'music_assistant','ref':{'uri':'qqmusic://track/one','provider':'qqmusic','item_id':'one'},'title':'离线曲目','duration_s':200,'artists':[]}
        self.service.current=dict(self.track)
        self.service.platform_session='offline-lease'
        self.service.bookmark={'position_s':50.0,'position_confirmed':True}
        self.service.stream._current=_Playback(self.device,{},'offline-playback',0,state='playing',sent_samples=55*SAMPLE_RATE,played_samples=50*SAMPLE_RATE,ack={'state':'progress','played_samples':50*SAMPLE_RATE},start_attempted=True)
        self.service.selections['next-selection']={'track':{**self.track,'title':'下一首'},'expires':time.monotonic()+60}
        self.voice_backends=[]

    async def asyncTearDown(self):
        for backend in self.voice_backends:await backend.close()
        self.service.closed=True
        self.client_patch.stop()
        self.directory.cleanup()

    def short_wait(self):
        original=asyncio.wait_for
        async def shortened(awaitable,timeout):return await original(awaitable,min(timeout,0.01))
        return patch('src.assistant_app.application.device_media_stream.asyncio.wait_for',side_effect=shortened)

    async def test_missing_pause_ack_returns_unknown_but_preserves_actual_last_checkpoint(self):
        with self.short_wait(),self.assertLogs('assistant.device_media_stream',level='ERROR'):
            result=await self.service.action('pause')
        self.assertEqual(result.get('status'),'unknown')
        self.assertIs(result.get('retryable'),False)
        self.assertEqual(result.get('reason'),'pause_unconfirmed')
        self.assertIn('暂停未确认',result.get('message',''))
        self.assertIn('最近设备进度不代表已暂停',result.get('message',''))
        self.assertEqual(result['state'],'error')
        self.assertEqual(result['position_basis'],'device_i2s_ack')
        self.assertTrue(tool_result_unconfirmed(result))
        self.assertEqual(self.service.bookmark,{'position_s':50.0,'position_confirmed':True})
        saved=json.loads(self.service.state_path.read_text(encoding='utf-8'))
        self.assertEqual(saved['bookmark'],self.service.bookmark)
        self.assertEqual(self.service.platform_session,'offline-lease')
        self.service.client.stop.assert_not_awaited()

    async def test_new_explicit_resume_still_uses_existing_checkpoint_after_pause_was_unconfirmed(self):
        with self.short_wait(),self.assertLogs('assistant.device_media_stream',level='ERROR'):
            await self.service.action('pause')
        self.service._request=AsyncMock(return_value={'status':'scheduled'})
        new_voice=SimpleNamespace(closed=False)
        result=await self.service.action('resume',voice_session=new_voice)
        self.assertEqual(result['status'],'scheduled')
        self.service._request.assert_awaited_once_with(self.track,new_voice,50.0)

    async def test_matching_pause_ack_keeps_confirmed_position_and_repeat_pause_is_bounded(self):
        self.device.ack_pause=True
        self.device.position=52*SAMPLE_RATE
        first=await self.service.action('pause')
        second=await self.service.action('pause')
        self.assertEqual(first.get('status'),'paused')
        self.assertEqual(first['state'],'paused')
        self.assertFalse(tool_result_unconfirmed(first))
        self.assertEqual(self.service.bookmark,{'position_s':52.0,'position_confirmed':True})
        self.assertEqual(second.get('status'),'paused')
        self.assertEqual(sum(item.get('type')=='media_pause' for item in self.device.messages),1)

    async def test_repeat_pause_after_error_remains_unknown_and_idle_pause_does_not_claim_active_playback(self):
        with self.short_wait(),self.assertLogs('assistant.device_media_stream',level='ERROR'):
            await self.service.action('pause')
        again=await self.service.action('pause')
        self.assertEqual(again.get('status'),'unknown')
        self.assertTrue(tool_result_unconfirmed(again))
        self.assertEqual(sum(item.get('type')=='media_pause' for item in self.device.messages),1)
        self.service.current=None
        stopped=await self.service.action('pause')
        self.assertEqual(stopped['status'],'stopped')
        self.assertFalse(tool_result_unconfirmed(stopped))

    async def test_http_pause_passes_actual_status_to_result_and_audit(self):
        @web.middleware
        async def local_principal(request,handler):
            request['principal']=SimpleNamespace(subject='offline-test');request['request_id']='offline-pause'
            return await handler(request)
        app=web.Application(middlewares=[local_principal]);app[INTERNET_MUSIC_KEY]=self.service
        app.router.add_post('/api/music/actions/{action}',music_action)
        client=TestClient(TestServer(app));await client.start_server()
        self.device.ack_pause=True
        try:
            with patch('src.assistant_app.api.internet_music._audit') as audit:
                response=await client.post('/api/music/actions/pause',json={})
            self.assertEqual(response.status,200)
            self.assertEqual((await response.json()).get('status'),'paused')
            self.assertEqual(audit.call_args.args[2],'paused')
        finally:await client.close()

    async def test_realtime_unknown_pause_blocks_following_same_input_tool(self):
        voice=VoiceSession({CONFIG_KEY:{},RUNTIME_KEY:self.runtime},SimpleNamespace(send_json=AsyncMock(),closed=False),browser=True)
        backend=RealtimeBackend(voice);backend.cloud=FakeCloud();voice.backend=backend;self.voice_backends.append(backend)
        await voice.observe_user_text('暂停音乐并把下一首加入队列',source='text')
        calls=[{'call_id':'pause','name':'music_pause','arguments':'{}'},{'call_id':'queue','name':'music_queue','arguments':'{"selection_id":"next-selection"}'}]
        with self.short_wait(),self.assertLogs('assistant.device_media_stream',level='ERROR'):
            await voice.process_tools(calls,voice.generation,response_id='real-pause-response',input_epoch=voice.accepted_input_epoch)
        self.assertEqual(self.service.queue,[])
        self.assertTrue(voice.tools_paused)
        outputs=[json.loads(item['item']['output']) for item in backend.cloud.messages if item.get('type')=='conversation.item.create']
        self.assertEqual(outputs[0]['status'],'unknown')
        self.assertEqual(outputs[1]['code'],'tool_chain_paused')

    async def test_chat_unknown_pause_blocks_remaining_tools_and_new_call_id_retry(self):
        voice=VoiceSession({CONFIG_KEY:{},RUNTIME_KEY:self.runtime},SimpleNamespace(send_json=AsyncMock(),closed=False),browser=True)
        backend=ChatBackend(voice,'qwen_chat');voice.backend=backend;self.voice_backends.append(backend)
        backend._history=[];backend._instructions='offline';backend._events=asyncio.Queue();backend._cancelled=set();backend._response_sequence=0;backend._pending_history=backend._active_response_id=None
        await voice.observe_user_text('暂停音乐并把下一首加入队列',source='text')
        scripts=[[
            {'index':0,'id':'pause','function':{'name':'music_pause','arguments':'{}'}},
            {'index':1,'id':'queue','function':{'name':'music_queue','arguments':'{"selection_id":"next-selection"}'}}
        ],[{'index':0,'id':'new-id-retry','function':{'name':'music_queue','arguments':'{"selection_id":"next-selection"}'}}],[]]
        async def model(_messages):
            tools=scripts.pop(0)
            yield _Chunk(tool_delta=tools) if tools else _Chunk()
        backend._request=model
        with self.short_wait(),self.assertLogs('assistant.device_media_stream',level='ERROR'):
            await backend._run_turn('暂停音乐并把下一首加入队列')
        self.assertEqual(self.service.queue,[])
        self.assertTrue(voice.tools_paused)
        self.assertTrue(backend._tool_chain_paused)


if __name__=='__main__':unittest.main()

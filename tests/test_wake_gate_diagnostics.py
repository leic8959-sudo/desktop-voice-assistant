"""唤醒门禁脱敏计数：真实会话/Realtime音频发送，云与设备全为受控替身。"""

import asyncio
import json
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp

from gateway import CONFIG_KEY, RUNTIME_KEY, RealtimeBackend, VoiceSession, WakePhraseRejected, WAKE_PHRASE
from tests.test_input_observation import MessageSocket


class WakeGateDiagnosticTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.socket = MessageSocket()
        runtime = SimpleNamespace(memory_service=None, media_service=None, device_music=None, last_provider=None, last_cloud_ready=None)
        config = {'enable_search':False, 'session_seconds':600, 'idle_seconds':600}
        self.voice = VoiceSession({CONFIG_KEY:config,RUNTIME_KEY:runtime}, self.socket, browser=True)
        self.voice.require_wake_phrase = True
        self.backend = RealtimeBackend(self.voice)
        self.backend.cloud = MessageSocket()
        self.voice.backend = self.backend
        self.voice.execute_tool = AsyncMock()
        self.voice.refresh_memory_context = AsyncMock()

        async def ready():
            self.voice._wake_ready_at = asyncio.get_running_loop().time()
            await self.voice.state('listening')
        self.voice.connect = ready

    async def asyncTearDown(self):
        await self.backend.close()

    @staticmethod
    def summaries(records):
        return [json.loads(record.getMessage().split(' ',2)[2]) for record in records if record.getMessage().startswith('wake_gate_summary ')]

    def transcript(self, text):
        self.backend.cloud.receive_event({'type':'conversation.item.input_audio_transcription.completed','transcript':text})

    async def run_rejected(self):
        with self.assertLogs('assistant',level='INFO') as logs:
            await asyncio.wait_for(self.voice.run(),2)
        result = self.summaries(logs.records)
        self.assertEqual(len(result),1)
        return result[0]

    async def test_partial_wake_accepts_with_same_gate_and_exactly_one_frozen_summary(self):
        self.transcript('你好'); self.transcript('小燕子')
        with self.assertLogs('assistant',level='INFO') as logs:
            task = asyncio.create_task(self.voice.run())
            try:
                await self.socket.wait(lambda:any(item.get('type')=='stt' for item in self.socket.json))
                self.socket.incoming.put_nowait(None)
                await asyncio.wait_for(task,2)
                self.voice._summarize_wake_gate('timeout')
                self.voice._note_wake_transcript('private-after-freeze','')
                self.voice._note_realtime_append(640)
                self.voice._summarize_wake_gate('accepted')
            finally:
                if not task.done(): task.cancel()
                await asyncio.gather(task,return_exceptions=True)
        summaries=self.summaries(logs.records)
        self.assertEqual(len(summaries),1)
        summary=summaries[0]
        self.assertEqual(summary['reason'],'accepted')
        self.assertEqual(summary['transcript_events'],2)
        self.assertEqual(summary['normalized_chars'],len(WAKE_PHRASE))
        self.assertEqual(summary['prefix_match_chars'],len(WAKE_PHRASE))
        self.assertFalse(summary['wake_after_start'])
        self.assertEqual(summary['buffer_events'],2)
        self.assertGreater(summary['buffer_bytes'],0)
        self.assertGreaterEqual(summary['first_stt_ms'],0)
        self.assertTrue(self.voice.wake_phrase_verified)
        self.assertEqual(self.voice._wake_diagnostic['realtime_append_messages'],0)

    async def test_prefix_and_late_complete_phrase_flags_do_not_log_private_content_or_allow_tools(self):
        for index,(text,matched,late) in enumerate([('你好小喵',3,False),('https://private.invalid/?token=secret !!! Привет 你好小燕子',0,True)]):
            with self.subTest(matched=matched):
                if index:
                    await self.backend.close()
                    self.setUp()
                self.transcript(text)
                self.backend.cloud.receive_event({'type':'response.done','response':{'id':'must-not-run','output':[{'type':'function_call','call_id':'private-call','name':'create_task','arguments':'{"title":"must-not-write"}'}]}})
                summary=await self.run_rejected()
                self.assertEqual(summary['reason'],'phrase_missing')
                self.assertEqual(summary['prefix_match_chars'],matched)
                self.assertEqual(summary['wake_after_start'],late)
                self.assertFalse(self.voice.wake_phrase_verified)
                self.voice.execute_tool.assert_not_awaited()
                encoded=json.dumps(summary,ensure_ascii=False)
                for private in (text,'private.invalid','secret','Привет','你好','小喵','private-call','must-not-write'):
                    self.assertNotIn(private,encoded)
                self.assertTrue(all(isinstance(value,(int,bool)) for key,value in summary.items() if key!='reason'))
                await self.backend.close()

    async def test_timeout_distinguishes_pcm_received_from_successful_realtime_appends(self):
        for _ in range(2): self.socket.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY,data=b'\x01\x00'*320))
        with patch('gateway.WAKE_VERIFICATION_SECONDS',0.01):
            summary=await self.run_rejected()
        self.assertEqual(summary['reason'],'timeout')
        self.assertEqual(summary['input_messages'],2)
        self.assertEqual(summary['input_pcm_bytes'],1280)
        self.assertEqual(summary['realtime_append_messages'],2)
        self.assertEqual(summary['realtime_append_pcm_bytes'],1280)
        self.assertEqual(summary['transcript_events'],0)
        self.assertEqual(summary['first_stt_ms'],-1)
        self.assertGreaterEqual(summary['first_input_ms'],0)
        self.assertEqual(sum(item.get('type')=='input_audio_buffer.append' for item in self.backend.cloud.json),2)
        self.voice.execute_tool.assert_not_awaited()

    async def test_failed_audio_send_is_not_counted_as_successful_append(self):
        self.backend.cloud.send_json=AsyncMock(side_effect=OSError('synthetic-private-send-failure'))
        self.socket.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY,data=b'\x01\x00'*320))
        summary=await self.run_rejected()
        self.assertEqual(summary['reason'],'error')
        self.assertEqual(summary['input_messages'],1)
        self.assertEqual(summary['input_pcm_bytes'],640)
        self.assertEqual(summary['realtime_append_messages'],0)
        self.assertEqual(summary['realtime_append_pcm_bytes'],0)
        self.assertNotIn('synthetic-private-send-failure',str(summary))

    async def test_reason_is_whitelisted_and_manual_sessions_emit_no_summary(self):
        async def rejected(): raise WakePhraseRejected('https://private.invalid/?token=secret')
        self.voice.connect=rejected
        summary=await self.run_rejected()
        self.assertEqual(summary['reason'],'rejected')
        self.assertNotIn('private',str(summary))
        self.voice._wake_summary_sent=False
        self.voice.require_wake_phrase=False
        with patch('gateway.LOGGER.info') as logger:
            self.voice._summarize_wake_gate('accepted')
        logger.assert_not_called()

    async def test_chat_precheck_counts_split_wake_once_without_output_reobservation(self):
        self.voice._wake_ready_at=asyncio.get_running_loop().time()
        pending={}
        with self.assertLogs('assistant',level='INFO') as logs:
            self.assertIsNone(self.voice.prepare_chat_user_text('你好',pending))
            self.assertEqual(self.voice.prepare_chat_user_text('小燕子',pending),WAKE_PHRASE)
            self.voice._summarize_wake_gate('session_ended')
        summary=self.summaries(logs.records)[0]
        self.assertEqual(summary['transcript_events'],2)
        self.assertEqual(summary['normalized_chars'],len(WAKE_PHRASE))
        self.assertEqual(summary['prefix_match_chars'],len(WAKE_PHRASE))
        self.assertEqual(summary['reason'],'accepted')

    async def test_ready_diagnostic_anchor_is_after_connect_wait_before_listening_notification(self):
        entered,release=asyncio.Event(),asyncio.Event()
        async def slow_connect(): entered.set();await release.wait()
        self.backend.connect=slow_connect
        original_send=self.socket.send_json
        observed=[]
        async def send(event):
            if event.get('type')=='custom' and event.get('payload',{}).get('state')=='listening': observed.append(self.voice._wake_ready_at)
            await original_send(event)
        self.socket.send_json=send
        with patch('gateway.provider_chain',return_value=['qwen_realtime']),patch('gateway.RealtimeBackend',return_value=self.backend):
            task=asyncio.create_task(VoiceSession.connect(self.voice))
            try:
                await asyncio.wait_for(entered.wait(),1)
                self.assertIsNone(self.voice._wake_ready_at)
                release.set();await asyncio.wait_for(task,1)
                self.assertEqual(observed,[self.voice._wake_ready_at])
                self.assertIsNotNone(observed[0])
                self.assertGreaterEqual(self.voice._wake_elapsed_ms(),0)
            finally:
                release.set();await asyncio.gather(task,return_exceptions=True)


if __name__=='__main__':unittest.main()

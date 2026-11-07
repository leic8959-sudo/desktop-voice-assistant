"""被动上行PCM数字摘要：仅合成PCM和受控后端，不打开麦克风或网络。"""

import json
import math
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import AsyncMock, patch

import aiohttp

from gateway import CONFIG_KEY, RealtimeBackend, RealtimeTransportError, VoiceSession, WAKE_PCM_HEAD_SAMPLES, WAKE_PHRASE
from tests.test_input_observation import MessageSocket


class WakePcmStatsTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.socket = MessageSocket()
        self.voice = VoiceSession({CONFIG_KEY:{'session_seconds':600}}, self.socket, raw_pcm=True)
        self.voice.require_wake_phrase = True

    def summaries(self, reason='phrase_missing'):
        with self.assertLogs('assistant', level='INFO') as logs:
            self.voice._summarize_wake_gate(reason)
        return [json.loads(record.getMessage().split(' ',2)[2]) for record in logs.records if record.getMessage().startswith('wake_gate_summary ')]

    async def test_little_endian_extremes_zero_clipping_and_integer_rms(self):
        self.voice._note_wake_pcm(b'\x01\x02\x00\x80\xff\x7f\x00\x00\x03\x00\xfc\xff')
        summary = self.summaries()[0]
        values = [513,-32768,32767,0,3,-4]
        self.assertEqual(summary['pcm_head_samples'], 6)
        self.assertEqual(summary['pcm_head_rms'], math.isqrt(sum(value*value for value in values) // len(values)))
        self.assertEqual(summary['pcm_head_peak'], 32768)
        self.assertEqual(summary['pcm_head_zero_samples'], 1)
        self.assertEqual(summary['pcm_head_clipped_samples'], 2)
        self.assertEqual({key:value for key,value in summary.items() if key.startswith('pcm_tail_')}, {'pcm_tail_samples':0,'pcm_tail_rms':0,'pcm_tail_peak':0,'pcm_tail_zero_samples':0,'pcm_tail_clipped_samples':0})
        self.assertTrue(all(type(value) is int for key,value in summary.items() if key.startswith('pcm_')))

    async def test_head_boundary_splits_inside_frame_and_tail_accumulates_across_frames(self):
        self.voice._note_wake_pcm(struct.pack('<h',2) * (WAKE_PCM_HEAD_SAMPLES - 2))
        self.voice._note_wake_pcm(struct.pack('<hhhh',3,4,0,-32768))
        self.voice._note_wake_pcm(struct.pack('<hh',32767,5))
        summary = self.summaries()[0]
        self.assertEqual(summary['pcm_head_samples'], 64000)
        self.assertEqual(summary['pcm_head_rms'], math.isqrt(((64000-2)*4 + 9 + 16) // 64000))
        self.assertEqual(summary['pcm_head_peak'], 4)
        self.assertEqual(summary['pcm_head_clipped_samples'], 0)
        self.assertEqual(summary['pcm_tail_samples'], 4)
        self.assertEqual(summary['pcm_tail_rms'], math.isqrt((32768**2 + 32767**2 + 25) // 4))
        self.assertEqual(summary['pcm_tail_zero_samples'], 1)
        self.assertEqual(summary['pcm_tail_clipped_samples'], 2)

    async def test_exact_head_boundary_does_not_create_tail_samples(self):
        self.voice._note_wake_pcm(b'\0\0' * WAKE_PCM_HEAD_SAMPLES)
        summary = self.summaries()[0]
        self.assertEqual(summary['pcm_head_samples'], 64000)
        self.assertEqual(summary['pcm_head_zero_samples'], 64000)
        self.assertEqual(summary['pcm_head_rms'], 0)
        self.assertEqual(summary['pcm_tail_samples'], 0)

    async def test_empty_pcm_summary_has_fixed_zero_integer_fields(self):
        self.voice._note_wake_pcm(b'')
        summary = self.summaries()[0]
        fields = {key:value for key,value in summary.items() if key.startswith('pcm_')}
        self.assertEqual(len(fields), 10)
        self.assertTrue(all(type(value) is int and value == 0 for value in fields.values()))

    async def test_manual_verified_and_summary_frozen_paths_do_not_scan_or_mutate(self):
        self.voice._note_wake_pcm(struct.pack('<hh',3,4))
        before = [dict(bucket) for bucket in self.voice._wake_pcm]
        for state in ('manual','verified','summary'):
            with self.subTest(state=state):
                self.voice.require_wake_phrase = state != 'manual'
                self.voice.wake_phrase_verified = state == 'verified'
                self.voice._wake_summary_sent = state == 'summary'
                with patch('gateway.struct.iter_unpack', side_effect=AssertionError('冻结后不得扫描PCM')):
                    self.voice._note_wake_pcm(b'\x00\x80')
                self.assertEqual(self.voice._wake_pcm, before)
        with patch('gateway.LOGGER.info') as logger:
            self.voice._summarize_wake_gate('error')
        logger.assert_not_called()

    async def test_actual_gate_acceptance_freezes_pcm_and_emits_one_private_safe_summary(self):
        backend = RealtimeBackend(self.voice)
        backend.cloud = MessageSocket()
        self.voice.backend = backend
        self.voice._note_wake_pcm(struct.pack('<hhhh',0,1,2,3))
        backend.cloud.receive_event({'type':'conversation.item.input_audio_transcription.completed','transcript':WAKE_PHRASE})
        backend.cloud.incoming.put_nowait(None)
        try:
            with self.assertLogs('assistant', level='INFO') as logs:
                accepted = [event async for event in self.voice.verified_backend_events()]
                frozen = [dict(bucket) for bucket in self.voice._wake_pcm]
                with patch('gateway.struct.iter_unpack', side_effect=AssertionError('已接受后不得扫描')):
                    self.voice._note_wake_pcm(b'private-waveform-not-retained')
                self.voice._note_wake_transcript('private-user-text','')
                self.voice._summarize_wake_gate('error')
            summaries = [json.loads(record.getMessage().split(' ',2)[2]) for record in logs.records if record.getMessage().startswith('wake_gate_summary ')]
            self.assertEqual(len(summaries), 1)
            self.assertEqual(summaries[0]['reason'], 'accepted')
            self.assertEqual(summaries[0]['pcm_head_samples'], 4)
            self.assertTrue(self.voice.wake_phrase_verified)
            self.assertEqual(self.voice._wake_pcm, frozen)
            self.assertEqual(len(accepted), 1)
            serialized = json.dumps(summaries[0], ensure_ascii=False)
            for private in (WAKE_PHRASE,'private-user-text','waveform','transcript','hash'):
                self.assertNotIn(private, serialized if private != 'transcript' else serialized.replace('transcript_events',''))
            self.assertTrue(all(isinstance(value,(int,bool)) for key,value in summaries[0].items() if key != 'reason'))
        finally:
            await backend.close()

    async def test_failed_append_counts_received_pcm_without_claiming_success(self):
        backend = RealtimeBackend(self.voice)
        backend.cloud = MessageSocket()
        original_error = OSError('private-send-error')
        backend.cloud.send_json = AsyncMock(side_effect=original_error)
        self.voice.backend = backend
        pcm = struct.pack('<hhh',0,-32768,7)
        self.socket.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY,data=pcm))
        try:
            with self.assertRaises(RealtimeTransportError) as raised:
                await self.voice.device_input()
            self.assertIs(raised.exception.__cause__, original_error)
            summary = self.summaries('error')[0]
            self.assertEqual(summary['input_messages'], 1)
            self.assertEqual(summary['input_pcm_bytes'], len(pcm))
            self.assertEqual(summary['pcm_head_samples'], 3)
            self.assertEqual(summary['pcm_head_peak'], 32768)
            self.assertEqual(summary['realtime_append_messages'], 0)
            self.assertEqual(summary['realtime_append_pcm_bytes'], 0)
            self.assertNotIn('private-send-error', str(summary))
        finally:
            await backend.close()

    async def test_successful_device_input_keeps_old_message_counts_separate_from_samples(self):
        backend = RealtimeBackend(self.voice)
        backend.cloud = MessageSocket()
        self.voice.backend = backend
        for values in ((0,3,4),(-7,)):
            self.socket.incoming.put_nowait(SimpleNamespace(type=aiohttp.WSMsgType.BINARY,data=struct.pack('<' + 'h'*len(values),*values)))
        self.socket.incoming.put_nowait(None)
        try:
            await self.voice.device_input()
            summary = self.summaries()[0]
            self.assertEqual(summary['input_messages'], 2)
            self.assertEqual(summary['realtime_append_messages'], 2)
            self.assertEqual(summary['input_pcm_bytes'], 8)
            self.assertEqual(summary['realtime_append_pcm_bytes'], 8)
            self.assertEqual(summary['pcm_head_samples'], 4)
        finally:
            await backend.close()

    async def test_buckets_retain_only_fixed_number_of_integer_accumulators(self):
        size = len(self.voice._wake_pcm)
        expected = {'samples','sum_squares','peak','zero_samples','clipped_samples'}
        for _ in range(10):
            self.voice._note_wake_pcm(b'\x01\x00' * 16000)
        self.assertEqual(len(self.voice._wake_pcm), size)
        for bucket in self.voice._wake_pcm:
            self.assertEqual(set(bucket), expected)
            self.assertTrue(all(type(value) is int for value in bucket.values()))
        self.assertEqual(self.voice._wake_pcm[0]['samples'], 64000)
        self.assertEqual(self.voice._wake_pcm[1]['samples'], 96000)


if __name__ == '__main__':
    unittest.main()

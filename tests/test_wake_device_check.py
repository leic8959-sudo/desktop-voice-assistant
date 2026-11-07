"""单次唤醒数字诊断工具：合成PCM、临时配对快照和本机假设备，无实机采音。"""

import asyncio
import base64
from contextlib import asynccontextmanager, redirect_stdout
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import aiohttp

from src.assistant_app.application.pairing import PairingManager
from tools import wake_device_check as tool


def arguments(**changes):
    return SimpleNamespace(seconds=6, capture_delay=0, capture_audio=True, stats_only=True, sample=None, output='unused.json', **changes)


class WakeDeviceStatsTests(unittest.TestCase):
    def test_synthetic_three_channel_statistics_are_exact_and_empty_is_safe(self):
        pcm = struct.pack('<hhhhhh', 0, 3, -4, 32767, -32768, 0)
        stats = tool.capture_statistics(pcm)
        self.assertEqual(stats['mic'], {'rms':round((32767**2 / 2)**0.5,2),'peak':32767,'clip_fraction':0.5,'zero_fraction':0.5})
        self.assertEqual(stats['reference'], {'rms':round(((3**2 + 32768**2) / 2)**0.5,2),'peak':32768,'clip_fraction':0.5,'zero_fraction':0})
        self.assertEqual(stats['clean'], {'rms':2.83,'peak':4,'clip_fraction':0,'zero_fraction':0.5})
        self.assertTrue(all(values == {'rms':0,'peak':0,'clip_fraction':0,'zero_fraction':0} for values in tool.capture_statistics(b'').values()))

    def test_encoded_decoded_total_and_channel_caps_fail_before_unbounded_decode(self):
        maximum = b'\0' * tool.CAPTURE_CHUNK_BYTES
        self.assertEqual(tool.decode_capture_chunk(base64.b64encode(maximum).decode(), 0), maximum)
        with patch.object(tool.base64, 'b64decode') as decode, self.assertRaises(ValueError):
            tool.decode_capture_chunk('A' * 4097, 0)
        decode.assert_not_called()
        for encoded, count in ((base64.b64encode(b'\0' * 8).decode(),0),('not-base64!',0),(None,0),(base64.b64encode(b'\0' * 12).decode(),tool.CAPTURE_MAX_BYTES - 6)):
            with self.subTest(count=count), self.assertRaises((ValueError, TypeError)):
                tool.decode_capture_chunk(encoded, count)
        for pcm in (b'\0' * 8, b'\0' * (tool.CAPTURE_MAX_BYTES + 6)):
            with self.assertRaises(ValueError):
                tool.capture_statistics(pcm)

    def test_stats_requires_explicit_capture_no_playback_and_bounded_complete_window(self):
        self.assertEqual(tool.observation_seconds(arguments()), 6)
        args = arguments(); args.seconds = 120
        self.assertEqual(tool.observation_seconds(args), 30)
        for field, value in (('capture_audio',False),('sample','private.wav'),('capture_delay',1),('seconds',0)):
            args = arguments(); setattr(args, field, value)
            with self.subTest(field=field), self.assertRaises(ValueError):
                tool.observation_seconds(args)
        args = arguments(); args.stats_only = False; args.seconds = 10
        self.assertEqual(tool.observation_seconds(args), 60)

    def test_metric_whitelist_rejects_private_strings_bool_nan_inf_and_outside_range(self):
        private = 'do-not-export-private-audio-text'
        self.assertEqual(tool.diagnostic_metrics({'voice':private,'clean_rms':private,'ref_rms':float('nan'),'correlation':float('inf')}), {'voice':False})
        self.assertEqual(tool.diagnostic_metrics({'voice':True,'clean_rms':True,'ref_rms':10**400,'correlation':2}), {'voice':True})
        self.assertEqual(tool.diagnostic_metrics({'voice':False,'clean_rms':32768,'ref_rms':12.5,'correlation':-0.5}), {'voice':False,'clean_rms':32768,'ref_rms':12.5,'correlation':-0.5})


class WakeDeviceCheckApiTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.runtime = self.root / 'runtime'
        self.snapshot = self.runtime / 'pairing-sessions.json'
        manager = PairingManager(storage_path=self.snapshot)
        pending = manager.create('offline-wake-check-nonce-0001')
        paired = manager.claim_code(pending.code, 'local')
        self.credential = paired.device_credential
        self.legacy = 'offline-wake-legacy-token-00000'
        self.config_path = self.root / 'config.local.json'
        self.config_path.write_text(json.dumps({'host':'127.0.0.1','port':0,'device_id':'offline-legacy','device_token':self.legacy,'runtime_dir':str(self.runtime)}), encoding='utf-8')
        self.before_config, self.before_pairing = self.config_path.read_bytes(), self.snapshot.read_bytes()

    def tearDown(self):
        self.assertEqual(self.config_path.read_bytes(), self.before_config)
        self.assertEqual(self.snapshot.read_bytes(), self.before_pairing)
        self.directory.cleanup()

    @asynccontextmanager
    async def running(self):
        args = arguments(); args.output = str(self.root / 'result.json')
        ready, addresses = asyncio.Event(), []
        original_start = tool.web.TCPSite.start
        output = io.StringIO()

        async def start(site):
            await original_start(site)
            addresses.extend(site._runner.addresses)
            ready.set()

        original_resolve = aiohttp.TCPConnector._resolve_host

        async def only_loopback(connector, host, port, *params, **kwargs):
            if host not in {'127.0.0.1','::1','localhost'}:
                raise AssertionError('唤醒诊断回归禁止非本机连接')
            return await original_resolve(connector, host, port, *params, **kwargs)

        with patch.object(tool,'ROOT',self.root), patch.object(tool.web.TCPSite,'start',start), patch.object(aiohttp.TCPConnector,'_resolve_host',only_loopback), patch.object(PairingManager,'_save',side_effect=AssertionError('工具不能写配对快照')), patch.object(PairingManager,'get',side_effect=AssertionError('工具不能调用可能写过期状态的get')), patch.object(tool.wave,'open',side_effect=AssertionError('stats-only不能写或读WAV')), patch.object(tool,'play_sample',side_effect=AssertionError('stats-only不能播放样本')), redirect_stdout(output):
            task = asyncio.create_task(tool.main(args))
            try:
                await asyncio.wait_for(ready.wait(), 2)
                async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=3)) as client:
                    yield SimpleNamespace(client=client,url=f'http://127.0.0.1:{addresses[0][1]}/device/v1/',task=task,args=args,stdout=output)
            finally:
                if not task.done():
                    task.cancel()
                await asyncio.gather(task, return_exceptions=True)
        self.assertEqual(list(self.root.rglob('*.wav')), [])

    async def device(self, run, credential=None):
        socket = await run.client.ws_connect(run.url, headers={'Authorization':'Bearer ' + (credential or self.credential)})
        await socket.send_json({'type':'hello','version':1})
        controls = []
        while True:
            event = await socket.receive_json(timeout=2)
            controls.append(event)
            if event.get('type') == 'diagnostics' and event.get('capture_audio') is True:
                return socket, controls

    async def closed_controls(self, socket, controls):
        async for message in socket:
            if message.type == aiohttp.WSMsgType.TEXT:
                controls.append(json.loads(message.data))
        self.assertTrue(socket.closed)
        self.assertTrue(any(event.get('type') == 'diagnostics' and event.get('enabled') is False for event in controls))

    def result(self, run):
        return json.loads(Path(run.args.output).read_text(encoding='utf-8'))

    async def test_paired_auth_is_read_only_unknown_nonascii_401_and_second_device_rejected(self):
        async with self.running() as run:
            for authorization in ('Bearer unknown-device-credential','Bearer 不是设备凭据',self.legacy):
                with self.assertRaises(aiohttp.WSServerHandshakeError) as error:
                    await run.client.ws_connect(run.url, headers={'Authorization':authorization})
                self.assertEqual(error.exception.status, 401)
            socket, controls = await self.device(run)
            with self.assertRaises(aiohttp.WSServerHandshakeError) as error:
                await run.client.ws_connect(run.url, headers={'Authorization':'Bearer ' + self.legacy})
            self.assertEqual(error.exception.status, 409)
            await socket.send_json({'type':'audio_diagnostic','pcm':'invalid!','done':True})
            await self.closed_controls(socket, controls)
            await asyncio.wait_for(run.task, 3)
            self.assertFalse(self.result(run)['capture_done'])
            self.assertEqual(self.result(run)['cleanup_errors'], [])
            self.assertNotIn(self.credential, run.stdout.getvalue())

    async def test_legacy_token_is_accepted_and_incomplete_capture_still_disables_diagnostics(self):
        async with self.running() as run:
            socket, controls = await self.device(run, self.legacy)
            await socket.send_json({'type':'audio_diagnostic','pcm':base64.b64encode(struct.pack('<hhh',1,2,3)).decode(),'done':True})
            await self.closed_controls(socket, controls)
            await asyncio.wait_for(run.task, 3)
            result = self.result(run)
            self.assertFalse(result['capture_done'])
            self.assertEqual(result['capture_errors'], ['ValueError'])
            self.assertEqual(result['captured_samples'], 1)
            self.assertIsNone(result['recording_file'])

    async def test_stats_only_full_synthetic_capture_has_no_audio_file_or_private_payload(self):
        async with self.running() as run:
            socket, controls = await self.device(run)
            private = 'private-device-transcript-must-not-survive'
            await socket.send_json({'type':'listen','state':'start','mode':private})
            await socket.send_json({'type':'barge_diagnostic','clean_rms':private,'ref_rms':float('inf'),'voice':private,'correlation':float('nan')})
            frame = struct.pack('<hhhhhh',0,3,-4,32767,-32768,0)
            pcm = frame * (tool.CAPTURE_MAX_BYTES // len(frame))
            encoded_first = None
            for offset in range(0, len(pcm), tool.CAPTURE_CHUNK_BYTES):
                chunk = pcm[offset:offset + tool.CAPTURE_CHUNK_BYTES]
                encoded = base64.b64encode(chunk).decode()
                encoded_first = encoded_first or encoded
                await socket.send_json({'type':'audio_diagnostic','pcm':encoded,'done':offset + len(chunk) == len(pcm)})
            await self.closed_controls(socket, controls)
            await asyncio.wait_for(run.task, 3)
            result = self.result(run)
            self.assertTrue(result['capture_done'])
            self.assertEqual(result['captured_samples'], 81920)
            self.assertEqual(result['capture_stats'], tool.capture_statistics(pcm))
            self.assertEqual(result['cleanup_errors'], [])
            self.assertIsNone(result['recording_file'])
            self.assertFalse(result['cloud_used'])
            self.assertEqual(result['capture_location'], 'software_aec_output_before_afe_not_final_uplink')
            self.assertEqual(result['events'][0]['mode'], 'unknown')
            self.assertEqual(result['diagnostics'][0]['voice'], False)
            serialized = Path(run.args.output).read_text(encoding='utf-8') + run.stdout.getvalue()
            self.assertNotIn(private, serialized)
            self.assertNotIn(encoded_first, serialized)
            self.assertNotIn(self.credential, serialized)
            self.assertFalse(any(event.get('test_listening') for event in controls))

    async def test_cancelled_window_finishes_disable_and_close_before_main_returns(self):
        async with self.running() as run:
            socket, controls = await self.device(run)
            run.task.cancel()
            await self.closed_controls(socket, controls)
            with self.assertRaises(asyncio.CancelledError):
                await run.task
            result = self.result(run)
            self.assertFalse(result['capture_done'])
            self.assertEqual(result['cleanup_errors'], [])

    async def test_window_timeout_disables_diagnostics_and_is_not_complete_capture(self):
        async with self.running() as run:
            socket, controls = await self.device(run)
            await self.closed_controls(socket, controls)
            await asyncio.wait_for(run.task, 3)
            result = self.result(run)
            self.assertFalse(result['capture_done'])
            self.assertIn('capture_window_timeout', result['capture_errors'])
            self.assertEqual(result['cleanup_errors'], [])


if __name__ == '__main__':
    unittest.main()

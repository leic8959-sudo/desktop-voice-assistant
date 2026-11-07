"""配对持久化和设备认证边界的离线验收。"""

import tempfile
import unittest
import asyncio
from datetime import datetime, timedelta, timezone
from pathlib import Path
from unittest.mock import patch

from aiohttp.test_utils import TestClient, TestServer
from aiohttp import WSServerHandshakeError, WSMsgType

from gateway import create_app
from control_api import PAIRING_KEY
from assistant_runtime import RUNTIME_KEY
from src.assistant_app.application.pairing import PairingManager, PairingStatus


class PairingPersistenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "pairing.json"
        self.manager = PairingManager(storage_path=self.path)

    def tearDown(self):
        self.temp.cleanup()

    def test_restart_preserves_claim_and_revocation(self):
        session = self.manager.create("nonce-for-restart-0001")
        claimed = self.manager.claim_code(session.code, "local")
        restored = PairingManager(storage_path=self.path)
        self.assertEqual(restored.authenticate_device(claimed.device_credential), claimed.device_id)
        restored.revoke(claimed.pairing_id)
        self.assertIsNone(PairingManager(storage_path=self.path).authenticate_device(claimed.device_credential))

    def test_nonce_retry_returns_same_session_after_restart(self):
        session = self.manager.create("nonce-for-retry-000001")
        restored = PairingManager(storage_path=self.path)
        self.assertEqual(restored.create(session.device_nonce).pairing_id, session.pairing_id)

    def test_delivery_grace_and_nonce_binding(self):
        session = self.manager.create("nonce-for-delivery-0001")
        self.manager.claim_code(session.code, "local")
        session.expires_at = datetime.now(timezone.utc) - timedelta(seconds=1)
        self.assertEqual(self.manager.device_result(session.pairing_id, session.code, session.device_nonce), session.device_credential)
        self.assertIsNone(self.manager.device_result(session.pairing_id, session.code, "incorrect-device-nonce"))
        session.delivery_expires_at = datetime.now(timezone.utc) - timedelta(seconds=1)
        self.assertIsNone(self.manager.device_result(session.pairing_id, session.code, session.device_nonce))

    def test_snapshot_failure_rolls_back_claim(self):
        session = self.manager.create("nonce-for-rollback-0001")
        with patch("src.assistant_app.application.pairing.os.replace", side_effect=OSError("disk unavailable")):
            with self.assertRaises(OSError):
                self.manager.claim_code(session.code, "local")
        self.assertEqual(self.manager.get(session.pairing_id).status, PairingStatus.PENDING)
        self.assertEqual(PairingManager(storage_path=self.path).get(session.pairing_id).status, PairingStatus.PENDING)
        self.assertIsNone(self.manager.get(session.pairing_id).device_credential)

    def test_directory_failure_rolls_back_claim(self):
        session = self.manager.create("nonce-for-mkdir-rollback")
        with patch("src.assistant_app.application.pairing.Path.mkdir", side_effect=OSError("directory denied")):
            with self.assertRaises(OSError):
                self.manager.claim_code(session.code, "local")
        self.assertEqual(self.manager.get(session.pairing_id).status, PairingStatus.PENDING)
        self.assertEqual(PairingManager(storage_path=self.path).get(session.pairing_id).status, PairingStatus.PENDING)


class PairingApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.token = "pairing-test-admin-token-000000000"
        config = {"host": "127.0.0.1", "port": 0, "public_base_url": "http://127.0.0.1:8765", "device_token": self.token, "provisioning_token": "pairing-test-provisioning-000000", "device_id": "legacy-test-device", "aliyun_api_key": "mock-key", "aliyun_workspace_id": "mock-space", "enable_search": False, "memory_backend": "pc_prototype", "home_entities": {}, "runtime_dir": self.temp.name}
        self.app = create_app(config)
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {"Authorization": "Bearer " + self.token}

    async def asyncTearDown(self):
        await self.client.close()
        self.temp.cleanup()

    async def test_device_start_claim_result_and_revoke(self):
        bad = await self.client.post("/api/devices/pairing/device-start", json={"device_nonce": "short"})
        self.assertEqual(bad.status, 400)
        payload = {"device_nonce": "api-device-nonce-00000001"}
        start = await self.client.post("/api/devices/pairing/device-start", json=payload)
        self.assertEqual(start.status, 201)
        session = await start.json()
        retry = await self.client.post("/api/devices/pairing/device-start", json=payload)
        self.assertEqual((await retry.json())["pairing_id"], session["pairing_id"])
        claim = await self.client.post("/api/devices/pairing/claim", headers=self.headers, json={"code": session["pairing_code"]})
        self.assertEqual(claim.status, 200)
        device_id = (await claim.json())["device_id"]
        result_path = f"/api/devices/pairing/{session['pairing_id']}/device-result"
        result = await self.client.post(result_path, json={**payload, "code": session["pairing_code"]})
        self.assertEqual(result.status, 200)
        credential = (await result.json())["device_credential"]
        device_headers = {"Authorization": "Bearer " + credential}
        forbidden = await self.client.get("/api/status", headers=device_headers)
        self.assertEqual(forbidden.status, 401)
        artifact = Path(self.temp.name) / "firmware.bin"
        artifact.write_bytes(b"offline-firmware-fixture")
        with patch("control_api.ota_firmware_manifest", return_value={"path": str(artifact)}):
            download = await self.client.get("/api/ota/firmware", headers=device_headers)
            self.assertEqual(download.status, 200)
            self.assertEqual(await download.read(), artifact.read_bytes())
            revoke = await self.client.post(f"/api/devices/{device_id}/unpair", headers=self.headers, json={})
            self.assertEqual(revoke.status, 200)
            rejected = await self.client.get("/api/ota/firmware", headers=device_headers)
            self.assertEqual(rejected.status, 401)
        self.assertIsNone(self.app[PAIRING_KEY].authenticate_device(credential))

    async def test_dedicated_websocket_handshake_and_revoke_disconnect(self):
        manager = self.app[PAIRING_KEY]
        session = manager.create("websocket-device-nonce-0001")
        paired = manager.claim_code(session.code, "local")
        headers = {"Authorization": "Bearer " + paired.device_credential}
        with patch("gateway.ota_firmware_manifest", return_value=None):
            socket = await self.client.ws_connect("/device/v1/", headers=headers)
            try:
                await socket.send_json({"type": "hello", "version": 1, "firmware_version": "pairing-test", "audio_params": {"format": "pcm_s16le", "sample_rate": 16000, "channels": 1}})
                hello = await socket.receive_json(timeout=2)
                self.assertEqual(hello["type"], "hello")
                self.assertEqual(hello["audio_params"]["sample_rate"], 24000)
                devices = self.app[RUNTIME_KEY].native_devices
                self.assertEqual({device.paired_device_id for device in devices}, {paired.device_id})
                self.assertFalse(self.app[RUNTIME_KEY].voice_sessions)
                revoked = await self.client.post(f"/api/devices/{paired.device_id}/unpair", headers=self.headers, json={})
                self.assertEqual(revoked.status, 200)
                async with asyncio.timeout(3):
                    while True:
                        message = await socket.receive()
                        if message.type in (WSMsgType.CLOSE, WSMsgType.CLOSED):
                            break
                self.assertFalse(self.app[RUNTIME_KEY].native_devices)
                with self.assertRaises(WSServerHandshakeError) as rejected:
                    await self.client.ws_connect("/device/v1/", headers=headers)
                self.assertEqual(rejected.exception.status, 401)
            finally:
                await socket.close()

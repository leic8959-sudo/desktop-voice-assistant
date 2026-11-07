"""本地模拟验证，不接真实阿里、家居或串口。"""

import asyncio
import base64
import json
import tempfile
import unittest
from unittest.mock import patch
from datetime import datetime, timedelta, timezone
from pathlib import Path
import sys

import aiohttp
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from gateway import create_app
from assistant_runtime import RUNTIME_KEY
from home_control import HomeController
from tests.home_fixture import register_home_api

TOKEN = "local-integration-token-000000000"


class GatewayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.blocked_hosts = []
        original_resolve = aiohttp.TCPConnector._resolve_host

        async def loopback_only(connector, host, port, *arguments, **keywords):
            if host not in {"127.0.0.1", "::1", "localhost"}:
                self.blocked_hosts.append(host)
                raise AssertionError("离线网关回归禁止访问非本机主机")
            return await original_resolve(connector, host, port, *arguments, **keywords)

        network_guard = patch.object(aiohttp.TCPConnector, "_resolve_host", loopback_only)
        network_guard.start()
        self.network_guard = network_guard
        self.addCleanup(network_guard.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.cloud_calls = 0
        self.cloud_events = []
        self.cloud_config = None
        self.received_pcm = None
        self.audio_chunk_count = 1
        self.mock_auto_reply = True
        self.mock_first_transcript = "你好，小燕子"
        self.mock_transcript_before_response = True
        self.mock_tool_response = None

        async def cloud(request):
            self.cloud_calls += 1
            socket = web.WebSocketResponse()
            await socket.prepare(request)
            await socket.send_json({"type": "session.created"})
            manual = False
            wake_transcript_sent = False

            async def reply():
                nonlocal wake_transcript_sent
                if not wake_transcript_sent:
                    wake_transcript_sent = True
                    if self.mock_transcript_before_response:
                        await socket.send_json({"type": "conversation.item.input_audio_transcription.completed", "transcript": self.mock_first_transcript})
                if self.mock_tool_response is not None:
                    tools = self.mock_tool_response
                    self.mock_tool_response = None
                    await socket.send_json({"type":"response.created", "response":{"id":"mock-tool-response"}})
                    await socket.send_json({"type":"response.done", "response":{"id":"mock-tool-response", "status":"completed", "output":tools, "usage":{"input_tokens":10,"output_tokens":5}}})
                    return
                await socket.send_json({"type": "response.created", "response": {"id": "mock-response"}})
                for _ in range(self.audio_chunk_count):
                    await socket.send_json({"type": "response.audio.delta", "response_id": "mock-response", "delta": base64.b64encode(b"\x01\x00" * 480).decode()})
                await socket.send_json({"type": "response.done", "response": {"id": "mock-response", "status": "completed", "output": [], "usage": {"input_tokens": 10, "output_tokens": 5}}})
                if not self.mock_transcript_before_response:
                    await socket.send_json({"type": "conversation.item.input_audio_transcription.completed", "transcript": self.mock_first_transcript})

            async for message in socket:
                if message.type != aiohttp.WSMsgType.TEXT:
                    continue
                event = json.loads(message.data)
                self.cloud_events.append(event)
                if event["type"] == "session.update":
                    self.cloud_config = event
                    if "turn_detection" in event["session"]:
                        manual = event["session"]["turn_detection"] is None
                    await socket.send_json({"type": "session.updated", "session": event["session"]})
                elif event["type"] == "input_audio_buffer.append":
                    self.received_pcm = base64.b64decode(event["audio"])
                    if not manual and self.mock_auto_reply:
                        await reply()
                elif event["type"] == "input_audio_buffer.commit":
                    await socket.send_json({"type": "input_audio_buffer.committed", "item_id": "input-" + str(len(self.cloud_events))})
                elif event["type"] == "response.create":
                    await reply()
            return socket

        async def semantic_model(request):
            await request.json()
            plan = {"status": "none", "actions": [], "clarification": ""}
            return web.json_response({"choices": [{"message": {"content": json.dumps(plan)}}]})

        self.mock_app = web.Application()
        self.mock_app.router.add_get("/realtime", cloud)
        self.mock_app.router.add_post("/text/v1/chat/completions", semantic_model)
        self.mock = TestServer(self.mock_app)
        await self.mock.start_server()
        self.config = {"host": "127.0.0.1", "port": 0, "public_base_url": "http://127.0.0.1:8765", "device_token": TOKEN, "provisioning_token": "local-provisioning-token-0000000", "device_id": "test-device", "aliyun_api_key": "mock-key", "aliyun_workspace_id": "mock-space", "aliyun_model": "mock", "aliyun_voice": "Tina", "enable_search": False, "max_connections": 2, "session_seconds": 600, "idle_seconds": 10, "memory_backend": "pc_prototype", "home_entities": {}, "runtime_dir": self.temp.name}
        self.config["qwen_chat_base_url"] = str(self.mock.make_url("/text/v1"))
        self.config["deepseek_base_url"] = str(self.mock.make_url("/text/v1"))
        self.app = create_app(self.config, str(self.mock.make_url("/realtime")))
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        self.headers = {"Authorization": "Bearer " + TOKEN}

    async def asyncTearDown(self):
        try:
            await self.client.close()
            await self.mock.close()
            self.temp.cleanup()
        finally:
            # GatewayTests也被其他测试手工复用，不能只依赖unittest的doCleanups。
            self.network_guard.stop()
        self.assertEqual(self.blocked_hosts, [], "测试期间存在未隔离的外部连接尝试")

    async def test_external_network_guard_blocks_unmocked_cloud_requests(self):
        async with aiohttp.ClientSession() as session:
            with self.assertRaisesRegex(AssertionError, "禁止访问非本机"):
                await session.get("https://unmocked-cloud.invalid/", timeout=aiohttp.ClientTimeout(total=1))
        self.assertEqual(self.blocked_hosts, ["unmocked-cloud.invalid"])
        self.blocked_hosts.clear()

    async def event(self, socket, wanted, state=None):
        async with asyncio.timeout(5):
            while True:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.TEXT:
                    item = json.loads(message.data)
                    if item.get("type") == wanted and (state is None or item.get("payload", {}).get("state") == state):
                        return item
                elif message.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                    self.fail("Socket ended unexpectedly")

    async def native(self):
        socket = await self.client.ws_connect("/device/v1/", headers=self.headers)
        await socket.send_json({"type": "hello", "version": 1, "audio_params": {"format": "pcm_s16le", "sample_rate": 16000, "channels": 1}})
        hello = await self.event(socket, "hello")
        self.assertEqual(hello["audio_params"]["sample_rate"], 24000)
        await self.event(socket, "custom")
        return socket

    async def test_api_auth_tasks_memories_and_restart_persistence(self):
        response = await self.client.get("/api/tasks")
        self.assertEqual(response.status, 401)
        payload = {"title": "联系客户", "due_at": None, "idempotency_key": "first-task"}
        response = await self.client.post("/api/tasks", headers=self.headers, json=payload)
        self.assertEqual(response.status, 201)
        first = (await response.json())["item"]
        response = await self.client.post("/api/tasks", headers=self.headers, json=payload)
        self.assertEqual((await response.json())["item"]["id"], first["id"])
        response = await self.client.post("/api/tasks", headers=self.headers, json={**payload, "title": "不同内容"})
        self.assertEqual(response.status, 409)
        response = await self.client.post(f"/api/tasks/{first['id']}/complete", headers=self.headers)
        self.assertEqual((await response.json())["item"]["status"], "completed")
        response = await self.client.put("/api/memories/称呼", headers=self.headers, json={"value": "牛牛"})
        self.assertEqual(response.status, 200)
        response = await self.client.delete("/api/memories/称呼", headers=self.headers)
        self.assertTrue((await response.json())["deleted"])
        await self.client.close()
        self.app = create_app(self.config, str(self.mock.make_url("/realtime")))
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        response = await self.client.get("/api/memories", headers=self.headers)
        self.assertEqual((await response.json())["items"], [])
        response = await self.client.get("/api/tasks", headers=self.headers)
        self.assertEqual((await response.json())["items"][0]["status"], "completed")

    async def test_native_idle_opens_no_cloud_then_pcm_roundtrip(self):
        socket = await self.native()
        self.assertEqual(self.cloud_calls, 0)
        await socket.send_json({"type": "listen", "state": "start"})
        await self.event(socket, "custom")
        pcm = b"\x00\x01" * 320
        await socket.send_bytes(pcm)
        async with asyncio.timeout(5):
            while True:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    self.assertEqual(message.data, b"\x01\x00" * 480)
                    break
        self.assertEqual(self.received_pcm, pcm)
        self.assertTrue(any(tool["name"] == "create_task" for tool in self.cloud_config["session"]["tools"]))
        await socket.send_json({"type": "session_end"})
        await self.event(socket, "custom")
        self.assertFalse(socket.closed)
        await socket.close()

    async def test_native_realtime_duplicate_tool_scope_creates_one_task_then_finishes_audio(self):
        self.mock_first_transcript = "你好，小燕子，帮我创建一个无提醒待办。"
        call = {"type":"function_call", "call_id":"duplicate-create", "name":"create_task", "arguments":json.dumps({"title":"离线重复调用只创建一次", "due_at":None}, ensure_ascii=False)}
        self.mock_tool_response = [dict(call), dict(call)]
        socket = await self.native()
        try:
            await socket.send_json({"type":"listen", "state":"start", "mode":"auto"})
            await self.event(socket, "custom")
            await socket.send_bytes(b"\x00\x01" * 320)
            audio = []
            stopped = False
            async with asyncio.timeout(5):
                while not stopped:
                    message = await socket.receive()
                    if message.type == aiohttp.WSMsgType.BINARY:
                        audio.append(message.data)
                    elif message.type == aiohttp.WSMsgType.TEXT:
                        event = json.loads(message.data)
                        stopped = event.get("type") == "tts" and event.get("state") == "stop"
                    elif message.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                        self.fail("工具续答音频完成前连接已结束")
            rows = self.app[RUNTIME_KEY].store.list_tasks()["tasks"]
            self.assertEqual(len(rows), 1)
            self.assertEqual(rows[0]["title"], "离线重复调用只创建一次")
            self.assertEqual(rows[0]["status"], "pending")
            outputs = [event["item"] for event in self.cloud_events if event.get("type") == "conversation.item.create" and event.get("item", {}).get("type") == "function_call_output"]
            self.assertEqual(len(outputs), 1)
            self.assertEqual(outputs[0]["call_id"], "duplicate-create")
            self.assertEqual(json.loads(outputs[0]["output"])["task_id"], rows[0]["task_id"])
            self.assertEqual(sum(event.get("type") == "response.create" for event in self.cloud_events), 1)
            self.assertEqual(audio, [b"\x01\x00" * 480])
        finally:
            await socket.close()

    async def test_wake_gate_drops_early_audio_then_rewakes_on_same_socket(self):
        self.mock_first_transcript = "你就这样"
        self.mock_transcript_before_response = False
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom")
        await socket.send_bytes(b"\x00\x01" * 320)
        saw_binary = False
        saw_idle = False
        async with asyncio.timeout(5):
            while not saw_idle:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    saw_binary = True
                elif message.type == aiohttp.WSMsgType.TEXT:
                    event = json.loads(message.data)
                    if event.get("type") == "custom" and event.get("payload", {}).get("state") == "idle":
                        saw_idle = True
        self.assertFalse(saw_binary)
        self.assertFalse(socket.closed)
        status=await self.client.get('/api/status',headers=self.headers)
        self.assertEqual(status.status,200)
        self.assertEqual((await status.json())['device']['state'],'idle')

        self.mock_first_transcript = "你好，小燕子"
        self.mock_transcript_before_response = True
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom")
        await socket.send_bytes(b"\x00\x01" * 320)
        async with asyncio.timeout(5):
            while True:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    self.assertEqual(message.data, b"\x01\x00" * 480)
                    break
        await socket.close()

    async def test_native_pcm_without_listen_does_not_start_cloud(self):
        socket = await self.native()
        await socket.send_bytes(b"\x00\x01" * 320)
        await asyncio.sleep(0.2)
        self.assertEqual(self.cloud_calls, 0)
        self.assertFalse(socket.closed)
        await socket.close()

    async def test_authenticated_native_reconnect_replaces_stale_connection(self):
        first = await self.native()
        second = await self.native()
        self.assertEqual(len(self.app[RUNTIME_KEY].native_devices), 1)
        self.assertEqual(self.cloud_calls, 0)
        async with asyncio.timeout(4):
            message = await first.receive()
        self.assertIn(message.type, (aiohttp.WSMsgType.CLOSE, aiohttp.WSMsgType.CLOSED))
        await second.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(second, "custom", state="listening")
        self.assertEqual(self.cloud_calls, 1)
        self.assertFalse(second.closed)
        await first.close()
        await second.close()

    async def test_native_auto_silence_returns_to_standby_and_rewakes(self):
        self.config["idle_seconds"] = 0.5
        self.mock_auto_reply = False
        socket = await self.native()
        self.assertEqual(self.cloud_calls, 0)
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom", state="listening")
        self.assertEqual(self.cloud_config["session"]["turn_detection"]["type"], "server_vad")
        self.assertIn("你叫小燕子", self.cloud_config["session"]["instructions"])
        # 持续上传静音也应到期，不能把PCM包本身算成有效说话。
        for _ in range(35):
            await socket.send_bytes(b"\x00\x00" * 320)
            await asyncio.sleep(0.04)
        async with asyncio.timeout(4):
            while True:
                event = await self.event(socket, "custom")
                if event["payload"]["state"] == "idle":
                    break
        self.assertFalse(socket.closed)
        async with asyncio.timeout(3):
            while self.app[RUNTIME_KEY].voice_sessions:
                await asyncio.sleep(0.02)
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        async with asyncio.timeout(4):
            while self.cloud_calls < 2:
                await asyncio.sleep(0.02)
        self.assertFalse(socket.closed)
        await socket.close()

    async def test_audio_playback_activity_defers_idle_timeout(self):
        self.config["idle_seconds"] = 0.5
        self.audio_chunk_count = 80
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom")
        await socket.send_bytes(b"\x01\x00" * 320)
        frames = 0
        async with asyncio.timeout(6):
            while frames < 80:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    frames += 1
                elif message.type == aiohttp.WSMsgType.TEXT:
                    event = json.loads(message.data)
                    if event.get("type") == "custom":
                        self.assertNotEqual(event["payload"]["state"], "idle")
        self.assertEqual(frames, 80)
        await socket.close()

    async def test_reminder_stays_pending_until_explicit_ack_without_voice(self):
        socket = await self.native()
        due = (datetime.now(timezone.utc) - timedelta(seconds=1)).isoformat()
        response = await self.client.post("/api/tasks", headers=self.headers, json={"title": "喝水", "due_at": due, "idempotency_key": "reminder"})
        task = (await response.json())["item"]
        notification = (await self.event(socket, "notification"))["notification"]
        self.assertEqual(notification["id"], task["id"])
        self.assertEqual(self.cloud_calls, 0)
        response = await self.client.get("/api/status", headers=self.headers)
        self.assertEqual(len((await response.json())["notifications"]), 1)
        await socket.send_json({"type": "notification_ack", "id": notification["id"], "lease_token": notification["lease_token"]})
        for _ in range(20):
            if not self.app[RUNTIME_KEY].notifications:
                break
            await asyncio.sleep(0.01)
        self.assertFalse(self.app[RUNTIME_KEY].notifications)
        result = self.app[RUNTIME_KEY].store.list_tasks()["tasks"][0]
        self.assertEqual(result["status"], "pending")
        self.assertEqual(result["reminder_state"], "acked")
        await socket.close()

    async def test_native_bad_ack_does_not_disconnect_and_snapshot_is_bounded(self):
        socket = await self.native()
        await socket.send_json({"type": "notification_ack", "id": [], "lease_token": "bad"})
        alert = await self.event(socket, "alert")
        self.assertEqual(alert["status"], "请求未完成")
        for index in range(10):
            self.app[RUNTIME_KEY].store.remember_fact(f"偏好{index}", "中文" * 1000)
        await socket.send_json({"type": "memory_request", "request_id": "boot"})
        snapshot = await self.event(socket, "memory_snapshot")
        self.assertLess(len(json.dumps(snapshot, ensure_ascii=False).encode("utf-8")), 6144)
        self.assertTrue(snapshot["partial"])
        self.assertFalse(socket.closed)
        self.assertEqual(self.cloud_calls, 0)
        await socket.close()

    async def test_native_button_manual_stop_commits_and_requests_reply(self):
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start", "mode": "manual"})
        await self.event(socket, "custom")
        await socket.send_bytes(b"\x00\x01" * 4800)
        await socket.send_json({"type": "listen", "state": "stop"})
        async with asyncio.timeout(5):
            while (await socket.receive()).type != aiohttp.WSMsgType.BINARY:
                pass
        kinds = [item["type"] for item in self.cloud_events]
        self.assertIn("input_audio_buffer.commit", kinds)
        self.assertEqual(kinds.count("response.create"), 1)
        await socket.close()

    async def test_manual_empty_short_duplicate_stop_and_next_turn_are_isolated(self):
        socket = await self.native()
        for manual_id, pcm in (("empty", b""), ("short", b"\0\1" * 320)):
            await socket.send_json({"type": "listen", "state": "start", "mode": "manual", "manual_id": manual_id})
            if pcm:
                await socket.send_bytes(pcm)
            await socket.send_json({"type": "listen", "state": "stop", "manual_id": manual_id, "audio_bytes": len(pcm)})
            self.assertEqual((await self.event(socket, "alert"))["status"], "请重新录音")
            await socket.send_json({"type": "listen", "state": "stop", "manual_id": manual_id, "audio_bytes": len(pcm)})
        self.assertFalse(any(item["type"] == "input_audio_buffer.commit" for item in self.cloud_events))
        await socket.send_json({"type": "listen", "state": "start", "mode": "manual", "manual_id": "valid"})
        pcm = b"\2\0" * 4800
        await socket.send_bytes(pcm)
        await socket.send_json({"type": "listen", "state": "stop", "manual_id": "short", "audio_bytes": 640})
        await socket.send_json({"type": "listen", "state": "stop", "manual_id": "valid", "audio_bytes": len(pcm)})
        await socket.send_json({"type": "listen", "state": "stop", "manual_id": "valid", "audio_bytes": len(pcm)})
        await self.event(socket, "tts")
        kinds = [item["type"] for item in self.cloud_events]
        self.assertIsNone(self.cloud_events[0]["session"]["turn_detection"])
        self.assertEqual(kinds.count("input_audio_buffer.commit"), 1)
        self.assertEqual(kinds.count("response.create"), 1)
        self.assertEqual(kinds.count("input_audio_buffer.clear"), 2)
        self.assertEqual(self.received_pcm, pcm)
        await socket.close()

    async def test_pcm_pacing_uses_sample_clock_without_per_packet_drift(self):
        self.audio_chunk_count = 100
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start"})
        await self.event(socket, "custom")
        await socket.send_bytes(b"\x00\x01" * 320)
        first = None
        last = None
        received = 0
        clock = asyncio.get_running_loop()
        async with asyncio.timeout(6):
            while received < 96000:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    first = clock.time() if first is None else first
                    last = clock.time()
                    received += len(message.data)
        self.assertEqual(received, 96000)
        # 2秒PCM允许提前80ms填缓冲；不应逐包积累Windows sleep超时误差。
        self.assertGreater(last - first, 1.7)
        self.assertLess(last - first, 2.3)
        await socket.close()

    async def test_home_write_occurs_once_and_waits_for_reported_state(self):
        writes = []
        reads = []

        async def write(request):
            writes.append(await request.json())
            return web.json_response([])

        async def read(request):
            reads.append(True)
            return web.json_response({"state": "off" if len(reads) == 1 else "on"})

        app = web.Application()
        app.router.add_post("/api/services/light/turn_on", write)
        app.router.add_get("/api/states/light.desk", read)
        register_home_api(app, lambda: [{"entity_id": "light.desk", "state": "off", "attributes": {}}], entity_ids=("light.desk",), domain_services={"light": ("turn_on", "turn_off")})
        server = TestServer(app)
        await server.start_server()
        try:
            async with aiohttp.ClientSession() as client:
                controller = HomeController({"home_assistant_url": str(server.make_url("")), "home_assistant_token": "mock", "home_entities": {"桌灯": "light.desk"}, "runtime_dir": self.temp.name}, client)
                result = await controller.set("桌灯", "on")
                self.assertEqual(len(writes), 1)
                self.assertEqual(len(reads), 2)
                self.assertTrue(result["target_observed"])
                with self.assertRaises(ValueError):
                    await controller.set("不存在的设备", "on")
        finally:
            await server.close()

    async def test_voice_selection_is_authenticated_persistent_and_next_session(self):
        response = await self.client.get("/api/voices")
        self.assertEqual(response.status, 401)
        response = await self.client.get("/api/voices", headers=self.headers)
        choices = await response.json()
        self.assertIn("Serena", {voice["id"] for voice in choices["items"]})
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start"})
        await self.event(socket, "custom", state="listening")
        self.assertEqual(self.cloud_config["session"]["audio"]["output"]["voice"], "Tina")
        response = await self.client.put("/api/voice", headers=self.headers, json={"voice": "Serena"})
        saved = await response.json()
        self.assertEqual(saved["current"], "Serena")
        self.assertEqual(saved["active_voice"], "Tina")
        response = await self.client.put("/api/voice", headers=self.headers, json={"voice": "unknown"})
        self.assertEqual(response.status, 400)
        preference = json.loads((Path(self.temp.name) / "voice-preference.json").read_text())
        self.assertEqual(preference, {"voice": "Serena"})
        await socket.close()
        await self.client.close()
        config = {**self.config, "aliyun_voice": "Tina"}
        self.app = create_app(config, str(self.mock.make_url("/realtime")))
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start"})
        await self.event(socket, "custom", state="listening")
        self.assertEqual(self.cloud_config["session"]["audio"]["output"]["voice"], "Serena")
        await socket.close()


if __name__ == "__main__":
    unittest.main()

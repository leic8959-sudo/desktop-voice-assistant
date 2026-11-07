"""chat 供应商管道端到端本地测试：mock DashScope ASR/TTS 与 chat SSE，不接真实云端。"""

import asyncio
import base64
import json
import tempfile
import unittest
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

TOKEN = "local-chat-pipeline-token-000000"
TRANSCRIPT = "你好小燕子帮我创建提醒"
TTS_FRAME = b"\x02\x00" * 480


def sse(body_parts):
    lines = []
    for part in body_parts:
        lines.append("data: " + json.dumps(part, ensure_ascii=False))
    lines.append("data: [DONE]")
    return "\n\n".join(lines) + "\n\n"


def text_stream(text_pieces, usage):
    parts = [{"choices": [{"index": 0, "delta": {"role": "assistant", "content": ""}}]}]
    for piece in text_pieces:
        parts.append({"choices": [{"index": 0, "delta": {"content": piece}}]})
    parts.append({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop", "usage": usage}]})
    return sse(parts)


def tool_stream(call_id, name, argument_pieces, usage):
    parts = [{"choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [{"index": 0, "id": call_id, "type": "function", "function": {"name": name, "arguments": ""}}]}}]}]
    for piece in argument_pieces:
        parts.append({"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "function": {"arguments": piece}}]}}]})
    parts.append({"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls", "usage": usage}]})
    return sse(parts)


class ChatPipelineTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.chat_requests = []
        self.memory_requests = []
        self.asr_tasks = 0
        self.tts_sentences = []
        self.asr_frames_before_result = 3
        self.transcripts = [TRANSCRIPT]
        self.responses = ["text"]
        self.hold_reply = None

        async def inference(request):
            socket = web.WebSocketResponse()
            await socket.prepare(request)
            task_kind = None
            frame_count = 0

            async for message in socket:
                if message.type == aiohttp.WSMsgType.TEXT:
                    event = json.loads(message.data)
                    action = event.get("header", {}).get("action")
                    payload = event.get("payload", {})
                    if action == "run-task":
                        task_kind = payload.get("task")
                        self.asr_tasks += 1 if task_kind == "asr" else 0
                        frame_count = 0
                        await socket.send_json({"header": {"task_id": event["header"]["task_id"], "event": "task-started"}, "payload": {}})
                    elif action == "continue-task" and task_kind == "tts":
                        text = payload.get("input", {}).get("text", "")
                        self.tts_sentences.append(text)
                        for _ in range(2):
                            await socket.send_bytes(TTS_FRAME)
                    elif action == "finish-task" and task_kind == "tts":
                        await socket.send_json({"header": {"task_id": event["header"]["task_id"], "event": "task-finished"}, "payload": {"usage": {"characters": 10}}})
                elif message.type == aiohttp.WSMsgType.BINARY:
                    frame_count += 1
                    if task_kind == "asr" and frame_count >= self.asr_frames_before_result and self.transcripts:
                        await socket.send_json({"header": {"task_id": "t", "event": "result-generated"}, "payload": {"output": {"sentence": {"text": self.transcripts.pop(0), "sentence_end": True}}}})
            return socket

        async def chat(request):
            body = await request.json()
            if not body.get("tools") and body.get("response_format") == {"type": "json_object"}:
                self.memory_requests.append(body)
                return web.json_response({"choices": [{"message": {"content": '{"memories":[]}'}}]})
            self.chat_requests.append(body)
            mode = self.responses.pop(0)
            if mode == "reject":
                return web.json_response({"error": {"code": "invalid_parameter", "message": "tools暂时无法与stream同时使用"}}, status=400)
            if mode == "tool":
                payload = sse([
                    {"choices": [{"index": 0, "delta": {"role": "assistant", "tool_calls": [{"index": 0, "id": "call-1", "type": "function", "function": {"name": "create_task", "arguments": ""}}]}}]},
                    {"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "function": {"arguments": "{\"title\":"}}]}}]},
                    {"choices": [{"index": 0, "delta": {"tool_calls": [{"index": 0, "function": {"arguments": "\"倒垃圾\"}"}}]}}]},
                    {"choices": [{"index": 0, "delta": {}, "finish_reason": "tool_calls", "usage": {"prompt_tokens": 9, "completion_tokens": 4}}]},
                ])
                return web.Response(text=payload, content_type="text/event-stream")
            if not body.get("stream"):
                return web.json_response({"choices": [{"index": 0, "message": {"role": "assistant", "content": "好的，已创建提醒。请记得完成。"}, "finish_reason": "stop"}], "usage": {"prompt_tokens": 12, "completion_tokens": 8}})
            payload = text_stream(["好的，已创建提醒。", "请记得完成。"], {"prompt_tokens": 12, "completion_tokens": 8})
            if self.hold_reply is None:
                return web.Response(text=payload, content_type="text/event-stream")
            # 打断测试：先给首块内容让回合进入 thinking，再按闸门保持流打开，保证 abort 有可取消的云端回合。
            parts = [{"choices": [{"index": 0, "delta": {"role": "assistant", "content": ""}}]}, {"choices": [{"index": 0, "delta": {"content": "好的，已创建提醒。"}}]}, {"choices": [{"index": 0, "delta": {"content": "请记得完成。"}}]}, {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop", "usage": {"prompt_tokens": 12, "completion_tokens": 8}}]}]
            response = web.StreamResponse()
            response.headers["Content-Type"] = "text/event-stream"
            await response.prepare(request)
            try:
                await response.write(sse(parts[:2]).encode())
                await self.hold_reply.wait()
                await response.write(sse(parts[2:]).encode())
                await response.write_eof()
            except Exception:
                pass
            return response

        async def failing_realtime(request):
            socket = web.WebSocketResponse()
            await socket.prepare(request)
            await socket.send_json({"type": "error", "error": {"code": "quota_exceeded", "message": "配额不足"}})
            await socket.close()
            return socket

        self.mock_app = web.Application()
        self.mock_app.router.add_get("/inference", inference)
        self.mock_app.router.add_post("/v1/chat/completions", chat)
        self.mock_app.router.add_get("/realtime-fail", failing_realtime)
        self.mock = TestServer(self.mock_app)
        await self.mock.start_server()
        self.config = {"host": "127.0.0.1", "port": 0, "public_base_url": "http://127.0.0.1:8765", "device_token": TOKEN, "provisioning_token": "local-provisioning-token-0000000", "device_id": "test-device", "aliyun_api_key": "mock-aliyun-key", "aliyun_workspace_id": "mock-space", "aliyun_model": "mock", "aliyun_voice": "Tina", "llm_provider": "qwen_chat", "qwen_chat_base_url": str(self.mock.make_url("/v1")), "dashscope_ws_url": str(self.mock.make_url("/inference")), "chat_turn_silence_ms": 250, "enable_search": False, "max_connections": 2, "session_seconds": 600, "idle_seconds": 30, "memory_backend": "pc_prototype", "home_entities": {}, "runtime_dir": self.temp.name}
        await self.start_app()

    async def start_app(self, realtime_url=None):
        self.app = create_app(self.config, realtime_url)
        self.client = TestClient(TestServer(self.app))
        await self.client.start_server()

    async def asyncTearDown(self):
        await self.client.close()
        await self.mock.close()
        self.temp.cleanup()

    async def event(self, socket, wanted, timeout=8):
        async with asyncio.timeout(timeout):
            while True:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.TEXT:
                    item = json.loads(message.data)
                    if item.get("type") == wanted:
                        return item
                elif message.type in (aiohttp.WSMsgType.CLOSED, aiohttp.WSMsgType.ERROR):
                    self.fail("Socket ended unexpectedly")

    async def native(self):
        socket = await self.client.ws_connect("/device/v1/", headers={"Authorization": "Bearer " + TOKEN})
        await socket.send_json({"type": "hello", "version": 1, "audio_params": {"format": "pcm_s16le", "sample_rate": 16000, "channels": 1}})
        hello = await self.event(socket, "hello")
        self.assertEqual(hello["audio_params"]["sample_rate"], 24000)
        await self.event(socket, "custom")
        return socket

    async def speak_and_expect_reply(self, socket):
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom")
        stt = None
        for _ in range(4):
            await socket.send_bytes(b"\x00\x01" * 320)
        async with asyncio.timeout(8):
            while stt is None:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.TEXT:
                    item = json.loads(message.data)
                    if item.get("type") == "stt":
                        stt = item
        audio_frames = 0
        stop_received = False
        async with asyncio.timeout(8):
            while not stop_received:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.BINARY:
                    audio_frames += 1
                elif message.type == aiohttp.WSMsgType.TEXT:
                    item = json.loads(message.data)
                    if item.get("type") == "tts" and item.get("state") == "stop":
                        stop_received = True
        return stt, audio_frames

    async def test_voice_turn_end_to_end_via_chat_provider(self):
        socket = await self.native()
        stt, audio_frames = await self.speak_and_expect_reply(socket)
        self.assertEqual(stt["text"], TRANSCRIPT)
        self.assertGreaterEqual(audio_frames, 1)
        self.assertEqual(len(self.chat_requests), 1)
        body = self.chat_requests[0]
        self.assertEqual(body["model"], "qwen3.8-flash")
        self.assertTrue(body["stream"])
        self.assertTrue(any(tool["function"]["name"] == "create_task" for tool in body["tools"]))
        self.assertEqual(body["messages"][0]["role"], "system")
        self.assertIn("你叫小燕子", body["messages"][0]["content"])
        self.assertEqual(body["messages"][-1], {"role": "user", "content": TRANSCRIPT})
        # ASR run-task 恰好一次；TTS 按句合成。
        self.assertEqual(self.asr_tasks, 1)
        self.assertEqual(self.tts_sentences, ["好的，已创建提醒。", "请记得完成。"])
        status = await self.client.get("/api/status", headers={"Authorization": "Bearer " + TOKEN})
        self.assertEqual((await status.json())["bridge"]["llm_provider"], "qwen_chat")
        await socket.send_json({"type": "session_end"})

    async def test_tool_call_round_trip_via_chat_provider(self):
        self.responses = ["tool", "text"]
        socket = await self.native()
        stt, audio_frames = await self.speak_and_expect_reply(socket)
        self.assertEqual(stt["text"], TRANSCRIPT)
        self.assertGreaterEqual(len(self.chat_requests), 2)
        follow_up = self.chat_requests[1]
        tool_messages = [item for item in follow_up["messages"] if item.get("role") == "tool"]
        self.assertEqual(len(tool_messages), 1)
        self.assertEqual(tool_messages[0]["tool_call_id"], "call-1")
        self.assertIn("ok", tool_messages[0]["content"])
        tasks = self.app[RUNTIME_KEY].store.list_tasks()["tasks"]
        self.assertEqual(tasks[0]["title"], "倒垃圾")
        self.assertEqual(tasks[0]["status"], "pending")
        self.assertEqual(len(self.tts_sentences), 2)
        await socket.send_json({"type": "session_end"})

    async def test_verified_first_sentence_extracts_memory_without_consuming_chat_script(self):
        socket = await self.native()
        stt, frames = await self.speak_and_expect_reply(socket)
        await asyncio.wait_for(self.app[RUNTIME_KEY].memory_service.queue.join(), 3)
        self.assertEqual(stt["text"], TRANSCRIPT)
        self.assertGreaterEqual(frames, 1)
        self.assertEqual(len(self.chat_requests), 1)
        self.assertEqual(len(self.memory_requests), 1)
        extraction = self.memory_requests[0]
        self.assertFalse(extraction.get("tools"))
        self.assertFalse(extraction["stream"])
        self.assertEqual(json.loads(extraction["messages"][-1]["content"])["utterance"], TRANSCRIPT)
        self.assertEqual(self.responses, [])
        await socket.send_json({"type": "session_end"})

    async def test_stream_rejection_falls_back_to_blocking_request(self):
        self.responses = ["reject", "text"]
        socket = await self.native()
        stt, audio_frames = await self.speak_and_expect_reply(socket)
        self.assertEqual(stt["text"], TRANSCRIPT)
        self.assertGreaterEqual(len(self.chat_requests), 2)
        self.assertFalse(self.chat_requests[1]["stream"])
        self.assertGreaterEqual(audio_frames, 1)
        await socket.send_json({"type": "session_end"})

    async def test_realtime_failure_falls_back_to_chat_provider(self):
        await self.client.close()
        self.config["llm_provider"] = "qwen_realtime"
        await self.start_app(str(self.mock.make_url("/realtime-fail")))
        socket = await self.native()
        stt, _ = await self.speak_and_expect_reply(socket)
        self.assertEqual(stt["text"], TRANSCRIPT)
        self.assertEqual(self.app[RUNTIME_KEY].last_provider, "qwen_chat")
        await socket.send_json({"type": "session_end"})

    async def test_llm_api_switch_persists_across_restart(self):
        headers = {"Authorization": "Bearer " + TOKEN}
        response = await self.client.get("/api/llm", headers=headers)
        payload = await response.json()
        self.assertEqual(payload["current"], "qwen_chat")
        self.assertTrue(any(item["id"] == "deepseek" and not item["available"] for item in payload["items"]))
        self.config["deepseek_api_key"] = "sk-deepseek-key-0001"
        response = await self.client.put("/api/llm", headers=headers, json={"provider": "deepseek"})
        self.assertEqual((await response.json())["current"], "deepseek")
        response = await self.client.put("/api/llm", headers=headers, json={"provider": "gpt"})
        self.assertEqual(response.status, 400)
        await self.client.close()
        await self.start_app()
        response = await self.client.get("/api/llm", headers=headers)
        self.assertEqual((await response.json())["current"], "deepseek")

    async def test_barge_in_cancels_active_chat_turn(self):
        self.hold_reply = asyncio.Event()
        socket = await self.native()
        await socket.send_json({"type": "listen", "state": "start", "mode": "auto"})
        await self.event(socket, "custom")
        for _ in range(4):
            await socket.send_bytes(b"\x00\x01" * 320)
        await self.event(socket, "stt")
        # 回合在静音计时后开始；等首块内容把状态推进到 thinking，abort 才有可取消的云端回合。
        thinking = False
        async with asyncio.timeout(8):
            while not thinking:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.TEXT:
                    item = json.loads(message.data)
                    thinking = item.get("type") == "custom" and item.get("payload", {}).get("state") == "thinking"
        # abort（设备按钮打断）必须取消云端回合并回到 listening。
        await socket.send_json({"type": "abort"})
        try:
            custom = await self.event(socket, "custom")
            self.assertEqual(custom["payload"]["state"], "listening")
            self.assertFalse(socket.closed)
        finally:
            self.hold_reply.set()
        await socket.send_json({"type": "session_end"})
        await self.event(socket, "custom")


if __name__ == "__main__":
    unittest.main()

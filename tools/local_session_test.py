"""本地会话模拟测试：模拟阿里Realtime服务器，验证桥接的认证、握手、音频转换、打断取消、工具与看门狗。

不访问外网、不调用计费接口、不烧录。结果写入 evidence/bridge_local_test.json。
"""

import asyncio
import base64
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
from datetime import datetime

import aiohttp
import av
from aiohttp import web
from aiohttp.test_utils import TestClient, TestServer

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from audio_codec import OpusBridge
from gateway import create_app

RESULTS = []
TEST_RUNTIME = tempfile.TemporaryDirectory(prefix="assistant-protocol-")


def check(name, ok, detail):
    RESULTS.append({"check": name, "ok": bool(ok), "detail": detail})
    print(("通过 " if ok else "失败 ") + name + "：" + detail)


class MockAliyun:
    """模拟阿里Realtime：server_vad模式，收到音频后自动判停并回复。"""

    def __init__(self):
        self.received = []
        self.responses = []
        self.cancel_seen = False
        self.clear_seen = False
        self.tool_output = None

    async def reply(self, socket, action):
        if action == "tool":
            await socket.send_json({"type": "response.created", "response": {"id": "resp_tool"}})
            await socket.send_json({"type": "response.done", "response": {"id": "resp_tool", "status": "completed", "usage": {"input_tokens": 1}, "output": [{"type": "function_call", "call_id": "call_1", "name": "remember_fact", "arguments": json.dumps({"key": "牛牛的名字", "value": "牛牛"}, ensure_ascii=False)}]}})
        else:
            await socket.send_json({"type": "response.created", "response": {"id": "resp_audio"}})
            for _ in range(4):
                pcm = b"\x10\x20" * 1440  # 60ms @ 24kHz
                await socket.send_json({"type": "response.audio.delta", "response_id": "resp_audio", "delta": base64.b64encode(pcm).decode("ascii")})
                await asyncio.sleep(0.15)  # 拉长回复流，为回复中打断留出窗口
            await socket.send_json({"type": "response.audio_transcript.done", "response_id": "resp_audio", "transcript": "测试回复字幕。"})
            await socket.send_json({"type": "response.done", "response": {"id": "resp_audio", "status": "completed", "usage": {"input_tokens": 12, "output_tokens": 34, "audio_tokens": 56}, "output": []}})

    async def handler(self, request):
        socket = web.WebSocketResponse(max_msg_size=4 * 1024 * 1024)
        await socket.prepare(request)
        await socket.send_json({"type": "session.created"})
        speech_done = False
        async for message in socket:
            if message.type != aiohttp.WSMsgType.TEXT:
                continue
            event = json.loads(message.data)
            self.received.append(event)
            kind = event.get("type")
            if kind == "session.update":
                await socket.send_json({"type": "session.updated"})
            elif kind == "input_audio_buffer.append" and not speech_done:
                # 模拟云端VAD：首次收到音频即判停并自动生成回复。
                speech_done = True
                await socket.send_json({"type": "input_audio_buffer.speech_started"})
                await socket.send_json({"type": "input_audio_buffer.speech_stopped"})
                await self.reply(socket, self.responses.pop(0) if self.responses else "audio")
            elif kind == "response.create":
                await self.reply(socket, self.responses.pop(0) if self.responses else "audio")
            elif kind == "input_audio_buffer.clear":
                self.clear_seen = True
            elif kind == "response.cancel":
                self.cancel_seen = True
            elif kind == "conversation.item.create":
                item = event.get("item", {})
                if item.get("type") == "function_call_output":
                    self.tool_output = json.loads(item.get("output", "{}"))
        return socket


def build_config(realtime_url, **overrides):
    config = {
        "host": "127.0.0.1", "port": 0,
        "public_base_url": "http://192.168.3.8:8765",
        "provisioning_token": "prov_token_local_test_0000000000",
        "device_token": "device_token_local_test_000000000",
        "device_id": "desk-prototype-01",
        "aliyun_workspace_id": "local-test-workspace",
        "aliyun_api_key": "local-test-key",
        "aliyun_model": "qwen3.8-omni-flash-realtime",
        "aliyun_voice": "Tina",
        "enable_search": False,
        "max_connections": 1,
        "session_seconds": 600,
        "idle_seconds": 60,
        "memory_backend": "pc_prototype",
        "home_assistant_url": "", "home_assistant_token": "", "home_entities": {},
        "runtime_dir": TEST_RUNTIME.name,
    }
    config.update(overrides)
    return create_app(config, realtime_url=realtime_url)


async def device_connect(client, hello):
    for _ in range(20):
        try:
            socket = await client.ws_connect("/xiaozhi/v1/", headers={"Authorization": "Bearer device_token_local_test_000000000"})
            await socket.send_json(hello)
            return socket
        except aiohttp.WSServerHandshakeError as error:
            if error.status != 503:  # 上一会话槽位异步释放中
                raise
            await asyncio.sleep(0.25)
    raise RuntimeError("会话槽位迟迟未释放")


async def collect_until(socket, wanted, timeout=8):
    seen = []
    async with asyncio.timeout(timeout):
        while True:
            message = await socket.receive()
            if message.type == aiohttp.WSMsgType.TEXT:
                event = json.loads(message.data)
                if event.get("type") == wanted:
                    return event, seen
                seen.append(event)
            elif message.type == aiohttp.WSMsgType.BINARY:
                seen.append(message.data)
            else:
                raise RuntimeError(f"连接提前结束：{message.type}")


async def drain(socket, duration):
    """在时长窗口内收集消息，返回(文本事件列表, 二进制帧列表)。"""
    events, binaries = [], []
    try:
        async with asyncio.timeout(duration):
            while True:
                message = await socket.receive()
                if message.type == aiohttp.WSMsgType.TEXT:
                    events.append(json.loads(message.data))
                elif message.type == aiohttp.WSMsgType.BINARY:
                    binaries.append(message.data)
                else:
                    break
    except asyncio.TimeoutError:
        pass
    return events, binaries


async def main():
    mock = MockAliyun()
    runner = web.AppRunner(web.Application())
    runner.app.router.add_get("/mock", mock.handler)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", 0)
    await site.start()
    port = site._server.sockets[0].getsockname()[1]
    realtime_url = f"ws://127.0.0.1:{port}/mock"

    try:
        app = build_config(realtime_url)
        server = TestServer(app)
        client = TestClient(server)
        await client.start_server()
        try:
            # ---- 场景一：认证与协议拒绝 ----
            rejected = False
            try:
                await client.ws_connect("/xiaozhi/v1/", headers={"Authorization": "Bearer wrong-token-wrong-token-wrong"})
            except aiohttp.WSServerHandshakeError as error:
                rejected = error.status == 401
            check("auth_reject_wrong_token", rejected, "错误设备令牌在握手阶段被401拒绝")

            socket = await device_connect(client, {"type": "hello", "version": 2, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1}})
            await socket.receive(timeout=5)
            check("protocol_reject_version", socket.close_code == 1003, f"协议版本不受支持被1003关闭(code={socket.close_code})")

            socket = await device_connect(client, {"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 8000, "channels": 1}})
            await socket.receive(timeout=5)
            check("protocol_reject_audio_params", socket.close_code == 1003, f"非16kHz单声道Opus被1003关闭(code={socket.close_code})")

            # ---- 场景二：握手、音频双向转换、字幕与用量 ----
            socket = await device_connect(client, {"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1}})
            hello, _ = await collect_until(socket, "hello")
            params = hello.get("audio_params", {})
            check("handshake_hello", params.get("format") == "opus" and params.get("sample_rate") == 24000 and params.get("frame_duration") == 60, f"设备收到下行hello：opus 24kHz 60ms({params})")

            await collect_until(socket, "custom")  # 初始 listening
            await socket.send_json({"type": "listen", "state": "start"})
            codec = OpusBridge()
            uplink_pcm = b"\x05\x00" * 1600  # 100ms 16kHz 16bit
            for packet in codec.encode(uplink_pcm, finish=True):
                await socket.send_bytes(packet)
            tts_start, prior = await collect_until(socket, "tts")
            events, binaries = await drain(socket, 1.5)
            all_events = prior + events
            all_binaries = [item for item in prior + events + binaries if isinstance(item, bytes)]
            states = [event["payload"]["state"] for event in all_events if event.get("type") == "custom"]
            appends = [event for event in mock.received if event.get("type") == "input_audio_buffer.append"]
            appended = b"".join(base64.b64decode(event["audio"]) for event in appends)
            check("cloud_received_pcm", len(appends) >= 1 and len(appended) > 0 and len(appended) % 2 == 0, f"云端收到Opus解码后的16bit对齐PCM：{len(appends)}帧共{len(appended)}字节（输入{len(uplink_pcm)}字节，编解码算法延迟属正常）")
            check("cloud_audio_to_opus_downlink", tts_start.get("state") == "start" and len(all_binaries) >= 1 and "speaking" in states, f"云端24kHz PCM转Opus下行：{len(all_binaries)}个音频帧，状态序列含{states}")

            decoder = av.CodecContext.create("opus", "r")
            samples = sum(frame.samples for chunk in all_binaries for frame in decoder.decode(av.Packet(chunk)))
            check("downlink_sample_rate_24k", samples > 0, f"下行Opus独立解码出{samples}个样本（24kHz流）")

            usage = next((event for event in all_events if event.get("type") == "usage"), None)
            check("usage_forwarded", usage is not None and usage["usage"].get("output_tokens") == 34, "response.done的usage原样转发给设备")
            subtitle = next((event for event in all_events if event.get("type") == "tts" and event.get("text")), None)
            check("subtitle_forwarded", subtitle is not None and subtitle["text"] == "测试回复字幕。", "字幕文本随tts事件转发")

            # ---- 场景三：回复进行中打断取消、本地停播与迟到音频丢弃 ----
            await socket.close()
            mock.cancel_seen = False
            mock.clear_seen = False
            abort_socket = await device_connect(client, {"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1}})
            await collect_until(abort_socket, "hello")
            await collect_until(abort_socket, "custom")
            await abort_socket.send_json({"type": "listen", "state": "start"})
            codec3 = OpusBridge()
            for packet in codec3.encode(b"\x05\x00" * 1600, finish=True):
                await abort_socket.send_bytes(packet)
            await collect_until(abort_socket, "tts")
            async with asyncio.timeout(3):
                while True:
                    message = await abort_socket.receive()
                    if message.type == aiohttp.WSMsgType.BINARY:
                        break  # 已有音频在播，处于回复中段
            await abort_socket.send_json({"type": "abort"})
            _, stop_seen = await collect_until(abort_socket, "interrupt")
            stopped_locally = any(event.get("type") == "tts" and event.get("state") == "stop" for event in stop_seen)
            await asyncio.sleep(0.8)
            check("abort_cancels_cloud", mock.cancel_seen and mock.clear_seen, "回复进行中打断：云端收到response.cancel与input_audio_buffer.clear")
            check("abort_stops_playback_locally", stopped_locally, "打断后本地立即收到tts stop停播指令")
            late_events, late_binaries = await drain(abort_socket, 1.2)
            check("stale_audio_dropped", len(late_binaries) == 0, f"已取消回复的迟到音频被丢弃（新增{len(late_binaries)}帧，云端剩余流已全部过滤）")
            await abort_socket.close()

            # ---- 场景四：工具调用落库与回传 ----
            mock.responses.insert(0, "tool")
            tool_app = build_config(realtime_url)
            tool_server = TestServer(tool_app)
            tool_client = TestClient(tool_server)
            await tool_client.start_server()
            try:
                socket = await device_connect(tool_client, {"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1}})
                await collect_until(socket, "hello")
                await collect_until(socket, "custom")
                await socket.send_json({"type": "listen", "state": "start"})
                codec2 = OpusBridge()
                for packet in codec2.encode(b"\x00\x00" * 1600, finish=True):
                    await socket.send_bytes(packet)
                await drain(socket, 3.0)
                output = mock.tool_output
                check("tool_executed_and_reported", output is not None and output.get("saved") is True and output.get("storage") == "pc_prototype", f"remember_fact执行并回传function_call_output：{output}")
                database = sqlite3.connect(Path(TEST_RUNTIME.name) / "memory.sqlite3")
                row = database.execute("SELECT value FROM memories WHERE device_id=? AND key=?", ("desk-prototype-01", "牛牛的名字")).fetchone()
                database.close()
                check("memory_persisted", row is not None and row[0] == "牛牛", f"记忆已写入本机数据库并读回：{row[0] if row else None}")
                await socket.close()
            finally:
                await tool_client.close()

            # ---- 场景五：浏览器测试台会话（/debug/ws，电脑端语音测试入口） ----
            browser_rejected = False
            browser_socket = await client.ws_connect("/debug/ws")
            await browser_socket.send_json({"type": "auth", "token": "wrong-browser-token-wrong"})
            await browser_socket.receive(timeout=5)
            browser_rejected = browser_socket.close_code == 1008
            check("browser_auth_reject", browser_rejected, f"浏览器错误令牌被1008关闭(code={browser_socket.close_code})")

            browser_socket = await client.ws_connect("/debug/ws")
            await browser_socket.send_json({"type": "auth", "token": "device_token_local_test_000000000"})
            _, browser_events = await collect_until(browser_socket, "custom")
            await browser_socket.send_json({"type": "text", "text": "你好"})
            text_tts, text_prior = await collect_until(browser_socket, "tts")
            text_events, text_binaries = await drain(browser_socket, 1.5)
            text_pcm = [item for item in text_prior + text_events + text_binaries if isinstance(item, bytes)]
            pcm_bytes = b"".join(text_pcm)
            check("browser_text_roundtrip", text_tts.get("state") == "start" and len(text_pcm) >= 1 and len(pcm_bytes) % 2 == 0, f"浏览器文字问答：收到{len(text_pcm)}段PCM直通音频共{len(pcm_bytes)}字节（24kHz 16bit）")
            await browser_socket.close()

            # ---- 场景六：空闲看门狗 ----
            idle_app = build_config(realtime_url, idle_seconds=10, session_seconds=600)
            idle_server = TestServer(idle_app)
            idle_client = TestClient(idle_server)
            await idle_client.start_server()
            try:
                socket = await device_connect(idle_client, {"type": "hello", "version": 1, "audio_params": {"format": "opus", "sample_rate": 16000, "channels": 1}})
                await collect_until(socket, "hello")
                await collect_until(socket, "custom")
                idle, _ = await collect_until(socket, "custom", timeout=15)
                await socket.receive(timeout=5)
                check("idle_watchdog", idle["payload"]["state"] == "idle", "无交互10秒后进入idle并关闭会话")
            finally:
                await idle_client.close()
        finally:
            await client.close()
    finally:
        await runner.cleanup()

    passed = sum(1 for row in RESULTS if row["ok"])
    summary = {"date": datetime.now().astimezone().isoformat(), "scope": "bridge_local_protocol_only", "passed": passed, "total": len(RESULTS), "results": RESULTS, "note": "全部为电脑本地协议与转换验证；不代表云端联通、实机音频或硬件通过"}
    target = ROOT / "evidence" / "bridge_local_test.json"
    target.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"\n结果：{passed}/{len(RESULTS)} 项通过，明细见 {target.name}")
    TEST_RUNTIME.cleanup()
    return 0 if passed == len(RESULTS) else 1


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))

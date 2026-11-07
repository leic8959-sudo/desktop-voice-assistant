"""临时局域网播放检查：低音量合成音，验证设备缺帧计数，不调用云端。

运行前需停止8765上的正常桥接，运行后恢复。只允许已配对设备令牌。
"""

import asyncio
import argparse
import hmac
import json
import math
from pathlib import Path
import struct
import time
import wave

from aiohttp import web, WSMsgType

ROOT = Path(__file__).resolve().parents[1]


async def main(check_ui=False, speech_file=None, state_stress=False):
    config = json.loads((ROOT / "config.local.json").read_text(encoding="utf-8"))
    result = asyncio.get_running_loop().create_future()
    sockets = set()
    notification_ack = False
    timeout_seconds = 60 if check_ui else 30
    pcm_source = None
    if speech_file:
        with wave.open(speech_file, "rb") as source:
            if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, 24000):
                raise ValueError("测试语音必须为24kHz单声道PCM16 WAV")
            pcm_source = source.readframes(source.getnframes())
        if not pcm_source or len(pcm_source) > 24_000 * 2 * 20:
            raise ValueError("测试语音时长必须在0至20秒之间")
    frame_count = (len(pcm_source) + 959) // 960 if pcm_source else 300
    audio_ms = frame_count * 20

    async def device(request):
        token = request.headers.get("Authorization", "").removeprefix("Bearer ")
        if not hmac.compare_digest(token, config["device_token"]):
            raise web.HTTPUnauthorized()
        socket = web.WebSocketResponse(heartbeat=10)
        await socket.prepare(request)
        sockets.add(socket)
        hello = await socket.receive_json(timeout=5)
        if hello.get("type") != "hello":
            await socket.close()
            return socket
        await socket.send_json({"type": "hello", "version": 1, "transport": "websocket", "audio_params": {"format": "pcm_s16le", "sample_rate": 24000, "channels": 1}})

        async def playback():
            await asyncio.sleep(0.6)
            if check_ui:
                for state in ("idle", "listening", "thinking", "executing"):
                    await socket.send_json({"type": "custom", "payload": {"state": state, "detail": "点云状态检查"}})
                    print("ui_state_sent=" + state, flush=True)
                    await asyncio.sleep(1.5)
                await socket.send_json({"type": "notification", "notification": {"id": "pointcloud-check", "lease_token": "local-display-check", "title": "点云提醒检查", "due_at": ""}})
                await asyncio.sleep(5.5)
            await socket.send_json({"type": "tts", "state": "start"})
            await socket.send_json({"type": "custom", "payload": {"state": "speaking", "detail": "播放连续性检查"}})
            start = time.monotonic()
            for frame in range(frame_count):
                wait = start + frame * 0.02 - time.monotonic() - 0.08
                if wait > 0:
                    await asyncio.sleep(wait)
                data = pcm_source[frame * 960:(frame + 1) * 960].ljust(960, b"\0") if pcm_source else b"".join(struct.pack("<h", int(700 * math.sin(2 * math.pi * 440 * (frame * 480 + index) / 24000))) for index in range(480))
                if state_stress and frame == frame_count // 2:
                    await socket.send_json({"type": "custom", "payload": {"state": "thinking", "detail": "延迟状态覆盖检查"}})
                await socket.send_bytes(data)
            await socket.send_json({"type": "tts", "state": "stop"})
            await socket.send_json({"type": "custom", "payload": {"state": "listening"}})
            if state_stress:
                await socket.send_json({"type": "tts", "state": "sentence_start", "text": "延迟字幕检查"})
            print(json.dumps({"sent_audio_ms": audio_ms, "elapsed_ms": round((time.monotonic() - start) * 1000)}), flush=True)

        task = asyncio.create_task(playback())
        try:
            async for message in socket:
                if message.type == WSMsgType.TEXT:
                    event = json.loads(message.data)
                    if event.get("type") == "memory_request":
                        continue
                    elif event.get("type") == "audio_stats":
                        if not result.done():
                            result.set_result({key: event.get(key) for key in ("frames", "underruns", "overflows", "elapsed_ms")})
                    elif event.get("type") == "notification_ack":
                        nonlocal notification_ack
                        notification_ack = event.get("id") == "pointcloud-check" and event.get("lease_token") == "local-display-check"
                        print("notification_screen_ack=" + str(notification_ack), flush=True)
        finally:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
            sockets.discard(socket)
        return socket

    app = web.Application()
    app.router.add_get("/device/v1/", device)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, config["host"], config["port"]).start()
    print(f"等待自有终端重连，播放6秒低音量测试音；最长等待{timeout_seconds}秒。", flush=True)
    try:
        stats = await asyncio.wait_for(result, timeout_seconds)
        stats.update({"test_audio_ms": audio_ms, "audio_source": "local_tts_file" if speech_file else "synthetic_tone", "cloud_used": False, "subjective_voice_verified": False, "state_stress": state_stress})
        if check_ui:
            stats.update({"states_sent": ["idle", "listening", "thinking", "executing", "notification", "speaking"], "notification_screen_ack": notification_ack})
        output_name = "aec-speech-state-check-20261029.json" if speech_file else "pointcloud-audio-continuity-20261029.json" if check_ui else "audio-continuity-20261029.json"
        (ROOT / "evidence" / output_name).write_text(json.dumps(stats, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps(stats), flush=True)
    finally:
        await asyncio.gather(*(socket.close() for socket in tuple(sockets)), return_exceptions=True)
        await runner.cleanup()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-cycle", action="store_true", help="同时检查点云状态切换及屏幕提醒确认")
    parser.add_argument("--speech-file", help="播放本地24kHz单声道PCM16 WAV，检查AEC软件参考")
    parser.add_argument("--state-stress", action="store_true", help="播放中发送思考状态及延迟字幕，检查显示归属")
    args = parser.parse_args()
    asyncio.run(main(args.ui_cycle, args.speech_file, args.state_stress))

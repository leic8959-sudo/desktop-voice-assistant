"""本地提示音链路检查：触发设备思考状态并同步抓取 mic/reference/clean 三通道波形。

只验证固件提示音真实进入播放链且 AEC 参考对齐；不调用云模型、不发送家电动作。
需要先停用桥接（占用同一端口），结束后由调用方重启桥接。
"""

import argparse
import asyncio
import base64
import hmac
import json
import math
import wave
from pathlib import Path

from aiohttp import WSMsgType, web

ROOT = Path(__file__).resolve().parents[1]
SAMPLE_RATE = 16000
FRAME_SAMPLES = 512
CHANNELS = 3


def goertzel(samples, rate, freq):
    """单频点能量，用于核对提示音两个音符的频率。"""
    coeff = 2.0 * math.cos(2.0 * math.pi * freq / rate)
    s1 = s2 = 0.0
    for value in samples:
        s0 = value + coeff * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - coeff * s1 * s2
    return math.sqrt(max(power, 0.0)) / max(len(samples) / rate, 1e-9)


async def main(args):
    config = json.loads((ROOT / "config.local.json").read_text(encoding="utf-8-sig"))
    captured = bytearray()
    export_done = asyncio.Event()
    finished = asyncio.get_running_loop().create_future()
    sockets = set()
    notes = {"tone1_988hz": 0.0, "tone2_1319hz": 0.0, "baseline_500hz": 0.0}
    rms = {"mic": 0.0, "reference": 0.0, "clean": 0.0}

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
        await socket.send_json({"type": "hello", "version": 1, "audio_params": {"format": "pcm_s16le", "sample_rate": 24000, "channels": 1}})
        await socket.send_json({"type": "custom", "payload": {"state": "idle"}})
        await asyncio.sleep(0.5)
        await socket.send_json({"type": "diagnostics", "enabled": True, "disable_barge": True, "capture_audio": True})
        await asyncio.sleep(0.3)
        print("CHIME_TRIGGER state=thinking", flush=True)
        await socket.send_json({"type": "custom", "payload": {"state": "thinking"}})
        # 边读诊断帧边等导出完成；设备音频诊断共5.12秒，超时视为失败。
        async def pump():
            async for message in socket:
                if message.type != WSMsgType.TEXT:
                    continue
                event = json.loads(message.data)
                if event.get("type") == "device_ping":
                    await socket.send_json({"type": "device_pong"})
                if event.get("type") == "audio_diagnostic":
                    captured.extend(base64.b64decode(event.get("pcm", ""), validate=True))
                    if event.get("done"):
                        export_done.set()
                elif event.get("type") == "barge_diagnostic":
                    print("BARGE_DIAG " + json.dumps({k: event.get(k) for k in ("barge_state", "speaking", "queue_depth", "played_samples", "overflows")}), flush=True)
        reader_task = asyncio.create_task(pump())
        try:
            await asyncio.wait_for(export_done.wait(), 25)
            # 诊断结束后多观察6秒，收集队列深度是否归零（判定播放任务是否卡死）。
            await asyncio.sleep(6)
            finished.set_result({"ok": True})
        except TimeoutError:
            finished.set_result({"ok": False, "error": "audio_diagnostic_timeout"})
        finally:
            try:
                await socket.send_json({"type": "custom", "payload": {"state": "idle"}})
            except Exception:
                pass
            reader_task.cancel()
            await asyncio.gather(reader_task, return_exceptions=True)
        return socket

    app = web.Application()

    async def device_handler(request):
        return await device(request)

    app.router.add_get("/device/v1/", device_handler)
    runner = web.AppRunner(app, access_log=None)
    await runner.setup()
    await web.TCPSite(runner, config["host"], config["port"]).start()
    print("CHIME_CHECK_READY waiting_device", flush=True)
    try:
        result = await asyncio.wait_for(finished, 60)
    finally:
        await asyncio.gather(*(socket.close() for socket in tuple(sockets)), return_exceptions=True)
        await runner.cleanup()
    if not captured:
        result["captured_bytes"] = 0
        Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(result), flush=True)
        raise RuntimeError("no_capture")
    frames = len(captured) // (FRAME_SAMPLES * CHANNELS * 2)
    with wave.open(str(Path(args.output).with_suffix(".wav")), "wb") as recording:
        recording.setnchannels(CHANNELS)
        recording.setsampwidth(2)
        recording.setframerate(SAMPLE_RATE)
        recording.writeframes(bytes(captured[: frames * FRAME_SAMPLES * CHANNELS * 2]))
    # 三通道交织：0=mic 1=reference 2=clean。
    channels = [[], [], []]
    stream = memoryview(captured[: frames * FRAME_SAMPLES * CHANNELS * 2]).cast("h")
    for index in range(len(stream)):
        channels[index % CHANNELS].append(stream[index])
    for name, channel in zip(("mic", "reference", "clean"), channels):
        rms[name] = math.sqrt(sum(value * value for value in channel) / max(len(channel), 1))
    # 提示音约250ms（4000样本），从参考信号最大能量点向后取窗口核对音符频率。
    reference = channels[1]
    window = 4000
    best_start = max(range(0, max(len(reference) - window, 1), 512), key=lambda start: sum(value * value for value in reference[start:start + window]))
    segment = reference[best_start:best_start + window]
    notes["tone1_988hz"] = goertzel(segment, SAMPLE_RATE, 988.0)
    notes["tone2_1319hz"] = goertzel(segment, SAMPLE_RATE, 1318.5)
    notes["baseline_500hz"] = goertzel(segment, SAMPLE_RATE, 500.0)
    result.update({"captured_frames": frames, "rms": {k: round(v, 1) for k, v in rms.items()}, "chime_reference_band_amplitude": {k: round(v, 1) for k, v in notes.items()}})
    Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True)
    asyncio.run(main(parser.parse_args()))

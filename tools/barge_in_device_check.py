"""Local paired-device barge-in check; no model or home service calls."""

import argparse
import asyncio
import ctypes
import hmac
import json
import time
import traceback
import wave
from pathlib import Path

from aiohttp import WSMsgType, web


ROOT = Path(__file__).resolve().parents[1]


def load_pcm(path, rate):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (1, 2, rate):
            raise ValueError(f"Expected mono PCM16 WAV at {rate} Hz")
        pcm = source.readframes(source.getnframes())
    if not pcm or len(pcm) > rate * 2 * 30:
        raise ValueError("Audio duration must be between 0 and 30 seconds")
    return pcm


def loop_pcm(pcm, rate, seconds):
    frame_bytes = 2
    target_bytes = int(rate * seconds) * frame_bytes
    repeats = (target_bytes + len(pcm) - 1) // len(pcm)
    return (pcm * repeats)[:target_bytes]


async def main(args):
    config = json.loads((ROOT / "config.local.json").read_text(encoding="utf-8-sig"))
    pcm = load_pcm(Path(args.far_file), 24000)
    if args.human_near:
        pcm = loop_pcm(pcm, 24000, 22.0)
    playback_id = "local-barge-check"
    events = []
    diagnostics = []
    captured_pcm = bytearray()
    near_window_started = None
    confirmation_source = None
    finished = asyncio.get_running_loop().create_future()
    audio_export_done = asyncio.Event()
    sockets = set()

    async def device(request):
        nonlocal confirmation_source
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
        gate = asyncio.Event()
        gate.set()
        ready = asyncio.Event()
        confirmed = False
        sent_frames = 0
        uploaded_samples = 0
        started = time.monotonic()

        async def acknowledge(event, phase):
            await socket.send_json({"type": "barge_in_ack", "phase": phase, "candidate_id": event["candidate_id"], "playback_id": playback_id, "played_samples": event.get("played_samples", 0)})

        async def playback():
            nonlocal sent_frames, near_window_started
            await asyncio.sleep(0.5)
            if args.wake_file:
                ctypes.windll.winmm.PlaySoundW(str(Path(args.wake_file).resolve()), None, 0x00020001)
            if args.direct_playback:
                await socket.send_json({"type": "diagnostics", "enabled": True, "test_listening": True, "disable_barge": args.capture_audio})
                ready.set()
            if args.human_near:
                print("NEAR_SPEECH_WINDOW prepare_seconds=15 far_playback_seconds=22; 请准备真人靠近设备，并在窗口内自然插话：‘等一下，我换个问题，三加四等于几’。near_window 仅表示时间窗口，不证明人声来源。", flush=True)
                events.append({"phase": "near_speech_window", "prepare_seconds": 15, "far_playback_seconds": 22, "source_verified": False, "elapsed_ms": round((time.monotonic() - started) * 1000)})
                await asyncio.sleep(15)
            print("WAITING_FOR_DEVICE_WAKE", flush=True)
            await asyncio.wait_for(ready.wait(), 75 if args.human_near else 60)
            await socket.send_json({"type": "custom", "payload": {"state": "listening"}})
            await asyncio.sleep(0.4)
            near_window_started = time.monotonic() if args.human_near else None
            await socket.send_json({"type": "tts", "state": "start", "playback_id": playback_id})
            await socket.send_json({"type": "custom", "payload": {"state": "speaking"}})
            clock = asyncio.get_running_loop()
            deadline = clock.time()
            next_near = deadline + args.near_after
            near_sent = False
            for offset in range(0, len(pcm), 960):
                if args.capture_audio and offset == 48000:
                    await socket.send_json({"type": "diagnostics", "enabled": True, "disable_barge": True, "capture_audio": True})
                await gate.wait()
                if confirmed:
                    break
                if clock.time() - deadline > 0.15:
                    deadline = clock.time()
                now = clock.time()
                if now - deadline > 0.15:
                    deadline = now
                wait = deadline - now - 0.06
                if wait > 0:
                    await asyncio.sleep(wait)
                await gate.wait()
                if confirmed:
                    break
                if args.near_file and not near_sent and clock.time() >= next_near:
                    near_sent = True
                    events.append({"phase": "near_audio_started", "elapsed_ms": round((time.monotonic() - started) * 1000)})
                    ctypes.windll.winmm.PlaySoundW(str(Path(args.near_file).resolve()), None, 0x00020001)
                await socket.send_bytes(pcm[offset:offset + 960].ljust(960, b"\0"))
                sent_frames += 1
                deadline += 0.02
            if not confirmed:
                await socket.send_json({"type": "tts", "state": "stop", "playback_id": playback_id})
                await socket.send_json({"type": "custom", "payload": {"state": "listening"}})
                print("sender_completed_frames=" + str(sent_frames), flush=True)
                await asyncio.sleep(8)
                if not finished.done():
                    finished.set_result({"confirmed": False, "device_capture_samples": uploaded_samples, "sent_frames": sent_frames, "error": "audio_stats_missing"})
            else:
                await asyncio.sleep(1.8)
                if not finished.done():
                    finished.set_result({"confirmed": True, "confirmation_source": confirmation_source, "uploaded_samples": uploaded_samples, "sent_frames": sent_frames})

        await socket.send_json({"type": "hello", "version": 1, "audio_params": {"format": "pcm_s16le", "sample_rate": 24000, "channels": 1}})
        await socket.send_json({"type": "custom", "payload": {"state": "idle"}})
        await socket.send_json({"type": "diagnostics", "enabled": True, "disable_barge": args.capture_audio})
        task = asyncio.create_task(playback())
        def playback_done(completed):
            if completed.cancelled():
                return
            error = completed.exception()
            if error is not None and not finished.done():
                finished.set_exception(error)
        task.add_done_callback(playback_done)
        try:
            async for message in socket:
                if message.type == WSMsgType.BINARY:
                    uploaded_samples += len(message.data) // 2
                    continue
                if message.type != WSMsgType.TEXT:
                    continue
                event = json.loads(message.data)
                kind = event.get("type")
                if kind == "device_ping":
                    await socket.send_json({"type": "device_pong"})
                    continue
                if kind == "audio_diagnostic":
                    import base64
                    captured_pcm.extend(base64.b64decode(event.get("pcm", ""), validate=True))
                    if event.get("done"):
                        audio_export_done.set()
                    continue
                if kind == "barge_diagnostic":
                    if len(diagnostics) < 100:
                        diagnostics.append(event)
                    print(json.dumps(event), flush=True)
                    continue
                if kind == "listen" and event.get("state") == "start":
                    ready.set()
                    continue
                if kind == "barge_in" and event.get("playback_id") == playback_id:
                    phase = event.get("phase")
                    entry = {key: event.get(key) for key in ("phase", "candidate_id", "playback_id", "played_samples")}
                    entry["elapsed_ms"] = round((time.monotonic() - started) * 1000)
                    events.append(entry)
                    print(json.dumps(entry), flush=True)
                    if phase == "candidate":
                        gate.clear()
                        await acknowledge(event, "candidate")
                    elif phase == "resume":
                        await acknowledge(event, "resume")
                        gate.set()
                    elif phase == "confirmed":
                        confirmation_source = "near_window" if near_window_started is not None and time.monotonic() >= near_window_started else "far_audio_or_before_window"
                        entry["confirmation_source"] = confirmation_source
                        confirmed = True
                        gate.set()
                        await socket.send_json({"type": "interrupt", "playback_id": playback_id})
                        await acknowledge(event, "confirmed")
                        await socket.send_json({"type": "custom", "payload": {"state": "listening"}})
                elif kind == "audio_stats" and not finished.done():
                    stats_result = {"confirmed": confirmed, "confirmation_source": confirmation_source, "uploaded_samples": uploaded_samples, "sent_frames": sent_frames, "audio_stats": {key: event.get(key) for key in ("frames", "underruns", "overflows", "elapsed_ms")}}
                    if args.human_near and not confirmed:
                        stats_result["error"] = "unconfirmed"
                    elif not args.human_near and not args.near_file and confirmed:
                        stats_result["error"] = "far_audio_confirmed"
                    elif args.human_near and confirmation_source != "near_window":
                        stats_result["confirmed"] = False
                        stats_result["error"] = "far_audio_confirmed"
                    finished.set_result(stats_result)
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
    print("LOCAL_BARGE_TEST_READY", flush=True)
    result = {"cloud_used": False, "human_near": args.human_near, "near_source": "computer_speaker" if args.near_file else "human" if args.human_near else "none", "confirmation_source": None, "events": events, "diagnostics": diagnostics}
    try:
        result.update(await asyncio.wait_for(finished, 150 if args.human_near else 90))
        if not args.human_near and not args.near_file and result.get("confirmed"):
            result["error"] = "far_audio_confirmed"
        elif (args.human_near or args.near_file) and not result.get("confirmed"):
            result["error"] = "unconfirmed"
        if args.capture_audio:
            await asyncio.wait_for(audio_export_done.wait(), 20 if args.human_near else 12)
    except TimeoutError:
        result["error"] = "device_check_timeout"
        traceback.print_exc()
        raise
    except Exception as error:
        result["error"] = type(error).__name__
        traceback.print_exc()
        raise
    finally:
        if captured_pcm:
            with wave.open(str(Path(args.output).with_suffix('.audio.wav')), 'wb') as recording:
                recording.setnchannels(3)
                recording.setsampwidth(2)
                recording.setframerate(16000)
                recording.writeframes(captured_pcm)
        Path(args.output).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        await asyncio.gather(*(socket.close() for socket in tuple(sockets)), return_exceptions=True)
        await runner.cleanup()
    print(json.dumps(result), flush=True)
    if result.get("error"):
        raise RuntimeError(result["error"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--far-file", required=True)
    parser.add_argument("--wake-file")
    parser.add_argument("--near-file")
    parser.add_argument("--human-near", "--human-near-speech", action="store_true", help="真人验收：播放前准备15秒，再循环远端语音22秒")
    parser.add_argument("--near-after", type=float, default=3.5)
    parser.add_argument("--direct-playback", action="store_true", help="仅本地检查：跳过唤醒入口，直接播放")
    parser.add_argument("--capture-audio", action="store_true", help="仅本地检查：临时获取同步mic/reference/clean波形")
    parser.add_argument("--output", required=True)
    asyncio.run(main(parser.parse_args()))

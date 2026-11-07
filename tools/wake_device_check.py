"""本地配对设备唤醒观测；不连接云模型或家居服务。"""

import argparse
import asyncio
import base64
import ctypes
import hmac
import json
import logging
import math
import struct
import sys
import time
import wave
from pathlib import Path

from aiohttp import WSMsgType, web


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from src.assistant_app.application.pairing import PairingManager

MAX_SECONDS = 120
MAX_STATS_SECONDS = 30
CAPTURE_SECONDS = 5.12
MAX_DIAGNOSTICS = 120
CAPTURE_CHUNK_BYTES = 3072
CAPTURE_MAX_BYTES = 491520
LOGGER = logging.getLogger("assistant.wake_check")


def decode_capture_chunk(encoded, captured_bytes):
    if not isinstance(encoded, str) or len(encoded) > ((CAPTURE_CHUNK_BYTES + 2) // 3) * 4:
        raise ValueError("audio_diagnostic 编码超过单帧容量")
    chunk = base64.b64decode(encoded, validate=True)
    if len(chunk) > CAPTURE_CHUNK_BYTES or captured_bytes + len(chunk) > CAPTURE_MAX_BYTES:
        raise ValueError("audio_diagnostic 超过有界采集容量")
    if len(chunk) % 6:
        raise ValueError("audio_diagnostic 不是三通道 PCM16")
    return chunk


def capture_statistics(pcm):
    if len(pcm) > CAPTURE_MAX_BYTES or len(pcm) % 6:
        raise ValueError("采集数据必须为有界三通道 PCM16")
    stats = [{"sum_square": 0, "peak": 0, "clipped": 0, "zero": 0} for _ in range(3)]
    for frame in struct.iter_unpack("<hhh", pcm):
        for values, value in zip(stats, frame):
            values["sum_square"] += value * value
            values["peak"] = max(values["peak"], abs(value))
            values["clipped"] += abs(value) >= 32767
            values["zero"] += value == 0
    sample_count = len(pcm) // 6
    return {name: {
        "rms": round((values["sum_square"] / sample_count) ** 0.5, 2) if sample_count else 0,
        "peak": values["peak"],
        "clip_fraction": values["clipped"] / sample_count if sample_count else 0,
        "zero_fraction": values["zero"] / sample_count if sample_count else 0,
    } for name, values in zip(("mic", "reference", "clean"), stats)}


def observation_seconds(args):
    if not 1 <= args.seconds <= MAX_SECONDS or not 0 <= args.capture_delay <= MAX_SECONDS:
        raise ValueError("观测时长需为1～120秒，采集延迟需为0～120秒")
    if args.stats_only:
        if not args.capture_audio or args.sample:
            raise ValueError("--stats-only 必须显式配合 --capture-audio，且不能播放 --sample")
        seconds = min(args.seconds, MAX_STATS_SECONDS)
        if args.capture_delay + CAPTURE_SECONDS > seconds:
            raise ValueError("stats-only 窗口必须容纳 capture-delay 加5.12秒，且最多30秒")
        return seconds
    return max(args.seconds, 60) if args.capture_audio else args.seconds


def diagnostic_metrics(event):
    result = {"voice": event.get("voice") if type(event.get("voice")) is bool else False}
    for name, lower, upper in (("clean_rms", 0, 32768), ("ref_rms", 0, 32768), ("correlation", -1, 1)):
        value = event.get(name)
        if type(value) in (int, float) and lower <= value <= upper and math.isfinite(value):
            result[name] = value
    return result


def validate_sample(path):
    if not path.exists() or not path.is_file():
        raise ValueError("sample WAV 不存在")
    try:
        with wave.open(str(path), "rb") as source:
            if source.getnchannels() != 1 or source.getsampwidth() != 2:
                raise ValueError("sample WAV 必须为单声道 PCM16")
            if source.getframerate() not in (16000, 24000):
                raise ValueError("sample WAV 采样率需为16000或24000 Hz")
            if source.getnframes() == 0:
                raise ValueError("sample WAV 不能为空")
    except (wave.Error, OSError) as error:
        raise ValueError("sample WAV 无法读取") from error


def play_sample(path):
    if path is None:
        return
    if not hasattr(ctypes, "windll"):
        raise RuntimeError("--sample 仅支持 Windows PlaySound")
    flags = 0x00020001  # SND_FILENAME | SND_ASYNC
    ctypes.windll.winmm.PlaySoundW(str(path.resolve()), None, flags)


def stop_sample():
    if hasattr(ctypes, "windll"):
        ctypes.windll.winmm.PlaySoundW(None, None, 0)


async def main(args):
    seconds = observation_seconds(args)
    config = json.loads((ROOT / "config.local.json").read_text(encoding="utf-8-sig"))
    pairing = PairingManager(storage_path=Path(config.get("runtime_dir", ROOT / "runtime")) / "pairing-sessions.json")
    sample = Path(args.sample).resolve() if args.sample else None
    if sample:
        validate_sample(sample)

    started = time.monotonic()
    events = []
    diagnostics = []
    sockets = set()
    wake_count = 0
    sample_played = False
    capture_pcm = bytearray()
    capture_done = asyncio.Event()
    capture_finished = asyncio.Event()
    capture_started = False
    capture_errors = []
    capture_task = None
    device_reserved = False
    cleanup_started = set()
    cleanup_errors = []

    async def finish_socket(socket):
        if socket in cleanup_started:
            return
        cleanup_started.add(socket)
        try:
            if not socket.closed:
                async with asyncio.timeout(2):
                    await socket.send_json({"type": "diagnostics", "enabled": False})
        except Exception as error:
            LOGGER.exception("wake_check_disable_diagnostics_failed")
            cleanup_errors.append(type(error).__name__)
        finally:
            try:
                async with asyncio.timeout(2):
                    await socket.close()
            except Exception as error:
                LOGGER.exception("wake_check_socket_close_failed")
                cleanup_errors.append(type(error).__name__)

    def elapsed_ms():
        return round((time.monotonic() - started) * 1000)

    async def device(request):
        nonlocal wake_count, sample_played, capture_started, capture_task, device_reserved
        authorization = request.headers.get("Authorization", "")
        token = authorization.removeprefix("Bearer ")
        if not authorization.startswith("Bearer ") or not token.isascii():
            raise web.HTTPUnauthorized()
        if not hmac.compare_digest(token, config["device_token"]) and not pairing.authenticate_device(token):
            raise web.HTTPUnauthorized()
        if device_reserved:
            raise web.HTTPConflict(text="单次观测仅接受一个已认证设备连接")
        device_reserved = True
        socket = web.WebSocketResponse(heartbeat=10, max_msg_size=65536)
        try:
            await socket.prepare(request)
            sockets.add(socket)
            hello = await socket.receive_json(timeout=5)
            if not isinstance(hello, dict) or hello.get("type") != "hello":
                return socket
            await socket.send_json({
                "type": "hello",
                "version": 1,
                "audio_params": {"format": "pcm_s16le", "sample_rate": 24000, "channels": 1},
            })
            await socket.send_json({"type": "custom", "payload": {"state": "idle"}})
            await socket.send_json({"type": "diagnostics", "enabled": True})

            async def request_capture():
                nonlocal capture_started
                await asyncio.sleep(args.capture_delay)
                if socket.closed or not args.capture_audio or capture_started:
                    return
                capture_started = True
                print("CAPTURE_START", flush=True)
                await socket.send_json({"type": "diagnostics", "enabled": True, "capture_audio": True})

            if args.capture_audio:
                capture_task = asyncio.create_task(request_capture())
            if sample and not sample_played:
                sample_played = True
                events.append({"type": "sample_started", "elapsed_ms": elapsed_ms()})
                play_sample(sample)
            async for message in socket:
                if message.type != WSMsgType.TEXT:
                    continue
                try:
                    event = json.loads(message.data)
                except (TypeError, ValueError):
                    LOGGER.exception("wake_check_invalid_event")
                    continue
                kind = event.get("type")
                if kind == "audio_diagnostic" and args.capture_audio:
                    try:
                        if not capture_started:
                            raise ValueError("采集尚未开始，拒绝音频数据")
                        chunk = decode_capture_chunk(event.get("pcm", ""), len(capture_pcm))
                        capture_pcm.extend(chunk)
                        if event.get("done") is True:
                            if len(capture_pcm) != CAPTURE_MAX_BYTES:
                                raise ValueError("audio_diagnostic 未返回完整5.12秒数据")
                            capture_done.set()
                            capture_finished.set()
                            print("CAPTURE_DONE captured_samples=" + str(len(capture_pcm) // 6), flush=True)
                            if args.stats_only:
                                break
                    except Exception as error:
                        LOGGER.exception("wake_capture_invalid_audio")
                        capture_errors.append(type(error).__name__)
                        capture_finished.set()
                        break
                    continue
                if kind == "listen" and event.get("state") == "start":
                    wake_count += 1
                    entry = {
                        "type": "wake",
                        "mode": event.get("mode") if event.get("mode") in ("manual", "auto", "realtime") else "unknown",
                        "monotonic_ms": round(time.monotonic() * 1000),
                        "elapsed_ms": elapsed_ms(),
                    }
                    if len(events) < MAX_DIAGNOSTICS:
                        events.append(entry)
                    print(json.dumps(entry, ensure_ascii=False), flush=True)
                    # 立即回待机，避免建立云会话，也避免连续对话状态混入下一次唤醒。
                    await socket.send_json({"type": "custom", "payload": {"state": "idle"}})
                elif kind == "barge_diagnostic" and len(diagnostics) < MAX_DIAGNOSTICS:
                    diagnostics.append({"elapsed_ms": elapsed_ms(), **diagnostic_metrics(event)})
        except Exception:
            LOGGER.exception("wake_check_connection_failed")
        finally:
            if capture_task is not None:
                capture_task.cancel()
                await asyncio.gather(capture_task, return_exceptions=True)
            await finish_socket(socket)
            sockets.discard(socket)
            if args.stats_only:
                capture_finished.set()
        return socket

    app = web.Application()
    app.router.add_get("/device/v1/", device)
    runner = web.AppRunner(app, access_log=None, shutdown_timeout=3)
    await runner.setup()
    await web.TCPSite(runner, config["host"], config["port"]).start()
    print("LOCAL_WAKE_TEST_READY", flush=True)
    result = {
        "status": "not_awakened",
        "wake_count": 0,
        "events": events,
        "diagnostics": diagnostics,
        "cloud_used": False,
        "recording_file": None,
        "captured_samples": 0,
        "capture_done": False,
        "capture_errors": capture_errors,
        "capture_stats": {},
        "stats_only": args.stats_only,
        "window_seconds": seconds,
        "sample_rate": 16000,
        "capture_location": "software_aec_output_before_afe_not_final_uplink",
        "cleanup_errors": cleanup_errors,
    }
    try:
        if args.stats_only:
            try:
                await asyncio.wait_for(capture_finished.wait(), seconds)
            except TimeoutError:
                capture_errors.append("capture_window_timeout")
        else:
            await asyncio.sleep(seconds)
    finally:
        if capture_task is not None:
            capture_task.cancel()
            await asyncio.gather(capture_task, return_exceptions=True)
        await asyncio.gather(*(finish_socket(socket) for socket in tuple(sockets)))
        await runner.cleanup()
        result["wake_count"] = wake_count
        result["status"] = "observed" if wake_count else "not_awakened"
        result["events"] = events
        result["diagnostics"] = diagnostics
        result["capture_done"] = capture_done.is_set()
        result["capture_errors"] = capture_errors
        if capture_pcm:
            sample_count = len(capture_pcm) // 6
            if not args.stats_only:
                recording_path = Path(args.output).with_suffix(".audio.wav").resolve()
                with wave.open(str(recording_path), "wb") as recording:
                    recording.setnchannels(3)
                    recording.setsampwidth(2)
                    recording.setframerate(16000)
                    recording.writeframes(capture_pcm)
                result["recording_file"] = str(recording_path)
            result["captured_samples"] = sample_count
            result["capture_stats"] = capture_statistics(capture_pcm)
        capture_pcm.clear()
        Path(args.output).resolve().write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if sample_played:
            stop_sample()
    print(json.dumps(result, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=int, default=45, help="观测时长，默认45秒，最大120秒")
    parser.add_argument("--output", required=True, help="结果JSON路径")
    parser.add_argument("--sample", help="可选：显式播放到电脑扬声器的单声道PCM16 WAV")
    parser.add_argument("--capture-audio", action="store_true", help="显式采集设备audio_diagnostic三通道5.12秒波形")
    parser.add_argument("--stats-only", action="store_true", help="仅配合显式capture，最多30秒窗口；只输出数字统计，不播放或写音频文件")
    parser.add_argument("--capture-delay", type=float, default=15, help="连接后等待采集的秒数，默认15")
    arguments = parser.parse_args()
    if not 1 <= arguments.seconds <= MAX_SECONDS:
        parser.error("--seconds 必须在1到120之间")
    if not 0 <= arguments.capture_delay <= MAX_SECONDS:
        parser.error("--capture-delay 必须在0到120之间")
    try:
        asyncio.run(main(arguments))
    except (OSError, ValueError, RuntimeError) as error:
        parser.error(str(error))

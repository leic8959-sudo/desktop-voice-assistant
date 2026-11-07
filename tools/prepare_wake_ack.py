"""用当前实时模型与音色生成一句固定应答；不采麦克风、不连接设备。"""

import asyncio
import base64
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import aiohttp

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from gateway import HTTP_KEY, RUNTIME_KEY, STORE_KEY, RealtimeBackend, load_config
from src.assistant_app.application.wake_ack import ACK_TEXT, MAX_ACK_BYTES, load_wake_ack


async def prepare():
    config = load_config(ROOT / "config.local.json")
    # 音色偏好在运行时由 VoiceSettings 覆盖，离线准备保持同一入口。
    from voice_settings import VoiceSettings
    runtime = Path(config.get("runtime_dir", ROOT / "runtime"))
    VoiceSettings(runtime / "voice-preference.json", config)
    directory = ROOT / "assets" / "wake_ack"
    if load_wake_ack(directory, config):
        return {"status": "already_prepared", "voice": config["aliyun_voice"]}
    pcm, transcript = bytearray(), ""
    async with aiohttp.ClientSession(timeout=aiohttp.ClientTimeout(total=20, sock_connect=10)) as client:
        session = SimpleNamespace(config=config, app={HTTP_KEY: client, RUNTIME_KEY: SimpleNamespace(), STORE_KEY: SimpleNamespace(recall_memory=lambda **_kwargs: {})}, browser=True, raw_pcm=True, manual=True, require_wake_phrase=False, session_id="prepare-wake-ack", active_tool_schemas=lambda: [])
        backend = RealtimeBackend(session)
        try:
            async with asyncio.timeout(30):
                await backend.connect()
                await backend.update_instructions("你只负责逐字朗读用户给出的文字，不增减字词。")
                await backend.send_user_text("请只朗读引号内的两个字，不增加其他字：\"我在\"。")
                async for event in backend.events():
                    kind = event.get("type")
                    if kind == "response.audio.delta":
                        pcm.extend(base64.b64decode(event["delta"], validate=True))
                        if len(pcm) > MAX_ACK_BYTES:
                            raise ValueError("唤醒应答超过三秒，不写入缓存")
                    elif kind == "response.audio_transcript.done":
                        transcript = event.get("transcript", "")
                    elif kind == "error":
                        raise RuntimeError("云端应答准备失败，code=" + str(event.get("error", {}).get("code")))
                    elif kind == "response.done":
                        if event.get("response", {}).get("status") != "completed":
                            raise RuntimeError("云端应答未完整结束")
                        break
        finally:
            await backend.close()
    if "".join(character for character in transcript if character.isalnum()) != "我在" or not pcm or len(pcm) % 2:
        raise ValueError(f"应答文本或PCM不匹配，不写入缓存：text={transcript[:40]!r}, pcm_bytes={len(pcm)}")
    metadata = {"text": ACK_TEXT, "format": "pcm_s16le_24000_mono", "model": config["aliyun_model"], "voice": config["aliyun_voice"], "sha256": hashlib.sha256(pcm).hexdigest(), "created_at": datetime.now(timezone.utc).isoformat()}
    directory.mkdir(parents=True, exist_ok=True)
    temporary = directory / "ack.pcm.tmp"
    temporary.write_bytes(pcm)
    temporary.replace(directory / "ack.pcm")
    temporary = directory / "manifest.json.tmp"
    temporary.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    temporary.replace(directory / "manifest.json")
    if load_wake_ack(directory, config) != pcm:
        raise RuntimeError("写入后校验失败")
    return {"status": "prepared", "voice": metadata["voice"], "model": metadata["model"], "audio_ms": len(pcm) * 1000 // 48000, "sha256": metadata["sha256"]}


if __name__ == "__main__":
    print(json.dumps(asyncio.run(prepare()), ensure_ascii=False))

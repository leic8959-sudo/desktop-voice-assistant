"""固定唤醒应答缓存；单个当前音色，PCM16/24kHz/mono，最多三秒。"""

import base64
import hashlib
import json
import logging
from pathlib import Path
import uuid

LOGGER = logging.getLogger("assistant")
ACK_TEXT = "我在。"
MAX_ACK_BYTES = 24000 * 2 * 3


def load_wake_ack(directory, config):
    directory = Path(directory)
    try:
        with (directory / "manifest.json").open("rb") as source:
            metadata = json.loads(source.read(4097))
        if (metadata.get("model"), metadata.get("voice")) != (config.get("aliyun_model"), config.get("aliyun_voice")):
            return None
        if (metadata.get("text"), metadata.get("format")) != (ACK_TEXT, "pcm_s16le_24000_mono"):
            raise ValueError("唤醒缓存格式不匹配")
        with (directory / "ack.pcm").open("rb") as source:
            pcm = source.read(MAX_ACK_BYTES + 1)
        if not pcm or len(pcm) > MAX_ACK_BYTES or len(pcm) % 2 or hashlib.sha256(pcm).hexdigest() != metadata.get("sha256"):
            raise ValueError("唤醒缓存长度或摘要不匹配")
        return pcm
    except FileNotFoundError:
        return None
    except Exception:
        LOGGER.exception("wake_ack_cache_load_failed")
        return None


def wake_ack_events(pcm):
    response_id = "wake-ack-" + uuid.uuid4().hex
    yield {"type": "response.created", "response": {"id": response_id}, "cached_wake": True}
    yield {"type": "response.audio.delta", "response_id": response_id, "delta": base64.b64encode(pcm).decode("ascii"), "cached_wake": True}
    yield {"type": "response.audio_transcript.done", "response_id": response_id, "transcript": ACK_TEXT}
    yield {"type": "response.done", "response": {"id": response_id, "status": "completed", "output": [], "usage": {}}}

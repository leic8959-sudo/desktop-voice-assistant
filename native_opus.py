"""原生控制连接的有界Opus边界；内部会话与计数始终使用PCM样本。"""

import json
import struct
from collections import deque

from aiohttp import WSMessage, WSMsgType

from audio_codec import OpusBridge

# [人工决策-2026-11-13 10:06:24] 牛牛接受无PSRAM板采用Opus，保留完整功能并逐板注明音质、延迟及功能差异。
FRAMING = "xy_opus_v1"
HEADER = struct.Struct("!4sIIIHHHH")
RESET, END, CANCEL = 1, 2, 4
MAX_PACKET_BYTES = 1275
INPUT_SAMPLES = 320
OUTPUT_SAMPLES = 480
MAX_PCM_MESSAGE_BYTES = 65536


def packet_samples(packet, sample_rate):
    if not 1 <= len(packet) <= MAX_PACKET_BYTES:
        raise ValueError("Opus包长度超限")
    configuration = packet[0] >> 3
    if packet[0] & 4:
        raise ValueError("原生Opus只接受单声道")
    if configuration < 12:
        duration = (10, 20, 40, 60)[configuration & 3]
        samples = sample_rate * duration // 1000
    elif configuration < 16:
        samples = sample_rate * (10, 20)[configuration & 1] // 1000
    else:
        samples = sample_rate * (1 << (configuration & 3)) // 400
    code = packet[0] & 3
    if code == 0:
        frames = 1
    elif code in (1, 2):
        frames = 2
    else:
        if len(packet) < 2:
            raise ValueError("Opus帧数缺失")
        frames = packet[1] & 63
    if frames == 0 or samples * frames != sample_rate // 50:
        raise ValueError("原生Opus必须为20ms音频包")
    return samples * frames


def pack_audio(stream, sequence, position, decoded, trim, valid, flags, payload=b""):
    if not 1 <= stream <= 0xFFFFFFFF or not 0 <= sequence <= 0xFFFFFFFF or not 0 <= position <= 0xFFFFFFFF:
        raise ValueError("Opus流计数超限，需重新连接")
    return HEADER.pack(b"XYO1", stream, sequence, position, decoded, trim, valid, flags) + payload


class OpusInput:
    def __init__(self):
        self.stream = 0
        self.sequence = 0
        self.position = 0
        self.active = False
        self.codec = None
        self.discarded = 0
        self.pending = bytearray()
        self.frames = deque()

    def _drain(self):
        output = bytearray()
        while self.frames and len(self.pending) >= INPUT_SAMPLES * 2:
            trim, valid = self.frames.popleft()
            output.extend(self.pending[trim * 2:(trim + valid) * 2])
            del self.pending[:INPUT_SAMPLES * 2]
        if len(self.pending) > INPUT_SAMPLES * 2 * 4:
            raise ValueError("Opus重采样缓存超限")
        return bytes(output)

    def decode(self, data):
        if not HEADER.size <= len(data) <= HEADER.size + MAX_PACKET_BYTES:
            raise ValueError("原生Opus包大小无效")
        magic, stream, sequence, position, decoded, trim, valid, flags = HEADER.unpack_from(data)
        payload = data[HEADER.size:]
        if magic != b"XYO1" or stream == 0 or flags & ~(RESET | END | CANCEL):
            raise ValueError("原生Opus包头无效")
        if flags & RESET:
            if self.active or self.frames or self.pending or stream <= self.stream or sequence != 0 or position != 0 or flags & (END | CANCEL):
                raise ValueError("Opus新流必须在旧流结束后从序号0开始")
            self.stream, self.sequence, self.position = stream, 0, 0
            self.discarded = 0
            self.codec = OpusBridge(frame_duration=20, bit_rate=40000, constant_bit_rate=True)
            self.active = True
        if not self.active or stream != self.stream or sequence != self.sequence or position != self.position:
            raise ValueError("Opus流代次、顺序或有效样本位置不符")
        if flags & (END | CANCEL):
            if flags not in (END, CANCEL) or payload or decoded or trim or valid:
                raise ValueError("Opus结束包必须为空且只携带END或CANCEL")
            if flags == CANCEL:
                self.pending.clear()
                self.frames.clear()
                self.active = False
                self.codec = None
                return b""
            self.pending.extend(self.codec.finish_input())
            output = self._drain()
            if self.frames or self.pending:
                raise ValueError("Opus结束时解码样本数不符")
            self.active = False
            self.codec = None
            return output
        if decoded != INPUT_SAMPLES or trim > decoded or valid > decoded - trim:
            raise ValueError("Opus有效样本范围无效")
        packet_samples(payload, 16000)
        if len(self.frames) >= 4 or self.position + valid > 0xFFFFFFFF or self.sequence == 0xFFFFFFFF:
            raise ValueError("Opus输入缓存或计数超限")
        self.discarded += decoded - valid
        if self.discarded > 16000 + 2 * INPUT_SAMPLES:
            raise ValueError("Opus单段预热与填充样本超过上限")
        self.frames.append((trim, valid))
        self.pending.extend(self.codec.decode(payload))
        self.position += valid
        self.sequence += 1
        return self._drain()

    def require_end(self):
        if self.active or self.frames or self.pending:
            raise ValueError("音频边界前必须先发送Opus结束包")


class OpusOutput:
    def __init__(self):
        self.stream = 0
        self.active = False
        self.codec = None
        self.sequence = self.position = self.input_samples = 0

    def begin(self):
        if self.stream == 0xFFFFFFFF:
            raise ValueError("Opus流编号耗尽，需重新连接")
        self.stream += 1
        self.sequence = self.position = self.input_samples = 0
        self.codec = OpusBridge(frame_duration=20, bit_rate=40000, constant_bit_rate=True)
        self.active = True

    def cancel(self):
        self.active = False
        self.codec = None

    def encode(self, pcm=b"", *, finish=False):
        if not self.active or len(pcm) % 2 or len(pcm) > MAX_PCM_MESSAGE_BYTES:
            raise ValueError("Opus输出必须位于已声明的播放流内，PCM须有界且16bit对齐")
        self.input_samples += len(pcm) // 2
        if self.input_samples > 0xFFFFFFFF:
            raise ValueError("Opus输出样本计数超限")
        packets = []
        for packet in self.codec.encode_packets(pcm, finish=finish):
            payload = bytes(packet)
            packet_samples(payload, 24000)
            if packet.pts is None or packet.time_base is None:
                raise ValueError("Opus编码器缺少样本时间戳，不能裁剪首尾")
            start = packet.pts * packet.time_base * 24000
            if start.denominator != 1:
                raise ValueError("Opus时间戳不在样本网格上")
            start = int(start)
            trim = min(OUTPUT_SAMPLES, max(0, -start))
            valid = max(0, min(start + OUTPUT_SAMPLES, self.input_samples) - max(start, 0))
            if finish and valid == 0 and start >= self.input_samples:
                continue
            if max(start, 0) != self.position or self.sequence == 0xFFFFFFFF:
                raise ValueError("Opus编码样本存在缺口或计数超限")
            packets.append(pack_audio(self.stream, self.sequence, self.position, OUTPUT_SAMPLES, trim, valid,
                                      RESET if self.sequence == 0 else 0, payload))
            self.position += valid
            self.sequence += 1
        if finish:
            if self.position != self.input_samples:
                raise ValueError("Opus输出有效样本数与原PCM不符")
            if self.sequence:
                packets.append(pack_audio(self.stream, self.sequence, self.position, 0, 0, 0, END))
            self.cancel()
        return packets


class NativeOpusSocket:
    """只在握手明确选择xy_opus_v1时，将物理Opus连接映射为现有PCM接口。"""

    def __init__(self, socket):
        self.socket = socket
        self.input = OpusInput()
        self.output = OpusOutput()
        self.playback_id = None

    def __getattr__(self, name):
        return getattr(self.socket, name)

    def __aiter__(self):
        return self

    async def __anext__(self):
        while True:
            message = await self.socket.__anext__()
            if message.type == WSMsgType.BINARY:
                pcm = self.input.decode(message.data)
                if pcm:
                    return WSMessage(WSMsgType.BINARY, pcm, "")
                continue
            if message.type == WSMsgType.TEXT:
                event = json.loads(message.data)
                if not isinstance(event, dict):
                    raise ValueError("原生控制消息必须为对象")
                if event.get("type") in {"wake_clip_end", "session_end", "abort"} or event.get("type") == "listen" and event.get("state") in {"stop", "start", "detect"}:
                    self.input.require_end()
                if event.get("type") == "hello":
                    validate_audio_params(event.get("audio_params"))
                    if event["audio_params"].get("format") != "opus":
                        raise ValueError("连接中不能更换音频格式")
            return message

    async def send_bytes(self, payload):
        for packet in self.output.encode(payload):
            await self.socket.send_bytes(packet)

    async def send_json(self, payload):
        kind = payload.get("type")
        start = kind == "media_start" or kind == "tts" and payload.get("state") == "start"
        finish = kind == "media_end" or kind == "tts" and payload.get("state") == "stop"
        cancel = kind in {"interrupt", "media_stop"}
        if start:
            self.output.begin()
            self.playback_id = payload.get("playback_id")
        elif (finish or cancel) and payload.get("playback_id") == self.playback_id:
            if cancel:
                self.output.cancel()
            elif self.output.active:
                for packet in self.output.encode(finish=True):
                    await self.socket.send_bytes(packet)
            self.playback_id = None
        if kind == "hello":
            payload = {**payload, "audio_params": {"format": "opus", "sample_rate": 24000, "channels": 1,
                                                  "frame_duration": 20, "framing": FRAMING, "bit_rate": 40000}}
        elif kind == "media_start":
            payload = {**payload, "format": "opus", "framing": FRAMING, "bit_rate": 40000}
        await self.socket.send_json(payload)


def validate_audio_params(params):
    if not isinstance(params, dict) or params.get("sample_rate") != 16000 or params.get("channels") != 1:
        raise ValueError("原生上行需要16kHz单声道")
    if params.get("format") == "pcm_s16le":
        return
    if params.get("format") != "opus" or params.get("frame_duration") != 20 or params.get("framing") != FRAMING:
        raise ValueError("原生Opus需要20ms和xy_opus_v1帧协议")


def native_audio_socket(socket, params):
    validate_audio_params(params)
    return NativeOpusSocket(socket) if params["format"] == "opus" else socket

"""小智 v1 Opus 帧与阿里 PCM 的转换，状态仅属于当前会话。"""

from fractions import Fraction

import av


class OpusBridge:
    def __init__(self, *, frame_duration=60, bit_rate=24000, constant_bit_rate=False):
        self.frame_duration = frame_duration
        self.bit_rate = bit_rate
        self.constant_bit_rate = constant_bit_rate
        self.decoder = av.CodecContext.create("opus", "r")
        self.resampler = av.AudioResampler(format="s16", layout="mono", rate=16000)
        self.encoder = av.CodecContext.create("libopus", "w")
        self.encoder.sample_rate = 24000
        self.encoder.layout = "mono"
        self.encoder.format = "s16"
        self.encoder.bit_rate = bit_rate
        self.encoder.options = {"application": "voip", "frame_duration": str(frame_duration)}
        if constant_bit_rate:
            self.encoder.options["vbr"] = "off"
        self.encoder.open()
        self.pending = bytearray()
        self.samples_sent = 0

    def decode(self, packet):
        output = bytearray()
        for frame in self.decoder.decode(av.Packet(packet)):
            for converted in self.resampler.resample(frame):
                output.extend(bytes(converted.planes[0])[:converted.samples * 2])
        return bytes(output)

    def encode(self, pcm, finish=False):
        return [bytes(packet) for packet in self.encode_packets(pcm, finish=finish)]

    def encode_packets(self, pcm, finish=False):
        self.pending.extend(pcm)
        frame_bytes = 24000 * self.frame_duration // 1000 * 2
        if finish and self.pending:
            self.pending.extend(b"\x00" * ((-len(self.pending)) % frame_bytes))
        packets = []
        while len(self.pending) >= frame_bytes:
            frame = av.AudioFrame(format="s16", layout="mono", samples=frame_bytes // 2)
            frame.sample_rate = 24000
            frame.time_base = Fraction(1, 24000)
            frame.pts = self.samples_sent
            frame.planes[0].update(bytes(self.pending[:frame_bytes]))
            del self.pending[:frame_bytes]
            self.samples_sent += frame.samples
            packets.extend(self.encoder.encode(frame))
        if finish:
            packets.extend(self.encoder.encode(None))
        return packets

    def finish_input(self):
        output = bytearray()
        for frame in self.decoder.decode(None):
            for converted in self.resampler.resample(frame):
                output.extend(bytes(converted.planes[0])[:converted.samples * 2])
        for converted in self.resampler.resample(None):
            output.extend(bytes(converted.planes[0])[:converted.samples * 2])
        return bytes(output)

    def reset_output(self):
        # 每轮重新建立编码器，丢弃被打断回复的内部延迟帧。
        replacement = OpusBridge(frame_duration=self.frame_duration, bit_rate=self.bit_rate, constant_bit_rate=self.constant_bit_rate)
        self.encoder = replacement.encoder
        self.pending.clear()
        self.samples_sent = 0

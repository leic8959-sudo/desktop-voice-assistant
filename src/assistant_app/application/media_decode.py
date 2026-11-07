"""固定子进程解码入口：stdin 音频 -> stdout PCM24；不接触网络或配置。"""
from __future__ import annotations

import json
import sys
import traceback

SAMPLE_RATE = 24000
MAX_SECONDS = 1800
MAX_INPUT_BYTES = 512 * 1024 * 1024
FORMATS = {"wav", "flac", "mp3", "ogg", "aac", "s16le"}
CODECS = {"pcm_s16le", "pcm_s24le", "pcm_s32le", "pcm_f32le", "pcm_f64le", "flac", "mp3", "mp3float", "vorbis", "opus", "aac", "aac_latm"}


class _BoundedInput:
    def __init__(self, stream):
        self.stream, self.count = stream, 0

    def read(self, size):
        if size < 0 or size > 1024 * 1024:
            raise ValueError("解码读取块超过限制")
        data = self.stream.read1(size)
        self.count += len(data)
        if self.count > MAX_INPUT_BYTES:
            raise ValueError("压缩输入超过限制")
        return data

    def readable(self):
        return True

    def seekable(self):
        return False


def decode(format_name, skip_samples, input_rate, input_channels):
    import av

    if format_name not in FORMATS or not 0 <= skip_samples < SAMPLE_RATE * MAX_SECONDS:
        raise ValueError("解码参数无效")
    av.logging.set_level(av.logging.PANIC)
    options = {"protocol_whitelist": "pipe", "enable_drefs": "0", "use_absolute_path": "0", "probesize": "262144", "analyzeduration": "5000000", "max_streams": "4"}
    if format_name == "s16le":
        if not 8000 <= input_rate <= 192000 or not 1 <= input_channels <= 8:
            raise ValueError("原始PCM参数无效")
        options.update(sample_rate=str(input_rate), ch_layout="mono" if input_channels == 1 else str(input_channels) + "c")

    def reject_secondary_io(*_arguments):
        raise PermissionError("解码器禁止打开二次文件或网络资源")

    raw = _BoundedInput(sys.stdin.buffer)
    samples, packets, frames, written = 0, 0, 0, 0
    with av.open(raw, mode="r", format=format_name, options=options, io_open=reject_secondary_io, buffer_size=32768) as container:
        audio_streams = list(container.streams.audio)
        if len(audio_streams) != 1 or len(container.streams) > 4:
            raise ValueError("必须仅有一条音轨")
        audio = audio_streams[0]
        context = audio.codec_context
        if context.name not in CODECS:
            raise ValueError("编码不在允许列表")
        if not 8000 <= context.sample_rate <= 192000 or not 1 <= len(context.layout.channels) <= 8:
            raise ValueError("音轨采样率或声道超过限制")
        context.thread_count = 1
        # HTTP WAV 常用未知长度占位头；最终上限始终按实际解码样本实施。
        if format_name != "wav" and audio.duration is not None and audio.time_base is not None and float(audio.duration * audio.time_base) > MAX_SECONDS + 1:
            raise ValueError("音轨超过30分钟")
        resampler = av.AudioResampler(format="s16", layout="mono", rate=SAMPLE_RATE)

        def emit(frame):
            nonlocal samples, written
            if not 0 < frame.samples <= 262144:
                raise ValueError("输出帧超过限制")
            previous = samples
            samples += frame.samples
            if samples > SAMPLE_RATE * MAX_SECONDS:
                raise ValueError("解码输出超过30分钟")
            offset = max(0, skip_samples - previous)
            if offset < frame.samples:
                data = bytes(frame.planes[0])[offset * 2:frame.samples * 2]
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
                written += len(data)

        for packet in container.demux(audio):
            packets += 1
            if packets > 300000 or packet.size > 1024 * 1024:
                raise ValueError("音频包超过限制")
            for frame in packet.decode():
                frames += 1
                if frames > 300000 or not 0 < frame.samples <= 262144 or not 8000 <= frame.sample_rate <= 192000 or not 1 <= len(frame.layout.channels) <= 8:
                    raise ValueError("输入音频帧超过限制")
                for converted in resampler.resample(frame):
                    emit(converted)
        for converted in resampler.resample(None):
            emit(converted)
        if not written:
            raise ValueError("音轨没有目标位置后的音频")


if __name__ == "__main__":
    # 固定纯数字/枚举参数；不读取环境凭据，不把解码器错误原文写到日志。
    try:
        if len(sys.argv) != 5:
            raise ValueError("入口参数无效")
        decode(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
    except BrokenPipeError:
        raise SystemExit(3)
    except Exception as error:
        # 仅固定结构的栈帧，不输出异常文本、链式异常或媒体元数据。
        frames = [[item.filename[:512], item.lineno, item.name[:128]] for item in traceback.extract_tb(error.__traceback__)]
        truncated = False
        while True:
            diagnostic = "MEDIA_DECODE_ERROR=" + json.dumps({"error_type": type(error).__name__, "frames": frames, "truncated": truncated}, ensure_ascii=True)
            if len(diagnostic.encode("utf-8")) <= 8191:
                break
            frames.pop(0)
            truncated = True
        sys.stderr.write(diagnostic + "\n")
        raise SystemExit(2)

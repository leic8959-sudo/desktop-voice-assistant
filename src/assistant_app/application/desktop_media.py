"""Windows PCM 播放器；只接受服务生成的本地路径，不接受 URL 或命令。"""

import ctypes
import uuid


class WindowsMediaPlayer:
    def __init__(self):
        self.alias = "xiaoyanzi_" + uuid.uuid4().hex
        self.opened = False

    def _command(self, command):
        output = ctypes.create_unicode_buffer(256)
        library = ctypes.windll.winmm
        library.mciSendStringW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint, ctypes.c_void_p]
        library.mciSendStringW.restype = ctypes.c_uint
        error = library.mciSendStringW(command, output, len(output), None)
        if error:
            detail = ctypes.create_unicode_buffer(256)
            library.mciGetErrorStringW(error, detail, len(detail))
            raise RuntimeError("Windows本地音频操作失败：" + (detail.value or str(error)))
        return output.value

    def open(self, path, position_ms=0):
        self.close()
        # 路径只能来自受限媒体目录或本地噪音生成器，双引号不能进入MCI指令。
        filename = str(path)
        if '"' in filename or "\n" in filename or "\r" in filename:
            raise ValueError("本地音源路径无效")
        self._command(f'open "{filename}" type waveaudio alias {self.alias}')
        self.opened = True
        try:
            self._command(f"set {self.alias} time format milliseconds")
            duration = int(self._command(f"status {self.alias} length"))
            if not 0 <= position_ms < duration:
                raise ValueError("音源已播放完毕，没有可恢复的位置")
            self._command(f"play {self.alias} from {int(position_ms)}")
            if self._command(f"status {self.alias} mode") != "playing":
                raise RuntimeError("Windows未确认音轨进入播放状态")
            return duration
        except Exception:
            self.close()
            raise

    def position(self):
        return int(self._command(f"status {self.alias} position")) if self.opened else None

    def mode(self):
        return self._command(f"status {self.alias} mode") if self.opened else "closed"

    def restart(self):
        self._command(f"play {self.alias} from 0")

    def close(self):
        if self.opened:
            self._command(f"close {self.alias}")
            self.opened = False

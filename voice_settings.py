"""千问3.8实时音色白名单及不含密钥的本地偏好持久化。"""

import json
import os
from pathlib import Path

VOICE_SOURCE = "https://help.aliyun.com/zh/model-studio/omni-voice-list#qwen38-voices"
VOICES = [
    {"id": "Tina", "name": "甜甜", "description": "温暖甜美"},
    {"id": "Serena", "name": "苏瑶", "description": "温柔女声"},
    {"id": "Maia", "name": "四月", "description": "知性温柔"},
    {"id": "Mia", "name": "舒然", "description": "舒缓生活感"},
    {"id": "Cici", "name": "绵绵", "description": "活泼软糯"},
    {"id": "Raymond", "name": "林川野", "description": "清亮男声"},
    {"id": "Evan", "name": "江晨", "description": "年轻男声"},
    {"id": "Zane", "name": "泽恩", "description": "磁性男声"},
    {"id": "Andre", "name": "安德雷", "description": "自然沉稳"},
    {"id": "Katerina", "name": "卡捷琳娜", "description": "成熟有韵律"},
    {"id": "Ryan", "name": "甜茶", "description": "富有表现力"},
    {"id": "Cindy", "name": "林欣宜", "description": "台湾口音女声"},
]
VOICE_IDS = frozenset(item["id"] for item in VOICES)


class VoiceSettings:
    def __init__(self, path, config):
        self.path = Path(path)
        self.config = config
        if self.path.exists():
            payload = json.loads(self.path.read_text(encoding="utf-8"))
            value = payload.get("voice") if isinstance(payload, dict) else None
            self.validate(value)
            config["aliyun_voice"] = value

    @staticmethod
    def validate(value):
        if not isinstance(value, str) or value not in VOICE_IDS:
            raise ValueError("请选择列表中的受支持音色")

    def save(self, value):
        self.validate(value)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        temporary = self.path.with_suffix(".tmp")
        # 偏好单独保存，不重写包含云端密钥的config.local.json。
        with temporary.open("w", encoding="utf-8") as output:
            json.dump({"voice": value}, output, ensure_ascii=False)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, self.path)
        self.config["aliyun_voice"] = value

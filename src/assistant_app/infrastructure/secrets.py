"""默认密钥提供方：环境变量优先，便于后续替换 Windows/Docker 密钥存储。"""

from __future__ import annotations

import os


class EnvironmentSecretProvider:
    def get(self, name: str) -> str | None:
        return os.getenv(name)

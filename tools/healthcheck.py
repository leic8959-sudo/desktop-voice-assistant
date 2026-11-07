"""容器健康检查：只验证本机 HTTP 服务是否能响应。"""

from __future__ import annotations

import json
import urllib.request


def main() -> int:
    try:
        with urllib.request.urlopen("http://127.0.0.1:8765/health", timeout=4) as response:
            return 0 if response.status < 500 else 1
    except Exception as error:  # pragma: no cover - 由容器运行时调用
        print(json.dumps({"healthcheck_error": type(error).__name__}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

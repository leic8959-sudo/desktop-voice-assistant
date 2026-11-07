"""模拟导航语音演示：触发网关向唯一在线设备按序 TTS 播报导航指令。

用法：python tools/navigation_demo.py [--host 127.0.0.1] [--port 8765]
全程约 1 分钟；播报期间设备点云进入"正在回答"形态，不再下发屏幕卡片。
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load_config() -> dict:
    for name in ("config.local.json", "config.example.json"):
        path = ROOT / name
        if path.exists():
            return json.loads(path.read_text(encoding="utf-8"))
    raise SystemExit("未找到 config.local.json / config.example.json")


def api(base: str, token: str, path: str, timeout: int, method: str = "GET"):
    request = urllib.request.Request(
        f"{base}{path}",
        method=method,
        data=b"{}" if method == "POST" else None,
        headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description="模拟导航语音演示（设备 TTS 播报）")
    parser.add_argument("--host", default=None, help="网关地址，默认取配置 host（0.0.0.0 视为 127.0.0.1）")
    parser.add_argument("--port", type=int, default=None, help="网关端口，默认取配置 port")
    args = parser.parse_args()

    config = load_config()
    host = args.host or ("127.0.0.1" if config.get("host") in ("0.0.0.0", "", None) else config["host"])
    port = args.port or config.get("port", 8765)
    base = f"http://{host}:{port}"
    token = str(config.get("device_token", ""))
    if not token or token == "REPLACE_WITH_RANDOM_DEVICE_TOKEN":
        raise SystemExit("配置中 device_token 未设置，无法通过管理员鉴权")

    try:
        with urllib.request.urlopen(f"{base}/health", timeout=4) as response:
            if response.status >= 500:
                raise SystemExit(f"网关异常响应 {response.status}")
    except Exception as error:
        raise SystemExit(f"网关未运行或不可达（{base}）：{type(error).__name__}；请先启动 gateway.py")

    try:
        devices = api(base, token, "/api/devices", 8).get("items", [])
    except urllib.error.HTTPError as error:
        raise SystemExit(f"设备查询失败 HTTP {error.status}：{error.read().decode('utf-8', errors='replace')[:200]}")
    if not devices:
        raise SystemExit("没有在线设备：请确认设备已开机联网并完成配对（控制台 > 设备 应显示在线）")

    print(f"设备在线：{', '.join(str(item.get('name')) for item in devices)}；开始语音导航演示（7 句，约 1 分钟）。")
    try:
        result = api(base, token, "/api/workflows/navigation-demo", 240, method="POST")
    except urllib.error.HTTPError as error:
        detail = error.read().decode("utf-8", errors="replace")[:300]
        raise SystemExit(f"演示失败 HTTP {error.status}：{detail}")
    except Exception as error:
        raise SystemExit(f"演示请求失败：{type(error).__name__}: {error}")

    for item in result.get("deliveries", []):
        status = item.get("status")
        extra = f"（{item.get('reason')}）" if item.get("reason") else f"（{item.get('audio_seconds', 0):.1f} 秒音频）" if status == "sent" else ""
        print(f"第 {item.get('step')} 句 {status} {extra}{item.get('text', '')}")
    status = result.get("status")
    if status == "completed":
        print("演示完成：语音播报已全部发送。若设备没声音，检查设备音量/会话静音设置与 TTS 配置（aliyun_api_key/chat_tts_model/chat_tts_voice）。")
    else:
        print(f"演示在第 {result.get('failed_step')} 步中止（{result.get('reason')}）；已播报 {result.get('completed_steps')} 句。")
    return 0 if status == "completed" else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())

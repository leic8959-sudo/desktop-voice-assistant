"""离线检查，不启动服务、不录音、不调用计费接口、不烧录。"""

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default=str(ROOT / "config.local.json"))
    args = parser.parse_args()
    results = []
    def check(name, ok, detail):
        results.append({"check": name, "ok": ok, "detail": detail})
    check("python", sys.version_info >= (3, 11), sys.version.split()[0])
    for name in ("aiohttp", "av"):
        check(name, importlib.util.find_spec(name) is not None, "已安装" if importlib.util.find_spec(name) else "未安装；由用户安装 requirements.txt")
    if all(row["ok"] for row in results):
        try:
            from audio_codec import OpusBridge
            codec = OpusBridge()
            packets = codec.encode(b"\x00" * 2880, finish=True)
            decoded = b"".join(codec.decode(packet) for packet in packets)
            check("opus_roundtrip", bool(packets) and bool(decoded), "已实际编码和解码，不代表硬件音频通过")
        except Exception:
            import traceback
            traceback.print_exc()
            check("opus_roundtrip", False, "编解码异常，见完整堆栈")
    try:
        from gateway import load_config
        config = load_config(args.config)
        check("config_structure", True, "结构正确；未输出密钥")
        ready = all(config.get(key) and "REPLACE" not in config[key] for key in ("aliyun_api_key", "aliyun_workspace_id"))
        check("aliyun_credentials_filled", ready, "仅判断已填写；未联网验证权限、余额和模型可用性")
        check("lan_address", "127.0.0.1" not in config["public_base_url"], "设备接入需电脑局域网IP；仅浏览器测试可用127.0.0.1")
    except Exception as error:
        check("config", False, f"{type(error).__name__}：配置缺失或不完整，请本地检查")
    manifest_file = ROOT / "evidence" / "sources.json"
    if manifest_file.exists():
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
        for artifact in manifest["artifacts"]:
            path = ROOT / artifact["file"]
            digest = "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else "missing"
            check(artifact["board"], digest == artifact["digest"], "SHA-256匹配" if digest == artifact["digest"] else "未下载或散列不匹配")
    else:
        check("firmware_manifest", False, "尚未准备固件清单")
    print(json.dumps({"checks": results, "hardware_verified": False, "cloud_verified": False}, ensure_ascii=False, indent=2))
    return 0 if all(row["ok"] for row in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())

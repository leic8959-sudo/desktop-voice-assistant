"""生成本地配置和独立随机令牌，不接触阿里账号，不覆盖现有配置。"""

import argparse
import ipaddress
import json
from pathlib import Path
import secrets

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lan-ip", default="127.0.0.1", help="Windows电脑的局域网IPv4地址")
    args = parser.parse_args()
    address = ipaddress.IPv4Address(args.lan_ip)
    if not address.is_private or address.is_unspecified or address.is_multicast:
        raise ValueError("请使用私有局域网地址或127.0.0.1")
    target = ROOT / "config.local.json"
    if target.exists():
        raise FileExistsError("config.local.json 已存在；请编辑该文件，不覆盖已有密钥")
    config = json.loads((ROOT / "config.example.json").read_text(encoding="utf-8"))
    config["device_token"] = secrets.token_urlsafe(32)
    config["provisioning_token"] = secrets.token_urlsafe(32)
    config["public_base_url"] = f"http://{address}:8765"
    config["host"] = "127.0.0.1" if address.is_loopback else "0.0.0.0"
    target.write_text(json.dumps(config, ensure_ascii=False, indent=2), encoding="utf-8")
    print("已生成 config.local.json；令牌未输出。请在本地编辑阿里 API Key 和北京业务空间ID。")


if __name__ == "__main__":
    main()

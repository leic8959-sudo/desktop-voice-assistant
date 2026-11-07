"""下载固定版本的上游固件及适配证据；不烧录、不安装依赖。"""

import base64
import hashlib
import json
import logging
import urllib.request
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REPOSITORY = "78/xiaozhi-esp32"
TAG = "v2.5.2"
BOARD_NAMES = ["waveshare-esp32-s3-touch-amoled-1.8", "waveshare-esp32-s3-touch-amoled-1.8-v2"]
SOURCE_PATHS = [
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8/config.h",
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8/config.json",
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8/esp32-s3-touch-amoled-1.8.cc",
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8-v2/config.h",
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8-v2/config.json",
    "main/boards/waveshare/esp32-s3-touch-amoled-1.8-v2/esp32-s3-touch-amoled-1.8-v2.cc",
    "main/ota.cc", "main/protocols/websocket_protocol.cc", "main/Kconfig.projbuild",
    "main/application.cc", "main/mcp_server.cc", "docs/websocket.md", "docs/mcp-protocol.md",
    "README_zh.md", "LICENSE",
]


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "desktop-voice-assistant-preflight"})
    with urllib.request.urlopen(request, timeout=20) as response:
        return response.read()


def save_source(path):
    url = f"https://api.github.com/repos/{REPOSITORY}/contents/{path}?ref={TAG}"
    payload = json.loads(fetch(url))
    data = base64.b64decode(payload["content"])
    target = ROOT / "evidence" / "upstream" / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data)
    return {"path": path, "url": payload["html_url"], "sha256": hashlib.sha256(data).hexdigest()}


def main():
    release = json.loads(fetch(f"https://api.github.com/repos/{REPOSITORY}/releases/tags/{TAG}"))
    evidence = ROOT / "evidence"
    evidence.mkdir(exist_ok=True)
    (evidence / "release.json").write_text(json.dumps(release, ensure_ascii=False, indent=2), encoding="utf-8")
    artifacts = []
    for board in BOARD_NAMES:
        name = f"{TAG}_{board}.zip"
        asset = next(item for item in release["assets"] if item["name"] == name)
        target = ROOT / "firmware" / "prebuilt" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        item = {"board": board, "file": str(target.relative_to(ROOT)), "url": asset["browser_download_url"], "digest": asset.get("digest")}
        try:
            data = target.read_bytes() if target.exists() else fetch(asset["browser_download_url"])
            digest = "sha256:" + hashlib.sha256(data).hexdigest()
            if digest != asset.get("digest"):
                raise ValueError(f"上游散列不匹配，停止：{name}")
            target.write_bytes(data)
            item.update({"size": len(data), "downloaded": True})
        except Exception:
            logging.exception("固件下载失败：%s", name)
            item["downloaded"] = False
        artifacts.append(item)
    with ThreadPoolExecutor(max_workers=4) as pool:
        futures = [(path, pool.submit(save_source, path)) for path in SOURCE_PATHS]
        sources = []
        for path, future in futures:
            try:
                sources.append(future.result())
            except Exception:
                logging.exception("源码证据下载失败：%s", path)
    manifest = {"checked_date": "2026-10-26", "repository": REPOSITORY, "tag": TAG,
                "release_url": release["html_url"], "artifacts": artifacts, "sources": sources}
    target = evidence / "sources.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"tag": TAG, "firmware": artifacts, "sources": len(sources)}, ensure_ascii=False))


if __name__ == "__main__":
    main()

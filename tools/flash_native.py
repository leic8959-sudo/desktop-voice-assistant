"""使用已构建的地址清单烧录自有固件；需要用户明确授权和已有完整备份。"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--backup", type=Path, required=True)
    args = parser.parse_args()
    backup = args.backup.resolve()
    if not backup.is_file() or backup.stat().st_size != 16 * 1024 * 1024:
        raise ValueError("必须先保存完整16MB设备备份")
    build = (ROOT / "firmware/assistant_espidf/build").resolve()
    manifest = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    if manifest.get("extra_esptool_args", {}).get("chip") != "esp32s3":
        raise ValueError("烧录目标不是ESP32-S3")
    artifacts = []
    for address, relative in manifest["flash_files"].items():
        start = int(address, 0)
        path = (build / relative).resolve()
        if not path.is_relative_to(build) or not path.is_file():
            raise ValueError("产物路径无效")
        size = path.stat().st_size
        if start < 0 or start + size > 16 * 1024 * 1024 or (start < 0xD000 and start + size > 0x9000):
            raise ValueError("写入范围超界或覆盖NVS配网配置")
        artifacts.append({"address": address, "path": str(path), "size": size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    artifacts.sort(key=lambda item: int(item["address"], 0))
    for previous, current in zip(artifacts, artifacts[1:]):
        if int(previous["address"], 0) + previous["size"] > int(current["address"], 0):
            raise ValueError("烧录产物重叠")
    settings = manifest["flash_settings"]
    tool = ROOT / "firmware/flash_tool/esptool-standalone/esptool-windows-amd64/esptool.exe"
    command = [str(tool), "--chip", "esp32s3", "--port", args.port, "--baud", "921600", "--before", "usb-reset", "--after", "watchdog-reset", "--connect-attempts", "2", "write-flash", "--no-progress", "--flash-mode", settings["flash_mode"], "--flash-size", settings["flash_size"], "--flash-freq", settings["flash_freq"]]
    for artifact in artifacts:
        command.extend((artifact["address"], artifact["path"]))
    log = ROOT / "evidence/native-flash-20261029.log"
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            print(line, end="", flush=True)
            output.write(line)
        code = process.wait()
    result = {"at": datetime.now(timezone.utc).isoformat(), "port": args.port, "exit_code": code, "backup": str(backup), "backup_sha256": hashlib.sha256(backup.read_bytes()).hexdigest(), "artifacts": artifacts, "nvs_preserved": True, "boot_verified": False, "audio_verified": False}
    (ROOT / "evidence/native-flash-result-20261029.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    if code:
        raise RuntimeError("烧录失败；原备份仍保留，不能判定设备已正常启动")


if __name__ == "__main__":
    main()

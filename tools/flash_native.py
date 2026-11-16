"""使用已构建的地址清单烧录自有固件；需要用户明确授权和已有完整备份。"""

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

if __package__:
    from .board_profiles import load_board_profiles
else:
    from board_profiles import load_board_profiles

ROOT = Path(__file__).resolve().parents[1]


def main():
    profiles = load_board_profiles()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--board", choices=tuple(profiles), default="waveshare-amoled-1.8")
    args = parser.parse_args()
    profile = profiles[args.board]
    flash_bytes = profile["flash_size_mb"] * 1024 * 1024
    preserved_ranges = profile.get("preserved_flash_ranges", [{"offset": 0x9000, "size": 0x4000}])
    backup = args.backup.resolve()
    if not backup.is_file() or backup.stat().st_size != flash_bytes:
        raise ValueError(f"必须先保存该板完整{profile['flash_size_mb']}MB设备备份")
    build = (ROOT / "firmware/assistant_espidf/build").resolve()
    if not profile["legacy"]:
        build = build / args.board
    target_path = build / "board-target.json"
    if target_path.exists():
        target = json.loads(target_path.read_text(encoding="utf-8"))
        if target.get("board") != args.board or target.get("project_name") != profile["project_name"] or target.get("target", profile["target"]) != profile["target"]:
            raise ValueError("构建产物与指定板型不一致，未烧录")
    elif not profile["legacy"]:
        raise ValueError("新板型产物缺少 board-target.json，未烧录")
    manifest = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
    if manifest.get("extra_esptool_args", {}).get("chip") != profile["target"]:
        raise ValueError("烧录芯片目标与指定板型不一致")
    artifacts = []
    for address, relative in manifest["flash_files"].items():
        start = int(address, 0)
        path = (build / relative).resolve()
        if not path.is_relative_to(build) or not path.is_file():
            raise ValueError("产物路径无效")
        size = path.stat().st_size
        if start < 0 or size <= 0 or start + size > flash_bytes:
            raise ValueError("写入范围超界或产物为空")
        erase_start = start // 4096 * 4096
        erase_end = (start + size + 4095) // 4096 * 4096
        if any(erase_start < region["offset"] + region["size"] and erase_end > region["offset"] for region in preserved_ranges):
            raise ValueError("写入的擦除扇区覆盖NVS或板级保留信息，未烧录")
        artifacts.append({"address": address, "path": str(path), "size": size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    artifacts.sort(key=lambda item: int(item["address"], 0))
    for previous, current in zip(artifacts, artifacts[1:]):
        if int(previous["address"], 0) + previous["size"] > int(current["address"], 0):
            raise ValueError("烧录产物重叠")
    settings = manifest["flash_settings"]
    if settings["flash_size"] != f"{profile['flash_size_mb']}MB":
        raise ValueError("镜像Flash容量与指定板型不一致，未烧录")
    tool = ROOT / "firmware/flash_tool/esptool-standalone/esptool-windows-amd64/esptool.exe"
    command = [str(tool), "--chip", profile["target"], "--port", args.port, "--baud", "921600", "--before", "usb-reset", "--after", "watchdog-reset", "--connect-attempts", "2", "write-flash", "--no-progress", "--flash-mode", settings["flash_mode"], "--flash-size", settings["flash_size"], "--flash-freq", settings["flash_freq"]]
    for artifact in artifacts:
        command.extend((artifact["address"], artifact["path"]))
    print(f"指定板型: {args.board}；实物和完整备份必须确属该板型，芯片型号无法自动识别整板型号。")
    (ROOT / "evidence").mkdir(exist_ok=True)
    log = ROOT / f"evidence/native-flash-{args.board}.log"
    with log.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        for line in process.stdout:
            print(line, end="", flush=True)
            output.write(line)
        code = process.wait()
    result = {"at": datetime.now(timezone.utc).isoformat(), "board": args.board, "port": args.port, "exit_code": code, "backup": str(backup), "backup_sha256": hashlib.sha256(backup.read_bytes()).hexdigest(), "artifacts": artifacts, "nvs_preserved": True, "preserved_flash_ranges": preserved_ranges, "boot_verified": False, "audio_verified": False}
    (ROOT / f"evidence/native-flash-result-{args.board}.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    if code:
        raise RuntimeError("烧录失败；原备份仍保留，不能判定设备已正常启动")


if __name__ == "__main__":
    main()

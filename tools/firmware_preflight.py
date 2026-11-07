"""固件布局检查；不安装依赖、不构建、不操作串口。"""

import argparse
import csv
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "firmware" / "assistant_espidf"


def number(value):
    value = value.strip()
    if value.lower().endswith("m"):
        return int(value[:-1]) * 1024 * 1024
    if value.lower().endswith("k"):
        return int(value[:-1]) * 1024
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-only", action="store_true", help="只检查源码布局；不能视为构建通过")
    args = parser.parse_args()
    rows = []
    lines = [line for line in (PROJECT / "partitions.csv").read_text(encoding="utf-8").splitlines() if line.strip() and not line.lstrip().startswith("#")]
    previous_end = 0x9000
    for row in csv.reader(lines):
        name, kind, subtype, offset, size = [value.strip() for value in row[:5]]
        start, length = number(offset), number(size)
        alignment = 0x10000 if kind == "app" else 0x1000
        if start % alignment or length % 0x1000 or start < previous_end or start + length > 16 * 1024 * 1024:
            raise ValueError("固件分区未对齐、重叠或超出16MB：" + name)
        previous_end = start + length
        rows.append({"name": name, "offset": hex(start), "size": length})
    names = {row["name"] for row in rows}
    if not {"nvs", "otadata", "phy_init", "app0", "app1", "model"}.issubset(names):
        raise ValueError("缺少必要分区")
    config = (PROJECT / "sdkconfig.defaults").read_text(encoding="utf-8")
    for setting in ("CONFIG_SPIRAM_MODE_OCT=y", "CONFIG_SR_WN_WN9_HILEXIN=y", "CONFIG_MODEL_IN_FLASH=y"):
        if setting not in config:
            raise ValueError("缺少硬件或模型配置：" + setting)
    sdk = {tool: shutil.which(tool) for tool in ("idf.py", "cmake", "ninja")}
    docker_sdk = False
    if shutil.which("docker"):
        probe = subprocess.run(["docker", "image", "inspect", "espressif/idf:v5.5.2", "--format", "{{.Id}}"], capture_output=True, timeout=10)
        docker_sdk = probe.returncode == 0
    build_record = ROOT / "evidence" / "native-build-result-20261029.json"
    native_record = json.loads(build_record.read_text(encoding="utf-8")) if build_record.exists() else {}
    result = {"partition_layout_valid": True, "partitions": rows, "sdk": {key: bool(value) for key, value in sdk.items()}, "docker_sdk_available": docker_sdk, "compiled": native_record.get("build_success", False), "flashed": native_record.get("firmware_flashed", False)}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if args.source_only or all(sdk.values()) or docker_sdk else 2


if __name__ == "__main__":
    raise SystemExit(main())

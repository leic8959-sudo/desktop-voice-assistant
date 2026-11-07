#!/usr/bin/env python3
"""Generate the LVGL9 device font from an OFL CJK source font.

The script deliberately delegates bitmap encoding to lv_font_conv; it only
builds the GB2312 and UI character set and verifies the generated artifacts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import re
from fontTools.ttLib import TTFont
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets/fonts/NotoSansCJKsc-Regular.otf"
OUTPUT = ROOT / "firmware/assistant_espidf/main/ui_font.c"
HEADER = ROOT / "firmware/assistant_espidf/main/ui_font.h"
REPORT = ROOT / "assets/fonts/device_font_coverage.json"


def gb2312_chars() -> str:
    chars = set()
    for high in range(0xA1, 0xFF):
        for low in range(0xA1, 0xFF):
            try:
                char = bytes((high, low)).decode("gb2312")
                if "\u4e00" <= char <= "\u9fff":
                    chars.add(char)
            except UnicodeDecodeError:
                pass
    return "".join(sorted(chars))


def ui_chars() -> str:
    chars = set()
    for path in (ROOT / "firmware/assistant_espidf/main").glob("*.cpp"):
        text = path.read_text(encoding="utf-8")
        chars.update(ch for ch in text if ord(ch) > 0x7F)
    return "".join(sorted(chars))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--converter", type=Path, help="lv_font_conv.js or its executable")
    parser.add_argument("--size", type=int, default=16)
    parser.add_argument("--bpp", type=int, choices=(1, 2, 3, 4), default=2)
    args = parser.parse_args()
    converter = args.converter or (ROOT / "tools/font_converter/node_modules/lv_font_conv/lv_font_conv.js")
    if not SOURCE.exists():
        raise SystemExit(f"missing font source: {SOURCE}")
    if not converter.exists():
        raise SystemExit(f"missing lv_font_conv: {converter}; install it before generation")
    chars = gb2312_chars()
    chars += "".join(ch for ch in ui_chars() if ch not in chars)
    chars += "".join(chr(codepoint) for codepoint in range(0x20, 0x7F) if chr(codepoint) not in chars)
    chars += "".join(chr(codepoint) for codepoint in range(0x3000, 0x3040) if chr(codepoint) not in chars)
    chars += "".join(chr(codepoint) for codepoint in range(0xFF00, 0xFFF0) if chr(codepoint) not in chars)
    with TTFont(SOURCE) as font:
        cmap = set(font.getBestCmap() or {})
    requested_codepoints = {ord(char) for char in chars}
    core_codepoints = set(range(0x20, 0x7F)) | set(range(0x3000, 0x3040)) | set(range(0xFF01, 0xFF5F)) | set(range(0xFFE0, 0xFFE6)) | {ord(char) for char in gb2312_chars()} | {ord(char) for char in ui_chars()}
    source_missing = sorted(core_codepoints - cmap)
    command = [
        "node", str(converter), "--size", str(args.size), "--bpp", str(args.bpp),
        "--font", str(SOURCE), "-r", "0x20-0x7f", "-r", "0x3000-0x303f",
        "-r", "0xff00-0xffef", "--symbols", chars, "--format", "lvgl",
        "--no-kerning", "--force-fast-kern-format",
        "--lv-font-name", "ui_font", "-o", str(OUTPUT),
    ]
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(command, check=True)
    HEADER.write_text("#pragma once\n#include \"lvgl.h\"\nLV_FONT_DECLARE(ui_font);\n", encoding="ascii")
    generated_text = OUTPUT.read_text(encoding="utf-8")
    generated_codepoints = {int(value, 16) for value in re.findall(r"/\* U\+([0-9A-Fa-f]+)", generated_text)}
    generated_missing = sorted(requested_codepoints - generated_codepoints)
    report = {
        "source": "NotoSansCJKsc-Regular.otf",
        "license": "OFL.txt",
        "size_px": args.size,
        "bpp": args.bpp,
        "gb2312_codepoints": len(gb2312_chars()),
        "gb2312_hanzi": len(gb2312_chars()),
        "ranges": ["U+0020-U+007F", "U+3000-U+303F", "U+FF00-U+FFEF"],
        "ui_extra_codepoints": sorted(f"U+{ord(char):04X}" for char in ui_chars() if char not in gb2312_chars()),
        "source_missing_core_codepoints": [f"U+{codepoint:04X}" for codepoint in source_missing],
        "generated_missing_requested": [f"U+{codepoint:04X}" for codepoint in generated_missing],
        "optional_source_missing": [f"U+{codepoint:04X}" for codepoint in sorted(requested_codepoints - core_codepoints - cmap)],
        "output_bytes": OUTPUT.stat().st_size,
        "estimated_bitmap_bytes": len(re.findall(r"0x[0-9a-fA-F]{2}", OUTPUT.read_text(encoding="utf-8"))),
        "output_sha256": hashlib.sha256(OUTPUT.read_bytes()).hexdigest(),
    }
    REPORT.write_text(json.dumps(report, ensure_ascii=True, indent=2) + "\n", encoding="ascii")
    if report["estimated_bitmap_bytes"] >= 1_000_000:
        raise SystemExit(f"glyph bitmap is {report['estimated_bitmap_bytes']} bytes; use --bpp 1 or compression")


if __name__ == "__main__":
    main()

"""Read native board identities shared by build, flash and inventory tools."""

from __future__ import annotations

import json
import re
from pathlib import Path

PROFILE_PATH = Path(__file__).resolve().parents[1] / "firmware/assistant_espidf/native_board_profiles.json"


def required_terminal_feature_gaps(profile: dict) -> list[str]:
    """Known deliberate omissions in current source selectors, not runtime acceptance."""
    definitions = profile["compile_definitions"]
    gaps = []
    if "ASSISTANT_BOARD_GUITION=1" in definitions or "ASSISTANT_BOARD_INTERNAL_RAM=1" in definitions:
        gaps.extend(["local_wake", "wake_clip", "natural_barge_in", "media_wake_word"])
    if "ASSISTANT_BOARD_INTERNAL_RAM=1" in definitions:
        gaps.extend(["afe", "aec"])
    if "ASSISTANT_BOARD_GUITION=1" in definitions:
        gaps.append("dual_app_ota")
    return gaps


def load_board_profiles(path: Path = PROFILE_PATH) -> dict[str, dict]:
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    if payload.get("schema_version") != 1 or not isinstance(payload.get("profiles"), dict):
        raise ValueError("Unsupported native board profile schema")
    profiles = payload["profiles"]
    if not 1 <= len(profiles) <= 256:
        raise ValueError("Invalid native board profile count")
    projects, variants = set(), set()
    for name, profile in profiles.items():
        if not re.fullmatch(r"[a-z0-9][a-z0-9.-]{0,63}", name) or not isinstance(profile, dict):
            raise ValueError("Invalid native board identifier")
        project = profile.get("project_name")
        if not isinstance(project, str) or not re.fullmatch(r"[a-z][a-z0-9_]{0,30}", project) or project in projects:
            raise ValueError(f"Invalid/duplicate application identity: {name}")
        projects.add(project)
        if profile.get("target") not in {"esp32s3", "esp32p4", "esp32s31", "esp32c5"}:
            raise ValueError("Current native engine has source ports for ESP32-S3, ESP32-P4, ESP32-S31 and the reviewed ESP32-C5 board only")
        if type(profile.get("flash_size_mb")) is not int or profile["flash_size_mb"] not in {4, 8, 16, 32}:
            raise ValueError(f"Unsupported native flash capacity: {name}")
        preserved_ranges = profile.get("preserved_flash_ranges", [{"offset": 0x9000, "size": 0x4000}])
        if not isinstance(preserved_ranges, list) or not 1 <= len(preserved_ranges) <= 8:
            raise ValueError(f"Invalid preserved flash ranges: {name}")
        previous_end = 0
        for region in preserved_ranges:
            if not isinstance(region, dict) or type(region.get("offset")) is not int or type(region.get("size")) is not int:
                raise ValueError(f"Invalid preserved flash region: {name}")
            offset, size = region["offset"], region["size"]
            if offset < previous_end or offset % 4096 or size <= 0 or size % 4096 or offset + size > profile["flash_size_mb"] * 1024 * 1024:
                raise ValueError(f"Preserved flash region exceeds capacity, overlaps or is not sector aligned: {name}")
            previous_end = offset + size
        component = profile.get("bsp_component")
        if not isinstance(component, str) or not re.fullmatch(r"[a-z0-9_-]+", component):
            raise ValueError(f"Invalid native component: {name}")
        definitions = profile.get("compile_definitions")
        if not isinstance(definitions, list) or not definitions or any(
            not isinstance(value, str) or not re.fullmatch(r"ASSISTANT_BOARD_[A-Z0-9_]+=1", value)
            for value in definitions
        ):
            raise ValueError(f"Invalid implementation macro: {name}")
        if type(profile.get("legacy")) is not bool or profile.get("qspi_panel") not in {None, "co5300", "sh8601"}:
            raise ValueError(f"Invalid native profile options: {name}")
        variant = profile.get("upstream_variant")
        if profile["legacy"]:
            if variant is not None or not profile.get("legacy_reason"):
                raise ValueError("Legacy profile must be separate from released variant identities")
        elif not isinstance(variant, str) or not re.fullmatch(r"[a-z0-9.-]+", variant) or variant in variants:
            raise ValueError(f"Invalid/duplicate upstream variant: {name}")
        else:
            variants.add(variant)
    return profiles

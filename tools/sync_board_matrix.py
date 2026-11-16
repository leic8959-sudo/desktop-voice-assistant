"""Read the pinned public XiaoZhi release inventory; never build or install firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import tempfile
import urllib.request
from datetime import datetime, timezone
from pathlib import Path, PurePosixPath

if __package__:
    from .board_profiles import PROFILE_PATH, load_board_profiles, required_terminal_feature_gaps
else:
    from board_profiles import PROFILE_PATH, load_board_profiles, required_terminal_feature_gaps

ROOT = Path(__file__).resolve().parents[1]
SOURCE_SHA = "49b4380b3faa0ceadd28a3e78aaf0f311c46c57e"
RAW_BASE = f"https://raw.githubusercontent.com/78/xiaozhi-esp32/{SOURCE_SHA}/"
MANIFEST_URL = "https://github.com/78/xiaozhi-esp32/releases/download/v2.5.2/release-manifest.json"
DESTINATION = ROOT / "firmware/assistant_espidf/board_matrix_v2.5.2.json"

NEARBY_BOARDS = {
    "espressif/esp32-s3-box", "espressif/esp32-s3-box-lite",
    "espressif/esp32-s3-box-3", "m5stack/core-s3", "lilygo/t-circle-s3",
    "lilygo/t-circle-s3-v11", "waveshare/esp32-s3-touch-amoled-1.75",
    "waveshare/esp32-s3-touch-amoled-1.8", "waveshare/esp32-s3-touch-amoled-1.8-v2",
}


def fetch(url: str, source_directory: Path | None = None) -> bytes:
    if source_directory is not None:
        filenames = {
            MANIFEST_URL: "release-manifest.json",
            RAW_BASE + "main/Kconfig.projbuild": "Kconfig.projbuild",
            RAW_BASE + "main/CMakeLists.txt": "CMakeLists.txt",
            f"https://api.github.com/repos/78/xiaozhi-esp32/git/trees/{SOURCE_SHA}?recursive=1": "source-tree.json",
        }
        source_file = source_directory / filenames[url]
        if source_file.stat().st_size > 4_000_000:
            raise ValueError("Cached public source exceeds the inventory size limit")
        return source_file.read_bytes()
    request = urllib.request.Request(url, headers={"User-Agent": "xiaoyanzi-board-inventory"})
    with urllib.request.urlopen(request, timeout=30) as response:
        body = response.read(4_000_001)
    if len(body) > 4_000_000:
        raise ValueError("Public source exceeds the inventory size limit")
    return body


def source_path(board: str) -> str:
    path = PurePosixPath(board)
    if path.is_absolute() or ".." in path.parts or not re.fullmatch(r"[a-zA-Z0-9._/-]+", board):
        raise ValueError(f"Invalid public board path: {board!r}")
    return f"main/boards/{board}"


def batch_for(board: str, name: str, target: str) -> tuple[str, str]:
    # This is a work queue based on upstream names, not verified hardware specs.
    if board in NEARBY_BOARDS:
        return "B1", "explicit_nearby_board_queue"
    if re.search(r"4g|ml307|nt26|rndis|lora|camera|cam-|s3-cam|robot|electron-bot|otto|sparkbot|esp-hi|watcher", board + "/" + name, re.I):
        return "B5", "upstream_name_requires_special_device_research"
    if target != "esp32s3" or re.search(r"epaper|rlcd|rgb-matrix", board, re.I):
        return "B4", "different_chip_or_special_display_queue"
    if re.search(r"audio-board|atom.*echo|korvo", board, re.I):
        return "B3", "audio_board_name_hint_display_capability_unverified"
    return "B2", "remaining_s3_board_hardware_research_required"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-directory", type=Path, help="Optional directory of previously fetched public metadata (four documented filenames)")
    args = parser.parse_args()
    profiles = load_board_profiles()
    source_profiles = {profile["upstream_variant"]: name for name, profile in profiles.items() if not profile["legacy"]}
    previous = json.loads(DESTINATION.read_text(encoding="utf-8-sig")) if DESTINATION.exists() else {}
    previous_variants = {
        item["full_name"]: item for item in previous.get("release_variants", [])
    } if previous.get("source_sha") == SOURCE_SHA else {}
    research_path = DESTINATION.parent / "board_research_v2.5.2.json"
    research = json.loads(research_path.read_text(encoding="utf-8-sig"))
    if research.get("source_sha") != SOURCE_SHA:
        raise ValueError("Static board research refers to a different source commit")
    findings = {
        name: finding for finding in research["findings"] for name in finding["variants"]
    }
    cached_build_records = [
        (path, json.loads(path.read_text(encoding="utf-8-sig")))
        for path in sorted(DESTINATION.parent.glob("cached_build_attempts_*.json"))
    ]
    manifest_bytes = fetch(MANIFEST_URL, args.source_directory)
    manifest = json.loads(manifest_bytes)
    if manifest.get("source_sha") != SOURCE_SHA:
        raise ValueError("Release manifest source changed; do not silently move the pinned baseline")
    raw_variants = manifest.get("variants")
    if not isinstance(raw_variants, list) or not 1 <= len(raw_variants) <= 256:
        raise ValueError("Invalid release variant list")
    kconfig_bytes = fetch(RAW_BASE + "main/Kconfig.projbuild", args.source_directory)
    cmake_bytes = fetch(RAW_BASE + "main/CMakeLists.txt", args.source_directory)
    tree_bytes = fetch(f"https://api.github.com/repos/78/xiaozhi-esp32/git/trees/{SOURCE_SHA}?recursive=1", args.source_directory)
    tree = json.loads(tree_bytes)
    if tree.get("truncated") or tree.get("sha") != SOURCE_SHA:
        raise ValueError("Source tree is truncated or does not match the pinned commit")
    config_directories = sorted({
        str(PurePosixPath(item["path"]).parent).removeprefix("main/boards/")
        for item in tree["tree"]
        if item["path"].startswith("main/boards/") and item["path"].endswith("/config.json")
    })
    symbol_directories: dict[str, str] = {}
    for match in re.finditer(r'(?:if|elseif)\(([^)\n]+)\)\s*set\(BOARD_DIR "([^"]+)"\)', cmake_bytes.decode("utf-8")):
        for symbol in re.findall(r"CONFIG_BOARD_TYPE_(\w+)", match[1]):
            symbol_directories[symbol] = match[2]
    options = []
    for match in re.finditer(r"(?ms)^\s*config BOARD_TYPE_(\w+)\s*\n(.*?)(?=^\s*config |\Z)", kconfig_bytes.decode("utf-8")):
        prompt = re.search(r'\bbool\s+"([^"]+)"', match[2])
        options.append({
            "board_type": match[1],
            "label": prompt[1] if prompt else None,
            "board_directory": symbol_directories.get(match[1]),
            "target_constraints": sorted(set(value.lower() for value in re.findall(r"IDF_TARGET_(\w+)", match[2]))),
        })
    variants = []
    seen = set()
    for entry in raw_variants:
        board, name, full_name, target = (entry[key] for key in ("board", "name", "full_name", "target"))
        if full_name in seen or not re.fullmatch(r"[a-z0-9.-]+", full_name):
            raise ValueError("Duplicate or invalid release artifact identifier")
        seen.add(full_name)
        path = source_path(board)
        batch, reason = batch_for(board, name, target)
        profile = source_profiles.get(full_name)
        feature_gaps = required_terminal_feature_gaps(profiles[profile]) if profile else []
        variant = {
            "board_directory": board, "name": name, "full_name": full_name,
            "target": target, "source_url": RAW_BASE + path + "/config.json",
            "board_type_options": sorted(key for key, value in symbol_directories.items() if value == board),
            "batch": batch, "batch_basis": reason,
            "native_profile": profile,
            "status": "source_profile_present" if profile else "inventory_only",
            "source_feature_scope": "known_required_feature_gaps" if feature_gaps else "no_known_omissions_in_selectors" if profile else "no_source_entry",
            "required_terminal_feature_gaps": feature_gaps,
            "dependency_resolution": "not_run", "build": "not_run", "device_acceptance": "not_run",
            "capabilities": {"display": None, "touch": None, "microphone": None, "speaker": None},
            "capability_evidence": "not_reviewed_for_this_release_variant",
        }
        if full_name in findings:
            variant["static_research"] = findings[full_name]
        prior = previous_variants.get(full_name, {})
        # Metadata refresh must not erase later evidence-backed review records.
        for key in ("dependency_resolution", "build", "device_acceptance", "capabilities", "capability_evidence", "acceptance_evidence"):
            if key in prior:
                variant[key] = prior[key]
        records = []
        for path, record in cached_build_records:
            if not profile or record["profile"] != profile:
                continue
            source_hashes = record.get("source_hashes", [])
            sources_match = bool(source_hashes) and all(
                (DESTINATION.parent / source["path"]).is_file() and
                hashlib.sha256((DESTINATION.parent / source["path"]).read_bytes()).hexdigest() == source["sha256"]
                for source in source_hashes
            )
            records.append({"record": path.name, "build_passed": record["build_passed"],
                            "sdk": record["sdk"]["version"], "sources_match_current": sources_match})
        if records:
            variant["cached_build_records"] = records
            # Keep cached-source evidence distinct from clean release builds.
            if variant["build"] in {"not_run", "passed_cached_sources", "failed_cached_sources", "stale_cached_sources"}:
                if any(record["build_passed"] and record["sources_match_current"] for record in records):
                    variant["build"] = "passed_cached_sources"
                else:
                    variant["build"] = "stale_cached_sources" if any(record["build_passed"] for record in records) else "failed_cached_sources"
            if variant["dependency_resolution"] == "not_run" and any(record["build_passed"] for record in records):
                variant["dependency_resolution"] = "resolved_local_overrides"
        variants.append(variant)
    released_directories = sorted({entry["board_directory"] for entry in variants})
    if manifest.get("firmware_count") != len(variants) or manifest.get("board_count") != len(released_directories):
        raise ValueError("Release manifest counts disagree with its entries")
    missing_source = set(released_directories) - set(config_directories)
    if missing_source:
        raise ValueError(f"Released boards missing source config: {sorted(missing_source)}")
    if set(source_profiles) - seen:
        raise ValueError("Native profile points to a variant missing from the pinned release")
    result = {
        "schema_version": 2, "upstream_repository": "78/xiaozhi-esp32", "upstream_ref": "v2.5.2",
        "source_sha": SOURCE_SHA, "upstream_idf_version": manifest["idf_version"],
        "upstream_readme_counts": {"board_directories": 138, "release_variants": 171},
        "release_board_count": len(released_directories), "release_variant_count": len(variants),
        "source_config_directory_count": len(config_directories), "kconfig_board_type_count": len(options),
        "recorded_at": datetime.now(timezone.utc).isoformat(),
        "source_feature_scope_counts": {
            scope: sum(variant["source_feature_scope"] == scope for variant in variants)
            for scope in ["known_required_feature_gaps", "no_known_omissions_in_selectors", "no_source_entry"]
        },
        "source_feature_scope_note": "Checks deliberate omissions selected by current GUITION/INTERNAL_RAM branches only. No known omission is not proof of full source functionality, build/boot, or physical acceptance. Limited prototypes remain in the full adaptation backlog.",
        "source_feature_audit_inputs": [
            {"path": relative, "sha256": hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()}
            for relative in [
                "tools/board_profiles.py", "firmware/assistant_espidf/main/CMakeLists.txt",
                "firmware/assistant_espidf/main/board_profile.h", "firmware/assistant_espidf/main/audio_io.cpp",
                "firmware/assistant_espidf/main/control_protocol.cpp", "firmware/assistant_espidf/main/manual_wake_word.cpp",
                "firmware/assistant_espidf/main/single_app_update.cpp",
                "firmware/assistant_espidf/boards/guition-jc4827w543/partitions.csv",
                "firmware/assistant_espidf/boards/guition-jc4827w543-ext-amp/partitions.csv",
            ]
        ],
        "sources": {
            "release_manifest": {"url": MANIFEST_URL, "sha256": hashlib.sha256(manifest_bytes).hexdigest()},
            "kconfig": {"url": RAW_BASE + "main/Kconfig.projbuild", "sha256": hashlib.sha256(kconfig_bytes).hexdigest()},
            "cmake": {"url": RAW_BASE + "main/CMakeLists.txt", "sha256": hashlib.sha256(cmake_bytes).hexdigest()},
            "local_research": {"path": "board_research_v2.5.2.json", "sha256": hashlib.sha256(research_path.read_bytes()).hexdigest()},
            "native_registry": {"path": "native_board_profiles.json", "sha256": hashlib.sha256(PROFILE_PATH.read_bytes()).hexdigest()},
        },
        "scope_note": "README counts are stale. All actual released variants are included; Kconfig options and source-only directories are tracked separately. Name-based batches are work queues, not capability assertions. Native source status never implies full required terminal features, a successful build or device acceptance. Known reduced prototypes are explicitly flagged and remain in the complete adaptation backlog.",
        "legacy_profiles": [
            {
                "native_profile": name, "upstream_variant": None, "reason": profile["legacy_reason"],
                "cached_build_records": [
                    {"record": path.name, "build_passed": record["build_passed"], "sdk": record["sdk"]["version"]}
                    for path, record in cached_build_records if record["profile"] == name
                ],
            }
            for name, profile in profiles.items() if profile["legacy"]
        ],
        "board_types": sorted(options, key=lambda entry: entry["board_type"]),
        "source_only_directories": sorted(set(config_directories) - set(released_directories)),
        "released_directories": released_directories,
        "release_variants": sorted(variants, key=lambda entry: entry["full_name"]),
    }
    # Refresh public inventory only. Preserve the old file on network/parse errors.
    descriptor, temporary_name = tempfile.mkstemp(prefix="board-matrix-", suffix=".tmp", dir=DESTINATION.parent)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as output:
            output.write(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        Path(temporary_name).replace(DESTINATION)
    finally:
        Path(temporary_name).unlink(missing_ok=True)
    print(f"Recorded {len(released_directories)} released directories, {len(variants)} variants, {len(options)} Kconfig options; no firmware was built.")


if __name__ == "__main__":
    main()

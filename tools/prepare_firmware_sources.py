"""Copy one native firmware source project without resolving or installing dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import yaml

if __package__:
    from .board_profiles import load_board_profiles, required_terminal_feature_gaps
else:
    from board_profiles import load_board_profiles, required_terminal_feature_gaps

PROJECT = Path(__file__).resolve().parents[1] / "firmware/assistant_espidf"


def stage_sources(profile_name: str, destination: Path) -> dict:
    profile = load_board_profiles()[profile_name]
    destination = destination.resolve()
    if destination.exists() or not destination.parent.is_dir() or " " in str(destination):
        raise ValueError("Choose a new directory without spaces under an existing parent")
    if destination.is_relative_to(PROJECT.parents[1]):
        raise ValueError("Source staging must be outside the working repository")
    board = PROJECT / "boards" / profile_name
    manifest = PROJECT / "main/idf_component.yml" if profile["legacy"] else board / "idf_component.yml"
    copies = {Path(name): PROJECT / name for name in (
        "CMakeLists.txt", "board_profiles.cmake", "native_board_profiles.json",
        "sdkconfig.defaults", f"sdkconfig_target_{profile['target']}.defaults", "partitions.csv",
    )}
    for source in sorted((PROJECT / "main").rglob("*")):
        if source.is_file():
            copies[source.relative_to(PROJECT)] = source
    copies[Path("main/idf_component.yml")] = manifest
    defaults = ["sdkconfig.defaults", f"sdkconfig_target_{profile['target']}.defaults"]
    if not profile["legacy"]:
        copies[Path("sdkconfig_board.defaults")] = board / "sdkconfig.defaults"
        defaults.append("sdkconfig_board.defaults")
        if (board / "partitions.csv").is_file():
            copies[Path("partitions.csv")] = board / "partitions.csv"
    missing = [str(source) for source in copies.values() if not source.is_file()]
    if missing:
        raise ValueError(f"Required source files are missing: {missing}; nothing staged")
    declarations = yaml.safe_load(manifest.read_text(encoding="utf-8-sig"))["dependencies"]
    if any(isinstance(value, dict) and "override_path" in value for value in declarations.values()):
        raise ValueError("Source preparation requires original manifests without local overrides")
    inputs = []
    for relative, source in copies.items():
        data = source.read_bytes()
        inputs.append({"source": source.relative_to(PROJECT).as_posix(),
                       "staged": relative.as_posix(), "bytes": len(data),
                       "sha256": hashlib.sha256(data).hexdigest()})
    destination.mkdir()
    for relative, source in copies.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    for item in inputs:
        if hashlib.sha256((destination / item["staged"]).read_bytes()).hexdigest() != item["sha256"]:
            raise RuntimeError(f"Source changed while copying {item['source']}; staging is incomplete")
    (destination / ".assistant-board").write_text(profile_name + "\n", encoding="ascii")
    record = {
        "schema_version": 1, "recorded_at": datetime.now(timezone.utc).isoformat(),
        "profile": profile_name, "project_name": profile["project_name"],
        "target": profile["target"], "destination": str(destination),
        "flash_size_mb": profile["flash_size_mb"],
        "compile_definitions": profile["compile_definitions"],
        "implementation_scope": profile.get("implementation_scope"),
        "required_terminal_feature_gaps": required_terminal_feature_gaps(profile),
        "required_idf_commit": profile.get("required_idf_commit"),
        "manifest": manifest.relative_to(PROJECT).as_posix(),
        "declared_dependencies": declarations, "sdkconfig_defaults_order": defaults,
        "inputs": inputs, "dependency_resolution": "not_run", "build": "not_run",
        "device_acceptance": "not_run",
        "note": "Source copy only; no SDK, components, locks, local overrides, credentials, build artifacts or firmware images copied. Dependency resolution may download components and must be run by the operator.",
    }
    (destination / "source-preparation.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return record


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=tuple(load_board_profiles()), required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    record = stage_sources(args.profile, args.destination)
    print(f"Prepared {record['profile']}: {len(record['inputs'])} source files at {record['destination']}; no dependency resolution, installation or build executed")


if __name__ == "__main__":
    main()

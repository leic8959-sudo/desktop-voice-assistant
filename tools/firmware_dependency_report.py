"""Report every direct firmware dependency condition without staging or installing."""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re

import yaml
from idf_component_tools.semver import SimpleSpec, Version

if __package__:
    from .board_profiles import load_board_profiles
    from .prepare_cached_firmware_build import PROJECT, read_component_cache, read_sdk_base_version
else:
    from board_profiles import load_board_profiles
    from prepare_cached_firmware_build import PROJECT, read_component_cache, read_sdk_base_version


def version_matches(requested: str, installed: str) -> bool:
    # An explicit registry revision pin is an exact input, as in the staging tool.
    if re.fullmatch(r"\d+\.\d+\.\d+(?:~\d+)?", requested):
        return requested == installed
    return SimpleSpec(requested.replace(" ", "")).match(Version(installed))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--idf-path", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="A new JSON report file; existing files are never overwritten")
    args = parser.parse_args()
    if args.output.exists() or not args.output.parent.is_dir():
        raise ValueError("Choose a new report file under an existing directory")
    sdk_version = read_sdk_base_version(args.idf_path)
    profiles = load_board_profiles()
    cache = read_component_cache()
    results = []
    requirements = {}
    manifest_hashes = {}
    sdk_groups = {}
    for name, profile in profiles.items():
        manifest_path = PROJECT / ("main/idf_component.yml" if profile["legacy"] else f"boards/{name}/idf_component.yml")
        manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8-sig"))
        manifest_hashes[manifest_path.relative_to(PROJECT).as_posix()] = hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        sdk_requirement = manifest["dependencies"].get("idf", "*")
        if isinstance(sdk_requirement, dict):
            sdk_requirement = sdk_requirement.get("version", "*")
        sdk_requirement = str(sdk_requirement)
        sdk_ready = version_matches(sdk_requirement, sdk_version)
        sdk_groups.setdefault(sdk_requirement, []).append(name)
        issues = []
        if not sdk_ready:
            issues.append({"kind": "SDK_version_mismatch", "requested": sdk_requirement, "installed": sdk_version})
        if not (PROJECT / f"sdkconfig_target_{profile['target']}.defaults").is_file():
            issues.append({"kind": "target_defaults_missing"})
        bsp_sources = [component for component, (_, metadata) in cache.items()
                       if component.split("/", 1)[1] == profile["bsp_component"]
                       and (not metadata.get("targets") or profile["target"] in metadata["targets"])]
        if len(bsp_sources) != 1:
            issues.append({"kind": "missing_or_ambiguous_BSP", "bsp": profile["bsp_component"], "compatible_sources": bsp_sources})
        dependencies = []
        for component, value in manifest["dependencies"].items():
            if component == "idf":
                continue
            requirement = value if isinstance(value, dict) else {"version": str(value)}
            requested = str(requirement.get("version", "*"))
            cached = cache.get(component)
            installed = str(cached[1]["version"]) if cached else None
            status = "compatible_direct_cache"
            if requirement.get("git"):
                status = "Git_checkout_required"
            elif requirement.get("rules") or requirement.get("path") or requirement.get("override_path"):
                status = "conditional_or_local_requirement_review"
            elif not cached:
                status = "missing_component"
            elif cached[1].get("targets") and profile["target"] not in cached[1]["targets"]:
                status = "target_incompatible_cache"
            elif not version_matches(requested, installed):
                status = "version_mismatch"
            item = {"component": component, "requested": requested, "requirement": requirement,
                    "installed_version": installed, "status": status}
            if cached:
                item["installed_targets"] = cached[1].get("targets", [])
            dependencies.append(item)
            if status != "compatible_direct_cache":
                issues.append({"kind": status, "component": component, "requested": requested, "installed": installed})
            # Keep incompatible versions and Git revisions in separate groups.
            identity = (component, json.dumps(requirement, sort_keys=True))
            group = requirements.setdefault(identity, {"component": component, "requested": requested,
                "requirement": requirement, "profiles": [], "targets": set(), "statuses": Counter(),
                "installed_version": installed})
            group["profiles"].append(name)
            group["targets"].add(profile["target"])
            group["statuses"][status] += 1
        results.append({"profile": name, "legacy": profile["legacy"], "upstream_variant": profile["upstream_variant"],
            "target": profile["target"], "bsp": profile["bsp_component"], "sdk_requirement": sdk_requirement,
            "sdk_compatible": sdk_ready, "dependencies": dependencies, "issues": issues,
            "direct_conditions_satisfied": not issues,
            "dependency_resolution": "not_run", "build": "not_run", "device_acceptance": "not_run"})
    groups = []
    for _, group in sorted(requirements.items()):
        group["targets"] = sorted(group["targets"])
        group["statuses"] = dict(group["statuses"])
        groups.append(group)
    root = PROJECT.parents[1]
    tool_files = [Path(__file__), Path(__file__).with_name("prepare_cached_firmware_build.py"), Path(__file__).with_name("board_profiles.py")]
    report = {"schema_version": 1, "recorded_at": datetime.now(timezone.utc).isoformat(),
        "scope": "All registered profiles and all declared direct manifest requirements; read-only source inspection, no resolver/staging/build/install",
        "SDK": {"path": str(args.idf_path.resolve()), "base_version": sdk_version,
                "version_source_sha256": hashlib.sha256((args.idf_path / "tools/cmake/version.cmake").read_bytes()).hexdigest()},
        "registry_sha256": hashlib.sha256((PROJECT / "native_board_profiles.json").read_bytes()).hexdigest(),
        "tool_hashes": {path.relative_to(root).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in tool_files},
        "manifest_hashes": manifest_hashes,
        "component_cache": [{"component": name, "path": str(path), "version": str(metadata["version"]),
                             "targets": metadata.get("targets", []),
                             "manifest_sha256": hashlib.sha256((path / "idf_component.yml").read_bytes()).hexdigest()}
                            for name, (path, metadata) in sorted(cache.items())],
        "summary": {"local_profiles": len(results), "official_profiles": sum(not row["legacy"] for row in results),
                    "all_direct_conditions_satisfied": sum(row["direct_conditions_satisfied"] for row in results),
                    "official_direct_conditions_satisfied": sum(row["direct_conditions_satisfied"] and not row["legacy"] for row in results),
                    "all_issue_counts": dict(Counter(issue["kind"] for row in results for issue in row["issues"])),
                    "direct_dependency_entries": sum(len(row["dependencies"]) for row in results)},
        "SDK_requirement_groups": [{"requested": requested, "profiles": names} for requested, names in sorted(sdk_groups.items())],
        "dependency_groups": groups, "profiles": results,
        "limitations": ["No transitive dependency resolution, conditional expression evaluation or registry availability lookup",
            "Compatible direct cache does not prove SDK/toolchain/API compatibility, firmware link/size/boot or physical operation",
            "SDK base version is read from version.cmake, not proof of release provenance or stable-tag availability",
            "Missing registry versions may be unavailable; Git dependency must use its exact verified repository/path/revision",
            "Conflicting pins need isolated board environments, not one shared cache upgraded to a single version"],
        "staging_build_install_download_Git_write_flash_release": "not_performed"}
    with args.output.open("x", encoding="utf8") as handle:
        json.dump(report, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    print(json.dumps(report["summary"], ensure_ascii=False))


if __name__ == "__main__":
    main()

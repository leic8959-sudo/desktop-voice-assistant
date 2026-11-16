"""Stage one native firmware build from existing local sources without installing dependencies."""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

import yaml

if __package__:
    from .board_profiles import load_board_profiles
else:
    from board_profiles import load_board_profiles

PROJECT = Path(__file__).resolve().parents[1] / "firmware/assistant_espidf"


def read_sdk_base_version(sdk_path: Path) -> str:
    source = (sdk_path / "tools/cmake/version.cmake").read_text(encoding="utf-8-sig")
    parts = []
    for part in ("MAJOR", "MINOR", "PATCH"):
        value = re.search(rf"(?m)^\s*set\(IDF_VERSION_{part}\s+(\d+)\s*\)", source)
        if not value:
            raise ValueError("Cannot read installed SDK version; nothing was staged")
        parts.append(value[1])
    return ".".join(parts)


def read_component_cache() -> dict:
    cache = {}
    for directory in sorted((PROJECT / "managed_components").iterdir()):
        if not directory.is_dir() or "__" not in directory.name:
            continue
        namespace, name = directory.name.split("__", 1)
        metadata = yaml.safe_load((directory / "idf_component.yml").read_text(encoding="utf-8-sig"))
        cache[f"{namespace}/{name}"] = (directory, metadata)
    return cache


def main() -> None:
    profiles = load_board_profiles()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=tuple(profiles), required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--idf-path", type=Path, help="Optional installed SDK to check before staging; never downloads missing submodules")
    parser.add_argument("--check-only", action="store_true", help="Check SDK and existing direct component sources without creating a staging directory")
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists() or not destination.parent.is_dir() or " " in str(destination):
        raise ValueError("Choose a new build directory without spaces under an existing parent")
    if destination.is_relative_to(PROJECT):
        raise ValueError("Cached build staging must be isolated from the working firmware directory")
    if args.idf_path:
        required_sdk_paths = (
            "tools/idf.py", "tools/cmake/version.cmake",
            "components/spiffs/spiffs/src", "components/mbedtls/mbedtls/include/mbedtls",
            "components/lwip/lwip/src",
        )
        missing = [relative for relative in required_sdk_paths if not (args.idf_path / relative).exists()]
        if missing:
            raise ValueError(f"Installed SDK is incomplete: {missing}; no files were staged or downloaded")
    profile = profiles[args.profile]
    target_defaults = PROJECT / f"sdkconfig_target_{profile['target']}.defaults"
    if not target_defaults.is_file():
        raise ValueError(f"No SDK defaults implemented for {profile['target']}; nothing was staged")
    manifest_path = PROJECT / "main/idf_component.yml" if profile["legacy"] else PROJECT / "boards" / args.profile / "idf_component.yml"
    manifest = yaml.safe_load(manifest_path.read_text(encoding="utf-8-sig"))
    sdk_version = None
    sdk_requirement = manifest["dependencies"].get("idf", "*")
    if isinstance(sdk_requirement, dict):
        sdk_requirement = sdk_requirement.get("version", "*")
    if args.idf_path:
        from idf_component_tools.semver import SimpleSpec, Version

        sdk_version = read_sdk_base_version(args.idf_path)
        if not SimpleSpec(str(sdk_requirement).replace(" ", "")).match(Version(sdk_version)):
            raise ValueError(f"Installed SDK {sdk_version} does not satisfy {args.profile} requirement {sdk_requirement}; nothing was staged")
    cache = {}
    incompatible_cache = {}
    for name, (directory, metadata) in read_component_cache().items():
        supported_targets = metadata.get("targets", [])
        if supported_targets and profile["target"] not in supported_targets:
            incompatible_cache[name] = supported_targets
            continue
        cache[name] = (directory, str(metadata["version"]))
    required_bsp = [key for key in cache if key.split("/", 1)[1] == profile["bsp_component"]]
    if len(required_bsp) != 1:
        raise ValueError(f"No unique installed source for BSP {profile['bsp_component']}; nothing was staged")
    for name, requirement in manifest["dependencies"].items():
        if name == "idf":
            continue
        if name not in cache:
            if name in incompatible_cache:
                raise ValueError(f"Installed {name} does not support target {profile['target']}; nothing was staged")
            raise ValueError(f"Required component is not installed: {name}; nothing was staged")
        if isinstance(requirement, dict) and requirement.get("git"):
            raise ValueError(f"Pinned Git dependency {name} requires a separately verified checkout; registry cache is not proof of its commit; nothing was staged")
        requested = requirement.get("version") if isinstance(requirement, dict) else requirement
        if requested and re.fullmatch(r"\d+\.\d+\.\d+(?:~\d+)?", str(requested)) and str(requested) != cache[name][1]:
            raise ValueError(f"Installed {name}={cache[name][1]} differs from pinned {requested}; nothing was staged")
    if args.check_only:
        print(f"Checked {args.profile}; installed SDK base={sdk_version or 'not checked'}, required SDK={sdk_requirement}, compatible existing components={len(cache)}, excluded target-incompatible components={len(incompatible_cache)}; no files staged, dependencies resolved, firmware built or installation executed")
        return
    destination.mkdir()
    for filename in ("CMakeLists.txt", "board_profiles.cmake", "native_board_profiles.json", "partitions.csv", "sdkconfig.defaults"):
        shutil.copy2(PROJECT / filename, destination / filename)
    shutil.copy2(target_defaults, destination / target_defaults.name)
    shutil.copytree(PROJECT / "main", destination / "main")
    if not profile["legacy"]:
        partition_file = PROJECT / "boards" / args.profile / "partitions.csv"
        if partition_file.is_file():
            shutil.copy2(partition_file, destination / "partitions.csv")
        shutil.copy2(PROJECT / "boards" / args.profile / "sdkconfig.defaults", destination / "sdkconfig_board.defaults")
    sources = []
    for name, (directory, version) in cache.items():
        target = destination / "vendor" / directory.name
        shutil.copytree(directory, target)
        requirement = manifest["dependencies"].get(name, {})
        if not isinstance(requirement, dict):
            requirement = {"version": requirement}
        # Normal IDF local overrides retain namespace/version handling, while
        # ensuring the resolver has all of this snapshot's existing components.
        requirement["override_path"] = target.as_posix()
        requirement.setdefault("version", version)
        manifest["dependencies"][name] = requirement
        sources.append({"component": name, "version": version, "source": str(directory), "staged": str(target)})
    (destination / "main/idf_component.yml").write_text(yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8")
    (destination / ".assistant-board").write_text(args.profile + "\n", encoding="ascii")
    (destination / "cached-build-inputs.json").write_text(json.dumps({
        "profile": args.profile, "project_name": profile["project_name"],
        "target": profile["target"], "components": sources,
        "installed_sdk": str(args.idf_path.resolve()) if args.idf_path else None,
        "installed_sdk_base_version": sdk_version,
        "required_sdk_version": sdk_requirement,
        "sdkconfig_defaults_order": ["sdkconfig.defaults", target_defaults.name] + ([] if profile["legacy"] else ["sdkconfig_board.defaults"]),
        "excluded_target_incompatible_components": incompatible_cache,
        "note": "Local override build only; not a clean install or canonical release dependency lock",
    }, indent=2) + "\n", encoding="utf-8")
    guard = destination / "offline_guard"
    guard.mkdir()
    (guard / "sitecustomize.py").write_text(
        'import sys\n'
        'def block_network(event, arguments):\n'
        '    if event == "socket.connect":\n'
        '        address = arguments[1]\n'
        '        if isinstance(address, tuple) and address[0] in {"127.0.0.1", "::1"}:\n'
        '            return\n'
        '        raise RuntimeError("External network is disabled for this cached-source firmware build")\n'
        '    if event in {"socket.getaddrinfo", "socket.gethostbyname"} and arguments[0] not in {"127.0.0.1", "::1", "localhost"}:\n'
        '        raise RuntimeError("External name resolution is disabled for this cached-source firmware build")\n'
        'sys.addaudithook(block_network)\n',
        encoding="ascii",
    )
    print(f"Staged {args.profile} and {len(sources)} existing component sources at {destination}; no firmware build or installation executed.")


if __name__ == "__main__":
    main()

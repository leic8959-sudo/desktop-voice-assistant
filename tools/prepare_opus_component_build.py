"""Prepare an isolated Opus component project; never resolve/install dependencies or build."""

import argparse
import hashlib
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/assistant_espidf/opus_candidate"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", choices=("esp32c3", "esp32c6", "esp32s3"), required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    destination = args.destination.resolve()
    if destination.exists() or not destination.parent.is_dir() or " " in str(destination) or destination.is_relative_to(ROOT):
        raise ValueError("Choose a new directory without spaces outside the working repository")
    inputs = []
    for source in sorted(SOURCE.rglob("*")):
        if source.is_file():
            inputs.append({"source": source.relative_to(ROOT).as_posix(), "relative": source.relative_to(SOURCE).as_posix(),
                           "bytes": source.stat().st_size, "sha256": hashlib.sha256(source.read_bytes()).hexdigest()})
    if not inputs:
        raise ValueError("Candidate sources missing")
    destination.mkdir()
    component = destination / "components/native_opus"
    shutil.copytree(SOURCE, component)
    (destination / "main").mkdir()
    (destination / "CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.19)
if(DEFINED IDF_TARGET AND NOT IDF_TARGET STREQUAL "{args.target}")
    message(FATAL_ERROR "Target differs from this isolated component project")
endif()
set(IDF_TARGET "{args.target}" CACHE STRING "Isolated component target")
set(COMPONENTS main)
include($ENV{{IDF_PATH}}/tools/cmake/project.cmake)
project(opus_component_{args.target})
target_link_options(${{CMAKE_PROJECT_NAME}}.elf PRIVATE "LINKER:--undefined=opus_component_roots")
''', encoding="utf-8")
    (destination / "main/CMakeLists.txt").write_text('idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES native_opus)\n', encoding="utf-8")
    (destination / "main/main.cpp").write_text('''#include "opus_capture_encoder.h"
#include "opus_playback_decoder.h"
#include "opus_segment_cache.h"
// Unexecuted API link roots. Empty app_main; this is not a board firmware.
static esp_err_t encoded_sink(const uint8_t *, size_t, void *) { return ESP_ERR_INVALID_STATE; }
static esp_err_t decoded_sink(const int16_t *, size_t, bool, void *) { return ESP_ERR_INVALID_STATE; }
extern "C" void opus_component_roots(uint16_t verified_delay_samples) {
    native_opus::CaptureEncoder encoder;
    (void)encoder.Initialize(verified_delay_samples, encoded_sink, nullptr);
    (void)encoder.Begin(1);
    (void)encoder.Encode(nullptr, 320);
    (void)encoder.End();
    (void)encoder.Abort();
    native_opus::PlaybackDecoder decoder;
    (void)decoder.Initialize(decoded_sink, nullptr);
    (void)decoder.BeginPlayback(1);
    (void)decoder.Decode(1, nullptr, 0);
    decoder.Abort();
}
template class native_opus::SegmentCache<5, 52, 128, 64000>;
extern "C" void app_main() {}
''', encoding="utf-8")
    (destination / "sdkconfig.defaults").write_text(f'''CONFIG_IDF_TARGET="{args.target}"
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_SPIRAM=n
CONFIG_COMPILER_CXX_EXCEPTIONS=y
''', encoding="utf-8")
    for item in inputs:
        if hashlib.sha256((component / item["relative"]).read_bytes()).hexdigest() != item["sha256"]:
            raise RuntimeError("Candidate changed while copying")
    command = f'python "$env:IDF_PATH/tools/idf.py" -C {destination.as_posix()} -DIDF_TARGET={args.target} reconfigure build'
    record = {"recorded_at": datetime.now(timezone.utc).isoformat(), "target": args.target, "inputs": inputs,
              "scope": "Isolated component sources with empty app_main and unexecuted API roots; not a board profile",
              "planned_user_command_not_executed": command, "dependency_resolution": "not_run", "build": "not_run",
              "device_acceptance": "not_run", "dependency": "espressif/esp_audio_codec=2.5.0"}
    (destination / "source-preparation.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"destination": str(destination), "copied_files": len(inputs), "planned_user_command_not_executed": command,
                      "dependency_resolution_build_install": "not_run"}, ensure_ascii=False))


if __name__ == "__main__":
    main()

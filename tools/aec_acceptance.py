"""AEC evidence helper.

This tool measures supplied recordings and parses device-reported counters.  It
does not implement an echo canceller, so a synthetic run cannot prove acoustic
performance on a board.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import wave
from pathlib import Path
from typing import Any

import numpy as np


HARDWARE_REFERENCE_SOURCES = {
    "i2s_rx_loopback",
    "codec_adc_loopback",
    "playback_capture",
    "hardware_loopback",
}
SOFTWARE_REFERENCE_SOURCES = {
    "pcm_buffer",
    "software_pcm",
    "software_pcm24",
    "digital_playback",
}


def _rms(samples: np.ndarray) -> float:
    values = samples.astype(np.float64, copy=False)
    return float(np.sqrt(np.mean(values * values))) if values.size else 0.0


def _read_wav(path: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(path), "rb") as source:
        channels = source.getnchannels()
        width = source.getsampwidth()
        rate = source.getframerate()
        frames = source.readframes(source.getnframes())
    if channels != 1 or width != 2:
        raise ValueError(f"{path}: only 16-bit mono WAV is supported")
    return rate, np.frombuffer(frames, dtype="<i2").astype(np.float32)


def _correlation(left: np.ndarray, right: np.ndarray) -> float:
    count = min(left.size, right.size)
    if count == 0:
        return 0.0
    left = left[:count].astype(np.float64) - np.mean(left[:count])
    right = right[:count].astype(np.float64) - np.mean(right[:count])
    denominator = np.linalg.norm(left) * np.linalg.norm(right)
    return float(np.dot(left, right) / denominator) if denominator else 0.0


def _best_lag(left: np.ndarray, right: np.ndarray, max_lag: int) -> tuple[int, float]:
    count = min(left.size, right.size)
    if count == 0:
        return 0, 0.0
    left = left[:count].astype(np.float64) - np.mean(left[:count])
    right = right[:count].astype(np.float64) - np.mean(right[:count])
    limit = min(max_lag, count - 1)
    correlation = np.correlate(left, right, mode="full")
    center = count - 1
    window = correlation[center - limit : center + limit + 1]
    index = int(np.argmax(np.abs(window)))
    peak = float(window[index])
    return index - limit, peak / (np.linalg.norm(left) * np.linalg.norm(right) or 1.0)


def measure_recordings(args: argparse.Namespace) -> dict[str, Any]:
    rate, microphone = _read_wav(Path(args.microphone))
    ref_rate, reference = _read_wav(Path(args.reference))
    if rate != ref_rate:
        raise ValueError("microphone and reference sample rates differ")

    result: dict[str, Any] = {
        "kind": "offline_recording_measurement",
        "synthetic_only": False,
        "sample_rate": rate,
        "microphone_rms": round(_rms(microphone), 3),
        "reference_rms": round(_rms(reference), 3),
        "mic_reference_correlation_zero_lag": round(_correlation(microphone, reference), 6),
        "mic_reference_best_lag": _best_lag(microphone, reference, round(rate * args.max_lag_ms / 1000)),
        "reference_present": _rms(reference) >= args.reference_rms_threshold,
        "echo_reduction_db": None,
        "status": "measured_only",
    }

    if args.near and args.clean:
        near_rate, near = _read_wav(Path(args.near))
        clean_rate, clean = _read_wav(Path(args.clean))
        if near_rate != rate or clean_rate != rate:
            raise ValueError("near and clean sample rates must match microphone")
        count = min(microphone.size, near.size, clean.size)
        baseline_echo = microphone[:count] - near[:count]
        residual_echo = clean[:count] - near[:count]
        baseline = _rms(baseline_echo)
        residual = _rms(residual_echo)
        result["echo_reduction_db"] = round(20 * math.log10((baseline + 1e-9) / (residual + 1e-9)), 3)
        result["clean_near_distortion_rms"] = round(residual, 3)
        result["status"] = "measured_with_near_truth"
    elif args.near or args.clean:
        raise ValueError("--near and --clean must be supplied together for ERLE")
    return result


def _write_wav(path: Path, rate: int, samples: np.ndarray) -> None:
    clipped = np.clip(samples, -32768, 32767).astype("<i2")
    with wave.open(str(path), "wb") as target:
        target.setnchannels(1)
        target.setsampwidth(2)
        target.setframerate(rate)
        target.writeframes(clipped.tobytes())


def synthesize(args: argparse.Namespace) -> dict[str, Any]:
    rate = args.sample_rate
    count = int(rate * args.duration)
    time_axis = np.arange(count, dtype=np.float64) / rate
    envelope = np.minimum(1.0, time_axis * 4.0) * np.minimum(1.0, (args.duration - time_axis) * 4.0)
    near = envelope * (1100 * np.sin(2 * np.pi * 190 * time_axis) + 450 * np.sin(2 * np.pi * 380 * time_axis))
    far = envelope * (900 * np.sin(2 * np.pi * 530 * time_axis) + 300 * np.sin(2 * np.pi * 1060 * time_axis))
    delay = round(rate * args.echo_delay_ms / 1000)
    echo = np.zeros_like(far)
    if delay < count:
        echo[delay:] += far[: count - delay] * 0.65
        if delay * 2 < count:
            echo[delay * 2 :] += far[: count - delay * 2] * 0.2
    rng = np.random.default_rng(20261029)
    noise = rng.normal(0, args.noise_rms, count)
    microphone = near + echo + noise
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    _write_wav(output / "far_reference.wav", rate, far)
    _write_wav(output / "near_truth.wav", rate, near)
    _write_wav(output / "mic_mix.wav", rate, microphone)
    _write_wav(output / "ideal_clean.wav", rate, near)
    manifest = {
        "kind": "synthetic_aec_measurement_fixture",
        "synthetic_only": True,
        "sample_rate": rate,
        "duration_ms": round(args.duration * 1000),
        "echo_delay_ms": args.echo_delay_ms,
        "echo_taps": [0.65, 0.2],
        "noise_rms": args.noise_rms,
        "files": ["far_reference.wav", "near_truth.wav", "mic_mix.wav", "ideal_clean.wav"],
        "warning": "Fixture validates measurement plumbing only; it is not a hardware or AEC pass.",
    }
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    return manifest


def _json_objects(path: Path) -> list[dict[str, Any]]:
    objects = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        candidate = line.strip()
        if not candidate.startswith("{"):
            match = re.search(r"(\{.*\})", candidate)
            candidate = match.group(1) if match else ""
        if candidate:
            try:
                value = json.loads(candidate)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                objects.append(value)
    return objects


def _console_key_values(path: Path) -> list[dict[str, Any]]:
    """Parse diagnostic lines such as ``aec_stats enabled=1 reference=...``."""
    reports = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "aec_stats" not in line.lower():
            continue
        fields: dict[str, Any] = {"type": "aec_stats"}
        for key, raw in re.findall(r"([A-Za-z_][A-Za-z0-9_]*)=([^\s,]+)", line):
            value: Any = raw.strip('"')
            if value in {"0", "1"} and key in {"enabled", "aec_enabled"}:
                value = value == "1"
            else:
                try:
                    value = float(value) if "." in value else int(value)
                except ValueError:
                    pass
            fields[key] = value
        reports.append(fields)
    return reports


def _pick(value: dict[str, Any], *names: str) -> Any:
    for name in names:
        if name in value:
            return value[name]
    for nested_name in ("aec", "aec_stats", "audio", "stats"):
        nested = value.get(nested_name)
        if isinstance(nested, dict):
            picked = _pick(nested, *names)
            if picked is not None:
                return picked
    return None


def _is_enabled(value: Any) -> bool:
    return value is True or value == 1 or (isinstance(value, str) and value.lower() in {"1", "true", "yes"})


def inspect_device_log(args: argparse.Namespace) -> dict[str, Any]:
    path = Path(args.input)
    objects = _json_objects(path)
    reports = []
    for value in objects:
        event_type = str(value.get("type", "")).lower()
        if "aec" in event_type or any(key in value for key in ("aec_enabled", "reference_source", "reference_samples")):
            reports.append(value)
    reports.extend(_console_key_values(path))
    plain_text = path.read_text(encoding="utf-8", errors="replace")
    no_playback_reference = bool(re.search(r"0\s*playback|playback\s*[:=]\s*0", plain_text, re.IGNORECASE))
    normalized = []
    for report in reports:
        source = _pick(report, "reference_source", "ref_source", "reference")
        normalized.append({
            "aec_enabled": _pick(report, "aec_enabled", "enabled"),
            "reference_source": source,
            "sample_rate": _pick(report, "sample_rate", "rate"),
            "frame_ms": _pick(report, "frame_ms", "frame_duration_ms"),
            "mic_frames": _pick(report, "mic_frames", "input_frames"),
            "reference_frames": _pick(report, "reference_frames", "ref_frames"),
            "output_frames": _pick(report, "output_frames", "clean_frames"),
            "reference_samples": _pick(report, "reference_samples", "ref_samples"),
            "reference_underflows": _pick(report, "reference_underflows", "ref_underflows"),
            "capture_overflows": _pick(report, "capture_overflows", "mic_overflows"),
            "mic_rms": _pick(report, "mic_rms", "input_rms", "micRMS"),
            "reference_rms": _pick(report, "reference_rms", "ref_rms", "refRMS"),
            "clean_rms": _pick(report, "clean_rms", "output_rms", "cleanRMS"),
        })

    enabled = [item for item in normalized if _is_enabled(item["aec_enabled"])]
    hardware_reference = [item for item in enabled if item["reference_source"] in HARDWARE_REFERENCE_SOURCES]
    software_reference = [item for item in enabled if item["reference_source"] in SOFTWARE_REFERENCE_SOURCES]
    if hardware_reference:
        status = "hardware_reference_reported"
        reason = "设备上报了启用AEC及硬件参考；仍需核验有效样本、时序和真人插话。"
    elif software_reference:
        status = "digital_reference_only"
        reason = "设备上报了软件播放PCM参考；该参考可用于AEC，回声减量与真人插话仍需实测。"
    elif normalized:
        status = "not_ready"
        reason = "设备有AEC相关报告，但没有可确认的硬件回采参考。"
    else:
        status = "insufficient_evidence"
        reason = "日志没有AEC报告；不能从全双工codec初始化或播放统计推断AEC。"
    return {
        "kind": "device_aec_acceptance_report",
        "source": str(path),
        "status": status,
        "reason": reason,
        "textual_no_playback_reference_hint": no_playback_reference,
        "report_count": len(normalized),
        "reports": normalized,
        "required_next_evidence": [
            "设备端上报sample_rate=16000、frame_ms=32、mic/reference/output帧计数；同时记录reference_underflows、capture_overflows、mic_rms、ref_rms、clean_rms",
            "明确参考来源；软件参考或硬件回采均需给出有效样本、RMS及播放时序对齐证据",
            "播放中近端真人插话的命中、误触发、延迟及主观可懂度记录",
        ],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    measure = subparsers.add_parser("measure", help="measure WAV recordings; does not run AEC")
    measure.add_argument("--microphone", required=True)
    measure.add_argument("--reference", required=True)
    measure.add_argument("--near")
    measure.add_argument("--clean")
    measure.add_argument("--max-lag-ms", type=float, default=100)
    measure.add_argument("--reference-rms-threshold", type=float, default=10)
    measure.add_argument("--json-out")
    measure.set_defaults(handler=measure_recordings)
    fixture = subparsers.add_parser("synthesize", help="create a labelled synthetic fixture")
    fixture.add_argument("--output", required=True)
    fixture.add_argument("--sample-rate", type=int, default=16000)
    fixture.add_argument("--duration", type=float, default=3.0)
    fixture.add_argument("--echo-delay-ms", type=float, default=37)
    fixture.add_argument("--noise-rms", type=float, default=40)
    fixture.set_defaults(handler=synthesize)
    report = subparsers.add_parser("device-report", help="parse device JSONL/log evidence")
    report.add_argument("--input", required=True)
    report.add_argument("--json-out")
    report.set_defaults(handler=inspect_device_log)
    args = parser.parse_args()
    result = args.handler(args)
    if args.command != "synthesize" and args.json_out:
        Path(args.json_out).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()

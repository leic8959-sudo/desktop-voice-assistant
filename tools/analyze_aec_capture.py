"""Measure a synchronized far-only mic/reference/clean device recording."""

import argparse
import json
import wave
from pathlib import Path

import numpy as np


def analyze(path):
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate()) != (3, 2, 16000):
            raise ValueError("Expected 3-channel 16 kHz PCM16 capture")
        samples = np.frombuffer(source.readframes(source.getnframes()), dtype="<i2").reshape(-1, 3).astype(float)
    if len(samples) < 1024:
        raise ValueError("Capture too short")
    rms = np.sqrt(np.mean(samples * samples, axis=0))
    centered = samples - samples.mean(axis=0)
    size = 1 << (2 * len(samples) - 1).bit_length()
    correlation = np.fft.irfft(np.fft.rfft(centered[:, 0], size) * np.conj(np.fft.rfft(centered[:, 1], size)), size)
    lags = np.arange(-4000, 4001)
    window = correlation[lags % size]
    index = int(np.argmax(np.abs(window)))
    lag = int(lags[index])
    normalization = np.linalg.norm(centered[:, 0]) * np.linalg.norm(centered[:, 1])
    return {
        "kind": "far_only_synchronized_capture",
        "source": str(path),
        "samples": len(samples),
        "seconds": len(samples) / 16000,
        "rms_mic": float(rms[0]),
        "rms_ref": float(rms[1]),
        "rms_clean": float(rms[2]),
        "far_only_reduction_db": float(20 * np.log10((rms[0] + 1e-9) / (rms[2] + 1e-9))),
        "mic_peak": int(np.max(np.abs(samples[:, 0]))),
        "clip_fraction": float(np.mean(np.abs(samples[:, 0]) >= 32760)),
        "best_lag_samples": lag,
        "best_lag_ms": lag / 16,
        "best_correlation": float(window[index] / normalization) if normalization else 0.0,
        "limitation": "Far-only test; does not prove double-talk quality or near-end speech preservation",
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = analyze(args.input)
    text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text)

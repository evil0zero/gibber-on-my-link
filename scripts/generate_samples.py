#!/usr/bin/env python3
"""Regenerate the two canonical audio demonstrations (deterministically)."""

import hashlib
from pathlib import Path

import numpy as np
from scipy.io import wavfile

from gibber import SAMPLE_RATE, encode

EXPECTED_HASHES = {
    "gibber_fries.wav": "11e37976212a305f4011d839047b0406a5828851ecec0d143484105fa73689c5",
    "gibber_fries_repeat_4x.wav": "0951c78b8478d6582835abddacc06b5c549b80593801be0a904b1069f5614ac2",
}


def write_wav(name, pcm):
    destination = Path("samples") / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    wavfile.write(destination, SAMPLE_RATE, pcm)
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    if digest != EXPECTED_HASHES[name]:
        raise RuntimeError(f"Unexpected audio output hash for {name}: {digest}")
    print(f"{destination} ({len(pcm) / SAMPLE_RATE:.3f}s) verified {digest}")


def main():
    burst = np.int16(encode() * 32767)
    write_wav("gibber_fries.wav", burst)

    gap = np.zeros(17280, dtype=np.int16)  # 360 ms between bursts
    repeated = np.concatenate(
        [part for index in range(4) for part in ([burst, gap] if index < 3 else [burst])]
    )
    write_wav("gibber_fries_repeat_4x.wav", repeated)


if __name__ == "__main__":
    main()

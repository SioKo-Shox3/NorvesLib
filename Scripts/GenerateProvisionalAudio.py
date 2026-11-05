# 仮の合成音を生成する。標準ライブラリだけで同じ WAV を再現できる。
# 使い方: python Scripts/GenerateProvisionalAudio.py [--out-dir 出力先]

import argparse
import math
from pathlib import Path
import random
import struct
import wave


SAMPLE_RATE = 44100


def make_rain():
    frame_count = 2 * SAMPLE_RATE
    rng = random.Random(729110)
    noise = [rng.uniform(-1.0, 1.0) for _ in range(frame_count)]
    high_coefficient = math.exp(-2.0 * math.pi * 400.0 / SAMPLE_RATE)
    low_coefficient = 1.0 - math.exp(-2.0 * math.pi * 4000.0 / SAMPLE_RATE)
    previous_input = noise[-1]
    high = 0.0
    low = 0.0
    filtered = []

    # 周期的な雑音を 2 周通し、状態が落ち着いた 2 周目を使う。
    for cycle in range(2):
        for value in noise:
            high = high_coefficient * (high + value - previous_input)
            previous_input = value
            low += low_coefficient * (high - low)
            if cycle == 1:
                filtered.append(low)

    rms = math.sqrt(math.fsum(value * value for value in filtered) / frame_count)
    peak = max(abs(value) for value in filtered)
    scale = min(10.0 ** (-12.0 / 20.0) / rms, 0.999 / peak)
    return [value * scale for value in filtered]


def make_footstep(seed, duration, frequency, decay, noise_level):
    rng = random.Random(seed)
    frame_count = round(duration * SAMPLE_RATE)
    attack_frames = round(0.006 * SAMPLE_RATE)
    release_frames = round(0.025 * SAMPLE_RATE)
    samples = []

    for index in range(frame_count):
        time = index / SAMPLE_RATE
        attack = min(index / attack_frames, 1.0)
        release = min((frame_count - 1 - index) / release_frames, 1.0)
        window = (0.5 - 0.5 * math.cos(math.pi * attack))
        window *= 0.5 - 0.5 * math.cos(math.pi * release)
        tone = 0.55 * math.sin(2.0 * math.pi * frequency * time) * math.exp(-decay * time)
        noise = noise_level * rng.uniform(-1.0, 1.0) * math.exp(-time / 0.012)
        samples.append((tone + noise) * window)
    return samples


def write_wave(path, samples):
    pcm = struct.pack(f"<{len(samples)}h", *(round(value * 32767.0) for value in samples))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm)
    return f"PROVISIONAL_AUDIO file={path.name} frames={len(samples)} bytes={path.stat().st_size}"


def main():
    parser = argparse.ArgumentParser(description="仮の雨音と足音の WAV を生成します。")
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "Assets" / "Audio" / "Provisional",
        help="WAV の出力先",
    )
    args = parser.parse_args()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    sounds = (
        ("rain_loop.wav", make_rain()),
        ("footstep_01.wav", make_footstep(729111, 0.18, 95.0, 19.0, 0.28)),
        ("footstep_02.wav", make_footstep(729112, 0.20, 120.0, 23.0, 0.35)),
        ("footstep_03.wav", make_footstep(729113, 0.22, 150.0, 27.0, 0.31)),
    )
    results = [write_wave(args.out_dir / name, samples) for name, samples in sounds]
    for result in results:
        print(result)


if __name__ == "__main__":
    main()

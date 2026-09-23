# R1の白炉と既知光度の行を、ラスタとパストレーサーで同じ評価関数に通し、同じ行のSceneColorを比べる。
from array import array
import math
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
WIDTH = 256
HEIGHT = 256
FURNACE_ROW_COUNT = 15
FURNACE_MASK_RADIUS = 48.0
FURNACE_CENTER = 127.5
FURNACE_BLOCK = 8
KNOWN_CD_ROI = (112, 143)
KNOWN_CD_STAGES = ("PureLambertA", "PureLambertB", "DirectPBRA", "DirectPBRB")

# 事前に固定した閾値。白炉はラスタとPTがどちらも目標100へ1%以内（R1の評価関数）なので、
# マスク平均の差1%、8x8区画平均の差2%とする。既知光度はR1の解析値の許容（平均1%、画素3%）に揃える。
FURNACE_MEAN_TOLERANCE = 0.01
FURNACE_BLOCK_TOLERANCE = 0.02
KNOWN_CD_MEAN_TOLERANCE = 0.01
KNOWN_CD_PIXEL_TOLERANCE = 0.03

# 白炉の単画素の雑音（マスク内の最大誤差3%の評価）を抑える試料数。既知光度は点光源の直接光だけで、
# 画素内の標本位置の平均に必要な数でよい。
FURNACE_SAMPLES = 32768
KNOWN_CD_SAMPLES = 1024
SAMPLES_PER_FRAME = 1024


def load_dump(path):
    data = path.read_bytes()
    newline = data.index(b"\n")
    header = data[:newline].decode("ascii").split()
    assert header[0] == "NLRGBA32F", f"ダンプの形式が不正です: {path}"
    width, height, samples = int(header[1]), int(header[2]), int(header[3])
    assert (width, height) == (WIDTH, HEIGHT), f"ダンプの大きさが不正です: {path}"
    values = array("f")
    values.frombytes(data[newline + 1:])
    assert len(values) == width * height * 4, f"ダンプの画素数が不正です: {path}"
    for value in values:
        if not math.isfinite(value):
            raise AssertionError(f"ダンプに有限でない値があります: {path}")
    return values, samples


def run_capture(executable, arguments, dump_dir):
    dump_dir.mkdir(parents=True, exist_ok=True)
    for stale in dump_dir.glob("*.nlrgba"):
        stale.unlink()
    command = [str(executable), "--scene=indoor", "--capture-source=scene-color",
               f"--dump-capture-dir={dump_dir.as_posix()}"] + arguments
    print("run:", " ".join(command), flush=True)
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("white furnace row" in line or "oracle detail" in line or
                "skipped" in line or "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def channel_mean(values, pixels, channel):
    total = 0.0
    for x, y in pixels:
        total += values[(y * WIDTH + x) * 4 + channel]
    return total / len(pixels)


def relative(actual, expected):
    return abs(actual - expected) / max(abs(expected), 1.0e-12)


def furnace_mask():
    pixels = []
    for y in range(HEIGHT):
        for x in range(WIDTH):
            if math.hypot(x + 0.5 - FURNACE_CENTER, y + 0.5 - FURNACE_CENTER) <= FURNACE_MASK_RADIUS:
                pixels.append((x, y))
    return pixels


def furnace_blocks(mask):
    inside = set(mask)
    blocks = []
    for by in range(0, HEIGHT, FURNACE_BLOCK):
        for bx in range(0, WIDTH, FURNACE_BLOCK):
            pixels = [(x, y) for y in range(by, by + FURNACE_BLOCK)
                      for x in range(bx, bx + FURNACE_BLOCK)]
            if all(pixel in inside for pixel in pixels):
                blocks.append(pixels)
    return blocks


def compare_furnace(raster_dir, path_dir):
    mask = furnace_mask()
    blocks = furnace_blocks(mask)
    failures = 0
    worst_mean = 0.0
    worst_block = 0.0
    for row in range(FURNACE_ROW_COUNT):
        name = f"white-furnace-row{row:02d}.nlrgba"
        raster, raster_samples = load_dump(raster_dir / name)
        traced, traced_samples = load_dump(path_dir / name)
        if raster_samples != 0 or traced_samples < FURNACE_SAMPLES:
            print(f"furnace row={row:02d} sample count invalid raster={raster_samples} "
                  f"path_tracing={traced_samples}")
            failures += 1
            continue
        for channel in range(3):
            raster_mean = channel_mean(raster, mask, channel)
            traced_mean = channel_mean(traced, mask, channel)
            mean_error = relative(traced_mean, raster_mean)
            block_error = max(relative(channel_mean(traced, block, channel),
                                       channel_mean(raster, block, channel))
                              for block in blocks)
            worst_mean = max(worst_mean, mean_error)
            worst_block = max(worst_block, block_error)
            print(f"furnace row={row:02d} channel={channel} raster_mean={raster_mean * 72.0:.4f} "
                  f"path_tracing_mean={traced_mean * 72.0:.4f} mean_rel={mean_error:.5f} "
                  f"block8_max_rel={block_error:.5f} samples={traced_samples}")
            if mean_error > FURNACE_MEAN_TOLERANCE or block_error > FURNACE_BLOCK_TOLERANCE:
                failures += 1
    print(f"furnace summary rows={FURNACE_ROW_COUNT} blocks={len(blocks)} "
          f"worst_mean_rel={worst_mean:.5f} worst_block8_rel={worst_block:.5f}")
    return failures


def compare_known_cd(raster_dir, path_dir):
    low, high = KNOWN_CD_ROI
    roi = [(x, y) for y in range(low, high + 1) for x in range(low, high + 1)]
    failures = 0
    for stage in KNOWN_CD_STAGES:
        name = f"known-cd-{stage}.nlrgba"
        raster, raster_samples = load_dump(raster_dir / name)
        traced, traced_samples = load_dump(path_dir / name)
        if raster_samples != 0 or traced_samples < KNOWN_CD_SAMPLES:
            print(f"known-cd stage={stage} sample count invalid raster={raster_samples} "
                  f"path_tracing={traced_samples}")
            failures += 1
            continue
        for channel in range(3):
            raster_mean = channel_mean(raster, roi, channel)
            traced_mean = channel_mean(traced, roi, channel)
            mean_error = relative(traced_mean, raster_mean)
            pixel_error = max(relative(traced[(y * WIDTH + x) * 4 + channel],
                                       raster[(y * WIDTH + x) * 4 + channel])
                              for x, y in roi)
            print(f"known-cd stage={stage} channel={channel} raster_mean={raster_mean:.6f} "
                  f"path_tracing_mean={traced_mean:.6f} mean_rel={mean_error:.5f} "
                  f"pixel_max_rel={pixel_error:.5f} samples={traced_samples}")
            if mean_error > KNOWN_CD_MEAN_TOLERANCE or pixel_error > KNOWN_CD_PIXEL_TOLERANCE:
                failures += 1
    return failures


def main():
    # 子プロセスの日本語ログを端末の符号化で表せない場合も、診断を途切れさせずに置き換えて出す。
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: PathTracingRasterParityTest.py <RenderingHdrSceneCaptureTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    path_tracing = ["--renderer=path-tracing", f"--path-tracing-samples-per-frame={SAMPLES_PER_FRAME}"]
    runs = [
        ("raster/furnace", ["--r1-scenario=white-furnace"]),
        ("path-tracing/furnace", ["--r1-scenario=white-furnace",
                                  f"--path-tracing-samples={FURNACE_SAMPLES}"] + path_tracing),
        ("raster/known-cd", ["--r1-scenario=known-cd-lambert"]),
        ("path-tracing/known-cd", ["--r1-scenario=known-cd-lambert",
                                   f"--path-tracing-samples={KNOWN_CD_SAMPLES}"] + path_tracing),
    ]
    for label, arguments in runs:
        code = run_capture(executable, arguments, root / label)
        if code == SKIP_RETURN_CODE:
            print(f"PathTracingRasterParityVulkanTest skipped: {label}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{label} の描画検証が失敗しました exit={code}")
            return 1

    failures = compare_furnace(root / "raster/furnace", root / "path-tracing/furnace")
    failures += compare_known_cd(root / "raster/known-cd", root / "path-tracing/known-cd")
    if failures != 0:
        print(f"ラスタとパストレーサーの差が閾値を超えました failures={failures}")
        return 1
    print("path_tracing_raster_parity=PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())

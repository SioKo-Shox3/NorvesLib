# R8の動きぼけの比較: PTのシャッター0の像・1次命中の距離・シャッター参照（基準と±20%・±40%）、
# ラスタのパイプラインの画面velocity・シャッター0と指定値の像を取得し、比較する。
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
# テストのC++側の定数（NominalShutter・FrameDuration）と同じ値。
NOMINAL_SHUTTER = 1.0 / 48.0
FRAME_DURATION = "1/24"
BATCHES = 3
# 1組の試料数。シャッター時刻はdispatchごとに1点を引き直すため、1 frameの試料数を抑えて時刻を細かく分ける。
MOTION_SAMPLES = 4096
MOTION_SAMPLES_PER_FRAME = 8
STATIC_SAMPLES = 4096
STATIC_SAMPLES_PER_FRAME = 256


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("r8_mb" in line or "yardstick" in line or "sanity" in line or "_vs_" in line or
                "engaged" in line or "VUID_COUNT" in line or "negative" in line or
                "over_limit" in line or "skipped" in line or "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: R8MotionBlurPathTracingReferenceTest.py <R8MotionBlurPathTracingReferenceVulkanTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.nlrgba"):
        stale.unlink()

    # PTはラスタと同じ画素中心から1次光線を出し（縁の被覆の違いを数えない）、R8-P3の連番の1フレームの
    # 経路で累積する。独立な試料の組を3枚描き、比較側で画素ごとの中央値にする。
    def path_tracing(samples, samples_per_frame, batch, shutter):
        return ["--renderer=path-tracing",
                "--path-tracing-pixel-sampling=center",
                "--path-tracing-transport=direct",
                f"--path-tracing-samples={samples}",
                f"--path-tracing-samples-per-frame={samples_per_frame}",
                f"--path-tracing-sample-batch={batch}",
                f"--path-tracing-frame-duration={FRAME_DURATION}",
                f"--path-tracing-shutter={shutter:.9f}"]

    captures = [
        ("pt-distance", ["--renderer=path-tracing",
                         "--path-tracing-pixel-sampling=center",
                         "--path-tracing-samples=1",
                         "--path-tracing-samples-per-frame=1",
                         f"--path-tracing-frame-duration={FRAME_DURATION}",
                         "--path-tracing-shutter=0",
                         "--path-tracing-debug-output=hit-distance"]),
        # ラスタは1回の起動で、A→Bの画面velocity・シャッターの指定値と0のSceneColorを続けて取得し、
        # raster-velocity・raster-mb・raster-staticを書く。
        ("raster", ["--capture-source=gbuffer-velocity", f"--r8-mb-shutter={NOMINAL_SHUTTER:.9f}"]),
    ]
    for batch in range(BATCHES):
        captures.append((f"pt-static-b{batch}",
                         path_tracing(STATIC_SAMPLES, STATIC_SAMPLES_PER_FRAME, batch, 0.0)))
    for name, scale in [("pt-mb", 1.0), ("pt-mb-plus20", 1.2), ("pt-mb-minus20", 0.8),
                        ("pt-mb-plus40", 1.4), ("pt-mb-minus40", 0.6)]:
        for batch in range(BATCHES):
            captures.append((f"{name}-b{batch}",
                             path_tracing(MOTION_SAMPLES, MOTION_SAMPLES_PER_FRAME, batch,
                                          NOMINAL_SHUTTER * scale)))
    for name, arguments in captures:
        dump = (root / name).as_posix() + ("" if name == "raster" else ".nlrgba")
        code = run([executable] + arguments + [f"--r8-mb-dump={dump}"])
        if code == SKIP_RETURN_CODE:
            print(f"R8MotionBlurPathTracingReferenceVulkanTest skipped: {name}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{name} の取得が失敗しました exit={code}")
            return 1
    return run([executable, f"--compare-dumps={root.as_posix()}"])


if __name__ == "__main__":
    sys.exit(main())

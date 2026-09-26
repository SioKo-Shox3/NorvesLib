# R8の被写界深度の比較: PTのピンホール像・1次命中の距離・薄レンズの参照（f値の基準と±20%・±40%）、
# ラスタのパイプラインのピンホールと被写界深度を取得し、比較する。
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
# テストのC++側の定数（NominalAperture・FocusDistance）と同じ値。
NOMINAL_APERTURE = 0.025
FOCUS_DISTANCE = 9.69
BATCHES = 3
# 1組の試料数。薄レンズはdispatchごとに1点のレンズ位置を引き直すため、1 frameに1試料で累積する。
DOF_SAMPLES = 4096
PINHOLE_SAMPLES = 4096
PINHOLE_SAMPLES_PER_FRAME = 256


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("r8_dof" in line or "yardstick" in line or "sanity" in line or "_vs_" in line or
                "coc_diameter" in line or "engaged" in line or "VUID_COUNT" in line or
                "over_limit" in line or "skipped" in line or "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: R8DepthOfFieldPathTracingReferenceTest.py <R8DepthOfFieldPathTracingReferenceVulkanTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.nlrgba"):
        stale.unlink()

    # PTのピンホール像と薄レンズ参照は、どちらも画素内の一様標本（box）で1次光線を出し、縁の位置を画素の平均として
    # 入力へ持たせる。R8-P3の連番の1フレームの経路（シャッター0の静止）で累積する。独立な試料の組を3枚描き、比較側で画素ごとの中央値にする。
    def path_tracing(samples, samples_per_frame, batch, aperture, focus_distance):
        arguments = ["--renderer=path-tracing",
                     "--path-tracing-pixel-sampling=box",
                     "--path-tracing-transport=direct",
                     f"--path-tracing-samples={samples}",
                     f"--path-tracing-samples-per-frame={samples_per_frame}",
                     f"--path-tracing-sample-batch={batch}",
                     "--path-tracing-shutter=0",
                     f"--path-tracing-focus-distance={focus_distance}"]
        if aperture > 0.0:
            arguments.append(f"--path-tracing-aperture={aperture}")
            arguments.append(f"--r8-dof-aperture={aperture}")
        arguments.append(f"--r8-dof-focus-distance={focus_distance}")
        return arguments

    captures = [
        # 深度は画素中心の1次命中の距離（縁で前後の面を混ぜない）。
        ("pt-distance", ["--renderer=path-tracing",
                         "--path-tracing-pixel-sampling=center",
                         "--path-tracing-samples=1",
                         "--path-tracing-samples-per-frame=1",
                         "--path-tracing-debug-output=hit-distance"]),
        ("raster-pinhole", []),
        ("raster-dof", [f"--r8-dof-aperture={NOMINAL_APERTURE}",
                        f"--r8-dof-focus-distance={FOCUS_DISTANCE}"]),
    ]
    for batch in range(BATCHES):
        captures.append((f"pt-pinhole-b{batch}",
                         path_tracing(PINHOLE_SAMPLES, PINHOLE_SAMPLES_PER_FRAME, batch, 0.0, 0.0)))
    for name, scale in [("pt-dof", 1.0), ("pt-dof-plus20", 1.2), ("pt-dof-minus20", 0.8),
                        ("pt-dof-plus40", 1.4), ("pt-dof-minus40", 0.6)]:
        for batch in range(BATCHES):
            captures.append((f"{name}-b{batch}",
                             path_tracing(DOF_SAMPLES, 1, batch, NOMINAL_APERTURE * scale,
                                          FOCUS_DISTANCE)))
    for name, arguments in captures:
        dump = (root / f"{name}.nlrgba").as_posix()
        code = run([executable] + arguments + [f"--r8-dof-dump={dump}"])
        if code == SKIP_RETURN_CODE:
            print(f"R8DepthOfFieldPathTracingReferenceVulkanTest skipped: {name}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{name} の取得が失敗しました exit={code}")
            return 1
    return run([executable, f"--compare-dumps={root.as_posix()}"])


if __name__ == "__main__":
    sys.exit(main())

# R7-O3: 空・霧を含む屋外シーンのラスタとPTをR2と同じ3時刻で取得し、LDR-FLIPで比べる。
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
# PT参照の試料数。空と霧の雑音が知覚差の閾値へ残らない数にする。
PATH_TRACING_SAMPLES = 4096
SAMPLES_PER_FRAME = 512
TIMES = ["morning", "noon", "evening"]


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("r7_outdoor" in line or "geometry_agreement" in line or "diagnostic_" in line or
                "local_leak" in line or "skipped" in line or "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: R7OutdoorPathTracingReferenceTest.py <R7OutdoorPathTracingReferenceVulkanTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.nlrgba"):
        stale.unlink()
    # PT参照はラスタのGBufferと同じ画素中心から1次光線を出し、縁の被覆（aliasing）の違いを
    # 光輸送の差に数えない。
    path_tracing = ["--renderer=path-tracing",
                    "--path-tracing-pixel-sampling=center",
                    f"--path-tracing-samples={PATH_TRACING_SAMPLES}",
                    f"--path-tracing-samples-per-frame={SAMPLES_PER_FRAME}"]
    # 1次命中の幾何は画素中心から出す光線で決まり、累積する試料数によらない（最小1試料を待つ）。
    path_tracing_geometry = ["--renderer=path-tracing",
                             "--path-tracing-pixel-sampling=center",
                             "--path-tracing-samples=1",
                             "--path-tracing-samples-per-frame=1"]
    # 太陽の可視は画素中心の1次命中から太陽円盤の方向を標本し、試料の平均で可視の割合にする。
    path_tracing_visibility = ["--renderer=path-tracing",
                               "--path-tracing-pixel-sampling=center",
                               "--path-tracing-samples=64",
                               "--path-tracing-samples-per-frame=64"]
    captures = [
        ("raster-normal", ["--r7-outdoor-time=geometry", "--r7-outdoor-debug-view=normal"]),
        ("raster-depth", ["--r7-outdoor-time=geometry", "--r7-outdoor-debug-view=depth"]),
        ("pt-normal", path_tracing_geometry + ["--r7-outdoor-time=geometry",
                                               "--path-tracing-debug-output=shading-normal"]),
        ("pt-depth", path_tracing_geometry + ["--r7-outdoor-time=geometry",
                                              "--path-tracing-debug-output=hit-distance"]),
    ]
    for time in TIMES:
        captures += [
            # 判定するラスタは直接光をPTと同じ解析BRDFで評価し、直接光の近似差を除く。
            (f"raster-{time}", [f"--r7-outdoor-time={time}", "--raster-direct-brdf=analytic"]),
            # 判定の参照: ラスタが実装する輸送（拡散2バウンス、命中面も拡散葉だけ）。
            (f"pt-two-{time}", path_tracing + [f"--r7-outdoor-time={time}",
                                               "--path-tracing-transport=two-diffuse-bounces"]),
            # 参考: 全輸送（3回目以降のバウンスと光沢のある相互反射を含む。既知差の大きさを記録する）。
            (f"pt-full-{time}", path_tracing + [f"--r7-outdoor-time={time}",
                                                "--path-tracing-transport=full"]),
            (f"pt-direct-{time}", path_tracing + [f"--r7-outdoor-time={time}",
                                                  "--path-tracing-transport=direct"]),
            (f"raster-sun-visibility-{time}", [f"--r7-outdoor-time={time}",
                                               "--r7-outdoor-debug-view=sun-visibility"]),
            (f"pt-sun-visibility-{time}", path_tracing_visibility + [
                f"--r7-outdoor-time={time}", "--path-tracing-debug-output=sun-visibility"]),
        ]
    for name, arguments in captures:
        dump = (root / f"{name}.nlrgba").as_posix()
        code = run([executable] + arguments + [f"--r7-outdoor-dump={dump}"])
        if code == SKIP_RETURN_CODE:
            print(f"R7OutdoorPathTracingReferenceVulkanTest skipped: {name}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{name} の取得が失敗しました exit={code}")
            return 1
    return run([executable, f"--compare-dumps={root.as_posix()}"])


if __name__ == "__main__":
    sys.exit(main())

# R6 RTGIの静止収束SceneColorとPT参照（直接光のみ・拡散1バウンス・多重散乱）、ラスタとPTの1次面の
# 幾何（法線・距離）を取得し、比較する。
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
# PT参照の試料数。拡散1バウンスの参照は比較の分母になるため、雑音が知覚差へ残らない数にする。
PATH_TRACING_SAMPLES = 16384
SAMPLES_PER_FRAME = 1024
BATCHES = 3
# 1組の試料数（合計がPATH_TRACING_SAMPLES以上になるよう、1 frameの試料数の倍数へ切り上げる）。
BATCH_SAMPLES = -(-PATH_TRACING_SAMPLES // (BATCHES * SAMPLES_PER_FRAME)) * SAMPLES_PER_FRAME


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("r6_reference" in line or "yardstick" in line or "sanity" in line or
                "_vs_" in line or "indirect_mean" in line or "skipped" in line or
                "local_leak" in line or "geometry_agreement" in line or "diagnostic" in line or
                "transport_order" in line or
                "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: R6RTGIPathTracingReferenceTest.py <R6RTGIPathTracingReferenceVulkanTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.nlrgba"):
        stale.unlink()
    # PT参照はラスタのGBufferと同じ画素中心から1次光線を出し、縁の被覆（aliasing）の違いを
    # 光輸送の差に数えない。
    # PTの参照は独立な試料の組（--path-tracing-sample-batch）を3枚描き、比較側で画素ごとの中央値に
    # する（median of means。まれな1試料の外れ値に強い）。合計の試料数は従来と同程度。
    def path_tracing(batch):
        return ["--renderer=path-tracing",
                "--path-tracing-pixel-sampling=center",
                f"--path-tracing-samples={BATCH_SAMPLES}",
                f"--path-tracing-samples-per-frame={SAMPLES_PER_FRAME}",
                f"--path-tracing-sample-batch={batch}"]
    # 1次命中の幾何は画素中心から出す光線で決まり、累積する試料数によらない（最小1試料を待つ）。
    path_tracing_geometry = ["--renderer=path-tracing",
                             "--path-tracing-pixel-sampling=center",
                             "--path-tracing-samples=1",
                             "--path-tracing-samples-per-frame=1"]
    captures = [
        # 判定するラスタは直接光をPTと同じ解析BRDFで評価し、R6の範囲外の直接光の近似差を除く。
        ("raster-rtgi", ["--raster-direct-brdf=analytic"]),
        # 診断: 既定のニューラルBRDFの直接光のラスタ。
        ("raster-rtgi-neural", ["--raster-direct-brdf=neural"]),
        ("raster-normal", ["--r6-reference-debug-view=normal"]),
        ("raster-depth", ["--r6-reference-debug-view=depth"]),
        ("pt-normal", path_tracing_geometry + ["--path-tracing-debug-output=shading-normal"]),
        ("pt-depth", path_tracing_geometry + ["--path-tracing-debug-output=hit-distance"]),
    ]
    for name, transport in [("pt-direct", "direct"), ("pt-single", "single-diffuse-bounce"),
                            ("pt-two", "two-diffuse-bounces"), ("pt-full", "full")]:
        for batch in range(BATCHES):
            captures.append((f"{name}-b{batch}",
                             path_tracing(batch) + [f"--path-tracing-transport={transport}"]))
    for name, arguments in captures:
        dump = (root / f"{name}.nlrgba").as_posix()
        code = run([executable] + arguments + [f"--r6-reference-dump={dump}"])
        if code == SKIP_RETURN_CODE:
            print(f"R6RTGIPathTracingReferenceVulkanTest skipped: {name}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{name} の取得が失敗しました exit={code}")
            return 1
    return run([executable, f"--compare-dumps={root.as_posix()}"])


if __name__ == "__main__":
    sys.exit(main())

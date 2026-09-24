# R4 DDGIのCornell画像と同じシーンの自前PT参照を取得し、R4の規定の指標で比べる（更新ルール5の再照合）。
from pathlib import Path
import subprocess
import sys

SKIP_RETURN_CODE = 125
# PT参照の試料数。ROIの平均を比べるため、ROIの雑音が指標へ残らない数にする。
PATH_TRACING_SAMPLES = 4096
SAMPLES_PER_FRAME = 256


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if ("r4_reference" in line or "control_" in line or "_vs_" in line or
                "skipped" in line or "[ERROR]" in line):
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 3:
        print("usage: R4DDGIPathTracingReferenceTest.py <R4DDGIPathTracingReferenceVulkanTest> <output-root>")
        return 2
    executable = Path(sys.argv[1])
    root = Path(sys.argv[2])
    root.mkdir(parents=True, exist_ok=True)
    for stale in root.glob("*.nlrgba"):
        stale.unlink()
    captures = [
        ("raster-ddgi", []),
        ("pt-full", ["--renderer=path-tracing",
                     f"--path-tracing-samples={PATH_TRACING_SAMPLES}",
                     f"--path-tracing-samples-per-frame={SAMPLES_PER_FRAME}",
                     "--path-tracing-transport=full"]),
    ]
    for name, arguments in captures:
        dump = (root / f"{name}.nlrgba").as_posix()
        code = run([executable] + arguments + [f"--r4-reference-dump={dump}"])
        if code == SKIP_RETURN_CODE:
            print(f"R4DDGIPathTracingReferenceVulkanTest skipped: {name}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{name} の取得が失敗しました exit={code}")
            return 1
    return run([executable, f"--compare-dumps={root.as_posix()}"])


if __name__ == "__main__":
    sys.exit(main())

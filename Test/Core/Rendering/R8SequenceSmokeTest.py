# R8の連番の書き出しと検査の短い確認: 屋内・屋外それぞれ8フレームを256×144・64 sppでEXRへ書き、
# EXR連番の検証exe（欠番・寸法・非有限の画素・ポッピング）に合格することを確かめる。
from pathlib import Path
import shutil
import subprocess
import sys

SKIP_RETURN_CODE = 125
FRAMES = 8
WIDTH = 256
HEIGHT = 144
SPP = 64


def run(command):
    print("run:", " ".join(str(part) for part in command), flush=True)
    result = subprocess.run([str(part) for part in command], stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    for line in output.splitlines():
        if "r8_" in line or "skipped" in line or "[ERROR]" in line or "usage" in line:
            print("  " + line)
    return result.returncode


def main():
    sys.stdout.reconfigure(errors="replace")
    if len(sys.argv) != 4:
        print("usage: R8SequenceSmokeTest.py <R8SequenceRenderer> <R8ExrSequenceValidator> <output-root>")
        return 2
    renderer = Path(sys.argv[1])
    validator = Path(sys.argv[2])
    root = Path(sys.argv[3])
    for scene in ("indoor", "outdoor"):
        directory = root / scene
        if directory.exists():
            shutil.rmtree(directory)
        directory.mkdir(parents=True)
        code = run([renderer, f"--r8-seq-scene={scene}", f"--r8-seq-out={directory.as_posix()}",
                    "--r8-seq-first=0", f"--r8-seq-count={FRAMES}", f"--r8-seq-width={WIDTH}",
                    f"--r8-seq-height={HEIGHT}", f"--r8-seq-spp={SPP}", "--r8-seq-seed=0"])
        if code == SKIP_RETURN_CODE:
            print(f"R8SequenceSmokeTest skipped: {scene}")
            return SKIP_RETURN_CODE
        if code != 0:
            print(f"{scene} の書き出しが失敗しました exit={code}")
            return 1
        code = run([validator, f"--dir={directory.as_posix()}", f"--scene={scene}", f"--frames={FRAMES}",
                    "--first=0"])
        if code != 0:
            print(f"{scene} の連番が検査に通りません exit={code}")
            return 1
    print("R8SequenceSmokeTest passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())

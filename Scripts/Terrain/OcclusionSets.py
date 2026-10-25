# 小さな遮蔽の比べ（SCN-WLR-OCCLUSION-*）の全組を、同じ Game.exe で撮り直し、Cycles の参照を描く台本。
# 後の直しの項目もこの台本で撮り直して、ReferenceCheck.py --occlusion で照らす。
#
#   python Scripts/Terrain/OcclusionSets.py --capture --out <置き場> [--build] [--sets terrain-sky,...]
#       組ごとに CaptureScene.ps1 -Hdr -Aov -LaunchPerView で <置き場>/<組> へ撮る（--build は最初の組だけビルドする）。
#       撮り終えたら、全組の Game.exe と実装のハッシュが揃っていること・小物の書き出し（occlusion_props.*）があること・
#       低木の組に比べる株（reference_plant.ply）があることを確かめ、<置き場>/occlusion_sets.json に組の一覧を書く。
#       1 組の撮影は約 2 分なので、verify では 1 行 10 分の上限に収まるよう --sets <組> で 1 組ずつ撮る。--sets で組を
#       絞ったときは撮った組だけを確かめ、全組の揃い（ハッシュ・撮っていない組）は記録だけにする（揃いの合否は
#       最後の ReferenceCheck.py --occlusion が全組の撮影で見る）。
#       --variant-args rtgi-specular=off のように、描画の経路を前の形へ戻す開発ビルドの起動引数を全組へ足して撮れる（同じ
#       Game.exe・同じ焼いた地形で変更の前後を比べる。組の起動引数の確かめはこの引数を含めて行う）。
#   python Scripts/Terrain/OcclusionSets.py --cycles --out <置き場> [--trial] [--sets ...] [--mesh <地形の書き出し>]
#       組ごとに BlenderOcclusionReference.py で Cycles の参照を <置き場>/cycles/<組> へ描く（--trial は低いサンプル数）。
#       --seeds N で種を N 個描く（測りの収束の確かめ。OcclusionMetric.py）。
#
# 組（3 種類 × 照明 2。照明は共通の照明の空だけ sky・太陽だけ sun。どれも灰色の材質・最も細かい段・FXAA で、礫は隠す。
# 1 組 1 視点）:
#   terrain-*:     地形だけ（DEM の溝。wasteland-hide=all）、ref_cliff_01
#   rocks-*:       崖錐の岩塊・スキャンの崖の塊と地形（雲・草と低木・礫・建物・空気遠近・霧を隠す）、contact_near
#   rocks-cliff-*: 同じ隠し方で ref_cliff_01（灰色の材質の撮影は崖錐の岩を視点から 300 m 以内に限るので、崖錐の岩は無く
#                  スキャンの崖の塊だけが置かれる）
#   plant-*:       低木の根元（wasteland-hide=all-but-plant。SCN-WLR-PLANT-REF の株）、plant_near。株は灰色にならず模型の色のまま
# 契約は Docs/RenderingValidation/WastelandOcclusion.md の「比べの書き出しの契約」と「比べの組」。
import argparse
import json
import os
import subprocess
import sys
import time

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
SETS_FILE = "occlusion_sets.json"
CYCLES_DIR = "cycles"
DEFAULT_MESH = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-BLENDER-TERRAIN-mesh")
BASE_ARGS = ("wasteland-common-light", "wasteland-finest-lod", "wasteland-gray-material", "wasteland-aa=fxaa")
HIDE = {
    "terrain": "all",
    "rocks": "clouds+foliage+gravel+buildings+aerial+fog",
    "plant": "all-but-plant",
}
# 組の名前の頭 → (種類, 視点)。組は 1 視点だけを持つ。Game は撮影の起動ごとに視点を中心に崖錐の岩を選んで
# occlusion_props.* を撮影のフォルダへ書くので、2 視点を同じフォルダで撮ると後の視点の配置で前の視点の配置が消える
SET_VIEWS = (
    ("terrain", "terrain", "ref_cliff_01"),
    ("rocks", "rocks", "contact_near"),
    ("rocks-cliff", "rocks", "ref_cliff_01"),
    ("plant", "plant", "plant_near"),
)
CASES = ("sky", "sun")
PLANT_PLY = "reference_plant.ply"


def set_definitions():
    """組の名前 → {kind, case, views, game_args}（起動引数は CaptureScene.ps1 の -GameArgs の形。先頭の -- なし）"""
    sets = {}
    for prefix, kind, view in SET_VIEWS:
        for case in CASES:
            game_args = list(BASE_ARGS) + [f"common-light-case={case}", f"wasteland-hide={HIDE[kind]}"]
            sets[f"{prefix}-{case}"] = {"kind": kind, "case": case, "views": [view], "game_args": game_args}
    return sets


def selected_sets(args):
    sets = set_definitions()
    if not args.sets:
        return sets
    wanted = [name.strip() for name in args.sets.split(",") if name.strip()]
    unknown = [name for name in wanted if name not in sets]
    if unknown:
        raise SystemExit(f"OCCLUSION_SETS error 知らない組 {unknown}（{', '.join(sets)}）")
    return {name: sets[name] for name in wanted}


def absolute(path):
    return os.path.normpath(path if os.path.isabs(path) else os.path.join(REPO_ROOT, path))


def rel(path):
    return os.path.relpath(path, REPO_ROOT).replace("\\", "/")


def read_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def capture_set(name, definition, out_dir, build):
    command = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(REPO_ROOT, "Scripts", "CaptureScene.ps1"),
               "-Scene", "wasteland", "-Configuration", "RelWithDebInfo"] + (["-Build"] if build else []) + \
        ["-RequireAssets", "-Width", "1920", "-Height", "1080", "-Hdr", "-Aov",
         "-Views", ",".join(definition["views"]), "-GameArgs", ",".join(definition["game_args"]), "-LaunchPerView",
         "-OutDir", out_dir]
    started = time.time()
    print(f"OCCLUSION_SETS capture set={name} views={','.join(definition['views'])} "
          f"args={','.join(definition['game_args'])} build={int(build)}", flush=True)
    completed = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, "capture.log"), "w", encoding="utf-8") as f:
        f.write(completed.stdout)
        f.write(completed.stderr)
    for line in completed.stdout.splitlines():
        if line.startswith("CAPTURE_SCENE") and (" run " in line or "result=" in line or " build " in line):
            print(f"  {line}", flush=True)
    print(f"OCCLUSION_SETS capture set={name} exit={completed.returncode} seconds={time.time() - started:.1f}", flush=True)
    return completed.returncode


def check_capture(name, definition, set_dir):
    """撮った組の確かめ。(記録, 失敗の一覧)"""
    failures = []
    entry = dict(definition)
    entry["dir"] = rel(set_dir)
    metrics_path = os.path.join(set_dir, "metrics.json")
    inputs_path = os.path.join(set_dir, "inputs.json")
    if not os.path.isfile(metrics_path) or not os.path.isfile(inputs_path):
        return entry, [f"組 {name} の撮影（metrics.json・inputs.json）が無い"]
    metrics = read_json(metrics_path)
    inputs = read_json(inputs_path)
    entry.update({"game_exe_sha256": inputs.get("game_exe_sha256"), "files_sha256": inputs.get("files_sha256"),
                  "git_commit": inputs.get("git_commit")})
    recorded = sorted(arg[2:] if arg.startswith("--") else arg for arg in (metrics.get("game_args") or []))
    if recorded != sorted(definition["game_args"]):
        failures.append(f"組 {name} の起動引数 {recorded} が組の条件と違う")
    if metrics.get("failures"):
        failures.append(f"組 {name} の撮影に失敗の記録がある")
    for view in definition["views"]:
        for suffix in (".hdr.pfm", ".aov.json"):
            if not os.path.isfile(os.path.join(set_dir, view + suffix)):
                failures.append(f"組 {name} に {view}{suffix} が無い")
    placements_path = os.path.join(set_dir, "occlusion_props.placements.csv")
    if not os.path.isfile(placements_path) or not os.path.isfile(os.path.join(set_dir, "occlusion_props.manifest.json")):
        failures.append(f"組 {name} に小物の書き出し occlusion_props.* が無い")
    else:
        with open(placements_path, encoding="utf-8") as f:
            entry["props"] = max(sum(1 for _ in f) - 1, 0)
        if definition["kind"] == "rocks" and entry["props"] == 0:
            failures.append(f"組 {name} に小物の配置が無い（岩塊の組）")
        if definition["kind"] != "rocks" and entry["props"] != 0:
            failures.append(f"組 {name} に小物が {entry['props']} 個置かれた（地形だけ・低木の組は 0 個）")
    if definition["kind"] == "plant" and not os.path.isfile(os.path.join(set_dir, PLANT_PLY)):
        failures.append(f"組 {name} に比べる株 {PLANT_PLY} が無い")
    return entry, failures


def run_capture(args, sets, root):
    failures = []
    build = args.build
    for name, definition in sets.items():
        code = capture_set(name, definition, os.path.join(root, name), build)
        build = False
        if code != 0:
            failures.append(f"組 {name} の撮影が終了コード {code} で終わった")
    records = {}
    for name, definition in sets.items():
        entry, set_failures = check_capture(name, definition, os.path.join(root, name))
        records[name] = entry
        failures.extend(set_failures)
        print(f"OCCLUSION_SETS set={name} kind={definition['kind']} case={definition['case']} "
              f"views={','.join(definition['views'])} props={entry.get('props')} "
              f"game_exe={str(entry.get('game_exe_sha256'))[:8]} files={str(entry.get('files_sha256'))[:8]} "
              f"pass={int(not set_failures)}", flush=True)
    # 全組を同じ Game.exe・同じ実装で撮ったこと（前に撮った組と混ぜない）
    previous = read_json(os.path.join(root, SETS_FILE)).get("sets", {}) if os.path.isfile(os.path.join(root, SETS_FILE)) else {}
    merged = {name: entry for name, entry in previous.items() if name in set_definitions()}
    merged.update(records)
    across = []
    for key in ("game_exe_sha256", "files_sha256"):
        values = {entry.get(key) for entry in merged.values()}
        if len(values) != 1 or None in values:
            across.append(f"組ごとの {key} が揃っていない（{len(values)} 通り）")
    missing = [name for name in set_definitions() if name not in merged]
    if missing:
        across.append(f"撮っていない組がある: {', '.join(missing)}")
    # 組を絞った撮影では、ほかの組は前の撮影のまま（撮り直しの途中）なので揃いは記録だけにする
    if args.sets:
        for reason in across:
            print(f"OCCLUSION_SETS note: {reason}（組を絞った撮影なので合否に入れない）")
    else:
        failures.extend(across)
    with open(os.path.join(root, SETS_FILE), "w", encoding="utf-8") as f:
        json.dump({"kind": "occlusion_sets", "sets": merged}, f, indent=2, ensure_ascii=False)
    for reason in failures:
        print(f"OCCLUSION_SETS failure: {reason}")
    print(f"OCCLUSION_SETS mode=capture sets={len(merged)} result={'pass' if not failures else 'fail'} out={rel(root)}")
    return 0 if not failures else 1


def run_cycles(args, sets, root):
    failures = []
    mesh_dir = absolute(args.mesh)
    for name, definition in sets.items():
        command = [sys.executable, os.path.join(REPO_ROOT, "Scripts", "Terrain", "BlenderOcclusionReference.py"),
                   "--mesh", mesh_dir, "--capture", os.path.join(root, name), "--views", ",".join(definition["views"]),
                   "--case", definition["case"], "--kind", definition["kind"], "--set", name,
                   "--out", os.path.join(root, CYCLES_DIR, name), "--seeds", str(args.seeds)] + \
            (["--trial"] if args.trial else [])
        started = time.time()
        print(f"OCCLUSION_SETS cycles set={name} views={','.join(definition['views'])} trial={int(args.trial)}", flush=True)
        completed = subprocess.run(command, cwd=REPO_ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
        for line in (completed.stdout + completed.stderr).splitlines():
            if line.startswith(("OCCLUSION_REFERENCE", "BLENDER_OCCLUSION", "Traceback")) or "Error" in line:
                print(f"  {line}", flush=True)
        print(f"OCCLUSION_SETS cycles set={name} exit={completed.returncode} seconds={time.time() - started:.1f}", flush=True)
        if completed.returncode != 0:
            failures.append(f"組 {name} の Cycles の描画が終了コード {completed.returncode} で終わった")
    for reason in failures:
        print(f"OCCLUSION_SETS failure: {reason}")
    print(f"OCCLUSION_SETS mode=cycles sets={len(sets)} result={'pass' if not failures else 'fail'} "
          f"out={rel(os.path.join(root, CYCLES_DIR))}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="小さな遮蔽の比べの全組の撮影と Cycles の参照")
    parser.add_argument("--capture", action="store_true", help="全組をエンジンで撮る")
    parser.add_argument("--cycles", action="store_true", help="全組の Cycles の参照を描く")
    parser.add_argument("--out", required=True, help="置き場（組ごとに <置き場>/<組>、Cycles は <置き場>/cycles/<組>）")
    parser.add_argument("--build", action="store_true", help="最初の組の撮影の前に Game をビルドする")
    parser.add_argument("--trial", action="store_true", help="Cycles を低いサンプル数で試しに描く")
    parser.add_argument("--sets", default="", help="組を絞る（コンマ区切り）")
    parser.add_argument("--variant-args", default="",
                        help="全組へ足す起動引数（コンマ区切り・先頭の -- なし。例 rtgi-specular=off）。--capture だけ")
    parser.add_argument("--seeds", type=int, default=1,
                        help="Cycles の本描画の種の数（収束の確かめは 2 つ以上。occlusion-regions.json の convergence）")
    parser.add_argument("--mesh", default=DEFAULT_MESH, help="地形の書き出しのフォルダ（--wasteland-export-terrain）")
    args = parser.parse_args()
    if args.capture == args.cycles:
        parser.error("--capture か --cycles のどちらか 1 つを指定する")
    sets = selected_sets(args)
    variant = [arg.strip() for arg in args.variant_args.split(",") if arg.strip()]
    for definition in sets.values():
        definition["game_args"] = list(definition["game_args"]) + variant
    root = absolute(args.out)
    os.makedirs(root, exist_ok=True)
    return run_capture(args, sets, root) if args.capture else run_cycles(args, sets, root)


if __name__ == "__main__":
    sys.exit(main())

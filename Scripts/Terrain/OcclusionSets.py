# 小さな遮蔽の比べ（SCN-WLR-OCCLUSION-*）の全組を、同じ Game.exe で撮り直し、Cycles の参照を描く台本。
# 後の直しの項目もこの台本で撮り直して、ReferenceCheck.py --occlusion で照らす。
#
#   python Scripts/Terrain/OcclusionSets.py --export-terrain --out <置き場>
#       焼いた地形の LOD 0 を <置き場>/mesh に書き出し、版・ハッシュ・区画数を export_record.json に記録する。
#       書き出し時の撮影とログは <置き場>/mesh-capture に分ける。既存の mesh は上書きしない。
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
import datetime
import hashlib
import json
import os
import re
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


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def head_cliff_detail_version():
    source = subprocess.run(["git", "show", "HEAD:Tools/AssetCook/TerrainCliffDetail.h"], cwd=REPO_ROOT,
                            capture_output=True, text=True, encoding="utf-8", check=True).stdout
    match = re.search(r"\bkTerrainCliffDetailVersion\s*=\s*(\d+)\s*[uUlL]*\s*;", source)
    if not match:
        raise ValueError("HEAD の崖の細部の版を読めない")
    return int(match.group(1))


def run_export_terrain(root):
    mesh_dir = os.path.join(root, "mesh")
    capture_dir = os.path.join(root, "mesh-capture")
    coverage_path = os.path.join(REPO_ROOT, "build", "CookedAssets", "cook_coverage.cliff-gen.json")
    cooked_manifest_path = os.path.join(REPO_ROOT, "build", "CookedAssets", "manifest.json")
    game_path = os.path.join(REPO_ROOT, "build", "Game", "RelWithDebInfo", "Game.exe")
    tiles = triangles = 0
    cliff_version = head_version = None
    failures = []
    try:
        coverage = read_json(coverage_path)
        cliff_version = coverage["cliff_detail_version"]
        asset_cook_hash = coverage["asset_cook_sha256"]
        if not isinstance(cliff_version, int) or not isinstance(asset_cook_hash, str) or not asset_cook_hash:
            raise ValueError("焼いた地形の版または AssetCook のハッシュを読めない")
        head_version = head_cliff_detail_version()
        git_commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=REPO_ROOT, capture_output=True,
                                    text=True, encoding="utf-8", check=True).stdout.strip()
        game_hash = sha256_file(game_path)
        coverage_hash = sha256_file(coverage_path)
        cooked_manifest_hash = sha256_file(cooked_manifest_path)
        if os.path.isdir(mesh_dir) and os.listdir(mesh_dir):
            raise ValueError("地形の書き出し先が空ではない（既存の書き出しを保護する）")
        os.makedirs(mesh_dir, exist_ok=True)
        definition = set_definitions()["terrain-sky"]
        # 書き出し自体は常に LOD 0。撮影側も比較と同じ最も細かい段に揃える。
        # CaptureScene.ps1 が Game を起動するとき、空白を含むパスも一つの引数になるよう囲む。
        definition["game_args"].append(f'wasteland-export-terrain="{mesh_dir}"')
        code = capture_set("terrain-export", definition, capture_dir, False)
        if code != 0:
            raise ValueError(f"地形の書き出しを伴う撮影が終了コード {code} で終わった")
        manifest_path = os.path.join(mesh_dir, "manifest.json")
        manifest = read_json(manifest_path)
        tiles = manifest["tile_count"]
        triangles = manifest["total_triangles"]
        vertices = manifest["total_vertices"]
        entries = manifest["tiles"]
        if not isinstance(tiles, int) or tiles <= 0 or len(entries) != tiles:
            raise ValueError("書き出した区画が無い、または区画の一覧と数が違う")
        if manifest.get("failed_tiles") != 0:
            raise ValueError(f"書き出せなかった区画がある: {manifest.get('failed_tiles')}")
        if any(not os.path.isfile(os.path.join(mesh_dir, tile["file"])) for tile in entries):
            raise ValueError("区画の一覧にある PLY が無い")
        if (sha256_file(game_path) != game_hash or sha256_file(coverage_path) != coverage_hash or
                sha256_file(cooked_manifest_path) != cooked_manifest_hash):
            raise ValueError("書き出し中に Game または焼いた資産の記録が変わった")
        record = {
            "exported_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "git_commit": git_commit,
            "game_exe_sha256": game_hash,
            "cliff_detail_version": cliff_version,
            "asset_cook_sha256": asset_cook_hash,
            "cook_coverage_sha256": coverage_hash,
            "cooked_manifest_sha256": cooked_manifest_hash,
            "head_cliff_detail_version": head_version,
            "mesh_manifest_sha256": sha256_file(manifest_path),
            "tile_count": tiles,
            "total_vertices": vertices,
            "total_triangles": triangles,
            "capture_log": os.path.join(capture_dir, "capture.log"),
            "game_log": os.path.join(capture_dir, "ref_cliff_01.Game.log"),
        }
        with open(os.path.join(mesh_dir, "export_record.json"), "w", encoding="utf-8") as f:
            json.dump(record, f, indent=2, ensure_ascii=False)
            f.write("\n")
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        failures.append(f"地形を書き出せない: {error}")
    for reason in failures:
        print(f"OCCLUSION_SETS failure: {reason}")
    print(f"OCCLUSION_SETS mode=export-terrain tiles={tiles} triangles={triangles} "
          f"cliff_detail_version={cliff_version} head_cliff_detail_version={head_version} "
          f"result={'pass' if not failures else 'fail'} out={rel(root)}")
    return 0 if not failures else 1


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
    render_codes = {}
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
        render_codes[name] = completed.returncode
        if completed.returncode != 0:
            failures.append(f"組 {name} の Cycles の描画が終了コード {completed.returncode} で終わった")
    export_path = os.path.join(mesh_dir, "export_record.json")
    if os.path.isfile(export_path):
        try:
            export_record = read_json(export_path)
            expected_hash = export_record["mesh_manifest_sha256"]
            if not isinstance(expected_hash, str) or sha256_file(os.path.join(mesh_dir, "manifest.json")) != expected_hash:
                raise ValueError("地形の一覧のハッシュが書き出しの記録と違う")
            cycles_dir = os.path.join(root, CYCLES_DIR)
            terrain_export_path = os.path.join(cycles_dir, "terrain_export.json")
            previous = read_json(terrain_export_path).get("sets", {}) if os.path.isfile(terrain_export_path) else {}
            merged = {name: entry for name, entry in previous.items() if name in set_definitions()}
            for name, code in render_codes.items():
                merged[name] = {"render_exit_code": code}
            for name, entry in merged.items():
                inputs_path = os.path.join(cycles_dir, name, "inputs.json")
                try:
                    manifest_hash = read_json(inputs_path).get("manifest_sha256")
                except (OSError, ValueError, TypeError):
                    manifest_hash = None
                entry["manifest_sha256"] = manifest_hash
                entry["manifest_match"] = entry.get("render_exit_code") == 0 and manifest_hash == expected_hash
                if not entry["manifest_match"]:
                    failures.append(f"組 {name} の地形の一覧が書き出しの記録と一致しない")
            record = dict(export_record)
            record.update({"mesh_dir": mesh_dir, "sets": merged})
            os.makedirs(cycles_dir, exist_ok=True)
            with open(terrain_export_path, "w", encoding="utf-8") as f:
                json.dump(record, f, indent=2, ensure_ascii=False)
                f.write("\n")
            matched = sum(entry["manifest_match"] for entry in merged.values())
            print(f"OCCLUSION_SETS terrain_export cliff_detail_version={export_record['cliff_detail_version']} "
                  f"manifest_match={matched}/{len(merged)}")
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            failures.append(f"地形の書き出しの記録を照合できない: {error}")
    for reason in failures:
        print(f"OCCLUSION_SETS failure: {reason}")
    print(f"OCCLUSION_SETS mode=cycles sets={len(sets)} result={'pass' if not failures else 'fail'} "
          f"out={rel(os.path.join(root, CYCLES_DIR))}")
    return 0 if not failures else 1


def main():
    parser = argparse.ArgumentParser(description="小さな遮蔽の比べの全組の撮影と Cycles の参照")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--export-terrain", action="store_true", help="焼いた地形を書き出して版を記録する")
    mode.add_argument("--capture", action="store_true", help="全組をエンジンで撮る")
    mode.add_argument("--cycles", action="store_true", help="全組の Cycles の参照を描く")
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
    root = absolute(args.out)
    os.makedirs(root, exist_ok=True)
    if args.export_terrain:
        return run_export_terrain(root)
    sets = selected_sets(args)
    variant = [arg.strip() for arg in args.variant_args.split(",") if arg.strip()]
    for definition in sets.values():
        definition["game_args"] = list(definition["game_args"]) + variant
    return run_capture(args, sets, root) if args.capture else run_cycles(args, sets, root)


if __name__ == "__main__":
    sys.exit(main())

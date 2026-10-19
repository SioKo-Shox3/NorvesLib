# 荒野のレイトレーシングのシーンに地形が入り、光線が地形に当たることの検査（SCN-WL-RT-TLAS-TERRAIN。
# ReferenceCheck.py --rt-scene から呼ぶ。単独では動かさない）。
#
# エンジンの撮影（CaptureScene.ps1 の出力。wasteland-hide=all で岩・建物を外した荒野）の視点ごとの <視点>.Game.log を読む。
#   落ちる: RT のシーンの選択の最後の行（RT_SCENE instances= ... mega= ...）で instances か mega が 0、
#           RTGI の当たりの統計の最後の行（RTGI_HITS。開発ビルドだけが出す）が無いか mega_hits が 0、
#           MegaGeometry の候補が地形の区画だけと言えない（WASTELAND_PROPS placed が 0 でない・岩を外していない）、
#           --verify で撮影の入力が今のファイルと違う
#   記録する: RT のシーンに入った区画の数・範囲の外・候補の数、光線・当たり・MegaGeometry への当たりの数と割合
# MegaGeometry の候補が地形の区画だけのとき、MegaGeometry への当たり（mega_hits）を地形への当たりとして数える。
# 結果は <撮影>/rt_scene_check.json に書く。
import json
import os
import re

REPORT = "rt_scene_check.json"
RT_SCENE = re.compile(r"RT_SCENE instances=(\d+) excluded=(\d+) path=(\w+) mesh=(\d+) mega=(\d+) candidates=(\d+) "
                      r"out_of_range=(\d+) over_limit=(\d+) not_ready=(\d+)")
RTGI_HITS = re.compile(r"RTGI_HITS reason=(\w+) dispatches=(\d+) rays=(\d+) hits=(\d+) mesh_hits=(\d+) mega_hits=(\d+)")
TERRAIN = re.compile(r"WASTELAND_TERRAIN tiles=(\d+) placed=(\d+)")
PROPS = re.compile(r"WASTELAND_PROPS placed=(\d+)")
HIDE = re.compile(r"WASTELAND_HIDE mask=(\d+) .*rocks=(\d)")


def last_match(pattern, lines):
    for line in reversed(lines):
        found = pattern.search(line)
        if found:
            return found
    return None


def check_view(capture_dir, view):
    failures = []
    result = {"view": view}
    log_path = os.path.join(capture_dir, f"{view}.Game.log")
    if not os.path.isfile(log_path):
        return {"view": view, "pass": False, "failures": [f"{view}.Game.log が無い"]}
    with open(log_path, encoding="utf-8", errors="replace") as f:
        lines = f.read().splitlines()

    scene = last_match(RT_SCENE, lines)
    if scene:
        keys = ("instances", "excluded", "path", "mesh", "mega", "candidates", "out_of_range", "over_limit", "not_ready")
        result["rt_scene"] = {k: (scene.group(i + 1) if k == "path" else int(scene.group(i + 1))) for i, k in enumerate(keys)}
        if result["rt_scene"]["instances"] == 0 or result["rt_scene"]["mega"] == 0:
            failures.append(f"RT のシーンに MegaGeometry が入っていない（{scene.group(0)}）")
    else:
        failures.append("RT_SCENE instances= の行が無い")

    hits = last_match(RTGI_HITS, lines)
    if hits:
        keys = ("reason", "dispatches", "rays", "hits", "mesh_hits", "mega_hits")
        stats = {k: (hits.group(i + 1) if k == "reason" else int(hits.group(i + 1))) for i, k in enumerate(keys)}
        stats["hit_ratio"] = stats["hits"] / stats["rays"] if stats["rays"] else 0.0
        stats["mega_hit_ratio"] = stats["mega_hits"] / stats["rays"] if stats["rays"] else 0.0
        result["rtgi_hits"] = stats
        if stats["mega_hits"] == 0:
            failures.append(f"RTGI の光線が MegaGeometry に当たっていない（{hits.group(0)}）")
    else:
        failures.append("RTGI_HITS の行が無い（開発ビルドで撮っていないか、RTGI が動いていない）")

    # MegaGeometry の候補が地形の区画だけか（岩を外し、置いた小物が 0）
    terrain = last_match(TERRAIN, lines)
    props = last_match(PROPS, lines)
    hide = last_match(HIDE, lines)
    result["terrain_tiles"] = int(terrain.group(2)) if terrain else None
    result["props_placed"] = int(props.group(1)) if props else None
    result["rocks_hidden"] = bool(hide and hide.group(2) == "1")
    terrain_only = bool(terrain and props and int(props.group(1)) == 0 and result["rocks_hidden"])
    result["mega_is_terrain"] = terrain_only
    if not terrain_only:
        failures.append("MegaGeometry の候補が地形の区画だけと言えない（wasteland-hide=all で撮る）")
    elif result.get("rt_scene") and result["rt_scene"]["candidates"] - result["rt_scene"]["mesh"] > result["terrain_tiles"]:
        # 候補のうちメッシュでない分は区画の数を超えない（超えれば地形でない MegaGeometry がある）
        failures.append(f"MegaGeometry の候補が区画の数 {result['terrain_tiles']} より多い")
    if terrain_only and result.get("rtgi_hits"):
        result["terrain_hits"] = result["rtgi_hits"]["mega_hits"]
    result["failures"] = failures
    result["pass"] = not failures
    return result


def run(rc, capture_dir, views, verify):
    views = list(views) or rc.views_from_metrics(capture_dir) or []
    failures = []
    report = {"mode": "rt_scene", "verify": bool(verify), "capture": os.path.relpath(capture_dir, rc.REPO_ROOT), "views": []}
    if not views:
        failures.append("視点が無い（metrics.json が無いか --views が空）")
    inputs_ok, input_failures, _ = rc.check_inputs(capture_dir)
    if verify:
        failures.extend(input_failures)
    report["inputs_current"] = inputs_ok
    for view in views:
        result = check_view(capture_dir, view)
        report["views"].append(result)
        scene = result.get("rt_scene") or {}
        hits = result.get("rtgi_hits") or {}
        print(f"REFERENCE_CHECK rt_scene view={view} pass={int(result['pass'])} instances={scene.get('instances')} "
              f"mega={scene.get('mega')} candidates={scene.get('candidates')} out_of_range={scene.get('out_of_range')} "
              f"rays={hits.get('rays')} hits={hits.get('hits')} mega_hits={hits.get('mega_hits')} "
              f"terrain_hits={result.get('terrain_hits')} mega_hit_ratio={hits.get('mega_hit_ratio', float('nan')):.4f}")
        failures.extend(f"{view}: {f}" for f in result["failures"])
    report["failures"] = failures
    report_path = os.path.join(capture_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=rt_scene views={len(views)} "
          f"inputs_current={int(inputs_ok)} report={report_path}")
    return 0 if not failures else 1

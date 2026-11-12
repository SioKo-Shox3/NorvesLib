# シーン配置と地形を照合し、問題・到達性・所要時間の報告を書き出す。
from __future__ import annotations

import argparse
from collections import Counter
from itertools import combinations
import json
import math
from pathlib import Path
import re
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np

import level_grid
from level_inputs import LevelInputError, load_config, load_scene, load_terrain


def _text(properties, name, default=""):
    value = properties.get(name, default)
    if not isinstance(value, str):
        raise ValueError(f"{name} は文字列で指定してください。")
    return value


def _number(value, name):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError(f"{name} は有限の 0 以上の数値で指定してください。")
    return float(value)


def _key(entity):
    return _text(entity.components.get("LevelMarkerComponent", {}), "Key")


def _zone(entity):
    properties = entity.components.get("EncounterZoneComponent")
    if properties is None:
        return None
    shape = properties.get("Shape", 0)
    if isinstance(shape, str):
        shape = {"Sphere": 0, "Box": 1, "CylinderY": 2}.get(shape.rsplit("::", 1)[-1], shape)
    if type(shape) is not int or shape not in (0, 1, 2):
        raise ValueError("Shape は Sphere・Box・CylinderY または整数 0・1・2 で指定してください。")
    radius = _number(properties.get("Radius", 1.0), "Radius")
    extents = properties.get("HalfExtents", (1.0, 1.0, 1.0))
    if not isinstance(extents, (tuple, list)) or len(extents) != 3:
        raise ValueError("HalfExtents は Vector3 で指定してください。")
    extents = tuple(_number(value, "HalfExtents") for value in extents)
    _number(properties.get("HalfHeight", 1.0), "HalfHeight")
    return shape, radius, extents


def _contains(entity, zone, x, z):
    dx, dz = x - entity.world_position[0], z - entity.world_position[2]
    shape, radius, extents = zone
    if shape == 1:
        return (np.abs(dx) <= extents[0]) & (np.abs(dz) <= extents[2])
    return np.hypot(dx, dz) <= radius


def _footprint(terrain, entity, zone):
    """足元に入る地形節点と、足元の地形外へのはみ出しを求める。"""
    x, _, z = entity.world_position
    shape, radius, extents = zone
    hx, hz = (extents[0], extents[2]) if shape == 1 else (radius, radius)
    partial = (x - hx < terrain.origin_x or x + hx > terrain.origin_x + terrain.size_x_m()
               or z - hz < terrain.origin_z or z + hz > terrain.origin_z + terrain.size_z_m())
    rows, cols = terrain.heights.shape
    r0, c0 = terrain.world_to_cell(x - hx, z - hz)
    r1, c1 = terrain.world_to_cell(x + hx, z + hz)
    rr = np.arange(max(0, math.ceil(r0)), min(rows - 1, math.floor(r1)) + 1)
    cc = np.arange(max(0, math.ceil(c0)), min(cols - 1, math.floor(c1)) + 1)
    grid_r, grid_c = np.meshgrid(rr, cc, indexing="ij")
    world_x = terrain.origin_x + grid_c * terrain.cell_size_m
    world_z = terrain.origin_z + grid_r * terrain.cell_size_m
    inside = _contains(entity, zone, world_x, world_z)
    selected = grid_r[inside], grid_c[inside]
    if not selected[0].size:
        row, col = terrain.nearest_cell(x, z)
        selected = np.array([row]), np.array([col])
    return selected, partial


def _trail_samples(points, step):
    """端の隣を複製する一様 Catmull-Rom を区間ごとの弦長に従って標本化する。"""
    points = np.asarray(points, dtype=np.float64)
    yield points[0]
    last = len(points) - 1
    for index in range(last):
        p0, p1 = points[max(0, index - 1)], points[index]
        p2, p3 = points[index + 1], points[min(last, index + 2)]
        divisions = max(1, math.ceil(math.hypot(*(p2 - p1)[[0, 2]]) / step))
        for sample in range(1, divisions + 1):
            if sample == divisions:
                yield p2
                continue
            u = sample / divisions
            yield p1 + ((p2 - p0) + ((p0 * 2 - p1 * 5 + p2 * 4 - p3)
                        + (p1 * 3 - p2 * 3 + p3 - p0) * u) * u) * (0.5 * u)


def _rounded(value):
    if isinstance(value, dict):
        return {key: _rounded(item) for key, item in value.items()}
    if isinstance(value, (tuple, list)):
        return [_rounded(item) for item in value]
    if isinstance(value, (float, np.floating)):
        if not math.isfinite(value):
            raise ValueError("報告の計算結果に非有限値があります。")
        return round(float(value), 6)
    if isinstance(value, np.integer):
        return int(value)
    return value


def _terrain_warning(message, terrain):
    """地形の注意に含まれる絶対パスをファイル名に置き換える。"""
    message = str(message).replace(terrain.spec_path, Path(terrain.spec_path).name)
    return re.sub(r"(?:[A-Za-z]:[\\/]|/)[^\s:：]+",
                  lambda match: re.split(r"[\\/]", match[0])[-1], message)


def validate(scene_path, terrain_spec_path, config_path,
             traversable_fn=level_grid.default_traversable) -> dict:
    """配置を検査する。GR21 の経路探索が入ったら traversable_fn を差し替える。"""
    config = load_config(config_path)
    terrain = load_terrain(terrain_spec_path)
    scene = load_scene(scene_path)
    slope = level_grid.slope_deg(terrain.heights, terrain.cell_size_m)
    water = level_grid.water_mask(terrain.heights, config["water"], config["_base_dir"])
    passable = np.asarray(traversable_fn(slope, water, config["traversal"]["max_slope_deg"]))
    if passable.dtype != np.bool_ or passable.shape != terrain.heights.shape:
        raise ValueError("通行判定は高さ格子と同じ寸法の bool 配列を返してください。")
    markers = [entity for entity in scene.entities if "LevelMarkerComponent" in entity.components]
    owners = [entity for entity in markers
              if _text(entity.components["LevelMarkerComponent"], "Tag") == config["owner_tag"]]
    territories = [entity for entity in markers if "TerritoryMarkerComponent" in entity.components]
    rests = [entity for entity in markers if "RestPointComponent" in entity.components]
    pois = sorted((entity for entity in markers if "PoiMarkerComponent" in entity.components),
                  key=lambda entity: (_key(entity), entity.alias))
    trails = [entity for entity in scene.entities if "ScentTrailComponent" in entity.components]
    issues, warnings, travel = [], [], []

    def add(destination, check, code, entity, message, **details):
        destination.append({"check": check, "code": code, "key": _key(entity) if entity else "",
                            "message": message, "details": details})

    # (b) 空白だけのキーも空として扱い、重複は保存値をそのまま照合する。
    keys = Counter(_key(entity) for entity in markers)
    for entity in markers:
        if not _key(entity).strip():
            add(issues, "b", "key_empty", entity, "マーカーの Key が空です。",
                alias=entity.alias, name=entity.name)
    for key, count in keys.items():
        if count > 1:
            issues.append({"check": "b", "code": "key_duplicate", "key": key,
                           "message": "マーカーの Key が重複しています。", "details": {"count": count}})

    # (a) 持ち場の対応と領域を確認する。
    owner_keys = {_key(entity) for entity in owners}
    territory_owners = [_text(entity.components["TerritoryMarkerComponent"], "OwnerKey")
                        for entity in territories]
    zones = [_zone(entity) for entity in territories]
    for entity in owners:
        count = territory_owners.count(_key(entity))
        if count == 0:
            add(issues, "a", "owner_without_territory", entity, "シ者に対応する持ち場がありません。", count=count)
        elif count > 1:
            add(issues, "a", "owner_multiple_territories", entity, "シ者に複数の持ち場が対応しています。", count=count)
    for entity, owner_key, zone in zip(territories, territory_owners, zones):
        if not owner_key.strip() or owner_key not in owner_keys:
            add(issues, "a", "territory_orphan", entity, "持ち場の OwnerKey に対応するシ者がありません。", OwnerKey=owner_key)
        if zone is None:
            add(issues, "a", "territory_without_zone", entity, "持ち場に EncounterZoneComponent がありません。")
            continue

        # (c) 中心が範囲外でも、地形に重なる足元の斜度と水域は確認する。
        x, y, z = entity.world_position
        if not terrain.in_bounds(x, z):
            add(issues, "c", "territory_out_of_bounds", entity, "持ち場の中心が地形の範囲外です。")
        else:
            difference = abs(y - terrain.height_at(x, z))
            if difference > config["territory"]["ground_tolerance_m"]:
                add(issues, "c", "territory_off_ground", entity, "持ち場の中心が地面から離れています。",
                    difference_m=difference)
        cells, partial = _footprint(terrain, entity, zone)
        maximum = float(np.max(slope[cells]))
        fraction = float(np.mean(water[cells]))
        if maximum > config["territory"]["max_slope_deg"]:
            add(issues, "c", "territory_slope_exceeded", entity, "持ち場の最大斜度が上限を超えています。",
                max_slope_deg=maximum)
        if fraction > config["territory"]["max_water_fraction"]:
            add(issues, "c", "territory_water_exceeded", entity, "持ち場の水域の割合が上限を超えています。",
                water_fraction=fraction)
        if partial:
            add(warnings, "c", "territory_partially_out_of_bounds", entity, "持ち場の足元が地形の外にはみ出しています。")

    rest_records = []
    for entity in rests:
        fast = entity.components["RestPointComponent"].get("bFastTravel", True)
        if type(fast) is not bool:
            raise ValueError("bFastTravel は bool の 0 または 1 で指定してください。")
        rest_records.append({"key": _key(entity), "bFastTravel": fast})

    # (d) 開始地点から寝床までの連結成分を比較する。
    start_x, start_z = config["reachability"]["start_xz"]
    if not terrain.in_bounds(start_x, start_z):
        add(issues, "d", "start_out_of_bounds", None, "開始地点が地形の範囲外です。")
    else:
        start = terrain.nearest_cell(start_x, start_z)
        if not passable[start]:
            add(issues, "d", "start_impassable", None, "開始地点の格子を通行できません。")
        else:
            labels = level_grid.label_components(passable)
            for entity, record in zip(rests, rest_records):
                x, _, z = entity.world_position
                if not terrain.in_bounds(x, z):
                    add(issues, "d", "rest_point_out_of_bounds", entity, "寝床が地形の範囲外です。",
                        bFastTravel=record["bFastTravel"])
                elif labels[terrain.nearest_cell(x, z)] != labels[start]:
                    add(issues, "d", "rest_point_unreachable", entity, "開始地点から寝床に到達できません。",
                        bFastTravel=record["bFastTravel"])

    # (e) 全見どころの組から、各点の最短の所要時間を求める。
    valid_pois = []
    for entity in pois:
        x, _, z = entity.world_position
        if terrain.in_bounds(x, z):
            valid_pois.append(entity)
        else:
            add(issues, "e", "poi_out_of_bounds", entity, "見どころが地形の範囲外です。")
    nearest = [[] for _ in valid_pois]
    for i, j in combinations(range(len(valid_pois)), 2):
        first, second = valid_pois[i], valid_pois[j]
        ax, _, az = first.world_position
        bx, _, bz = second.world_position
        path = level_grid.astar(terrain.heights, passable, terrain.cell_size_m,
                               terrain.nearest_cell(ax, az), terrain.nearest_cell(bx, bz),
                               config["travel"]["slope_cost"])
        time = path.cost_m / config["travel"]["cruise_speed_mps"] if path else None
        travel.append({"from": _key(first), "to": _key(second),
                       "straight_m": math.hypot(bx - ax, bz - az) if path else None,
                       "path_length_m": path.length_m if path else None,
                       "cost_m": path.cost_m if path else None, "time_s": time, "reachable": path is not None})
        if path:
            nearest[i].append((time, _key(second), second.alias))
            nearest[j].append((time, _key(first), first.alias))
    band = config["poi_travel_time_band_s"]
    for entity, choices in zip(valid_pois, nearest):
        if choices:
            time, target, _ = min(choices)
            if not band["min"] <= time <= band["max"]:
                add(warnings, "e", "poi_interval_out_of_band", entity, "最も近い見どころへの所要時間が帯の外です。",
                    nearest=target, time_s=time, band=band)
        elif len(pois) >= 2:
            add(warnings, "e", "poi_isolated", entity, "到達できる相手の見どころがありません。")

    # (f) 曲線上の標本と終点の持ち場を確認する。
    for entity in trails:
        points = [child.world_position for child in scene.child_entities(entity)
                  if "ScentTrailPointComponent" in child.components]
        if len(points) < 2:
            add(issues, "f", "trail_too_few_points", entity, "匂いの痕跡の点が 2 つ未満です。", count=len(points))
        else:
            outside_count = blocked_count = 0
            first_outside = first_blocked = None
            for sample in _trail_samples(points, config["trail"]["sample_step_m"]):
                x, _, z = sample
                if not terrain.in_bounds(x, z):
                    outside_count += 1
                    if first_outside is None:
                        first_outside = sample.tolist()
                elif not passable[terrain.nearest_cell(x, z)]:
                    blocked_count += 1
                    if first_blocked is None:
                        first_blocked = sample.tolist()
            if outside_count:
                add(issues, "f", "trail_out_of_bounds", entity, "匂いの痕跡に地形の範囲外の標本があります。",
                    count=outside_count, first_position=first_outside)
            if blocked_count:
                add(issues, "f", "trail_impassable", entity, "匂いの痕跡に通行できない標本があります。",
                    count=blocked_count, first_position=first_blocked)
        target = _text(entity.components["ScentTrailComponent"], "TargetKey")
        matches = [index for index, territory in enumerate(territories) if _key(territory) == target]
        if not matches:
            matches = [index for index, owner in enumerate(territory_owners) if owner == target]
        if not matches:
            add(issues, "f", "trail_target_unknown", entity, "匂いの痕跡の TargetKey に対応する持ち場がありません。",
                TargetKey=target)
        elif points:
            index = matches[0]
            x, _, z = points[-1]
            if zones[index] is not None and not _contains(territories[index], zones[index], x, z):
                add(issues, "f", "trail_end_outside_territory", entity, "匂いの痕跡の終点が持ち場の足元の外です。",
                    TargetKey=target)

    provisional = []
    for key in config["provisional"]:
        value = config
        for part in key.split("."):
            value = value[part]
        provisional.append({"key": key, "value": value, "note": "未採用の仮置き"})
    order = lambda entry: tuple(entry[key] for key in ("check", "code", "key", "message"))
    return _rounded({
        "schema": "level_report/1", "status": "fail" if issues else "pass",
        "inputs": {"scene": Path(scene_path).name, "terrain_spec": Path(terrain_spec_path).name,
                   "config": Path(config_path).name},
        "provisional": provisional,
        "terrain": {"rows": terrain.heights.shape[0], "cols": terrain.heights.shape[1],
                    "cell_size_m": terrain.cell_size_m,
                    "warnings": [_terrain_warning(message, terrain) for message in terrain.warnings]},
        "counts": {"markers": len(markers), "owners": len(owners), "territories": len(territories),
                   "rest_points": len(rests), "pois": len(pois), "trails": len(trails)},
        "rest_points": rest_records,
        "poi_travel_time_band_s": band,
        "issues": sorted(issues, key=order), "warnings": sorted(warnings, key=order), "poi_travel": travel,
        "summary": {"issues": len(issues), "warnings": len(warnings)},
    })


def _markdown(value):
    return str(value).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("|", "&#124;").replace("\r\n", "<br>").replace("\r", "<br>").replace("\n", "<br>")


def _display(value):
    if value is None:
        return "—"
    return f"{value:.6f}".rstrip("0").rstrip(".") if isinstance(value, float) else str(value)


def write_reports(report: dict, out_dir) -> tuple[Path, Path]:
    """機械照合用 JSON と日本語の配置検査報告を UTF-8・LF で書く。"""
    report = _rounded(report)
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path, markdown_path = out_dir / "level_report.json", out_dir / "level_report.md"
    json_path.write_bytes((json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8"))
    result = "合格" if report["status"] == "pass" else "不合格"
    lines = ["# 配置検査報告", "", f"結果: {result}（問題 {report['summary']['issues']} 件、警告 {report['summary']['warnings']} 件）", "",
             "## 入力", "", f"- シーン: {_markdown(report['inputs']['scene'])}",
             f"- 地形仕様: {_markdown(report['inputs']['terrain_spec'])}",
             f"- 設定: {_markdown(report['inputs']['config'])}", "", "## 仮置きの値", ""]
    if report["provisional"]:
        for item in report["provisional"]:
            value = json.dumps(item["value"], ensure_ascii=False, sort_keys=True)
            lines.append(f"- {_markdown(item['key'])} = {_markdown(value)}（未採用の仮置き）")
    else:
        lines.append("なし")
    for field, title in (("issues", "問題"), ("warnings", "警告")):
        lines.extend(["", f"## {title}", "", "| 検査 | コード | Key | 内容 | 詳細 |", "| --- | --- | --- | --- | --- |"])
        for item in report[field]:
            details = json.dumps(item["details"], ensure_ascii=False, sort_keys=True)
            lines.append("| " + " | ".join(_markdown(value) for value in
                         (item["check"], item["code"], item["key"], item["message"], details)) + " |")
        if not report[field]:
            lines.append("| — | — | — | なし | — |")
    lines.extend(["", "## 寝床・ファストトラベル点", "", "| Key | ファストトラベル |", "| --- | --- |"])
    for item in report.get("rest_points", []):
        lines.append(f"| {_markdown(item['key'])} | {'有効' if item['bFastTravel'] else '無効'} |")
    if not report.get("rest_points"):
        lines.append("| — | なし |")
    lines.extend(["", "## 見どころの間の所要時間", "",
                  "| from | to | 直線 (m) | 経路長 (m) | 所要秒 | 帯の内外 |", "| --- | --- | --- | --- | --- | --- |"])
    band = report["poi_travel_time_band_s"]
    for item in report["poi_travel"]:
        if not item["reachable"]:
            relation = "到達不可"
        else:
            relation = "帯内" if band["min"] <= item["time_s"] <= band["max"] else "帯外"
        lines.append("| " + " | ".join(_markdown(_display(value)) for value in
                     (item["from"], item["to"], item["straight_m"], item["path_length_m"], item["time_s"], relation)) + " |")
    if not report["poi_travel"]:
        lines.append("| — | — | — | — | — | なし |")
    if report["terrain"]["warnings"]:
        lines.extend(["", "## 地形の注意", ""])
        lines.extend(f"- {_markdown(message)}" for message in report["terrain"]["warnings"])
    markdown_path.write_bytes(("\n".join(lines) + "\n").encode("utf-8"))
    return json_path, markdown_path


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="シーン配置と地形を検査し、日本語の報告を書き出します。")
    for name, description in (("scene", "シーン JSON"), ("terrain", "地形仕様 JSON"),
                              ("config", "検査設定 JSON"), ("out", "報告の出力ディレクトリ")):
        parser.add_argument(f"--{name}", required=True, help=description)
    args = parser.parse_args(argv)
    try:
        report = validate(args.scene, args.terrain, args.config)
        write_reports(report, args.out)
    except (LevelInputError, ValueError, OSError) as error:
        print(f"配置検査の入力または書き出しに不備があります: {error}", file=sys.stderr)
        return 2
    return 1 if report["issues"] else 0


if __name__ == "__main__":
    raise SystemExit(main())

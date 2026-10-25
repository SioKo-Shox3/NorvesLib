# 写真ごとの量の行の合否（CompareReference.py の --look・--shape の --expect。単独では動かさない）。
#
# 仕様の JSON: {"description": "<説明>", "rows": [<行>, ...]}（行の並びだけでもよい）。行の鍵（ほかの鍵は書き間違いとして拒む）:
#   photo: 写真の ID（CompareReference.PHOTOS の id）。--photos で選んでいない写真の行は飛ばす
#   cliff: 崖の名前（--shape だけ。ShapeAnnotations.json の name。省くと写真の全部の崖をまとめた量）
#   quantity: 量。写真の記録（look_metrics.json・shape_metrics.json の photos.<写真>。cliff があれば cliffs.<崖>）の中の
#     値への . 区切りの道（--look は regions.ground.png.luminance・derived.png.ground_over_blue_sky など、--shape は
#     shelf_match・corner_match・outline_distance_median_px など）
#   min・max: 下限・上限（両端を含む。どちらか一方は要る）。note: 行の説明（判定には使わない）
#   group: 組の名前（省いてよい）。組の行は 1 行ずつでは落ちず、仕様の "groups": {"<名前>": {"min_pass": <数>}} の
#     数以上の行が範囲の内なら組が合格（「5 つの崖のうち 3 つ以上で」のような行）。groups に無い組の名前は落ちる
#   moved_to: 合否を移した先の項目の ID（省いてよい）。印の付いた行は値と範囲を result=moved で出すだけで、合否と組に
#     数えない（項目を閉じるときに残った行を、値を変えずに後の項目へ移す）
# 落ちる: 仕様が読めない・行が無い・知らない鍵・未知の写真の ID・min と max が無い・値が求まらない（道が無い・数でない）・
#   選んだ写真の行が 1 つも無い（空の選択）・値が範囲の外。
import json
import os

ROW_KEYS = ("photo", "cliff", "quantity", "min", "max", "note", "group", "moved_to")


def load_spec(path):
    with open(path, encoding="utf-8-sig") as file:
        spec = json.load(file)
    rows = spec.get("rows") if isinstance(spec, dict) else spec
    groups = spec.get("groups", {}) if isinstance(spec, dict) else {}
    return (rows if isinstance(rows, list) else None), (groups if isinstance(groups, dict) else None)


def load_rows(path):
    return load_spec(path)[0]


def lookup(entry, path):
    value = entry
    for key in path.split("."):
        if not isinstance(value, dict) or key not in value:
            return None
        value = value[key]
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def evaluate(spec_path, photos_report, selected, known, prefix, cliff_allowed):
    """仕様の行を写真の記録で判定する。失敗の一覧を返し、行ごとの結果を prefix の行で出す"""
    if not os.path.isfile(spec_path):
        return [f"仕様 {spec_path} が無い"]
    try:
        rows, groups = load_spec(spec_path)
    except (OSError, ValueError) as error:
        return [f"仕様 {spec_path} が読めない: {error}"]
    if not rows:
        return [f"仕様 {spec_path} に行が無い"]
    if groups is None:
        return [f"仕様 {spec_path} の groups がオブジェクトでない"]
    failures = []
    evaluated = 0
    # 組ごとの [範囲の内の行の数, 判定した行の数]
    group_counts = {}
    for index, row in enumerate(rows):
        label = f"行 {index + 1}"
        if not isinstance(row, dict):
            failures.append(f"{label} が JSON のオブジェクトでない")
            continue
        unknown = [key for key in row if key not in ROW_KEYS or (key == "cliff" and not cliff_allowed)]
        if unknown:
            failures.append(f"{label} に知らない鍵 {unknown}")
            continue
        photo = row.get("photo")
        quantity = row.get("quantity")
        if photo not in known:
            failures.append(f"{label} の写真の ID {photo} が未知")
            continue
        if not quantity:
            failures.append(f"{label} に quantity が無い")
            continue
        group = row.get("group")
        if group is not None and not isinstance((groups.get(group) or {}).get("min_pass"), int):
            failures.append(f"{label} の組 {group} が groups に無いか min_pass が整数でない")
            continue
        if "min" not in row and "max" not in row:
            failures.append(f"{label} に min も max も無い")
            continue
        if photo not in selected:
            print(f"{prefix} skip photo={photo} quantity={quantity}（--photos で選んでいない）")
            continue
        evaluated += 1
        entry = photos_report.get(photo)
        if entry is not None and row.get("cliff"):
            entry = entry.get("cliffs", {}).get(row["cliff"])
        value = lookup(entry, quantity) if entry is not None else None
        where = (f"photo={photo}" + (f" cliff={row['cliff']}" if row.get("cliff") else "") + f" quantity={quantity}"
                 + (f" group={group}" if group is not None else ""))
        if value is None:
            failures.append(f"{label}（{where}）の値が求まらない")
            print(f"{prefix} {where} value=undetermined result=fail")
            continue
        low, high = row.get("min"), row.get("max")
        ok = (low is None or value >= low) and (high is None or value <= high)
        if row.get("moved_to"):
            print(f"{prefix} {where} value={value:.4f} min={low} max={high} result=moved"
                  f" moved_to={row['moved_to']} in_range={'yes' if ok else 'no'}")
            continue
        if group is not None:
            counts = group_counts.setdefault(group, [0, 0])
            counts[0] += int(ok)
            counts[1] += 1
            print(f"{prefix} {where} value={value:.4f} min={low} max={high} result={'in' if ok else 'out'}")
            continue
        print(f"{prefix} {where} value={value:.4f} min={low} max={high} result={'pass' if ok else 'fail'}")
        if not ok:
            failures.append(f"{label}（{where}）の値 {value:.4f} が範囲 [{low}, {high}] の外")
    for group, (inside, judged) in group_counts.items():
        need = groups[group]["min_pass"]
        ok = inside >= need
        print(f"{prefix} group={group} inside={inside} judged={judged} min_pass={need} result={'pass' if ok else 'fail'}")
        if not ok:
            failures.append(f"組 {group} で範囲の内の行が {inside} / {judged}（{need} 以上が要る）")
    if evaluated == 0:
        failures.append(f"仕様 {spec_path} に選んだ写真の行が無い（空の選択）")
    return failures

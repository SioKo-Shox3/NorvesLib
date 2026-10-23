# 写真ごとの量の行の合否（CompareReference.py の --look・--shape の --expect。単独では動かさない）。
#
# 仕様の JSON: {"description": "<説明>", "rows": [<行>, ...]}（行の並びだけでもよい）。行の鍵（ほかの鍵は書き間違いとして拒む）:
#   photo: 写真の ID（CompareReference.PHOTOS の id）。--photos で選んでいない写真の行は飛ばす
#   cliff: 崖の名前（--shape だけ。ShapeAnnotations.json の name。省くと写真の全部の崖をまとめた量）
#   quantity: 量。写真の記録（look_metrics.json・shape_metrics.json の photos.<写真>。cliff があれば cliffs.<崖>）の中の
#     値への . 区切りの道（--look は regions.ground.png.luminance・derived.png.ground_over_blue_sky など、--shape は
#     shelf_match・corner_match・outline_distance_median_px など）
#   min・max: 下限・上限（両端を含む。どちらか一方は要る）。note: 行の説明（判定には使わない）
# 落ちる: 仕様が読めない・行が無い・知らない鍵・未知の写真の ID・min と max が無い・値が求まらない（道が無い・数でない）・
#   選んだ写真の行が 1 つも無い（空の選択）・値が範囲の外。
import json
import os

ROW_KEYS = ("photo", "cliff", "quantity", "min", "max", "note")


def load_rows(path):
    with open(path, encoding="utf-8-sig") as file:
        spec = json.load(file)
    rows = spec.get("rows") if isinstance(spec, dict) else spec
    return rows if isinstance(rows, list) else None


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
        rows = load_rows(spec_path)
    except (OSError, ValueError) as error:
        return [f"仕様 {spec_path} が読めない: {error}"]
    if not rows:
        return [f"仕様 {spec_path} に行が無い"]
    failures = []
    evaluated = 0
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
        where = f"photo={photo}" + (f" cliff={row['cliff']}" if row.get("cliff") else "") + f" quantity={quantity}"
        if value is None:
            failures.append(f"{label}（{where}）の値が求まらない")
            print(f"{prefix} {where} value=undetermined result=fail")
            continue
        low, high = row.get("min"), row.get("max")
        ok = (low is None or value >= low) and (high is None or value <= high)
        print(f"{prefix} {where} value={value:.4f} min={low} max={high} result={'pass' if ok else 'fail'}")
        if not ok:
            failures.append(f"{label}（{where}）の値 {value:.4f} が範囲 [{low}, {high}] の外")
    if evaluated == 0:
        failures.append(f"仕様 {spec_path} に選んだ写真の行が無い（空の選択）")
    return failures

# 荒野の正解の絵の比較（SCN-WLR-*）の記録を、2 つの署名に分ける（SCN-WLR-REF-SPLIT。ReferenceCheck.py から呼ぶ。単独では動かさない）。
#
# 参照の入力（Cycles の参照の署名。<参照>/cycles/reference.json）:
#   地形のメッシュ（書き出しの元の入力のハッシュ〔AO の画像 orm_ao は除く〕・書き出しの一覧・読んだ PLY）、材質の値の表（Cycles が
#   使う値だけ。材質の AO〔AO の段・AO の定数〕は Cycles が使わないので入れない。灰色では CommonLight.json の gray_material）、共通の照明（CommonLight.json・GrayRegions.py）、
#   カメラと Blender のカメラのずらし量、Blender の台本と出力の画像のハッシュ。
#   描画のコードや Game.exe は入れないので、参照の入力が同じなら、エンジンの実装が変わっても Cycles の参照をそのまま使える。
# エンジンの実装（Game の撮影の署名。撮影の inputs.json の engine_impl）:
#   描画のコード（ENGINE_IMPL_PATHS。起動引数を解く Game/GameApplicationHandler.cpp を含む）、カメラ台本（起動引数の
#   --camera-script= のファイル）、撮影に使った Game.exe、出力の画像（<視点>.png・<視点>.*.pfm・<視点>.<名前>.png）のハッシュ。
#   エンジンの画も、遠景のぼけの ref2x・resident・FXAA などの対照の組も、これが今と一致しないと古いとして落とす。
#   撮影に使った共通の照明のハッシュも書き、参照と同じ照明で撮ったことを照らす。
#
# 今のファイルとの照合は、ファイルの一覧を git（追跡か、無視されていないもの）から取り、中身を root（既定はリポジトリのルート）から
# 読む。--ref-split-selftest は root に写しを作り、写しの側だけを書き換えて負の確認をする（リポジトリのファイルは触らない）。
import hashlib
import json
import os
import re
import shutil
import tempfile

# エンジンの実装の入力（リポジトリのルートからのパス。フォルダはその下のすべて）
ENGINE_IMPL_PATHS = (
    "Assets/Shaders",
    "Library/Core/Private/Rendering",
    "Library/Core/Public/Rendering",
    "Game/GameModes/Wasteland",
    "Game/GameApplicationHandler.cpp",
    "Scripts/CaptureScene.ps1",
)
CAMERA_SCRIPT_PREFIX = "--camera-script="
COMMON_LIGHT_PATH = "Scripts/Terrain/CommonLight.json"
GRAY_REGIONS_PATH = "Scripts/Terrain/GrayRegions.py"
BLENDER_SCRIPT = "Scripts/Terrain/BlenderReference.py"
MATERIAL_SCRIPT = "Scripts/Terrain/TerrainMaterialReference.py"
OSL_SOURCE = "Scripts/Terrain/TerrainAlbedo.osl"
REFERENCE_FILE = "reference.json"
REFERENCE_KINDS = {"gray": "cycles_gray", "lit": "cycles_lit"}
# 実材質の画素ごとの値のうち、Cycles が使うもの（材質の AO の occlusion は Cycles に掛けないので入れない）
LIT_SCREEN_KEYS = ("albedo", "shaded_normal", "roughness")
# 値の表のうち値でないもの（実装のファイルのハッシュ）と、AO の段
TABLE_IMPLEMENTATION_KEYS = ("shader_sha256", "material_log_sha256")
AO_TEXTURE_SUFFIX = "_ao"
# シェーダーの定数のうち材質の AO だけに効くもの（名前にこれを含む。STRATA_*_MEAN_OCCLUSION など）
AO_CONSTANT_MARK = "OCCLUSION"
# 地形の資産の組のテクスチャのうち AO の画像を指す鍵（地形のメッシュの入力に入れない）
AO_ASSET_KEYS = ("orm_ao",)
# 出力の画像（視点の名前に続くもの）
OUTPUT_RE = r"^{view}(\.[A-Za-z0-9_-]+)*\.(png|pfm)$"
SELFTEST_DIR = os.path.join(".harness", "runs", "scene-capture", "SCN-WLR-REF-SPLIT")


def canonical_sha(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, ensure_ascii=False).encode("utf-8")).hexdigest()


def sha_at(root, rel):
    path = os.path.join(root, rel)
    if not os.path.isfile(path):
        return "absent"
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def hash_paths(rc, paths, root):
    """パスごとのハッシュと全体のハッシュ。一覧は git から、中身は root から読む"""
    files = rc.list_input_files(list(paths))
    total = hashlib.sha256()
    per_path = {p: hashlib.sha256() for p in paths}
    for rel in files:
        line = f"{rel}\0{sha_at(root, rel)}\n".encode("utf-8")
        total.update(line)
        for p in paths:
            if rel == p or rel.startswith(p.rstrip("/") + "/"):
                per_path[p].update(line)
    absent = [p for p in paths if not os.path.exists(os.path.join(rc.REPO_ROOT, p))]
    for p in absent:
        total.update(f"{p}\0absent\n".encode("utf-8"))
    return {"sha256": total.hexdigest(), "file_count": len(files),
            "paths": {p: ("absent" if p in absent else per_path[p].hexdigest()) for p in paths}}


def engine_outputs(rc, capture_dir):
    """撮影の出力の画像（metrics.json の視点ごとの PNG・PFM）の名前 → ハッシュ"""
    views = rc.views_from_metrics(capture_dir) or []
    outputs = {}
    names = sorted(os.listdir(capture_dir)) if os.path.isdir(capture_dir) else []
    for view in views:
        pattern = re.compile(OUTPUT_RE.format(view=re.escape(view)))
        for name in names:
            if pattern.match(name):
                outputs[name] = sha_at(capture_dir, name)
    return outputs


def camera_scripts(game_args, root):
    scripts = {}
    for arg in game_args or []:
        if arg.startswith(CAMERA_SCRIPT_PREFIX):
            rel = arg[len(CAMERA_SCRIPT_PREFIX):].replace("\\", "/")
            scripts[rel] = sha_at(root, rel)
    return scripts


def engine_identity(record):
    """撮影の組が同じ実装で撮られたかを比べる値（出力の画像は組ごとに違うので入れない）"""
    return canonical_sha({k: record.get(k) for k in ("inputs_sha256", "game_exe_sha256", "camera_scripts")})


def engine_impl_record(rc, capture_dir, game_exe, root=None):
    root = root or rc.REPO_ROOT
    hashed = hash_paths(rc, ENGINE_IMPL_PATHS, root)
    exe_rel = os.path.relpath(game_exe, rc.REPO_ROOT).replace("\\", "/") if game_exe else None
    record = {
        "inputs_sha256": hashed["sha256"],
        "file_count": hashed["file_count"],
        "paths": hashed["paths"],
        "game_exe": exe_rel,
        "game_exe_sha256": sha_at(root, exe_rel) if exe_rel else None,
        "camera_scripts": camera_scripts(rc.plate_capture_args(capture_dir), root),
        "common_light_sha256": sha_at(root, COMMON_LIGHT_PATH),
        "outputs_sha256": engine_outputs(rc, capture_dir),
    }
    record["identity"] = engine_identity(record)
    return record


def write_engine_record(rc, capture_dir, game_exe, require_same_exe):
    """保存した撮影の inputs.json に engine_impl を書き足す（--record-engine）。撮影に使った Game.exe と同じ中身の exe でだけ書く"""
    path = os.path.join(capture_dir, "inputs.json")
    if not os.path.isfile(path):
        return False, "inputs.json が無い"
    with open(path, encoding="utf-8") as f:
        record = json.load(f)
    impl = engine_impl_record(rc, capture_dir, game_exe)
    if not impl["outputs_sha256"]:
        return False, "metrics.json の視点から出力の画像を辿れない（空の署名は書かない）"
    if require_same_exe and record.get("game_exe_sha256") != impl["game_exe_sha256"]:
        return False, "撮影に使った Game.exe と今の Game.exe が違う（撮り直す）"
    record["engine_impl"] = impl
    with open(path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    return True, impl["identity"]


def check_engine(rc, capture_dir, root=None):
    """撮影のエンジンの実装を今と照らす。(合格か, 理由の一覧, 記録)。記録には identity（同じ実装か比べる値）が入る"""
    root = root or rc.REPO_ROOT
    path = os.path.join(capture_dir, "inputs.json")
    if not os.path.isfile(path):
        return False, ["inputs.json が無い（撮影が入力の記録を書いていない）"], None
    with open(path, encoding="utf-8") as f:
        record = (json.load(f) or {}).get("engine_impl")
    if not record:
        return False, ["inputs.json にエンジンの実装の記録（engine_impl）が無い（古い形の撮影。撮り直す）"], None
    reasons = []
    current = hash_paths(rc, ENGINE_IMPL_PATHS, root)
    if current["sha256"] != record.get("inputs_sha256"):
        changed = [p for p, h in current["paths"].items() if (record.get("paths") or {}).get(p) != h]
        reasons.append(f"エンジンの実装が撮影の後に変わった（古い結果）: {', '.join(changed) or '一覧'}")
    exe = record.get("game_exe")
    if not exe or not record.get("game_exe_sha256"):
        reasons.append("撮影に使った Game.exe の記録が無い")
    elif sha_at(root, exe) != record["game_exe_sha256"]:
        reasons.append(f"撮影に使った Game.exe（{exe}）が今のビルドと違うか無い（古い結果）")
    for rel, digest in (record.get("camera_scripts") or {}).items():
        if sha_at(root, rel) != digest:
            reasons.append(f"カメラ台本 {rel} が撮影の後に変わった（古い結果）")
    args_scripts = set(camera_scripts(rc.plate_capture_args(capture_dir), root))
    if args_scripts != set(record.get("camera_scripts") or {}):
        reasons.append("起動引数のカメラ台本と記録が食い違う")
    outputs = record.get("outputs_sha256") or {}
    if not outputs:
        reasons.append("出力の画像の記録が無い")
    changed = [n for n, digest in outputs.items() if sha_at(capture_dir, n) != digest]
    if changed:
        reasons.append(f"出力の画像が撮影の後に書き換わったか無い: {', '.join(changed[:5])}"
                       f"{' ほか' if len(changed) > 5 else ''}（{len(changed)} 個）")
    if engine_identity(record) != record.get("identity"):
        reasons.append("エンジンの実装の記録の identity が中身と合わない")
    return not reasons, reasons, record


# ---- 参照の入力 ----

def cycles_constants(constants):
    """シェーダーの定数のうち Cycles が使うもの（材質の AO の定数を除く）"""
    return {name: item for name, item in (constants or {}).items() if AO_CONSTANT_MARK not in name}


def material_values(table):
    """値の表のうち Cycles が使う値（実装のファイルのハッシュと、AO の段・AO の定数を除く）"""
    values = {k: v for k, v in table.items() if k not in TABLE_IMPLEMENTATION_KEYS}
    values["shader_constants"] = cycles_constants(table.get("shader_constants"))
    values["textures"] = {name: entry for name, entry in (table.get("textures") or {}).items()
                          if not name.endswith(AO_TEXTURE_SUFFIX)}
    return values


def load_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def reference_record(rc, reference_root, mode, root=None):
    """今のファイルから参照の入力の記録を作る。(記録, 失敗の一覧)"""
    root = root or rc.REPO_ROOT
    # 保存したデータ（書き出しのメッシュ・値の表・OSL・ミップの元の画像）はリポジトリから、台本と照明は root から読む
    data = rc.REPO_ROOT
    cycles_dir = os.path.join(reference_root, rc.GRAY_CYCLES_DIR)
    failures = []
    inputs_path = os.path.join(cycles_dir, "inputs.json")
    if not os.path.isfile(inputs_path):
        return None, [f"{rc.GRAY_CYCLES_DIR}/inputs.json が無い（Cycles を描いていない）"]
    blender = load_json(inputs_path)
    if blender.get("kind") != REFERENCE_KINDS[mode]:
        failures.append(f"Cycles の記録の種類が {REFERENCE_KINDS[mode]} でない（{blender.get('kind')}）")
    views = sorted((blender.get("views") or {}).keys())
    record = {
        "kind": f"reference_{mode}",
        "terrain_mesh": {
            "inputs_sha256": rc.hash_inputs("terrain_mesh", AO_ASSET_KEYS)["files_sha256"],
            "mesh_dir": blender.get("mesh_dir"),
            "manifest_sha256": sha_at(data, os.path.join(blender.get("mesh_dir", ""), "manifest.json")),
            "ply_sha256": {n: sha_at(data, os.path.join(blender.get("mesh_dir", ""), n))
                           for n in sorted(blender.get("ply_sha256") or {})},
        },
        "common_light": {"common_light_sha256": sha_at(root, COMMON_LIGHT_PATH),
                         "gray_regions_sha256": sha_at(root, GRAY_REGIONS_PATH)},
        "cameras": {},
        "blender": {"scripts": {BLENDER_SCRIPT: sha_at(root, BLENDER_SCRIPT)}, "outputs_sha256": {}, "plates": {}},
    }
    for view in views:
        meta_path = os.path.join(cycles_dir, f"{view}.cycles.json")
        if not os.path.isfile(meta_path):
            failures.append(f"{rc.GRAY_CYCLES_DIR}/{view}.cycles.json が無い")
            continue
        meta = load_json(meta_path)
        record["cameras"][view] = {"camera": meta.get("camera_engine"), "width": meta.get("width"),
                                   "height": meta.get("height"), "camera_shift_px": meta.get("camera_shift_px")}
        record["blender"]["outputs_sha256"][view] = {n: sha_at(cycles_dir, n)
                                                     for n in sorted(meta.get("outputs_sha256") or {})}
    for case in rc.PLATE_CASES if mode == "gray" else ():
        record["blender"]["plates"][case] = sha_at(cycles_dir, f"plate_{case}.pfm")
    if mode == "gray":
        record["material"] = {"gray_material": rc.load_common_light().get("gray_material")}
    else:
        material_dir = os.path.join(reference_root, rc.LIT_MATERIAL_DIR)
        material_inputs = os.path.join(material_dir, "inputs.json")
        if not os.path.isfile(material_inputs):
            failures.append(f"{rc.LIT_MATERIAL_DIR}/inputs.json が無い（画素ごとの材質を描いていない）")
            return record, failures
        material_record = load_json(material_inputs)
        material = material_record.get("material") or {}
        table_path = os.path.join(data, material.get("table", ""))
        if not os.path.isfile(table_path):
            failures.append("値の表（terrain_material.json）が無い")
            return record, failures
        table = load_json(table_path)
        mips_path = os.path.join(data, material_record.get("mips_dir", ""), "mips.json")
        mips = load_json(mips_path) if os.path.isfile(mips_path) else {}
        record["blender"]["scripts"][MATERIAL_SCRIPT] = sha_at(root, MATERIAL_SCRIPT)
        record["blender"]["scripts"][OSL_SOURCE] = sha_at(root, OSL_SOURCE)
        record["material"] = {
            "values_sha256": canonical_sha(material_values(table)),
            "osl_sha256": sha_at(data, material.get("osl", "")),
            "mips_sources_sha256": {name: sha_at(data, entry["source"])
                                    for name, entry in sorted((mips.get("textures") or {}).items())
                                    if not name.endswith(AO_TEXTURE_SUFFIX)},
            "screen_sha256": {view: {key: sha_at(material_dir, f"{view}.{key}.pfm") for key in LIT_SCREEN_KEYS}
                              for view in views},
        }
        for view in views:
            shift = ((material_record.get("views") or {}).get(view) or {}).get("camera_shift_px")
            if view in record["cameras"] and shift != record["cameras"][view]["camera_shift_px"]:
                failures.append(f"{view}: Cycles のカメラのずらしが画素ごとの材質を描いたときと違う")
    return record, failures


def check_drawn_inputs(rc, reference_root, mode):
    """記録を書く前に、Cycles と材質を描いたときの入力（Blender の記録）が今のファイルと同じことを確かめる（エンジンの撮影は見ない）"""
    cycles_dir = os.path.join(reference_root, rc.GRAY_CYCLES_DIR)
    ok, reasons, blender = rc.check_blender_inputs(cycles_dir)
    if blender is None:
        return reasons
    for key, rel in (("gray_regions_sha256", GRAY_REGIONS_PATH), ("common_light_sha256", COMMON_LIGHT_PATH)):
        if sha_at(rc.REPO_ROOT, rel) != blender.get(key):
            reasons.append(f"{rel} が描いた後に変わった（描き直す）")
    if mode == "lit":
        material_dir = os.path.join(reference_root, rc.LIT_MATERIAL_DIR)
        m_ok, m_reasons, material_record = rc.check_blender_inputs(material_dir)
        reasons.extend(m_reasons)
        material = (material_record or {}).get("material") or {}
        if sha_at(rc.REPO_ROOT, MATERIAL_SCRIPT) != (material_record or {}).get("material_script_sha256"):
            reasons.append(f"{MATERIAL_SCRIPT} が描いた後に変わった（描き直す）")
        if sha_at(rc.REPO_ROOT, OSL_SOURCE) != material.get("osl_source_sha256"):
            reasons.append(f"{OSL_SOURCE} が描いた後に変わった（描き直す）")
        for key, rel in (("table_sha256", material.get("table", "")), ("osl_sha256", material.get("osl", ""))):
            if sha_at(rc.REPO_ROOT, rel) != material.get(key):
                reasons.append(f"{rel or key} が描いた後に変わった")
        osl = sha_at(rc.REPO_ROOT, (blender.get("material_osl") or ""))
        if osl != blender.get("material_osl_sha256"):
            reasons.append("Cycles の当たりの材質の OSL が描いた後に変わった")
        for view, view_record in (blender.get("views") or {}).items():
            for key, digest in (view_record.get("screen_material_sha256") or {}).items():
                if sha_at(material_dir, f"{view}.{key}.pfm") != digest:
                    reasons.append(f"{view}: 画素ごとの材質 {key} が Cycles で描いた後に変わった")
    return reasons


def write_reference(rc, reference_root, mode):
    """参照の入力の記録 <参照>/cycles/reference.json を書く（--record-reference）"""
    reasons = check_drawn_inputs(rc, reference_root, mode)
    record, failures = reference_record(rc, reference_root, mode)
    reasons.extend(failures)
    # 新しい参照はずらし 0 で描く（ずらしありの旧参照は書いてある記録のまま使う。ReferenceCheck.SHIFTED_REFERENCE_SCOPE）
    for view, camera in sorted(((record or {}).get("cameras") or {}).items()):
        shift = rc.reference_shift_px(camera)
        if any(v != 0.0 for v in shift):
            reasons.append(f"{view}: Blender のカメラが ({shift[0]:+.3f}, {shift[1]:+.3f}) 画素ずらしてある"
                           "（新しい参照はずらし 0 で描く）")
    if reasons:
        return False, reasons
    record["signature_sha256"] = canonical_sha(record)
    with open(os.path.join(reference_root, rc.GRAY_CYCLES_DIR, REFERENCE_FILE), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    return True, [record["signature_sha256"]]


def diff_keys(saved, current, prefix=""):
    """2 つの記録の違う所（ドット区切りの鍵）"""
    if isinstance(saved, dict) and isinstance(current, dict):
        keys = []
        for key in sorted(set(saved) | set(current)):
            keys.extend(diff_keys(saved.get(key), current.get(key), f"{prefix}{key}."))
        return keys
    return [] if saved == current else [prefix.rstrip(".")]


def check_reference(rc, reference_root, mode, root=None):
    """参照の入力を今のファイルと照らす。(合格か, 理由の一覧, 記録)"""
    path = os.path.join(reference_root, rc.GRAY_CYCLES_DIR, REFERENCE_FILE)
    if not os.path.isfile(path):
        return False, [f"{rc.GRAY_CYCLES_DIR}/{REFERENCE_FILE} が無い（参照の入力の記録を書いていない。--record-reference）"], None
    saved = load_json(path)
    reasons = []
    body = {k: v for k, v in saved.items() if k != "signature_sha256"}
    if canonical_sha(body) != saved.get("signature_sha256"):
        reasons.append("参照の入力の記録の署名が中身と合わない")
    current, failures = reference_record(rc, reference_root, mode, root)
    reasons.extend(failures)
    if current is not None:
        # 同じ画を描く前の版の台本で描いた参照は、今の台本で描いたものとして照らす（ReferenceCheck.BLENDER_SCRIPT_EQUIVALENT_SHA256）
        scripts = current["blender"]["scripts"]
        saved_script = (((body.get("blender") or {}).get("scripts")) or {}).get(BLENDER_SCRIPT)
        if rc.blender_script_matches(scripts[BLENDER_SCRIPT], saved_script):
            scripts[BLENDER_SCRIPT] = saved_script
        changed = diff_keys(body, current)
        if changed:
            shown = sorted({".".join(k.split(".")[:3]) for k in changed})
            reasons.append(f"参照の入力が描いた後に変わった（古い参照）: {', '.join(shown[:6])}"
                           f"{' ほか' if len(shown) > 6 else ''}")
    return not reasons, reasons, saved


def check_reference_engine(reference, engine_records, engine_logs, table=None):
    """参照とエンジンの撮影が同じ条件か（共通の照明、実材質では値の表）。理由の一覧"""
    import TerrainMaterialReference as TM
    reasons = []
    light = ((reference or {}).get("common_light") or {}).get("common_light_sha256")
    for name, record in engine_records.items():
        if record and record.get("common_light_sha256") != light:
            reasons.append(f"{name}: 撮影の共通の照明が参照と違う")
    if (reference or {}).get("kind") == "reference_lit":
        if table is None:
            return reasons + ["参照の値の表（terrain_material.json）が無い"]
        json_text, material_now, log_failures = TM.material_from_logs(engine_logs)
        reasons.extend(log_failures)
        if material_now is not None:
            reasons.extend(compare_table(TM, table, material_now))
    return reasons


def compare_table(TM, table, material_now):
    """今の撮影のログの材質の値・今のシェーダーの定数が、参照の値の表と同じか。理由の一覧"""
    reasons = []
    if material_now != table.get("material"):
        reasons.append("撮影のログの材質の値（WASTELAND_TERRAIN_MATERIAL）が参照の値の表と違う（参照を描き直す）")
    elif TM.pack_params(material_now) != table.get("packed"):
        reasons.append("今の詰め方の式で詰めた値が参照の値の表と違う（参照を描き直す）")
    if cycles_constants(TM.shader_constants()) != cycles_constants(table.get("shader_constants")):
        reasons.append("今のシェーダーの定数（Cycles が再現する値）が参照の値の表と違う（参照を描き直す）")
    return reasons


def load_reference_table(rc, reference_root):
    material_inputs = os.path.join(reference_root, rc.LIT_MATERIAL_DIR, "inputs.json")
    if not os.path.isfile(material_inputs):
        return None
    table_rel = (load_json(material_inputs).get("material") or {}).get("table", "")
    table_path = os.path.join(rc.REPO_ROOT, table_rel)
    return load_json(table_path) if os.path.isfile(table_path) else None


def resolve_view_dirs(engine_dirs, views, suffix):
    """視点ごとの撮影先（与えた撮影先のうち、<視点><suffix> を持つ最初のもの）。見つからない視点は入れない"""
    found = {}
    for view in views:
        for d in engine_dirs:
            if os.path.isfile(os.path.join(d, f"{view}{suffix}")):
                found[view] = d
                break
    return found


def resolve_group_dir(engine_dirs, rel):
    """組の撮影先（与えた撮影先のうち、<rel>/metrics.json を持つ最初のもの。無ければ先頭の撮影先の下）"""
    for d in engine_dirs:
        candidate = os.path.normpath(os.path.join(d, rel))
        if os.path.isfile(os.path.join(candidate, "metrics.json")):
            return candidate
    return os.path.normpath(os.path.join(engine_dirs[0], rel))


# ---- --ref-split-selftest（負の確認）----

def mirror_root(rc, temp_root, game_exe_rel):
    """エンジンの実装・参照の入力の比べるファイルを temp_root へ写す（中身を root から読む照合の写し）"""
    rels = set(rc.list_input_files(list(ENGINE_IMPL_PATHS)))
    rels.update([COMMON_LIGHT_PATH, GRAY_REGIONS_PATH, BLENDER_SCRIPT, MATERIAL_SCRIPT, OSL_SOURCE])
    rels.update(rc.list_input_files(["Scripts/CameraScripts"]))
    if game_exe_rel:
        rels.add(game_exe_rel)
    for rel in rels:
        src = os.path.join(rc.REPO_ROOT, rel)
        if os.path.isfile(src):
            dst = os.path.join(temp_root, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)


def append_byte(path):
    with open(path, "ab") as f:
        f.write(b"\n")


def run_selftest(rc, verify):
    """保存の写しで、署名の分け方の正と負を確かめる。結果を SELFTEST_DIR/ref_split_selftest.json に書く"""
    sc = os.path.join(rc.REPO_ROOT, ".harness", "runs", "scene-capture")
    gray_src = os.path.join(sc, "SCN-WLR-CYCLES-GRAY")
    motion_src = os.path.join(sc, "SCN-WLR-FAR-BLUR-MOTION", "fxaa")
    out_dir = os.path.join(rc.REPO_ROOT, SELFTEST_DIR)
    os.makedirs(out_dir, exist_ok=True)
    cases = []
    failures = []

    def expect(name, ok, want_ok, reasons, want_text=None):
        hit = (ok == want_ok) and (want_text is None or any(want_text in r for r in reasons))
        cases.append({"case": name, "ok": bool(ok), "expected_ok": want_ok, "reasons": reasons, "pass": hit})
        print(f"REFERENCE_CHECK ref_split_selftest case={name} ok={int(bool(ok))} expected={int(want_ok)} pass={int(hit)}"
              + (f" reason={reasons[0]}" if reasons else ""))
        if not hit:
            failures.append(f"{name}: 期待 {want_ok}、結果 {ok}（{'; '.join(reasons[:2])}）")

    temp = tempfile.mkdtemp(prefix="ref_split_")
    try:
        root = os.path.join(temp, "root")
        exe_rel = "build/Game/RelWithDebInfo/Game.exe"
        mirror_root(rc, root, exe_rel)
        # 写し: 灰色のエンジンの撮影（ref_far_01 の出力と記録）と、動きの FXAA の組（カメラ台本つき）、Cycles の参照
        engine_copy = os.path.join(temp, "gray")
        os.makedirs(engine_copy)
        for name in os.listdir(gray_src):
            if name.startswith("ref_far_01.") or name in ("metrics.json", "inputs.json", "views.Game.log"):
                shutil.copyfile(os.path.join(gray_src, name), os.path.join(engine_copy, name))
        shutil.copytree(os.path.join(gray_src, rc.GRAY_CYCLES_DIR), os.path.join(engine_copy, rc.GRAY_CYCLES_DIR))
        motion_copy = os.path.join(temp, "motion")
        shutil.copytree(motion_src, motion_copy)
        # 写しの撮影に、写しの側の今の実装で撮ったとしてエンジンの実装の記録を書く
        for capture in (engine_copy, motion_copy):
            with open(os.path.join(capture, "inputs.json"), encoding="utf-8") as f:
                record = json.load(f)
            record["engine_impl"] = engine_impl_record(rc, capture, os.path.join(rc.REPO_ROOT, exe_rel), root)
            with open(os.path.join(capture, "inputs.json"), "w", encoding="utf-8") as f:
                json.dump(record, f, indent=2, ensure_ascii=False)
        ok, reasons = write_reference(rc, engine_copy, "gray")
        expect("reference_recorded", ok, True, [] if ok else reasons)

        ok, reasons, _ = check_engine(rc, engine_copy, root)
        expect("engine_current", ok, True, reasons)
        ok, reasons, _ = check_engine(rc, motion_copy, root)
        expect("motion_engine_current", ok, True, reasons)
        ok, reasons, _ = check_reference(rc, engine_copy, "gray", root)
        expect("reference_current", ok, True, reasons)

        # エンジンの実装の 1 ファイルを書き換える → エンジンの撮影は古い・参照は古くならない
        append_byte(os.path.join(root, "Game", "GameApplicationHandler.cpp"))
        ok, reasons, _ = check_engine(rc, engine_copy, root)
        expect("engine_file_changed", ok, False, reasons, "Game/GameApplicationHandler.cpp")
        ok, reasons, _ = check_reference(rc, engine_copy, "gray", root)
        expect("reference_after_engine_change", ok, True, reasons)
        shader = next(r for r in rc.list_input_files(["Assets/Shaders"]) if r.endswith(".frag"))
        append_byte(os.path.join(root, shader))
        ok, reasons, _ = check_reference(rc, engine_copy, "gray", root)
        expect("reference_after_shader_change", ok, True, reasons)
        mirror_root(rc, root, exe_rel)

        # Game.exe を書き換える → 古い
        append_byte(os.path.join(root, exe_rel))
        ok, reasons, _ = check_engine(rc, engine_copy, root)
        expect("game_exe_changed", ok, False, reasons, "Game.exe")
        mirror_root(rc, root, exe_rel)

        # カメラ台本を書き換える → 動きの撮影は古い
        script = next(iter(json.load(open(os.path.join(motion_copy, "inputs.json"), encoding="utf-8"))
                           ["engine_impl"]["camera_scripts"]))
        append_byte(os.path.join(root, script))
        ok, reasons, _ = check_engine(rc, motion_copy, root)
        expect("camera_script_changed", ok, False, reasons, "カメラ台本")
        mirror_root(rc, root, exe_rel)

        # 出力の画像を書き換える → 古い
        append_byte(os.path.join(engine_copy, "ref_far_01.png"))
        ok, reasons, _ = check_engine(rc, engine_copy, root)
        expect("engine_output_changed", ok, False, reasons, "出力の画像")

        # 参照の入力（共通の照明・Cycles の出力）を書き換える → 参照は古い
        append_byte(os.path.join(root, COMMON_LIGHT_PATH))
        ok, reasons, _ = check_reference(rc, engine_copy, "gray", root)
        expect("reference_light_changed", ok, False, reasons, "common_light")
        mirror_root(rc, root, exe_rel)
        append_byte(os.path.join(engine_copy, rc.GRAY_CYCLES_DIR, "ref_far_01.cycles.hdr.pfm"))
        ok, reasons, _ = check_reference(rc, engine_copy, "gray", root)
        expect("reference_output_changed", ok, False, reasons, "blender")
    finally:
        shutil.rmtree(temp, ignore_errors=True)

    report = {"mode": "ref_split_selftest", "verify": bool(verify), "cases": cases, "failures": failures}
    report_path = os.path.join(out_dir, "ref_split_selftest.json")
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=ref_split_selftest cases={len(cases)} "
          f"report={report_path}")
    return 0 if not failures else 1

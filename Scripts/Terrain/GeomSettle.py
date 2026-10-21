# VRAM の予算が足りないときのジオメトリのページの落ち着きの検査（SCN-WL-GEOM-LOWVRAM。ReferenceCheck.py --geom-settle から呼ぶ。
# 単独では動かさない）。
#
# 入力: CaptureScene.ps1 で -GameArgs vram-budget-mb=<MB>,geom-settle-trace=<秒> を付けて撮った出力先（metrics.json・<起動>.Game.log・
# inputs.json）。Game は読み込みが落ち着いた時点から 1 秒ごとに GEOM_SETTLE_TRACE の行を書き（Game/Debug/GeometrySettleTrace.h）、
# 書き終えてから撮る。
#   落ちる: 記録が無い・秒数に足りない、書き込みと追い出しが止まる前から記録した（settle_timeout=1）、予算の上書きが効いていない（VRAM_BUDGET の cap_mb・記録の上限が引数と違う・目標が無い）、
#           記録の間に追い出しか書き込みがあった（落ち着いていない）、常駐の量がページの目標を超えた、ジオメトリのプールの使用量が
#           ジオメトリの枠の目標を超えた、段ごとの常駐の数が記録の間に変わった、--verify で入力のハッシュが撮影の後に変わった
#   記録する: 予算が足りていたか（要求されたまま常駐していないページが残る、または目標に収まらず書き込みを見送ったフレームが増えた）、
#           段ごとの常駐と未常駐の数、最も細かい常駐の段、予算と使用量の値
# 結果は <撮影>/geom_settle_check.json に書く。
import json
import os
import re

REPORT = "geom_settle_check.json"
TRACE_PREFIX = "GEOM_SETTLE_TRACE "
BEGIN_PREFIX = "GEOM_SETTLE_TRACE_BEGIN "
END_PREFIX = "GEOM_SETTLE_TRACE_END "
# ログの MB は小数 2 桁に丸めて書くので、比較にその丸めの分だけ余裕を持たせる
MB_ROUNDING = 0.01
KEY_VALUE = re.compile(r"([a-z_]+)=(\S+)")


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def parse_fields(text):
    fields = {}
    for key, value in KEY_VALUE.findall(text):
        fields[key] = value
    return fields


def parse_levels(text):
    if not text or text == "-":
        return []
    return [int(v) for v in text.split("/")]


def game_arg_value(game_args, name):
    prefix = f"--{name}="
    for argument in game_args or []:
        if argument.startswith(prefix):
            return argument[len(prefix):]
    return None


def read_log(path):
    """ログから記録の始め・行・終わりと、記録の間の VRAM_BUDGET の行を取り出す"""
    begin = None
    end = None
    samples = []
    budgets = []
    budgets_in_trace = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if BEGIN_PREFIX in line:
                begin = parse_fields(line.split(BEGIN_PREFIX, 1)[1])
            elif END_PREFIX in line:
                end = parse_fields(line.split(END_PREFIX, 1)[1])
            elif TRACE_PREFIX in line and " t=" in line:
                fields = parse_fields(line.split(TRACE_PREFIX, 1)[1])
                sample = {k: float(v) for k, v in fields.items() if k not in ("levels_resident", "levels_pending")}
                sample["levels_resident"] = parse_levels(fields.get("levels_resident"))
                sample["levels_pending"] = parse_levels(fields.get("levels_pending"))
                samples.append(sample)
            elif "VRAM_BUDGET " in line:
                fields = parse_fields(line.split("VRAM_BUDGET ", 1)[1])
                budgets.append(fields)
                if begin is not None and end is None:
                    budgets_in_trace.append(fields)
    return begin, end, samples, budgets, budgets_in_trace


def check_view(capture_dir, view, cap_mb, seconds):
    failures = []
    result = {"view": view["view"], "game_log": view.get("game_log")}
    log_path = os.path.join(capture_dir, view.get("game_log") or "")
    if not view.get("game_log") or not os.path.isfile(log_path):
        return result, ["Game.log が無い（RelWithDebInfo か Debug で撮る）"]
    begin, end, samples, budgets, budgets_in_trace = read_log(log_path)
    if begin is None or end is None or not samples:
        return result, ["GEOM_SETTLE_TRACE の記録が無いか終わっていない（--geom-settle-trace を付けて撮る）"]
    result["samples"] = len(samples)
    result["streaming_wait_s"] = float(begin.get("streaming_wait_s", "nan"))
    if begin.get("settle_timeout") == "1":
        failures.append(f"ページの書き込みと追い出しが {result['streaming_wait_s']:.0f} 秒待っても止まらず、止まる前から記録した")
    result["trace_seconds"] = samples[-1]["t"]
    if len(samples) < seconds + 1 or samples[-1]["t"] < seconds:
        failures.append(f"記録が {seconds} 秒に足りない（{len(samples)} 行・最後 t={samples[-1]['t']:.2f}）")

    # 予算の上書きが効いていること
    caps = sorted({b.get("cap_mb") for b in budgets})
    result["vram_budget_cap_mb"] = caps
    if caps != [str(cap_mb)]:
        failures.append(f"VRAM_BUDGET の cap_mb が {caps}（--vram-budget-mb={cap_mb} が効いていない）")
    if any(s["limited"] != 1 for s in samples):
        failures.append("ページの常駐の目標が無い行がある（予算の上限が効いていない）")
    ceiling = max(s["ceiling_mb"] for s in samples)
    result["ceiling_mb"] = ceiling
    if ceiling > cap_mb + MB_ROUNDING:
        failures.append(f"記録の上限 {ceiling:.2f} MB が --vram-budget-mb={cap_mb} を超える")
    heap_usage = [float(b["heap_usage_mb"]) for b in budgets_in_trace if "heap_usage_mb" in b]
    result["heap_usage_mb_in_trace"] = heap_usage

    # 落ち着き: 記録の間に追い出しも書き込みも無く、段ごとの常駐の数が変わらない
    first, last = samples[0], samples[-1]
    evicted = last["evicted"] - first["evicted"]
    uploaded = last["uploaded"] - first["uploaded"]
    result["evicted_in_trace"] = int(evicted)
    result["uploaded_in_trace"] = int(uploaded)
    if evicted != 0:
        failures.append(f"記録の間に {int(evicted)} ページを追い出した（追い出しが止まっていない）")
    if uploaded != 0:
        failures.append(f"記録の間に {int(uploaded)} ページを書き込んだ（読み込みが落ち着いていない）")
    changed = [s["t"] for s in samples if s["levels_resident"] != first["levels_resident"]]
    if changed:
        failures.append(f"段ごとの常駐の数が記録の間に変わった（t={changed[0]:.2f} から）")

    # 予算の内: 常駐の量がページの目標を、プールの使用量がジオメトリの枠の目標を超えない
    over_page = [s for s in samples if s["resident_mb"] > s["page_budget_mb"] + MB_ROUNDING]
    over_pool = [s for s in samples if s["pool_used_mb"] > s["geometry_target_mb"] + MB_ROUNDING]
    if over_page:
        s = over_page[0]
        failures.append(f"常駐の量 {s['resident_mb']:.2f} MB がページの目標 {s['page_budget_mb']:.2f} MB を超える（t={s['t']:.2f}）")
    if over_pool:
        s = over_pool[0]
        failures.append(f"プールの使用量 {s['pool_used_mb']:.2f} MB が枠の目標 {s['geometry_target_mb']:.2f} MB を超える（t={s['t']:.2f}）")

    pending = int(last["wanted"] + last["reading"] + last["ready"])
    blocked = int(last["budget_blocked"] - first["budget_blocked"])
    resident_levels = last["levels_resident"]
    pending_levels = last["levels_pending"]
    result.update({
        "budget_short": bool(pending > 0 or blocked > 0),
        "pending_pages": pending,
        "budget_blocked_in_trace": blocked,
        "resident_pages": int(last["resident"]),
        "resident_mb": last["resident_mb"],
        "page_budget_mb": last["page_budget_mb"],
        "fixed_mb": last["fixed_mb"],
        "pool_used_mb": last["pool_used_mb"],
        "pool_capacity_mb": last["pool_capacity_mb"],
        "geometry_target_mb": last["geometry_target_mb"],
        "request_overflow_in_trace": int(last["request_overflow"] - first["request_overflow"]),
        "evicted_before_trace": int(first["evicted"]),
        "levels_resident": resident_levels,
        "levels_pending": pending_levels,
        "finest_resident_level": next((i for i, n in enumerate(resident_levels) if n > 0), None),
        "max_page_use_ratio": max((s["resident_mb"] / s["page_budget_mb"]) if s["page_budget_mb"] > 0 else 0.0
                                  for s in samples),
    })
    return result, failures


def run(rc, capture_dir, verify):
    failures = []
    report = {"mode": "geom_settle", "verify": bool(verify), "capture": os.path.relpath(capture_dir, rc.REPO_ROOT),
              "criteria": {"evicted_in_trace": 0, "uploaded_in_trace": 0, "levels_resident": "unchanged",
                           "resident_mb": "<= page_budget_mb", "pool_used_mb": "<= geometry_target_mb",
                           "mb_rounding": MB_ROUNDING},
              "views": []}
    metrics_path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(metrics_path):
        print(f"REFERENCE_CHECK result=fail reason=metrics_missing（CaptureScene.ps1 の出力先を指定する） path={capture_dir}")
        return 1
    metrics = load_json(metrics_path)
    game_args = metrics.get("game_args") or []
    cap_text = game_arg_value(game_args, "vram-budget-mb")
    seconds_text = game_arg_value(game_args, "geom-settle-trace")
    if cap_text is None or seconds_text is None:
        failures.append("撮影の起動引数に vram-budget-mb と geom-settle-trace が無い")
    if metrics.get("configuration") == "Release":
        failures.append("Release の撮影（検証用の記録は開発ビルドだけが書く）")
    inputs_ok, input_failures, _ = rc.check_inputs(capture_dir)
    if verify:
        failures.extend(input_failures)
    report["inputs_current"] = inputs_ok
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    if not views:
        failures.append("撮影に視点が無い")
    if cap_text is not None and seconds_text is not None:
        cap_mb, seconds = int(cap_text), int(seconds_text)
        report["vram_budget_mb"] = cap_mb
        report["trace_seconds"] = seconds
        for view in views:
            result, view_failures = check_view(capture_dir, view, cap_mb, seconds)
            result["pass"] = not view_failures
            result["failures"] = view_failures
            report["views"].append(result)
            failures.extend(f"{view['view']}: {f}" for f in view_failures)
            levels = "/".join(str(n) for n in result.get("levels_resident") or []) or "-"
            pending_levels = "/".join(str(n) for n in result.get("levels_pending") or []) or "-"
            print(f"REFERENCE_CHECK geom_settle view={view['view']} pass={int(result['pass'])} "
                  f"samples={result.get('samples')} evicted={result.get('evicted_in_trace')} "
                  f"uploaded={result.get('uploaded_in_trace')} resident_mb={result.get('resident_mb', float('nan')):.2f} "
                  f"page_budget_mb={result.get('page_budget_mb', float('nan')):.2f} "
                  f"pool_used_mb={result.get('pool_used_mb', float('nan')):.2f} "
                  f"geometry_target_mb={result.get('geometry_target_mb', float('nan')):.2f} "
                  f"budget_short={int(bool(result.get('budget_short')))} pending={result.get('pending_pages')} "
                  f"finest_level={result.get('finest_resident_level')} levels_resident={levels} "
                  f"levels_pending={pending_levels}")
    report["failures"] = failures
    report_path = os.path.join(capture_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=geom_settle views={len(report['views'])} "
          f"inputs_current={int(inputs_ok)} report={report_path}")
    return 0 if not failures else 1

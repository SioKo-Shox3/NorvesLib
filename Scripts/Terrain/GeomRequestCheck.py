# ジオメトリのページの要求の列の溢れの検査（SCN-WL-GEOM-REQUEST-CAPACITY。ReferenceCheck.py --geom-requests から呼ぶ。
# 単独では動かさない）。
#
# 入力: CaptureScene.ps1 の出力先（metrics.json・<起動>.Game.log）。描画側は常駐の数が変わるたびに GEOMETRY_PAGES の行を書く
# （RenderResources.cpp。request_overflow は起動からの延べ件数、request_capacity は要求 1 枚の容量、request_peak は 1 枚へ
# 積もうとした要求の最大）。
#   落ちる: Game.log か GEOMETRY_PAGES の行が無い、ログの最後の行で request_overflow・wanted・failed のどれかが 0 でない、
#           視点を撮る直前の行が無い（撮影の印（CAPTURE_VIEWS view=<視点> saved）が無いか、ログが途中で切れた。
#           視点ごとに起動した撮影は印を書かないので、1 視点だけのログは最後の行を使う）か
#           その行で request_overflow が 0 でない、Release の撮影（行を書かない）
#           視点を撮る直前の行で wanted・failed が 0 でない、視点の落ち着くまでの時間が SETTLE_LIMIT_SECONDS を超える
#           （1 回の起動で撮る形だけ。前の視点の撮影（初めの視点はシーンの用意 SCENE_READY）から撮るまでの時間）、
#           Game.log が大きさの上限（LogTypes.h の maxLogFileSize = 10 MB）に届いた（後の行が書かれていない）
#   --verify: 撮影の容量が今の既定（GeometryPageRequestSet.h の DefaultCapacity）と違う、環境変数で容量を上書きした
#             （GEOMETRY_PAGE_REQUEST_CAPACITY source=env の行がある。既定の容量で溢れないことを確かめる検査なので）、
#             --wasteland-finest-lod の撮影（容量を広げる）を、古い・測り方の違う結果として失敗にする。
#             起動引数 geometry-request-capacity=<件数> で撮ったときは、その件数を既定の代わりに照合し、その件数の上書きの行を認める
#   記録する: 視点ごとの撮る直前の行の値と落ち着くまでの時間、ログの最後の行の値・大きさ、request_peak の最大と容量に対する割合
# 結果は <撮影>/geom_request_check.json に書く。
import datetime
import json
import os
import re

REPORT = "geom_request_check.json"
PAGES_PREFIX = "GEOMETRY_PAGES "
ENV_CAPACITY_MARK = "GEOMETRY_PAGE_REQUEST_CAPACITY "
CAPACITY_HEADER = os.path.join("Library", "Core", "Public", "Rendering", "MegaGeometry", "GeometryPageRequestSet.h")
CAPACITY_PATTERN = re.compile(r"constexpr\s+uint32_t\s+DefaultCapacity\s*=\s*(\d+)\s*;")
KEY_VALUE = re.compile(r"([a-z_]+)=(\S+)")
SAVED_PATTERN = re.compile(r"CAPTURE_VIEWS view=(\S+) saved ")
TIME_PATTERN = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")
READY_MARK = "SCENE_READY scene="
CAPACITY_ARG = "geometry-request-capacity="
# 視点の落ち着くまでの時間の上限（秒）
SETTLE_LIMIT_SECONDS = 120.0
# Game.log の大きさの上限（LogTypes.h の maxLogFileSize）。届くと後の行が書かれない
LOG_LIMIT_BYTES = 10 * 1024 * 1024
# 0 でなければ落とす、ログの最後の行の値
ZERO_KEYS = ("request_overflow", "wanted", "failed")


def load_json(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def parse_pages(line):
    fields = {}
    for key, value in KEY_VALUE.findall(line.split(PAGES_PREFIX, 1)[1]):
        try:
            fields[key] = float(value) if "." in value else int(value)
        except ValueError:
            fields[key] = value
    return fields


def line_time(line):
    match = TIME_PATTERN.match(line)
    return datetime.datetime.strptime(match.group(1), "%Y-%m-%d %H:%M:%S.%f") if match else None


def read_log(path):
    """ログから GEOMETRY_PAGES の行と、視点ごとの撮る直前の行・落ち着くまでの時間（秒）、容量の上書きの行を取り出す"""
    pages = []
    before_saved = {}
    settle_seconds = {}
    env_lines = []
    start = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if PAGES_PREFIX in line:
                pages.append(parse_pages(line))
            elif ENV_CAPACITY_MARK in line and "source=env" in line:
                env_lines.append(line.strip())
            elif READY_MARK in line and start is None and not before_saved:
                start = line_time(line)
            else:
                saved = SAVED_PATTERN.search(line)
                if saved and saved.group(1) not in before_saved:
                    before_saved[saved.group(1)] = pages[-1] if pages else None
                    now = line_time(line)
                    if start is not None and now is not None:
                        settle_seconds[saved.group(1)] = round((now - start).total_seconds(), 1)
                    start = now
    return pages, before_saved, settle_seconds, env_lines


def default_capacity(repo_root):
    with open(os.path.join(repo_root, CAPACITY_HEADER), encoding="utf-8-sig") as f:
        match = CAPACITY_PATTERN.search(f.read())
    return int(match.group(1)) if match else None


def run(rc, capture_dir, verify):
    failures = []
    report = {"mode": "geom_requests", "verify": bool(verify), "capture": os.path.relpath(capture_dir, rc.REPO_ROOT),
              "criteria": {k: 0 for k in ZERO_KEYS}, "logs": [], "views": []}
    metrics_path = os.path.join(capture_dir, "metrics.json")
    if not os.path.isfile(metrics_path):
        print(f"REFERENCE_CHECK result=fail reason=metrics_missing（CaptureScene.ps1 の出力先を指定する） path={capture_dir}")
        return 1
    metrics = load_json(metrics_path)
    report["scene"] = metrics.get("scene")
    if metrics.get("configuration") == "Release":
        failures.append("Release の撮影（GEOMETRY_PAGES の行は開発ビルドだけが書く）")
    game_args = metrics.get("game_args") or []
    capacity = default_capacity(rc.REPO_ROOT)
    report["default_capacity"] = capacity
    # 起動引数で容量を替えた撮影は、その件数を既定の代わりに照合する
    arg_capacity = None
    for a in game_args:
        text = str(a).lstrip("-")
        if text.startswith(CAPACITY_ARG) and text[len(CAPACITY_ARG):].isdigit():
            arg_capacity = int(text[len(CAPACITY_ARG):])
    report["arg_capacity"] = arg_capacity
    expected_capacity = arg_capacity if arg_capacity is not None else capacity
    if verify and any(str(a).lstrip("-") == "wasteland-finest-lod" for a in game_args):
        failures.append("--wasteland-finest-lod の撮影（要求の列の容量を広げるので、既定の容量の検査に使えない）")
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    if not views:
        failures.append("撮影に視点が無い")

    # 視点ごとに起動した撮影（視点が 1 つ・計測）は撮影の印を書かないので、そのログの最後の行を撮る直前の行とする
    views_per_log = {}
    for view in views:
        views_per_log[view.get("game_log")] = views_per_log.get(view.get("game_log"), 0) + 1
    parsed = {}
    for view in views:
        log_name = view.get("game_log")
        if not log_name:
            failures.append(f"{view.get('view')}: Game.log が無い（RelWithDebInfo か Debug で撮る）")
            continue
        if log_name not in parsed:
            log_path = os.path.join(capture_dir, log_name)
            if not os.path.isfile(log_path):
                failures.append(f"{log_name} が無い")
                parsed[log_name] = None
                continue
            pages, before_saved, settle_seconds, env_lines = read_log(log_path)
            parsed[log_name] = (pages, before_saved, settle_seconds)
            log_bytes = os.path.getsize(log_path)
            entry = {"game_log": log_name, "lines": len(pages), "env_capacity": env_lines, "log_bytes": log_bytes}
            if log_bytes >= LOG_LIMIT_BYTES:
                failures.append(f"{log_name}: ログが大きさの上限 {LOG_LIMIT_BYTES} バイトに届いた（{log_bytes} バイト。後の行が無い）")
            if not pages:
                failures.append(f"{log_name}: GEOMETRY_PAGES の行が無い（ページを持つメッシュを描いていない）")
            else:
                last = pages[-1]
                peak = max((p.get("request_peak", 0) for p in pages), default=0)
                entry.update({"last": last, "request_peak_max": peak,
                              "request_capacity": last.get("request_capacity")})
                if last.get("request_capacity"):
                    entry["request_peak_ratio"] = round(peak / last["request_capacity"], 4)
                for key in ZERO_KEYS:
                    if last.get(key, 0) != 0:
                        failures.append(f"{log_name}: 最後の GEOMETRY_PAGES の {key}={last.get(key)}")
                if verify and expected_capacity is not None and last.get("request_capacity") != expected_capacity:
                    failures.append(f"{log_name}: 撮影の容量 {last.get('request_capacity')} が"
                                    f"{'起動引数の' if arg_capacity is not None else '今の既定'} {expected_capacity} と違う（古い撮影）")
                print(f"REFERENCE_CHECK geom_requests log={log_name} lines={len(pages)} "
                      f"request_overflow={last.get('request_overflow')} wanted={last.get('wanted')} "
                      f"failed={last.get('failed')} evicted={last.get('evicted')} parent_wait={last.get('parent_wait')} "
                      f"request_capacity={last.get('request_capacity')} request_peak_max={peak}")
            unexpected_env = [l for l in env_lines
                              if arg_capacity is None or f"capacity={arg_capacity} " not in l + " "]
            if verify and unexpected_env:
                failures.append(f"{log_name}: 要求の列の容量を環境変数で上書きした撮影（{unexpected_env[0]}）")
            report["logs"].append(entry)
        if parsed.get(log_name) is None:
            continue
        pages, before_saved, settle_seconds = parsed[log_name]
        line = before_saved.get(view["view"])
        if line is None and not before_saved and views_per_log.get(log_name) == 1 and pages:
            line = pages[-1]
        settle = settle_seconds.get(view["view"])
        result = {"view": view["view"], "game_log": log_name, "before_saved": line, "settle_seconds": settle}
        if line is None:
            # ログは大きさの上限で途中から書かれなくなることがある（10 MB）。撮る直前の値が無い視点は判定できない
            failures.append(f"{view['view']}: 撮る直前の GEOMETRY_PAGES の行が無い（撮影の印が無いか、ログが途中で切れた）")
        else:
            for key in ZERO_KEYS:
                if line.get(key, 0) != 0:
                    failures.append(f"{view['view']}: 撮る直前の {key}={line[key]}")
        if settle is not None and settle > SETTLE_LIMIT_SECONDS:
            failures.append(f"{view['view']}: 落ち着くまで {settle} 秒（上限 {SETTLE_LIMIT_SECONDS:g} 秒）")
        report["views"].append(result)
        if line is not None:
            print(f"REFERENCE_CHECK geom_requests view={view['view']} request_overflow={line.get('request_overflow')} "
                  f"wanted={line.get('wanted')} failed={line.get('failed')} resident={line.get('resident')} "
                  f"evicted={line.get('evicted')} request_peak={line.get('request_peak')} settle_s={settle}")

    report["failures"] = failures
    report_path = os.path.join(capture_dir, REPORT)
    with open(report_path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    for failure in failures:
        print(f"REFERENCE_CHECK failure: {failure}")
    print(f"REFERENCE_CHECK result={'pass' if not failures else 'fail'} mode=geom_requests views={len(report['views'])} "
          f"default_capacity={capacity} report={report_path}")
    return 0 if not failures else 1

# 計測の項目の検証で、重い撮影をやり直さずに、反復の中で撮った結果（CaptureScene.ps1 の metrics.json）を確かめる。
# 結果が揃っていること・失敗が無いこと・計測の値が入っていること・溢れの数が 0 であること・
# 撮影がいまの Game.exe より後に撮られたこと（古い結果の流用でないこと）を見る。
#
# 使い方: python Scripts/CheckCaptureMetrics.py <撮影の出力ディレクトリ> [--views horizon,overview]
#         [--timing] [--configuration RelWithDebInfo]
# 終了コード: すべて満たせば 0、どれかが欠ければ 1。
import argparse
import json
import os
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory")
    parser.add_argument("--views", default="")
    parser.add_argument("--timing", action="store_true")
    parser.add_argument("--configuration", default="RelWithDebInfo")
    args = parser.parse_args()

    failures = []
    metrics_path = os.path.join(args.directory, "metrics.json")
    if not os.path.isfile(metrics_path):
        print("CHECK_CAPTURE_METRICS result=fail reason=missing path=%s" % metrics_path)
        return 1
    with open(metrics_path, encoding="utf-8-sig") as stream:
        metrics = json.load(stream)

    if metrics.get("failures"):
        failures.append("capture_failures=%d" % len(metrics["failures"]))
    if metrics.get("configuration") != args.configuration:
        failures.append("configuration=%s" % metrics.get("configuration"))

    # 撮影がいまの Game.exe より後であること（ビルドし直した後に撮り直していない結果を通さない）
    game_exe = os.path.join("build", "Game", args.configuration, "Game.exe")
    if os.path.isfile(game_exe) and os.path.getmtime(metrics_path) < os.path.getmtime(game_exe):
        failures.append("stale=metrics_older_than_game_exe")

    views = {view.get("view"): view for view in metrics.get("views", [])}
    wanted = [name for name in args.views.split(",") if name] or list(views)
    for name in wanted:
        view = views.get(name)
        if view is None:
            failures.append("view_missing=%s" % name)
            continue
        # 計測の撮影は組み立て完了の印を待たない作りなので、印は計測でない撮影でだけ見る
        if not args.timing and not view.get("scene_ready", False):
            failures.append("scene_not_ready=%s" % name)
        stats = view.get("mega_stats") or {}
        for key in ("draw_dropped_last", "draw_dropped_max"):
            if stats.get(key, 0):
                failures.append("%s=%s:%s" % (key, name, stats[key]))
        if args.timing:
            timing = view.get("gpu_timing") or {}
            for key in ("gpu_frame_ms_median", "gpu_frame_ms_p95", "game_thread_ms_median",
                        "game_thread_ms_p95", "render_thread_ms_median", "render_thread_ms_p95"):
                if not timing.get(key):
                    failures.append("timing_missing=%s:%s" % (name, key))

    if failures:
        print("CHECK_CAPTURE_METRICS result=fail " + " ".join(failures))
        return 1
    print("CHECK_CAPTURE_METRICS result=pass views=%s" % ",".join(wanted))
    return 0


if __name__ == "__main__":
    sys.exit(main())

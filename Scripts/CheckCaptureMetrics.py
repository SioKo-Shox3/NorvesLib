# 計測の項目の検証で、重い撮影をやり直さずに、反復の中で撮った結果（CaptureScene.ps1 の metrics.json）を確かめる。
# 結果が揃っていること・失敗が無いこと・計測の値が入っていること・溢れの数が 0 であること・
# 撮影がいまの Game.exe より後に撮られたこと（古い結果の流用でないこと）を見る。
#
# 使い方: python Scripts/CheckCaptureMetrics.py <撮影の出力ディレクトリ> [--views horizon,overview]
#         [--timing] [--configuration RelWithDebInfo] [--min-frames N]
# --timing では、シーンの組み立ての完了（scene_ready と scene_ready_frame）が記録されていることも見る。
# --min-frames N は、計測の有効な標本（gpu_timing.valid_samples。シーンの初期配置と公開の完了の後の描画フレームから
# ウォームアップと末尾を件数で除いた窓の件数）が N 件以上で、計測の開始のフレーム（timing_start_frame）が記録され、
# 窓が SceneReady のフレームより後で、ウォームアップを指定の件数だけ除いていることを見る。
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
    # 合否の閾値（節目の計測で使う）。指定したものだけを見る。単位は ms
    limits = (
        ("gpu_frame_ms_median", "max_gpu_median_ms"), ("gpu_frame_ms_p95", "max_gpu_p95_ms"),
        ("game_thread_ms_median", "max_gt_median_ms"), ("game_thread_ms_p95", "max_gt_p95_ms"),
        ("render_thread_ms_median", "max_rt_median_ms"), ("render_thread_ms_p95", "max_rt_p95_ms"),
    )
    for _, option in limits:
        parser.add_argument("--" + option.replace("_", "-"), type=float, default=None)
    parser.add_argument("--min-frames", type=int, default=0)
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
        if not view.get("scene_ready", False):
            failures.append("scene_not_ready=%s" % name)
        if args.timing and view.get("scene_ready_frame") is None:
            failures.append("scene_ready_frame_missing=%s" % name)
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
            if args.min_frames:
                # 有効な標本の数（SceneReady の後のウォームアップを除いた窓の、FrameGPU が有効なフレーム数）を見る。
                # 計測の開始のフレームが記録されていない撮影は、初期配置の前を含みうるので通さない。
                valid = timing.get("valid_samples")
                if valid is None:
                    failures.append("valid_samples_missing=%s" % name)
                elif valid < args.min_frames:
                    failures.append("valid_samples=%s:%s<%d" % (name, valid, args.min_frames))
                if view.get("timing_start_frame") is None:
                    failures.append("timing_start_frame_missing=%s" % name)
                ready = view.get("scene_ready_frame")
                first = timing.get("window_first_frame")
                if ready is not None and first is not None and first < ready:
                    failures.append("window_before_scene_ready=%s:%s<%s" % (name, first, ready))
                warmup = metrics.get("timing_warmup_frames", 0)
                removed = timing.get("warmup_frames_removed")
                if removed is None or removed < warmup:
                    failures.append("warmup_not_removed=%s:%s<%s" % (name, removed, warmup))
            for key, option in limits:
                limit = getattr(args, option)
                value = timing.get(key)
                if limit is not None and value is not None and value > limit:
                    failures.append("%s=%s:%.3f>%.3f" % (key, name, value, limit))

    if failures:
        print("CHECK_CAPTURE_METRICS result=fail " + " ".join(failures))
        return 1
    print("CHECK_CAPTURE_METRICS result=pass views=%s" % ",".join(wanted))
    return 0


if __name__ == "__main__":
    sys.exit(main())

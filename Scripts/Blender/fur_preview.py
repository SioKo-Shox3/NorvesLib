"""焼いた毛のマップから、UV 空間の流れと根元の印を PNG の線画にする。

使い方: python -B Scripts/Blender/fur_preview.py INPUT [--out PATH] [--spacing PX]
        [--scale N] [--background length|region|none]
INPUT は fur_maps.json またはそのディレクトリ。追加の依存は numpy。
赤い根元から黄色い線が伸びる向きが毛の流れで、画像の V は下向き。
終了コードは 0 が成功、1 が入力または保存の誤り、2 が引数の誤り。
"""

import argparse
import math
from pathlib import Path
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import common
from fur_maps_core import FurMapsError, read_png, write_png


REGION_COLORS = np.array([
    (52, 66, 88), (88, 48, 48), (42, 78, 52), (82, 66, 34),
    (70, 46, 88), (32, 76, 82), (88, 50, 72), (66, 70, 48),
], dtype=np.uint8)
LINE_COLOR = (255, 230, 72)
ROOT_COLOR = (255, 64, 88)


class ArgumentParser(argparse.ArgumentParser):
    """引数の誤りを日本語で通知する。"""

    def error(self, message):
        self.print_usage(sys.stderr)
        self.exit(2, "引数が正しくありません。入力、--out PATH、--spacing の整数（1 以上）、"
                  "--scale の整数（1〜8）、--background（length・region・none）を確認してください。\n")


def load_maps(manifest_path):
    """情報と 4 枚の PNG の形式・大きさを検査して読み込む。"""
    info = common.read_json(manifest_path)
    if not isinstance(info, dict) or info.get("format") != "norveslib.fur_maps.v1":
        raise FurMapsError("マップの情報の format は norveslib.fur_maps.v1 が必要です。")
    resolution = info.get("resolution")
    if isinstance(resolution, bool) or not isinstance(resolution, int) or resolution < 1:
        raise FurMapsError("resolution は 1 以上の整数が必要です。")
    if info.get("v_axis") != "down":
        raise FurMapsError("マップの情報の v_axis は down が必要です。")
    regions = info.get("regions")
    if not isinstance(regions, list) or not 1 <= len(regions) <= 8:
        raise FurMapsError("regions は 1〜8 個の領域の配列が必要です。")
    names, ids = set(), set()
    for region in regions:
        if not isinstance(region, dict):
            raise FurMapsError("regions の各領域はオブジェクトが必要です。")
        name, region_id = region.get("name"), region.get("id")
        if not isinstance(name, str) or not name or name in names:
            raise FurMapsError("領域の name は重複のない空でない文字列が必要です。")
        if (isinstance(region_id, bool) or not isinstance(region_id, int)
                or not 0 <= region_id <= 7 or region_id in ids):
            raise FurMapsError("領域の id は重複のない 0〜7 の整数が必要です。")
        names.add(name)
        ids.add(region_id)
    entries = info.get("maps")
    if not isinstance(entries, dict):
        raise FurMapsError("maps は 4 枚の PNG の情報を持つオブジェクトが必要です。")
    images, paths = {}, {manifest_path}
    for name, channels in (("length", 1), ("flow", 3), ("density_clump", 3), ("region", 1)):
        entry = entries.get(name)
        filename = entry.get("file") if isinstance(entry, dict) else None
        if not isinstance(filename, str) or not filename:
            raise FurMapsError(f"maps.{name}.file は PNG の相対パスが必要です。")
        relative_path = Path(filename)
        path = (manifest_path.parent / relative_path).resolve()
        if relative_path.is_absolute() or not path.is_relative_to(manifest_path.parent):
            raise FurMapsError(f"maps.{name}.file は情報ファイルのディレクトリ内の相対パスが必要です。")
        image = read_png(path)
        if image.shape != (resolution, resolution, channels):
            raise FurMapsError(
                f"{name} の大きさとチャンネル数は ({resolution}, {resolution}, {channels}) が必要です。"
                f"実際: {image.shape}")
        images[name] = image
        paths.add(path)
    if np.any(images["region"][:, :, 0] > 7):
        raise FurMapsError("領域 PNG の ID は 0〜7 の整数のバイトが必要です。")
    return images, paths


def draw_line(image, x0, y0, x1, y1):
    """整数の端点を Bresenham の線分で結び、画像の外には書かない。"""
    height, width = image.shape[:2]
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    error = dx + dy
    while True:
        if 0 <= x0 < width and 0 <= y0 < height:
            image[y0, x0] = LINE_COLOR
        if x0 == x1 and y0 == y1:
            break
        twice_error = 2 * error
        if twice_error >= dy:
            error += dy
            x0 += sx
        if twice_error <= dx:
            error += dx
            y0 += sy


def render_preview(images, spacing, scale, background):
    """最近傍の流れへ 1 texel ずつ進み、拡大した背景に線と根元を描く。"""
    length = images["length"][:, :, 0]
    flow = images["flow"][:, :, :2].astype(np.float64) * (2.0 / 255.0) - 1.0
    strength = np.linalg.norm(flow, axis=2)
    height, width = length.shape
    if background == "length":
        base = np.repeat((length // 2)[:, :, None], 3, axis=2)
    elif background == "region":
        base = REGION_COLORS[images["region"][:, :, 0]]
    else:
        base = np.zeros((height, width, 3), dtype=np.uint8)
    image = np.repeat(np.repeat(base, scale, axis=0), scale, axis=1)
    seeds = [(x, y) for y in range(spacing // 2, height, spacing)
             for x in range(spacing // 2, width, spacing)
             if length[y, x] > 0 or strength[y, x] > 0.5]
    max_distance = 0.8 * spacing
    for seed_x, seed_y in seeds:
        # texel の中心を連続座標にし、V 下向きの成分をそのまま使う。
        x, y = seed_x + 0.5, seed_y + 0.5
        distance = 0.0
        while distance < max_distance:
            ix, iy = math.floor(x), math.floor(y)
            magnitude = strength[iy, ix]
            if magnitude < 0.5:
                break
            step = min(1.0, max_distance - distance)
            vx, vy = flow[iy, ix] / magnitude
            next_x, next_y = x + step * vx, y + step * vy
            if not (0 <= next_x < width and 0 <= next_y < height):
                break
            if strength[math.floor(next_y), math.floor(next_x)] < 0.5:
                break
            draw_line(image, math.floor(x * scale), math.floor(y * scale),
                      math.floor(next_x * scale), math.floor(next_y * scale))
            x, y = next_x, next_y
            distance += step
    # 他の流線との交差でも根元の位置が分かるよう、印を最後に重ねる。
    for seed_x, seed_y in seeds:
        x, y = math.floor((seed_x + 0.5) * scale), math.floor((seed_y + 0.5) * scale)
        image[max(0, y - 1):min(height * scale, y + 2),
              max(0, x - 1):min(width * scale, x + 2)] = ROOT_COLOR
    return image, len(seeds)


def main(argv=None):
    parser = ArgumentParser(
        description="毛のマップから UV 空間の流れの線画を作る。",
        usage="%(prog)s INPUT [--out PATH] [--spacing PX] [--scale N] [--background length|region|none]",
        add_help=False)
    parser._positionals.title = "位置引数"
    parser._optionals.title = "オプション"
    parser.add_argument("input", metavar="INPUT", help="fur_maps.json またはそれを含むディレクトリ")
    parser.add_argument("--out", metavar="PATH", help="出力 PNG（既定: 入力と同じ場所の fur_flow_preview.png）")
    parser.add_argument("--spacing", metavar="PX", type=int, default=16, help="種の格子の間隔（texel、1 以上、既定: 16）")
    parser.add_argument("--scale", metavar="N", type=int, default=2, help="画像の拡大倍率（1〜8、既定: 2）")
    parser.add_argument("--background", choices=("length", "region", "none"), default="length",
                        help="背景（length: 長さ、region: 領域、none: 黒、既定: length）")
    parser.add_argument("-h", "--help", action="help", help="使い方を表示して終了する")
    args = parser.parse_args(argv)
    if args.spacing < 1 or not 1 <= args.scale <= 8:
        parser.error("spacing または scale が範囲外")
    try:
        manifest_path = Path(args.input).resolve()
        if manifest_path.is_dir():
            manifest_path = manifest_path / "fur_maps.json"
        output_path = Path(args.out).resolve() if args.out else manifest_path.parent / "fur_flow_preview.png"
        images, input_paths = load_maps(manifest_path)
        if output_path in input_paths:
            parser.error("出力先が入力と同じ")
        image, seed_count = render_preview(images, args.spacing, args.scale, args.background)
        write_png(output_path, image)
        common.log_line("FUR_PREVIEW", {"output": str(output_path), "seeds": seed_count})
    except (FurMapsError, common.ConfigError) as error:
        print(f"毛の線画を作れません: {error}", file=sys.stderr)
        return 1
    except (OSError, ValueError, OverflowError, RecursionError):
        print("入力の形式、ファイルのパス、読み書きの権限を確認してください。", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

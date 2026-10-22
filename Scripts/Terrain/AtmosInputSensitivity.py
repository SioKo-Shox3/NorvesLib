# 遠景の霞の色を決める空の大気の入力（地表の反射の RGB・Mie の密度の倍率・Mie の尺度高さ）の感度を、入力を差し替えて撮った
# 大気の書き出し（CaptureScene.ps1 -GameArgs atmosphere-dump。入力は開発ビルドの環境変数 NORVES_WASTELAND_ATMOS_INPUT）から並べる。
# 記録は Docs/RenderingValidation/WastelandAtmosphereReference.md の SCN-WL-ATMOS-INPUT。
#
# 使い方: python Scripts/Terrain/AtmosInputSensitivity.py --case <名前>=<撮影の出力先> [--case ...] [--view ref_ground_02] [--json <出力>]
#   初めの --case を基準（今の設定）にする。
# 読む値（書き出しの probe の地表への視線。目の 1.7 m 下の水平面、4 方位の平均）:
#   距離 3・10・30 km の空気遠近の透過率 T と散乱光 L（cd/m²）の色度（R ÷ G・B ÷ R）。
#   見かけの遠景の色: 同じ反射率の面を、書き出しの水平面の照度（太陽 ＋ 空）で照らした輝度 S = ρ/π × E として、
#   遠景 = T × S ＋ L、近景 = S（空気遠近を掛けない）。遠景 − 近景 の B ÷ R の増えが、写真の「遠いほど青い」の向き
#   （WastelandLook.md の 遠景の帯 − 地面 の B ÷ R。晴天の far_01 で 0.25・ground_02 で 0.46）と比べる量。
#   ρ は仮の赤い岩（ROCK_ALBEDO）と灰色 0.2 の 2 通り。面の反射率は書き出しに無いので仮の値で、色の向きの比べにだけ使う。
import argparse
import json
import os
import sys

import numpy as np

DISTANCES_M = (3000.0, 10000.0, 30000.0)
# 仮の赤い岩の線形のアルベド（赤褐色の砂岩。Scripts/Terrain/TerrainMaterials.py の砂岩の線形の平均 0.22 に近い明るさで、
# 赤が青の約 2.5 倍）。色の向きの比べにだけ使う
ROCK_ALBEDO = np.array([0.32, 0.20, 0.13])
GRAY_ALBEDO = np.array([0.2, 0.2, 0.2])
# far_01 の遠景の帯の深度の中央値[m]（WastelandLook.md。-Aov の撮影の深度）
FAR_01_BAND_M = 4500.0


def read_probe(capture_dir, view):
    """書き出しの JSON と probe の配列（行 × 視線 × 4）を読む"""
    meta_path = os.path.join(capture_dir, f"{view}.atmos.json")
    with open(meta_path, encoding="utf-8") as handle:
        meta = json.load(handle)
    probe = meta["probe"]
    shape = tuple(int(n) for n in probe["shape"])
    data = np.fromfile(os.path.join(capture_dir, probe["file"]), dtype="<f4").reshape(shape).astype(np.float64)
    rows = [name.split("|")[0] for name in probe["rows"]]
    return meta, data, rows, probe["rays"]


def chroma(rgb):
    """(R ÷ G, B ÷ R)"""
    return float(rgb[0] / rgb[1]), float(rgb[2] / rgb[0])


def summarize(capture_dir, view):
    meta, data, rows, rays = read_probe(capture_dir, view)
    transmittance_row = rows.index("aerial_transmittance_rgb")
    scattering_row = rows.index("aerial_scattering_rgb_cd_m2")
    illuminance = meta["ground_illuminance"]
    horizontal = np.array(illuminance["sun_horizontal_lux"]) + np.array(illuminance["sky_horizontal_lux"])
    sky = meta["sky"]
    result = {
        "inputs": {
            "ground_albedo": sky["ground_albedo"],
            "mie_density_scale": sky["mie_density_scale"],
            "mie_scale_height_m": sky["mie_scale_height_m"],
        },
        "horizontal_illuminance_lux": horizontal.tolist(),
        "distances": {},
    }
    for distance in DISTANCES_M:
        indices = [i for i, ray in enumerate(rays)
                   if ray.get("kind") == "ground" and abs(ray.get("nominal_distance_m", 0.0) - distance) < 1.0]
        if not indices:
            raise RuntimeError(f"{capture_dir}: 地表への視線 {distance:.0f} m が書き出しに無い")
        transmittance = data[transmittance_row, indices, :3].mean(axis=0)
        scattering = data[scattering_row, indices, :3].mean(axis=0)
        entry = {
            "transmittance_rgb": transmittance.tolist(),
            "scattering_rgb": scattering.tolist(),
            "transmittance_rg_br": chroma(transmittance),
            "scattering_rg_br": chroma(scattering),
            "scattering_y": float(0.2126 * scattering[0] + 0.7152 * scattering[1] + 0.0722 * scattering[2]),
        }
        for name, albedo in (("rock", ROCK_ALBEDO), ("gray", GRAY_ALBEDO)):
            surface = albedo / np.pi * horizontal
            far = transmittance * surface + scattering
            near_rg, near_br = chroma(surface)
            far_rg, far_br = chroma(far)
            entry[f"{name}_far_minus_near_rg"] = far_rg - near_rg
            entry[f"{name}_far_minus_near_br"] = far_br - near_br
        result["distances"][f"{distance / 1000:.0f}km"] = entry
    # 地平線の近くの空（仰角 2°・20° の視線の空の放射輝度 LUT。方位の平均）の色度。差 6（地平線の空が白い）と同じ入力で動くかを見る
    sky_row = rows.index("sky_radiance_lut_rgb_cd_m2")
    for elevation in (2.0, 20.0):
        indices = [i for i, ray in enumerate(rays)
                   if ray.get("kind") == "direction" and abs(ray.get("elevation_degrees", -99.0) - elevation) < 0.01]
        radiance = data[sky_row, indices, :3].mean(axis=0)
        result[f"sky_{elevation:.0f}deg_rg_br"] = chroma(radiance)
        result[f"sky_{elevation:.0f}deg_y"] = float(0.2126 * radiance[0] + 0.7152 * radiance[1] + 0.0722 * radiance[2])
    # far_01 の写真と撮影が同じ物を見ている遠景の帯（約 4 km のメサ。撮影の深度の中央値 4.5 km）の値を、3 km と 10 km の間で
    # 距離の対数について線形に補う（書き出しの視線は 3・10・30 km だけ）
    weight = np.log(FAR_01_BAND_M / DISTANCES_M[0]) / np.log(DISTANCES_M[1] / DISTANCES_M[0])
    near_entry, far_entry = result["distances"]["3km"], result["distances"]["10km"]
    for key in ("rock_far_minus_near_br", "rock_far_minus_near_rg", "gray_far_minus_near_br"):
        result[f"far_01_band_{key}"] = float((1.0 - weight) * near_entry[key] + weight * far_entry[key])
    return result


def main():
    parser = argparse.ArgumentParser(description="大気の入力ごとの遠景の帯の散乱光と透過率の色度を並べる")
    parser.add_argument("--case", action="append", required=True, help="<名前>=<撮影の出力先>。初めが基準")
    parser.add_argument("--view", default="ref_ground_02")
    parser.add_argument("--json", help="結果の JSON の書き出し先")
    args = parser.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")

    cases = []
    for text in args.case:
        name, _, directory = text.partition("=")
        cases.append((name, directory, summarize(directory, args.view)))

    base = cases[0][2]
    print("| 入力 | 距離 | T の R÷G / B÷R | L の R÷G / B÷R | L の Y (cd/m²) | 赤い岩の 遠景−近景 B÷R | 灰色の 遠景−近景 B÷R |")
    print("|---|---|---|---|---|---|---|")
    for name, _, summary in cases:
        for key, entry in summary["distances"].items():
            t_rg, t_br = entry["transmittance_rg_br"]
            l_rg, l_br = entry["scattering_rg_br"]
            print(f"| {name} | {key} | {t_rg:.3f} / {t_br:.3f} | {l_rg:.3f} / {l_br:.3f} | {entry['scattering_y']:.0f} | "
                  f"{entry['rock_far_minus_near_br']:+.3f} | {entry['gray_far_minus_near_br']:+.3f} |")
    print()
    print("基準からの変化（10 km の地表への視線）:")
    base_entry = base["distances"]["10km"]
    for name, _, summary in cases[1:]:
        entry = summary["distances"]["10km"]
        print(f"  {name}: L の B÷R {entry['scattering_rg_br'][1] / base_entry['scattering_rg_br'][1]:.3f} 倍・"
              f"L の Y {entry['scattering_y'] / base_entry['scattering_y']:.3f} 倍・"
              f"T の B÷R {entry['transmittance_rg_br'][1] / base_entry['transmittance_rg_br'][1]:.3f} 倍・"
              f"赤い岩の 遠景−近景 B÷R {base_entry['rock_far_minus_near_br']:+.3f} → {entry['rock_far_minus_near_br']:+.3f}")
    print()
    print("far_01 の帯（4.5 km に補った値）の 遠景−近景: 赤い岩の B÷R・R÷G / 灰色の B÷R"
          "（写真の帯 − 地面 B÷R +0.25・R÷G −0.40、今の撮影の HDR +0.17・−0.30）:")
    for name, _, summary in cases:
        print(f"  {name}: {summary['far_01_band_rock_far_minus_near_br']:+.3f}・"
              f"{summary['far_01_band_rock_far_minus_near_rg']:+.3f} / {summary['far_01_band_gray_far_minus_near_br']:+.3f}")
    print()
    print("空の放射輝度（方位の平均）の B÷R・Y (cd/m²): 仰角 2° / 仰角 20°")
    for name, _, summary in cases:
        print(f"  {name}: {summary['sky_2deg_rg_br'][1]:.3f}・{summary['sky_2deg_y']:.0f} / "
              f"{summary['sky_20deg_rg_br'][1]:.3f}・{summary['sky_20deg_y']:.0f}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as handle:
            json.dump({name: summary for name, _, summary in cases}, handle, ensure_ascii=False, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())

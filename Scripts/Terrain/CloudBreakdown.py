# 雲のエンジンと Cycles の差（ReferenceCheck.py --cloud の表）を分けるため、エンジンの雲の式の散乱光を項ごとに求め直す（SCN-WLR-CLOUD-REF）。
#
# 使い方:
#   python Scripts/Terrain/CloudBreakdown.py --engine <雲の書き出しの撮影> --reference <Cycles の書き先>
# <Cycles の書き先>/cloud_breakdown.json を書き、領域ごとの値を 1 行ずつ出す。
#
# 書き出しの式（CloudDumpCheck.CloudModel の march。エンジンの式の写し）の太陽の項だけを差し替えて、同じ視線・同じ歩で積み直す:
#   engine        エンジンの式のまま（書き出しの GPU の値との一致の確かめ）
#   ambient       太陽の項を 0（空と地面の照り返しの近似の項だけ）
#   sun_single    多重散乱の近似の 1 回目（octave 0）だけ。太陽への光学的深さはエンジンの 6 回の歩
#   exact_single  octave 0 だけで、太陽への光学的深さを細部つきの式で SUN_STEP_M ごとに雲の層の頂まで積む
#   exact_all     3 回の和のまま、太陽への光学的深さだけを exact_single と同じに積む
# 項: 太陽の 1 回目 = sun_single − ambient、多重散乱の近似の分 = engine − sun_single、
#     太陽への歩の差 = exact_single − sun_single（エンジンの 6 回の歩が影を薄く・濃くする分）。
# 値は霞を掛ける前の散乱光の Y（cd/m²。書き出しの行 1 と同じ）で、領域の視線の平均。Cycles の Y は cloud_check.json の視線の値。
#
# 仰角ごとの見え方（手本の写真 far_01 の空は仰角 3〜10°）: 書き出しのカメラから、仰角 ELEVATIONS_DEG ごとに方位を
# AZIMUTH_STEP_DEG 刻みで一周する視線をエンジンの式で積み、雲の当たる割合と、雲の画素の色を空と比べた値を書く。
#   画素の色は cloud_layer.frag の合成（雲の放射輝度 + 空 ×（1 − 不透明度 × 消え）。空は LUT の向きの値）で、霞と消えを
#   掛けない版（霞の前）と掛けた版（霞の後）を求める。空気遠近・露出・トーンマップの前の値なので、写真の値とは直に比べない。
#   R ÷ B ≥ CLOUD_RED_BLUE_RATIO（CloudPhotoStats.py の雲の印と同じ）の視線の割合を、写真の被覆と並べる目安にする。
# 終了コード: 書けたら 0、書き出し・報告が無いときは 1。
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import CloudDumpCheck as CD  # noqa: E402
import CloudVolume as CV  # noqa: E402

LUMA = np.array([0.2126, 0.7152, 0.0722])
SUN_STEP_M = 8.0
VARIANTS = ("engine", "ambient", "sun_single", "exact_single", "exact_all")
ELEVATIONS_DEG = (3.0, 5.0, 7.0, 10.0, 15.0, 30.0, 45.0, 60.0)
AZIMUTH_STEP_DEG = 5.0
CLOUD_RED_BLUE_RATIO = 0.7


class VariantModel(CD.CloudModel):
    """太陽の項の求め方を差し替えた CloudModel"""

    def __init__(self, base, variant):
        self.__dict__.update(base.__dict__)
        self.variant = variant

    def exact_light_depth(self, noise_positions):
        """太陽へ向かう光学的深さを、細部つきの式で SUN_STEP_M ごとに雲の層の頂まで積む（エンジンの歩と同じく高度は y で進める）"""
        thickness = self.top - self.bottom
        count = noise_positions.shape[0]
        depth = np.zeros(count)
        steps = int(np.ceil(thickness / max(self.sun_direction[1], 0.05) / SUN_STEP_M)) + 1
        for index in range(steps):
            positions = noise_positions + self.sun_direction[None, :] * (index + 0.5) * SUN_STEP_M
            heights = (positions[:, 1] - self.bottom) / thickness
            use = (heights >= 0.0) & (heights <= 1.0)
            if np.any(use):
                depth[use] += self.density(positions[use], heights[use], np.ones(int(use.sum()), dtype=bool)) * SUN_STEP_M
        return depth * self.extinction

    def sun_term_parts(self, noise_positions, direction, jitter=0.5):
        # march は単一散乱と多重散乱の項を分けて引く。エンジン以外の版は差し替えた太陽の項をそのまま単一散乱の側に置く
        if self.variant == "engine":
            return super().sun_term_parts(noise_positions, direction, jitter)
        zeros = np.zeros(noise_positions.shape[0])
        return self.sun_term(noise_positions, direction, jitter), zeros, zeros

    def sun_term(self, noise_positions, direction, jitter=0.5):
        if self.variant == "engine":
            return super().sun_term(noise_positions, direction, jitter)
        if self.variant == "ambient":
            return np.zeros(noise_positions.shape[0])
        phases = self.phases(float(np.dot(direction, self.sun_direction)))
        if self.variant == "sun_single":
            # エンジンの 6 回の歩の光学的深さで、3 回の和の octave 0 の項だけ
            return np.exp(-self.engine_light_depth(noise_positions)) * phases[0]
        depth = self.exact_light_depth(noise_positions)
        if self.variant == "exact_single":
            return np.exp(-depth) * phases[0]
        term = np.zeros(noise_positions.shape[0])
        for octave in range(3):
            term += 0.5 ** octave * np.exp(-depth * 0.5 ** octave) * phases[octave]
        return term

    def engine_light_depth(self, noise_positions):
        """エンジンの太陽への 6 回の歩の光学的深さ（CloudModel.sun_term と同じ歩）"""
        thickness = self.top - self.bottom
        count = noise_positions.shape[0]
        light_depth = np.zeros(count)
        alive = np.ones(count, dtype=bool)
        light_distance = 0.0
        light_step = CD.LIGHT_FIRST_STEP
        for light_index in range(CD.LIGHT_STEPS):
            positions = noise_positions + self.sun_direction[None, :] * (light_distance + light_step * 0.5)
            heights = (positions[:, 1] - self.bottom) / thickness
            alive &= ~(heights > 1.0)
            use = alive & (heights >= 0.0)
            if np.any(use):
                light_depth[use] += self.density(positions[use], heights[use], light_index < 3) * light_step
            light_distance += light_step
            light_step *= CD.LIGHT_STEP_GROWTH
        return light_depth * self.extinction


def elevation_sweep(model):
    """仰角ごとに方位を一周する視線の、雲の当たる割合と霞の前後の画素の色"""
    rows = []
    for elevation in ELEVATIONS_DEG:
        e = np.radians(elevation)
        samples = []
        for azimuth in np.arange(0.0, 360.0, AZIMUTH_STEP_DEG):
            a = np.radians(azimuth)
            direction = np.array([np.cos(e) * np.sin(a), np.sin(e), np.cos(e) * np.cos(a)])
            march = model.march(direction, 0.5)
            sky = CD.sample_sky(model.sky_lut, direction)
            if not march["marched"]:
                samples.append({"opacity": 0.0, "before": sky, "after": sky, "sky": sky})
                continue
            opacity = 1.0 - march["transmittance"]
            before = march["scattered"] + sky * (1.0 - opacity)
            after = march["radiance"] + sky * (1.0 - opacity * march["fade"])
            samples.append({"opacity": opacity, "before": before, "after": after, "sky": sky,
                            "start": march["start_distance"], "distance": march["cloud_distance"],
                            "haze": float(march["haze"]), "fade": march["fade"]})
        cloudy = [s for s in samples if s["opacity"] >= CD.CLOUD_OPACITY]

        def red_blue(rgb):
            return float(rgb[0] / max(rgb[2], 1e-6))

        def mean_of(key, items):
            return float(np.mean([s[key] for s in items])) if items else None

        rows.append({
            "elevation_deg": elevation, "rays": len(samples),
            "cloud_fraction": len(cloudy) / len(samples),
            "thick_fraction": sum(s["opacity"] >= CD.THICK_OPACITY for s in samples) / len(samples),
            "cloud_distance_m": mean_of("distance", cloudy), "haze": mean_of("haze", cloudy), "fade": mean_of("fade", cloudy),
            "white_fraction_before": sum(red_blue(s["before"]) >= CLOUD_RED_BLUE_RATIO for s in samples) / len(samples),
            "white_fraction_after": sum(red_blue(s["after"]) >= CLOUD_RED_BLUE_RATIO for s in samples) / len(samples),
            "cloud_over_sky_before": float(np.mean([s["before"] @ LUMA / (s["sky"] @ LUMA) for s in cloudy])) if cloudy else None,
            "cloud_over_sky_after": float(np.mean([s["after"] @ LUMA / (s["sky"] @ LUMA) for s in cloudy])) if cloudy else None,
            "sky_red_blue": float(np.median([red_blue(s["sky"]) for s in samples])),
        })
    return rows


def main():
    parser = argparse.ArgumentParser(description="雲のエンジンの式の散乱光を項ごとに求め直し、Cycles の値と並べる")
    parser.add_argument("--engine", required=True, help="雲の書き出しの撮影（CaptureScene.ps1 -GameArgs cloud-dump の OutDir）")
    parser.add_argument("--reference", required=True, help="Cycles の書き先（ReferenceCheck.py --cloud の cloud_check.json がある）")
    args = parser.parse_args()

    report_path = os.path.join(args.reference, "cloud_check.json")
    if not os.path.isfile(report_path):
        print(f"CLOUD_BREAKDOWN error 報告が無い {report_path}")
        return 1
    with open(report_path, encoding="utf-8") as file:
        report = json.load(file)
    result = {"engine_dir": os.path.abspath(args.engine), "reference_dir": os.path.abspath(args.reference),
              "sun_step_m": SUN_STEP_M, "views": []}
    for view_report in report["views"]:
        view = view_report["view"]
        try:
            meta, model, probe = CV.load_dump(args.engine, view)
        except ValueError as error:
            print(f"CLOUD_BREAKDOWN error view={view} 雲の書き出しを読めない（{error}）")
            return 1
        rays = view_report["rays"]
        members = view_report["members"]
        used = sorted({index for indices in members.values() for index in indices})
        target = CV.target_size(meta)
        models = {name: VariantModel(model, name) for name in VARIANTS}
        values = {}
        for index in used:
            pixel = rays[index]["pixel"]
            direction = model.ray_direction(pixel, target)
            jitter = model.jitter(pixel)
            values[index] = {}
            for name in VARIANTS:
                march = models[name].march(direction, jitter)
                values[index][name] = float(march["scattered"] @ LUMA) if march["marched"] else 0.0
            values[index]["probe"] = float(np.asarray(rays[index]["engine_scattered_rgb"]) @ LUMA)
            values[index]["cycles"] = float(rays[index]["cycles_y"])
        regions = {}
        for name, indices in members.items():
            if not indices:
                continue
            mean = {key: float(np.mean([values[i][key] for i in indices]))
                    for key in VARIANTS + ("probe", "cycles")}
            terms = {
                "ambient": mean["ambient"],
                "sun_single": mean["sun_single"] - mean["ambient"],
                "multiple_scattering_approx": mean["engine"] - mean["sun_single"],
                "light_march_difference": mean["exact_single"] - mean["sun_single"],
            }
            regions[name] = {"rays": len(indices), "mean_y": mean, "terms": terms}
            print(f"CLOUD_BREAKDOWN view={view} region={name} rays={len(indices)} probe={mean['probe']:.0f} "
                  f"engine={mean['engine']:.0f} ambient={terms['ambient']:.0f} sun_single={terms['sun_single']:.0f} "
                  f"ms_approx={terms['multiple_scattering_approx']:.0f} exact_single={mean['exact_single']:.0f} "
                  f"exact_all={mean['exact_all']:.0f} cycles={mean['cycles']:.0f}")
        sweep = elevation_sweep(model)
        for row in sweep:
            print(f"CLOUD_BREAKDOWN view={view} elevation={row['elevation_deg']:.0f} cloud={row['cloud_fraction']:.2f} "
                  f"thick={row['thick_fraction']:.2f} distance_m={row['cloud_distance_m'] or 0:.0f} haze={row['haze'] or 0:.2f} "
                  f"fade={row['fade'] or 0:.2f} white_before={row['white_fraction_before']:.2f} "
                  f"white_after={row['white_fraction_after']:.2f} cloud_over_sky_before={row['cloud_over_sky_before'] or 0:.2f} "
                  f"cloud_over_sky_after={row['cloud_over_sky_after'] or 0:.2f} sky_rb={row['sky_red_blue']:.2f}")
        result["views"].append({"view": view, "regions": regions, "elevation_sweep": sweep,
                                "rays": {str(index): value for index, value in values.items()}})
    out_path = os.path.join(args.reference, "cloud_breakdown.json")
    with open(out_path, "w", encoding="utf-8") as file:
        json.dump(result, file, ensure_ascii=False, indent=2)
    print(f"CLOUD_BREAKDOWN result=written report={out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

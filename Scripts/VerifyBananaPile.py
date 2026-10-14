# -*- coding: utf-8 -*-
"""バナナの山が下の形に支えられているかを、頂点から数えて確かめる検算。

mega-bananas の開発ビルドが出す `MEGA_BANANAS_PILE_ITEM`（置いた順の位置・ヨー・大きさ）を Game.log から読み、
生成した GLB の全頂点を描画側と同じ規約（行ベクトル規約: 回転は共役として点に作用する）でワールドへ変換する。
山の各バナナについて、自分より前に置かれたバナナ（と床）の頂点の最大の高さと、自分の頂点の高さの差を
水平 5 mm の格子で比べ、最小の差（接触の隙間）を数える。

  隙間 > +許容: 下の形から浮いている
  隙間 < -許容: 下の形へ食い込んでいる（床は kSinkMeters = 2 mm 沈めるので -2 mm が正常）

--decimate N を付けると、隙間を確かめるのは各山の N 本に 1 本（置いた順の番号が N の倍数）だけにする。
下の形の高さは間引かず、全部のバナナの全頂点で積む（数万本の塚を数分で確かめる）。

使い方: python Scripts/VerifyBananaPile.py <Game.log> [--glb Assets/Models/Generated/banana/banana.glb] [--decimate N]
"""

import argparse
import json
import math
import re
import struct
import sys

import numpy as np

CELL = 0.005
TOLERANCE = 0.005
ITEM_PATTERN = re.compile(
    r"MEGA_BANANAS_PILE_ITEM pile=(\d+) index=(\d+) x=(-?[\d.]+) y=(-?[\d.]+) z=(-?[\d.]+) yaw=(-?[\d.]+) scale=(-?[\d.]+)"
)


def load_glb_positions(path):
    """GLB の最初のメッシュの POSITION を (N, 3) で返す。"""
    with open(path, "rb") as stream:
        data = stream.read()
    magic, _version, _length = struct.unpack_from("<4sII", data, 0)
    if magic != b"glTF":
        raise ValueError("GLB ではありません: " + path)
    offset = 12
    document = None
    binary = None
    while offset < len(data):
        chunk_length, chunk_type = struct.unpack_from("<II", data, offset)
        body = data[offset + 8 : offset + 8 + chunk_length]
        if chunk_type == 0x4E4F534A:
            document = json.loads(body.decode("utf-8"))
        elif chunk_type == 0x004E4942:
            binary = body
        offset += 8 + chunk_length
    primitive = document["meshes"][0]["primitives"][0]
    accessor = document["accessors"][primitive["attributes"]["POSITION"]]
    view = document["bufferViews"][accessor["bufferView"]]
    start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    stride = view.get("byteStride", 12)
    count = accessor["count"]
    if stride == 12:
        return np.frombuffer(binary, dtype="<f4", count=count * 3, offset=start).reshape(count, 3).astype(np.float64)
    out = np.empty((count, 3), dtype=np.float64)
    for index in range(count):
        out[index] = struct.unpack_from("<3f", binary, start + index * stride)
    return out


def quaternion_multiply(a, b):
    """Core/Public/Math/Quaternion.h の operator* と同じ積。(x, y, z, w)"""
    return np.array(
        [
            a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1],
            a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3],
            a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2],
        ]
    )


def axis_angle(axis, angle):
    half = angle * 0.5
    return np.array([axis[0] * math.sin(half), axis[1] * math.sin(half), axis[2] * math.sin(half), math.cos(half)])


def rotate_points(quaternion, points):
    """点を q * v * conj(q) で回す（Quaternion::operator*(Vector3) と同じ）。"""
    x, y, z, w = quaternion
    # 回転行列（標準形）
    matrix = np.array(
        [
            [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
        ]
    )
    return points @ matrix.T


def world_vertices(local, x, y, z, yaw, scale):
    roll = axis_angle((0.0, 0.0, 1.0), 0.5 * math.pi)
    turn = axis_angle((0.0, 1.0, 0.0), yaw)
    rotation = quaternion_multiply(roll, turn)
    # 描画側の行ベクトル規約では、回転は共役（逆回転）として点に作用する
    inverse = np.array([-rotation[0], -rotation[1], -rotation[2], rotation[3]])
    return rotate_points(inverse, local * scale) + np.array([x, y, z])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    parser.add_argument("--glb", default="Assets/Models/Generated/banana/banana.glb")
    parser.add_argument("--decimate", type=int, default=1, help="隙間を確かめるのを N 本に 1 本にする（下の形は全部で積む）")
    args = parser.parse_args()
    decimate = max(1, args.decimate)

    local = load_glb_positions(args.glb)
    piles = {}
    with open(args.log, "r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = ITEM_PATTERN.search(line)
            if match:
                pile = int(match.group(1))
                piles.setdefault(pile, []).append(tuple(float(v) for v in match.groups()[2:]))
    if not piles:
        print("MEGA_BANANAS_PILE_ITEM が見つかりません（開発ビルドの Game.log が必要）")
        return 2

    # 頂点が原点から届く水平の最大距離（山の格子の範囲を決める）
    reach = float(np.sqrt((local ** 2).sum(axis=1)).max()) * 1.2 + 2 * CELL

    total = {"supported": 0, "floating": 0, "penetrating": 0, "bananas": 0, "placed": 0}
    worst_penetration = 0.0
    worst_floating = 0.0
    floating_over_10mm = 0
    on_floor_only = 0
    stacked = 0
    for pile in sorted(piles):
        items = np.array(piles[pile])
        # 山の範囲の高さの格子（床は 0）。升は世界の原点にそろえる
        min_x = int(math.floor((items[:, 0].min() - reach) / CELL))
        min_z = int(math.floor((items[:, 2].min() - reach) / CELL))
        size_x = int(math.ceil((items[:, 0].max() + reach) / CELL)) - min_x + 1
        size_z = int(math.ceil((items[:, 2].max() + reach) / CELL)) - min_z + 1
        envelope = np.zeros(size_x * size_z, dtype=np.float64)
        pile_stats = {"supported": 0, "floating": 0, "penetrating": 0}
        gaps = []
        for index, (x, y, z, yaw, scale) in enumerate(piles[pile]):
            vertices = world_vertices(local, x, y, z, yaw, scale)
            cell_x = np.floor(vertices[:, 0] / CELL).astype(np.int64) - min_x
            cell_z = np.floor(vertices[:, 2] / CELL).astype(np.int64) - min_z
            keys = cell_x * size_z + cell_z
            if index % decimate == 0:
                below = envelope[keys]
                # 下の形（と床）に対する各頂点の高さの差。最小値が接触の隙間
                difference = vertices[:, 1] - np.maximum(below, 0.0)
                gap = float(difference.min())
                support = float(below[np.argmin(difference)])
                gaps.append(gap)
                if support > 0.01:
                    stacked += 1
                else:
                    on_floor_only += 1
                if gap > TOLERANCE:
                    pile_stats["floating"] += 1
                    if gap > 2 * TOLERANCE:
                        floating_over_10mm += 1
                    worst_floating = max(worst_floating, gap)
                elif gap < -TOLERANCE - 0.002:
                    pile_stats["penetrating"] += 1
                    worst_penetration = min(worst_penetration, gap)
                else:
                    pile_stats["supported"] += 1
            np.maximum.at(envelope, keys, vertices[:, 1])
        gaps = np.array(gaps)
        print(
            "pile=%d bananas=%d checked=%d supported=%d floating=%d penetrating=%d gap_min=%.4f gap_max=%.4f peak=%.4f"
            % (pile, len(items), len(gaps), pile_stats["supported"], pile_stats["floating"], pile_stats["penetrating"],
               gaps.min(), gaps.max(), envelope.max())
        )
        for name in ("supported", "floating", "penetrating"):
            total[name] += pile_stats[name]
        total["bananas"] += len(gaps)
        total["placed"] += len(items)
    print(
        "total bananas=%d placed=%d decimate=%d supported=%d floating=%d penetrating=%d (許容 ±%.0f mm) worst_floating=%.4f "
        "worst_penetration=%.4f floating_over_10mm=%d stacked_on_others=%d on_floor_only=%d"
        % (total["bananas"], total["placed"], decimate, total["supported"], total["floating"], total["penetrating"],
           TOLERANCE * 1000, worst_floating, worst_penetration, floating_over_10mm, stacked, on_floor_only)
    )
    return 0 if total["floating"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())

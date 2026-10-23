# Battleship Rock の尾根に積む厚い節理の塊（平らな頂と縦の側面を持つ砂岩の段）の素材を、Poly Haven の rock_09（薄い板状の岩の
# スキャン）から派生させる台本（SCN-WL-CLIFF-SCAN-NEAR）。板の厚みの方向（glTF の +Y）だけを THICKEN 倍に伸ばし、上面の平らな
# 板の面を頂に、板の縁を縦の側面にする。伸ばしただけでは板の上面の凹凸が尖った頂に、縁の丸みが縦に伸びた丸い肩になるので、
# 上の TOP_TRIM と横の SIDE_TRIM を超える頂点を水平・縦の面へ寄せ、平らな頂と縦の側面が鋭い角で交わる節理の塊にする。UV はそのままで、アルベド・法線・ORM は rock_09 の焼いたテクスチャを使う
# （Scripts/Terrain/CliffScanPlacements.json の素材の行）。伸ばした glTF は Assets/AssetSets/WastelandModels.json が焼く。
#
# 使い方: python Scripts/Terrain/MakeJointBlock.py
#   Assets/Models/PolyHaven/rock_09/rock_09_block_4k.gltf と rock_09_block.bin を書く（元の rock_09 は Scripts/FetchPolyHavenModels.ps1 が取得する）。
#   標準出力の最後の行: JOINT_BLOCK path=<glTF> thicken=<倍率> size=<x>,<y>,<z>
import json
import os
import sys

import numpy as np

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE_DIR = "Assets/Models/PolyHaven/rock_09"
SOURCE_GLTF = "rock_09_4k.gltf"
OUTPUT_GLTF = "rock_09_block_4k.gltf"
OUTPUT_BIN = "rock_09_block.bin"
# 厚みの方向の倍率。rock_09 の厚さ 0.033 m を 0.13 m にし、拡大率 120〜230 で段の高さ 16〜30 m（写真の段の 10〜30 m）にする
THICKEN = 4.0
# 頂を寄せる高さ（伸ばした後の高さに対する、上から切る割合）と、横の 4 面を寄せる幅（x・z の幅に対する、片側から切る割合）
TOP_TRIM = 0.15
SIDE_TRIM = 0.12
# 頂の下の張り出し（砂岩の帽岩と層の段）。(上からの深さの割合, その深さより下の側面を内へ寄せる幅の割合)。寄せ幅は上から足し合わせる。
# 下から見上げる視点（ref_cliff_01）では塊の頂の面が見えず、角が空との縁だけになって横の折れが出ないので、張り出しの下面で
# 角のすぐ下に横の折れを作る
LEDGES = ((0.10, 0.06), (0.45, 0.05))


def main():
    source_dir = os.path.join(REPO_ROOT, SOURCE_DIR)
    with open(os.path.join(source_dir, SOURCE_GLTF), encoding="utf-8") as f:
        gltf = json.load(f)
    with open(os.path.join(source_dir, gltf["buffers"][0]["uri"]), "rb") as f:
        blob = bytearray(f.read())
    primitive = gltf["meshes"][0]["primitives"][0]

    def view_array(accessor_index, width):
        item = gltf["accessors"][accessor_index]
        view = gltf["bufferViews"][item["bufferView"]]
        offset = view.get("byteOffset", 0) + item.get("byteOffset", 0)
        return np.frombuffer(blob, dtype=np.float32, count=item["count"] * width, offset=offset).reshape(-1, width), offset

    positions, position_offset = view_array(primitive["attributes"]["POSITION"], 3)
    normals, normal_offset = view_array(primitive["attributes"]["NORMAL"], 3)
    stretched = positions.astype(np.float64) * np.array([1.0, THICKEN, 1.0])
    lo, hi = stretched.min(axis=0), stretched.max(axis=0)
    span = hi - lo
    stretched[:, 1] = np.minimum(stretched[:, 1], hi[1] - TOP_TRIM * span[1])
    top = hi[1] - TOP_TRIM * span[1]
    trim = np.full(len(stretched), SIDE_TRIM)
    for depth, inset in LEDGES:
        trim += np.where(stretched[:, 1] < top - depth * (top - lo[1]), inset, 0.0)
    for axis in (0, 2):
        stretched[:, axis] = np.clip(stretched[:, axis], lo[axis] + trim * span[axis], hi[axis] - trim * span[axis])
    # 法線は寄せた後の三角形の面積の重みの和から求め直す
    indices = gltf["accessors"][primitive["indices"]]
    index_view = gltf["bufferViews"][indices["bufferView"]]
    index_dtype = {5125: np.uint32, 5123: np.uint16}[indices["componentType"]]
    triangles = np.frombuffer(blob, dtype=index_dtype, count=indices["count"],
                              offset=index_view.get("byteOffset", 0) + indices.get("byteOffset", 0)).astype(np.int64).reshape(-1, 3)
    corners = stretched[triangles]
    face = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0])
    turned = np.zeros_like(stretched)
    for k in range(3):
        np.add.at(turned, triangles[:, k], face)
    length = np.linalg.norm(turned, axis=1, keepdims=True)
    # 面積の和が 0 の頂点（寄せて潰れた三角形だけに使われる）は元の法線を伸ばしの逆転置で写したもの
    fallback = normals.astype(np.float64) * np.array([1.0, 1.0 / THICKEN, 1.0])
    turned = np.where(length > 1.0e-14, turned / np.maximum(length, 1.0e-30),
                      fallback / np.maximum(np.linalg.norm(fallback, axis=1, keepdims=True), 1.0e-12))
    blob[position_offset:position_offset + stretched.size * 4] = stretched.astype(np.float32).tobytes()
    blob[normal_offset:normal_offset + turned.size * 4] = turned.astype(np.float32).tobytes()

    accessor = gltf["accessors"][primitive["attributes"]["POSITION"]]
    accessor["min"] = [float(v) for v in stretched.min(axis=0).astype(np.float32)]
    accessor["max"] = [float(v) for v in stretched.max(axis=0).astype(np.float32)]
    gltf["buffers"][0]["uri"] = OUTPUT_BIN
    gltf["meshes"][0]["name"] = "rock_09_block"
    gltf["nodes"][0]["name"] = "rock_09_block_LOD0"
    with open(os.path.join(source_dir, OUTPUT_BIN), "wb") as f:
        f.write(bytes(blob))
    output_path = os.path.join(source_dir, OUTPUT_GLTF)
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(gltf, f, ensure_ascii=False, indent=2)
    size = stretched.max(axis=0) - stretched.min(axis=0)
    print(f"JOINT_BLOCK path={SOURCE_DIR}/{OUTPUT_GLTF} thicken={THICKEN} top_trim={TOP_TRIM} side_trim={SIDE_TRIM} ledges={len(LEDGES)} size={size[0]:.4f},{size[1]:.4f},{size[2]:.4f}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

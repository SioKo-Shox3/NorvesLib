# 荒野の近くの崖の面に貼るスキャンの塊（SCN-WL-CLIFF-SCAN-*）を Blender で地形の上に置き、配置表に書き出す台本。
# 配置表（Scripts/Terrain/CliffScanPlacements.json）は荒野のシーンが起動のたびに読み、塊ごとに仮想化ジオメトリの Entity を置く
# （Game/GameModes/Wasteland/WastelandCliffScan.h）。配置を変えても地形の焼き直しは要らない（配置表と素材のクックだけ）。
# 地形はエンジンが描くメッシュ（Game の --wasteland-export-terrain が書いた区画ごとの LOD 0 の PLY。BlenderReference.py と同じ）。
#
# 使い方（ふつうの Python から。Blender はこの台本が --blender の実行ファイルをヘッドレスで起動する）:
#   python Scripts/Terrain/BlenderCliffScan.py --place --mesh <地形の書き出し> [--save-blend <.blend>]
#       PLAN の塊を地形の崖の面に置いて配置表を書く。塊ごとに、狙いの点へ水平の光線を撃って崖の面の当たりと面の向き（当たりの
#       格子に当てた平面）を求め、殻の表の向き（面の法線の面積の重みの和）を面の法線へ、局所の横（glTF の +X。層の筋）を
#       水平に向けて、塊の範囲の中心を当たりに置く（殻を壁に貼る向き）。向きを面の法線から ±FIT_DEGREES 振った組ごとに、
#       開いた縁（殻の縁の頂点）の地形の面からの距離の RIM_GAP_PERCENTILE 百分位が RIM_GAP_MAX_M に収まる最も外への押し出しを
#       二分で求め、地形の面の外に出る面の割合（見える割合）が最も大きい組を選ぶ（浮かせず、埋もれ過ぎない）。
#       --save-blend を与えると、置いた塊（配置の印の付いた空の物体と、その子の glTF）と周りの地形を .blend に保存する。
#   python Scripts/Terrain/BlenderCliffScan.py --export --blend <.blend>
#       人が Blender で動かした .blend の塊（カスタムの属性 cliff_scan_asset を持つ空の物体）から、配置表を書き直す。
#   python Scripts/Terrain/BlenderCliffScan.py --render --mesh <地形の書き出し> --capture <エンジンの撮影（-Aov）> --out <書き先>
#          [--views cliff,ref_cliff_01]
#       配置表どおりに塊を置き、エンジンの撮影（<視点>.aov.json）と同じカメラで Cycles に描いて、<書き先>/<視点>.blender.png
#       （見るための画）と <視点>.blender_label.png（塊の画素に塊の番号 1〜、ほかは 0）・<視点>.blender_depth.pfm（カメラの
#       前方向への距離 m、空は +∞）・<視点>.blender.json を書く。
#   python Scripts/Terrain/BlenderCliffScan.py --compare --capture <塊ありの -Aov の撮影> --without <塊なしの -Aov の撮影>
#          --blender-dir <--render の書き先> --engine <見比べるエンジンの撮影の PNG のフォルダ> --out <書き先> [--views ...]
#       エンジンの塊の画素（塊ありと塊なしの深度の差。塊なしは配置表を退けて撮る）と Blender の塊の画素を重ね、塊ごとの
#       IoU と重心のずれ[画素]を <書き先>/cliff_scan_compare.json に書き、Blender｜エンジンの画と、塊の周りの拡大
#       （Blender・エンジン・エンジンに両方の輪郭。赤が Blender、水色がエンジン）を <書き先>/compare_<視点>.png に書く。
#       両方に塊が映った画素の深度の差の中央値（と、周りの地形の画素の差）も書く。--verify を付けると、全視点・全塊の
#       IoU が COMPARE_IOU_MIN 以上・重心のずれが COMPARE_CENTROID_MAX_PX 以下・深度の差が周りの地形の差 ＋ COMPARE_BLOCK_DEPTH_EXCESS_M 以下で
#       なければ終了コード 1。
#   どのモードも --blender <blender.exe> で Blender の実行ファイルを替えられる（既定は BlenderReference.DEFAULT_BLENDER）。
#
# 座標の変換は BlenderReference.py の「座標の変換」の節（engine_to_blender）を使う。塊の回転は、Blender の空の物体の回転 R_b
# （Blender の座標）を、エンジンの座標の回転 R_e = C⁻¹ R_b C（C は engine_to_blender の行列）へ移し、その四元数の共役を
# 配置表の rotation_xyzw に書く（Entity の行ベクトルのワールド行列は、ローカルの向きを四元数の標準の回転の逆で写す）。
import argparse
import hashlib
import json
import math
import os
import subprocess
import sys
import time

try:
    import bpy  # noqa: F401  Blender の中で動くときだけある
    IN_BLENDER = True
except ImportError:
    IN_BLENDER = False

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import BlenderReference as BR  # noqa: E402

REPO_ROOT = BR.REPO_ROOT
TABLE_PATH = "Scripts/Terrain/CliffScanPlacements.json"

# 素材（Poly Haven の CC0 のスキャン。選んだ理由は Docs/RenderingValidation/WastelandReference.md の
# 「近くの崖のスキャンの素材」）。アルベドと ORM は崖の砂岩へ寄せた較正の画像（Scripts/CalibrateScanRockAlbedo.py）を
# Assets/AssetSets/WastelandModels.json が焼いたもの
ASSETS = {
    asset_id: {
        "id": asset_id,
        "mesh": f"Assets/Models/PolyHaven/{asset_id}/{asset_id}_4k.gltf",
        "albedo": f"Assets/Models/PolyHaven/{asset_id}/cooked/{asset_id}_albedo",
        "normal": f"Assets/Models/PolyHaven/{asset_id}/cooked/{asset_id}_normal",
        "orm": f"Assets/Models/PolyHaven/{asset_id}/cooked/{asset_id}_orm",
    }
    for asset_id in ("rock_face_01", "rock_face_02", "boulder_01", "rock_09")
}
# 尾根の段の厚い節理の塊（rock_09 を厚みの方向へ伸ばした派生。Scripts/Terrain/MakeJointBlock.py が書く）。テクスチャは rock_09 のもの
ASSETS["rock_09_block"] = dict(ASSETS["rock_09"], id="rock_09_block", mesh="Assets/Models/PolyHaven/rock_09/rock_09_block_4k.gltf")

# 置く塊の案（エンジンの座標）。target は狙いの点（崖の手前の x・塊の中心を置く高さ y・崖に沿う z）、toward は撃つ水平の向き、
# scale は一様な拡大率、turn_degrees は向きを探す中心を面の法線から鉛直の軸まわりに振る角（繰り返しを崩す）。
# 置き場は Battleship Rock の南西の角に近い上の壁（x −1615〜−1585・z −865〜−815・高さ 137〜189 m、面の法線は西南西）。
# cliff（南西 520 m）と ref_cliff_01（西北西 1.1 km）の両方から見える崖の面を、両方の撮影の深度（-Aov）に崖の頂点を
# 投影して選んだ（その下の西の壁の中ほどは、ref_cliff_01 からは手前の崖錐の高まりに隠れる）
PLAN = (
    {"name": "battleship_west_01", "asset": "rock_face_01", "target": (-1700.0, 145.0, -850.0), "toward": (1.0, 0.0, 0.0),
     "scale": 3.0, "turn_degrees": 0.0},
    {"name": "battleship_west_02", "asset": "rock_face_02", "target": (-1700.0, 170.0, -830.0), "toward": (1.0, 0.0, 0.0),
     "scale": 4.0, "turn_degrees": 0.0},
    {"name": "battleship_west_03", "asset": "rock_face_01", "target": (-1700.0, 145.0, -822.0), "toward": (1.0, 0.0, 0.0),
     "scale": 3.0, "turn_degrees": 0.0},
)
# 棚に沿って半分埋める閉じた岩塊の狙い（SCN-WL-CLIFF-SCAN-NEAR）。Battleship Rock の北西の壁（ref_cliff_01 の正面、550〜900 m）の
# 写真の注釈の棚（cliff_01 の 4 本）の線を、置く前の撮影（SCN-WL-SHAPE-CAUSE の lod-default、注釈のずらし 0, −7 画素）の深度で
# 世界へ戻し、11 m 以上離れた点を選んだもの。(棚の番号, 壁の点, カメラから壁の点への水平の向き (x, z))。殻（rock_face）は
# この壁では縁を 0.5 m に収める向きが無いか埋もれ過ぎる（壁の崖の細部の溝の起伏が殻の厚みより大きい）ので、閉じた岩塊を使う
SHELF_POINTS = (
    (0, (-1909.7, 141.6, -1274.3), (0.928, 0.373)),
    (0, (-1926.2, 139.7, -1278.4), (0.926, 0.376)),
    (0, (-1940.4, 137.7, -1283.0), (0.926, 0.378)),
    (0, (-1951.0, 136.3, -1286.0), (0.925, 0.379)),
    (0, (-1962.2, 134.8, -1288.2), (0.924, 0.382)),
    (0, (-1974.3, 133.7, -1287.9), (0.921, 0.389)),
    (0, (-1985.4, 132.7, -1285.1), (0.917, 0.399)),
    (0, (-1996.4, 131.7, -1283.7), (0.914, 0.406)),
    (0, (-2008.2, 130.6, -1282.4), (0.91, 0.415)),
    (0, (-2022.8, 128.6, -1286.5), (0.908, 0.418)),
    (0, (-2033.7, 127.7, -1284.6), (0.904, 0.427)),
    (0, (-2045.8, 126.5, -1283.7), (0.9, 0.436)),
    (0, (-2056.7, 125.5, -1281.1), (0.895, 0.447)),
    (0, (-2067.8, 124.5, -1279.3), (0.89, 0.457)),
    (0, (-2078.5, 123.2, -1282.2), (0.888, 0.46)),
    (0, (-2088.0, 122.7, -1276.4), (0.88, 0.475)),
    (0, (-2092.1, 123.2, -1266.2), (0.871, 0.492)),
    (0, (-2088.1, 124.7, -1255.2), (0.864, 0.503)),
    (0, (-2081.9, 126.1, -1245.8), (0.86, 0.51)),
    (0, (-2073.2, 127.8, -1238.9), (0.859, 0.512)),
    (0, (-2083.5, 126.3, -1242.8), (0.857, 0.515)),
    (0, (-2064.0, 129.5, -1229.8), (0.856, 0.516)),
    (1, (-1969.7, 115.5, -1302.4), (0.93, 0.368)),
    (1, (-1982.1, 114.2, -1304.8), (0.928, 0.371)),
    (1, (-1995.1, 113.0, -1304.6), (0.926, 0.379)),
    (1, (-2008.9, 111.6, -1306.2), (0.923, 0.384)),
    (1, (-2020.4, 110.5, -1306.4), (0.921, 0.39)),
    (1, (-2031.2, 109.7, -1303.4), (0.916, 0.401)),
    (1, (-2042.5, 108.6, -1303.4), (0.913, 0.408)),
    (1, (-2057.1, 107.2, -1304.9), (0.91, 0.415)),
    (1, (-2068.3, 105.9, -1308.9), (0.909, 0.416)),
    (1, (-2079.4, 104.9, -1309.1), (0.906, 0.423)),
    (1, (-2090.6, 103.9, -1308.6), (0.902, 0.432)),
    (1, (-2099.8, 103.3, -1302.6), (0.895, 0.447)),
    (1, (-2107.6, 103.0, -1294.5), (0.886, 0.464)),
    (1, (-2113.4, 102.9, -1284.9), (0.876, 0.482)),
    (1, (-2122.0, 102.5, -1277.5), (0.866, 0.499)),
    (1, (-2125.1, 102.8, -1266.5), (0.856, 0.517)),
    (1, (-2122.4, 103.6, -1255.8), (0.849, 0.529)),
    (1, (-2121.0, 104.3, -1244.4), (0.84, 0.542)),
    (1, (-2107.3, 106.3, -1230.2), (0.836, 0.548)),
    (1, (-2074.9, 110.5, -1207.6), (0.835, 0.55)),
    (2, (-2099.5, 82.5, -1333.6), (0.916, 0.4)),
    (2, (-2110.6, 81.9, -1331.4), (0.911, 0.411)),
    (2, (-2116.2, 81.9, -1321.6), (0.903, 0.431)),
    (2, (-2124.9, 81.6, -1314.8), (0.894, 0.447)),
    (2, (-2128.5, 81.7, -1304.1), (0.885, 0.466)),
    (2, (-2135.3, 81.6, -1295.3), (0.875, 0.485)),
    (2, (-2143.4, 81.3, -1287.9), (0.865, 0.502)),
    (2, (-2149.8, 81.3, -1278.8), (0.854, 0.52)),
    (2, (-2149.2, 81.7, -1266.9), (0.844, 0.536)),
    (2, (-2144.0, 82.3, -1257.0), (0.839, 0.545)),
    (2, (-2140.1, 83.0, -1246.1), (0.832, 0.555)),
    (2, (-2138.5, 83.4, -1235.2), (0.823, 0.568)),
    (2, (-2135.8, 84.0, -1224.3), (0.816, 0.579)),
    (2, (-2129.6, 84.7, -1213.5), (0.81, 0.586)),
    (2, (-2120.7, 85.5, -1206.2), (0.81, 0.587)),
    (2, (-2087.6, 88.5, -1179.7), (0.808, 0.589)),
    (2, (-2086.8, 88.9, -1168.8), (0.8, 0.6)),
    (3, (-1759.5, 131.0, -1249.6), (0.941, 0.338)),
    (3, (-1792.0, 127.9, -1260.5), (0.941, 0.339)),
    (3, (-1802.9, 128.1, -1257.9), (0.938, 0.346)),
    (3, (-1932.0, 114.7, -1304.6), (0.938, 0.347)),
    (3, (-1880.4, 120.4, -1284.8), (0.938, 0.348)),
    (3, (-1926.6, 115.5, -1301.3), (0.937, 0.349)),
    (3, (-1939.0, 114.8, -1303.5), (0.936, 0.352)),
    (3, (-1954.3, 113.4, -1308.6), (0.936, 0.353)),
    (3, (-1944.1, 114.7, -1303.3), (0.935, 0.354)),
    (3, (-1969.8, 112.2, -1311.9), (0.935, 0.356)),
    (3, (-1962.0, 114.3, -1303.9), (0.932, 0.362)),
    (3, (-1979.4, 112.9, -1308.1), (0.931, 0.366)),
)
# 置いた後の形の撮影（--shape、注釈のずらし 0, −5 画素）で棚の線の上に折れが 14 画素以上続かない区間の中ほどの壁の点
# （線の 4 画素下の深度から戻し、線の高さへ 0.7 m 上げた）。SHELF_POINTS の後ろに足して同じ置き方で埋める
SHELF_POINTS = SHELF_POINTS + (
    (0, (-1986.2, 132.1, -1285.0), (0.917, 0.399)),
    (0, (-2075.9, 122.8, -1286.0), (0.891, 0.453)),
    (0, (-2096.1, 122.9, -1260.9), (0.865, 0.501)),
    (0, (-2092.4, 124.5, -1247.7), (0.857, 0.515)),
    (1, (-2018.3, 109.8, -1317.7), (0.928, 0.374)),
    (1, (-2110.8, 102.3, -1295.8), (0.885, 0.465)),
    (1, (-2116.5, 102.4, -1283.6), (0.874, 0.486)),
    (2, (-2119.4, 81.5, -1320.7), (0.901, 0.434)),
    (2, (-2145.5, 81.1, -1285.0), (0.861, 0.508)),
    (2, (-2152.1, 81.0, -1276.6), (0.851, 0.525)),
    (3, (-1959.7, 111.2, -1315.3), (0.938, 0.347)),
    (3, (-1984.7, 110.6, -1315.5), (0.933, 0.359)),
)
# 岩塊の素材と拡大率と鉛直の軸まわりの振り[°]と壁に沿う軸まわりの傾き[°]の巡回（繰り返しを崩す。boulder_01 は 1.27 × 1.83 ×
# 高さ 1.0 m なので拡大率 ≈ 高さ[m]、rock_09 は 0.07 × 0.15 × 厚さ 0.03 m の板で、拡大率 100 で 7 × 15 × 3 m）
SHELF_VARIANTS = (
    ("boulder_01", 9.0, 0.0, 0.0),
    ("rock_09", 110.0, 8.0, -4.0),
    ("boulder_01", 7.5, -12.0, 3.0),
    ("rock_09", 90.0, -6.0, 5.0),
    ("boulder_01", 10.5, 15.0, -3.0),
    ("rock_09", 125.0, 3.0, 2.0),
    ("boulder_01", 8.0, -4.0, 6.0),
)
# 岩塊の体積の標本のうち壁の外に出す割合（写真の棚の 1〜3 m の張り出しと、影を落とす立体に見える深さ）
EMBED_VISIBLE = 0.45
# 二分の後の割合がこの範囲を外れる狙い（壁が平らでなく、押し出しで割合が決まらない所）は置かずに飛ばす
EMBED_VISIBLE_RANGE = (0.25, 0.7)
# 素材の高さ[m]（拡大率 1 の glTF の上下の幅）。棚の塊は上端を棚の線に揃える（中心を線から高さの半分下げる）。中心を線に置くと
# 上端と下端の折れが線から 20〜25 画素離れ、線の上には折れが来ない
ASSET_HEIGHT_M = {"boulder_01": 1.004, "rock_09": 0.0329, "rock_09_block": 0.1118}
PLAN = PLAN + tuple(
    {"name": f"battleship_shelf_{index:02d}", "kind": "embed", "asset": SHELF_VARIANTS[index % len(SHELF_VARIANTS)][0],
     "target": (wall[0] - toward[0] * 60.0,
                wall[1] - 0.5 * ASSET_HEIGHT_M[SHELF_VARIANTS[index % len(SHELF_VARIANTS)][0]] * SHELF_VARIANTS[index % len(SHELF_VARIANTS)][1],
                wall[2] - toward[1] * 60.0), "toward": (toward[0], 0.0, toward[1]),
     "scale": SHELF_VARIANTS[index % len(SHELF_VARIANTS)][1], "turn_degrees": SHELF_VARIANTS[index % len(SHELF_VARIANTS)][2],
     "tilt_degrees": SHELF_VARIANTS[index % len(SHELF_VARIANTS)][3], "visible": EMBED_VISIBLE}
    for index, (_, wall, toward) in enumerate(SHELF_POINTS)
)
# 外形を写真へ上げる尾根の上の岩塊（cap）。ref_cliff_01 の列ごとに、撮影の最も上の崖の画素（深度 1000 m 以内）が写真の注釈の
# 外形より 10 画素以上低い所の尾根の点と、写真の外形の高さを深度で m に直した上端の高さ Y。(尾根の点, 上端の Y, 鉛直の軸の振り[°])。
# 右の段（写真の x 0.6〜0.83）は写真が平らな段の壁で、撮影は滑らかな斜面のため 12〜30 m 低い。左の上の縁は 2.5〜4.4 m 低い
# 岩塊は長い軸（boulder_01 の 1.83 m）を鉛直に立てた柱にする（写真の段の壁は縦の節理で割れた平らな頂の柱で、寝かせた岩塊は
# 丸い塊の山に見え、幅が写真の外形の右へはみ出した）。右端の段（写真の x 0.8 より右）は柱の幅で外形の外へ出るので置かない
CAP_POINTS = (
    ((-2059.7, 120.2, -1212.6), 127.0, 0.0),
    ((-2076.5, 109.1, -1204.9), 125.0, 25.0),
    ((-2082.0, 102.4, -1195.3), 125.0, -15.0),
    ((-2083.0, 92.4, -1182.7), 122.0, 40.0),
    ((-2086.5, 89.8, -1174.3), 103.0, 10.0),
    ((-2092.5, 84.9, -1165.1), 101.0, -30.0),
    ((-2124.9, 72.1, -1179.7), 96.0, 20.0),
    ((-1845.5, 167.3, -1259.0), 171.7, 30.0),
    ((-1880.8, 167.4, -1266.0), 171.6, -20.0),
    ((-1914.0, 167.0, -1262.0), 170.0, 5.0),
    # 左の縁（写真の x 0.19〜0.27）: 手前の肩の縦の壁と、奥の壁の上の縁
    ((-1948.6, 99.7, -1329.0), 124.0, 10.0),
    ((-1684.5, 160.2, -1226.5), 166.0, -10.0),
    ((-1697.5, 156.0, -1225.1), 165.5, 35.0),
    ((-1778.8, 149.3, -1249.1), 176.8, 0.0),
    ((-1801.7, 167.4, -1254.8), 174.7, -25.0),
    ((-1827.8, 164.5, -1258.3), 172.5, 15.0),
)
# 立てた柱の高さ[m]（boulder_01 の拡大率 1 の長い軸）
CAP_UPRIGHT_HEIGHT_M = 1.83
# 岩塊の高さのうち尾根より下へ埋める割合（尾根の両側の斜面に足を入れて浮かせない）
CAP_BURIED = 0.45
# 尾根の岩塊を配置表へ書くか。立てた boulder_01 は頂が尖った単独の針に、逆さにすると丸い巨岩の山に見え、写真の平らな頂の段と
# 角にならない（外形の距離は基準に入るが角の一致は上がらない。WastelandCliffScan.md の「Battleship Rock の壁を覆う試し」）ので、
# 平らな頂の素材が揃うまで既定の景色には置かない
CAP_ENABLED = False
PLAN = PLAN + (() if not CAP_ENABLED else tuple(
    {"name": f"battleship_cap_{index:02d}", "kind": "cap", "asset": "boulder_01", "ridge": ridge, "top_y": top_y,
     "scale": max(4.0, (top_y - ridge[1]) / (1.0 - CAP_BURIED) / CAP_UPRIGHT_HEIGHT_M), "turn_degrees": turn,
     "target": ridge}
    for index, (ridge, top_y, turn) in enumerate(CAP_POINTS)
))
# 尾根の点に rock_09 の板を寝かせたまま積んだ段（stack）。板の上面が平らな頂、板の縁が縦の側面になり、隣の点と上端の高さが
# 違う所が写真の段の角になる（立てた boulder_01 は針か丸い山にしかならなかった）。各層は上端を STACK_LAYER_STEP × 板の厚さずつ
# 下げ、尾根の高さより板の厚さの STACK_BURIED だけ下まで積む。拡大率・振り・ずらしを層ごとに巡回して積み目の繰り返しを崩す
# 段と角の板を配置表へ書くか。外形・棚は基準に入り角は 3/7 まで上がるが、既定の撮影で薄い板を積んだ塔に見え（右の段の塔に
# 白っぽい細かな横縞も出る）、写真の厚い砂岩の段にならないので、厚い板の素材が揃うまで既定の景色には置かない
# （WastelandCliffScan.md の「Battleship Rock の壁を覆う試し」）
# 厚い節理の塊（rock_09_block）に替えても、外形は基準に入るが角の一致 3/7・棚の一致 0.54〜0.59 で基準に届かず、棚の一致は
# 置かない配置表の 0.604 より下がるので、既定の景色には置かない（WastelandCliffScan.md の「Battleship Rock の壁を覆う試し」）
STACK_ENABLED = False
# 段と角に積む素材。rock_09 の薄い板（厚さ 3〜7 m）は積み目が細かな横縞の塔に見えたので、厚みの方向へ 4 倍に伸ばした
# 厚い節理の塊（1 つで段の高さ 13〜26 m、頂と側面を平らな面へ寄せた素材）にする
STACK_ASSET = "rock_09_block"
# 素材の拡大率 1 の水平の幅（局所の z の長い辺, 局所の x の短い辺）[m]
STACK_FOOTPRINT_M = {"rock_09": (0.15, 0.07), "rock_09_block": (0.110, 0.056)}
# 段と角の拡大率の倍率。rock_09_block は側面を寄せて幅が板より 24 % 細く、右の段の外形が写真より約 10 画素内側に写るが、
# 1.25 倍に広げると外形の位置合わせのずらしが (−16, −3) → (−18, −5) へさらに動き、角の一致は 3/7 のままだったので広げない
STACK_WIDEN = 1.0
STACK_SCALES = (200.0, 230.0, 185.0, 215.0)
STACK_LAYER_STEP = 0.5
# 板の上面は丸く、箱の上端より撮影の外形が 4〜6 画素（約 1.2 m）低く写ったので、段と角の上端をこの分だけ上げる
# （頂を平らな面へ寄せた rock_09_block は上げない）
STACK_TOP_LIFT_M = 1.2 if STACK_ASSET == "rock_09" else 0.0
STACK_BURIED = 0.5
STACK_JITTER_M = ((0.0, 0.0), (1.8, -1.2), (-1.5, 1.6), (1.1, 1.9))
# CAP_POINTS ごとの段の合わせ（最下層の振り[°]、層のずらしの倍率、拡大率の倍率、尾根の点からの水平のずらし (x, z) [m]）。
# 段の板の箱を ref_cliff_01 のカメラへ投影し、写真の外形（注釈のずらし 3, −5 画素）より 4 画素を超えて上へ出ない組のうち、
# 置く前の撮影（SCN-WL-SHAPE-CAUSE の lod-default）の外形と写真の外形の間を最も埋める組を、振り 5 通り・ずらしの倍率 2 通り・
# 拡大率 4 通り・カメラの右の向きへ ±8 m の中から選んだ（板が写真の段の縦の縁から横へはみ出すと角が出ない）
STACK_FIT = (
    (0.0, 0.0, 1.0, (1.91, -3.52)),
    (135.0, 1.0, 0.7, (-1.91, 3.52)),
    (-15.0, 0.0, 0.7, (3.82, -7.03)),
    (90.0, 1.0, 0.5, (3.82, -7.03)),
    (10.0, 0.0, 1.0, (3.82, -7.03)),
    (45.0, 1.0, 1.0, (3.82, -7.03)),
    (45.0, 0.0, 1.0, (3.82, -7.03)),
    (90.0, 1.0, 1.0, (-3.82, 7.03)),
    (90.0, 1.0, 0.7, (-3.82, 7.03)),
    (135.0, 1.0, 0.35, (1.91, -3.52)),
    (90.0, 0.0, 0.5, (-3.82, 7.03)),
    (135.0, 1.0, 0.7, (-1.91, 3.52)),
    (135.0, 1.0, 0.7, (-1.91, 3.52)),
    (135.0, 0.0, 0.5, (-3.82, 7.03)),
    (-25.0, 1.0, 0.5, (-3.82, 7.03)),
    (90.0, 1.0, 0.5, (-1.91, 3.52)),
)



# 写真の段の角に板の箱の角を合わせる段（corner）。(角の点 P, 岩の側, 尾根の高さ Y)。P は写真の注釈の角（ずらし 0, −7 画素）を
# 通る ref_cliff_01 の画素の光線上の、置く前の撮影（lod-default）で角のすぐ内側の崖の外形の深度の点。板の長い軸をカメラの
# 右の向きへ揃え（振り CORNER_YAW_DEGREES・層ごとに回さない・ずらさない）、手前の面の上の縁の角が P に来るように、
# 岩の側（"ur" = 角の左下、"ul" = 角の右下）へ広げる。A・C は右の段の外の角、D・E は左の縁の角。
# D・E は一度置いた撮影で角の画素からずれた分（D 左へ 19 画素、E 上へ 9 画素）を直した点。
# 4 点とも、外形の位置合わせのずらしが x −5 画素に落ち着いたので、角の点を左へ 5 画素ぶん寄せた。右の段の内側の角（写真の x 0.74）に
# 小さな張り出しを足す試しは縦の折れが出ず、外形の位置合わせのずらしを (−6, −7) へ動かしたので置かない
CORNER_STACKS = (
    ((-2089.96, 121.2, -1185.42), "ur", 92.3),
    ((-2126.78, 91.1, -1171.07), "ur", 70.6),
    ((-1945.62, 104.8, -1338.68), "ul", 97.6),
    ((-1799.29, 175.5, -1260.22), "ul", 162.5),
)
CORNER_SCALE = 120.0 * STACK_WIDEN
# 板の縁は丸く、箱の角より撮影の角が内側へ約 9 画素（1.7 m）入ったので、角の点を岩の外側へこの分だけ出す
# （側面を縦の面へ寄せた rock_09_block は縁が鋭いので出さない）
CORNER_OUT_M = 1.7 if STACK_ASSET == "rock_09" else 0.0
CORNER_YAW_DEGREES = 151.5
# ref_cliff_01 のカメラの右と前の水平の向き（エンジンの x, z）
CAMERA_RIGHT_XZ = (-0.4772, 0.8788)
CAMERA_FORWARD_XZ = (0.8788, 0.4772)


def corner_layers(point, side, ridge_y):
    width = STACK_FOOTPRINT_M[STACK_ASSET][0] * CORNER_SCALE
    depth = STACK_FOOTPRINT_M[STACK_ASSET][1] * CORNER_SCALE
    thickness = ASSET_HEIGHT_M[STACK_ASSET] * CORNER_SCALE
    lateral = (-0.5 * width + CORNER_OUT_M) if side == "ur" else (0.5 * width - CORNER_OUT_M)
    center = (point[0] + CAMERA_RIGHT_XZ[0] * lateral + CAMERA_FORWARD_XZ[0] * 0.5 * depth, ridge_y,
              point[2] + CAMERA_RIGHT_XZ[1] * lateral + CAMERA_FORWARD_XZ[1] * 0.5 * depth)
    layers = []
    while True:
        layer_top = point[1] + STACK_TOP_LIFT_M - len(layers) * STACK_LAYER_STEP * thickness
        layers.append((CORNER_SCALE, layer_top, CORNER_YAW_DEGREES, center))
        if layer_top - thickness < ridge_y - STACK_BURIED * thickness:
            return layers


def stack_layers(ridge, top_y, turn, factor=1.0, jitter=1.0):
    layers = []
    while True:
        k = len(layers)
        scale = STACK_SCALES[k % len(STACK_SCALES)] * factor * STACK_WIDEN
        thickness = ASSET_HEIGHT_M[STACK_ASSET] * scale
        layer_top = top_y - k * STACK_LAYER_STEP * thickness
        dx, dz = STACK_JITTER_M[k % len(STACK_JITTER_M)]
        layers.append((scale, layer_top, turn + 23.0 * k, (ridge[0] + dx * jitter, ridge[1], ridge[2] + dz * jitter)))
        if layer_top - thickness < ridge[1] - STACK_BURIED * thickness:
            return layers


PLAN = PLAN + (() if not STACK_ENABLED else tuple(
    {"name": f"battleship_stack_{index:02d}_{k}", "kind": "cap", "asset": STACK_ASSET, "upright": False, "ridge": center,
     "top_y": layer_top, "scale": round(scale, 2), "turn_degrees": yaw, "target": center}
    for index, ((ridge, top_y, _), (turn, jitter, factor, (sx, sz))) in enumerate(zip(CAP_POINTS, STACK_FIT))
    for k, (scale, layer_top, yaw, center) in enumerate(
        stack_layers((ridge[0] + sx, ridge[1], ridge[2] + sz), top_y + STACK_TOP_LIFT_M, turn, factor, jitter))
) + tuple(
    {"name": f"battleship_corner_{index:02d}_{k}", "kind": "cap", "asset": STACK_ASSET, "upright": False, "ridge": center,
     "top_y": layer_top, "scale": scale, "turn_degrees": yaw, "target": center}
    for index, (point, side, ridge_y) in enumerate(CORNER_STACKS)
    for k, (scale, layer_top, yaw, center) in enumerate(corner_layers(point, side, ridge_y))
))
# 置く範囲の地形を読む箱の余白[m]（狙いの点の箱から広げる）
PLACE_REGION_MARGIN_M = 90.0
# 崖の面の向きを求める当たりの格子（塊の幅・高さに対する広がりの割合と、1 辺の本数）
FACE_FIT_SPAN = 0.6
FACE_FIT_RAYS = 5
# 向きを探す範囲: 崖の面の水平の法線から、鉛直の軸まわりと壁に沿う水平の軸まわりに ±FIT_DEGREES を FIT_STEP_DEGREES 刻みで振る
FIT_DEGREES = 15.0
FIT_STEP_DEGREES = 5.0
# 面の法線の向きの押し出しを二分で探す回数（範囲は塊の法線の向きの厚みの ±1 倍）
SEARCH_BISECTIONS = 10
# 開いた縁（殻の縁の頂点）の、地形の面からの符号付きの距離（面の外が正）の RIM_GAP_PERCENTILE 百分位の上限[m]。
# rock_face の殻は法線の向きの厚みが ±0.5 m×拡大率の平たい岩肌で、縁も同じ厚みの帯にあり、崖の壁にも数 m の起伏があるので、
# 縁を全部壁に入れると殻のほとんどが壁に埋もれる。縁と壁の隙間は 0.5 m までとする（最も遠い ref_cliff_01 の 1.1 km で約 1.5 画素、
# cliff の 500 m で約 1 画素。隙間の奥に見えるのはすぐ後ろの崖の面）
RIM_GAP_PERCENTILE = 90
RIM_GAP_MAX_M = 0.5
# 見える面（地形の面の外にある面）の割合の下限（埋もれ過ぎ）
VISIBLE_MIN = 0.25
# 探すときに使う縁の頂点と面の標本の数（種は固定）
RIM_SAMPLES = 400
VISIBLE_SAMPLES = 1000
# 地形の高さの格子の刻み[m]（上から真下へ撃った最初の当たり = 地形の上面の高さ。記録の below_top の割合だけに使う）
HEIGHT_GRID_STEP_M = 0.25
# 殻の縁の頂点を求めるときに同じ位置とみなす刻み[m]（glTF は UV の継ぎ目で頂点を分けるので、位置でまとめてから数える）
WELD_QUANTUM_M = 1.0e-4

# 描く視点と太陽（エンジンの撮影と同じ。cliff は荒野の既定の太陽、ref_cliff_01 は WastelandRoutine.cpp の kReferenceShots）
RENDER_VIEWS = ("cliff", "ref_cliff_01")
VIEW_SUN = {"cliff": (58.5, 154.0), "ref_cliff_01": (10.0, 172.0)}
# 描く地形: 視錐台に掛かり、カメラから最も遠い塊までの距離にこれを足した範囲（塊より奥は背景だけなので切る）
RENDER_TERRAIN_BEYOND_M = 400.0
RENDER_SPP = 64
SUN_STRENGTH = 4.0
SKY_RGB = (0.35, 0.45, 0.65)
TERRAIN_ALBEDO = (0.30, 0.15, 0.09)

# 比べ: エンジンの塊の画素は、塊ありの深度が塊なしより COMPARE_DEPTH_MIN_M か深度の COMPARE_DEPTH_RATIO の大きい方より近い画素。
# 塊は壁から 0.5〜3 m しか出ないので閾値は小さく取る（2 つの撮影は同じカメラ・FXAA の決定的な撮影で、塊の外の深度は同じになる。
# 塊の外にこの差の画素があれば engine_pixels_outside_blocks に数える）
COMPARE_DEPTH_MIN_M = 0.1
COMPARE_DEPTH_RATIO = 0.0002
# エンジンの塊の画素を Blender の塊へ割り当てる広がり[画素]
COMPARE_ATTRIBUTE_RADIUS_PX = 12
# 合格の線: Blender は地形の LOD 0 を、エンジンは遠くを粗い段で描くので、塊が壁から 0.5 m 前後しか出ない縁では、どちらの
# 地形が前に出るかで塊の画素の縁が 1 画素ほど入れ替わる（ref_cliff_01 の 1.1 km で 1 画素 ≈ 0.36 m）。塊は 140〜260 画素で、
# 縁の 1 画素の帯が面積の 3〜4 割になるので、IoU は 0.7、重心のずれは 3 画素（ref_cliff_01 で約 1 m、cliff で約 1.6 m）までとする。
# 位置そのものは、両方に塊が映った画素の深度の差の中央値で見る。2 つの描画には画面全体に共通の深度の差（ref_cliff_01 の
# 1.1 km で 0.6 m 前後）があるので、周りの地形の画素（COMPARE_RING_PX の帯）の差の中央値に COMPARE_BLOCK_DEPTH_EXCESS_M を
# 足した所までとする
COMPARE_IOU_MIN = 0.7
COMPARE_CENTROID_MAX_PX = 3.0
COMPARE_BLOCK_DEPTH_EXCESS_M = 0.25
COMPARE_RING_PX = 6
# 拡大の倍率と余白[画素]
COMPARE_ZOOM = 4
COMPARE_CROP_PAD_PX = 24
# 比較図の添え書きの字体（日本語が描ける順に探す。CompareLight.py と同じ）
LABEL_FONT_PATHS = ("C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/msgothic.ttc")
LABEL_FONT_SIZE = 16


# ---- 塊の素材のメッシュ（glTF を numpy で読む。Blender の読み込みと同じ形） ----

def read_gltf_mesh(path):
    """glTF の最初のメッシュの位置 (N, 3) float64 と三角形 (M, 3) int64（glTF のローカルの座標）。節の変換は持たない前提"""
    with open(path, encoding="utf-8") as f:
        gltf = json.load(f)
    nodes = [n for n in gltf.get("nodes", []) if "mesh" in n]
    if len(nodes) != 1 or any(key in nodes[0] for key in ("matrix", "translation", "rotation", "scale")):
        raise ValueError(f"節の変換の無いメッシュ 1 つの glTF でない: {path}")
    with open(os.path.join(os.path.dirname(path), gltf["buffers"][0]["uri"]), "rb") as f:
        blob = f.read()

    def accessor(index):
        item = gltf["accessors"][index]
        view = gltf["bufferViews"][item["bufferView"]]
        dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[item["componentType"]]
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[item["type"]]
        offset = view.get("byteOffset", 0) + item.get("byteOffset", 0)
        data = np.frombuffer(blob, dtype=dtype, count=item["count"] * width, offset=offset)
        return data.reshape(-1, width) if width > 1 else data

    positions = []
    triangles = []
    base = 0
    for primitive in gltf["meshes"][nodes[0]["mesh"]]["primitives"]:
        p = accessor(primitive["attributes"]["POSITION"]).astype(np.float64)
        i = accessor(primitive["indices"]).astype(np.int64).reshape(-1, 3)
        positions.append(p)
        triangles.append(i + base)
        base += len(p)
    return np.concatenate(positions), np.concatenate(triangles)


def asset_geometry(asset_id):
    """塊の素材の、Blender の読み込みと同じ向き（glTF のローカルに engine_to_blender を掛けた座標）の形:
    頂点・面の重心・面積・殻の縁の頂点（位置でまとめた後に 1 つの面にしか使われない辺の端）"""
    positions, triangles = read_gltf_mesh(os.path.join(REPO_ROOT, ASSETS[asset_id]["mesh"]))
    c = np.asarray(BR.ENGINE_TO_BLENDER, dtype=np.float64)
    local = positions @ c.T
    corners = local[triangles]
    centroids = corners.mean(axis=1)
    areas = 0.5 * np.linalg.norm(np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]), axis=1)
    keys = np.round(positions / WELD_QUANTUM_M).astype(np.int64)
    _, welded = np.unique(keys, axis=0, return_inverse=True)
    welded = welded.reshape(-1)
    t = welded[triangles]
    edges = np.sort(np.concatenate([t[:, [0, 1]], t[:, [1, 2]], t[:, [2, 0]]]), axis=1)
    unique_edges, counts = np.unique(edges, axis=0, return_counts=True)
    rim_welded = np.unique(unique_edges[counts == 1].ravel())
    first_vertex = np.full(welded.max() + 1, -1, dtype=np.int64)
    first_vertex[welded[::-1]] = np.arange(len(welded))[::-1]
    rim = local[first_vertex[rim_welded]]
    # 殻の表の向き: 面の法線の面積の重みの和（底と背面が開いた殻では、開いた側の逆を向く）
    normal_sum = np.cross(corners[:, 1] - corners[:, 0], corners[:, 2] - corners[:, 0]).sum(axis=0)
    return {"vertices": local, "centroids": centroids, "areas": areas, "rim": rim, "front": normal_sum / np.linalg.norm(normal_sum),
            "min": local.min(axis=0), "max": local.max(axis=0), "triangles": len(triangles)}


# ---- 回転と配置表 ----

def rotation_to_table(rotation_blender):
    """Blender の座標の回転（3×3）を配置表の rotation_xyzw（Entity::SetRotation へ渡す四元数）へ"""
    import mathutils
    c = mathutils.Matrix(BR.ENGINE_TO_BLENDER)
    engine = c.inverted() @ mathutils.Matrix(rotation_blender) @ c
    q = engine.to_quaternion()
    q = mathutils.Quaternion((q.w, -q.x, -q.y, -q.z))
    if q.w < 0.0:
        q = mathutils.Quaternion((-q.w, -q.x, -q.y, -q.z))
    q.normalize()
    return [q.x, q.y, q.z, q.w]


def table_to_rotation(rotation_xyzw):
    """配置表の rotation_xyzw を Blender の座標の回転（3×3 の mathutils.Matrix）へ"""
    import mathutils
    x, y, z, w = (float(v) for v in rotation_xyzw)
    engine = mathutils.Quaternion((w, -x, -y, -z)).normalized().to_matrix()
    c = mathutils.Matrix(BR.ENGINE_TO_BLENDER)
    return c @ engine @ c.inverted()


def placement_matrix(position_engine, rotation_xyzw, scale):
    """配置表の 1 行から、Blender の空の物体の matrix_world"""
    import mathutils
    rotation = table_to_rotation(rotation_xyzw)
    return (mathutils.Matrix.Translation(mathutils.Vector(BR.engine_to_blender(position_engine))) @
            rotation.to_4x4() @ mathutils.Matrix.Diagonal((scale, scale, scale, 1.0)))


def read_table(path=None):
    with open(path or os.path.join(REPO_ROOT, TABLE_PATH), encoding="utf-8-sig") as f:
        return json.load(f)


def world_points(matrix_world, local):
    m = np.asarray(matrix_world, dtype=np.float64)
    return local @ m[:3, :3].T + m[:3, 3]


def write_table(roots, source, extra=None):
    """配置の印（cliff_scan_asset）の付いた空の物体の並びから配置表を書く。extra は名前ごとの記録（置いたときの検査の値）"""
    placements = []
    used = []
    for root in sorted(roots, key=lambda obj: obj["cliff_scan_name"]):
        matrix = root.matrix_world
        location, rotation, scale = matrix.decompose()
        if max(scale) - min(scale) > 1.0e-4 * max(scale):
            raise ValueError(f"塊 {root['cliff_scan_name']} の拡大が一様でない（{tuple(scale)}）")
        asset_id = root["cliff_scan_asset"]
        if asset_id not in ASSETS:
            raise ValueError(f"塊 {root['cliff_scan_name']} の素材 {asset_id} が ASSETS に無い")
        if asset_id not in used:
            used.append(asset_id)
        c = np.asarray(BR.ENGINE_TO_BLENDER, dtype=np.float64)
        position_engine = c.T @ np.asarray(location, dtype=np.float64)
        geometry = asset_geometry(asset_id)
        world_engine = world_points(matrix, geometry["vertices"]) @ c
        item = {
            "name": root["cliff_scan_name"],
            "asset": asset_id,
            "position": [round(float(v), 5) for v in position_engine],
            "rotation_xyzw": [round(float(v), 8) for v in rotation_to_table(rotation.to_matrix())],
            "scale": round(float(scale[0]), 6),
            "world_bounds_min": [round(float(v), 3) for v in world_engine.min(axis=0)],
            "world_bounds_max": [round(float(v), 3) for v in world_engine.max(axis=0)],
        }
        if extra and item["name"] in extra:
            item["blender_check"] = extra[item["name"]]
        placements.append(item)
    table = {
        "version": 1,
        "description": "荒野の崖の面に貼るスキャンの塊の配置（Scripts/Terrain/BlenderCliffScan.py が書く。"
                       "読むのは Game/GameModes/Wasteland/WastelandCliffScan.h）",
        "coordinate": "エンジンのワールド座標: m、+X 東・+Y 上・+Z 南（原点と高さの基準は Assets/Terrain/ValleyOfTheGods/height.json）",
        "rotation_convention": "rotation_xyzw はそのまま Entity::SetRotation に渡す。行ベクトルのワールド行列は、"
                               "ローカルの向きを四元数の標準の回転の逆で写す",
        "source": source,
        "assets": [ASSETS[asset_id] for asset_id in used],
        "placements": placements,
    }
    path = os.path.join(REPO_ROOT, TABLE_PATH)
    # BOM なしの UTF-8・CRLF（エンジンの JSON の読み手と同じ形。Scripts/Terrain/CommonLight.json と同じ）
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        json.dump(table, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print(f"CLIFF_SCAN_TABLE path={TABLE_PATH} assets={len(used)} placements={len(placements)}", flush=True)
    return table


# ---- Blender の中: 地形・塊・高さの格子 ----

def load_terrain(mesh_dir, lo_engine, hi_engine, name="Terrain", material=None):
    """地形の書き出しのうち、エンジンの座標の箱 [lo, hi]（X・Z）に掛かる三角形を 1 つの物体に読む（Blender の座標へ変換済み）。
    三角形の巻きは頂点の法線の向きへ揃える（符号付きの距離の面の外を、法線の側にする）"""
    import bpy
    import mathutils
    manifest = BR.load_manifest(mesh_dir)
    parts_p = []
    parts_t = []
    base = 0
    for tile in manifest["tiles"]:
        tlo, thi = tile["bounds_min"], tile["bounds_max"]
        if thi[0] < lo_engine[0] or tlo[0] > hi_engine[0] or thi[2] < lo_engine[2] or tlo[2] > hi_engine[2]:
            continue
        positions, triangles, normals = BR.read_terrain_ply(os.path.join(mesh_dir, tile["file"]), with_normals=True)
        inside = ((positions[:, 0] >= lo_engine[0]) & (positions[:, 0] <= hi_engine[0]) &
                  (positions[:, 2] >= lo_engine[2]) & (positions[:, 2] <= hi_engine[2]))
        keep = triangles[inside[triangles].any(axis=1)]
        if not len(keep):
            continue
        keep, _ = BR.orient_triangles(positions, keep, normals)
        used, remap = np.unique(keep, return_inverse=True)
        parts_p.append(positions[used])
        parts_t.append(remap.reshape(-1, 3).astype(np.int32) + base)
        base += len(used)
    if not parts_p:
        raise RuntimeError("箱に掛かる地形が無い")
    positions = np.concatenate(parts_p).astype(np.float32)
    triangles = np.concatenate(parts_t)
    mesh = BR.build_mesh(name, positions, triangles)
    if material is not None:
        mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.matrix_world = mathutils.Matrix(BR.engine_to_blender_matrix4())
    bpy.context.view_layer.update()
    return obj, positions, triangles


def terrain_bvh(positions_engine, triangles):
    """地形の三角形の BVH（Blender のワールドの座標）"""
    from mathutils.bvhtree import BVHTree
    positions = positions_engine.astype(np.float64) @ np.asarray(BR.ENGINE_TO_BLENDER, dtype=np.float64).T
    return BVHTree.FromPolygons(positions.tolist(), triangles.tolist())


def height_grid(bvh, center_xy, half_size, step):
    """Blender の (x, y) の正方形の格子で、上から真下へ撃った最初の当たりの高さ（地形の上面）。外れは −∞"""
    from mathutils import Vector
    count = int(math.ceil(2.0 * half_size / step)) + 1
    xs = center_xy[0] - half_size + step * np.arange(count)
    ys = center_xy[1] - half_size + step * np.arange(count)
    grid = np.full((count, count), -np.inf)
    down = Vector((0.0, 0.0, -1.0))
    for j, y in enumerate(ys):
        for i, x in enumerate(xs):
            hit = bvh.ray_cast(Vector((x, y, 2000.0)), down, 5000.0)
            if hit[0] is not None:
                grid[j, i] = hit[0].z
    return {"x0": xs[0], "y0": ys[0], "step": step, "grid": grid}


def sample_height(heights, xy):
    """高さの格子を双線形で引く（格子の外は −∞）"""
    grid = heights["grid"]
    fx = (xy[:, 0] - heights["x0"]) / heights["step"]
    fy = (xy[:, 1] - heights["y0"]) / heights["step"]
    i0 = np.floor(fx).astype(np.int64)
    j0 = np.floor(fy).astype(np.int64)
    ok = (i0 >= 0) & (j0 >= 0) & (i0 + 1 < grid.shape[1]) & (j0 + 1 < grid.shape[0])
    out = np.full(len(xy), -np.inf)
    i0c, j0c = np.clip(i0, 0, grid.shape[1] - 2), np.clip(j0, 0, grid.shape[0] - 2)
    tx, ty = fx - i0c, fy - j0c
    h = (grid[j0c, i0c] * (1 - tx) * (1 - ty) + grid[j0c, i0c + 1] * tx * (1 - ty) +
         grid[j0c + 1, i0c] * (1 - tx) * ty + grid[j0c + 1, i0c + 1] * tx * ty)
    out[ok] = h[ok]
    return out


def fit_face(bvh, origin_b, toward_b, width, height):
    """狙いの点から水平に撃った光線の当たりと、周りの当たりの格子に当てた平面の水平の法線（光線の来た側を向く）。(当たり, 法線)"""
    from mathutils import Vector
    toward = Vector(toward_b).normalized()
    up = Vector((0.0, 0.0, 1.0))
    side = toward.cross(up).normalized()
    hits = []
    center = None
    offsets = np.linspace(-0.5, 0.5, FACE_FIT_RAYS) * FACE_FIT_SPAN
    for a in offsets:
        for b in offsets:
            start = Vector(origin_b) + side * (a * width) + up * (b * height)
            hit = bvh.ray_cast(start, toward, 1000.0)
            if hit[0] is not None:
                hits.append(np.asarray(hit[0], dtype=np.float64))
                if a == 0.0 and b == 0.0:
                    center = np.asarray(hit[0], dtype=np.float64)
    if center is None or len(hits) < FACE_FIT_RAYS * FACE_FIT_RAYS // 2:
        raise RuntimeError(f"崖の面に当たらない（狙い {tuple(origin_b)}、当たり {len(hits)}）")
    points = np.asarray(hits)
    _, _, vt = np.linalg.svd(points - points.mean(axis=0))
    normal = vt[2]
    if normal @ np.asarray(toward) > 0.0:
        normal = -normal
    horizontal = np.array([normal[0], normal[1], 0.0])
    length = np.linalg.norm(horizontal)
    if length < 0.2:
        raise RuntimeError(f"崖の面が水平に近い（法線 {normal}）")
    return center, horizontal / length, float(np.degrees(np.arccos(min(1.0, abs(normal[2])))))


def signed_distances(bvh, points):
    """点ごとの地形の面までの符号付きの距離[m]（面の外が正。面の向きは頂点の法線に揃えた三角形の巻き）"""
    from mathutils import Vector
    out = np.empty(len(points))
    for index, point in enumerate(points):
        location, normal, _, distance = bvh.find_nearest(Vector(point))
        if location is None:
            out[index] = np.inf
            continue
        side = (point[0] - location.x) * normal.x + (point[1] - location.y) * normal.y + (point[2] - location.z) * normal.z
        out[index] = distance if side >= 0.0 else -distance
    return out


def block_frame(geometry, normal):
    """殻を壁に貼る回転（Blender の座標の 3×3）: 殻の表の向き（法線の和）を normal へ、局所の横（glTF の +X。スキャンの層の
    筋の向き）を水平に保つ。局所の枠（横・表・上）と世界の枠（壁に沿う水平・normal・normal に直交する上）を、どちらも
    表 × 横 = 上 の右手系で作って写す"""
    front_l = geometry["front"]
    side_l = np.array([1.0, 0.0, 0.0]) - front_l[0] * front_l
    side_l /= np.linalg.norm(side_l)
    local_frame = np.column_stack([side_l, front_l, np.cross(front_l, side_l)])
    up = np.array([0.0, 0.0, 1.0]) - normal[2] * normal
    up /= np.linalg.norm(up)
    world_frame = np.column_stack([np.cross(up, normal), normal, up])
    return world_frame @ local_frame.T


def turned_normal(normal, yaw_degrees, pitch_degrees):
    """水平の法線を鉛直の軸まわりに yaw、壁に沿う水平の軸まわりに pitch（正で上を向く）だけ振った向き"""
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    horizontal = np.array([normal[0] * math.cos(yaw) - normal[1] * math.sin(yaw),
                           normal[0] * math.sin(yaw) + normal[1] * math.cos(yaw), 0.0])
    horizontal /= np.linalg.norm(horizontal)
    return horizontal * math.cos(pitch) + np.array([0.0, 0.0, math.sin(pitch)])


def solve_block(plan, geometry, bvh):
    """1 つの塊の Blender の matrix_world（4×4 の numpy）と検査の値を求める。向き（面の法線から ±FIT_DEGREES）ごとに、
    開いた縁の壁からの距離の RIM_GAP_PERCENTILE 百分位が RIM_GAP_MAX_M に収まる最も外への押し出しを二分で求め、
    見える面の割合が最も大きい向きを選ぶ"""
    scale = float(plan["scale"])
    size = (geometry["max"] - geometry["min"]) * scale
    origin_b = BR.engine_to_blender(plan["target"])
    toward_b = BR.engine_to_blender(plan["toward"])
    anchor, face_normal, face_tilt = fit_face(bvh, origin_b, toward_b, size[0], size[2])
    rng = np.random.default_rng(0)
    rim_all = geometry["rim"]
    rim_local = rim_all[rng.choice(len(rim_all), size=min(RIM_SAMPLES, len(rim_all)), replace=False)]
    areas = geometry["areas"]
    picked = rng.choice(len(areas), size=min(VISIBLE_SAMPLES, len(areas)), replace=False, p=areas / areas.sum())
    centroid_local = geometry["centroids"][picked]
    base_turn = float(plan.get("turn_degrees", 0.0))
    best = None
    for yaw in np.arange(-FIT_DEGREES, FIT_DEGREES + 1.0e-6, FIT_STEP_DEGREES):
        for pitch in np.arange(-FIT_DEGREES, FIT_DEGREES + 1.0e-6, FIT_STEP_DEGREES):
            normal = turned_normal(face_normal, base_turn + yaw, pitch)
            rotation = block_frame(geometry, normal)
            vertices = geometry["vertices"] * scale @ rotation.T
            center = 0.5 * (vertices.min(axis=0) + vertices.max(axis=0))
            rim = rim_local * scale @ rotation.T - center
            centroids = centroid_local * scale @ rotation.T - center
            spread = float(np.ptp(vertices @ normal))

            def gap(t):
                return float(np.percentile(signed_distances(bvh, rim + anchor + normal * t), RIM_GAP_PERCENTILE))

            lo, hi = -spread, spread
            if gap(lo) > RIM_GAP_MAX_M:
                continue
            if gap(hi) <= RIM_GAP_MAX_M:
                lo = hi
            else:
                for _ in range(SEARCH_BISECTIONS):
                    mid = 0.5 * (lo + hi)
                    if gap(mid) <= RIM_GAP_MAX_M:
                        lo = mid
                    else:
                        hi = mid
            t = lo
            visible = float(np.mean(signed_distances(bvh, centroids + anchor + normal * t) > 0.0))
            key = (visible, -abs(yaw) - abs(pitch))
            if best is None or key > best[0]:
                best = (key, float(yaw), float(pitch), t, normal, rotation, center)
    if best is None:
        raise RuntimeError(f"{plan['name']}: 開いた縁を壁から {RIM_GAP_MAX_M} m 以内に収められる向きが無い")
    _, yaw, pitch, t, normal, rotation, center = best
    origin = anchor + normal * t
    vertices = geometry["vertices"] * scale @ rotation.T - center + origin
    rim_d = signed_distances(bvh, rim_all * scale @ rotation.T - center + origin)
    pick_all = rng.choice(len(areas), size=min(4 * VISIBLE_SAMPLES, len(areas)), replace=False, p=areas / areas.sum())
    visible = float(np.mean(signed_distances(bvh, geometry["centroids"][pick_all] * scale @ rotation.T - center + origin) > 0.0))
    heights = height_grid(bvh, anchor[:2], float(np.linalg.norm(size)) + 4.0, HEIGHT_GRID_STEP_M)
    below_top = float(np.mean(vertices[:, 2] < sample_height(heights, vertices[:, :2])))
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * scale
    # ワールドの頂点 = 回転と拡大を掛けた頂点 − 中心 + origin なので、平行移動は origin − 中心
    matrix[:3, 3] = origin - center
    to_engine = np.asarray(BR.ENGINE_TO_BLENDER).T
    check = {
        "anchor_engine": [round(float(v), 3) for v in to_engine @ anchor],
        "face_normal_engine": [round(float(v), 4) for v in to_engine @ face_normal],
        "face_tilt_degrees": round(face_tilt, 1),
        "fit_yaw_degrees": round(yaw, 1),
        "fit_pitch_degrees": round(pitch, 1),
        "offset_along_normal_m": round(float(t), 3),
        "rim_gap_p50_p90_max_m": [round(float(v), 3) for v in np.percentile(rim_d, [50, RIM_GAP_PERCENTILE, 100])],
        "rim_within_tolerance_fraction": round(float(np.mean(rim_d <= RIM_GAP_MAX_M)), 4),
        "visible_area_fraction": round(visible, 4),
        "vertex_below_top_surface_fraction": round(below_top, 4),
        "rim_vertices": int(len(rim_all)),
    }
    print(f"CLIFF_SCAN_PLACE name={plan['name']} asset={plan['asset']} scale={scale} yaw={yaw:+.0f} pitch={pitch:+.0f} "
          f"offset={t:.2f} rim_gap_p90={check['rim_gap_p50_p90_max_m'][1]:.2f} visible={visible:.3f} "
          f"below_top={below_top:.3f} face_tilt={face_tilt:.1f}", flush=True)
    if visible < VISIBLE_MIN:
        raise RuntimeError(f"{plan['name']}: 見える面の割合 {visible:.3f} が {VISIBLE_MIN} に届かない（埋もれ過ぎ）")
    return matrix, check


def solve_cap(plan, geometry, bvh):
    """尾根の上に載せる岩塊の matrix_world と検査の値。局所の y（長い軸）を鉛直に立て、鉛直の軸まわりに plan の角だけ回し、
    上端を plan["top_y"]、水平の中心を尾根の点に置く（下の CAP_BURIED の割合は尾根の中）"""
    scale = float(plan["scale"])
    yaw = math.radians(float(plan.get("turn_degrees", 0.0)))
    cz, sz = math.cos(yaw), math.sin(yaw)
    # plan["upright"] が False なら寝かせたまま（rock_09 の板を積む段）
    upright = np.array([[1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]]) if plan.get("upright", True) else np.eye(3)
    rotation = np.array([[cz, -sz, 0.0], [sz, cz, 0.0], [0.0, 0.0, 1.0]]) @ upright
    vertices = geometry["vertices"] * scale @ rotation.T
    lo_v, hi_v = vertices.min(axis=0), vertices.max(axis=0)
    top_b = np.asarray(BR.engine_to_blender((plan["ridge"][0], plan["top_y"], plan["ridge"][2])), dtype=np.float64)
    # 頂点の上端が top_b の高さ、水平の範囲の中心が尾根の点
    shift = np.array([top_b[0] - 0.5 * (lo_v[0] + hi_v[0]), top_b[1] - 0.5 * (lo_v[1] + hi_v[1]), top_b[2] - hi_v[2]])
    world = vertices + shift
    rng = np.random.default_rng(0)
    areas = geometry["areas"]
    picked = rng.choice(len(areas), size=min(VISIBLE_SAMPLES, len(areas)), replace=False, p=areas / areas.sum())
    surface_visible = float(np.mean(signed_distances(bvh, geometry["centroids"][picked] * scale @ rotation.T + shift) > 0.0))
    heights = height_grid(bvh, top_b[:2], float(np.linalg.norm(hi_v - lo_v)) + 4.0, HEIGHT_GRID_STEP_M)
    below_top = float(np.mean(world[:, 2] < sample_height(heights, world[:, :2])))
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * scale
    matrix[:3, 3] = shift
    check = {
        "kind": "cap",
        "ridge_engine": [round(float(v), 3) for v in plan["ridge"]],
        "top_y": round(float(plan["top_y"]), 3),
        "visible_surface_fraction": round(surface_visible, 4),
        "vertex_below_top_surface_fraction": round(below_top, 4),
        "size_m": [round(float(v), 2) for v in hi_v - lo_v],
    }
    print(f"CLIFF_SCAN_CAP name={plan['name']} scale={scale:.1f} top_y={plan['top_y']:.1f} visible_surface={surface_visible:.3f} "
          f"below_top={below_top:.3f}", flush=True)
    return matrix, check


def solve_embed(plan, geometry, bvh):
    """閉じた岩塊を壁に半分埋める matrix_world と検査の値。長い水平の軸を壁に沿わせ（振りと傾きは plan の角）、体積の標本
    （面の重心を中心へ寄せた点）のうち壁の外に出る割合が plan["visible"] になる面の法線の向きの押し出しを二分で求める"""
    scale = float(plan["scale"])
    size = (geometry["max"] - geometry["min"]) * scale
    origin_b = BR.engine_to_blender(plan["target"])
    toward_b = BR.engine_to_blender(plan["toward"])
    anchor, face_normal, face_tilt = fit_face(bvh, origin_b, toward_b, max(size[0], size[1]), size[2])
    normal = np.array([face_normal[0], face_normal[1], 0.0])
    normal /= np.linalg.norm(normal)
    tangent = np.cross(np.array([0.0, 0.0, 1.0]), normal)
    # 局所の長い水平の軸（x と y の長い方）を壁に沿う向きへ、上（z）を鉛直へ
    long_axis_angle = 0.0 if size[0] >= size[1] else 0.5 * math.pi
    yaw = math.atan2(tangent[1], tangent[0]) - long_axis_angle + math.radians(float(plan.get("turn_degrees", 0.0)))
    cz, sz = math.cos(yaw), math.sin(yaw)
    rot_z = np.array([[cz, -sz, 0.0], [sz, cz, 0.0], [0.0, 0.0, 1.0]])
    tilt = math.radians(float(plan.get("tilt_degrees", 0.0)))
    k = np.array([[0.0, -tangent[2], tangent[1]], [tangent[2], 0.0, -tangent[0]], [-tangent[1], tangent[0], 0.0]])
    rotation = (np.eye(3) + math.sin(tilt) * k + (1.0 - math.cos(tilt)) * (k @ k)) @ rot_z
    vertices = geometry["vertices"] * scale @ rotation.T
    center = 0.5 * (vertices.min(axis=0) + vertices.max(axis=0))
    rng = np.random.default_rng(0)
    areas = geometry["areas"]
    picked = rng.choice(len(areas), size=min(VISIBLE_SAMPLES, len(areas)), replace=False, p=areas / areas.sum())
    surface = geometry["centroids"][picked] * scale @ rotation.T - center
    # 面の重心を中心へ 0〜0.9 の割合で寄せた点を体積の標本にする
    volume = surface * (1.0 - rng.uniform(0.0, 0.9, size=len(picked))[:, None])
    spread = float(np.ptp(vertices @ normal))

    def visible_at(t):
        return float(np.mean(signed_distances(bvh, volume + anchor + normal * t) > 0.0))

    lo, hi = -spread, spread
    for _ in range(SEARCH_BISECTIONS):
        mid = 0.5 * (lo + hi)
        if visible_at(mid) < float(plan["visible"]):
            lo = mid
        else:
            hi = mid
    t = 0.5 * (lo + hi)
    origin = anchor + normal * t
    visible = visible_at(t)
    surface_visible = float(np.mean(signed_distances(bvh, surface + origin) > 0.0))
    world = vertices - center + origin
    heights = height_grid(bvh, anchor[:2], float(np.linalg.norm(size)) + 4.0, HEIGHT_GRID_STEP_M)
    below_top = float(np.mean(world[:, 2] < sample_height(heights, world[:, :2])))
    matrix = np.eye(4)
    matrix[:3, :3] = rotation * scale
    matrix[:3, 3] = origin - center
    to_engine = np.asarray(BR.ENGINE_TO_BLENDER).T
    check = {
        "kind": "embed",
        "anchor_engine": [round(float(v), 3) for v in to_engine @ anchor],
        "face_normal_engine": [round(float(v), 4) for v in to_engine @ face_normal],
        "face_tilt_degrees": round(face_tilt, 1),
        "offset_along_normal_m": round(float(t), 3),
        "visible_volume_fraction": round(visible, 4),
        "visible_surface_fraction": round(surface_visible, 4),
        "vertex_below_top_surface_fraction": round(below_top, 4),
        "size_m": [round(float(v), 2) for v in size],
    }
    print(f"CLIFF_SCAN_EMBED name={plan['name']} asset={plan['asset']} scale={scale} offset={t:.2f} visible_volume={visible:.3f} "
          f"visible_surface={surface_visible:.3f} face_tilt={face_tilt:.1f}", flush=True)
    if not EMBED_VISIBLE_RANGE[0] <= visible <= EMBED_VISIBLE_RANGE[1]:
        raise RuntimeError(f"{plan['name']}: 壁の外の体積の割合 {visible:.3f} が {EMBED_VISIBLE_RANGE} に入らない")
    return matrix, check


def import_block(scene, asset_id, name, matrix_world, pass_index=0):
    """塊の glTF を読み、配置の印の付いた空の物体の子にして置く。空の物体を返す"""
    import bpy
    import mathutils
    try:
        bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
    except (RuntimeError, AttributeError):
        pass
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(REPO_ROOT, ASSETS[asset_id]["mesh"]))
    imported = [obj for obj in bpy.data.objects if obj not in before]
    root = bpy.data.objects.new(f"CliffScan_{name}", None)
    scene.collection.objects.link(root)
    root["cliff_scan_asset"] = asset_id
    root["cliff_scan_name"] = name
    root.empty_display_type = "ARROWS"
    for obj in imported:
        if obj.parent is None:
            obj.parent = root
        if obj.type == "MESH":
            obj.pass_index = pass_index
    root.matrix_world = mathutils.Matrix([list(row) for row in matrix_world])
    bpy.context.view_layer.update()
    return root, [obj for obj in imported if obj.type == "MESH"]


def check_import_matches(meshes, root, geometry):
    """Blender の読み込みの頂点が、numpy で読んだ形に空の物体の変換を掛けたものと揃うか（読み込みの軸の約束の確認）"""
    got = np.concatenate([world_points(obj.matrix_world, np.array([v.co for v in obj.data.vertices])) for obj in meshes])
    want = world_points(root.matrix_world, geometry["vertices"])
    error = float(np.max(np.abs(np.concatenate([got.min(axis=0) - want.min(axis=0), got.max(axis=0) - want.max(axis=0)]))))
    if error > 1.0e-3:
        raise RuntimeError(f"glTF の読み込みの向きが想定と違う（範囲の差 {error:.4f} m）")
    return error


def run_place_job(job):
    import bpy
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    targets = np.array([p["target"] for p in PLAN], dtype=np.float64)
    lo = targets.min(axis=0) - PLACE_REGION_MARGIN_M
    hi = targets.max(axis=0) + PLACE_REGION_MARGIN_M
    hi[0] += PLACE_REGION_MARGIN_M  # 撃つ向き（+X）の奥の崖まで
    started = time.time()
    _, positions, triangles = load_terrain(job["mesh"], lo, hi)
    bvh = terrain_bvh(positions, triangles)
    print(f"CLIFF_SCAN_TERRAIN triangles={len(triangles)} seconds={time.time() - started:.1f}", flush=True)
    checks = {}
    roots = []
    for plan in PLAN:
        geometry = asset_geometry(plan["asset"])
        if plan.get("kind") == "embed":
            try:
                matrix, check = solve_embed(plan, geometry, bvh)
            except RuntimeError as error:
                print(f"CLIFF_SCAN_SKIP name={plan['name']} reason={error}", flush=True)
                continue
        elif plan.get("kind") == "cap":
            matrix, check = solve_cap(plan, geometry, bvh)
        else:
            matrix, check = solve_block(plan, geometry, bvh)
        root, meshes = import_block(scene, plan["asset"], plan["name"], matrix)
        check["import_bounds_error_m"] = round(check_import_matches(meshes, root, geometry), 6)
        checks[plan["name"]] = check
        roots.append(root)
    manifest_path = os.path.join(job["mesh"], "manifest.json")
    source = {"generator": "Scripts/Terrain/BlenderCliffScan.py --place",
              "terrain_mesh_manifest_sha256": hashlib.sha256(open(manifest_path, "rb").read()).hexdigest(),
              "blender": bpy.app.version_string}
    write_table(roots, source, checks)
    if job.get("save_blend"):
        os.makedirs(os.path.dirname(os.path.abspath(job["save_blend"])), exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(job["save_blend"]))
        print(f"CLIFF_SCAN_BLEND path={job['save_blend']}", flush=True)


def run_export_job(job):
    import bpy
    bpy.ops.wm.open_mainfile(filepath=os.path.abspath(job["blend"]))
    roots = [obj for obj in bpy.data.objects if "cliff_scan_asset" in obj.keys()]
    if not roots:
        raise RuntimeError("cliff_scan_asset の付いた物体が無い")
    previous = read_table() if os.path.isfile(os.path.join(REPO_ROOT, TABLE_PATH)) else {}
    write_table(roots, {"generator": "Scripts/Terrain/BlenderCliffScan.py --export", "blend": job["blend"],
                        "blender": bpy.app.version_string,
                        "terrain_mesh_manifest_sha256": (previous.get("source") or {}).get("terrain_mesh_manifest_sha256")})


def run_render_job(job):
    import bpy
    import mathutils
    import OpenImageIO as oiio
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    camera_engine = job["camera"]
    table = read_table()
    eye = np.asarray(camera_engine["position"], dtype=np.float64)
    reach = max(float(np.linalg.norm(np.asarray(p["position"]) - eye)) for p in table["placements"]) + RENDER_TERRAIN_BEYOND_M

    # 地形: 視錐台に掛かり、カメラから reach 以内の三角形
    terrain_material = BR.make_diffuse_material("Terrain", TERRAIN_ALBEDO[0])
    next(n for n in terrain_material.node_tree.nodes if n.type == "BSDF_DIFFUSE").inputs["Color"].default_value = \
        TERRAIN_ALBEDO + (1.0,)
    manifest = BR.load_manifest(job["mesh"])
    triangle_count = 0
    for tile in manifest["tiles"]:
        take, _ = BR.tile_in_range(tile, camera_engine, "frustum")
        lo, hi = np.asarray(tile["bounds_min"]), np.asarray(tile["bounds_max"])
        nearest = np.clip(eye, lo, hi)
        if not take or np.linalg.norm(nearest - eye) > reach:
            continue
        positions, triangles = BR.read_terrain_ply(os.path.join(job["mesh"], tile["file"]))
        triangles = BR.frustum_triangles(positions, triangles, camera_engine, BR.FRUSTUM_MARGIN)
        near = np.linalg.norm(positions.astype(np.float64) - eye, axis=1) <= reach
        triangles = triangles[near[triangles].any(axis=1)]
        if not len(triangles):
            continue
        used, remap = np.unique(triangles, return_inverse=True)
        mesh = BR.build_mesh(tile["name"], positions[used], remap.reshape(-1, 3).astype(np.int32))
        mesh.materials.append(terrain_material)
        obj = bpy.data.objects.new(tile["name"], mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = mathutils.Matrix(BR.engine_to_blender_matrix4())
        triangle_count += len(triangles)

    # 塊（配置表どおり。番号は配置表の並びの 1〜）
    blocks = []
    for index, placement in enumerate(table["placements"], start=1):
        matrix = placement_matrix(placement["position"], placement["rotation_xyzw"], float(placement["scale"]))
        root, meshes = import_block(scene, placement["asset"], placement["name"], [list(r) for r in matrix], index)
        error = check_import_matches(meshes, root, asset_geometry(placement["asset"]))
        blocks.append({"index": index, "name": placement["name"], "asset": placement["asset"], "import_bounds_error_m": error})

    matrix, vertical_fov = BR.camera_to_blender(camera_engine)
    camera = bpy.data.cameras.new("EngineCamera")
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.clip_start = float(camera_engine.get("near", 0.1))
    camera.clip_end = BR.CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object
    BR.make_uniform_world(scene, SKY_RGB)
    BR.make_sun_lamp(scene, SUN_STRENGTH, BR.engine_sun_direction(*VIEW_SUN[job["view"]]), BR.SUN_ANGLE_DEGREES)

    render = scene.render
    render.engine = "CYCLES"
    device = BR.setup_cycles_device(scene)
    render.resolution_x = int(job["width"])
    render.resolution_y = int(job["height"])
    render.resolution_percentage = 100
    render.film_transparent = False
    cycles = scene.cycles
    cycles.samples = RENDER_SPP
    cycles.use_adaptive_sampling = False
    cycles.use_denoising = True
    cycles.pixel_filter_type = "BOX"
    cycles.filter_width = 1.0
    cycles.max_bounces = 4
    scene.view_settings.view_transform = "Standard"
    view_layer = scene.view_layers[0]
    view_layer.use_pass_object_index = True
    view_layer.use_pass_z = True
    render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    render.image_settings.color_depth = "32"
    started = time.time()
    channels = BR.render_and_read(scene, os.path.join(job["out"], f"{job['view']}.render.exr"))
    seconds = time.time() - started
    rgb = BR.pass_rgb(channels, "Combined")
    index_keys = [key for key in channels if ".Object Index." in key or ".IndexOB." in key]
    if not index_keys:
        raise RuntimeError(f"EXR に Object Index のパスが無い（あるもの: {sorted(channels)}）")
    labels = np.rint(channels[index_keys[0]]).astype(np.int32)
    labels[(labels < 0) | (labels > len(blocks))] = 0
    # 深度（Cycles の Depth はカメラの前方向への距離 m。エンジンの AOV の depth と同じ意味。空は大きな値なので +∞ にする）
    depth_keys = [key for key in channels if key.endswith(".Depth.Z") or key.endswith(".Depth.V")]
    if not depth_keys:
        raise RuntimeError(f"EXR に Depth のパスが無い（あるもの: {sorted(channels)}）")
    depth = channels[depth_keys[0]].astype(np.float32)
    depth[depth >= 0.5 * BR.CLIP_END_M] = np.inf
    BR.write_pfm(os.path.join(job["out"], f"{job['view']}.blender_depth.pfm"), depth)
    # 見るための画: 地形の画素の輝度の中央値を 0.18 に合わせ、x / (1 + x) で丸めて sRGB へ
    luminance = rgb @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    exposure = 0.18 / max(float(np.median(luminance)), 1.0e-6)
    mapped = rgb * exposure
    mapped = mapped / (1.0 + mapped)
    encoded = np.where(mapped <= 0.0031308, mapped * 12.92, 1.055 * np.power(np.maximum(mapped, 0.0), 1.0 / 2.4) - 0.055)
    for path, image in ((os.path.join(job["out"], f"{job['view']}.blender.png"),
                         np.clip(encoded * 255.0 + 0.5, 0, 255).astype(np.uint8)),
                        (os.path.join(job["out"], f"{job['view']}.blender_label.png"), labels.astype(np.uint8)[..., None])):
        output = oiio.ImageOutput.create(path)
        spec = oiio.ImageSpec(image.shape[1], image.shape[0], image.shape[2], "uint8")
        output.open(path, spec)
        output.write_image(np.ascontiguousarray(image))
        output.close()
    for block in blocks:
        block["pixels"] = int(np.count_nonzero(labels == block["index"]))
    with open(os.path.join(job["out"], f"{job['view']}.blender.json"), "w", encoding="utf-8") as f:
        json.dump({"view": job["view"], "camera_engine": camera_engine, "width": job["width"], "height": job["height"],
                   "terrain_triangles": triangle_count, "terrain_reach_m": reach, "blocks": blocks, "device": device,
                   "samples": RENDER_SPP, "render_seconds": seconds, "sun_altitude_azimuth_degrees": VIEW_SUN[job["view"]],
                   "table_sha256": hashlib.sha256(open(os.path.join(REPO_ROOT, TABLE_PATH), "rb").read()).hexdigest()},
                  f, indent=2, ensure_ascii=False)
    print(f"CLIFF_SCAN_RENDER view={job['view']} triangles={triangle_count} blocks="
          f"{','.join(str(b['pixels']) for b in blocks)} seconds={seconds:.1f} device={device}", flush=True)


# ---- ふつうの Python の側 ----

def run_blender(args, job, label):
    os.makedirs(args.work, exist_ok=True)
    job_path = os.path.join(args.work, f"{label}.job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, indent=2, ensure_ascii=False)
    command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
               "--python", os.path.abspath(__file__), "--", "--job", job_path]
    started = time.time()
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(os.path.join(args.work, f"{label}.blender.log"), "w", encoding="utf-8") as f:
        f.write(completed.stdout)
        f.write(completed.stderr)
    for line in completed.stdout.splitlines():
        if line.startswith("CLIFF_SCAN_"):
            print(line, flush=True)
    print(f"CLIFF_SCAN_BLENDER job={label} exit={completed.returncode} seconds={time.time() - started:.1f}", flush=True)
    if completed.returncode != 0:
        tail = "\n".join((completed.stdout + completed.stderr).splitlines()[-15:])
        print(tail, flush=True)
    return completed.returncode == 0


def engine_block_mask(with_dir, without_dir, view):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402
    near = RC.read_pfm_any(os.path.join(with_dir, f"{view}.aov.depth.pfm"))[..., 0]
    far = RC.read_pfm_any(os.path.join(without_dir, f"{view}.aov.depth.pfm"))[..., 0]
    finite = np.isfinite(near)
    gap = np.where(np.isfinite(far), far, np.float32(1.0e9)) - near
    return finite & (gap > np.maximum(COMPARE_DEPTH_MIN_M, COMPARE_DEPTH_RATIO * np.where(finite, near, 0.0)))


def RC_read_depth(path):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402
    return RC.read_pfm_any(path)[..., 0]


def dilate(mask, radius):
    out = mask.copy()
    for dy in range(-radius, radius + 1):
        for dx in range(-radius, radius + 1):
            if dx * dx + dy * dy <= radius * radius:
                out |= np.roll(np.roll(mask, dy, axis=0), dx, axis=1)
    return out


def outline(mask):
    return mask & ~(np.roll(mask, 1, 0) & np.roll(mask, -1, 0) & np.roll(mask, 1, 1) & np.roll(mask, -1, 1))


def run_compare(args):
    from PIL import Image, ImageDraw, ImageFont
    font_path = next((path for path in LABEL_FONT_PATHS if os.path.isfile(path)), None)
    font = ImageFont.truetype(font_path, LABEL_FONT_SIZE) if font_path else ImageFont.load_default()
    views = [v for v in args.views.split(",") if v]
    os.makedirs(args.out, exist_ok=True)
    report = {"views": {}, "iou_min": COMPARE_IOU_MIN, "centroid_max_px": COMPARE_CENTROID_MAX_PX,
              "block_depth_excess_max_m": COMPARE_BLOCK_DEPTH_EXCESS_M}
    ok = True
    for view in views:
        labels = np.asarray(Image.open(os.path.join(args.blender, f"{view}.blender_label.png")).convert("L"), dtype=np.int32)
        with open(os.path.join(args.blender, f"{view}.blender.json"), encoding="utf-8") as f:
            record = json.load(f)
        engine_mask = engine_block_mask(args.capture, args.without, view)
        engine_depth = RC_read_depth(os.path.join(args.capture, f"{view}.aov.depth.pfm"))
        blender_depth = RC_read_depth(os.path.join(args.blender, f"{view}.blender_depth.pfm"))
        blocks = []
        for block in record["blocks"]:
            mine = labels == block["index"]
            if not mine.any():
                blocks.append({"name": block["name"], "pixels_blender": 0, "visible": False})
                ok = False
                continue
            theirs = engine_mask & dilate(mine, COMPARE_ATTRIBUTE_RADIUS_PX)
            union = np.count_nonzero(mine | theirs)
            iou = np.count_nonzero(mine & theirs) / union if union else 0.0
            ys, xs = np.nonzero(mine)
            ey, ex = np.nonzero(theirs) if theirs.any() else (ys, xs)
            offset = (float(ex.mean() - xs.mean()), float(ey.mean() - ys.mean()))
            both = mine & theirs
            block_depth = float(np.median(np.abs(engine_depth[both] - blender_depth[both]))) if both.any() else math.inf
            ring = dilate(mine | theirs, COMPARE_RING_PX) & ~(mine | theirs) & np.isfinite(engine_depth) & np.isfinite(blender_depth)
            ring_depth = float(np.median(np.abs(engine_depth[ring] - blender_depth[ring]))) if ring.any() else math.inf
            passed = bool(iou >= COMPARE_IOU_MIN and math.hypot(*offset) <= COMPARE_CENTROID_MAX_PX and
                          block_depth <= ring_depth + COMPARE_BLOCK_DEPTH_EXCESS_M)
            ok = ok and passed
            blocks.append({"name": block["name"], "pixels_blender": int(mine.sum()), "pixels_engine": int(theirs.sum()),
                           "iou": round(float(iou), 4), "centroid_offset_px": [round(offset[0], 3), round(offset[1], 3)],
                           "block_depth_abs_diff_median_m": round(block_depth, 3),
                           "surrounding_terrain_depth_abs_diff_median_m": round(ring_depth, 3),
                           "bbox_blender": [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())], "pass": passed})
            print(f"CLIFF_SCAN_COMPARE view={view} block={block['name']} iou={iou:.3f} "
                  f"offset_px={offset[0]:+.2f},{offset[1]:+.2f} pixels={int(mine.sum())}/{int(theirs.sum())} "
                  f"block_depth_diff={block_depth:.3f}m terrain_depth_diff={ring_depth:.3f}m "
                  f"{'pass' if passed else 'fail'}", flush=True)
        stray = engine_mask & ~dilate(labels > 0, COMPARE_ATTRIBUTE_RADIUS_PX)
        report["views"][view] = {"blocks": blocks, "engine_pixels_outside_blocks": int(stray.sum())}

        # 画: 上の段に Blender｜エンジン（半分の大きさ）、下の段に塊の周りの拡大（Blender・エンジン・エンジンに両方の輪郭）
        blender_img = Image.open(os.path.join(args.blender, f"{view}.blender.png")).convert("RGB")
        engine_img = Image.open(os.path.join(args.engine, f"{view}.png")).convert("RGB")
        width, height = engine_img.size
        any_block = labels > 0
        ys, xs = np.nonzero(any_block | engine_mask) if (any_block | engine_mask).any() else (np.array([0]), np.array([0]))
        x0, x1 = max(0, xs.min() - COMPARE_CROP_PAD_PX), min(width, xs.max() + COMPARE_CROP_PAD_PX + 1)
        y0, y1 = max(0, ys.min() - COMPARE_CROP_PAD_PX), min(height, ys.max() + COMPARE_CROP_PAD_PX + 1)
        overlay = np.asarray(engine_img).copy()
        overlay[outline(engine_mask)] = (0, 230, 255)
        overlay[outline(any_block)] = (255, 40, 40)
        crops = [img.crop((x0, y0, x1, y1)) for img in (blender_img, engine_img, Image.fromarray(overlay))]
        zoom = max(1, min(COMPARE_ZOOM, (width * 3 // 2) // max(1, 3 * (x1 - x0))))
        crops = [c.resize(((x1 - x0) * zoom, (y1 - y0) * zoom), Image.NEAREST) for c in crops]
        half = (width // 2, height // 2)
        top_h = half[1] + 28
        canvas = Image.new("RGB", (max(width, sum(c.width for c in crops)), top_h + crops[0].height + 28), (24, 24, 24))
        canvas.paste(blender_img.resize(half, Image.LANCZOS), (0, 28))
        canvas.paste(engine_img.resize(half, Image.LANCZOS), (half[0], 28))
        draw = ImageDraw.Draw(canvas)
        draw.text((8, 4), f"{view}: 左 Blender／右 エンジン", fill=(255, 255, 255), font=font)
        x = 0
        for crop, title in zip(crops, ("Blender", "エンジン", "エンジン＋輪郭（赤 Blender・水色 エンジン）")):
            canvas.paste(crop, (x, top_h + 28))
            draw.text((x + 8, top_h + 4), f"{title} ×{zoom} [{x0},{y0}]-[{x1},{y1}]", fill=(255, 255, 255), font=font)
            x += crop.width
        path = os.path.join(args.out, f"compare_{view}.png")
        canvas.save(path)
        print(f"CLIFF_SCAN_COMPARE view={view} image={path} engine_pixels_outside_blocks={int(stray.sum())}", flush=True)
    report["result"] = "pass" if ok else "fail"
    with open(os.path.join(args.out, "cliff_scan_compare.json"), "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    print(f"CLIFF_SCAN_COMPARE result={report['result']}", flush=True)
    return 0 if ok or not args.verify else 1


def main():
    if IN_BLENDER:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
        if len(argv) == 2 and argv[0] == "--job":
            with open(argv[1], encoding="utf-8") as f:
                job = json.load(f)
            {"place": run_place_job, "export": run_export_job, "render": run_render_job}[job["kind"]](job)
            return 0
        print("この台本はふつうの Python から起動する（--place・--export・--render・--compare）")
        return 1
    parser = argparse.ArgumentParser(description="崖の面に貼るスキャンの塊を Blender で置き、配置表に書き出す")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--place", action="store_true")
    mode.add_argument("--export", action="store_true")
    mode.add_argument("--render", action="store_true")
    mode.add_argument("--compare", action="store_true")
    parser.add_argument("--mesh", default="", help="地形の書き出し（Game --wasteland-export-terrain=<フォルダ>）")
    parser.add_argument("--save-blend", default="")
    parser.add_argument("--blend", default="")
    parser.add_argument("--capture", default="", help="塊ありの -Aov の撮影（カメラと深度）")
    parser.add_argument("--without", default="", help="--compare: 塊なしの -Aov の撮影")
    parser.add_argument("--engine", default="", help="--compare: 見比べるエンジンの撮影の PNG のフォルダ")
    parser.add_argument("--blender-dir", dest="blender", default="", help="--compare: --render の書き先")
    parser.add_argument("--out", default="")
    parser.add_argument("--views", default=",".join(RENDER_VIEWS))
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--blender", dest="blender_exe", default=BR.DEFAULT_BLENDER)
    args = parser.parse_args()
    args.blender_exe = os.path.abspath(args.blender_exe) if os.path.exists(args.blender_exe) else args.blender_exe
    work_root = args.out or os.path.dirname(os.path.abspath(args.save_blend or args.blend or TABLE_PATH))
    args.work = os.path.join(os.path.abspath(work_root), "blender-jobs")
    launcher = argparse.Namespace(blender=args.blender_exe, work=args.work)
    if args.place:
        if not args.mesh:
            parser.error("--place には --mesh が要る")
        return 0 if run_blender(launcher, {"kind": "place", "mesh": os.path.abspath(args.mesh),
                                           "save_blend": args.save_blend}, "place") else 1
    if args.export:
        if not args.blend:
            parser.error("--export には --blend が要る")
        return 0 if run_blender(launcher, {"kind": "export", "blend": os.path.abspath(args.blend)}, "export") else 1
    if args.render:
        if not (args.mesh and args.capture and args.out):
            parser.error("--render には --mesh・--capture・--out が要る")
        os.makedirs(args.out, exist_ok=True)
        ok = True
        for view in [v for v in args.views.split(",") if v]:
            camera, width, height = BR.read_engine_camera(os.path.abspath(args.capture), view)
            ok = run_blender(launcher, {"kind": "render", "mesh": os.path.abspath(args.mesh), "view": view, "camera": camera,
                                        "width": width, "height": height, "out": os.path.abspath(args.out)},
                             f"render-{view}") and ok
        return 0 if ok else 1
    if not (args.capture and args.without and args.blender and args.engine and args.out):
        parser.error("--compare には --capture・--without・--blender-dir・--engine・--out が要る")
    return run_compare(args)


if __name__ == "__main__":
    sys.exit(main())

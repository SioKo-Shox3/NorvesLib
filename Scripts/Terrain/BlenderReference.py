# 荒野の正解の絵の比較（SCN-WLR-*、計画は Docs/Plans/WastelandRebuild.md）の Blender 側の台本。
# エンジンが描く地形のメッシュ（Game の --wasteland-export-terrain が書いた区画ごとの LOD 0 の PLY）を Blender へ読み込み、
# エンジンの撮影と同じカメラを置いて、Cycles で描く。
#
# 使い方（ふつうの Python から。視点ごとに Blender をヘッドレスで起動し、この台本を --python で渡す）:
#   python Scripts/Terrain/BlenderReference.py --depth --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views a,b,...] [--range view|all] [--blender <blender.exe>]
#       エンジンの撮影（CaptureScene.ps1 -Aov）の <視点>.aov.json のカメラ（位置・向き・縦の画角・縦横比・画素数）を置き、
#       地形の深度（カメラの前方向への距離 m。エンジンの AOV の depth と同じ意味）を Cycles の 1 サンプルで描いて、
#       <撮影>/blender/<視点>.depth.pfm（下の行から並ぶ float32 の 1 チャンネル。空は +∞）と <視点>.json を書く。
#       最後に入力の記録 <撮影>/blender/inputs.json（台本・メッシュの一覧と PLY・地形のメッシュの元の入力のハッシュ、視点のカメラ）
#       を書く。照合は Scripts/Terrain/ReferenceCheck.py --projection。
#
# 座標の変換（m・軸・標高の基準・原点）はこの台本の「座標の変換」の節だけで行う。
# 読む区画（--range）: frustum（既定。深度に要るのは見える物だけ）は視錐台に掛かる区画、shadow はそれに加えてカメラから水平に
#   SHADOW_RANGE_M（影の届く 4 km）以内に掛かる区画（照明を比べるとき）、all は全区画。
#   書き出しは全区画（近景の DEM・遠景の段の全部）なので、影・空の遮蔽の届く範囲はすべて書き出しに入っている。
#   全量（LOD 0 で約 1.5 億三角形）を Blender に読むとメモリが 60 GB を超えるので、視点ごとに読む範囲を絞る。
# 読み方: PLY は Blender の読み込み（bpy.ops.wm.ply_import）でも読めるが、法線を角ごとの法線として持ち、1 億三角形で数十 GB に
#   なるので、深度では書き出しの決まった並びを numpy で読み、位置と三角形だけでメッシュを作る（read_terrain_ply）。
#
# 深度の描き方: 地形の全区画に、Cycles の Camera Data の View Z Depth（カメラの前方向への距離）をそのまま放射する材質を掛け、
#   放射だけを 1 サンプルで描く（跳ね返り 0・デノイズなし）。画素の中の標本の位置は箱のフィルタの幅 PIXEL_FILTER_WIDTH 画素で
#   画素の中心に寄せる（エンジンの GBuffer は画素の中心の値）。空（何にも当たらない画素）は透明の背景で見分ける。
#   Blender 5.2 の多層 EXR は多パートで、Combined 以外のパスは別のサブ画像に入る（深度ではパスを使わない）。
#
#   python Scripts/Terrain/BlenderReference.py --gray --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views a,b,...] [--range shadow] [--max-spp N] [--max-seconds S] [--chunk-spp N] [--skip-plate|--plate-only]
#       灰色の材質（CommonLight.json の gray_material: Principled BSDF の Base Color・Roughness・IOR 1.5）の地形を、共通の照明
#       （一様な空の放射輝度の World と、照度 E⊥ の Sun。角の直径はエンジンの太陽の円盤と同じ SUN_ANGLE_DEGREES）で Cycles に描き、
#       <撮影>/cycles/ に書く。値はエンジンの HDR と同じ尺度（Sun の Strength に lux、World に cd/m² の数をそのまま入れ、線形の出力を
#       そのまま読む。光の単位の換算は掛けない）。デノイズは掛けない。
#       1. 尺度の較正（--skip-plate で省く）: 灰色の板（gray_plate のアルベド、鏡面なしの Diffuse BSDF）だけの場面を sun・sky の
#          2 通りで描き、plate_<case>.exr / .pfm と plate_<case>.json を書く（ReferenceCheck.py --gray が期待の値と ±3 % で照らす）。
#       2. 視点ごとに（--plate-only で省く）: 太陽の見え方（白いランバート面・太陽だけ・直接光だけ、VISIBILITY_SPP サンプル）と
#          法線を描いて <視点>.aux.exr と PFM（visibility・n_dot_l・normal・alpha）を書き、続けて本描画を CHUNK_SPP サンプルずつ
#          種を変えて重ね、偶数番目を A・奇数番目を B に足す。組ごとに GrayRegions.convergence で領域ごとの収束を測り、
#          1 本あたり MIN_SPP_PER_SEED 以上で全領域が収束するか、--max-spp か --max-seconds に達したら止める。<視点>.cycles.exr（2 本の平均。
#          合成・太陽の光の組・空の光の組・直接光・間接光）と、照合の読む PFM（<視点>.cycles.{hdr,seed_a,seed_b,sun,sky,
#          direct,indirect,indirect_diffuse}.pfm。indirect_diffuse は間接光の拡散の分）・<視点>.cycles.json（設定と収束の経過）を書く。
#       最後に入力の記録 <撮影>/cycles/inputs.json（台本・GrayRegions.py・CommonLight.json・メッシュの一覧と PLY・地形のメッシュの元の
#       入力のハッシュ、カメラと深度を借りたエンジンの撮影の入力のハッシュ files_sha256）を書く。
#
#   python Scripts/Terrain/BlenderReference.py --albedo|--normal --mesh <書き出しのフォルダ> --capture <エンジンの撮影> [--mips <フォルダ>]
#       地表の材質の照明の無い値を、OSL のシェーダー（TerrainAlbedo.osl に値の表を足した terrain_albedo.osl。値の表とミップの段は
#       TerrainMaterialReference.py が撮影のログから作る）の出力を放射させて 1 サンプルで描く。--albedo はアルベドを
#       <撮影>/albedo/ に、--normal は法線（ShadedNormal）・粗さ（Roughness）・法線マップを掛ける前の法線（BaseNormal）を
#       出力ごとに描き直して <撮影>/normal/ に書く。どちらも頂点の法線と被覆も書き、最後に入力の記録 inputs.json を書く。
#       照合は ReferenceCheck.py --albedo・--normal。
#
#   python Scripts/Terrain/BlenderReference.py --lit --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views a,b,...] [--range shadow] [--max-spp N] [--max-seconds S] [--skip-material|--material-only]
#       実材質の地形を共通の照明で描く（SCN-WLR-CYCLES-LIT）。
#       1. 画素ごとの材質（--skip-material で省く）: --albedo と同じ描き方（OSL の出力を 1 サンプルで放射、撮影の記録のカメラの
#          まま）で、アルベド・法線マップを掛けた法線・粗さ・材質の AO を <撮影>/material/ に書く。
#       2. Cycles の描画: --gray と同じ照明・収束の基準・書き出し（<撮影>/cycles/）で、材質だけを替える。カメラの光線の当たりは
#          1 の画素ごとの値を画面の座標で最近傍に引き（エンジンの GBuffer と同じ画素の中心の材質）、影・照り返しの光線の
#          当たりは OSL の PASS_BOUNCE（アルベドと粗さを法線マップなし・等方の足跡で）で求める。
#       新しい描画はどれもカメラをずらさない（SCN-WL-CAMERA-OFFSET。撮影の画は記録のカメラと揃う）。カメラのずらしが 0 でない
#       描画は Blender を起動する前に断る（--skip-material でずらしありの旧材質を使う --lit も断る。材質から描き直す）。
#       ずらしありで描いた旧参照は ReferenceCheck.SHIFTED_REFERENCE_SCOPE の範囲でだけ使う。
#          材質の AO は Cycles に掛けない（エンジンだけの近似。照合の表に領域ごとの平均を書く）。照合は ReferenceCheck.py --lit。
#
#   python Scripts/Terrain/BlenderReference.py --lit --crop-regions shadow_cliff,shadow_ground --views <視点> ...
#       本描画で収束しない領域だけを切り出して描く（SCN-WLR-LIT-SHADOW-REF-C01）。結果は <撮影>/LIT_CROP_DIR/ の下に
#       material/・cycles/ として --lit と同じ形で書く（本描画の参照は触らない）。太陽の見え方で決めた領域のうち名指しの領域の
#       画素を囲む枠（CROP_PAD_PX 画素の余白つき）を Cycles の border で描き、収束は名指しの領域だけで測る。
#       本描画の 2 本の差は、2 回目の当たりで BSDF の標本が太陽の円盤に入った 1 標本（スループット ≈ アルベドの 2 乗 ×
#       太陽の放射輝度）だけで決まっていた。ライトツリーは当たりの点の法線で太陽の選ばれやすさを見積もり、それが 0 に近い点では
#       BSDF の標本の重みがほぼ 1 になるので、切り出しではライトツリーを切る（光の選び方だけの違いで、期待値は変わらない）。
#       種ごとに、同じ枠を跳ね返り 0（直接光だけ）でも描き、太陽の組・空の組を太陽の直接光（sun_direct）・空の直接光
#       （sky_direct）として書く（間接光 indirect は本描画の Diffuse/Glossy Indirect。その拡散の分だけを indirect_diffuse
#       にも書く。エンジンの RTGI の照り返しは拡散だけなので、ReferenceCheck.py --bounce はこちらと比べる）。種は 1 本ずつ別の起動で描き、
#       1 本 CROP_MAX_TOTAL_SECONDS（読み込みと最初の 1 回の描画を除く）で止める: A は時間の上限まで描いて途中の和
#       （cycles.part_a.*）を書き、B は A と同じサンプル数まで描いて A と合わせ、収束を測って --lit と同じ出力を書く。
#       最初の 1 回の描画は BVH の構築を含み、同じ枠でも 500〜1000 秒と揺れるので、上限はその後のサンプルの時間に掛ける。
#       --crop-reuse-a は記録と中身の合う A の途中の和を残し、B だけを描く。
#
#   python Scripts/Terrain/BlenderReference.py --building --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views outpost] [--range shadow] [--trial] [--max-spp N] [--out <書き先>]
#       前哨地の建物 1 棟を比べる（SCN-WLR-BUILDING-REF-BASE）。エンジンの撮影（Game の --wasteland-common-light・
#       --wasteland-hide=all-but-building・--wasteland-finest-lod を CaptureScene.ps1 -Hdr -Aov で撮ったもの）のログの
#       WASTELAND_BUILDING_PLACEMENT reference=1 の行から glTF と配置（位置・Y 軸まわりの回転・拡大率）を読み、同じ glTF を
#       Blender の glTF の読み込み（材質も glTF のまま）で同じ配置に置く。地形は灰色の材質（--gray と同じ）で、照明・カメラ・
#       収束の重ね方・書き出しは --gray と同じ。建物の領域と収束は BuildingRegions.py（足元の帯・壁の下端・壁の日なたと陰）。
#       領域を決めるため、太陽の見え方と一緒に物体の番号（建物 1）を描き、続けて --depth と同じ描き方で深度を描いて、
#       <視点>.aux.depth.pfm（カメラの前方向への距離 m、空は +∞）と <視点>.aux.building.pfm（建物の画素 1）を書く。
#       本描画の拡散の色のパス（Diffuse Color。glTF の基本色をテクセルごとに線形へ戻してから標本した値）を
#       <視点>.cycles.diffuse_color.pfm に書く（エンジンのアルベドの AOV と比べる）。
#       --trial は 1 本 BUILDING_TRIAL_SPP サンプルで止める試しの描画（書き先の既定 <撮影>/cycles-trial。収束しない）。
#       入力の記録 <書き先>/inputs.json に、台本・領域の定義・共通の照明・glTF・地形のメッシュのハッシュとカメラ・配置を書く。
#       照合は ReferenceCheck.py --building。
#
#   python Scripts/Terrain/BlenderReference.py --plant --mesh <書き出しのフォルダ> --capture <エンジンの撮影>
#          [--views plant_near] [--range shadow] [--trial] [--max-spp N] [--out <書き先>]
#       低木 1 株を比べる（SCN-WLR-PLANT-REF-BASE）。エンジンの撮影（Game の --wasteland-common-light・
#       --wasteland-hide=all-but-plant・--wasteland-finest-lod を CaptureScene.ps1 -Hdr -Aov で撮ったもの）のログの
#       WASTELAND_PLANT_PLACEMENT reference=1 の行と、撮影のフォルダの reference_plant.ply（Game が書いた株のワールドの
#       三角形と線形のアルベド）を読み、同じ三角形を置く（PlantRegions.py）。株の模型はアルファ抜きを使わないので不透明度の
#       マスクは全面 1。枝は Principled BSDF（頂点のアルベド・ログの粗さ・IOR 1.5）、葉はそれに両面の透過（Translucent BSDF。
#       色は頂点のアルベドの PLANT_LEAF_TRANSMITTANCE_RATIO 倍）を足す。地形は灰色の材質で、照明・カメラ・収束の重ね方・
#       書き出しは --building と同じ。物体の番号（枝 1・葉 2）と、葉の裏の見え方（白い透過の面で太陽だけを描く）も描き、
#       <視点>.aux.depth.pfm・<視点>.aux.plant.pfm（物体の番号）・<視点>.aux.back_visibility.pfm を書く。領域はエンジンの深度
#       （<撮影>/<視点>.aov.depth.pfm）と同じ面に当たった画素に限る。--trial は 1 本 PLANT_TRIAL_SPP サンプルで止める試しの描画
#       （書き先の既定 <撮影>/cycles-trial）。入力の記録 <書き先>/inputs.json に、台本・領域の定義・共通の照明・株の PLY・
#       地形のメッシュのハッシュとカメラ・配置を書く。照合は ReferenceCheck.py --plant。
#
#   python Scripts/Terrain/BlenderReference.py --cloud --capture <雲の書き出しの撮影> [--views cloud_sky] [--trial]
#          [--max-spp N] [--max-seconds S] [--out <書き先>]
#       雲を比べる（SCN-WLR-CLOUD-REF-BLENDER）。CaptureScene.ps1 -GameArgs cloud-dump の撮影の書き出し（<視点>.cloud.json と
#       3D ノイズの格子・空の放射輝度 LUT。契約は Docs/RenderingValidation/WastelandCloudDump.md）から、CloudVolume.py が
#       エンジンの密度の式（CloudDumpCheck.CloudModel）を 16 m の格子で評価した体積（OpenVDB、格子 density）を Blender の
#       Volume として置き、雲の層の外は真空で描く（空気遠近・霞は描かない。エンジンの霞の前の散乱光と比べる）。
#       材質: 散乱の係数 = 密度 × 消散係数（荒野 0.006 /m）で吸収は 0（エンジンの積分と同じく散乱のアルベド 1）。位相関数は
#       エンジンの最初の回の mix(HG(0.8), HG(−0.3), 0.3) を、異方性 0.8 と −0.3 の Volume Scatter を係数 0.7 と 0.3 で足して作る
#       （多重散乱はエンジンの近似でなく Cycles が光路で数える。体積の跳ね返り CLOUD_VOLUME_BOUNCES 回まで）。
#       太陽: 書き出しの太陽の地表の照度（RGB）を照度とする Sun（角の直径は SUN_ANGLE_DEGREES）。空: 空の放射輝度 LUT を
#       エンジンの SampleSky と同じ引き方（正距円筒、横は繰り返し、縦は端で止める線形）で引く World。地面: カメラの下の高度 0 に
#       書き出しの地面のアルベドの拡散の板（半辺 CLOUD_GROUND_HALF_SIZE_M）。カメラ: 書き出しの逆の視線行列から求めた向き・
#       縦の画角を、雲の式の高度（max(カメラの y, 1)）に置く。
#       描き方: 透明の背景で、雲の散乱光（RGB）と不透明度（α = 1 − 雲の透過率）を、画素の中心に寄せたフィルタで描く。
#       先に雲を隠した空だけの画（<視点>.aux.sky.pfm）と、同じ格子の吸収だけの体積を光線の歩で積んだ透過率の画
#       （<視点>.aux.transmittance.pfm。置き方の照合用）を描き、続けて種を変えた本描画を CHUNK_SPP サンプルずつ A・B に重ね、
#       CloudVolume の視線の領域で収束を測る（--gray と同じ基準と止め方）。<視点>.cycles.{hdr,seed_a,seed_b}.pfm（RGB）・
#       <視点>.cycles.{alpha,alpha_a,alpha_b}.pfm・<視点>.cycles.json（設定・格子・収束の経過）を書く。--trial は 1 本
#       CLOUD_TRIAL_SPP サンプルで止める試しの描画（書き先の既定 <撮影>/cycles-trial）。入力の記録 <書き先>/inputs.json に、
#       台本・CloudVolume.py・CloudDumpCheck.py・GrayRegions.py と書き出しのファイルのハッシュ、カメラ・格子・領域を書く。
#       照合は ReferenceCheck.py --cloud。
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

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_BLENDER = r"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe"
HEIGHT_DESCRIPTOR = "Assets/Terrain/ValleyOfTheGods/height.json"
# 影の届く範囲[m]（太陽の低い視点で、崖の影が届く距離として計画で決めた値）
SHADOW_RANGE_M = 4000.0
# 視錐台で区画を選ぶときの余裕（画角の正接に掛ける）
FRUSTUM_MARGIN = 1.02
# 画素の中の標本を中心へ寄せる箱のフィルタの幅[画素]（Cycles の最小）
PIXEL_FILTER_WIDTH = 0.01
# Blender のカメラの遠い面[m]（エンジンの撮影の far 400 km より遠く）
CLIP_END_M = 1.0e6
RESULT_DIR = "blender"

# --gray（灰色の地形の Cycles の描画）
GRAY_RESULT_DIR = "cycles"
COMMON_LIGHT = "Scripts/Terrain/CommonLight.json"
GRAY_REGIONS = "Scripts/Terrain/GrayRegions.py"
GRAY_VIEWS = ("ref_cliff_01", "ref_far_01")
# 太陽の円盤の角の直径[度]: エンジンの SolarDiskSolidAngleSteradians（6.87e-5 sr）の円の 2 倍の角半径
SUN_ANGLE_DEGREES = math.degrees(2.0 * math.sqrt(6.87e-5 / math.pi))
# 跳ね返りの上限（アルベド 0.3 では 16 回目の寄与は 0.3^16 ≈ 4e-9 で無視できる）
GRAY_MAX_BOUNCES = 16
# 画素のフィルタ（箱、1 画素。エンジンの画素は TAA のジッタで画素の面を平均する）
GRAY_PIXEL_FILTER_WIDTH = 1.0
# 太陽の見え方の描画のサンプル数
VISIBILITY_SPP = 16
# 本描画: 1 回の描画のサンプル数・1 本（A か B）あたりの上限・1 視点あたりの時間の上限[秒]
CHUNK_SPP = 64
# 収束の判定を始める 1 本あたりのサンプル数（少ない標本で、まれな経路を取りこぼしたまま 2 本が偶然揃うのを避ける）
MIN_SPP_PER_SEED = 512
MAX_SPP_PER_SEED = 2048
MAX_SECONDS_PER_VIEW = 2400.0
# 切り出しの描画（--crop-regions）: 結果の置き場、枠の余白、1 本の時間の上限（読み込みと最初の 1 回の描画を除く）、
# 1 本あたりのサンプル数の上限、
# 直接光だけの描画の種（本描画の種と重ならない）
LIT_CROP_DIR = "shadow"
CROP_PAD_PX = 8
CROP_MAX_TOTAL_SECONDS = 1200.0
CROP_MAX_SPP_PER_SEED = 16384
CROP_DIRECT_SEED_OFFSET = 1000000
# 建物 1 棟の比較（--building）: 視点・書き先・試しの描画の 1 本あたりのサンプル数・領域の定義
BUILDING_VIEWS = ("outpost",)
BUILDING_RESULT_DIR = "cycles-building"
BUILDING_TRIAL_DIR = "cycles-trial"
BUILDING_TRIAL_SPP = 64
BUILDING_REGIONS = "Scripts/Terrain/BuildingRegions.py"
# 低木 1 株の比較（--plant）: 視点・書き先・試しの描画の 1 本あたりのサンプル数・領域の定義・株の PLY の名前
PLANT_VIEWS = ("plant_near",)
PLANT_RESULT_DIR = "cycles-plant"
PLANT_TRIAL_DIR = "cycles-trial"
PLANT_TRIAL_SPP = 64
PLANT_REGIONS = "Scripts/Terrain/PlantRegions.py"
PLANT_PLY = "reference_plant.ply"
# 葉の透過の色の、頂点のアルベド（反射）に対する比。薄い葉の可視域の透過は反射と同じ程度で、乾いた土地の低木の小さく厚い葉は
# それより小さいので半分とする（エンジンの葉は透過を持たない。比べる段で透過の有無を切り分けるための素直な値）
PLANT_LEAF_TRANSMITTANCE_RATIO = 0.5
# 雲の比較（--cloud）
CLOUD_VIEWS = ("cloud_sky",)
CLOUD_RESULT_DIR = "cycles-cloud"
CLOUD_TRIAL_DIR = "cycles-trial"
CLOUD_TRIAL_SPP = 64
CLOUD_VOLUME = "Scripts/Terrain/CloudVolume.py"
CLOUD_DUMP_CHECK = "Scripts/Terrain/CloudDumpCheck.py"
# 体積の跳ね返りの上限（雲の光学的深さは数〜十程度で、散乱のアルベド 1 の光路は深さの 2 乗ほど跳ねる）と地面の拡散の跳ね返り
CLOUD_VOLUME_BOUNCES = 256
CLOUD_DIFFUSE_BOUNCES = 8
# 地面の板の半辺[m]（雲の層の 2500〜3600 m から 2.9° より下を見る向きは板に当たる）
CLOUD_GROUND_HALF_SIZE_M = 60000.0
# 空だけの画のサンプル数（空の引き方の照合。画素の中心の 1 本の視線の値）
CLOUD_SKY_SPP = 4
# 透過率の画のサンプル数（同じ密度の吸収だけの体積を光線の歩で積む。散乱のある本描画の α は散乱した標本で透明が切れるので
# 粒が粗く、置き方の照合に使えない）
CLOUD_TRANSMITTANCE_SPP = 16
# 板の較正（エンジンの板の撮影 SCN-WLR-COMMON-LIGHT と同じ視点の太陽）
PLATE_VIEW = "ref_cliff_01"
PLATE_CASES = ("sun", "sky")
PLATE_WIDTH = 384
PLATE_HEIGHT = 216
PLATE_VERTICAL_FOV_DEGREES = 40.0
PLATE_SPP = {"sun": 64, "sky": 1024}
# --albedo: 照明の無いアルベドの再現（SCN-WLR-MATERIAL-ALBEDO）
ALBEDO_RESULT_DIR = "albedo"
ALBEDO_VIEWS = ("ref_cliff_01", "ref_ground_01")
# --normal: 照明の無い法線と粗さの再現（SCN-WLR-MATERIAL-NORMAL）。同じ OSL の別の出力を描く
NORMAL_RESULT_DIR = "normal"
# 描く出力: OSL の出力の名前、書く PFM の名前、意味、復号（encoded_normal は放射の 0〜1 を −1〜1 の向きへ戻して正規化する）、
# OSL の入力 Pass の値（TerrainAlbedo.osl の PASS_*。その出力に要らない画像を引かない）
MATERIAL_PASSES = {
    "albedo": [("Albedo", "albedo", "linear_albedo", None, 0)],
    "normal": [("ShadedNormal", "shaded_normal", "shaded_world_normal_engine", "encoded_normal", 1),
               ("Roughness", "roughness", "r_final_roughness_g_base_roughness_b_lost_variance", None, 2),
               ("BaseNormal", "base_normal", "geometric_world_normal_engine_after_far_map", "encoded_normal", 3)],
    "lit": [("Albedo", "albedo", "linear_albedo", None, 0),
            ("ShadedNormal", "shaded_normal", "shaded_world_normal_engine", "encoded_normal", 1),
            ("Roughness", "roughness", "r_final_roughness_g_base_roughness_b_lost_variance", None, 2),
            ("Occlusion", "occlusion", "material_ambient_occlusion", None, 4)],
}
# --lit: 実材質の地形を共通の照明で描く（SCN-WLR-CYCLES-LIT）。まず画素ごとの材質（アルベド・法線・粗さ・AO）を --albedo と
# 同じ描き方で <撮影>/material/ に描き、続けて Cycles の照明つきの描画を <撮影>/cycles/ に書く
LIT_MATERIAL_DIR = "material"
LIT_VIEWS = ("ref_cliff_01", "ref_far_01", "ref_ground_01")
MATERIAL_RESULT_DIRS = {"albedo": ALBEDO_RESULT_DIR, "normal": NORMAL_RESULT_DIR, "lit": LIT_MATERIAL_DIR}
# カメラから直接見えない当たりの材質を描く OSL の Pass（TerrainAlbedo.osl の PASS_BOUNCE）
PASS_BOUNCE = 5
# 区画の三角形の和がこれを超える視点は、画面を縦の列に分け、列ごとに Blender を起動してその列の視錐台に掛かる三角形だけで描く
# （OSL の法線の出力の JIT が、数千万の三角形を読んだ後のプロセスで LLVM の IMAGE_REL_AMD64_ADDR32NB の制約に当たって
# 落ちる。ref_ground_01 の 1000 万三角形では通り、ref_cliff_01 の 5500 万では落ちた）。列の視錐台に掛かる三角形は漏れなく
# 読むので、列の中の画素の隠れ方は 1 回で描くのと同じ
MATERIAL_COLUMN_TRIANGLES = 20000000
MATERIAL_COLUMNS = 4
# 列の視錐台と描く範囲を列の外へ広げる画素の数
MATERIAL_COLUMN_PAD_PX = 4
MATERIAL_SCRIPT = "Scripts/Terrain/TerrainMaterialReference.py"
# 三角形を視錐台で間引くときの画角の余裕（区画を選ぶ FRUSTUM_MARGIN と同じ）
ALBEDO_TRIANGLE_MARGIN = FRUSTUM_MARGIN


# ---- 座標の変換（この台本でここだけ） ----
# エンジンのワールド: m、+X 東・+Y 上・+Z 南の右手系。原点は height.json の origin_utm（UTM 12N の east・north）で、
#   Y = 0 は NAVD88 の標高 height_offset_m（X = east − origin.east、Z = −(north − origin.north)、Y = NAVD88 − height_offset_m）。
# Blender: m、+X 東・+Y 北・+Z 上の右手系。原点と標高の基準はエンジンと同じにする（x = X、y = −Z、z = Y）。
#   UTM と NAVD88 へは east = x + origin.east、north = y + origin.north、NAVD88 = z + height_offset_m。
ENGINE_TO_BLENDER = ((1.0, 0.0, 0.0), (0.0, 0.0, -1.0), (0.0, 1.0, 0.0))


def engine_to_blender(v):
    """エンジンのワールドの位置か向き（3 成分）を Blender の座標へ"""
    return tuple(sum(ENGINE_TO_BLENDER[row][col] * float(v[col]) for col in range(3)) for row in range(3))


def engine_to_blender_matrix4():
    """エンジンの座標のままの頂点を持つ物体に掛ける 4×4 の変換（行の並び）"""
    return [list(ENGINE_TO_BLENDER[0]) + [0.0], list(ENGINE_TO_BLENDER[1]) + [0.0],
            list(ENGINE_TO_BLENDER[2]) + [0.0], [0.0, 0.0, 0.0, 1.0]]


def camera_to_blender(camera):
    """エンジンのカメラ（aov.json の camera）を Blender のカメラの姿勢（4×4 の行の並び）と縦の画角[ラジアン]へ。
    Blender のカメラは自分の −Z を向き、+Y が上、+X が右"""
    position = engine_to_blender(camera["position"])
    right = engine_to_blender(camera["right"])
    up = engine_to_blender(camera["up"])
    back = tuple(-c for c in engine_to_blender(camera["forward"]))
    matrix = [[right[i], up[i], back[i], position[i]] for i in range(3)] + [[0.0, 0.0, 0.0, 1.0]]
    return matrix, math.radians(float(camera["vertical_fov_degrees"]))


def coordinate_record():
    """記録に書く座標の約束（height.json の原点と標高の基準を添える）"""
    with open(os.path.join(REPO_ROOT, HEIGHT_DESCRIPTOR), encoding="utf-8-sig") as f:
        height = json.load(f)
    return {
        "engine": "m, +X east, +Y up, +Z south (right-handed)",
        "blender": "m, +X east, +Y north, +Z up (right-handed); x = X, y = -Z, z = Y",
        "origin_utm": height.get("origin_utm"),
        "crs": height.get("crs"),
        "height_offset_m": height.get("height_offset_m"),
        "vertical_datum": height.get("vertical_datum"),
    }


# ---- ふつうの Python の側（区画を選び、視点ごとに Blender を起動して、記録を書く） ----

def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 24), b""):
            digest.update(block)
    return digest.hexdigest()


def load_previous_record(path, record, keys):
    """前の入力の記録を読み、keys の値がすべて今の記録と同じときだけ返す（視点を分けて描いたときに前の視点を残すため）"""
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as f:
        previous = json.load(f)
    if any(previous.get(key) != record.get(key) for key in keys):
        return None
    return previous


def load_manifest(mesh_dir):
    path = os.path.join(mesh_dir, "manifest.json")
    with open(path, encoding="utf-8-sig") as f:
        manifest = json.load(f)
    if manifest.get("failed_tiles"):
        raise RuntimeError(f"書き出しに失敗した区画がある（{manifest['failed_tiles']}）")
    return manifest


def tile_in_range(tile, camera, range_mode):
    """区画（manifest の境界の箱。エンジンの座標）を読むか。(読むか, 理由)"""
    if range_mode == "all":
        return True, "all"
    lo = np.asarray(tile["bounds_min"], dtype=np.float64)
    hi = np.asarray(tile["bounds_max"], dtype=np.float64)
    eye = np.asarray(camera["position"], dtype=np.float64)
    if range_mode == "shadow":
        # 水平の距離（X・Z の箱への最短）
        dx = max(lo[0] - eye[0], 0.0, eye[0] - hi[0])
        dz = max(lo[2] - eye[2], 0.0, eye[2] - hi[2])
        if math.hypot(dx, dz) <= SHADOW_RANGE_M:
            return True, "shadow_range"
    # 視錐台: 箱の 8 隅がすべて同じ面の外なら外
    forward = np.asarray(camera["forward"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    tan_v = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0) * FRUSTUM_MARGIN
    tan_h = tan_v * float(camera["aspect_ratio"])
    planes = [forward, forward * tan_h - right, forward * tan_h + right, forward * tan_v - up, forward * tan_v + up]
    corners = np.array([[x, y, z] for x in (lo[0], hi[0]) for y in (lo[1], hi[1]) for z in (lo[2], hi[2])]) - eye
    for normal in planes:
        if np.all(corners @ normal < 0.0):
            return False, "outside"
    return True, "frustum"


def read_engine_camera(capture_dir, view):
    path = os.path.join(capture_dir, f"{view}.aov.json")
    with open(path, encoding="utf-8-sig") as f:
        record = json.load(f)
    return record["camera"], int(record["width"]), int(record["height"])


def views_from_metrics(capture_dir):
    with open(os.path.join(capture_dir, "metrics.json"), encoding="utf-8-sig") as f:
        metrics = json.load(f)
    views = metrics.get("views") or []
    if isinstance(views, dict):
        views = [views]
    return [v["view"] for v in views if isinstance(v, dict) and v.get("view")]


def run_depth(args):
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.join(capture_dir, RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    manifest = load_manifest(mesh_dir)
    views = [v for v in args.views.split(",") if v] if args.views else views_from_metrics(capture_dir)
    if not views:
        print("BLENDER_REFERENCE error 視点が無い")
        return 1

    used_tiles = set()
    view_records = {}
    for view in views:
        camera, width, height = read_engine_camera(capture_dir, view)
        tiles = []
        reasons = {}
        for tile in manifest["tiles"]:
            take, reason = tile_in_range(tile, camera, args.range)
            if take:
                tiles.append(tile)
                reasons[reason] = reasons.get(reason, 0) + 1
        triangles = sum(int(t["triangles"]) for t in tiles)
        job = {
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "out_pfm": os.path.join(out_dir, f"{view}.depth.pfm"),
            "out_json": os.path.join(out_dir, f"{view}.json"),
            "exr": os.path.join(out_dir, f"{view}.depth.exr"),
        }
        job_path = os.path.join(out_dir, f"{view}.job.json")
        with open(job_path, "w", encoding="utf-8") as f:
            json.dump(job, f, indent=2, ensure_ascii=False)
        for stale in (job["out_pfm"], job["out_json"]):
            if os.path.exists(stale):
                os.remove(stale)
        print(f"BLENDER_REFERENCE view={view} tiles={len(tiles)} triangles={triangles} range={args.range} "
              f"reasons={json.dumps(reasons, sort_keys=True)}", flush=True)
        started = time.time()
        command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
                   "--python", os.path.abspath(__file__), "--",
                   "--render-job", job_path]
        completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
        with open(os.path.join(out_dir, f"{view}.blender.log"), "w", encoding="utf-8") as f:
            f.write(completed.stdout)
            f.write(completed.stderr)
        if completed.returncode != 0 or not os.path.isfile(job["out_pfm"]) or not os.path.isfile(job["out_json"]):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（終了コード {completed.returncode}。"
                  f"{RESULT_DIR}/{view}.blender.log）")
            return 1
        print(f"BLENDER_REFERENCE view={view} done seconds={time.time() - started:.1f}", flush=True)
        os.remove(job_path)
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles}

    # 入力の記録（ReferenceCheck.py --projection --verify が今のファイルと照らす）
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    record = {
        "kind": "blender_depth",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "mesh_dir": normalize_rel(mesh_dir),
        "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "range": args.range,
        "shadow_range_m": SHADOW_RANGE_M,
        "coordinates": coordinate_record(),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE record views={len(views)} ply={len(used_tiles)} "
          f"terrain_mesh_sha256={record['terrain_mesh_inputs']['files_sha256']}")
    return 0


def normalize_rel(path):
    rel = os.path.relpath(path, REPO_ROOT)
    return rel.replace("\\", "/")


def load_common_light():
    with open(os.path.join(REPO_ROOT, COMMON_LIGHT), encoding="utf-8-sig") as f:
        return json.load(f)


def common_light_sun(light, view):
    """視点の太陽の (仰角, 方位角)[度]（views に無ければ default_sun）"""
    for item in light.get("views", []):
        if item.get("name") == view:
            return float(item["altitude_degrees"]), float(item["azimuth_degrees"])
    return float(light["default_sun"]["altitude_degrees"]), float(light["default_sun"]["azimuth_degrees"])


def engine_sun_direction(altitude_degrees, azimuth_degrees):
    """太陽へ向かう向き（エンジンの座標。SkyAtmosphere.cpp の MakeSunDirection と同じ: 方位角は +X から +Z へ）"""
    altitude = math.radians(altitude_degrees)
    azimuth = math.radians(azimuth_degrees)
    return (math.cos(altitude) * math.cos(azimuth), math.sin(altitude), math.cos(altitude) * math.sin(azimuth))


def reusable_part_a(part_a):
    """切り出しの種 A の途中の和（[npz, json]）が揃い、中身が記録のハッシュと合うか（--crop-reuse-a）"""
    npz_path, json_path = part_a
    if not (os.path.isfile(npz_path) and os.path.isfile(json_path)):
        print("BLENDER_REFERENCE 種 A の途中の和が無いので A から描く", flush=True)
        return False
    with open(json_path, encoding="utf-8") as f:
        record = json.load(f)
    if record.get("seed") != "a" or sha256_file(npz_path) != record.get("part_sha256"):
        print("BLENDER_REFERENCE 種 A の途中の和が記録と合わないので A から描く", flush=True)
        return False
    with np.load(npz_path) as data:
        if "indirect_diffuse" not in data.files:
            print("BLENDER_REFERENCE 種 A の途中の和に間接光の拡散の分が無い（前の版の A）ので A から描く", flush=True)
            return False
    return True


def run_blender_job(args, out_dir, name, job, label="gray"):
    """job を書いて Blender をヘッドレスで 1 回起動する。成否"""
    # 新しい描画はカメラをずらさない（ずらしが 0 でない job は起動する前に断る）
    shift = [float(v) for v in (job.get("camera_shift_px") or [0.0, 0.0])]
    if any(v != 0.0 for v in shift):
        print(f"BLENDER_REFERENCE error job={name} カメラを ({shift[0]:+.3f}, {shift[1]:+.3f}) 画素ずらす描画は断る"
              "（新しい描画はずらし 0）", flush=True)
        return False
    job_path = os.path.join(out_dir, f"{name}.job.json")
    with open(job_path, "w", encoding="utf-8") as f:
        json.dump(job, f, indent=2, ensure_ascii=False)
    started = time.time()
    command = [args.blender, "-b", "--factory-startup", "--python-exit-code", "1",
               "--python", os.path.abspath(__file__), "--", "--render-job", job_path]
    completed = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace")
    with open(os.path.join(out_dir, f"{name}.blender.log"), "w", encoding="utf-8") as f:
        f.write(completed.stdout)
        f.write(completed.stderr)
    for line in completed.stdout.splitlines():
        if line.startswith("BLENDER_GRAY") or line.startswith("BLENDER_ALBEDO") or line.startswith("BLENDER_CLOUD"):
            print(line, flush=True)
    ok = completed.returncode == 0 and all(os.path.isfile(p) for p in job["required_outputs"])
    print(f"BLENDER_REFERENCE {label} job={name} ok={int(ok)} exit={completed.returncode} "
          f"seconds={time.time() - started:.1f}", flush=True)
    if ok:
        os.remove(job_path)
    return ok


def run_gray(args, lit=False):
    """--gray（灰色の材質）と --lit（実材質。lit = True）の Cycles の描画"""
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    # 切り出しの描画は <撮影>/LIT_CROP_DIR/ の下に材質と Cycles を書く（カメラ・深度・撮影の記録は <撮影> から読む）
    crop_regions = [r for r in (getattr(args, "crop_regions", "") or "").split(",") if r]
    result_root = os.path.join(capture_dir, LIT_CROP_DIR) if crop_regions else capture_dir
    out_dir = os.path.join(result_root, GRAY_RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    light = load_common_light()
    gray = light["gray_material"]
    plate = light["gray_plate"]
    lighting = {
        "sky_radiance_rgb": [float(v) for v in light["sky_radiance_rgb"]],
        "sun_illuminance_normal_lux": float(light["sun_illuminance_normal_lux"]),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
    }
    views = [v for v in args.views.split(",") if v]

    # 実材質: 画素ごとの材質（カメラの光線の当たり）と、それを描いたときのカメラの小数のずらし・OSL
    lit_material = None
    if lit:
        if not args.skip_material and run_material(args, "lit", result_root) != 0:
            return 1
        if args.material_only:
            return 0
        material_dir = os.path.join(result_root, LIT_MATERIAL_DIR)
        record_path = os.path.join(material_dir, "inputs.json")
        if not os.path.isfile(record_path):
            print(f"BLENDER_REFERENCE error {LIT_MATERIAL_DIR}/inputs.json が無い（--skip-material の前に材質を描いていない）")
            return 1
        with open(record_path, encoding="utf-8") as f:
            lit_material = json.load(f)
        missing = [v for v in views if v not in lit_material.get("views", {})]
        if missing:
            print(f"BLENDER_REFERENCE error 画素ごとの材質に視点 {missing} が無い")
            return 1
        # 材質はカメラの光線の当たりなので、Cycles も材質と同じカメラで描く。ずらしありで描いた旧材質は使わない
        shifted = {v: lit_material["views"][v].get("camera_shift_px") for v in views
                   if any(float(s) != 0.0 for s in (lit_material["views"][v].get("camera_shift_px") or [0.0, 0.0]))}
        if shifted:
            print(f"BLENDER_REFERENCE error 画素ごとの材質がカメラをずらして描いた旧材質 {shifted}"
                  "（--skip-material を外して材質から描き直す）")
            return 1

    plate_records = {}
    if not args.skip_plate and not lit:
        altitude, azimuth = common_light_sun(light, PLATE_VIEW)
        for case in PLATE_CASES:
            name = f"plate_{case}"
            job = {
                "kind": "plate",
                "case": case,
                "lighting": lighting,
                "sun_altitude_degrees": altitude,
                "sun_azimuth_degrees": azimuth,
                "to_sun_engine": engine_sun_direction(altitude, azimuth),
                "plate": plate,
                "width": PLATE_WIDTH,
                "height": PLATE_HEIGHT,
                "vertical_fov_degrees": PLATE_VERTICAL_FOV_DEGREES,
                "samples": PLATE_SPP[case],
                "out_exr": os.path.join(out_dir, f"{name}.exr"),
                "out_pfm": os.path.join(out_dir, f"{name}.pfm"),
                "out_json": os.path.join(out_dir, f"{name}.json"),
            }
            job["required_outputs"] = [job["out_exr"], job["out_pfm"], job["out_json"]]
            if not run_blender_job(args, out_dir, name, job):
                print(f"BLENDER_REFERENCE error 板の較正 {case} が失敗した（{GRAY_RESULT_DIR}/{name}.blender.log）")
                return 1
            plate_records[case] = {"view": PLATE_VIEW, "samples": PLATE_SPP[case]}

    manifest = load_manifest(mesh_dir) if not args.plate_only else None
    used_tiles = set()
    view_records = {}
    for view in ([] if args.plate_only else views):
        camera, width, height = read_engine_camera(capture_dir, view)
        tiles = [t for t in manifest["tiles"] if tile_in_range(t, camera, args.range)[0]]
        altitude, azimuth = common_light_sun(light, view)
        prefix = os.path.join(out_dir, f"{view}")
        job = {
            "kind": "gray",
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "engine_depth_pfm": os.path.join(capture_dir, f"{view}.aov.depth.pfm"),
            "lighting": lighting,
            "sun_altitude_degrees": altitude,
            "sun_azimuth_degrees": azimuth,
            "to_sun_engine": engine_sun_direction(altitude, azimuth),
            "gray_material": gray,
            "chunk_spp": args.chunk_spp,
            "max_spp_per_seed": args.max_spp,
            "max_seconds": args.max_seconds,
            "prefix": prefix,
        }
        if lit:
            material_view = lit_material["views"][view]
            job.update({
                "material_mode": "lit",
                "camera_shift_px": material_view["camera_shift_px"],
                "screen_material": {key: os.path.join(result_root, LIT_MATERIAL_DIR, f"{view}.{key}.pfm")
                                    for key in ("albedo", "shaded_normal", "roughness")},
                "osl": os.path.join(REPO_ROOT, lit_material["material"]["osl"]),
            })
        if crop_regions:
            job["crop"] = {"regions": crop_regions, "pad_px": CROP_PAD_PX, "max_total_seconds": CROP_MAX_TOTAL_SECONDS,
                           "direct_seed_offset": CROP_DIRECT_SEED_OFFSET}
            job["max_seconds"] = CROP_MAX_TOTAL_SECONDS
        parts = ("hdr", "seed_a", "seed_b", "sun", "sky", "direct", "indirect", "indirect_diffuse") + \
            (("sun_direct", "sky_direct") if crop_regions else ())
        aux_outputs = [f"{prefix}.aux.exr"] + \
            [f"{prefix}.aux.{part}.pfm" for part in ("visibility", "n_dot_l", "normal", "alpha")]
        job["required_outputs"] = [f"{prefix}.cycles.exr", f"{prefix}.cycles.json"] + \
            [f"{prefix}.cycles.{part}.pfm" for part in parts] + aux_outputs
        part_a = [f"{prefix}.cycles.part_a.npz", f"{prefix}.cycles.part_a.json"]
        reuse_a = bool(crop_regions and args.crop_reuse_a and reusable_part_a(part_a))
        for stale in job["required_outputs"] + (part_a if crop_regions and not reuse_a else []):
            if os.path.exists(stale):
                os.remove(stale)
        triangles = sum(int(t["triangles"]) for t in tiles)
        label = "lit" if lit else "gray"
        print(f"BLENDER_REFERENCE {label} view={view} tiles={len(tiles)} triangles={triangles} range={args.range}",
              flush=True)
        if crop_regions:
            # 切り出しは種を 1 本ずつ別の起動で描く（1 本 CROP_MAX_TOTAL_SECONDS）: A を時間の上限まで、B を A と同じサンプル数まで
            job_a = dict(job, crop=dict(job["crop"], seed="a"), required_outputs=aux_outputs + part_a)
            if reuse_a:
                print(f"BLENDER_REFERENCE {label} view={view} 種 A の途中の和を使い直す（{os.path.basename(part_a[1])}）",
                      flush=True)
            elif not run_blender_job(args, out_dir, f"{view}.seed_a", job_a, label=label):
                print(f"BLENDER_REFERENCE error view={view} 種 A の Blender が失敗した（{GRAY_RESULT_DIR}/{view}.seed_a.blender.log）")
                return 1
            with open(part_a[1], encoding="utf-8") as f:
                spp_a = int(json.load(f)["spp"])
            job = dict(job, crop=dict(job["crop"], seed="b", target_spp=spp_a, merge_from=part_a[1]), max_spp_per_seed=spp_a)
        if not run_blender_job(args, out_dir, view, job, label=label):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（{GRAY_RESULT_DIR}/{view}.blender.log）")
            return 1
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles,
                              "sun_altitude_degrees": altitude, "sun_azimuth_degrees": azimuth}
        if crop_regions:
            view_records[view]["crop_regions"] = crop_regions
        if lit:
            view_records[view]["camera_shift_px"] = job["camera_shift_px"]
            view_records[view]["screen_material_sha256"] = {key: sha256_file(path)
                                                            for key, path in job["screen_material"].items()}

    # 入力の記録（ReferenceCheck.py --gray --verify が今のファイルと照らす）。視点や板を描き直さなかった分は前の記録を残す
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    record_path = os.path.join(out_dir, "inputs.json")
    # エンジンの撮影はカメラと深度を借りるだけなので、撮影の入力のハッシュ（撮り直しても同じコードなら同じ）で結び付ける
    with open(os.path.join(capture_dir, "inputs.json"), encoding="utf-8") as f:
        engine_inputs = json.load(f)
    current = {
        "kind": "cycles_lit" if lit else "cycles_gray",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "gray_regions_sha256": sha256_file(os.path.join(REPO_ROOT, GRAY_REGIONS)),
        "common_light_sha256": sha256_file(os.path.join(REPO_ROOT, COMMON_LIGHT)),
        "engine_capture_files_sha256": engine_inputs.get("files_sha256"),
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
    }
    if lit:
        # 当たりの材質の OSL（カメラの光線の当たりの材質は視点ごとの screen_material_sha256 で結び付ける）
        current["material_osl"] = lit_material["material"]["osl"]
        current["material_osl_sha256"] = lit_material["material"]["osl_sha256"]
        current["bounce_pass"] = PASS_BOUNCE
    # 台本・領域・照明・撮影・OSL が前の記録と同じときだけ、描き直さなかった視点と板の記録を残す
    record = load_previous_record(record_path, current, ("kind", "script_sha256", "gray_regions_sha256",
                                                        "common_light_sha256", "engine_capture_files_sha256",
                                                        "material_osl_sha256")) or {}
    previous_ply = record.get("ply_sha256", {})
    record.update(current)
    if plate_records:
        record.setdefault("plates", {}).update(plate_records)
    if view_records:
        previous_ply.update({name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)})
        record.update({
            "mesh_dir": normalize_rel(mesh_dir),
            "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
            "ply_sha256": previous_ply,
            "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
            "range": args.range,
            "shadow_range_m": SHADOW_RANGE_M,
            "coordinates": coordinate_record(),
        })
        record.setdefault("views", {}).update(view_records)
    with open(record_path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE gray record plates={len(record.get('plates', {}))} views={len(record.get('views', {}))}")
    return 0


def run_building(args):
    """--building: 前哨地の建物 1 棟と灰色の地形を、エンジンの撮影と同じカメラ・共通の照明で描く（SCN-WLR-BUILDING-REF-BASE）"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    BR = load_building_regions()
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.abspath(args.out) if args.out else \
        os.path.join(capture_dir, BUILDING_TRIAL_DIR if args.trial else BUILDING_RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    light = load_common_light()
    lighting = {
        "sky_radiance_rgb": [float(v) for v in light["sky_radiance_rgb"]],
        "sun_illuminance_normal_lux": float(light["sun_illuminance_normal_lux"]),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
    }
    views = [v for v in args.views.split(",") if v]
    manifest = load_manifest(mesh_dir)
    used_tiles = set()
    view_records = {}
    for view in views:
        # 建物の配置はエンジンの撮影のログ（WASTELAND_BUILDING_PLACEMENT の reference=1）から読む
        log_name = RC.view_game_log(capture_dir, view) or f"{view}.Game.log"
        with open(os.path.join(capture_dir, log_name), encoding="utf-8", errors="replace") as f:
            placement, count = BR.parse_placement(f.read())
        if placement is None or count != 1:
            print(f"BLENDER_REFERENCE error view={view} 撮影のログに比べる建物の配置（WASTELAND_BUILDING_PLACEMENT reference=1）が "
                  f"1 行でない（{count} 行。--wasteland-hide=all-but-building で撮る）")
            return 1
        gltf_path = os.path.join(REPO_ROOT, placement["gltf"])
        bounds = BR.gltf_bounds(gltf_path)
        camera, width, height = read_engine_camera(capture_dir, view)
        tiles = [t for t in manifest["tiles"] if tile_in_range(t, camera, args.range)[0]]
        altitude, azimuth = common_light_sun(light, view)
        prefix = os.path.join(out_dir, view)
        job = {
            "kind": "gray",
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "lighting": lighting,
            "sun_altitude_degrees": altitude,
            "sun_azimuth_degrees": azimuth,
            "to_sun_engine": engine_sun_direction(altitude, azimuth),
            "gray_material": light["gray_material"],
            "chunk_spp": args.chunk_spp,
            "max_spp_per_seed": args.max_spp,
            "max_seconds": args.max_seconds,
            "prefix": prefix,
            "building": {"gltf": placement["gltf"], "gltf_path": gltf_path, "placement": placement, "bounds": bounds},
        }
        parts = ("hdr", "seed_a", "seed_b", "sun", "sky", "direct", "indirect", "indirect_diffuse", "diffuse_color")
        job["required_outputs"] = [f"{prefix}.cycles.exr", f"{prefix}.cycles.json", f"{prefix}.aux.exr"] + \
            [f"{prefix}.cycles.{part}.pfm" for part in parts] + \
            [f"{prefix}.aux.{part}.pfm" for part in ("visibility", "n_dot_l", "normal", "alpha", "depth", "building")]
        for stale in job["required_outputs"]:
            if os.path.exists(stale):
                os.remove(stale)
        triangles = sum(int(t["triangles"]) for t in tiles)
        print(f"BLENDER_REFERENCE building view={view} id={placement['id']} tiles={len(tiles)} triangles={triangles} "
              f"range={args.range} max_spp={args.max_spp}", flush=True)
        if not run_blender_job(args, out_dir, view, job, label="building"):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（{os.path.basename(out_dir)}/{view}.blender.log）")
            return 1
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height, "placement": placement,
                              "bounds": bounds, "tiles": sorted(t["file"] for t in tiles), "triangles": triangles,
                              "sun_altitude_degrees": altitude, "sun_azimuth_degrees": azimuth,
                              "max_spp_per_seed": args.max_spp, "trial": bool(args.trial)}

    # 参照の入力の記録（ReferenceCheck.py --building --verify が今のファイルと照らす）。エンジンの実装には結び付けない
    # （カメラと建物の配置は記録の値をエンジンの撮影と照らす）
    record = {
        "kind": "cycles_building",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "building_regions_sha256": sha256_file(os.path.join(REPO_ROOT, BUILDING_REGIONS)),
        "gray_regions_sha256": sha256_file(os.path.join(REPO_ROOT, GRAY_REGIONS)),
        "common_light_sha256": sha256_file(os.path.join(REPO_ROOT, COMMON_LIGHT)),
        "gltf_sha256": {view_records[v]["placement"]["gltf"]: sha256_file(os.path.join(REPO_ROOT, view_records[v]["placement"]["gltf"]))
                        for v in view_records},
        "terrain_material": "gray_material（CommonLight.json）",
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
        "mesh_dir": normalize_rel(mesh_dir),
        "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "range": args.range,
        "shadow_range_m": SHADOW_RANGE_M,
        "coordinates": coordinate_record(),
        "trial": bool(args.trial),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE building record views={len(view_records)} out={normalize_rel(out_dir)}")
    return 0


def run_plant(args):
    """--plant: 低木 1 株と灰色の地形を、エンジンの撮影と同じカメラ・共通の照明で描く（SCN-WLR-PLANT-REF-BASE）"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    PR = load_plant_regions()
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.abspath(args.out) if args.out else \
        os.path.join(capture_dir, PLANT_TRIAL_DIR if args.trial else PLANT_RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    light = load_common_light()
    lighting = {
        "sky_radiance_rgb": [float(v) for v in light["sky_radiance_rgb"]],
        "sun_illuminance_normal_lux": float(light["sun_illuminance_normal_lux"]),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
    }
    views = [v for v in args.views.split(",") if v]
    manifest = load_manifest(mesh_dir)
    used_tiles = set()
    view_records = {}
    ply_path = os.path.join(capture_dir, PLANT_PLY)
    for view in views:
        # 株の配置はエンジンの撮影のログ（WASTELAND_PLANT_PLACEMENT の reference=1）から、形は撮影のフォルダの PLY から読む
        log_name = RC.view_game_log(capture_dir, view) or f"{view}.Game.log"
        with open(os.path.join(capture_dir, log_name), encoding="utf-8", errors="replace") as f:
            placement, count = PR.parse_placement(f.read())
        if placement is None or count != 1 or placement["ply"] != PLANT_PLY:
            print(f"BLENDER_REFERENCE error view={view} 撮影のログに比べる株の配置（WASTELAND_PLANT_PLACEMENT reference=1、"
                  f"ply={PLANT_PLY}）が 1 行でない（{count} 行。開発ビルドの --wasteland-hide=all-but-plant で撮る）")
            return 1
        if not os.path.isfile(ply_path):
            print(f"BLENDER_REFERENCE error view={view} 株のメッシュ {PLANT_PLY} が撮影のフォルダに無い")
            return 1
        camera, width, height = read_engine_camera(capture_dir, view)
        with open(os.path.join(capture_dir, f"{view}.aov.json"), encoding="utf-8-sig") as f:
            engine_depth_pfm = os.path.join(capture_dir, json.load(f)["depth"]["file"])
        tiles = [t for t in manifest["tiles"] if tile_in_range(t, camera, args.range)[0]]
        altitude, azimuth = common_light_sun(light, view)
        prefix = os.path.join(out_dir, view)
        job = {
            "kind": "gray",
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "lighting": lighting,
            "sun_altitude_degrees": altitude,
            "sun_azimuth_degrees": azimuth,
            "to_sun_engine": engine_sun_direction(altitude, azimuth),
            "gray_material": light["gray_material"],
            "chunk_spp": args.chunk_spp,
            "max_spp_per_seed": args.max_spp,
            "max_seconds": args.max_seconds,
            "prefix": prefix,
            "engine_depth_pfm": engine_depth_pfm,
            "plant": {"ply_path": ply_path, "placement": placement},
        }
        parts = ("hdr", "seed_a", "seed_b", "sun", "sky", "direct", "indirect", "indirect_diffuse")
        job["required_outputs"] = [f"{prefix}.cycles.exr", f"{prefix}.cycles.json", f"{prefix}.aux.exr"] + \
            [f"{prefix}.cycles.{part}.pfm" for part in parts] + \
            [f"{prefix}.aux.{part}.pfm" for part in ("visibility", "n_dot_l", "normal", "alpha", "depth", "plant",
                                                      "back_visibility")]
        for stale in job["required_outputs"]:
            if os.path.exists(stale):
                os.remove(stale)
        triangles = sum(int(t["triangles"]) for t in tiles)
        print(f"BLENDER_REFERENCE plant view={view} id={placement['id']} tiles={len(tiles)} triangles={triangles} "
              f"plant_triangles={placement['triangles']} range={args.range} max_spp={args.max_spp}", flush=True)
        if not run_blender_job(args, out_dir, view, job, label="plant"):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（{os.path.basename(out_dir)}/{view}.blender.log）")
            return 1
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height, "placement": placement,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles,
                              "sun_altitude_degrees": altitude, "sun_azimuth_degrees": azimuth,
                              "engine_depth_sha256": sha256_file(engine_depth_pfm),
                              "max_spp_per_seed": args.max_spp, "trial": bool(args.trial)}

    # 参照の入力の記録（ReferenceCheck.py --plant --verify が今のファイルと照らす）。株の PLY はエンジンの撮影の出力なので、
    # 照合はエンジンの撮影のフォルダの PLY のハッシュと比べる
    record = {
        "kind": "cycles_plant",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "plant_regions_sha256": sha256_file(os.path.join(REPO_ROOT, PLANT_REGIONS)),
        "gray_regions_sha256": sha256_file(os.path.join(REPO_ROOT, GRAY_REGIONS)),
        "common_light_sha256": sha256_file(os.path.join(REPO_ROOT, COMMON_LIGHT)),
        "plant_ply_sha256": sha256_file(ply_path),
        "leaf_transmittance_ratio": PLANT_LEAF_TRANSMITTANCE_RATIO,
        "terrain_material": "gray_material（CommonLight.json）",
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
        "mesh_dir": normalize_rel(mesh_dir),
        "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "range": args.range,
        "shadow_range_m": SHADOW_RANGE_M,
        "coordinates": coordinate_record(),
        "trial": bool(args.trial),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE plant record views={len(view_records)} out={normalize_rel(out_dir)}")
    return 0


def load_cloud_volume():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import CloudVolume  # noqa: E402  Blender の中でも numpy だけで動く
    return CloudVolume


def run_cloud(args):
    """--cloud: 雲の書き出しの密度場を Blender の体積として、書き出しの太陽・空とカメラで描く（SCN-WLR-CLOUD-REF-BLENDER）"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    CV = load_cloud_volume()
    capture_dir = os.path.abspath(args.capture)
    out_dir = os.path.abspath(args.out) if args.out else \
        os.path.join(capture_dir, CLOUD_TRIAL_DIR if args.trial else CLOUD_RESULT_DIR)
    os.makedirs(out_dir, exist_ok=True)
    views = [v for v in args.views.split(",") if v]
    view_records = {}
    dump_sha256 = {}
    for view in views:
        try:
            meta, model, probe = CV.load_dump(capture_dir, view)
            target = CV.target_size(meta)
            frame = CV.camera_frame(model, target)
            grid = CV.grid_spec(model, target, frame)
        except ValueError as error:
            print(f"BLENDER_REFERENCE error view={view} 雲の書き出しを読めない（{error}）")
            return 1
        pixels = meta["probe"]["pixels"]
        members = CV.region_members(CV.ray_table(model, probe, pixels, target))
        for name in CV.dump_files(meta, view):
            dump_sha256[name] = sha256_file(os.path.join(capture_dir, name))
        lighting = {
            "sun_direction": model.sun_direction.tolist(), "sun_illuminance_lux": model.sun_illuminance.tolist(),
            "sun_angle_degrees": SUN_ANGLE_DEGREES, "ground_albedo": model.ground_albedo.tolist(),
            "extinction_per_m": model.extinction,
            "phase": meta["lighting"]["phase"],
        }
        prefix = os.path.join(out_dir, view)
        job = {
            "kind": "cloud",
            "view": view,
            "capture_dir": capture_dir,
            "width": target[0],
            "height": target[1],
            "camera": frame,
            "grid": grid,
            "pixels": pixels,
            "regions": members,
            "lighting": lighting,
            "chunk_spp": args.chunk_spp,
            "max_spp_per_seed": args.max_spp,
            "max_seconds": args.max_seconds,
            "trial": bool(args.trial),
            "prefix": prefix,
        }
        job["required_outputs"] = [f"{prefix}.cycles.json", f"{prefix}.aux.sky.pfm", f"{prefix}.aux.transmittance.pfm"] + \
            [f"{prefix}.cycles.{part}.pfm" for part in ("hdr", "seed_a", "seed_b", "alpha", "alpha_a", "alpha_b")]
        for stale in job["required_outputs"]:
            if os.path.exists(stale):
                os.remove(stale)
        print(f"BLENDER_REFERENCE cloud view={view} grid={grid['dims']} voxel={grid['voxel_size_m']} "
              f"regions={ {k: len(v) for k, v in members.items()} } max_spp={args.max_spp}", flush=True)
        if not run_blender_job(args, out_dir, view, job, label="cloud"):
            print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（{os.path.basename(out_dir)}/{view}.blender.log）")
            return 1
        view_records[view] = {"camera": frame, "grid": grid, "width": target[0], "height": target[1],
                              "regions": members, "lighting": lighting, "max_spp_per_seed": args.max_spp,
                              "trial": bool(args.trial)}

    # 参照の入力の記録（ReferenceCheck.py --cloud が書き出しのハッシュを照らし、--verify で台本を今のファイルと照らす）
    record = {
        "kind": "cycles_cloud",
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "cloud_volume_sha256": sha256_file(os.path.join(REPO_ROOT, CLOUD_VOLUME)),
        "cloud_dump_check_sha256": sha256_file(os.path.join(REPO_ROOT, CLOUD_DUMP_CHECK)),
        "gray_regions_sha256": sha256_file(os.path.join(REPO_ROOT, GRAY_REGIONS)),
        "engine_dir": normalize_rel(capture_dir),
        "dump_sha256": dump_sha256,
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "sun_angle_degrees": SUN_ANGLE_DEGREES,
        "volume_bounces": CLOUD_VOLUME_BOUNCES,
        "diffuse_bounces": CLOUD_DIFFUSE_BOUNCES,
        "ground_half_size_m": CLOUD_GROUND_HALF_SIZE_M,
        "criteria": CV.criteria(),
        "trial": bool(args.trial),
        "views": view_records,
    }
    with open(os.path.join(out_dir, "inputs.json"), "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE cloud record views={len(view_records)} out={normalize_rel(out_dir)}")
    return 0


def run_material(args, mode, result_root=None):
    """--albedo（mode = albedo）・--normal（mode = normal）・--lit の画素ごとの材質（mode = lit）。result_root を渡すと
    <撮影> の代わりにその下へ書く（切り出しの描画）"""
    mesh_dir = os.path.abspath(args.mesh)
    capture_dir = os.path.abspath(args.capture)
    result_dir = MATERIAL_RESULT_DIRS[mode]
    out_dir = os.path.join(result_root or capture_dir, result_dir)
    mips_dir = os.path.abspath(args.mips) if args.mips else os.path.join(out_dir, "mips")
    os.makedirs(out_dir, exist_ok=True)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402  ふつうの Python の側だけで使う
    import TerrainMaterialReference as TM  # noqa: E402
    views = [v for v in args.views.split(",") if v]

    # 値の表（エンジンの撮影のログの WASTELAND_TERRAIN_MATERIAL）・ミップの段・OSL
    logs = [os.path.join(capture_dir, RC.view_game_log(capture_dir, view) or f"{view}.Game.log") for view in views]
    json_text, material, failures = TM.material_from_logs(logs)
    if failures:
        print(f"BLENDER_REFERENCE error 材質の値を撮影のログから読めない（{'; '.join(failures)}）")
        return 1
    material_record = TM.write_material_table(out_dir, json_text, material, mips_dir,
                                              log=lambda line: print(line, flush=True))

    manifest = load_manifest(mesh_dir)
    used_tiles = set()
    view_records = {}
    for view in views:
        camera, width, height = read_engine_camera(capture_dir, view)
        tiles = [t for t in manifest["tiles"] if tile_in_range(t, camera, "frustum")[0]]
        prefix = os.path.join(out_dir, view)
        job = {
            "kind": "albedo",
            "mode": mode,
            "passes": MATERIAL_PASSES[mode],
            "view": view,
            "camera": camera,
            "width": width,
            "height": height,
            "tiles": [os.path.join(mesh_dir, t["file"]) for t in tiles],
            "osl": material_record["osl"],
            "exr": f"{prefix}.{mode}.exr",
            "prefix": prefix,
        }
        parts = [p[1] for p in MATERIAL_PASSES[mode]] + ["normal", "alpha", "depth"]
        final_outputs = [f"{prefix}.{mode}.json"] + [f"{prefix}.{part}.pfm" for part in parts]
        for stale in final_outputs:
            if os.path.exists(stale):
                os.remove(stale)
        triangles = sum(int(t["triangles"]) for t in tiles)
        column_count = MATERIAL_COLUMNS if triangles > MATERIAL_COLUMN_TRIANGLES else 1
        edges = [round(width * i / column_count) for i in range(column_count + 1)]
        print(f"BLENDER_REFERENCE {mode} view={view} tiles={len(tiles)} triangles={triangles} columns={column_count}",
              flush=True)
        with open(os.path.join(capture_dir, f"{view}.aov.json"), encoding="utf-8") as f:
            engine_depth_file = json.load(f)["depth"]["file"]
        engine_depth = RC.read_pfm_any(os.path.join(capture_dir, engine_depth_file))[..., 0].astype(np.float64)
        # 撮影の記録のカメラのまま（ずらし 0）で 1 回描く。深度から求めた小数のずれは記録するだけで、カメラを動かして描き直さない
        # （残りのずれの合否は ReferenceCheck.py の照合が決める）
        shift = [0.0, 0.0]
        column_prefixes = []
        for index in range(column_count):
            column_job = dict(job)
            column_job["camera_shift_px"] = shift
            name = view if column_count == 1 else f"{view}.col{index}"
            column_job["prefix"] = os.path.join(out_dir, name)
            column_job["exr"] = f"{column_job['prefix']}.{mode}.exr"
            if column_count > 1:
                column_job["columns"] = [edges[index], edges[index + 1]]
            column_job["required_outputs"] = [f"{column_job['prefix']}.{mode}.json"] + \
                [f"{column_job['prefix']}.{part}.pfm" for part in parts]
            if not run_blender_job(args, out_dir, name, column_job, label=mode):
                print(f"BLENDER_REFERENCE error view={view} Blender が失敗した（{result_dir}/{name}.blender.log）")
                return 1
            column_prefixes.append(column_job["prefix"])
        if column_count > 1:
            join_material_columns(prefix, column_prefixes, edges, mode, parts)
        blender_depth = RC.read_pfm_any(f"{prefix}.depth.pfm")[..., 0].astype(np.float64)
        fit = RC.depth_subpixel_shift(engine_depth, blender_depth)
        fit["camera_shift_px"] = list(shift)
        fits = [fit]
        print(f"BLENDER_REFERENCE {mode} view={view} subpixel right_px={fit['right_px']:+.3f} "
              f"down_px={fit['down_px']:+.3f} depth_bias={fit['depth_bias']:+.2e} pixels={fit['pixels']}", flush=True)
        used_tiles.update(t["file"] for t in tiles)
        view_records[view] = {"camera": camera, "width": width, "height": height,
                              "tiles": sorted(t["file"] for t in tiles), "triangles": triangles,
                              "camera_shift_px": shift, "subpixel_fits": fits}

    # 入力の記録（ReferenceCheck.py --albedo・--normal の --verify が今のファイルと照らす）
    with open(os.path.join(capture_dir, "inputs.json"), encoding="utf-8") as f:
        engine_inputs = json.load(f)
    record = {
        "kind": f"blender_{mode}",
        "passes": [p[1] for p in MATERIAL_PASSES[mode]],
        "script": normalize_rel(os.path.abspath(__file__)),
        "script_sha256": sha256_file(os.path.abspath(__file__)),
        "material_script_sha256": sha256_file(os.path.join(REPO_ROOT, MATERIAL_SCRIPT)),
        "engine_capture_files_sha256": engine_inputs.get("files_sha256"),
        "engine_game_exe_sha256": engine_inputs.get("game_exe_sha256"),
        "material": {key: (normalize_rel(value) if key in ("table", "osl") else value)
                     for key, value in material_record.items()},
        "mips_dir": normalize_rel(mips_dir),
        "git_commit": (RC.git_output(["rev-parse", "HEAD"]) or "").strip(),
        "mesh_dir": normalize_rel(mesh_dir),
        "manifest_sha256": sha256_file(os.path.join(mesh_dir, "manifest.json")),
        "ply_sha256": {name: sha256_file(os.path.join(mesh_dir, name)) for name in sorted(used_tiles)},
        "terrain_mesh_inputs": RC.hash_inputs("terrain_mesh"),
        "range": "frustum",
        "coordinates": coordinate_record(),
        "views": view_records,
    }
    # 視点を分けて描いたときは、同じ台本・同じ値の表・同じ撮影で描いた前の視点の記録を残す（違えば捨てる）
    record_path = os.path.join(out_dir, "inputs.json")
    previous = load_previous_record(record_path, record, ("kind", "script_sha256", "material_script_sha256",
                                                          "engine_capture_files_sha256", "manifest_sha256",
                                                          "mips_dir", "material"))
    if previous:
        record["ply_sha256"] = {**previous.get("ply_sha256", {}), **record["ply_sha256"]}
        record["views"] = {**previous.get("views", {}), **view_records}
    with open(record_path, "w", encoding="utf-8") as f:
        json.dump(record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE {mode} record views={len(view_records)} ply={len(used_tiles)} "
          f"osl_sha256={material_record['osl_sha256'][:12]}")
    return 0


def join_material_columns(prefix, column_prefixes, edges, mode, parts):
    """列ごとに描いた PFM と記録を 1 枚へつなぎ、列の出力を消す"""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import ReferenceCheck as RC  # noqa: E402
    for part in parts:
        joined = None
        for index, column_prefix in enumerate(column_prefixes):
            image = RC.read_pfm_any(f"{column_prefix}.{part}.pfm")
            if joined is None:
                joined = np.zeros_like(image)
            joined[:, edges[index]:edges[index + 1]] = image[:, edges[index]:edges[index + 1]]
        if joined.shape[-1] == 1:
            write_pfm(f"{prefix}.{part}.pfm", joined[..., 0])
        else:
            write_pfm_rgb(f"{prefix}.{part}.pfm", joined)
    metas = []
    for column_prefix in column_prefixes:
        with open(f"{column_prefix}.{mode}.json", encoding="utf-8") as f:
            metas.append(json.load(f))
    view = metas[0]["view"]
    # 記録の中のファイルの名前は、列でも視点の名前で書かれている（つないだ後の名前と同じ）
    meta = dict(metas[0])
    for key in ("vertices", "triangles", "flipped_triangles", "load_seconds", "render_seconds"):
        meta[key] = sum(m[key] for m in metas)
    meta["surface_pixels"] = int(np.count_nonzero(RC.read_pfm_any(f"{prefix}.alpha.pfm")[..., 0] > 0.5))
    meta["columns"] = [{"pixels": [edges[i], edges[i + 1]], "triangles": m["triangles"],
                        "render_seconds": m["render_seconds"]} for i, m in enumerate(metas)]
    meta.pop("column_pixels", None)
    with open(f"{prefix}.{mode}.json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
    for column_prefix in column_prefixes:
        for path in [f"{column_prefix}.{mode}.json"] + [f"{column_prefix}.{part}.pfm" for part in parts]:
            os.remove(path)
    print(f"BLENDER_REFERENCE {mode} view={view} joined columns={len(column_prefixes)} triangles={meta['triangles']}",
          flush=True)


# ---- Blender の中の側（1 視点を描く） ----

def write_pfm(path, image):
    """1 チャンネルの float32 を PFM（下の行から並ぶ、little endian）で書く。image は上の行が 0 行目"""
    height, width = image.shape
    with open(path, "wb") as f:
        f.write(f"Pf\n{width} {height}\n-1.0\n".encode("ascii"))
        f.write(np.ascontiguousarray(image[::-1], dtype="<f4").tobytes())


def write_pfm_rgb(path, image):
    """3 チャンネルの float32 を PFM（"PF"、下の行から並ぶ、little endian）で書く。image は (高さ, 幅, 3) で上の行が 0 行目"""
    height, width, _ = image.shape
    with open(path, "wb") as f:
        f.write(f"PF\n{width} {height}\n-1.0\n".encode("ascii"))
        f.write(np.ascontiguousarray(image[::-1], dtype="<f4").tobytes())


def frustum_triangles(positions, triangles, camera, margin, ndc_x=None):
    """3 つの頂点がそろって同じ視錐台の面（近い面・左右・上下。画角に margin の余裕）の外にある三角形を除いた三角形。
    ndc_x = (左, 右) なら、左右の面を画面の横の位置（−1〜1）のその範囲に狭める"""
    eye = np.asarray(camera["position"], dtype=np.float64)
    forward = np.asarray(camera["forward"], dtype=np.float64)
    up = np.asarray(camera["up"], dtype=np.float64)
    right = np.asarray(camera["right"], dtype=np.float64)
    tan_v = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0) * margin
    tan_h = tan_v * float(camera["aspect_ratio"])
    planes = np.array([forward, forward * tan_h - right, forward * tan_h + right, forward * tan_v - up,
                       forward * tan_v + up], dtype=np.float64)
    if ndc_x is not None:
        tan_h_exact = math.tan(math.radians(float(camera["vertical_fov_degrees"])) / 2.0) * float(camera["aspect_ratio"])
        planes[1] = forward * (ndc_x[1] * tan_h_exact) - right
        planes[2] = right - forward * (ndc_x[0] * tan_h_exact)
    near = float(camera.get("near", 0.1))
    offsets = np.array([near, 0.0, 0.0, 0.0, 0.0])
    inside = (positions.astype(np.float64) - eye) @ planes.T - offsets >= 0.0
    outside_all = ~inside[triangles[:, 0]] & ~inside[triangles[:, 1]] & ~inside[triangles[:, 2]]
    return triangles[~np.any(outside_all, axis=1)]


PLY_VERTEX_PROPERTIES = ["property float x", "property float y", "property float z",
                         "property float nx", "property float ny", "property float nz"]
PLY_FACE_PROPERTY = "property list uchar int vertex_indices"


def read_terrain_ply(path, with_normals=False):
    """Game の WastelandTerrainExport が書いた PLY（binary_little_endian、頂点は位置と法線の float 6 つ、面は 3 角の int）を読む。
    (位置 (頂点数, 3) float32, 三角形の頂点の番号 (三角形数, 3) int32)。with_normals なら頂点の法線 (頂点数, 3) も後ろに足す。
    並びが違えば例外"""
    with open(path, "rb") as f:
        data = f.read()
    marker = b"end_header\n"
    end = data.find(marker)
    if end < 0:
        raise ValueError(f"PLY の見出しが無い: {path}")
    header = data[:end].decode("ascii").splitlines()
    body = end + len(marker)
    if header[:2] != ["ply", "format binary_little_endian 1.0"]:
        raise ValueError(f"PLY の形式が違う: {path}")
    lines = [line for line in header[2:] if not line.startswith("comment")]
    if not (len(lines) == 9 and lines[0].startswith("element vertex ") and lines[1:7] == PLY_VERTEX_PROPERTIES and
            lines[7].startswith("element face ") and lines[8] == PLY_FACE_PROPERTY):
        raise ValueError(f"PLY の要素の並びが書き出しと違う: {path}")
    vertex_count = int(lines[0].split()[2])
    face_count = int(lines[7].split()[2])
    if len(data) - body != vertex_count * 24 + face_count * 13:
        raise ValueError(f"PLY の本体の大きさが見出しと合わない: {path}")
    vertices = np.frombuffer(data, dtype="<f4", count=vertex_count * 6, offset=body).reshape(vertex_count, 6)
    faces = np.frombuffer(data, dtype=np.dtype([("n", "u1"), ("i", "<i4", (3,))]), count=face_count,
                          offset=body + vertex_count * 24)
    if np.any(faces["n"] != 3):
        raise ValueError(f"三角形でない面がある: {path}")
    indices = np.ascontiguousarray(faces["i"], dtype=np.int32)
    if indices.size and (indices.min() < 0 or indices.max() >= vertex_count):
        raise ValueError(f"面の頂点の番号が範囲の外: {path}")
    if with_normals:
        return (np.ascontiguousarray(vertices[:, :3], dtype=np.float32), indices,
                np.ascontiguousarray(vertices[:, 3:6], dtype=np.float32))
    return np.ascontiguousarray(vertices[:, :3], dtype=np.float32), indices


def orient_triangles(positions, triangles, normals):
    """三角形の巻き（p0→p1→p2 の外積の向き）を頂点の法線の向きへ揃える。Cycles は裏から当たった面の法線を光線の側へ反転する
    ので、巻きが頂点の法線と逆だと陰影が裏返る。(揃えた三角形, 入れ替えた三角形の数)"""
    p0 = positions[triangles[:, 0]]
    face = np.cross(positions[triangles[:, 1]] - p0, positions[triangles[:, 2]] - p0)
    vertex = normals[triangles[:, 0]] + normals[triangles[:, 1]] + normals[triangles[:, 2]]
    flip = np.einsum("ij,ij->i", face, vertex) < 0.0
    if flip.any():
        triangles = triangles.copy()
        triangles[flip, 1], triangles[flip, 2] = triangles[flip, 2], triangles[flip, 1].copy()
    return triangles, int(flip.sum())


def build_mesh(name, positions, triangles, normals=None):
    """位置と三角形から Blender のメッシュを作る（UV は持たない）。normals（頂点の法線、メッシュの座標）を与えると、
    点の領域の custom_normal の属性に入れて、Cycles が頂点の法線を補間して陰影を付けるようにする（エンジンの GBuffer と同じ）"""
    import bpy
    mesh = bpy.data.meshes.new(name)
    mesh.vertices.add(len(positions))
    mesh.vertices.foreach_set("co", positions.ravel())
    mesh.loops.add(triangles.size)
    mesh.loops.foreach_set("vertex_index", triangles.ravel())
    mesh.polygons.add(len(triangles))
    mesh.polygons.foreach_set("loop_start", np.arange(0, triangles.size, 3, dtype=np.int32))
    mesh.update(calc_edges=True)
    if normals is not None:
        attribute = mesh.attributes.new("custom_normal", "FLOAT_VECTOR", "POINT")
        attribute.data.foreach_set("vector", normals.ravel())
        mesh.update()
        if mesh.normals_domain != "POINT":
            raise RuntimeError(f"頂点の法線が点の領域にならない（{mesh.normals_domain}）: {name}")
    return mesh


def render_job(job_path):
    with open(job_path, encoding="utf-8") as f:
        job = json.load(f)
    kind = job.get("kind", "depth")
    if kind == "plate":
        render_plate_job(job)
    elif kind == "gray":
        render_gray_job(job)
    elif kind == "albedo":
        render_albedo_job(job)
    elif kind == "cloud":
        render_cloud_job(job)
    else:
        render_depth_job(job)


def render_depth_job(job):
    import bpy
    import mathutils
    import OpenImageIO as oiio

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene

    # 深度を放射する材質（View Z Depth はカメラの前方向への距離 m）
    material = bpy.data.materials.new("EngineViewDepth")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    camera_data = nodes.new("ShaderNodeCameraData")
    emission = nodes.new("ShaderNodeEmission")
    output = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(camera_data.outputs["View Z Depth"], emission.inputs["Color"])
    emission.inputs["Strength"].default_value = 1.0
    material.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])

    # 区画を読み、エンジンの座標から Blender の座標へ変換を掛ける
    started = time.time()
    to_blender = mathutils.Matrix(engine_to_blender_matrix4())
    vertex_count = 0
    triangle_count = 0
    for path in job["tiles"]:
        name = os.path.splitext(os.path.basename(path))[0]
        positions, triangles = read_terrain_ply(path)
        mesh = build_mesh(name, positions, triangles)
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        vertex_count += len(positions)
        triangle_count += len(triangles)
        del positions, triangles
    load_seconds = time.time() - started

    # カメラ（エンジンの撮影と同じ位置・向き・縦の画角・縦横比）
    matrix, vertical_fov = camera_to_blender(job["camera"])
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.shift_x = 0.0
    camera.shift_y = 0.0
    camera.clip_start = float(job["camera"].get("near", 0.1))
    camera.clip_end = CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object

    render = scene.render
    render.engine = "CYCLES"
    render.resolution_x = int(job["width"])
    render.resolution_y = int(job["height"])
    render.resolution_percentage = 100
    render.pixel_aspect_x = 1.0
    render.pixel_aspect_y = 1.0
    render.film_transparent = True
    cycles = scene.cycles
    cycles.device = "CPU"
    cycles.samples = 1
    cycles.use_adaptive_sampling = False
    cycles.use_denoising = False
    cycles.pixel_filter_type = "BOX"
    cycles.filter_width = PIXEL_FILTER_WIDTH
    cycles.max_bounces = 0
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    render.image_settings.color_depth = "32"
    render.filepath = job["exr"]
    started = time.time()
    bpy.ops.render.render(write_still=True)
    render_seconds = time.time() - started

    # EXR（上の行から）の Combined の R が深度、A が当たり（空は 0）
    image = oiio.ImageInput.open(job["exr"])
    if image is None:
        raise RuntimeError(f"EXR を開けない: {job['exr']}")
    spec = image.spec()
    names = list(spec.channelnames)
    pixels = np.asarray(image.read_image(0, 0, 0, spec.nchannels, "float")).reshape(spec.height, spec.width, spec.nchannels)
    image.close()
    depth = pixels[:, :, names.index("ViewLayer.Combined.R")].astype(np.float32)
    alpha = pixels[:, :, names.index("ViewLayer.Combined.A")]
    depth = np.where(alpha > 0.5, depth, np.float32(np.inf))
    write_pfm(job["out_pfm"], depth)
    os.remove(job["exr"])

    with open(job["out_json"], "w", encoding="utf-8") as f:
        json.dump({
            "view": job["view"],
            "format": "pfm_f32_le_bottom_up",
            "meaning": "view_axis_distance",
            "units": "m",
            "sky_value": "inf",
            "renderer": f"Blender {bpy.app.version_string} Cycles CPU",
            "samples": 1,
            "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH},
            "depth_source": "emission of Camera Data View Z Depth",
            "width": int(job["width"]),
            "height": int(job["height"]),
            "camera_engine": job["camera"],
            "camera_blender_matrix_world": matrix,
            "camera_vertical_fov_degrees": math.degrees(vertical_fov),
            "tiles": len(job["tiles"]),
            "vertices": vertex_count,
            "triangles": triangle_count,
            "surface_pixels": int(np.count_nonzero(alpha > 0.5)),
            "load_seconds": load_seconds,
            "render_seconds": render_seconds,
        }, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_REFERENCE_JOB view={job['view']} tiles={len(job['tiles'])} triangles={triangle_count} "
          f"load={load_seconds:.1f}s render={render_seconds:.1f}s")


def render_albedo_job(job):
    """照明の無い材質の値: 地形に OSL のシェーダー（terrain_albedo.osl。TerrainAlbedo.osl に値の表を足したもの）の出力を
    放射させ、放射だけを CPU（OSL）の 1 サンプルで画素の中心に寄せて描く。job の passes の出力ごとに、放射へつなぐ出力を
    替えて描き直す（OSL の出力を AOV のノードへつなぐと値が入らないので、放射で描く）。頂点の法線（Normal のパス）も書く"""
    import bpy
    import mathutils

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    camera_record = job["camera"]
    # スクリプトノードは描画エンジンが Cycles の OSL のときに台本を読んで出入り口を作るので、材質より先に決める
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.shading_system = True

    material = bpy.data.materials.new("TerrainAlbedo")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    script = nodes.new("ShaderNodeScript")
    script.mode = "EXTERNAL"
    script.filepath = job["osl"]
    if "Albedo" not in script.outputs:
        raise RuntimeError(f"OSL の台本を読めない（出力が無い）: {job['osl']}")
    tan_half = math.tan(math.radians(float(camera_record["vertical_fov_degrees"])) / 2.0)
    inputs = {
        "CameraPosition": camera_record["position"],
        "CameraForward": camera_record["forward"],
        "CameraRight": camera_record["right"],
        "CameraUp": camera_record["up"],
        "TanHalfVertical": tan_half,
        "Aspect": float(camera_record["aspect_ratio"]),
        "Width": float(job["width"]),
        "Height": float(job["height"]),
    }
    for name, value in inputs.items():
        script.inputs[name].default_value = value
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Strength"].default_value = 1.0
    output = nodes.new("ShaderNodeOutputMaterial")
    passes = job.get("passes") or MATERIAL_PASSES["albedo"]
    missing = [p[0] for p in passes if p[0] not in script.outputs] + [name for name in ("Pass",) if name not in script.inputs]
    if missing:
        raise RuntimeError(f"OSL の台本に出力 {missing} が無い: {job['osl']}")
    # 切り分けの試しでは、層の重み（emit = weights）を放射させて描ける
    first_socket = "Weights" if job.get("emit") == "weights" else passes[0][0]
    script.inputs["Pass"].default_value = int(passes[0][4])
    links.new(script.outputs[first_socket], emission.inputs["Color"])
    links.new(emission.outputs["Emission"], output.inputs["Surface"])

    # 区画を読み、視錐台（列に分けて描くときは列の視錐台）の外の三角形を除いて、巻きを頂点の法線へ揃える
    columns = job.get("columns")
    ndc_x = None
    if columns:
        pad = MATERIAL_COLUMN_PAD_PX
        ndc_x = (2.0 * (columns[0] - pad) / float(job["width"]) - 1.0, 2.0 * (columns[1] + pad) / float(job["width"]) - 1.0)
    started = time.time()
    to_blender = mathutils.Matrix(engine_to_blender_matrix4())
    vertex_count = 0
    triangle_count = 0
    flipped = 0
    for path in job["tiles"]:
        name = os.path.splitext(os.path.basename(path))[0]
        positions, triangles, normals = read_terrain_ply(path, with_normals=True)
        triangles = frustum_triangles(positions, triangles, camera_record, ALBEDO_TRIANGLE_MARGIN, ndc_x)
        if len(triangles) == 0:
            continue
        used, remap = np.unique(triangles, return_inverse=True)
        positions = np.ascontiguousarray(positions[used])
        normals = np.ascontiguousarray(normals[used])
        triangles = remap.reshape(-1, 3).astype(np.int32)
        triangles, count = orient_triangles(positions, triangles, normals)
        flipped += count
        mesh = build_mesh(name, positions, triangles, normals)
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        vertex_count += len(positions)
        triangle_count += len(triangles)
        del positions, triangles, normals
    load_seconds = time.time() - started

    matrix, vertical_fov = camera_to_blender(camera_record)
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.clip_start = float(camera_record.get("near", 0.1))
    camera.clip_end = CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object
    # 画素の小数のずらし（右・上が正の画素）。縦で合わせるカメラのずらしの単位は画の高さ
    shift_px = job.get("camera_shift_px") or [0.0, 0.0]
    camera.shift_x = float(shift_px[0]) / float(job["height"])
    camera.shift_y = float(shift_px[1]) / float(job["height"])

    render = scene.render
    render.engine = "CYCLES"
    render.resolution_x = int(job["width"])
    render.resolution_y = int(job["height"])
    render.resolution_percentage = 100
    render.film_transparent = True
    cycles = scene.cycles
    cycles.device = "CPU"
    cycles.shading_system = True
    cycles.samples = 1
    cycles.use_adaptive_sampling = False
    cycles.use_denoising = False
    cycles.pixel_filter_type = "BOX"
    cycles.filter_width = PIXEL_FILTER_WIDTH
    cycles.max_bounces = 0
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    view_layer = scene.view_layers[0]
    view_layer.use_pass_normal = True
    view_layer.use_pass_z = True
    if job.get("border"):
        # 切り分けの試しで画面の一部（左・下・右・上の割合）だけを描く
        render.use_border = True
        render.use_crop_to_border = False
        render.border_min_x, render.border_min_y, render.border_max_x, render.border_max_y = job["border"]
    elif columns:
        # 列（と左右に MATERIAL_COLUMN_PAD_PX 画素）だけを描く。つなぐときは列の中だけを使う
        render.use_border = True
        render.use_crop_to_border = False
        width = float(job["width"])
        render.border_min_x = max(0.0, (columns[0] - MATERIAL_COLUMN_PAD_PX) / width)
        render.border_max_x = min(1.0, (columns[1] + MATERIAL_COLUMN_PAD_PX) / width)
        render.border_min_y = 0.0
        render.border_max_y = 1.0
    render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    render.image_settings.color_depth = "32"
    prefix = job["prefix"]
    mode = job.get("mode", "albedo")
    pass_files = {}
    pass_seconds = {}
    render_seconds = 0.0
    surface = None
    for index, (socket, key, meaning, decode, pass_id) in enumerate(passes):
        if index > 0:
            links.new(script.outputs[socket], emission.inputs["Color"])
            script.inputs["Pass"].default_value = int(pass_id)
        started = time.time()
        channels = render_and_read(scene, job["exr"])
        pass_seconds[key] = time.time() - started
        render_seconds += pass_seconds[key]
        if index == 0:
            alpha = channels["ViewLayer.Combined.A"]
            surface = alpha > 0.5
            normal_blender = pass_rgb(channels, "Normal", ("X", "Y", "Z"))
            normal = np.stack([normal_blender[..., 0], normal_blender[..., 2], -normal_blender[..., 1]], axis=-1)
            normal = np.where(surface[..., None], normal, 0.0).astype(np.float32)
            write_pfm_rgb(f"{prefix}.normal.pfm", normal)
            write_pfm(f"{prefix}.alpha.pfm", alpha.astype(np.float32))
            # 深度（カメラの前方向への距離 m。空は inf）。標本の位置の揃え（エンジンの深度との小数のずれ）に使う
            depth = np.where(surface, channels["ViewLayer.Depth.Z"], np.inf).astype(np.float32)
            write_pfm(f"{prefix}.depth.pfm", depth)
        value = pass_rgb(channels, "Combined").astype(np.float64)
        if decode == "encoded_normal":
            value = value * 2.0 - 1.0
            value /= np.maximum(np.linalg.norm(value, axis=-1, keepdims=True), 1e-12)
        value = np.where(surface[..., None], value, 0.0).astype(np.float32)
        write_pfm_rgb(f"{prefix}.{key}.pfm", value)
        pass_files[key] = {"file": f"{job['view']}.{key}.pfm", "meaning": meaning, "osl_output": socket, "osl_pass": int(pass_id),
                           "sky_value": 0}
        del channels
    with open(f"{prefix}.{mode}.json", "w", encoding="utf-8") as f:
        json.dump({
            "view": job["view"],
            "mode": mode,
            "format": "pfm_f32_le_bottom_up",
            **pass_files,
            "normal": {"file": f"{job['view']}.normal.pfm", "meaning": "interpolated_vertex_normal_engine_world",
                       "sky_value": 0},
            "alpha": {"file": f"{job['view']}.alpha.pfm", "meaning": "surface_coverage"},
            "depth": {"file": f"{job['view']}.depth.pfm", "meaning": "view_axis_distance", "units": "m", "sky_value": "inf"},
            "camera_shift_px": [float(v) for v in (job.get("camera_shift_px") or [0.0, 0.0])],
            "renderer": f"Blender {bpy.app.version_string} Cycles CPU OSL",
            "samples": 1,
            "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH},
            "osl": os.path.basename(job["osl"]),
            "width": int(job["width"]),
            "height": int(job["height"]),
            "camera_engine": camera_record,
            "camera_blender_matrix_world": matrix,
            "camera_vertical_fov_degrees": math.degrees(vertical_fov),
            "tiles": len(job["tiles"]),
            "vertices": vertex_count,
            "triangles": triangle_count,
            "flipped_triangles": flipped,
            "surface_pixels": int(np.count_nonzero(surface)),
            "load_seconds": load_seconds,
            "render_seconds": render_seconds,
            "pass_render_seconds": pass_seconds,
            "column_pixels": columns,
        }, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_ALBEDO view={job['view']} mode={mode} triangles={triangle_count} load={load_seconds:.1f}s "
          f"render={render_seconds:.1f}s")


def setup_cycles_device(scene):
    """OptiX の GPU があれば使い、無ければ CPU。使った装置の記述を返す"""
    import bpy
    preferences = bpy.context.preferences.addons["cycles"].preferences
    used = []
    try:
        preferences.compute_device_type = "OPTIX"
        preferences.get_devices()
        for device in preferences.devices:
            device.use = device.type == "OPTIX"
            if device.use:
                used.append(device.name)
    except (TypeError, ValueError):
        used = []
    scene.cycles.device = "GPU" if used else "CPU"
    return f"OPTIX {', '.join(used)}" if used else "CPU"


def configure_gray_cycles(scene, width, height, exr_path):
    """灰色の比較の描画の設定（デノイズ・適応サンプリング・明るさの上限なし、箱の 1 画素のフィルタ、線形の多層 EXR）"""
    render = scene.render
    render.engine = "CYCLES"
    render.resolution_x = int(width)
    render.resolution_y = int(height)
    render.resolution_percentage = 100
    render.pixel_aspect_x = 1.0
    render.pixel_aspect_y = 1.0
    render.film_transparent = True
    render.use_persistent_data = True
    cycles = scene.cycles
    cycles.use_adaptive_sampling = False
    cycles.use_denoising = False
    cycles.pixel_filter_type = "BOX"
    cycles.filter_width = GRAY_PIXEL_FILTER_WIDTH
    cycles.sample_clamp_direct = 0.0
    cycles.sample_clamp_indirect = 0.0
    cycles.blur_glossy = 0.0
    cycles.use_animated_seed = False
    set_gray_bounces(scene, GRAY_MAX_BOUNCES)
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    render.image_settings.media_type = "MULTI_LAYER_IMAGE"
    render.image_settings.file_format = "OPEN_EXR_MULTILAYER"
    render.image_settings.color_depth = "32"
    render.filepath = exr_path


def set_gray_bounces(scene, bounces):
    cycles = scene.cycles
    cycles.max_bounces = bounces
    cycles.diffuse_bounces = bounces
    cycles.glossy_bounces = bounces
    cycles.transmission_bounces = 0
    cycles.volume_bounces = 0
    cycles.transparent_max_bounces = 8


def make_uniform_world(scene, rgb):
    """一様な空（全方向で同じ放射輝度の Background）。(world, Background の節)"""
    import bpy
    world = bpy.data.worlds.new("CommonLightSky")
    if world.node_tree is None:
        world.use_nodes = True
    nodes = world.node_tree.nodes
    background = next((n for n in nodes if n.type == "BACKGROUND"), None)
    output = next((n for n in nodes if n.type == "OUTPUT_WORLD"), None)
    if background is None:
        background = nodes.new("ShaderNodeBackground")
    if output is None:
        output = nodes.new("ShaderNodeOutputWorld")
    world.node_tree.links.new(background.outputs["Background"], output.inputs["Surface"])
    background.inputs["Color"].default_value = (float(rgb[0]), float(rgb[1]), float(rgb[2]), 1.0)
    background.inputs["Strength"].default_value = 1.0
    scene.world = world
    return world, background


def make_sun_lamp(scene, lux, to_sun_engine, angle_degrees):
    """照度 lux（太陽に垂直な面）の Sun を、エンジンの太陽へ向かう向き to_sun_engine から差す向きに置く"""
    import bpy
    import mathutils
    light = bpy.data.lights.new("CommonLightSun", "SUN")
    light.energy = float(lux)
    light.angle = math.radians(angle_degrees)
    light.color = (1.0, 1.0, 1.0)
    obj = bpy.data.objects.new("CommonLightSun", light)
    scene.collection.objects.link(obj)
    # Sun は自分の −Z の向きへ光を出すので、+Z を太陽へ向ける
    z = mathutils.Vector(engine_to_blender(to_sun_engine)).normalized()
    helper = mathutils.Vector((0.0, 0.0, 1.0)) if abs(z.z) < 0.99 else mathutils.Vector((1.0, 0.0, 0.0))
    x = helper.cross(z).normalized()
    y = z.cross(x)
    obj.matrix_world = mathutils.Matrix(((x.x, y.x, z.x, 0.0), (x.y, y.y, z.y, 0.0), (x.z, y.z, z.z, 0.0),
                                         (0.0, 0.0, 0.0, 1.0)))
    return obj


def make_diffuse_material(name, albedo):
    import bpy
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    diffuse = nodes.new("ShaderNodeBsdfDiffuse")
    output = nodes.new("ShaderNodeOutputMaterial")
    diffuse.inputs["Color"].default_value = (albedo, albedo, albedo, 1.0)
    diffuse.inputs["Roughness"].default_value = 0.0
    material.node_tree.links.new(diffuse.outputs["BSDF"], output.inputs["Surface"])
    return material


def make_gray_material(gray):
    """Principled BSDF の灰色（Base Color・Roughness・Metallic 0・IOR 1.5 で F0 0.04）"""
    import bpy
    albedo = float(gray["albedo"])
    f0 = float(gray["f0"])
    ior = (1.0 + math.sqrt(f0)) / (1.0 - math.sqrt(f0))
    material = bpy.data.materials.new("GrayTerrain")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    output = nodes.new("ShaderNodeOutputMaterial")
    principled.inputs["Base Color"].default_value = (albedo, albedo, albedo, 1.0)
    principled.inputs["Metallic"].default_value = 0.0
    principled.inputs["Roughness"].default_value = float(gray["roughness"])
    principled.inputs["IOR"].default_value = ior
    principled.inputs["Specular IOR Level"].default_value = 0.5
    material.node_tree.links.new(principled.outputs["BSDF"], output.inputs["Surface"])
    return material, ior


def screen_image(name, image, normal=False):
    """画素ごとの材質の画（上の行が 0 行目、(高さ, 幅, チャンネル)）を Blender の浮動小数の画像にする（色空間の変換なし）。
    normal なら値をエンジンの座標の向きから Blender の座標の向きへ移す。1 チャンネルは灰色の RGB にする"""
    import bpy
    height, width = image.shape[:2]
    rgba = np.ones((height, width, 4), dtype=np.float32)
    if normal:
        rgba[..., :3] = np.stack([image[..., 0], -image[..., 2], image[..., 1]], axis=-1)
    elif image.shape[-1] == 1:
        rgba[..., :3] = image[..., :1]
    else:
        rgba[..., :3] = image[..., :3]
    result = bpy.data.images.new(name, width=width, height=height, alpha=True, float_buffer=True)
    result.colorspace_settings.name = "Non-Color"
    # Blender の画像は下の行から並ぶ
    result.pixels.foreach_set(np.ascontiguousarray(rgba[::-1]).ravel())
    result.update()
    return result


def make_lit_material(job):
    """実材質の地形（--lit）: カメラの光線の当たりは画素ごとの材質（--albedo と同じ描き方で描いたアルベド・法線マップを
    掛けた法線・粗さ。画面の座標で最近傍に引く）、それ以外の光線（影・照り返し）の当たりは OSL の PASS_BOUNCE
    （アルベドと粗さ、法線マップなし、等方の足跡）を Principled BSDF（金属度 0・IOR 1.5 で F0 0.04）に入れる。
    (材質, IOR, 記録)"""
    import bpy
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import GrayRegions as GR  # noqa: E402
    f0 = 0.04
    ior = (1.0 + math.sqrt(f0)) / (1.0 - math.sqrt(f0))
    material = bpy.data.materials.new("TerrainLit")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    output = nodes.new("ShaderNodeOutputMaterial")
    mix = nodes.new("ShaderNodeMixShader")
    light_path = nodes.new("ShaderNodeLightPath")
    links.new(light_path.outputs["Is Camera Ray"], mix.inputs["Fac"])
    links.new(mix.outputs["Shader"], output.inputs["Surface"])

    def principled():
        node = nodes.new("ShaderNodeBsdfPrincipled")
        node.inputs["Metallic"].default_value = 0.0
        node.inputs["IOR"].default_value = ior
        node.inputs["Specular IOR Level"].default_value = 0.5
        return node

    # カメラの光線: 画面の座標（Window。左下が 0、右上が 1。カメラのずらしを含む描く枠）で画素の値を最近傍に引く
    camera_bsdf = principled()
    coords = nodes.new("ShaderNodeTexCoord")
    width, height = int(job["width"]), int(job["height"])
    images = {}
    for key, socket, normal in (("albedo", "Base Color", False), ("shaded_normal", "Normal", True),
                                ("roughness", "Roughness", False)):
        pixels = GR.read_pfm(job["screen_material"][key])
        if pixels.shape[:2] != (height, width):
            raise RuntimeError(f"画素ごとの材質 {key} の大きさ {pixels.shape[:2]} が描画 {height}x{width} と違う")
        if key == "roughness":
            pixels = pixels[..., :1]
        texture = nodes.new("ShaderNodeTexImage")
        texture.image = screen_image(f"Screen_{key}", pixels, normal=normal)
        texture.interpolation = "Closest"
        texture.extension = "EXTEND"
        links.new(coords.outputs["Window"], texture.inputs["Vector"])
        links.new(texture.outputs["Color"], camera_bsdf.inputs[socket])
        images[key] = os.path.basename(job["screen_material"][key])

    # それ以外の光線: OSL の PASS_BOUNCE
    bounce_bsdf = principled()
    script = nodes.new("ShaderNodeScript")
    script.mode = "EXTERNAL"
    script.filepath = job["osl"]
    if "Albedo" not in script.outputs or "Pass" not in script.inputs:
        raise RuntimeError(f"OSL の台本を読めない（出力が無い）: {job['osl']}")
    camera_record = job["camera"]
    inputs = {
        "CameraPosition": camera_record["position"],
        "CameraForward": camera_record["forward"],
        "CameraRight": camera_record["right"],
        "CameraUp": camera_record["up"],
        "TanHalfVertical": math.tan(math.radians(float(camera_record["vertical_fov_degrees"])) / 2.0),
        "Aspect": float(camera_record["aspect_ratio"]),
        "Width": float(width),
        "Height": float(height),
        "Pass": PASS_BOUNCE,
    }
    for name, value in inputs.items():
        script.inputs[name].default_value = value
    separate = nodes.new("ShaderNodeSeparateColor")
    links.new(script.outputs["Albedo"], bounce_bsdf.inputs["Base Color"])
    links.new(script.outputs["Roughness"], separate.inputs["Color"])
    links.new(separate.outputs["Red"], bounce_bsdf.inputs["Roughness"])
    links.new(bounce_bsdf.outputs["BSDF"], mix.inputs[1])
    links.new(camera_bsdf.outputs["BSDF"], mix.inputs[2])
    record = {
        "type": "Principled BSDF（金属度 0・IOR {:.4f}・Specular IOR Level 0.5、F0 {}）".format(ior, f0),
        "ior": ior,
        "f0": f0,
        "camera_rays": {"source": "画素ごとの材質（画面の座標で最近傍）", "images": images},
        "other_rays": {"source": "OSL", "osl": os.path.basename(job["osl"]), "pass": PASS_BOUNCE,
                       "normal": "補間した頂点の法線（法線マップなし）"},
        "material_ambient_occlusion": "Cycles には掛けない（エンジンは GBuffer の材質の AO を環境光と直接光の一部に掛ける）",
    }
    return material, ior, record


def look_at_matrix(position, target):
    """position から target を見る Blender のカメラの姿勢（−Z が視線、+Y が上に近い向き）"""
    import mathutils
    eye = mathutils.Vector(position)
    back = (eye - mathutils.Vector(target)).normalized()
    right = mathutils.Vector((0.0, 0.0, 1.0)).cross(back).normalized()
    up = back.cross(right)
    return mathutils.Matrix(((right.x, up.x, back.x, eye.x), (right.y, up.y, back.y, eye.y),
                             (right.z, up.z, back.z, eye.z), (0.0, 0.0, 0.0, 1.0)))


def render_and_read(scene, exr_path):
    """描いて多層 EXR を読み、チャンネルの名前 → (高さ, 幅) の float32 の辞書を返す（上の行が 0 行目）"""
    import bpy
    import OpenImageIO as oiio
    scene.render.filepath = exr_path
    bpy.ops.render.render(write_still=True)
    image = oiio.ImageInput.open(exr_path)
    if image is None:
        raise RuntimeError(f"EXR を開けない: {exr_path}")
    # Blender 5.2 の多層 EXR は多パートで、パスごとに別のサブ画像に入る
    channels = {}
    subimage = 0
    while image.seek_subimage(subimage, 0):
        spec = image.spec()
        names = list(spec.channelnames)
        pixels = np.asarray(image.read_image(subimage, 0, 0, spec.nchannels, "float")).reshape(
            spec.height, spec.width, spec.nchannels)
        for index, name in enumerate(names):
            channels[name] = pixels[:, :, index].astype(np.float32)
        subimage += 1
    image.close()
    os.remove(exr_path)
    return channels


def pass_rgb(channels, pass_name, components=("R", "G", "B")):
    """多層 EXR の 1 つのパスの成分を (高さ, 幅, 成分) で取り出す"""
    keys = [f"ViewLayer.{pass_name}.{c}" for c in components]
    missing = [k for k in keys if k not in channels]
    if missing:
        raise RuntimeError(f"EXR にパス {pass_name} が無い（{missing}。あるもの: {sorted(channels)}）")
    return np.stack([channels[k] for k in keys], axis=-1)


def write_exr(path, image, names):
    """(高さ, 幅, チャンネル) の float32 を名前つきのチャンネルの EXR（zip）で書く（上の行が 0 行目）"""
    import OpenImageIO as oiio
    image = np.ascontiguousarray(image, dtype=np.float32)
    height, width, count = image.shape
    output = oiio.ImageOutput.create(path)
    if output is None:
        raise RuntimeError(f"EXR を書けない: {path}")
    spec = oiio.ImageSpec(width, height, count, "float")
    spec.channelnames = tuple(names)
    spec.attribute("compression", "zip")
    output.open(path, spec)
    output.write_image(image)
    output.close()


def load_gray_regions():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import GrayRegions  # noqa: E402  Blender の中でも numpy だけで動く
    return GrayRegions


def load_building_regions():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import BuildingRegions  # noqa: E402  Blender の中でも numpy だけで動く
    return BuildingRegions


def load_plant_regions():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import PlantRegions  # noqa: E402  Blender の中でも numpy だけで動く
    return PlantRegions


def make_plant_material(name, roughness, leaf):
    """株の材質: 頂点の色の属性 albedo（線形）を Base Color にした Principled BSDF（金属 0・IOR 1.5 で F0 0.04）。葉は
    それに Translucent BSDF（色はアルベドの PLANT_LEAF_TRANSMITTANCE_RATIO 倍）を足して両面から光を通す"""
    import bpy
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    attribute = nodes.new("ShaderNodeAttribute")
    attribute.attribute_type = "GEOMETRY"
    attribute.attribute_name = "albedo"
    principled = nodes.new("ShaderNodeBsdfPrincipled")
    output = nodes.new("ShaderNodeOutputMaterial")
    links.new(attribute.outputs["Color"], principled.inputs["Base Color"])
    principled.inputs["Metallic"].default_value = 0.0
    principled.inputs["Roughness"].default_value = float(roughness)
    principled.inputs["IOR"].default_value = 1.5
    principled.inputs["Specular IOR Level"].default_value = 0.5
    if not leaf:
        links.new(principled.outputs["BSDF"], output.inputs["Surface"])
        return material
    scale = nodes.new("ShaderNodeVectorMath")
    scale.operation = "SCALE"
    scale.inputs["Scale"].default_value = PLANT_LEAF_TRANSMITTANCE_RATIO
    links.new(attribute.outputs["Color"], scale.inputs[0])
    translucent = nodes.new("ShaderNodeBsdfTranslucent")
    links.new(scale.outputs["Vector"], translucent.inputs["Color"])
    add = nodes.new("ShaderNodeAddShader")
    links.new(principled.outputs["BSDF"], add.inputs[0])
    links.new(translucent.outputs["BSDF"], add.inputs[1])
    links.new(add.outputs["Shader"], output.inputs["Surface"])
    return material


def make_translucent_white_material(name):
    """白い透過の面（Translucent BSDF、色 1）。葉の裏の見え方を描く"""
    import bpy
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    translucent = nodes.new("ShaderNodeBsdfTranslucent")
    output = nodes.new("ShaderNodeOutputMaterial")
    translucent.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    material.node_tree.links.new(translucent.outputs["BSDF"], output.inputs["Surface"])
    return material


def place_plant(scene, plant):
    """比べる株の PLY（エンジンのワールドの三角形）を枝と葉の 2 つの物体にして置く。物体の番号は枝
    PlantRegions.PLANT_BRANCH_PASS_INDEX・葉 PLANT_LEAF_PASS_INDEX。記録の辞書を返す"""
    import bpy
    import mathutils
    R = load_plant_regions()
    placement = plant["placement"]
    positions, normals, colors, triangles = R.read_plant_ply(plant["ply_path"])
    if len(positions) != int(placement["vertices"]) or len(triangles) != int(placement["triangles"]):
        raise RuntimeError(f"株の PLY の頂点・三角形の数がログと違う（PLY {len(positions)}・{len(triangles)}、ログ "
                           f"{placement['vertices']}・{placement['triangles']}）")
    triangles, flipped = orient_triangles(positions, triangles, normals)
    leaf = R.leaf_triangles(normals, triangles)
    to_blender = mathutils.Matrix(engine_to_blender_matrix4())
    parts = {}
    for part, mask, index in (("branch", ~leaf, R.PLANT_BRANCH_PASS_INDEX), ("leaf", leaf, R.PLANT_LEAF_PASS_INDEX)):
        selected = triangles[mask]
        if not len(selected):
            raise RuntimeError(f"株の PLY に{'葉' if part == 'leaf' else '枝'}の三角形が無い")
        used, remapped = np.unique(selected, return_inverse=True)
        name = f"ReferencePlant_{part}"
        mesh = build_mesh(name, positions[used], remapped.reshape(-1, 3).astype(np.int32), normals[used])
        attribute = mesh.color_attributes.new("albedo", "FLOAT_COLOR", "POINT")
        rgba = np.concatenate([colors[used], np.ones((len(used), 1), dtype=np.float32)], axis=1)
        attribute.data.foreach_set("color", rgba.ravel())
        mesh.materials.append(make_plant_material(name, placement["roughness"], part == "leaf"))
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        obj.pass_index = index
        parts[part] = {"vertices": int(len(used)), "triangles": int(len(selected)),
                       "albedo_mean": [float(v) for v in colors[used].mean(axis=0)]}
    print(f"BLENDER_GRAY plant id={placement['id']} branch_triangles={parts['branch']['triangles']} "
          f"leaf_triangles={parts['leaf']['triangles']} rewound={flipped}", flush=True)
    return {"id": placement["id"], "placement": placement, "ply": PLANT_PLY, "parts": parts, "triangles_rewound": flipped,
            "opacity_mask": "なし（模型はアルファ抜きを使わない。全面 1）",
            "material": {"branch": "Principled BSDF（頂点のアルベド・粗さ・金属 0・IOR 1.5）",
                         "leaf": "Principled BSDF + Translucent BSDF（色はアルベドの "
                                 f"{PLANT_LEAF_TRANSMITTANCE_RATIO} 倍、両面）",
                         "roughness": float(placement["roughness"]),
                         "leaf_transmittance_ratio": PLANT_LEAF_TRANSMITTANCE_RATIO},
            "occlusion": "頂点の遮蔽（FoliageVertex.Occlusion）は Cycles では使わない（光線で遮る）"}


def place_building(scene, building):
    """比べる建物の glTF を Blender の読み込みで読み、エンジンの配置（位置・Y 軸まわりの回転・拡大率）で置く。材質は glTF の
    ものをそのまま使い、メッシュの物体の番号を BuildingRegions.BUILDING_PASS_INDEX にする。記録の辞書を返す"""
    import bpy
    import mathutils
    R = load_building_regions()
    try:
        bpy.ops.preferences.addon_enable(module="io_scene_gltf2")
    except (RuntimeError, AttributeError):
        pass
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=building["gltf_path"])
    imported = [obj for obj in bpy.data.objects if obj not in before]
    meshes = [obj for obj in imported if obj.type == "MESH"]
    if not meshes:
        raise RuntimeError(f"glTF からメッシュを読めない: {building['gltf_path']}")
    root = bpy.data.objects.new("ReferenceBuilding", None)
    scene.collection.objects.link(root)
    for obj in imported:
        if obj.parent is None:
            obj.parent = root
    # glTF の読み込みはローカルの +Y 上を +Z 上へ移す（x, y, z → x, −z, y。エンジンからの移し方と同じ）。エンジンの yaw は
    # ローカルの (x, z) をワールドの (x cos − z sin, z cos + x sin) へ写すので、Blender（y = −Z）では z 軸まわりの −yaw
    placement = building["placement"]
    scale = float(placement["scale"])
    root.matrix_world = (mathutils.Matrix.Translation(mathutils.Vector(engine_to_blender(placement["position"]))) @
                         mathutils.Matrix.Rotation(-float(placement["yaw_rad"]), 4, "Z") @
                         mathutils.Matrix.Diagonal((scale, scale, scale, 1.0)))
    bpy.context.view_layer.update()
    lowest = math.inf
    triangles = 0
    materials = set()
    for obj in meshes:
        obj.pass_index = R.BUILDING_PASS_INDEX
        world = obj.matrix_world
        lowest = min(lowest, min((world @ vertex.co).z for vertex in obj.data.vertices))
        triangles += sum(len(polygon.vertices) - 2 for polygon in obj.data.polygons)
        materials.update(slot.material.name for slot in obj.material_slots if slot.material)
    expected_bottom = float(placement["position"][1] + placement["mesh_min_y"] * scale)
    print(f"BLENDER_GRAY building id={placement['id']} meshes={len(meshes)} triangles={triangles} "
          f"bottom={lowest:.4f} expected_bottom={expected_bottom:.4f}", flush=True)
    return {"id": placement["id"], "gltf": building["gltf"], "placement": placement, "bounds": building["bounds"],
            "meshes": len(meshes), "triangles": triangles, "materials": sorted(materials),
            "material_source": "glTF の材質（Blender の glTF の読み込み。遮蔽のテクスチャは Cycles では使わない）",
            "blender_bottom_z": lowest, "placement_bottom_y": expected_bottom}


def render_view_depth(scene, exr_path, background):
    """カメラの前方向への距離 m（View Z Depth を放射する材質を全体に掛け、1 サンプル・箱の幅 PIXEL_FILTER_WIDTH で描く。
    --depth と同じ描き方）。空は +∞。描いた後に設定を本描画のものへ戻す"""
    import bpy
    material = bpy.data.materials.new("EngineViewDepth")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    nodes.clear()
    camera_data = nodes.new("ShaderNodeCameraData")
    emission = nodes.new("ShaderNodeEmission")
    output = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(camera_data.outputs["View Z Depth"], emission.inputs["Color"])
    emission.inputs["Strength"].default_value = 1.0
    material.node_tree.links.new(emission.outputs["Emission"], output.inputs["Surface"])
    # 放射する三角形を光源として標本にしない（全体に掛けると光源の木を何千万の三角形で作り直して止まったように遅くなる。
    # 深度はカメラの光線の当たりの放射だけを読むので値は変わらない）
    material.cycles.emission_sampling = "NONE"
    view_layer = scene.view_layers[0]
    view_layer.material_override = material
    background.inputs["Strength"].default_value = 0.0
    set_gray_bounces(scene, 0)
    scene.cycles.samples = 1
    scene.cycles.seed = 0
    scene.cycles.filter_width = PIXEL_FILTER_WIDTH
    channels = render_and_read(scene, exr_path)
    view_layer.material_override = None
    background.inputs["Strength"].default_value = 1.0
    set_gray_bounces(scene, GRAY_MAX_BOUNCES)
    scene.cycles.filter_width = GRAY_PIXEL_FILTER_WIDTH
    depth = channels["ViewLayer.Combined.R"]
    return np.where(channels["ViewLayer.Combined.A"] > 0.5, depth, np.float32(np.inf)).astype(np.float32)


def render_plate_job(job):
    """灰色の板だけの場面を sun か sky で描く（Cycles 側の尺度の較正）"""
    import bpy
    import mathutils

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    device = setup_cycles_device(scene)
    GR = load_gray_regions()
    plate = job["plate"]
    half = 0.5 * float(plate["size_m"])
    positions = np.array([[-half, -half, 0.0], [half, -half, 0.0], [half, half, 0.0], [-half, half, 0.0]],
                         dtype=np.float32)
    triangles = np.array([[0, 1, 2], [0, 2, 3]], dtype=np.int32)
    mesh = build_mesh("GrayPlate", positions, triangles)
    mesh.materials.append(make_diffuse_material("GrayPlateDiffuse", float(plate["albedo"])))
    obj = bpy.data.objects.new("GrayPlate", mesh)
    scene.collection.objects.link(obj)

    camera = bpy.data.cameras.new("PlateCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = math.radians(float(job["vertical_fov_degrees"]))
    camera_object = bpy.data.objects.new("PlateCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = look_at_matrix(
        (0.0, -float(plate["forward_m"]), float(plate["camera_height_above_plate_m"])), (0.0, 0.0, 0.0))
    scene.camera = camera_object

    lighting = job["lighting"]
    case = job["case"]
    world, background = make_uniform_world(scene, lighting["sky_radiance_rgb"])
    if case == "sun":
        background.inputs["Strength"].default_value = 0.0
        make_sun_lamp(scene, lighting["sun_illuminance_normal_lux"], job["to_sun_engine"],
                      lighting["sun_angle_degrees"])
    exr_tmp = job["out_exr"] + ".render.exr"
    configure_gray_cycles(scene, job["width"], job["height"], exr_tmp)
    scene.render.film_transparent = False
    scene.cycles.samples = int(job["samples"])
    scene.cycles.seed = 0
    started = time.time()
    channels = render_and_read(scene, exr_tmp)
    seconds = time.time() - started
    rgb = pass_rgb(channels, "Combined")
    write_exr(job["out_exr"], rgb, ("R", "G", "B"))
    GR.write_pfm(job["out_pfm"], rgb)
    with open(job["out_json"], "w", encoding="utf-8") as f:
        json.dump({
            "case": case,
            "renderer": f"Blender {bpy.app.version_string} Cycles",
            "device": device,
            "samples": int(job["samples"]),
            "width": int(job["width"]),
            "height": int(job["height"]),
            "plate": plate,
            "material": "Diffuse BSDF (roughness 0)",
            "sun_altitude_degrees": job["sun_altitude_degrees"],
            "sun_azimuth_degrees": job["sun_azimuth_degrees"],
            "lighting": lighting,
            "render_seconds": seconds,
            "exr_sha256": sha256_file(job["out_exr"]),
            "pfm_sha256": sha256_file(job["out_pfm"]),
        }, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_GRAY plate case={case} samples={job['samples']} device={device} seconds={seconds:.1f} "
          f"center={rgb[rgb.shape[0] // 2, rgb.shape[1] // 2].tolist()}")


def render_gray_job(job):
    """灰色（job の material_mode が lit なら実材質）の地形を 1 視点描く: 太陽の見え方と法線、続けて種を変えた本描画を
    収束まで重ねる"""
    import bpy
    import mathutils

    GR = load_gray_regions()
    # 建物 1 棟の比較（--building）: 領域と収束は BuildingRegions、ほかは灰色の地形と同じ
    building = job.get("building")
    # 低木 1 株の比較（--plant）: 領域と収束は PlantRegions
    plant = job.get("plant")
    R = load_building_regions() if building else (load_plant_regions() if plant else GR)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    device = setup_cycles_device(scene)
    lit = job.get("material_mode") == "lit"
    if lit:
        # スクリプトノードは描画エンジンが Cycles の OSL のときに台本を読んで出入り口を作るので、材質より先に決める
        scene.render.engine = "CYCLES"
        scene.cycles.shading_system = True
        material, ior, material_record = make_lit_material(job)
    else:
        material, ior = make_gray_material(job["gray_material"])
        material_record = {"type": "Principled BSDF", "base_color": float(job["gray_material"]["albedo"]),
                           "roughness": float(job["gray_material"]["roughness"]), "metallic": 0.0, "ior": ior,
                           "specular_ior_level": 0.5, "f0": float(job["gray_material"]["f0"])}

    started = time.time()
    to_blender = mathutils.Matrix(engine_to_blender_matrix4())
    vertex_count = 0
    triangle_count = 0
    flipped_count = 0
    for path in job["tiles"]:
        name = os.path.splitext(os.path.basename(path))[0]
        positions, triangles, normals = read_terrain_ply(path, with_normals=True)
        triangles, flipped = orient_triangles(positions, triangles, normals)
        flipped_count += flipped
        mesh = build_mesh(name, positions, triangles, normals)
        mesh.materials.append(material)
        obj = bpy.data.objects.new(name, mesh)
        scene.collection.objects.link(obj)
        obj.matrix_world = to_blender
        vertex_count += len(positions)
        triangle_count += len(triangles)
        del positions, triangles, normals
    building_record = place_building(scene, building) if building else None
    plant_record = place_plant(scene, plant) if plant else None
    load_seconds = time.time() - started

    matrix, vertical_fov = camera_to_blender(job["camera"])
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.clip_start = float(job["camera"].get("near", 0.1))
    camera.clip_end = CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(matrix)
    scene.camera = camera_object
    # 画素の小数のずらし（右・上が正の画素。画素ごとの材質を描いたときと同じ。縦で合わせるカメラのずらしの単位は画の高さ）
    shift_px = job.get("camera_shift_px") or [0.0, 0.0]
    camera.shift_x = float(shift_px[0]) / float(job["height"])
    camera.shift_y = float(shift_px[1]) / float(job["height"])

    lighting = job["lighting"]
    world, background = make_uniform_world(scene, lighting["sky_radiance_rgb"])
    sun = make_sun_lamp(scene, lighting["sun_illuminance_normal_lux"], job["to_sun_engine"],
                        lighting["sun_angle_degrees"])
    # 太陽の光と空の光を別々のパスにも書く（光の組）
    view_layer = scene.view_layers[0]
    view_layer.lightgroups.add(name="sun")
    view_layer.lightgroups.add(name="sky")
    sun.lightgroup = "sun"
    world.lightgroup = "sky"
    view_layer.use_pass_normal = True
    view_layer.use_pass_object_index = bool(building or plant)
    # 株の葉の透過は光の種類の分け方で透過のパスへ入ることがあるので、株では透過のパスも書いて直接光・間接光に足す
    for kind in ("diffuse", "glossy") + (("transmission",) if plant else ()):
        for part in ("direct", "indirect", "color"):
            setattr(view_layer, f"use_pass_{kind}_{part}", True)

    prefix = job["prefix"]
    exr_tmp = prefix + ".render.exr"
    width, height = int(job["width"]), int(job["height"])
    configure_gray_cycles(scene, width, height, exr_tmp)

    # 1. 太陽の見え方: 白いランバート面・太陽だけ・直接光だけ。輝度 / (E⊥ × max(n·l, 0) / π) が見え方（影は 0、半影は間）
    white = make_diffuse_material("VisibilityWhite", 1.0)
    view_layer.material_override = white
    background.inputs["Strength"].default_value = 0.0
    set_gray_bounces(scene, 0)
    scene.cycles.samples = VISIBILITY_SPP
    scene.cycles.seed = 0
    vis_started = time.time()
    channels = render_and_read(scene, exr_tmp)
    visibility_seconds = time.time() - vis_started
    white_rgb = pass_rgb(channels, "Combined")
    alpha = channels["ViewLayer.Combined.A"]
    normal_blender = pass_rgb(channels, "Normal", ("X", "Y", "Z"))
    normal = np.stack([normal_blender[..., 0], normal_blender[..., 2], -normal_blender[..., 1]], axis=-1)
    length = np.linalg.norm(normal, axis=-1, keepdims=True)
    normal = np.where(length > 1e-6, normal / np.maximum(length, 1e-6), 0.0).astype(np.float32)
    to_sun = np.asarray(job["to_sun_engine"], dtype=np.float32)
    n_dot_l = (normal @ to_sun).astype(np.float32)
    expected = float(lighting["sun_illuminance_normal_lux"]) * np.maximum(n_dot_l, 0.0) / math.pi
    white_y = GR.luminance(white_rgb)
    visibility = np.where(n_dot_l > 1e-3, white_y / np.maximum(expected, 1e-6), 0.0).astype(np.float32)
    if not (alpha > 0.5).any() or not (white_y > 0.0).any():
        raise RuntimeError("太陽の見え方の描画が空（表面か光が無い。装置のメモリ不足を疑う）")
    if building:
        # 建物の画素（Object Index。画素の中の標本の取り方に依らず、当たった物の番号）
        # パスの名前は Blender 5.2 で「Object Index」（前の版は IndexOB）
        index_keys = [key for key in channels if ".Object Index." in key or ".IndexOB." in key]
        if not index_keys:
            raise RuntimeError(f"EXR に Object Index のパスが無い（あるもの: {sorted(channels)}）")
        building_mask = (np.abs(channels[index_keys[0]] - R.BUILDING_PASS_INDEX) < 0.5) & (alpha > 0.5)
        if not building_mask.any():
            raise RuntimeError("建物が描画に映っていない（配置かカメラが違う）")
    if plant:
        # 株の画素の物体の番号（枝 1・葉 2、地形 0）
        index_keys = [key for key in channels if ".Object Index." in key or ".IndexOB." in key]
        if not index_keys:
            raise RuntimeError(f"EXR に Object Index のパスが無い（あるもの: {sorted(channels)}）")
        plant_index = np.where(alpha > 0.5, np.rint(channels[index_keys[0]]), 0.0).astype(np.float32)
        if not (plant_index == R.PLANT_LEAF_PASS_INDEX).any() or not (plant_index == R.PLANT_BRANCH_PASS_INDEX).any():
            raise RuntimeError("株の枝か葉が描画に映っていない（配置かカメラが違う）")
        # 葉の裏の見え方: 白い透過の面を太陽だけ・直接光だけで描く。輝度 / (E⊥ × max(−n·l, 0) / π) が裏から見た太陽の見え方
        view_layer.material_override = make_translucent_white_material("BackVisibilityWhite")
        scene.cycles.seed = 0
        back_rgb = pass_rgb(render_and_read(scene, exr_tmp), "Combined")
        expected_back = float(lighting["sun_illuminance_normal_lux"]) * np.maximum(-n_dot_l, 0.0) / math.pi
        back_visibility = np.where(n_dot_l < -1e-3, GR.luminance(back_rgb) / np.maximum(expected_back, 1e-6),
                                   0.0).astype(np.float32)
    view_layer.material_override = None
    background.inputs["Strength"].default_value = 1.0
    set_gray_bounces(scene, GRAY_MAX_BOUNCES)
    aux = np.concatenate([visibility[..., None], n_dot_l[..., None], normal, alpha[..., None]], axis=-1)
    write_exr(prefix + ".aux.exr", aux, ("visibility", "n_dot_l", "normal.X", "normal.Y", "normal.Z", "A"))
    GR.write_pfm(prefix + ".aux.visibility.pfm", visibility)
    GR.write_pfm(prefix + ".aux.n_dot_l.pfm", n_dot_l)
    GR.write_pfm(prefix + ".aux.normal.pfm", normal)
    GR.write_pfm(prefix + ".aux.alpha.pfm", alpha)

    # 2. 領域（灰色の地形: エンジンの深度と Cycles の法線・見え方から。建物: Cycles の深度・物体の番号・法線・見え方から）
    if building:
        depth = render_view_depth(scene, exr_tmp, background)
        GR.write_pfm(prefix + ".aux.depth.pfm", depth)
        GR.write_pfm(prefix + ".aux.building.pfm", building_mask.astype(np.float32))
        masks, region_record = R.region_masks(job["camera"], depth, building_mask, normal[..., 1], n_dot_l, visibility,
                                              alpha > 0.5, building["placement"], building["bounds"])
        building_record["regions"] = region_record
    elif plant:
        depth = render_view_depth(scene, exr_tmp, background)
        engine_depth = GR.read_pfm(job["engine_depth_pfm"])[..., 0]
        if engine_depth.shape != (height, width):
            raise RuntimeError(f"エンジンの深度の大きさ {engine_depth.shape} が描画 {height}x{width} と違う")
        GR.write_pfm(prefix + ".aux.depth.pfm", depth)
        GR.write_pfm(prefix + ".aux.plant.pfm", plant_index)
        GR.write_pfm(prefix + ".aux.back_visibility.pfm", back_visibility)
        masks, region_record = R.region_masks(job["camera"], depth, engine_depth, plant_index, n_dot_l, visibility,
                                              back_visibility, alpha > 0.5, plant["placement"])
        plant_record["regions"] = region_record
    else:
        depth = GR.read_pfm(job["engine_depth_pfm"])[..., 0]
        if depth.shape != (height, width):
            raise RuntimeError(f"エンジンの深度の大きさ {depth.shape} が描画 {height}x{width} と違う")
        masks = GR.region_masks(depth, normal[..., 1], n_dot_l, visibility, alpha > 0.5)
    print(f"BLENDER_GRAY view={job['view']} regions=" +
          json.dumps({name: int(masks[name].sum()) for name in R.REGIONS}), flush=True)

    # 切り出し（job の crop）: 名指しの領域の画素を囲む枠だけを描き、領域は枠の中に限る。ライトツリーを切る
    crop = job.get("crop")
    crop_record = None
    if crop:
        targets = list(crop["regions"])
        target = np.zeros((height, width), dtype=bool)
        for name in targets:
            target |= masks[name]
        if not target.any():
            raise RuntimeError(f"切り出しの領域 {targets} に画素が無い")
        rows = np.nonzero(target.any(axis=1))[0]
        columns = np.nonzero(target.any(axis=0))[0]
        pad = int(crop["pad_px"])
        x0, x1 = max(0, int(columns[0]) - pad), min(width - 1, int(columns[-1]) + pad)
        y0, y1 = max(0, int(rows[0]) - pad), min(height - 1, int(rows[-1]) + pad)
        # Blender の枠は左下が 0、右上が 1（画像の行は上が 0）
        scene.render.use_border = True
        scene.render.use_crop_to_border = False
        scene.render.border_min_x, scene.render.border_max_x = x0 / width, (x1 + 1) / width
        scene.render.border_min_y, scene.render.border_max_y = 1.0 - (y1 + 1) / height, 1.0 - y0 / height
        inside = np.zeros((height, width), dtype=bool)
        inside[y0:y1 + 1, x0:x1 + 1] = True
        masks = {name: masks[name] & inside for name in GR.REGIONS}
        scene.cycles.use_light_tree = False
        crop_record = {"regions": targets, "border_px": [x0, y0, x1, y1], "pad_px": pad,
                       "border_pixels": int(inside.sum()), "target_pixels": int(target.sum()),
                       "max_total_seconds": float(crop["max_total_seconds"]), "light_tree": False,
                       "direct_seed_offset": int(crop["direct_seed_offset"])}
        print(f"BLENDER_GRAY view={job['view']} crop border=({x0},{y0})-({x1},{y1}) pixels={int(inside.sum())} "
              f"target_pixels={int(target.sum())}", flush=True)

    # 3. 本描画を CHUNK_SPP ずつ種を変えて重ね、偶数番目を A・奇数番目を B に足す（切り出しでは種ごとに直接光だけの描画も足す）。
    # 切り出しは 1 回の起動で 1 本（crop の seed）だけを描く: A は時間の上限まで描いて途中の和を書き、B は A と同じサンプル数まで
    # 描いて A の和と合わせる（収束は合わせた後に測る）
    groups = ("combined", "sun", "sky", "direct", "indirect", "indirect_diffuse") + \
        (("sun_direct", "sky_direct") if crop else ()) + (("diffuse_color",) if building else ())
    sums = {seed: {g: np.zeros((height, width, 3), dtype=np.float64) for g in groups} for seed in ("a", "b")}
    renders = {"a": 0, "b": 0}
    seed_order = (crop["seed"],) if crop else ("a", "b")
    target_spp = int(crop.get("target_spp") or 0) if crop else 0
    chunk_spp = int(job["chunk_spp"])
    max_spp = int(job["max_spp_per_seed"])
    max_seconds = float(job["max_seconds"])
    scene.cycles.samples = chunk_spp
    history = []
    loop_started = time.time()
    chunk = 0
    pair_seconds = 0.0
    stats = {}
    all_converged = False
    stop_reason = ""
    # 切り出しの時間の上限は最初の 1 回の描画（BVH の構築を含む）の後から数える
    first_render_seconds = None
    sampling_started = None
    while True:
        pair_started = time.time()
        for seed in seed_order:
            # 種の番号は A が 1, 3, 5…・B が 2, 4, 6…（1 本ずつ描く切り出しでも同じ番号になる）
            seed_index = 2 * renders[seed] + (0 if seed == "a" else 1) if crop else chunk
            scene.cycles.seed = 1 + seed_index
            channels = render_and_read(scene, exr_tmp)
            parts = {name: pass_rgb(channels, name) for name in
                     ("Combined", "Combined_sun", "Combined_sky", "Diffuse Direct", "Diffuse Indirect",
                      "Diffuse Color", "Glossy Direct", "Glossy Indirect", "Glossy Color") +
                     (("Transmission Direct", "Transmission Indirect", "Transmission Color") if plant else ())}
            values = {
                "combined": parts["Combined"],
                "sun": parts["Combined_sun"],
                "sky": parts["Combined_sky"],
                "direct": parts["Diffuse Direct"] * parts["Diffuse Color"] +
                          parts["Glossy Direct"] * parts["Glossy Color"],
                "indirect": parts["Diffuse Indirect"] * parts["Diffuse Color"] +
                            parts["Glossy Indirect"] * parts["Glossy Color"],
                # 間接光のうち拡散の分（エンジンの RTGI の照り返しと同じ物。ReferenceCheck.py --bounce が比べる）
                "indirect_diffuse": parts["Diffuse Indirect"] * parts["Diffuse Color"],
            }
            if building:
                # 拡散の色（照明の無い基本色。画素の中の標本の平均なので、線形の値を画素の広さで平均したもの）
                values["diffuse_color"] = parts["Diffuse Color"]
            if plant:
                values["direct"] = values["direct"] + parts["Transmission Direct"] * parts["Transmission Color"]
                values["indirect"] = values["indirect"] + parts["Transmission Indirect"] * parts["Transmission Color"]
            if crop:
                # 枠が名指しの領域を覆うこと（描いた画素の被覆）
                if chunk == 0 and (channels["ViewLayer.Combined.A"][target] < 0.5).any():
                    raise RuntimeError("切り出しの枠が名指しの領域の画素を覆っていない")
                # 直接光だけ（跳ね返り 0）を別の種で描き、太陽の組・空の組を太陽の直接光・空の直接光にする
                set_gray_bounces(scene, 0)
                scene.cycles.seed = int(crop["direct_seed_offset"]) + seed_index
                direct_channels = render_and_read(scene, exr_tmp)
                set_gray_bounces(scene, GRAY_MAX_BOUNCES)
                values["sun_direct"] = pass_rgb(direct_channels, "Combined_sun")
                values["sky_direct"] = pass_rgb(direct_channels, "Combined_sky")
            for group in groups:
                sums[seed][group] += values[group]
            renders[seed] += 1
            chunk += 1
        pair_seconds = time.time() - pair_started
        if crop:
            spp = renders[crop["seed"]] * chunk_spp
            elapsed = time.time() - loop_started
            if sampling_started is None:
                first_render_seconds = pair_seconds
                sampling_started = time.time()
            history.append({"seed": crop["seed"], "spp_per_seed": spp, "elapsed_seconds": elapsed})
            print(f"BLENDER_GRAY view={job['view']} seed={crop['seed']} spp={spp} elapsed={elapsed:.0f}s", flush=True)
            if target_spp and spp >= target_spp:
                stop_reason = "target_spp"
                break
            if spp + chunk_spp > max_spp:
                stop_reason = "max_spp"
                break
            if time.time() - sampling_started + pair_seconds > crop_record["max_total_seconds"]:
                stop_reason = "max_seconds"
                break
            continue
        per_seed = chunk // 2
        spp = per_seed * chunk_spp
        y_a = GR.luminance(sums["a"]["combined"] / per_seed)
        y_b = GR.luminance(sums["b"]["combined"] / per_seed)
        stats, all_converged = R.convergence(y_a, y_b, masks)
        elapsed = time.time() - loop_started
        history.append({"spp_per_seed": spp, "elapsed_seconds": elapsed, "regions": stats})
        print(f"BLENDER_GRAY view={job['view']} spp_per_seed={spp} elapsed={elapsed:.0f}s all_converged={int(all_converged)} " +
              " ".join(f"{name}={stats[name].get('mean_diff', float('nan')):.4f}/{stats[name].get('pixel_rms', float('nan')):.3f}"
                       for name in R.REGIONS), flush=True)
        if all_converged and spp >= MIN_SPP_PER_SEED:
            stop_reason = "converged"
            break
        if spp + chunk_spp > max_spp:
            stop_reason = "max_spp"
            break
        if elapsed + pair_seconds > max_seconds:
            stop_reason = "max_seconds"
            break
    render_seconds = time.time() - loop_started

    seed_runs = None
    if crop:
        run_record = {"seed": crop["seed"], "renders": renders[crop["seed"]], "chunk_spp": chunk_spp,
                      "spp": renders[crop["seed"]] * chunk_spp, "stop_reason": stop_reason, "load_seconds": load_seconds,
                      "visibility_seconds": visibility_seconds, "render_seconds": render_seconds,
                      "first_render_seconds": first_render_seconds,
                      "sampling_seconds": time.time() - sampling_started if sampling_started else 0.0,
                      "total_seconds": time.time() - started, "history": history}
        if crop["seed"] == "a":
            # A: 途中の和（各組の平均）と記録を書いて終える。合わせるのは B の起動
            np.savez(prefix + ".cycles.part_a.npz", **{g: (sums["a"][g] / max(renders["a"], 1)).astype(np.float32)
                                                       for g in groups})
            with open(prefix + ".cycles.part_a.json", "w", encoding="utf-8") as f:
                json.dump(dict(run_record, crop=crop_record,
                               part_sha256=sha256_file(prefix + ".cycles.part_a.npz")), f, indent=2, ensure_ascii=False)
            print(f"BLENDER_GRAY view={job['view']} part seed=a spp={run_record['spp']} stop={stop_reason} "
                  f"load={load_seconds:.1f}s render={render_seconds:.1f}s", flush=True)
            return
        # B: A の途中の和を読み、枠・描画の条件が同じことを確かめて合わせる
        with open(crop["merge_from"], encoding="utf-8") as f:
            part_a = json.load(f)
        npz_path = os.path.splitext(crop["merge_from"])[0] + ".npz"
        if sha256_file(npz_path) != part_a["part_sha256"] or part_a["crop"]["border_px"] != crop_record["border_px"] or \
                int(part_a["chunk_spp"]) != chunk_spp:
            raise RuntimeError("種 A の途中の和が記録と違うか、枠・1 回のサンプル数が B と違う")
        with np.load(npz_path) as data:
            for g in groups:
                sums["a"][g] = data[g].astype(np.float64) * int(part_a["renders"])
        renders["a"] = int(part_a["renders"])
        seed_runs = {"a": {k: v for k, v in part_a.items() if k not in ("crop",)}, "b": run_record}
        load_seconds = {"a": part_a["load_seconds"], "b": load_seconds}
        visibility_seconds = {"a": part_a["visibility_seconds"], "b": visibility_seconds}
        render_seconds = {"a": part_a["render_seconds"], "b": render_seconds}
        stop_reason = f"a={part_a['stop_reason']},b={stop_reason}"
        history = part_a["history"] + history

    # 4. 書き出し: 2 本の平均（各組）と、A・B の合成（切り出しでは 2 本のサンプル数が違うことがあるので数で重みを付ける）
    per_seed = min(renders["a"], renders["b"])
    final = {g: ((sums["a"][g] + sums["b"][g]) / float(renders["a"] + renders["b"])).astype(np.float32) for g in groups}
    seed_a = (sums["a"]["combined"] / renders["a"]).astype(np.float32)
    seed_b = (sums["b"]["combined"] / renders["b"]).astype(np.float32)
    if crop:
        # 名指しの領域がすべて収束したか（画素が下限に足りない領域は収束しない）
        stats, _ = GR.convergence(GR.luminance(seed_a), GR.luminance(seed_b), masks)
        all_converged = all(stats[name]["converged"] for name in crop_record["regions"])
        print(f"BLENDER_GRAY view={job['view']} merged spp_per_seed={per_seed * chunk_spp} "
              f"all_converged={int(all_converged)} " +
              " ".join(f"{name}={stats[name].get('mean_diff', float('nan')):.4f}/"
                       f"{stats[name].get('pixel_rms', float('nan')):.3f}" for name in crop_record["regions"]),
              flush=True)
    surface = alpha > 0.5
    combined_y = GR.luminance(final["combined"])[surface]
    split_y = GR.luminance(final["direct"] + final["indirect"])[surface]
    groups_y = GR.luminance(final["sun"] + final["sky"])[surface]
    consistency = {
        "direct_plus_indirect_over_combined": float(split_y.sum() / max(combined_y.sum(), 1e-30)),
        "sun_plus_sky_over_combined": float(groups_y.sum() / max(combined_y.sum(), 1e-30)),
    }
    if crop:
        # 太陽の直接光 + 空の直接光（直接光だけの描画）+ 間接光（本描画）を、名指しの領域ごとに合成と照らす
        def three_way(mask):
            whole = GR.luminance(final["combined"])[mask].sum()
            parts_y = GR.luminance(final["sun_direct"] + final["sky_direct"] + final["indirect"])[mask].sum()
            return float(parts_y / max(whole, 1e-30))
        consistency["sun_direct_plus_sky_direct_plus_indirect_over_combined"] = three_way(target)
        consistency["regions"] = {name: three_way(masks[name]) for name in crop_record["regions"] if masks[name].any()}
    names = []
    planes = []
    for group in groups:
        for index, component in enumerate(("R", "G", "B")):
            names.append(component if group == "combined" else f"{group}.{component}")
            planes.append(final[group][..., index])
    write_exr(prefix + ".cycles.exr", np.stack(planes, axis=-1), names)
    write_exr(prefix + ".cycles.seed_a.exr", seed_a, ("R", "G", "B"))
    write_exr(prefix + ".cycles.seed_b.exr", seed_b, ("R", "G", "B"))
    GR.write_pfm(prefix + ".cycles.hdr.pfm", final["combined"])
    GR.write_pfm(prefix + ".cycles.seed_a.pfm", seed_a)
    GR.write_pfm(prefix + ".cycles.seed_b.pfm", seed_b)
    for group in groups[1:]:
        GR.write_pfm(prefix + f".cycles.{group}.pfm", final[group])
    outputs = {}
    for suffix in (".cycles.exr", ".cycles.seed_a.exr", ".cycles.seed_b.exr", ".aux.exr", ".cycles.hdr.pfm",
                   ".cycles.seed_a.pfm", ".cycles.seed_b.pfm", ".cycles.sun.pfm", ".cycles.sky.pfm",
                   ".cycles.direct.pfm", ".cycles.indirect.pfm", ".cycles.indirect_diffuse.pfm", ".aux.visibility.pfm", ".aux.n_dot_l.pfm",
                   ".aux.normal.pfm", ".aux.alpha.pfm") + \
            ((".cycles.sun_direct.pfm", ".cycles.sky_direct.pfm") if crop else ()) + \
            ((".aux.depth.pfm", ".aux.building.pfm", ".cycles.diffuse_color.pfm") if building else ()) + \
            ((".aux.depth.pfm", ".aux.plant.pfm", ".aux.back_visibility.pfm") if plant else ()):
        outputs[os.path.basename(prefix) + suffix] = sha256_file(prefix + suffix)
    meta = {
        "view": job["view"],
        "renderer": f"Blender {bpy.app.version_string} Cycles",
        "device": device,
        "width": width,
        "height": height,
        "camera_engine": job["camera"],
        "camera_blender_matrix_world": matrix,
        "camera_vertical_fov_degrees": math.degrees(vertical_fov),
        "tiles": len(job["tiles"]),
        "vertices": vertex_count,
        "triangles": triangle_count,
        "normals": "PLY の頂点の法線（custom_normal、点の領域）",
        "triangles_rewound": flipped_count,
        "material": material_record,
        "camera_shift_px": [float(v) for v in shift_px],
        "lighting": lighting,
        "sun_altitude_degrees": job["sun_altitude_degrees"],
        "sun_azimuth_degrees": job["sun_azimuth_degrees"],
        "settings": {"max_bounces": GRAY_MAX_BOUNCES, "pixel_filter": {"type": "BOX", "width_px": GRAY_PIXEL_FILTER_WIDTH},
                     "clamp": 0.0, "denoise": False, "adaptive": False, "chunk_spp": chunk_spp,
                     "min_spp_per_seed": MIN_SPP_PER_SEED, "max_spp_per_seed": max_spp, "max_seconds": max_seconds,
                     "visibility_spp": VISIBILITY_SPP},
        "seeds": {"a": [1 + 2 * k for k in range(renders["a"])], "b": [2 + 2 * k for k in range(renders["b"])]},
        "spp_per_seed": per_seed * chunk_spp,
        "stop_reason": stop_reason,
        "all_converged": bool(all_converged),
        "regions": stats,
        "region_criteria": R.criteria(),
        "history": history,
        "consistency": consistency,
        "load_seconds": load_seconds,
        "visibility_seconds": visibility_seconds,
        "render_seconds": render_seconds,
        "outputs_sha256": outputs,
    }
    if crop:
        meta["crop"] = crop_record
        meta["settings"]["light_tree"] = False
        meta["seed_runs"] = seed_runs
    if building:
        meta["building"] = building_record
        meta["depth"] = {"meaning": "view_axis_distance", "units": "m", "sky_value": "inf", "samples": 1,
                         "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH},
                         "depth_source": "emission of Camera Data View Z Depth"}
    if plant:
        meta["plant"] = plant_record
        meta["depth"] = {"meaning": "view_axis_distance", "units": "m", "sky_value": "inf", "samples": 1,
                         "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH},
                         "depth_source": "emission of Camera Data View Z Depth"}
        meta["settings"]["transmission_passes"] = True
    with open(prefix + ".cycles.json", "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, ensure_ascii=False)
    if crop:
        print(f"BLENDER_GRAY view={job['view']} done spp_per_seed={per_seed * chunk_spp} stop={stop_reason} "
              f"load={json.dumps(load_seconds)} render={json.dumps(render_seconds)} consistency={json.dumps(consistency)}",
              flush=True)
        return
    print(f"BLENDER_GRAY view={job['view']} done spp_per_seed={per_seed * chunk_spp} stop={stop_reason} "
          f"load={load_seconds:.1f}s render={render_seconds:.1f}s consistency={json.dumps(consistency)}", flush=True)


def make_cloud_material(extinction, phase):
    """雲の体積の材質: 格子 density × 消散係数を、位相関数の 2 つの HG の重みで 2 つの Volume Scatter（白、吸収なし）に分けて足す"""
    import bpy
    material = bpy.data.materials.new("CloudVolume")
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    nodes.clear()
    attribute = nodes.new("ShaderNodeAttribute")
    attribute.attribute_type = "GEOMETRY"
    attribute.attribute_name = "density"
    add = nodes.new("ShaderNodeAddShader")
    output = nodes.new("ShaderNodeOutputMaterial")
    backward_weight = float(phase["backward_weight"])
    lobes = ((float(phase["g_forward"]), 1.0 - backward_weight), (float(phase["g_backward"]), backward_weight))
    for index, (anisotropy, weight) in enumerate(lobes):
        scale = nodes.new("ShaderNodeMath")
        scale.operation = "MULTIPLY"
        scale.inputs[1].default_value = float(extinction) * weight
        links.new(attribute.outputs["Fac"], scale.inputs[0])
        scatter = nodes.new("ShaderNodeVolumeScatter")
        scatter.phase = "HENYEY_GREENSTEIN"
        # Volume Scatter の色の既定は 0.8（散乱の係数 = 密度 × 色）なので白にする
        scatter.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
        scatter.inputs["Anisotropy"].default_value = anisotropy
        links.new(scale.outputs["Value"], scatter.inputs["Density"])
        links.new(scatter.outputs["Volume"], add.inputs[index])
    links.new(add.outputs["Shader"], output.inputs["Volume"])
    return material


def make_lut_world(scene, lut):
    """空の放射輝度 LUT（[行, 列, RGBA]、正距円筒）を、エンジンの SampleSky と同じ引き方で引く World。
    エンジンの向き (X, Y, Z) = (x, z, −y)（Blender の視線の向き）で、u = atan2(Z, X) / 2π + 0.5、v = asin(−Y) / π + 0.5。
    画像の行は LUT の行の順に置く（Blender の画像の v = (行 + 0.5) / 高さ が LUT の行）。横は繰り返し、縦は端の texel の中心で止める"""
    import bpy
    height, width = lut.shape[:2]
    image = bpy.data.images.new("SkyRadianceLut", width=width, height=height, alpha=True, float_buffer=True)
    image.colorspace_settings.name = "Non-Color"
    image.pixels.foreach_set(np.ascontiguousarray(lut, dtype=np.float32).ravel())
    world = bpy.data.worlds.new("CloudSky")
    world.use_nodes = True
    nodes = world.node_tree.nodes
    links = world.node_tree.links
    nodes.clear()
    coordinate = nodes.new("ShaderNodeTexCoord")
    unit = nodes.new("ShaderNodeVectorMath")
    unit.operation = "NORMALIZE"
    links.new(coordinate.outputs["Generated"], unit.inputs[0])
    separate = nodes.new("ShaderNodeSeparateXYZ")
    links.new(unit.outputs["Vector"], separate.inputs[0])

    def math_node(operation, *inputs):
        node = nodes.new("ShaderNodeMath")
        node.operation = operation
        for slot, value in enumerate(inputs):
            if isinstance(value, (int, float)):
                node.inputs[slot].default_value = float(value)
            else:
                links.new(value, node.inputs[slot])
        return node.outputs["Value"]

    engine_z = math_node("MULTIPLY", separate.outputs["Y"], -1.0)
    u = math_node("MULTIPLY_ADD", math_node("ARCTAN2", engine_z, separate.outputs["X"]), 0.5 / math.pi, 0.5)
    v = math_node("MULTIPLY_ADD", math_node("ARCSINE", math_node("MULTIPLY", separate.outputs["Z"], -1.0)),
                  1.0 / math.pi, 0.5)
    clamp = nodes.new("ShaderNodeClamp")
    clamp.inputs["Min"].default_value = 0.5 / height
    clamp.inputs["Max"].default_value = 1.0 - 0.5 / height
    links.new(v, clamp.inputs["Value"])
    combine = nodes.new("ShaderNodeCombineXYZ")
    links.new(u, combine.inputs["X"])
    links.new(clamp.outputs["Result"], combine.inputs["Y"])
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = image
    texture.interpolation = "Linear"
    texture.extension = "REPEAT"
    links.new(combine.outputs["Vector"], texture.inputs["Vector"])
    background = nodes.new("ShaderNodeBackground")
    background.inputs["Strength"].default_value = 1.0
    links.new(texture.outputs["Color"], background.inputs["Color"])
    output = nodes.new("ShaderNodeOutputWorld")
    links.new(background.outputs["Background"], output.inputs["Surface"])
    scene.world = world
    return world


def render_cloud_job(job):
    """雲の体積を 1 視点描く: 書き出しから密度の格子を焼き、空だけの画と、種を変えた本描画を収束まで重ねる"""
    import bpy
    import mathutils
    import openvdb

    GR = load_gray_regions()
    CV = load_cloud_volume()
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    device = setup_cycles_device(scene)
    prefix = job["prefix"]
    width, height = int(job["width"]), int(job["height"])
    pixels = job["pixels"]
    regions = {name: [int(i) for i in indices] for name, indices in job["regions"].items()}

    # 1. 密度の格子（OpenVDB の格子 density。値は格子の点、格子の点 (i, j, k) は格子の空間の (i, j, k) × 大きさ）
    started = time.time()
    meta, model, _ = CV.load_dump(job["capture_dir"], job["view"])
    grid = job["grid"]
    density = CV.bake_density(model, grid)
    bake_seconds = time.time() - started
    grid_record = {
        "dims": [int(n) for n in density.shape], "voxel_size_m": float(grid["voxel_size_m"]),
        "nonzero_voxels": int(np.count_nonzero(density)), "max_density": float(density.max()),
        "density_sum": float(density.sum(dtype=np.float64)),
        "density_sha256": hashlib.sha256(np.ascontiguousarray(density).tobytes()).hexdigest(),
        "bake_seconds": bake_seconds,
    }
    vdb = openvdb.FloatGrid()
    vdb.name = "density"
    vdb.copyFromArray(density, tolerance=0.0)
    vdb.transform = openvdb.createLinearTransform(voxelSize=float(grid["voxel_size_m"]))
    grid_record["active_voxels"] = int(vdb.activeVoxelCount())
    del density
    vdb_path = prefix + ".density.vdb"
    openvdb.write(vdb_path, grids=[vdb])
    del vdb
    volume = bpy.data.volumes.new("CloudDensity")
    volume.filepath = vdb_path
    volume.grids.load()
    volume.render.precision = "FULL"
    # 密度がこれ以下の格子の点は空（エンジンの視線も密度 0.001 以下の歩を飛ばす）
    volume.render.clipping = CV.CD.DENSITY_THRESHOLD
    lighting = job["lighting"]
    volume.materials.append(make_cloud_material(lighting["extinction_per_m"], lighting["phase"]))
    volume_object = bpy.data.objects.new("CloudDensity", volume)
    scene.collection.objects.link(volume_object)
    axis_i = engine_to_blender(grid["axes"]["i"])
    axis_j = engine_to_blender(grid["axes"]["j"])
    axis_k = engine_to_blender(grid["axes"]["k"])
    origin = engine_to_blender(grid["origin_frame"])
    matrix = mathutils.Matrix(((axis_i[0], axis_j[0], axis_k[0], origin[0]), (axis_i[1], axis_j[1], axis_k[1], origin[1]),
                               (axis_i[2], axis_j[2], axis_k[2], origin[2]), (0.0, 0.0, 0.0, 1.0)))
    if matrix.to_3x3().determinant() <= 0.0:
        raise RuntimeError("格子の軸が左手系（体積が裏返る）")
    volume_object.matrix_world = matrix

    # 2. 地面・太陽・空・カメラ
    half = CLOUD_GROUND_HALF_SIZE_M
    ground_mesh = bpy.data.meshes.new("CloudGround")
    ground_mesh.from_pydata([(-half, -half, 0.0), (half, -half, 0.0), (half, half, 0.0), (-half, half, 0.0)], [],
                            [(0, 1, 2, 3)])
    ground_material = bpy.data.materials.new("CloudGround")
    ground_material.use_nodes = True
    ground_nodes = ground_material.node_tree.nodes
    ground_nodes.clear()
    diffuse = ground_nodes.new("ShaderNodeBsdfDiffuse")
    albedo = [float(v) for v in lighting["ground_albedo"]]
    diffuse.inputs["Color"].default_value = (albedo[0], albedo[1], albedo[2], 1.0)
    diffuse.inputs["Roughness"].default_value = 0.0
    ground_output = ground_nodes.new("ShaderNodeOutputMaterial")
    ground_material.node_tree.links.new(diffuse.outputs["BSDF"], ground_output.inputs["Surface"])
    ground_mesh.materials.append(ground_material)
    ground = bpy.data.objects.new("CloudGround", ground_mesh)
    scene.collection.objects.link(ground)

    sun_rgb = [float(v) for v in lighting["sun_illuminance_lux"]]
    sun_peak = max(sun_rgb)
    sun = make_sun_lamp(scene, sun_peak, lighting["sun_direction"], lighting["sun_angle_degrees"])
    sun.data.color = tuple(v / sun_peak for v in sun_rgb)
    make_lut_world(scene, model.sky_lut)

    frame = job["camera"]
    camera_matrix, vertical_fov = camera_to_blender({
        "position": frame["position_frame"], "right": frame["right"], "up": frame["up"], "forward": frame["forward"],
        "vertical_fov_degrees": frame["vertical_fov_degrees"]})
    camera = bpy.data.cameras.new("EngineCamera")
    camera.type = "PERSP"
    camera.sensor_fit = "VERTICAL"
    camera.angle_y = vertical_fov
    camera.clip_start = 0.1
    camera.clip_end = CLIP_END_M
    camera_object = bpy.data.objects.new("EngineCamera", camera)
    scene.collection.objects.link(camera_object)
    camera_object.matrix_world = mathutils.Matrix(camera_matrix)
    scene.camera = camera_object

    exr_tmp = prefix + ".render.exr"
    configure_gray_cycles(scene, width, height, exr_tmp)
    cycles = scene.cycles
    # 視線の画素の値と比べるので、画素の中の標本を画素の中心に寄せる
    cycles.filter_width = PIXEL_FILTER_WIDTH
    cycles.max_bounces = CLOUD_VOLUME_BOUNCES + CLOUD_DIFFUSE_BOUNCES
    cycles.diffuse_bounces = CLOUD_DIFFUSE_BOUNCES
    cycles.glossy_bounces = 0
    cycles.transmission_bounces = 0
    cycles.volume_bounces = CLOUD_VOLUME_BOUNCES
    cycles.transparent_max_bounces = 8
    load_seconds = time.time() - started

    # 3. 空だけの画（雲を隠し、背景を描く。空の引き方とカメラの視線の照合に使う）
    volume_object.hide_render = True
    scene.render.film_transparent = False
    cycles.samples = CLOUD_SKY_SPP
    cycles.seed = 0
    sky = pass_rgb(render_and_read(scene, exr_tmp), "Combined")
    GR.write_pfm(prefix + ".aux.sky.pfm", sky)
    volume_object.hide_render = False
    scene.render.film_transparent = True

    # 4. 透過率の画: 同じ格子・同じ消散係数の吸収だけの体積を、光線の歩で積む描き方（volume_biased）で描き、1 − α を書く
    #    （置き方・密度・消散係数の照合に使う。散乱が無いので α は標本の揺らぎをほとんど持たない）
    scatter_material = volume.materials[0]
    absorb_material = bpy.data.materials.new("CloudAbsorption")
    absorb_material.use_nodes = True
    absorb_nodes = absorb_material.node_tree.nodes
    absorb_nodes.clear()
    absorb_attribute = absorb_nodes.new("ShaderNodeAttribute")
    absorb_attribute.attribute_type = "GEOMETRY"
    absorb_attribute.attribute_name = "density"
    absorb_scale = absorb_nodes.new("ShaderNodeMath")
    absorb_scale.operation = "MULTIPLY"
    absorb_scale.inputs[1].default_value = float(lighting["extinction_per_m"])
    absorption = absorb_nodes.new("ShaderNodeVolumeAbsorption")
    # 吸収の係数 = 密度 × (1 − 色) なので黒にする
    absorption.inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
    absorb_output = absorb_nodes.new("ShaderNodeOutputMaterial")
    absorb_links = absorb_material.node_tree.links
    absorb_links.new(absorb_attribute.outputs["Fac"], absorb_scale.inputs[0])
    absorb_links.new(absorb_scale.outputs["Value"], absorption.inputs["Density"])
    absorb_links.new(absorption.outputs["Volume"], absorb_output.inputs["Volume"])
    volume.materials[0] = absorb_material
    cycles.volume_biased = True
    cycles.samples = CLOUD_TRANSMITTANCE_SPP
    cycles.seed = 0
    transmittance = 1.0 - render_and_read(scene, exr_tmp)["ViewLayer.Combined.A"]
    GR.write_pfm(prefix + ".aux.transmittance.pfm", transmittance)
    volume.materials[0] = scatter_material
    cycles.volume_biased = False

    # 5. 本描画: 種を変えて A・B に重ね、視線の領域で収束を測る
    chunk_spp = int(job["chunk_spp"])
    max_spp = int(job["max_spp_per_seed"])
    max_seconds = float(job["max_seconds"])
    cycles.samples = chunk_spp
    sums = {seed: np.zeros((height, width, 4), dtype=np.float64) for seed in ("a", "b")}
    chunk = 0
    history = []
    stop_reason = None
    loop_started = time.time()
    while True:
        for seed in ("a", "b"):
            cycles.seed = 1 + chunk
            sums[seed] += pass_rgb(render_and_read(scene, exr_tmp), "Combined", ("R", "G", "B", "A"))
            chunk += 1
        per_seed = chunk // 2
        spp = per_seed * chunk_spp
        y_a = GR.luminance(sums["a"][..., :3] / per_seed)
        y_b = GR.luminance(sums["b"][..., :3] / per_seed)
        stats, all_converged = CV.convergence(y_a, y_b, pixels, regions)
        elapsed = time.time() - loop_started
        history.append({"spp_per_seed": spp, "elapsed_seconds": elapsed, "regions": stats})
        print(f"BLENDER_CLOUD view={job['view']} spp_per_seed={spp} elapsed={elapsed:.0f}s "
              f"all_converged={int(all_converged)} " +
              " ".join(f"{name}={stats[name].get('mean_diff', float('nan')):.4f}/{stats[name].get('pixel_rms', float('nan')):.3f}"
                       for name in CV.REGIONS), flush=True)
        if all_converged and spp >= MIN_SPP_PER_SEED:
            stop_reason = "converged"
            break
        if spp + chunk_spp > max_spp:
            stop_reason = "max_spp"
            break
        if elapsed + elapsed / per_seed > max_seconds:
            stop_reason = "max_seconds"
            break
    render_seconds = time.time() - loop_started
    if os.path.exists(vdb_path):
        os.remove(vdb_path)

    per_seed = chunk // 2
    seed_a = sums["a"] / per_seed
    seed_b = sums["b"] / per_seed
    GR.write_pfm(prefix + ".cycles.seed_a.pfm", seed_a[..., :3])
    GR.write_pfm(prefix + ".cycles.seed_b.pfm", seed_b[..., :3])
    GR.write_pfm(prefix + ".cycles.hdr.pfm", 0.5 * (seed_a[..., :3] + seed_b[..., :3]))
    GR.write_pfm(prefix + ".cycles.alpha_a.pfm", seed_a[..., 3])
    GR.write_pfm(prefix + ".cycles.alpha_b.pfm", seed_b[..., 3])
    GR.write_pfm(prefix + ".cycles.alpha.pfm", 0.5 * (seed_a[..., 3] + seed_b[..., 3]))
    meta_record = {
        "view": job["view"],
        "renderer": f"Blender {bpy.app.version_string} Cycles",
        "device": device,
        "width": width,
        "height": height,
        "camera": frame,
        "grid": grid,
        "grid_record": grid_record,
        "lighting": lighting,
        "settings": {"volume_bounces": CLOUD_VOLUME_BOUNCES, "diffuse_bounces": CLOUD_DIFFUSE_BOUNCES,
                     "max_bounces": CLOUD_VOLUME_BOUNCES + CLOUD_DIFFUSE_BOUNCES, "volume_biased": bool(cycles.volume_biased),
                     "volume_precision": volume.render.precision, "volume_clipping": float(volume.render.clipping),
                     "pixel_filter": {"type": "BOX", "width_px": PIXEL_FILTER_WIDTH}, "film_transparent": True,
                     "clamp": 0.0, "denoise": False, "adaptive": False, "chunk_spp": chunk_spp,
                     "min_spp_per_seed": MIN_SPP_PER_SEED, "max_spp_per_seed": max_spp, "max_seconds": max_seconds,
                     "sky_spp": CLOUD_SKY_SPP, "ground_half_size_m": CLOUD_GROUND_HALF_SIZE_M,
                     "transmittance_pass": {"material": "Volume Absorption（密度 × 消散係数）", "volume_biased": True,
                                            "spp": CLOUD_TRANSMITTANCE_SPP},
                     "sun_color": list(sun.data.color), "sun_strength": float(sun.data.energy)},
        "regions": regions,
        "spp_per_seed": per_seed * chunk_spp,
        "stop_reason": stop_reason,
        "all_converged": bool(history[-1]["regions"] and all(history[-1]["regions"][n]["converged"] for n in CV.REGIONS)),
        "trial": bool(job.get("trial")),
        "load_seconds": load_seconds,
        "render_seconds": render_seconds,
        "history": history,
    }
    with open(prefix + ".cycles.json", "w", encoding="utf-8") as f:
        json.dump(meta_record, f, indent=2, ensure_ascii=False)
    print(f"BLENDER_CLOUD view={job['view']} done spp_per_seed={per_seed * chunk_spp} stop={stop_reason} "
          f"bake={bake_seconds:.1f}s load={load_seconds:.1f}s render={render_seconds:.1f}s "
          f"active_voxels={grid_record['active_voxels']}", flush=True)


def main():
    if IN_BLENDER:
        argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
        parser = argparse.ArgumentParser(description="Blender の中で 1 視点を描く")
        parser.add_argument("--render-job", required=True)
        args = parser.parse_args(argv)
        render_job(args.render_job)
        return 0
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--depth", action="store_true", help="地形の深度を描く")
    parser.add_argument("--gray", action="store_true", help="灰色の地形を共通の照明で描く（板の較正と視点）")
    parser.add_argument("--mesh", default="", help="Game の --wasteland-export-terrain が書いたフォルダ（--cloud では使わない）")
    parser.add_argument("--capture", required=True, help="エンジンの撮影（CaptureScene.ps1 -Aov の -OutDir）")
    parser.add_argument("--views", default="", help="描く視点（カンマ区切り。--depth で省くと metrics.json の全視点、"
                                                    "--gray で省くと " + ",".join(GRAY_VIEWS) + "）")
    parser.add_argument("--range", default=None, choices=("frustum", "shadow", "all"),
                        help="読む区画の範囲（既定は --depth が frustum、--gray が shadow）")
    parser.add_argument("--blender", default=DEFAULT_BLENDER, help="blender.exe")
    parser.add_argument("--chunk-spp", type=int, default=CHUNK_SPP, help="--gray: 1 回の描画のサンプル数")
    parser.add_argument("--max-spp", type=int, default=None,
                        help=f"--gray: 1 本あたりのサンプル数の上限（既定 {MAX_SPP_PER_SEED}、--crop-regions では {CROP_MAX_SPP_PER_SEED}）")
    parser.add_argument("--max-seconds", type=float, default=MAX_SECONDS_PER_VIEW, help="--gray: 1 視点の本描画の時間の上限")
    parser.add_argument("--skip-plate", action="store_true", help="--gray: 板の較正を描かない")
    parser.add_argument("--plate-only", action="store_true", help="--gray: 板の較正だけを描く")
    parser.add_argument("--albedo", action="store_true", help="地表の材質の照明の無いアルベドを描く")
    parser.add_argument("--normal", action="store_true", help="地表の材質の照明の無い法線と粗さを描く")
    parser.add_argument("--mips", default="",
                        help="--albedo・--normal・--lit: ミップの段の画像を置くフォルダ（省くと <撮影>/albedo/mips・"
                             "<撮影>/normal/mips・<撮影>/material/mips）")
    parser.add_argument("--lit", action="store_true", help="実材質の地形を共通の照明で描く（画素ごとの材質と Cycles）")
    parser.add_argument("--skip-material", action="store_true", help="--lit: 画素ごとの材質を描き直さない")
    parser.add_argument("--material-only", action="store_true", help="--lit: 画素ごとの材質だけを描く（Cycles で描かない）")
    parser.add_argument("--crop-regions", default="",
                        help="--lit: 名指しの領域（カンマ区切り。GrayRegions.REGIONS）だけを切り出して <撮影>/" + LIT_CROP_DIR +
                             "/ に描く（1 視点）")
    parser.add_argument("--crop-reuse-a", action="store_true",
                        help="--crop-regions: 記録と中身の合う種 A の途中の和を残し、種 B だけを描く")
    parser.add_argument("--building", action="store_true",
                        help="前哨地の建物 1 棟（撮影のログの配置）と灰色の地形を共通の照明で描く")
    parser.add_argument("--plant", action="store_true",
                        help="低木 1 株（撮影のログの配置と撮影のフォルダの PLY）と灰色の地形を共通の照明で描く")
    parser.add_argument("--cloud", action="store_true",
                        help="雲の書き出し（--capture に cloud-dump の撮影）の密度場を体積として、書き出しの太陽と空で描く")
    parser.add_argument("--trial", action="store_true",
                        help=f"--building・--plant・--cloud: 試しの描画（1 本 {BUILDING_TRIAL_SPP} サンプル。既定の書き先 "
                             f"<撮影>/{BUILDING_TRIAL_DIR}）")
    parser.add_argument("--out", default="", help=f"--building・--plant・--cloud: 書き先（省くと <撮影>/{BUILDING_RESULT_DIR}・"
                                                  f"<撮影>/{PLANT_RESULT_DIR}・<撮影>/{CLOUD_RESULT_DIR}）")
    args = parser.parse_args()
    if int(args.depth) + int(args.gray) + int(args.albedo) + int(args.normal) + int(args.lit) + int(args.building) + \
            int(args.plant) + int(args.cloud) != 1:
        parser.error("--depth・--gray・--albedo・--normal・--lit・--building・--plant・--cloud のどれか 1 つを指定してください")
    if args.cloud:
        args.views = args.views or ",".join(CLOUD_VIEWS)
        args.max_spp = args.max_spp or (CLOUD_TRIAL_SPP if args.trial else MAX_SPP_PER_SEED)
        return run_cloud(args)
    if not args.mesh:
        parser.error("--mesh を指定してください（--cloud のほかは地形のメッシュを読む）")
    if args.building:
        args.range = args.range or "shadow"
        args.views = args.views or ",".join(BUILDING_VIEWS)
        args.max_spp = args.max_spp or (BUILDING_TRIAL_SPP if args.trial else MAX_SPP_PER_SEED)
        return run_building(args)
    if args.plant:
        args.range = args.range or "shadow"
        args.views = args.views or ",".join(PLANT_VIEWS)
        args.max_spp = args.max_spp or (PLANT_TRIAL_SPP if args.trial else MAX_SPP_PER_SEED)
        return run_plant(args)
    if args.crop_reuse_a and not (args.crop_regions and args.skip_material):
        parser.error("--crop-reuse-a は --crop-regions と --skip-material と使う（材質を描き直すと A の途中の和は古い）")
    if args.crop_regions:
        GR = load_gray_regions()
        unknown = [r for r in args.crop_regions.split(",") if r not in GR.REGIONS]
        if not args.lit or unknown or len([v for v in args.views.split(",") if v]) != 1:
            parser.error(f"--crop-regions は --lit と 1 つの --views と使い、領域は {','.join(GR.REGIONS)} から選ぶ"
                         f"（知らない領域 {unknown}）")
    args.max_spp = args.max_spp or (CROP_MAX_SPP_PER_SEED if args.crop_regions else MAX_SPP_PER_SEED)
    if args.albedo or args.normal:
        args.views = args.views or ",".join(ALBEDO_VIEWS)
        return run_material(args, "albedo" if args.albedo else "normal")
    if args.lit:
        args.range = args.range or "shadow"
        args.views = args.views or ",".join(LIT_VIEWS)
        return run_gray(args, lit=True)
    if args.gray:
        args.range = args.range or "shadow"
        args.views = args.views or ",".join(GRAY_VIEWS)
        return run_gray(args)
    args.range = args.range or "frustum"
    return run_depth(args)


if __name__ == "__main__":
    status = main()
    if not IN_BLENDER:
        sys.exit(status)

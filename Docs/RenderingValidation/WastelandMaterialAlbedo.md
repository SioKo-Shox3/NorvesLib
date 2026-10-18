# 地表の材質のアルベドを Blender で再現し、照明の無い値を照合する

荒野の正解の絵（SCN-WLR-*）で、地形の地表の材質（砂・土・岩肌の 3 層の混ぜ合わせ）のアルベドの経路を Blender の Cycles の
OSL のシェーダーで写し、エンジンの GBuffer の生のアルベド（SCN-WLR-BARE-AOV の書き出し）と照明の無い値で比べた記録。
判定日: 2026-10-19。ブランチ `feature/scene-launcher`。タスク SCN-WLR-MATERIAL-ALBEDO。法線と粗さは SCN-WLR-MATERIAL-NORMAL で足す。

## 道具

| 何 | どこ | 何をするか |
|---|---|---|
| `WASTELAND_TERRAIN_MATERIAL` のログ | `Game/GameModes/Wasteland/WastelandRoutine.cpp` | 地形の材質を組み立てるときに、`WastelandRoutine.cpp` の定数から描画へ渡す混ぜ方の値（`MegaTerrainBlend`）・テクスチャのパス・地表の分類の地図と遠景の地図の範囲を 1 行の JSON で書く |
| `Scripts/Terrain/TerrainMaterialReference.py` | | 撮影のログの JSON から値の表 `terrain_material.json` を作る。シェーダーへ詰めた値（`TerrainBlendShading.h` の `PackTerrainBlendParams` と同じ式を float32 で）、GLSL の const（`TerrainLayerBlend.glsl`・`TerrainStrata.glsl` から式のまま読む）、テクスチャの全ミップの段の画像（クックの `BuildMipChain` と同じ作り方）を書き、OSL の本体の前に置く定数とテクスチャの関数を生成する |
| `Scripts/Terrain/TerrainAlbedo.osl` | | `ShadeTerrainLayers` と `ComputeTerrainStrata` のうちアルベドに効く所を同じ式・同じ順で写した OSL の本体 |
| `Scripts/Terrain/BlenderReference.py --albedo` | Blender 5.2.2 をヘッドレスで起動 | 視点ごとに、エンジンのカメラで書き出しの地形（SCN-WLR-BLENDER-TERRAIN の LOD 0 の PLY）を、OSL のアルベドを放射させて 1 サンプルで描く。`<撮影>/albedo/` にアルベド・頂点の法線・被覆の PFM と `inputs.json` を書く |
| `Scripts/Terrain/ReferenceCheck.py --albedo` | | エンジンの撮影（起動引数・ログ・撮った時点の常駐）と Blender の結果の入力（台本・OSL・値の表・ミップの段・メッシュのハッシュ、値の表が今の撮影のログ・詰め方の式・GLSL の定数と同じこと）を検査し、領域ごとの平均と模様を照合して、見比べの画像 `albedo_<視点>.png` を書く |

値の表は両方が同じものを使う: エンジンは `WastelandRoutine.cpp` の定数を描画へ渡し、同じ値をログへ書く。Blender の側はそのログから
表を作り、照合は表が今の撮影のログ・今の詰め方の式・今の GLSL の定数と一致することを確かめる（どれかが変わると古い結果として落ちる）。

## 再現した処理

- 三方向の投影（重み・切り捨て）と、距離の 2 つの段（倍率・回転・ずらし、段の間の混ぜ）。土と砂の横 2 枚の並びの枠（枠の内側へ畳む余白、勾配の半分）
- 地表の分類の地図（ずらし、外縁の帯、礫の舗石の斑、砂の割合）と、傾斜・高さ・ノイズの式。地図の外縁の帯での混ぜ
- 遠景の地図（分類の重みと法線、遠くで地図へ移す割合）
- 土の皮膜と地図の砂の高さの混ぜ（AO を高さに使う）、3 層の高さの混ぜ
- 層ごとの明るさと色味の揺らぎ（マクロの揺らぎ。画素より細かいノイズを平均へ寄せる処理を含む）
- 岩肌の地層（層の色・AO の倍率・交差層理の葉理の明暗・層群の色）と砂漠ワニス（崖の上端からの落差を地図で測る）
- 整数の格子のハッシュ（32 bit の積のあふれと論理の右送りを int で写す）
- 標本: クックと同じ作り方のミップの段（sRGB のアルベドは線形へ戻して箱の平均、8 bit へ丸め）を、画素の勾配から Vulkan の仕様の
  異方性の式（足跡の長い軸に沿って N ≤ 16 の標本、短い軸に合わせた段、段の間は線形、端は繰り返し）で引く。UNORM で張られた
  アルベドは標本の後に sRGB から線形へ戻す（エンジンと同じ順。フィルタは符号化した値の上で掛かる）
- 画素あたりの位置の変化（エンジンの `WorldDx`・`WorldDy`）は、可視バッファの解決と同じく隣の画素の中心を通る視線と三角形の面の交点との差

## 再現できない処理とその扱い

| 処理 | 扱い | 影響（測った値） |
|---|---|---|
| GPU（RTX 4080）の異方性の標本の実装 | 仕様の式に段の偏り +1 を足して近似する（`TerrainMaterialReference.py` の `ANISO_LOD_OFFSET`） | 下の「異方性の切り分け」。偏りが無いと ref_ground_01 の中景の青が 5.1 % 暗く、近景の細部が 1.15 倍 |
| BC7 の圧縮 | 再現しない（Blender は圧縮前の 8 bit の段を引く） | 等方の撮影との一致（平均 0.6 % 以内・細部の比 1.025）に含まれる大きさで、照合の基準に比べて小さい |
| 可視バッファの解決とラスタの経路の違い | 再現しない（ラスタの `megageometry.frag` は `dFdx`・`dFdy`） | どちらも 1 画素の差で、大きさは同じ。三角形の境の画素だけ違いうる（照合は輪郭の 3 画素を除く） |

### 異方性の切り分け

エンジンを地形のサンプラーの異方性を切った起動（`--terrain-anisotropy=1`、等方の線形）で ref_ground_01 を撮り、Blender を
異方性 1 の同じ式で描くと、近景・中景とも一致した。再現の残りの差は、異方性の標本の実装だけにある。

| エンジン | Blender | 近景の平均の比（R, G, B） | 近景の細部の比 | 中景の平均の比（R, G, B） | 中景の細部の比 |
|---|---|---|---|---|---|
| 等方 | 等方 | 0.996, 0.997, 0.997 | 1.025 | 0.999, 0.998, 0.994 | 1.012 |
| 異方性 16 | 仕様の式 | 0.996, 0.998, 0.998 | 1.153 | 0.992, 0.981, 0.949 | 1.047 |
| 異方性 16 | 仕様の式 + 0.5 段 | 0.998, 1.000, 1.001 | 1.079 | 0.995, 0.988, 0.964 | 1.021 |
| 異方性 16 | 仕様の式 + 1 段 | 0.999, 1.002, 1.004 | 0.984 | 0.999, 0.998, 0.989 | 0.986 |
| 異方性 16 | 異方性 1 | 1.001, 1.006, 1.009 | 0.779 | 0.999, 0.992, 0.971 | 1.007 |

比は Blender ÷ エンジン。細部は輝度から σ 3 画素のぼかしを引いた高域の標準偏差。Blender は画面の下 670 行だけを描いた。
GPU の異方性の標本は仕様の式より粗い段を読み、符号化した値の上で平均する標本が減るので、細部が減って平均が明るくなる
（暗い青で大きい）。標本の数を 2 のべき乗へ丸め足跡の長さを成分の絶対値の和で近似した式も試したが、近景の細部の比は 1.087 で、
一定の偏りの方が近い。偏りは近景の細部の比が 1 に最も近い 1 段に決めた（平均は決めるのに使っていない）。
ref_cliff_01 は決めるのに使っていない視点で、偏りを入れると崖の模様の SSIM が 0.865 から 0.992 へ上がった。

## 照合の測り方

- 撮影: 共通の照明・地形だけ（`--wasteland-hide=all`）・最細の LOD（`--wasteland-finest-lod`）、1920×1080、RelWithDebInfo、
  `-Aov`。撮った時点でジオメトリのページが常駐しきり、描くクラスタが溢れていないことを検査する
- Blender: Cycles の CPU と OSL、1 サンプル（画素の中心）、箱のフィルタ、跳ね返り 0。描いた深度とエンジンの深度から求めた画素の
  小数のずれが 0.05 画素を超えた視点は、そのずれだけカメラをずらして描き直す（ref_cliff_01 は横 +0.243・縦 +0.027 画素。
  `WastelandMaterialNormal.md` の「標本の位置の揃え」）
- 共通の表面: 両方が表面で、輪郭（空との境・深度の跳び）から 3 画素より離れた画素
- 領域: Blender の頂点の法線の上向きの成分 y とエンジンの深度で、平らな地面（y ≥ 0.8）を近景（30 m 未満）・中景（30〜300 m）・
  遠景（300 m 以上）に、斜面（0.5 ≤ y < 0.8）と崖（y < 0.5）に分ける。5000 画素未満の領域は判定しない
- 平均: R・G・B ごとの線形の平均の比（Blender ÷ エンジン）が 1 ± 0.05
- 局所の模様: 輝度を σ 1 画素でぼかし（標本の位置の 1 画素未満のずれを除く）、16 画素角の窓（8 画素おき、窓の全画素が領域の中）の
  SSIM（C1 = (0.01 × 0.3)²・C2 = (0.03 × 0.3)²）の平均が 0.7 以上。窓が 20 未満の領域は平均だけ判定する。
  対照として Blender の画を窓 1 つ分横へずらした SSIM も測り、0.6 未満の領域を「模様の比べ方が見分けられる領域」とする。
  視点ごとに見分けられる領域が 1 つ以上要る

## 結果

`python Scripts/Terrain/ReferenceCheck.py --albedo --capture .harness/runs/scene-capture/SCN-WLR-MATERIAL-ALBEDO --verify` は合格。

| 視点 | 領域 | 画素 | 深度の中央値 | 平均の比（R, G, B） | SSIM の平均 | 10 百分位 | ずらした対照 |
|---|---|---|---|---|---|---|---|
| ref_cliff_01 | 中景の地面 | 6543 | 295 m | 1.021, 1.012, 1.012 | 0.940 | 0.895 | 0.771 |
| ref_cliff_01 | 遠景の地面 | 469028 | 465 m | 1.008, 1.001, 0.996 | 0.965 | 0.907 | 0.568 |
| ref_cliff_01 | 斜面 | 90672 | 555 m | 1.006, 1.001, 1.001 | （窓 14。平均だけ） | | |
| ref_cliff_01 | 崖 | 206016 | 612 m | 1.000, 0.999, 1.000 | 0.996 | 0.990 | 0.456 |
| ref_ground_01 | 近景の地面 | 1183158 | 6 m | 0.999, 1.002, 1.004 | 0.995 | 0.990 | 0.454 |
| ref_ground_01 | 中景の地面 | 18923 | 55 m | 0.999, 0.998, 0.989 | 0.960 | 0.909 | 0.439 |
| ref_ground_01 | 遠景の地面 | 13451 | 1189 m | 1.009, 1.000, 0.995 | （窓 6。平均だけ） | | |
| ref_ground_01 | 斜面 | 12864 | 1301 m | 1.007, 1.005, 1.007 | 0.981 | 0.942 | 0.703 |

ref_cliff_01 の近景の地面と ref_ground_01 の崖は画素が無いか 5000 未満で判定していない。空と表面の分かれ方の食い違いは
ref_cliff_01 が 0.002 %、ref_ground_01 が 0.0004 %。最も大きい差は ref_cliff_01 の中景の地面の赤の +2.2 %。

見比べの画像 `albedo_<視点>.png` は、左上がエンジン・右上が Blender（同じ倍率）・左下が輝度の比の log2（±0.25 段で飽和。赤が
Blender の方が明るい）・右下が領域。どちらの視点も地層の筋・分類の斑・舗石の斑が同じ位置に出ている。

## 描き直しの手順

```
# エンジン（照合の読む撮影）
powershell -File Scripts/CaptureScene.ps1 -Scene wasteland -OutDir .harness/runs/scene-capture/SCN-WLR-MATERIAL-ALBEDO `
  -Views ref_cliff_01,ref_ground_01 -Configuration RelWithDebInfo -Build -Width 1920 -Height 1080 -Aov -LaunchPerView `
  -GameArgs @('--wasteland-common-light','--wasteland-hide=all','--wasteland-finest-lod')
# Blender（値の表・ミップの段・OSL を作り、2 視点を描く。ref_cliff_01 は約 2 分、ref_ground_01 は約 1 分）
python Scripts/Terrain/BlenderReference.py --albedo --mesh .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-mesh `
  --capture .harness/runs/scene-capture/SCN-WLR-MATERIAL-ALBEDO
# 照合
python Scripts/Terrain/ReferenceCheck.py --albedo --capture .harness/runs/scene-capture/SCN-WLR-MATERIAL-ALBEDO --verify
```

エンジンのコード・シェーダー・`WastelandRoutine.cpp` の値が変わると撮影の入力のハッシュが変わり、OSL の本体・台本・値の表・
ミップの段の元の画像が変わると Blender の結果が古いとして照合が落ちる。どちらも撮り直し・描き直しで戻る。

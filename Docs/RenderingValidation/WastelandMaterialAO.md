# 地表の材質の AO の値の出どころを確かめる

荒野の地形の地表の材質の AO（ORM の R）が、平らな砂地でも 0.6 台になる理由を、素材の地図・アトラス・クック・層の混ぜの順に
確かめた記録。判定日: 2026-10-21。ブランチ `feature/scene-launcher`。タスク SCN-WL-MATERIAL-AO-BAKE。

## 結論

- 経路に誤りは無い。0.6 台は Poly Haven の AO の地図の値そのもの（谷床の赤茶の土 `red_mud_stones` の平均 0.558、
  礫の舗石 `red_laterite_soil_stones` の平均 0.626）で、アトラス・クック・層の混ぜはどれも値を変えずに運ぶ。
- ただし地図の値は、素材自身の起伏から焼いた遮蔽より大幅に暗い。変位の地図を法線の地図の傾きに合わせて実寸にした高さ場
  （全体の高さ 25〜40 mm）で焼き直すと、平均は 0.92〜0.95。Poly Haven の AO は変位から焼き直した AO と形の相関は高い（0.74・0.86）が、
  同じ平均にするには起伏を約 4 倍にする必要がある。遮蔽の物理量ではなく、くぼみを強調した地図として作られている。
- ORM の R（AO の地図）は層の混ぜの高さにも使うので、値を変えると土・皮膜・砂・岩肌の境が変わる。R はそのまま残し、
  照明が遮蔽として読む値だけを起伏から焼いた値にした（SCN-WL-MATERIAL-AO-OCCLUSION。下の「遮蔽の焼き直し」）。
- RTGI が働く荒野の撮影では、材質の AO は間接の鏡面（空の映り込み）にだけ掛かる（SCN-WL-MATERIAL-AO-SHADING の後）。
  地図の暗さで削られる分は、日なたの地面の明るさの約 1 %（ref_ground_01）〜約 4 %（ref_cliff_02）と見積もった。

## 経路の確認

| 段 | どこ | 確かめたこと |
|---|---|---|
| 素材の地図 | `Assets/Textures/PolyHaven/<素材>/<素材>_ao_4k.jpg` | 8 bit のグレースケール（L）。Poly Haven の AO の地図は色でなくデータ（線形）として読む |
| アトラス | `Scripts/Terrain/TerrainMaterials.py`（`ATLAS_MAPS` の `ao` は `L`） | 砂の枠 `wasteland_soil_sand_ao_8k.png` の左・右、土の枠 `wasteland_dirt_crust_ao_8k.png` の左・右の平均（小数 4 桁）と分位（小数 3 桁）が、元の 4 枚と一致する（並べるだけで変換しない） |
| クック | `Tools/AssetCook/TextureCooker.cpp` の `PackOrmBase` | AO の画像を 1 チャンネルで読み、8 bit をそのまま R に詰める。形式は `nvtex.v0.2.bc7.linear`（UNORM。sRGB の変換なし） |
| 標本 | `Assets/Shaders/Common/PbrMaterialEvaluation.glsl` の `DecodePbrOrmSample` | R を `Material.z` へそのまま入れる |
| 層の混ぜ | `Assets/Shaders/Common/TerrainLayerBlend.glsl` | 投影・距離の段・層（土・皮膜・砂・岩肌）の重みでの線形の和。岩肌だけ地層の `strata.Occlusion`（0.83〜0.99）を掛ける。AO は混ぜの高さ（くぼみほど低い）にも使う |

参照の材質の再現（`SCN-WLR-CYCLES-LIT/material/<視点>.occlusion.pfm`。エンジンの層の混ぜを Blender で再現したもの）の
平らな地面（法線の y が 0.97 より上）の AO は、ref_ground_01 で平均 0.619・中央 0.603、ref_cliff_02 で平均 0.601・中央 0.567。
遠くの地面ほどミップで地図の平均に寄り、赤茶の土の 0.558 に近づく。0.8 を超える画素（皮膜の付いた土・沢底の砂）は 6〜11 %。

## 測った値

素材の AO の地図と、素材自身の起伏から焼き直した AO（平らな面の上向きの法線に対する余弦の重みの見える割合。方位ごとの
1 − sin²（地平の仰角）の平均）。

| 素材 | 実寸 | 地図の平均 | 地図の分位 10・50・90 % | 法線から焼き直し | 変位から焼き直し | 地図との相関 |
|---|---|---|---|---|---|---|
| 赤茶の土 `red_mud_stones` | 1.5 m | 0.558 | 0.36・0.58・0.73 | 0.950 | 0.945（全体の高さ 40.2 mm） | 0.66（法線）・0.74（変位） |
| 礫の舗石 `red_laterite_soil_stones` | 2.0 m | 0.626 | 0.42・0.62・0.84 | 0.934 | 0.916（全体の高さ 25.4 mm） | 0.79（法線）・0.86（変位） |
| 皮膜の付いた土 `cracked_red_ground` | 2.0 m | 0.858 | 0.74・0.88・0.95 | 0.976 | （変位は落としていない） | 0.88（法線） |
| 沢底と吹きだまりの砂 `red_sand` | 3.0 m | 0.850 | 0.78・0.86・0.90 | 0.981 | （同上） | 0.63（法線） |
| 赤褐色の砂岩 `cliff_side` | 1.8 m | 0.961 | 0.90・0.98・1.00 | （測っていない） | （同上） | — |

- 法線から: DirectX の法線の地図の傾きをフーリエ変換で積分した高さ場（1024 画素、タイルの周期を使う）。探す距離を 2 cm〜50 cm で
  変えても平均は 0.001 以内で同じ（遮蔽は数 mm〜1 cm の起伏で決まる）。
- 変位から: Poly Haven の 4k の変位の地図の高さの倍率を、法線の地図の傾きに最小二乗で合わせて実寸にした（傾きの相関 0.84・0.87）。
  変位の倍率を 2 倍・4 倍にすると平均は 0.857・0.702（赤茶の土）、0.795・0.617（礫の舗石）。地図の平均に届くのは約 4 倍のとき。
- mtlx の変位の倍率（0.01）は素材ごとの値でない既定値なので使っていない。

## 今の描画での効き方

`lighting.frag` は、RTGI が働く撮影では材質の AO を直接光にも RTGI の拡散にも掛けない（SCN-WL-MATERIAL-AO-SHADING）。残るのは
間接の鏡面の `ComputeSpecularAO`（Lagarde 2014）で、粗さ 0.9〜1.0 では指数がほぼ 0 になり、値は材質の AO とほぼ同じになる。
DDGI（RTGI が使えないとき）は拡散の環境光に材質の AO を掛ける。

空の映り込みの反射率（F0 0.04、GGX・高さ相関の Smith の split-sum を数値で積分）は、N·V 0.5・粗さ 0.9 で 0.026、
N·V 0.09・粗さ 1.0 で 0.055。地面のアルベド（輝度 0.16）に対して空の拡散の 16 %・34 % に当たり、Cycles の日なたの地面の
空の組の割合（ref_ground_01 17 %、ref_cliff_02 29 %）を掛けると、空の映り込みは全体の約 3 %・約 10 %。AO 0.62 はその 38 % を
削るので、全体の約 1 %・約 4 %。起伏から焼いた AO（0.93 前後）なら削られる分は約 0.2 %・約 0.7 % になる。

## 参照の扱い

AO の値も、アルベド・法線・粗さも変えていないので、Cycles の参照（SCN-WLR-CYCLES-LIT）は描き直さない。
`ReferenceCheck.py --lit --expect material-ao.json --verify` は今の入力で `inputs_current=1`・`cycles_inputs_current=1`
（参照の署名 `33d9c699…9e86930e9d05`）、日なたの地面の比 ref_ground_01 0.9722・ref_cliff_02 0.9246 で合格。

## 遮蔽の焼き直し（SCN-WL-MATERIAL-AO-OCCLUSION、2026-10-21）

高さと遮蔽を ORM の別のチャンネルに分けた。R は今までどおり AO の地図（層の混ぜの高さ）、A に素材の起伏から焼いた遮蔽を入れ、
照明が読む材質の AO（`Material.z`）を A の値にする。

| 段 | どこ | 中身 |
|---|---|---|
| 焼き直し | `Scripts/Terrain/TerrainOcclusion.py --build` | 法線の地図を 1024 画素へ縮めて積分した実寸の高さ場で、上の「法線から」と同じ遮蔽を焼き、タイルの周期で双線形に 4k へ広げて、アトラスの並びの `<アトラス>_occl_8k.png` を書く。`--verify` は法線・AO の地図の md5、作り直しとの一致、素材ごとの平均（0.90〜0.99 で地図より明るい）を確かめる |
| 資産の組 | `Assets/AssetSets/WastelandTerrain.json` の `orm_occlusion` | 砂の枠・土の枠は焼いた画像、岩肌（`cliff_side`）は AO の地図そのもの（平均 0.961 で起伏とほぼ同じなので変えない） |
| クック | `Scripts/CookAssets.ps1`（`--orm-occlusion`）→ `Tools/AssetCook/TextureCooker.cpp` の `PackOrmBase` | 4 枠目を A に詰める。無い ORM は従来どおり A=255 で、差分クックの印も従来と同じバイト列（ほかの資産は焼き直されない） |
| 層の混ぜ | `Assets/Shaders/Common/TerrainLayerBlend.glsl` | 層の標本に `Occlusion`（A）を足し、皮膜・砂の混ぜ・層の重みで R と同じ重みで混ぜる。岩肌は地層の `strata.Occlusion` を高さと遮蔽の両方に掛ける。高さの混ぜは R のまま |

焼いた平均: 赤茶の土 0.950・礫の舗石 0.934・皮膜の付いた土 0.976・砂 0.981（地図は 0.558・0.625・0.858・0.850）。
焼く刻みを 1024 画素にしたのは、変位の地図から焼いた値と平均が揃うため。元の 4k の刻みでは JPEG の法線の雑音が高さに積もり、
赤茶の土 0.906・砂 0.968 と暗くなる（2048 画素では 0.924・0.973）。

### 撮影での確かめ

- 層の混ぜ: SCN-WL-MATERIAL-AO-SHADING の撮影とのアルベドの AOV の差は、平均のアルベドが小数 5 桁まで同じで、差が 0.01 を
  超える画素は ref_ground_01 716・ref_cliff_02 750（全体の 0.03〜0.04 %。0.05 を超えるのは 9・17）。差の画素は皮膜の斑の縁と
  地平の遠い地形に点で散る。A に遮蔽が入って BC7 の符号の詰め方が変わり、R・G の丸めが 1〜2 段動いた分（粗さの AOV の差は
  平均 0.001・最大 0.047）で、境の位置は変わらない。
- 参照: `--lit --expect material-ao.json --verify` は `inputs_current=1`・`cycles_inputs_current=1`、参照の署名は
  `33d9c699…9e86930e9d05` のまま（`orm_occlusion` は `RefSplit.AO_ASSET_KEYS` で地形のメッシュの入力から外す。Cycles は材質の AO を
  使わない）。日なたの地面の比は ref_ground_01 0.9722 → 0.9830（+1.1 %）、ref_cliff_02 0.9246 → 0.9426（+1.9 %）。
  ref_cliff_02 は見込みの約 +3 % より小さい。`lit_check.json` の `material_ao` は参照の材質の再現の値（AO の地図）のままで、
  エンジンの遮蔽ではない。
- 回帰: 荒野の外のシーン（rendering3dtest・mega-bananas・sandbox・night-village）は PSNR 61 以上か差なし。

## 記録の場所

- `.harness/runs/20261021-055341/aux-SCN-WL-MATERIAL-AO-BAKE-analysis.txt`: 地図の値・焼き直し・平らな地面の AO・映り込みの反射率
- `.harness/runs/20261021-055341/ao-bake/`: 測りに使った台本（`ao_stats.py`・`ao_from_normal.py`・`ao_from_disp.py`）。
  変位の地図は Poly Haven の files の API から落とした（`red_mud_stones_disp_4k.png` md5 `1c82936a…`、
  `red_laterite_soil_stones_disp_4k.png` md5 `2457439d…`。リポジトリには置いていない）
- `.harness/runs/20261021-055341/verify-SCN-WL-MATERIAL-AO-BAKE-1.txt`: `--lit --verify` の出力
- `.harness/runs/20261021-055341/verify-SCN-WL-MATERIAL-AO-OCCLUSION-*.txt`: 遮蔽の焼き直しの後のビルド・撮影・`--lit --verify`・回帰。
  `aux-SCN-WL-MATERIAL-AO-OCCLUSION-aov-diff.txt` が AOV の差、`ao-occlusion/` が地図と焼いた遮蔽・アルベドの差の画

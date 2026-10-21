# 前哨地の建物 1 棟を正解の絵と比べる仕組み

荒野の前哨地の建物 1 棟を、エンジンと Blender Cycles の正解の絵で同じ照明・同じカメラのまま比べる仕組みの記録。
判定日: 2026-10-21。ブランチ `feature/scene-launcher`。タスク SCN-WLR-BUILDING-REF-BASE。
この段は仕組みを作って試しに回すところまでで、エンジンとの一致は求めない（値は表に書くだけ）。

## 手順

1. エンジンの撮影（建物 1 棟だけ・共通の照明・最も細かい段の地形）:

   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views outpost -GameArgs wasteland-common-light,wasteland-hide=all-but-building,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE
   ```

   `--wasteland-hide=all-but-building` は雲・草・岩・礫・空気遠近・霧を外し、前哨地の建物のうち比べる 1 棟だけを残す。
   比べる 1 棟は道の東の石積みの物置（`wooden_shack`、造成の範囲 3。`WastelandLayout.cpp` の `kReferenceBuildingPad`）。
   outpost の視点から、共通の照明の太陽（仰角 58.5°・方位 154°）で日なたの西の壁と、陰の北の壁が両方見える。
   道の西の建物（日干し煉瓦の廃屋など）は、見える北と東の壁がどちらも陰になるので選ばない。
   撮影のログには置いた建物ごとに `WASTELAND_BUILDING_PLACEMENT`（glTF・`reference`・Entity の位置・Y 軸まわりの回転
   `yaw_rad`・拡大率・メッシュの最も低い y）が出る。
2. Cycles の正解の絵（同じ glTF を同じ配置で置く）:

   ```
   python Scripts/Terrain/BlenderReference.py --building --trial --mesh .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-mesh --capture .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE
   ```

   建物は Blender の glTF の読み込みで読み（材質も glTF のまま）、ログの配置で置く。glTF の読み込みはローカルの +Y 上を
   +Z 上へ移すので、エンジンの yaw（ローカルの (x, z) をワールドの (x cos − z sin, z cos + x sin) へ写す）は Blender の
   z 軸まわりの −yaw になる。地形は灰色の材質（`CommonLight.json` の `gray_material`。`--gray` と同じ）。
   `--trial` は 1 本 64 サンプルで止める試しの描画（書き先 `cycles-trial`）。外すと 1 本 512〜2048 サンプルで収束まで重ねる
   （書き先 `cycles-building`）。
3. 照合:

   ```
   python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE/cycles-trial --trial --verify
   ```

## 比較が成り立つ条件（満たさなければ落ちる）

| 条件 | 基準 |
|---|---|
| エンジンの撮影 | 起動引数とログの印、建物の配置の行が 1 行だけ、撮った時点のジオメトリのページの進行中 0・追い出しなし |
| 配置とカメラ | 参照が置いた glTF・位置・回転・拡大率がエンジンのログと 1e-4 以内、カメラが撮影の記録と同じ |
| 深度の輪郭 | 建物の画素の外接枠を 48 画素広げた枠で、強い輪郭（深度の比 8 %）から相手の弱い輪郭（4 %）までが 2 画素以内の画素の割合が両方向とも 0.99 以上 |
| Cycles の収束 | 1 本 512 サンプル以上で、領域ごとの 2 本の差が SCN-WLR-CYCLES-GRAY と同じ基準（平均の差 1 %・画素の差の RMS 10 %）。`--trial` はこれだけを外す |
| 領域 | どの領域も 200 画素以上（outpost から建物まで約 40 m で、足元の帯は地面をかすめて見るので数百画素）。表の値が求まる |
| 光の内訳 | Cycles の直接 + 間接・太陽の組 + 空の組が合成の ±1 % |

`--verify` はエンジンの撮影の入力と、参照の入力（台本・`BuildingRegions.py`・`GrayRegions.py`・共通の照明・glTF・
地形のメッシュ）を今のファイルと照らす。

## 領域と量（`Scripts/Terrain/BuildingRegions.py`）

領域は正解の側（Blender の深度・物体の番号・法線・太陽の見え方）から決め、エンジンと Cycles の同じ画素を比べる。

| 領域 | 定義 |
|---|---|
| 足元から 0〜2 m の地面 | 地形の画素で、足元の範囲（glTF の頂点の範囲の XZ の箱）の外への水平の距離が 0〜2 m |
| 足元から 4〜8 m の日なたの地面 | 同じく 4〜8 m で日なた。足元の帯との比で、地面の材質に依らない足元の暗さを見る |
| 壁の下端 | 建物の画素で法線の上向きの成分が 0.3 以下、建物の周り 1 m の地面の高さから 0.5 m 以下 |
| 壁の日なた・陰 | 壁で地面から 0.5 m より上の、日なた・影（GrayRegions と同じ太陽の見え方の基準） |

量: 壁の日なた ÷ 陰、壁の下端 ÷ 壁の日なたと陰の平均、足元の帯 ÷ 離れた日なたの地面（描画ごと）。建物の下端と地面の高さの差は、
描画ごとにその深度から戻した位置で、建物の画素のうち足元の範囲の中の高さの 2 % 点から、周り 1 m の地面の高さの中央値を引く。

## 試しの結果（64 サンプル、outpost）

撮影 `game_exe` 4a55284e…、Cycles は Blender 5.2.2・OptiX、地形 136 区画（8980 万三角形、影の届く 4 km）と建物 59.7 万三角形。
読み込み 228 秒、描画 42 秒。深度の輪郭は両方向とも 2 画素以内が 1.000。

| 領域 | 画素 | エンジン Y | Cycles Y | エンジン ÷ Cycles |
|---|---|---|---|---|
| 足元から 0〜2 m の地面 | 740 | 4733 | 8251 | 0.574 |
| 足元から 4〜8 m の日なたの地面 | 6351 | 5359 | 10072 | 0.532 |
| 壁の下端 | 879 | 4645 | 3289 | 1.412 |
| 壁の日なた | 396 | 10932 | 8543 | 1.280 |
| 壁の陰 | 2764 | 3001 | 2574 | 1.166 |

| 量 | エンジン | Cycles |
|---|---|---|
| 壁の日なた ÷ 陰 | 3.64 | 3.32 |
| 壁の下端 ÷ 壁の日なたと陰の平均 | 0.667 | 0.592 |
| 足元の帯 ÷ 離れた日なたの地面 | 0.883 | 0.819 |
| 建物の下端 − 地面の高さ（m） | +0.047 | +0.047 |

読み方の注意（この段では値の差を判定しない）:

- 地面の値は材質が違う（エンジンは実材質の赤茶の地面、Cycles は灰色 0.3）ので、地面どうしの比だけを見る。
  壁の値も、照り返しの地面の色と明るさが違う分を含む。
- 建物の下端と地面の高さの差は両方の描画で +0.047 m と同じで、配置の最下点（0.167 m）は地面（0.39 m 前後）より 0.22 m 下に
  埋まっている。+0.047 m は、見える最も低い壁の画素が地面の接する画素より 1〜2 画素上にある分（約 40 m 先の 1 画素は約 2.5 cm）。
- 64 サンプルでも 2 本の差は全領域で基準に収まったが、収束の判定は 512 サンプルからなので、`--trial` を外した本描画で確かめる。
- エンジンの北の壁（陰）に、石積みの目地のあたりで黒に近い斑点が出る（見比べの画像 `cycles-trial/building_outpost.png` の左上。
  Cycles には無い）。照明を直した後の比較（SCN-WLR-BUILDING-* の後段）で原因を切り分ける。

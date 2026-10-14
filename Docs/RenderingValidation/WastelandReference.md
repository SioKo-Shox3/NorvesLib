# 荒野の手本（Valley of the Gods）

荒野（`wasteland`）を実在の地形から作り直すときの手本。比較に使う写真、DEM から切り出す範囲、主役にする地形、
前哨地を置く平らな場所の候補を決める。判定日: 2026-10-15。ブランチ `feature/scene-launcher`。タスク SCN-WL-REFERENCE-SELECT。

- 写真と `manifest.json`（URL・作者・ライセンス・撮影の位置と向きの推定・写っている物・SHA-256）と縮小一覧 `contact_sheet.png` は
  `Assets/Reference/ValleyOfTheGods/`（git 管理外）。`python Scripts/Terrain/ReferencePhotos.py --fetch` で落とし直せる。
- 確かめるのは `python Scripts/Terrain/ReferencePhotos.py --verify`（ファイルと SHA-256・ライセンス・役割ごとの枚数・範囲がタイルの内側・
  名前の付いた地形と前哨地の候補が範囲の内側・`.gitignore`・この文書）。
- 座標は NAD83 / UTM 12N（EPSG:26912）の m。地形の座標は GNIS 由来の Wikidata・Commons の緯度経度を換算した値（誤差は数十 m）。

## 切り出す範囲

| 項目 | 値 |
|---|---|
| タイル | 3DEP 1 m `USGS_1M_12_x60y413`（E 600000〜610000・N 4120000〜4130000。TNM の API の範囲と一致） |
| 範囲 | **E 602500〜606500・N 4125000〜4129000**（4 km × 4 km） |
| 四隅の緯度経度 | 南西 37.26593, -109.84399 / 北東 37.30153, -109.79831 |
| 中心 | E 604500・N 4127000（37.28373, -109.82115） |
| タイルの端までの余り | 西 2500 m・東 3500 m・南 5000 m・北 1000 m |
| 標高（目安） | 1396〜1651 m、中央値 1452 m。傾斜 15°超の面積 7%、3°未満 47% |

標高と傾斜の目安は、3DEP の標高サービス（`3DEPElevation/ImageServer` の `getSamples`）から 50 m 間隔で引いた標本で見た値。
1 m の値と形の確認は SCN-WL-DEM-CROP の陰影図と断面で行う。

### 選んだ理由

- タイルの中で、主役になる大きなビュートが 4 つ（Battleship Rock・Franklin Butte・Rooster Butte・Setting Hen Butte）
  まとまって入る（Seven Sailors は南へ離れ、Castle Butte はタイルの外）。比高は 50 m の標本で Battleship Rock 約 186 m、Franklin Butte 約 116 m、
  Setting Hen Butte 約 114 m、Rooster Butte 約 90 m（頂部と周り 700 m の低い側 20% の差）。
- 谷床は北から南東へ緩く下がる（範囲の中で約 1°）平らな面で、ビュートの間に 1 km 以上の開けた谷床が残る。
  手本の写真の「平らな谷床に孤立したビュートが立つ」構図を範囲の中で作れる。
- 写真の撮影位置のうち 2 つ（far_01・far_02）が範囲の内側にあり、cliff_02 も北の外 305 m にある（下の SCN-WL-DEM-CROP への注記）。
- 外した地形: Seven Sailors（E 604278・N 4122514）は Battleship Rock から南へ約 5.4 km で、両方を 4 km 四方に入れられない。
  Castle Butte（E 601557・N 4130371）と Lady in the Bathtub（E 599397・N 4126740）はタイルの外。これらと北の Cedar Mesa の縁の崖は
  遠景として SCN-WL-HORIZON の粗い地形で出す。
- 範囲の西端から Battleship Rock の西の端（E 約 602900）まで約 400 m、東端から Setting Hen Butte まで約 700 m あり、主役が範囲の端で切れない。

### SCN-WL-DEM-CROP への注記

- 余白は 100 m 以上の決まりのままでよいが、北側だけは 400 m 以上にすると cliff_02 の撮影位置（N 4129305）が DEM の上に乗る
  （タイルの北端まで 1000 m あるので取れる）。
- 主役の崖の断面は Battleship Rock を南北に横切る線（E 603100、N 4127500〜4128400）で見る。

## 主役の地形

| 地形 | 役 | E | N | 見るところ |
|---|---|---|---|---|
| Battleship Rock | 主役 | 603113 | 4127955 | 範囲で最も高く長い。上部の垂直な崖・中段の段々の地層・下部の崖錐の斜面の 3 段（cliff_01） |
| Franklin Butte | 準主役 | 603983 | 4127319 | ピラミッド状の地層の斜面と頂部の柱（cliff_02） |
| Rooster Butte | 中景 | 605425 | 4126197 | 尖塔の頭の小さなビュート（far_02） |
| Setting Hen Butte | 中景 | 605801 | 4125647 | 横縞の地層の大きなビュート（far_02） |

## 前哨地を置く平らな場所の候補

半径 150 m の窓で、50 m の標本に平面を当てた傾き（tilt）と平面からのずれ（RMS）で選んだ。範囲の中で傾きが 1.5°以下・ずれが 1 m 以下の所。

| 候補 | E | N | 傾き | ずれ | 周りとの関係 |
|---|---|---|---|---|---|
| **A（第一）** | 605700 | 4126850 | 1.1° | 0.5 m | Rooster Butte の北北東 710 m、Setting Hen Butte の北 1.2 km、Battleship Rock の東南東 2.8 km。東端まで 800 m |
| B | 605550 | 4127450 | 1.0° | 0.4 m | A の北北西 600 m。ビュートから 1.2 km 以上離れ、まわりが最も開けている |
| C | 604100 | 4126850 | 1.3° | 0.7 m | Franklin Butte の南 480 m。崖錐の裾に近いので、建物の裏に崖が迫る構図にするとき |

A を推奨する: 前哨地から西に Battleship Rock と Franklin Butte、南に Rooster Butte と Setting Hen Butte が見え、outpost の視点で
主役を背にした構図が作れる。崖錐から離れていて、造成（SCN-WL-OUTPOST）は緩い傾きを均すだけで済む。
1 m の DEM での平らさの確認（沢の切れ込み・小さな段が無いこと）は SCN-WL-DEM-CROP の陰影図で行い、だめなら B、C の順に替える。

## 写真

役割ごとに、遠景 2 枚・主役の崖 2 枚・足元 2 枚。すべて Wikimedia Commons の Valley of the Gods の分類から取り、ライセンスは
CC BY・CC BY-SA・CC0（NC・ND・FAL・GFDL は除いた）。CC BY・CC BY-SA の写真を公開物（画面写真・資料）に載せるときは、
`manifest.json` の作者・ライセンス・URL を表示する。撮影の位置は EXIF の GPS があればその値で、数十〜数百 m の誤差がありうる。

### far_01（遠景）

- `File:Valley of the Gods - panoramio (1).jpg`、runt35、CC BY 3.0、2012-05-18、5184×3456。
- 撮影位置: GPS 37.2978, -109.8318（E 603537・N 4128549、範囲の内側、Battleship Rock の北東 700 m）。向きは北西（方位 300〜320°）で Castle Butte の方。
- 写っている物: 手前に板状に割れた砂岩の露頭と赤い砂、低木が 1〜3 m 間隔で一面に点在。中景に平らな谷床、奥に城のようなビュートと二本の尖塔、背後に段の付いた崖。晴天に積雲。
- 比較で見る特徴: 谷床の低木の被覆の密度と大きさ、谷床の平らさと赤の彩度、奥の崖までの霞の量、ビュートの高さと間隔の比。
  手前〜中景（約 1 km まで）は範囲の内側、奥のビュートと崖は範囲の外なので SCN-WL-HORIZON の遠景と合わせて見る。

### far_02（遠景）

- `File:Grand Staircase 201812 0239 (43703896942).jpg`、The Greater Southwestern Exploration Company、CC BY 2.0、2018-05-11、3264×2448。
- 撮影位置: GPS 37.27479, -109.80596（E 605860・N 4126025、範囲の内側）。GPS の位置は 2 つのビュートに近すぎる（両方がほぼ 100°離れた向きになる）ので、
  実際は数百 m 離れた所から西〜南西を向いたと推定する。SCN-WL-REFERENCE-CAPTURE で 2 つのビュートの見かけの間隔から位置を合わせる。
- 写っている物: 左に尖塔の頭の小さなビュート、右に横縞の地層の大きなビュート（Rooster Butte と Setting Hen Butte）。手前は礫の混じる赤い緩斜面と株立ちの低木（2〜5 m 間隔）、ビュートの裾に崖錐。薄曇り。
- 比較で見る特徴: ビュートの裾の崖錐の広がりと傾き、地層の横縞の間隔、手前の礫の大きさと低木の間隔、薄曇りの空と地面の明るさの比。

### cliff_01（主役の崖）

- `File:Battleship Rock, Valley Of The Gods.jpg`、pspechtenhauser、CC BY 2.0、2008-09-24、3648×2378。
- 撮影位置: 記録なし。Battleship Rock の南〜南東の谷床、1〜1.5 km 離れた所から北〜北西を向いたと推定する。
- 写っている物: 上部の垂直な崖と城壁のような頭部、中段の段々の地層と棚、下部の崖錐の斜面と落ちた岩塊、手前の谷床。晴天で横からの日差し。
- 比較で見る特徴: 崖の高さと崖錐の斜面の高さの比、地層の段の数と厚さ、崖の上端の凹凸、落石の岩塊の大きさと散らばり、日向と影の明るさの比。
  崖と崖錐の形は SCN-WL-CLIFF-GEOMETRY・SCN-WL-SCATTER-ROCKS、地層は SCN-WL-MATERIAL-STRATA、光は SCN-WL-LIGHT-BASE の手本にする。

### cliff_02（主役の崖）

- `File:Franklin Butte, Valley of the Gods.jpg`、Mobilus In Mobili、CC BY-SA 3.0、2018-11-04、3903×1931。
  `File:Utah Valley of the Gods 17 Mile Loop (32365160328).jpg`（同じ作者のパノラマ）からの切り出し。
- 撮影位置: 元のパノラマの GPS 37.30475, -109.84583（E 602284・N 4129305、範囲の北の外 305 m）。向きは南東（方位 約 140°）、右手前 1.6 km に Battleship Rock がある。
- 写っている物: Franklin Butte の北西面。ピラミッド状の地層の斜面と頂部の柱状の岩、奥に地平線の山並み。色は HDR の処理で彩度と紫みが強い。
- 比較で見る特徴: 形と地層の間隔だけ（色・明るさは比較に使わない）。斜面の傾きと頂部の柱の太さの比。

### ground_01（足元）

- `File:Valley of the Gods - HCP - March 17, 2025 - 006.jpg`、Vulturesong、CC0、2025-03-17、3951×2963。
- 撮影位置: 記録なし。Valley of the Gods の道沿いの谷床（同じ日の連作の 1 枚）。奥に平らなメサが 2 つ。
- 写っている物: 礫（数 cm〜20 cm）が一面に散る赤い地表、低木の株（高さ 0.3〜1 m）、手前の高さ数十 cm の小さな侵食の段（沢の肩）。曇天。
- 比較で見る特徴: 礫の大きさと被覆、低木の株の大きさと間隔、地表の赤の色味（曇天なので素材の色の比較に向く）、侵食の段の形。
  地表の素材（SCN-WL-MATERIAL-PALETTE）・草と低木の散布・沢（SCN-WL-MASKS-FLOW）の手本にする。

### ground_02（足元）

- `File:Valley of the Gods - HCP - March 17, 2025 - 015.jpg`、Vulturesong、CC0、2025-03-17、3024×4032（縦）。
- 撮影位置: 記録なし。崖の上の縁の近く（遠くに平原が見える。ground_01 と同じ日の連作）。
- 写っている物: 崖から割れ落ちた砂岩の岩塊（1〜4 m）と板状に割れた岩、その間の礫と低木。曇天。
- 比較で見る特徴: 岩塊の大きさの分布と丸まり方、割れた面と風化した面の色の差、岩の間の礫と砂の溜まり方。
  崖錐と大岩（SCN-WL-SCATTER-ROCKS）・崖の素材（SCN-WL-CLIFF-ASSETS）の手本にする。

### 足元の写真についての注記

沢底（砂の溜まった平らな流路）を正面から写した、使えるライセンスの写真は Valley of the Gods の分類に無かった。
ground_01 の侵食の段と far_02 の手前の緩斜面で代え、沢の形は DEM の流れの解析（SCN-WL-MASKS-FLOW）で決める。
ダウンロードの許可が写真 4〜6 枚なので、Cedar Mesa・Monument Valley 周辺の写真は取っていない。

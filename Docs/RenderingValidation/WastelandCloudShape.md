# 荒野の雲の形

荒野の雲を一様な薄い層から切れ間のある積雲へ変えるときの、比べの基準と形の量を置く文書。
雲の Cycles の比べの仕組みは `WastelandCloudBlender.md`、書き出しの契約は `WastelandCloudDump.md`、
前の正解（SCN-WLR-CLOUD-REF）との比べと差の一覧は `WastelandCloudRef.md`。

## 基準（2026-10-24 の密度場）

雲の形を変える前の、今のコードの書き出しとその Cycles の正解の組。後の雲の項目の比べ（`--cloud --expect` など）の基準の値はこの表。
値は測っただけで、エンジンの値と既定は変えていない。判定日: 2026-10-24。ブランチ `feature/scene-launcher`。タスク SCN-WLR-CLOUD-REF-CURRENT。

### 組と条件

- エンジン: `.harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump`（Game.exe ab0adb16…、RelWithDebInfo、入力の記録の
  コミット 069508d8）。cloud_sky（南南西を仰角 45° で見上げる、1920 × 1080）の雲の書き出し（`-GameArgs cloud-dump`）。
  1c32fa29 の入力で撮った最初の置き場は、その後に雲と関係の無い入力（`WastelandModels.json`）が変わって `--verify` で古い扱いになったので
  `dump-1c32fa29` へ移し、同じ Game.exe で撮り直した書き出し（SCN-WL-CLOUD-CHECK-EXPECT の 2 回目の撮影。雲の書き出しの 6 ファイルの
  ハッシュが最初の置き場と同じ）に置き換えた。Cycles の正解は描き直していない。
- Cycles: `.harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles`。台本は前の正解と同じ（`BlenderReference.py --cloud`、変えていない）。
  密度場を 16 m の格子の体積（1785 × 1045 × 74、値のある格子 33612591、密度の最大 0.741）として置いた。Blender 5.2.2・OptiX（RTX 4080）。
  焼き 263 秒・読み込み 269 秒、描画 139 秒で、1 本 704 サンプルで全領域が収束して止まった（全体 413 秒、25 分の上限から遠い）。
- 書き出しと正解の組: Cycles の `inputs.json` は読んだ書き出しのハッシュ（`cloud_sky.cloud.json` 34943e9f…・`cloud_sky.cloud.probe.f32` 496193cb…）
  を持ち、`--verify` は今の置き場の書き出しとの一致を求める。同じ Game.exe でも `cloud_sky.cloud.json` は撮るたびに同じとは限らない
  （6e7105e3… と 34943e9f… の 2 通りを撮った。probe と散乱光・不透明度の値は同じ）ので、書き出しを撮り直したら Cycles も描き直す。
- 前の正解との関係: 前の正解（`SCN-WLR-CLOUD-REF`、8113ba38 のコードの書き出しから描いた）は消していない。前の正解が記録した書き出しの
  ハッシュ（`cloud_sky.cloud.json` 36755f04…・`cloud_sky.cloud.probe.f32` e24ecf5a…）は今の書き出し（34943e9f…・496193cb…）と合わないので、
  今のコードの比べは `--verify` で古い扱いになる。形のノイズ（dbf55267…）・細部のノイズ（e024ab47…）・空の LUT（03700ec1…）のハッシュは同じ。
  今の組の値は前の正解の表と、エンジンの散乱光・不透明度が 144 本の視線で一致し、Cycles の Y も 10⁻⁶ の相対差で一致した
  （cloud_sky の視点では雲は 3.5〜5 km にあり、その後の遠い雲の変更〔積む範囲・空気遠近の合成〕の効かない範囲）。

再現（書き出しの撮影と Cycles の描画は続けて 1 回ずつ行い、照合は保存した組に対して回す）:

```
cmake --build build --config RelWithDebInfo --target Game -- /m:1
powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump
python Scripts/Terrain/BlenderReference.py --cloud --capture .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump --max-seconds 1500 --out .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles
python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles --verify
```

`--cloud --verify` は result=pass（書き出しの検査に合格、投影の差 0.22 画素、空の相対差 最大 0.09 %、透過率の画と式の差 最大 0.0050、
Cycles の出力に NaN・Inf なし、全領域が収束、`inputs_current=1`）。下の表の値は 6e7105e3… の書き出しから描いた組で測り、
34943e9f… の書き出しから描き直した今の組でも 3 桁まで同じ（Cycles の Y の相対差 10⁻⁶ 以下）。

### 5 領域の比

| 領域 | 視線 | エンジン Y（cd/m²） | Cycles Y | エンジン ÷ Cycles | 視線ごとの比の幅（中央値） | 不透明度 エンジン・Cycles | Cycles の収束（平均の差 / 画素の差の RMS） |
|---|---|---|---|---|---|---|---|
| 厚い雲 | 42 | 19478 | 19295 | 1.009 | 0.60〜1.49（1.08） | 0.696・0.697 | 0.0021 / 0.060 |
| 縁 | 22 | 4622 | 3107 | 1.487 | 1.19〜1.83（1.61） | 0.244・0.246 | 0.0038 / 0.096 |
| 銀の縁 | 9 | 32748 | 31218 | 1.049 | 1.01〜1.14（1.08） | 0.230・0.231 | 0.0027 / 0.031 |
| 日の当たる側 | 15 | 14724 | 12444 | 1.183 | 1.06〜1.49（1.24） | 0.665・0.666 | 0.0018 / 0.058 |
| 影の側 | 7 | 13524 | 18911 | 0.715 | 0.60〜1.03（0.69） | 0.919・0.920 | 0.0075 / 0.079 |

収束の差は、種の違う 2 本の Cycles の描画の領域の平均の相対差と、画素ごとの差の RMS を領域の平均で割った値（判定の上限は 0.01 と 0.1）。

### 3 つの量

| 量 | エンジン | Cycles | エンジン ÷ Cycles |
|---|---|---|---|
| 縁の明るさ（縁の Y / 不透明度 ÷ 厚い雲の Y / 不透明度） | 0.645 | 0.426 | 1.514 |
| 銀の縁（銀の縁の Y / 不透明度 ÷ 縁の Y / 不透明度） | 8.67 | 12.88 | 0.673 |
| 影の側の暗さ（影の側の Y ÷ 日の当たる側の Y） | 0.918 | 1.520 | 0.604 |

見比べの画 `.harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles/cloud_cloud_sky.png`（左 エンジン、右 Cycles）を開いて
見たこと: 雲の塊の形と位置（右上の大きな塊、中央の塊、左の 2 つの塊、右下の帯、下端の連なり）は両方で同じ。エンジンは縁が明るく
塊の右下の影の側が灰色に沈み、Cycles は縁が透けて塊の中が一様に白い（前の正解と同じ差）。エンジンの画には霞と太陽の円盤が入る。

GPU 時間は測っていない（描画のコードを変えていない）。

## 比の悪化の合否

`--cloud --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json` は、上の表の 5 領域の比と 3 つの量の比（エンジンの量 ÷ Cycles の量）が
基準の組より 1 から遠ざかったら落とす。判定日: 2026-10-24。タスク SCN-WL-CLOUD-CHECK-EXPECT。

- 誤差: e = |ln(エンジン ÷ Cycles)|。1 を挟んだ両側を同じ尺で測るので、縁の 1.48 → 1.60 も影の側の 0.72 → 0.60 も e が増えて悪化になる。
  1 の反対側へ跳んでも、隔たりが基準より大きければ悪化。
- 合否: 行ごとに e ≦ |ln(基準の比)| ＋ 許容 なら通る。未知の領域・量、基準の欠け（判定した視点の 5 領域・3 量のどれかに行が無い、
  基準の比が正の有限でない）、収束していない領域（量は使う 2 領域。`--trial` でも求める）、比が求まらない行は落ちる。
- 基準の比: 上の表の組（`cycles/cloud_check.json`）の値を小数 4 桁にした値。

### 許容の根拠

許容 = 同じ撮影を 2 回撮った揺れ ＋ Cycles の収束の差。どちらも e の尺（相対差）で足し、0.001 に切り上げた。

- 2 回の撮影の揺れ: 同じ Game.exe（ab0adb16…）で cloud_sky の書き出しを 2 回撮った（`SCN-WLR-CLOUD-REF-CURRENT/dump-1c32fa29` と
  `SCN-WL-CLOUD-CHECK-EXPECT/dump2`）。雲の書き出しの 6 ファイルのハッシュが同じで、144 本の視線のエンジンの散乱光・不透明度の差は 0、
  領域の分け方も同じ。揺れは 0（`.harness/runs/20261023-124311/aux-SCN-WL-CLOUD-CHECK-EXPECT-jitter.txt`）。
- Cycles の収束の差: 基準の組の、種の違う 2 本の描画の領域の平均の相対差（上の 5 領域の表の「平均の差」）。量は使う 2 領域の和。

| 行 | 基準の比 | 基準の e | 揺れ | Cycles の平均の差 | 許容 | 落ちる e |
|---|---|---|---|---|---|---|
| 厚い雲 | 1.0095 | 0.0095 | 0 | 0.0021 | 0.003 | 0.0125 超 |
| 縁 | 1.4875 | 0.3971 | 0 | 0.0038 | 0.004 | 0.4011 超 |
| 銀の縁 | 1.0490 | 0.0478 | 0 | 0.0027 | 0.003 | 0.0508 超 |
| 日の当たる側 | 1.1832 | 0.1682 | 0 | 0.0018 | 0.002 | 0.1702 超 |
| 影の側 | 0.7151 | 0.3353 | 0 | 0.0075 | 0.008 | 0.3433 超 |
| 縁の明るさ | 1.5143 | 0.4150 | 0 | 0.0059（縁 ＋ 厚い雲） | 0.006 | 0.4210 超 |
| 銀の縁（量） | 0.6729 | 0.3962 | 0 | 0.0065（銀の縁 ＋ 縁） | 0.007 | 0.4032 超 |
| 影の側の暗さ | 0.6044 | 0.5035 | 0 | 0.0093（影の側 ＋ 日の当たる側） | 0.010 | 0.5135 超 |

許容は基準の組の Cycles の揺れで、後の比べの組が新しい Cycles の描画でも同じ許容を使う（新しい描画の収束の判定は
`--cloud` の収束の条件〔平均の差 0.01 以下〕が別に求める）。

### 確かめたこと

- `python Scripts/Terrain/ReferenceCheck.py --cloud --selftest`: 合成の結果の 48 の場合で result=pass。基準のまま・許容の半分だけ遠ざけた・
  1 へ近づけた結果は通り、各行を許容 ＋ 0.01 だけ 1 から遠ざけた結果・1 の反対側へ遠く跳んだ結果・縁 1.60・影の側 0.60・未知の領域と量・
  領域と量の行の欠け・base_ratio の欠けと 0・tolerance の欠け・知らない鍵・重なった行・結果の無い視点・収束していない領域（領域の行と
  その領域を使う量の行）・求まらない比は落ちる。
- `--cloud --engine .../SCN-WLR-CLOUD-REF-CURRENT/dump --reference .../cycles --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json --verify`:
  8 行とも通り result=pass（`inputs_current=1`）。

この組は描画のソース（雲のシェーダーや密度場）が変わると `--verify` で古い扱いになるので、後の項目の固定の試験にはしない。
描画を変えた後の比べは、新しい書き出しとその Cycles の組を撮って描き、この仕様（基準の比はこの表のまま）で判定する。

## 塊・切れ間・縁の量（SCN-WL-CLOUD-SHAPE-METRIC）

雲の形を調整する前に、塊・切れ間・縁を量で測る道具と合否の定義を固定する。判定日: 2026-10-24。ブランチ `feature/scene-launcher`。
道具は `python Scripts/Terrain/ReferenceCheck.py --cloud-shape`（本体 `Scripts/Terrain/CloudShapeCheck.py`）、合否の定義は
`Scripts/Terrain/ExpectSpecs/cloud-shape.json`。入力は SCN-WL-CLOUD-OPACITY-AOV の全画素の雲だけの不透明度の画像
（`<視点>.cloud.opacity.f32` の r = cloud_layer.frag が重ねるアルファ。契約は `WastelandCloudDump.md`）で、撮り直さない。

### 測る画素と量

地形のマスクの外で、画素の中心の視線の仰角が 2° より上の画素（空の画素）だけを数える。視線の向きは記録の逆の射影から求める
（`--cloud-opacity` と同じ投影。雲の書き出しの視線の向きと 1e-4 の桁で合う）。

| 量 | 定義 |
|---|---|
| `gap_fraction` | 不透明度 0.1 未満の空の画素の割合（切れ間） |
| `film_fraction` | 0.1 以上 0.3 未満の割合（薄い膜） |
| `blob_count` | 0.3 以上の画素の 8 連結の成分のうち、面積が空の画素の 0.05 % 以上の物（塊）の数 |
| `blob_area_median`・`blob_area_p90` | 塊の面積（空の画素に対する割合）の中央値・90 百分位 |
| `blob_area_p90_over_median` | その比。大小の塊が混じるほど大きい |
| `largest_blob_share` | 最大の塊 ÷ 0.3 以上の画素の全部（小さな成分も含む） |
| `edge_blobs` | 画面の端に触れる塊の数（記録だけ） |
| `thick_share` | 0.6 以上の画素 ÷ 0.3 以上の画素（厚い部分） |
| `blob_opacity_std_median` | 塊ごとの不透明度の標準偏差の中央値（塊の中の揺れ） |
| `edge_width_median_px` | 縁の幅の中央値（画素） |
| `edge_measured_fraction` | 標本にした外周の画素のうち、縁の幅を測れた割合 |

縁の幅: 塊の外周の画素（塊の画素で 8 近傍に塊の外がある物。12000 を超えたら等間隔に間引く）ごとに、不透明度の勾配（3 × 3 の Sobel）の
向きの直線を ±64 画素、0.25 画素の歩で双線形に標本し、中心から戻って 0.1 を切る位置と、そこから進んで「0.5 と塊の最大の 80 % の
小さい方」に届く位置を線形の補間で求め、その差を幅とする。その区間がほかの塊（`other_blob`）・画面の外（`screen_edge`）・空の外
（地形か 2° より下、`outside_sky`）を通る縁、64 画素の内に下の閾値（`no_lower`）か上の閾値（`no_upper`）に届かない縁、勾配の無い縁
（`no_gradient`）は測れない縁として除き、理由ごとの数を報告に書く。双線形の標本は補間に使う 4 画素（重みが 0 でない物）の全部が画面の内・空の内のときだけ閾値の交差に使い、1 画素でも画面の外か空の外を含む標本が区間に入れば `screen_edge` か `outside_sky` にする（画面の外の値や空の外の 0 を混ぜた架空の交差を作らない）。限界: 片側 64 画素より広い縁は `no_lower` か `no_upper` になって
中央値から抜けるので、ぼけた縁の幅は小さめに出る（その分 `edge_measured_fraction` が下がる）。

### 独立の見本（Poly Haven の HDRI）

`Assets/Reference/SkySamples/`（git 管理外）。取得は `python Scripts/Terrain/FetchSkySamples.py`（取得の前に名前と大きさを出力し、
30 MB を超える物は落とさず、API の MD5 と照らす）。すべて CC0（https://polyhaven.com/license）、2k の Radiance .hdr。

| 名前 | 作者 | 大きさ（バイト） | MD5 |
|---|---|---|---|
| `kloofendal_48d_partly_cloudy_puresky` | Greg Zaal, Jarod Guest | 5451493 | 2eba3a4d7eeb23cbfbeca364c97e7980 |
| `kloofendal_38d_partly_cloudy_puresky` | Greg Zaal, Jarod Guest | 5294817 | 38ee83ed18b97311af058e41056999ea |
| `sunflowers_puresky` | Jarod Guest, Sergej Majboroda | 5658712 | 2609ea973b9b1a3cdc4234961606ebc2 |

地面の映らない pure skies の、昼の部分曇り（積雲と高積雲）の空を選んだ。候補だった `kloppenheim_05_puresky`・`rustig_koppie_puresky` は
cloud_sky の画角の中がほぼ晴れで「雲なし」に近く、見本の幅に雲なしが入るので外した（取得して見た後に消した）。

投影: cloud_sky の画面の大きさ・カメラ・太陽の向き（`cloud-shape.json` の `sample_view` に写した）で画素ごとの視線を求め、
正距円筒の HDRI を双線形で引く。方位は HDRI の最も明るい画素（太陽）の方位を撮影の太陽の方位に合わせ、そこから 120°・180°・240°
回した 3 つ（見本 9 枚）。太陽から 45° 以内と仰角 2° 以下は測る画素から除く。雲のマスクは赤 ÷ 青 > 0.6（全天カメラの雲の判別で
使われる固定の比の閾値）。見本のマスクの画は `Assets/Reference/SkySamples/masks/<名前>_<方位>.png`（左: 投影した空、右: 塊の色分け。
茶は除いた画素）、値は同じ置き場の `sample_values.json`。

マスクの近似と限界:

- マスクは 0 か 1 なので、測れるのは切れ間・塊の数・面積の分布・最大の塊の割合だけ。薄い膜・厚い部分・塊の中の揺れ・縁の幅は
  見本から決められない。
- 太陽の周りの光輪は空でも白っぽく、赤 ÷ 青が 0.6 を超えて雲に入る。太陽を画面に入れる向き（0°・90°）では光輪と周りの雲が 1 つの
  塊につながった（sunflowers の 0° で最大の塊の割合 0.99）ので、その向きは使わない。120° でも sunflowers は太陽の側の明るい空が
  一部雲に入る（最大の塊の割合 0.69）。
- 薄い巻雲や高積雲の筋も雲に入り、積雲どうしをつなぐことがある。雲の底の灰色の部分は赤 ÷ 青が 0.8〜0.9 で雲に入る。
- HDRI の 2k は約 5.7 画素/度で、cloud_sky（中央で約 10 画素/度）より粗い。最小の面積に近い小さな塊の数は投影の補間で変わる。
- 正距円筒の方位の向き（鏡像）は決めていない。数・面積の分布は鏡像で変わらない。

| 見本 | 切れ間 | 塊の数 | 面積の中央値 | 面積の 90 百分位 | 90 百分位 ÷ 中央値 | 最大の塊の割合 |
|---|---|---|---|---|---|---|
| kloofendal_38d@120 | 0.6389 | 14 | 0.00101 | 0.06793 | 67.26 | 0.5706 |
| kloofendal_38d@180 | 0.7126 | 11 | 0.0009216 | 0.03276 | 35.55 | 0.7392 |
| kloofendal_38d@240 | 0.7445 | 4 | 0.04081 | 0.1423 | 3.487 | 0.6698 |
| kloofendal_48d@120 | 0.7794 | 7 | 0.004237 | 0.09648 | 22.77 | 0.5531 |
| kloofendal_48d@180 | 0.7780 | 13 | 0.007445 | 0.03765 | 5.057 | 0.3552 |
| kloofendal_48d@240 | 0.7165 | 12 | 0.01093 | 0.05327 | 4.874 | 0.4891 |
| sunflowers@120 | 0.6119 | 7 | 0.003489 | 0.1600 | 45.86 | 0.6857 |
| sunflowers@180 | 0.5531 | 13 | 0.007337 | 0.1080 | 14.71 | 0.3270 |
| sunflowers@240 | 0.5906 | 17 | 0.002593 | 0.06622 | 25.54 | 0.3993 |

### 合否の定義（調整の前に固定）

行はすべて cloud_sky に掛ける（`views`）。ref_far_01 は空の大半が仰角 2〜10° で、見本の投影は cloud_sky の向きで決めたので判定しない
（値は記録する）。値の求まらない量（塊が無いときの面積など）は落ちる。

見本の行（`basis: samples`）: 9 枚の最小・最大を、割合は幅の 25 % ずつ、数・面積・比は 1.25 倍ずつ外へ広げた（数は整数へ外向きに丸めた）。

| 量 | 下限 | 上限 |
|---|---|---|
| 切れ間の割合 | 0.4966 | 0.8359 |
| 塊の数 | 3 | 22 |
| 面積の中央値 | 0.000737 | 0.051 |
| 面積の 90 百分位 | 0.0262 | 0.2 |
| 90 百分位 ÷ 中央値 | 2.79 | 84.0724 |
| 最大の塊の割合 | 0.224 | 0.8423 |

美術上の判断の行（`basis: art`。不透明度でしか測れない）:

| 量 | 下限 | 上限 | 理由 |
|---|---|---|---|
| 薄い膜の割合 | — | 0.10 | 写真の積雲の半透明の縁は塊の外周の数画素の帯で、空の 1 割を超える膜は晴れの空を一様に覆う膜に見える（2026-10-23 の親の所見の欠陥そのもの） |
| 厚い部分の割合 | 0.5 | 0.95 | 積雲の本体は光学的に厚く、塊の過半は 0.6 以上。0.95 を超えると縁の階調の無い切り抜き |
| 塊の中の揺れ | 0.08 | 0.30 | 塊の中に厚い芯と薄い縁・ちぎれがある。0.08 未満は一様な斑点、0.30 を超えるのは画素ごとの雑音 |
| 縁の幅の中央値 | 1.5 画素 | 12 画素 | cloud_sky（横 118°、中央で約 10 画素/度）で、積雲の縁の移り変わりは 1° 前後まで。1.5 画素未満は急な切り抜き、12 画素を超えるとぼけた綿 |
| 測れた縁の割合 | 0.25 | — | 縁の幅の中央値が外周の偏った一部にならないための下限 |

### 合成の画像の試験（`--cloud-shape --selftest`）

1920 × 1080 の全画素を空として測る。固定した定義で次が落ちる（落ちる行の数）: 一様な層（0.27、11 行）、雲なし（10 行）、
細かなノイズ（画素ごとに 0〜1 の一様乱数、8 行: 1 つの塊につながる）、粗い斑点（不透明度 0.7・半径 140 画素の独立した斑点 10 個・縁が急、
5 行: 切れ間 0.70 と塊の数 10 は見本の幅に入るが、面積の比 1・最大の塊の割合 0.1・厚い部分 1・揺れ 0・縁の幅 0.59 画素で落ちる）、
穴の開いた膜（0.2 の膜に半径 60 画素の穴 40 個、11 行）。半径方向に 50 画素で 0 から 1 へ直線で上がる円盤の縁の幅は、期待 20 画素
（0.4 × 50）に対して 19.9996 画素、測れた縁 100 %。見本 9 枚はマスクで測れる 6 行をすべて通る。

### 今の値（SCN-WL-CLOUD-OPACITY-AOV の撮影。値は変えていない）

| 量 | cloud_sky | 定義での cloud_sky | ref_far_01（記録） |
|---|---|---|---|
| 空の画素 | 2073600 | | 991023（地形 1082577） |
| 切れ間の割合 | 0.4804 | 落ちる（下限 0.4966） | 0.0084 |
| 薄い膜の割合 | 0.1428 | 落ちる（上限 0.10） | 0.0084 |
| 塊の数 | 6（成分 137 のうち 131 は最小の面積の未満） | 通る | 1 |
| 面積の中央値 | 0.05205 | 落ちる（上限 0.051） | 0.9832 |
| 面積の 90 百分位 | 0.1214 | 通る | 0.9832 |
| 90 百分位 ÷ 中央値 | 2.333 | 落ちる（下限 2.79） | 1 |
| 最大の塊の割合 | 0.3349 | 通る | 1.000 |
| 画面の端に触れる塊 | 5 | （記録） | 1 |
| 厚い部分の割合 | 0.4993 | 落ちる（下限 0.5） | 0.9804 |
| 塊の中の揺れ | 0.1553 | 通る | 0.0929 |
| 縁の幅の中央値 | 49.39 画素（10・90 百分位 16.1・87.4） | 落ちる（上限 12） | 11.03 画素 |
| 測れた縁の割合 | 0.469（測れない縁: 下の閾値 4128・上の閾値 1103・ほかの塊 612・画面の端 522） | 通る | 0.119（空の外 3043・下の閾値 3280・画面の端 114） |

cloud_sky は 6 行で落ちる: 縁が美術上の上限の 4 倍ぼけ（49 画素 ≈ 5°）、薄い膜が空の 14 %、塊が同じ大きさばかり（比 2.3）。ref_far_01 は
空全体が 1 つの塊（切れ間 0.8 %）で、晴れの写真の視点で白い曇り空に見える所見と合う。

### 確かめたこと

- `verify-SCN-WL-CLOUD-SHAPE-METRIC-1.txt`（`--cloud-shape --selftest --expect cloud-shape.json` result=pass）、`-2.txt`（`--cloud-shape --engine
  .../SCN-WL-CLOUD-OPACITY-AOV --views cloud_sky,ref_far_01 --verify` result=pass、`inputs_current=1`）。固定した定義での今の cloud_sky の判定は
  `aux-SCN-WL-CLOUD-SHAPE-METRIC-engine-expect.txt`（result=fail、上の 6 行）。いずれも `.harness/runs/20261023-124311/`。
- 開いた画: 量の画 `.harness/runs/scene-capture/SCN-WL-CLOUD-OPACITY-AOV/cloud_shape_cloud_sky.png`・`cloud_shape_ref_far_01.png`（左が不透明度、
  右が塊の色分けと縁の点。cloud_sky は塊の周りに灰の膜の帯が広く、測れた縁の緑の点が塊の外周に並ぶ）、見本のマスク
  `Assets/Reference/SkySamples/masks/*.png`（積雲の塊と青い切れ間がマスクと合う。光輪と巻雲の混入は上の限界のとおり）。

## 形を変える前の撮影（SCN-WL-CLOUD-SHAPE-BEFORE）

雲の形の調整（SCN-WL-CLOUD-SHAPE-TUNE）と受け入れ（SCN-WL-CLOUD-SHAPE-ACCEPT）が比べる元。コードと値を変えずに今の雲のシェーダーで撮った。

### 撮った版

- Game.exe（RelWithDebInfo、コミット 11e1afca、作業ツリーの変更なし）: SHA-256 `d041326b223422fb6dce2fd61083cbccdcb60c8dd26e78ce965dc929ef44ae6f`
- 雲のシェーダーは起動時にソースから組み立てる（`VulkanShaderCompiler::CompileFromSource`。組み立て済みの SPIR-V は置かない）ので、ソースの SHA-256 を記録する。
  最後に変えたコミットは 26771864。
  - `Assets/Shaders/cloud_layer.frag`: `39793e56aab649406c3898b616289f59255735a5280c14fea6498ab63a1b1359`
  - `Assets/Shaders/Common/CloudLayerCommon.glsl`: `b000287e91465c4ef80e08c4e9cc009511776bfc4eee3e4251afa475aef1f0ba`
  - `Assets/Shaders/cloud_layer_opacity.frag`（不透明度の画像。既定の描画では使わない）: `87a026729af42acc28b12bff6052867c295ac54aeaa1cbeb5b114dbfb6c68fbe`
- 撮影の入力の記録（`far-flicker/before/inputs.json`）: sha256 `278b65b7…`、files 689・asset_sources 189・asset_missing 0。

### 置き場と値（`.harness/runs/` の下）

| 組 | 置き場 | 中身 |
|---|---|---|
| ちらつきの連番 | `scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/before` | ref_far_01、`-Hdr`、台本 `wasteland-cloud-flicker.txt` の `seq-0`〜`seq-7`（8 枚、7 フレームおき）の PNG と PFM |
| 段 | `scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/banding` | ref_far_02 の既定の撮影の PNG |
| 6 視点 | `scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/views` | ref_far_01・ref_far_02・ref_cliff_01・ref_cliff_02・ref_ground_01・ref_ground_02 |
| GPU 時間 | `scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/timing` | ref_far_01、300 フレーム |
| 回帰 | `regression/SCN-WL-CLOUD-SHAPE-ACCEPT/before` | rendering3dtest・mega-bananas・sandbox・night-village の overview（検証レイヤーあり） |

GPU 時間（ref_far_01、300 フレーム、記録だけで合否にしない）: 中央値 13.95 ms・平均 14.05 ms・95 百分位 17.47 ms・最大 20.75 ms（予算 33.3 ms に収まる）。
処理ごとの中央値は FoliagePass 3.31・LightingPass 2.55・**CloudLayerPass 2.35**・VisibilityRasterPass 1.34・MegaGeometry 1.34 ms
（CloudLayerPass の 95 百分位 4.09 ms・最大 4.46 ms）。CPU は中央値 27.6 ms（ゲームスレッドの中央値 27.4 ms）。

### 開いた画

- 6 視点: どの視点も雲の層が空を広く覆う。ref_far_01・ref_cliff_02・ref_ground_02 は空のほぼ全部が白い薄い膜で、青い切れ間がほとんど無い。
  ref_far_02・ref_ground_01・ref_cliff_01 は青い切れ間があるが、塊の縁が広くぼけて層の帯に見える（上の量の表と合う）。
- 段（`banding/ref_far_02.png`）: 雲の明るさに等高線のような階調の段がはっきり出る（とくに画面の上半分の塊の縁と右上）。
- ちらつきの連番: 8 枚とも撮れ、画面の平均の明るさは 8 枚で同じ。
- 回帰: 4 シーンの overview が撮れ、`capture.txt` は all_ok=1。

### 確かめたこと

`.harness/runs/20261023-124311/` の `verify-SCN-WL-CLOUD-SHAPE-BEFORE-1.txt`（RelWithDebInfo のビルド EXIT=0）・`-2.txt`（連番 result=pass、
台本の capture 8 個と PNG 8 枚が一致）・`-3.txt`（段 result=pass）・`-4.txt`（6 視点 result=pass）・`-5.txt`（GPU 時間 result=pass、
within_budget=True）・`-6.txt`（回帰 result=pass、482 秒）。

## 形の調整（SCN-WL-CLOUD-SHAPE-TUNE）

一様な薄い層を、はっきりした縁と青空の切れ間のある積雲の塊にした。合否は「塊・切れ間・縁の量」の定義（`cloud-shape.json`、変えていない）。

### 美術上の判断: 天気は far_01 の部分曇り

荒野の天気は写真 `Assets/Reference/ValleyOfTheGods/far_01.jpg` の部分曇りに合わせる。底の平らな積雲が地平線の上に列になって重なり、
上の空は青く開き、塊の間から青空がのぞく。晴れの写真 ground_02・cliff_01 は雲の少ない別の日の天気で、雲の量の違いは天気の違いとして扱い、
雲の形の合否には使わない（両方に同時に合わせる値は無い）。

### 変えたもの

| 場所 | 前 | 後 | 理由 |
|---|---|---|---|
| `WastelandRoutine.cpp` の被覆 `kCloudCoverage` | 0.43 | 0.35 | 狭い覆いの閾値と組で、切れ間を空の 7 割強にする（0.37 では 5 割を切る） |
| `WastelandRoutine.cpp` の形の大きさ `kCloudShapeScale` | 4500 m | 2500 m | 天気の模様の周期 16.7 km → 9.25 km。cloud_sky の画角に大小の塊が 15 前後入る（前は空の半分を覆う大きな膜 6 つ） |
| `CloudLayerCommon.glsl` の覆いの閾値の幅 | 1 − 被覆 −0.08〜+0.22 | −0.03〜+0.08（`kCoverageEdgeBelow`・`kCoverageEdgeAbove`） | 塊の外形を天気の模様の等値線で締め、外側の薄い膜を空の 1 割未満にする |
| `CloudLayerCommon.glsl` の密度の倍率 | なし | 3（`kCloudDensityGain`、1 で頭打ち） | 塊の本体を光学的に厚くし、縁の薄い所だけを半透明に残す |

雲の底 2500 m・頂 3600 m・消散係数・細部と天気の比・遠くの細部のミップは変えていない。密度の式の写し（`CloudDumpCheck.py` の `CloudModel`。
`CloudBreakdown.py`・`CloudVolume.py` はそれを使う）と書き出しの契約（`WastelandCloudDump.md`）を同じ変更で直した。

値は、SCN-WL-CLOUD-OPACITY-AOV の cloud_sky の書き出し（格子・定数）から 1/4 の解像度で不透明度を求め直す試算（今の式で撮影との
平均の差 0.003）で被覆・閾値・倍率・大きさを振って決め、撮影で確かめた。

### 結果（`.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE`）

撮った Game.exe（RelWithDebInfo、コミット 320fbe2c にこの変更を載せた作業ツリー）の SHA-256 `ff4fbd2a3d073b632e6d9a63b753383f158d6481c0b12375c48ccbddca890646`、
`CloudLayerCommon.glsl` の SHA-256 `692308d36b8673b81ea7a8215adbf95bde2d900f0a9057adff57713378b70a17`（`cloud_layer.frag` は前と同じ 39793e56…）。

| 量 | cloud_sky（前） | cloud_sky（後） | 定義 | ref_far_01（後、記録） |
|---|---|---|---|---|
| 切れ間の割合 | 0.4804 | 0.7302 | 0.4966〜0.8359 | 0.1316 |
| 薄い膜の割合 | 0.1428 | 0.0353 | 〜0.10 | 0.0453 |
| 塊の数 | 6 | 17 | 3〜22 | 2 |
| 面積の中央値 | 0.05205 | 0.003198 | 0.000737〜0.051 | 0.4115 |
| 面積の 90 百分位 | 0.1214 | 0.04301 | 0.0262〜0.2 | 0.7346 |
| 90 百分位 ÷ 中央値 | 2.333 | 13.45 | 2.79〜84.07 | 1.785 |
| 最大の塊の割合 | 0.3349 | 0.3512 | 0.224〜0.8423 | 0.9905 |
| 厚い部分の割合 | 0.4993 | 0.8196 | 0.5〜0.95 | 0.9183 |
| 塊の中の揺れ | 0.1553 | 0.164 | 0.08〜0.30 | 0.1848 |
| 縁の幅の中央値 | 49.39 画素 | 8.87 画素（10・90 百分位 4.2・24.3） | 1.5〜12 | 9.17 画素 |
| 測れた縁の割合 | 0.469 | 0.829 | 0.25〜 | 0.642 |

cloud_sky は定義の 11 行すべてで通る。ref_far_01 は地平線の方向へ積雲の列が重なって 1 つの大きな塊につながる（記録だけ）。切れ間は前の 0.008 から 0.13 になった。

### 開いた画

- `cloud_sky.png`: 青空に大小の積雲の塊が散らばり、縁は白く締まり、斑点や細かなノイズには見えない。右上の大きな塊の一部に縦の直線の縁が
  ある。不透明度の画では、塊の外形のところどころが角張った折れ線になる（閾値を狭めて、天気の模様の格子の線形補間の折れ目が縁に出る見込み。
  原因は確かめていない）。
- `ref_far_01.png` と写真 `far_01.jpg`: 地平線の上に底の平らな積雲が列になって重なり、上の空は青く開く。写真の部分曇りと雲の量・並びが近い。
  前の撮影（空のほぼ全部が白い薄い膜）の曇り空ではなくなった。
- 量の画 `cloud_shape_cloud_sky.png`・`cloud_shape_ref_far_01.png`: 塊の外周に薄い膜の灰の帯がほとんど無く、測れた縁の点が塊の外周に並ぶ。

遠景のちらつき・段・6 視点・回帰・GPU 時間の受け入れは SCN-WL-CLOUD-SHAPE-ACCEPT で、比べる元は「形を変える前の撮影」の置き場。

### 確かめたこと

`.harness/runs/20261023-124311/` の `verify-SCN-WL-CLOUD-SHAPE-TUNE-1.txt`（Debug のビルド EXIT=0）・`-2.txt`（RelWithDebInfo のビルド EXIT=0）・
`-3.txt`（撮影 result=pass）・`-7.txt`（`--cloud-shape --expect cloud-shape.json --verify` result=pass）。
照合の台本を直した後に撮り直した結果は `-8.txt`（Debug のビルド EXIT=0）・`-9.txt`（RelWithDebInfo のビルド EXIT=0）・`-10.txt`（撮影 result=pass）・
`-11.txt`（`--cloud-dump --verify` result=pass、全部の視線の透過率の差の最大は下の「照合の直し」）・`-12.txt`（`--cloud-shape --expect cloud-shape.json --verify`
result=pass、量は上の表と同じ）。`-4.txt` は照合の台本を直す前の結果（ref_far_01 で空でない視線と、画素から求めた向きで進めた遠い視線の透過率の差
0.060 で fail）。

### 照合の直し

`--cloud-dump` の透過率の照合は、地形の入る視点の空でない画素の視線も含めて全部の視線で 0.01 以内を求める。倍精度で歩の位置と高度を
求めると、ref_far_01 の地平線の近くを長く進む視線（画素 (780.5, 780.5) など）は単精度の高度の丸め（惑星の中心から 6.36e6 m で 0.5 m）の差で
縁の締まった雲を別の所へ抜け、差が 0.031 になる。歩の位置を GPU の書き出した区間に固定し、位置・高度を GPU と同じ単精度で求めると、
同じ撮影で全部の視線の差の最大は cloud_sky 0.0018・ref_far_01 0.0019（空でない視線 73 本では 0.0003、画素 (780.5, 780.5) は 0.0002）。
区間・歩数・1 歩の長さは自分で求めた値で GPU と照らす（相対差の最大 cloud_sky 2.7e-4・ref_far_01 1.1e-4）。

## 遠い雲の段と直線の縁（SCN-WL-CLOUD-CONTOUR-FIX）

所見（2026-10-24、SCN-WL-CLOUD-SHAPE-TUNE の撮影）: (1) ref_far_01 の地平の上の雲が、平らな色の段を等高線のように重ねた絵に見え、段の縁が硬い。
(2) cloud_sky の雲のいくつかに、画面の縦横に沿った直線の縁と四角い角がある（中央の縦長の雲の下端・右の大きな雲の左の縁）。

### 原因の切り分け

雲の書き出し（`-GameArgs cloud-dump`）の格子と定数から組み立てた式の写し（`CloudDumpCheck.CloudModel`）で候補を 1 つずつ替えて
画面の一部を求め直し、描画で確かめる候補は GLSL だけを替えて撮った（雲のシェーダーは起動時にソースから組み立てるので、
撮影の間にビルドは要らない。撮影の置き場は `.harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX-try/`）。

| 候補 | 確かめ方 | 結果 |
|---|---|---|
| 表示の量子化 | ref_far_01 の PNG の空の行の G の値 | 表示は既にディザ済み（`blit.frag`）。段は 198〜212 の幅の中の平らな部分（同じ値が 10〜20 画素）と、1 画素に 1 段ずつ変わる坂の繰り返しで、放射輝度そのものに平らな部分がある。量子化の段ではない |
| 3D ノイズの補間（8 bit の値の 3 次元の線形の補間） | 写しで形のノイズと天気の模様を 3 次の B スプラインの補間に替える | cloud_sky の中央の雲の四角い下端は残る（外形は少し縮むだけ）。補間の折れが原因ではない |
| 歩の数 | 写しで診断の切り替え（最大 1024 歩） | cloud_sky・ref_far_01 とも視線の歩数が上限 192 に届かず、値は同じ |
| 密度の倍率 3 の頭打ち | 写しで頭打ち（1）を外す | 塊の中の密度は変わるが、太陽への光学的深さが大きくなって陰が環境光だけになり、平らさは変わらない |
| 形のノイズの向き | 写しで形のノイズの座標を 0.5 rad 回す | cloud_sky の中央の雲の外形が変わる。外形は形のノイズ（世界の軸に沿った 4 格子の Perlin-Worley）が決める |
| 太陽への光の歩の位置 | GLSL で 6 回の点を区間の中点から主の歩と同じ端数へ替えて撮る | ref_far_01 の塊の中の硬い縁の段が柔らかくなる（下の表）。**段の原因** |
| 雲の底の平らな面 | `--cloud-shape` の直線の縁の量の画（直線と数えた区間に印）を開く | 両方の視点で、画面の横に沿ってまっすぐ続くと数えた縁は雲の底（層の底 2500 m で 77 m の間に立ち上がる平らな面）。**直線の縁の原因** |

段の仕組み: 太陽への光学的深さは 6 回の点（区間 70・126・227・408・735・1323 m）で数え、後半の 1 点が数百 m〜1.3 km の区間を代表する。
点を区間の中点に固定すると、点が塊の急な縁（狭い覆いの閾値と密度の倍率 3 で縁が締まっている）をまたぐ所で光学的深さが 1323 m × 0.006 /m ≈ 8 まで跳び、
陰が硬い縁で区切られた平らな段になる。遠景では塊の陰の側が環境光でほぼ一様に照らされるので、段が等高線のように重なって見える。

### 直したもの

`Common/CloudLayerCommon.glsl` の太陽への光の歩の点を、区間の始まり ＋ 区間の長さ × 主の歩と同じ画素ごとの端数にした（フレームごとに位相が変わり、
時間の蓄積で区間の平均に均される）。式の写し `CloudDumpCheck.py`（`sun_term` の `jitter`）と `CloudBreakdown.py`、契約 `WastelandCloudDump.md` を同じ変更で直した。

### 結果（`.harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX`、証拠は `.harness/runs/20261024-183517/verify-SCN-WL-CLOUD-CONTOUR-FIX-1〜8.txt`）

| 量 | 前（SCN-WL-CLOUD-SHAPE-TUNE） | 後 | 合否 |
|---|---|---|---|
| `BandingCheck.py`（ref_far_01、行 0〜450）平らな帯の長さの平均 / 95 % 点 | 2.306 / 7 | 1.982 / 5 | 合格（前より短い） |
| 同 局所の平均の差の平均 / 最大 | — | 0.248 / 2.30 段 | 合格（0.5 段以下） |
| `--cloud-shape` 直線の縁の量 cloud_sky / ref_far_01 | 0.0090 / 0.0481 | 0.0090 / 0.0481 | **未達**（目標は半分以下。光の直しは不透明度を変えない） |
| `cloud-shape.json`（cloud_sky） | 全行合格 | 全行合格（不透明度は同じ） | 合格 |
| `--cloud-dump --verify` 透過率の差の最大 cloud_sky / ref_far_01 | 0.0018 / 0.0019 | 0.0018 / 0.0019 | 合格（許容は変えていない） |
| GPU 時間（ref_far_01、300 フレーム、RelWithDebInfo）中央値 / 95 % 点 | 18.12 / 23.24 ms | 18.38 / 23.63 ms | 予算 33.3 ms 内 |
| 同 CloudLayerPass の中央値 / 95 % 点 | 2.12 / 4.21 ms | 2.60 / 4.47 ms | 光の点が散って標本の局所性が落ちた分 |

直線の縁の量（`CloudShapeCheck.measure_straight_edges`）: ぼかした（σ 1.5 画素）不透明度の 0.3 の等値線が隣り合う 2 画素の間を横切る所を縁の 1 画素とし、
同じ行（列）に 20 画素以上続き、画素の間の位置の幅が 0.5 画素以内の区間をまっすぐな縁とする。合成の四角い雲で 1.0、半径 60 画素の丸い雲で 0
（`--selftest`）。cloud-shape.json の行には足していない。

### 開いた画

- `SCN-WL-CLOUD-SHAPE-TUNE/ref_far_01.png` と `SCN-WL-CLOUD-CONTOUR-FIX/look/ref_far_01.png`: 大きな塊の陰の中の硬い縁の段が柔らかい濃淡に変わる。
  塊の底の横に続く明るい縁と、塊どうしの重なりの段（遠い塊ほど霞む）は残る
- `SCN-WL-CLOUD-SHAPE-TUNE/cloud_sky.png` と `SCN-WL-CLOUD-CONTOUR-FIX/cloud_sky.png`: 外形は同じ。中央の縦長の雲の四角い下端と右の大きな雲の直線的な左の縁は残る

### 直線の縁の試し（採っていない）

雲の底の立ち上がりの始まりを模様で層の厚さの 0〜0.08 持ち上げると、ref_far_01 の直線の縁の量は下がる（天気の細かい模様で 0.0305、
形のノイズの Worley 8・16 格子の成分で 0.0204）が、cloud_sky は 0.0153・0.0178 に上がり、ref_far_01 の `BandingCheck.py` の局所の平均の差が
1.15・1.18 段になって 0.5 段の合否を超える（`-try/base-var-dump`・`-try/base-shape-dump`）。雲の外形を変える直しは、遠景の外形の縁が動く分だけ
局所の平均が変わるので、同じ視点の局所の平均の差 0.5 段以下と両立しない。

## 形のノイズの作り直し（SCN-WL-CLOUD-SHAPE-NOISE）

SCN-WL-CLOUD-CONTOUR-FIX の後に cloud_sky に残った四角い縁（中央の雲の下端・右の大きな雲の左の縁）と、ref_far_01 の遠い雲の縦の筋を消す。
試しは GLSL だけを替えて撮った（置き場 `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE-try/`）。

### 試しと選んだ形

| 試し | cloud_sky 横 / 縦（画素） | ref_far_01 横 / 縦（画素） | 画 |
|---|---|---|---|
| 前（SCN-WL-CLOUD-CONTOUR-FIX） | 36 / 49（合計 0.0090） | 365 / 22（縦 0.0027） | 中央の雲の四角い下端・右の雲の直線の左の縁。ref_far_01 の左上の薄い雲に縦の筋 |
| 形のノイズの格子を傾けて回す（`rot25`） | 132 / 0（0.0145） | 474 / 47（縦 0.0060） | 中央の雲の四角い下端は消える。右の雲の左の縁と ref_far_01 の縦の筋は残る |
| 回す ＋ 天気の模様を引く位置を 3D の歪みでずらす（`warp400`、採った形） | 41 / 0（0.0041） | 182 / 0（縦 0） | 四角い縁と縦の筋が消え、側面が不揃いな積雲の塊になる |

回すだけで縦の筋が残ったのは、天気の模様（覆いの印）が雲の位置の x・z だけで決まり、覆いの閾値を狭く取った塊の外形が
高さ方向にまっすぐ押し出された柱になるため。横から見る ref_far_01 では柱の側面が縦の直線、見上げる cloud_sky では右の雲の左の縁になる。

採った形（`CloudLayerCommon.glsl` の `SampleCloudDensity`）:

- **形のノイズの格子の回転**: テクスチャ座標 = R × ((p + 風のずれ) / 周期)。R は四元数 (4, 1, 2, 2) の回転（行 (9, −12, 20)・(20, 15, 0)・(−12, 16, 15) / 25）で、
  格子のどの軸も世界の縦・横に沿わない。成分が 1/25 の整数倍なので、風のずれを周期の 25 倍で折り返す（`CloudLayerPass.inl`）と回した後の座標は整数だけずれ、
  折り返した瞬間に雲が飛ばない。任意の角度の回転では、周期で折り返した瞬間に形のノイズが飛ぶ。
- **天気の模様の歪み**: 形のノイズの格子を同じ回した座標 ＋ 位相 (0.37, 0.53, 0.19) で引いた G・B を (値 − 0.5) × 2 × 400 m の水平のずれにして、
  天気の模様を引く (x, z) に足す。歪みは高さでも変わるので、塊の側面が鉛直の壁にならない。400 m は層の厚さ 1100 m・形のノイズの Worley の格子 625 m に対して、
  側面の傾きが場所ごとに変わる大きさ。歪みは覆いの印より前に引くので、層の中の全部の歩で 3D ノイズを 1 回多く引く。
- 雲の底の平らな面（横の直線）は積雲の凝結高度の姿なので変えていない。

式の写し `CloudDumpCheck.py`（`shape_coordinate`・`weather_warp`・`coverage_mask` の歪み・形のずれの折り返しの周期）と、Blender の正解の体積を焼く
`CloudVolume.bake_density`（覆いの印を列ごとから点ごとに）、契約 `WastelandCloudDump.md` を同じ変更で直した。

### 結果（`.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE`、証拠は `.harness/runs/20261024-183517/verify-SCN-WL-CLOUD-SHAPE-NOISE-1〜7.txt`）

| 量 | 前（SCN-WL-CLOUD-CONTOUR-FIX） | 後 | 合否 |
|---|---|---|---|
| `--cloud-shape` 直線の縁 cloud_sky 横 + 縦 | 0.0038 + 0.0052 = 0.0090 | 0.0041 + 0 = 0.0041 | 合格（0.0045 以下） |
| 同 ref_far_01 縦（横は記録） | 0.0027（横 0.0453） | 0（横 0.0218） | 合格（0.00137 以下） |
| `cloud-shape.json`（cloud_sky） | 全行合格 | 全行合格（切れ間 0.704・塊 13・面積の比 10.38・厚い部分 0.804・縁の幅 9.90 画素） | 合格 |
| `--cloud-dump --verify` 透過率の差の最大 cloud_sky / ref_far_01 | 0.0018 / 0.0019 | 0.0027 / 0.0036 | 合格（許容 0.01 は変えていない） |
| `BandingCheck.py --no-worse-bands`（ref_far_01、行 0〜450）平らな帯の平均 / 95 % 点 | 1.982 / 5 | 1.89 / 5 | 合格（前の 1.1 倍以内）。局所の平均の差 2.76 段（外形が動いた分、記録だけ） |
| GPU 時間（ref_far_01、300 フレーム、RelWithDebInfo）中央値 / 95 % 点 | 18.38 / 23.63 ms | 14.16 / 15.65 ms | 予算 33.3 ms 内 |
| 同 CloudLayerPass の中央値 | 2.60 ms | 3.30 ms | 歪みの 3D ノイズの 1 回分 |

GPU の全体の中央値が下がったのは雲以外のパスの揺れ（同じ置き場の前の計測は `SCN-WL-CLOUD-CONTOUR-FIX/timing-after`、後は `SCN-WL-CLOUD-SHAPE-NOISE/timing-after`）。

### 開いた画

- `SCN-WL-CLOUD-CONTOUR-FIX/cloud_sky.png` と `SCN-WL-CLOUD-SHAPE-NOISE/cloud_sky.png`: 中央の雲の四角い下端と、右の大きな雲の縦にまっすぐな左の縁が消え、
  外形が不揃いに盛り上がる積雲の塊になる。雲の量と切れ間の割合はほぼ同じ
- `SCN-WL-CLOUD-CONTOUR-FIX/look/ref_far_01.png` と `SCN-WL-CLOUD-SHAPE-NOISE/look/ref_far_01.png` の空（行 0〜450）: 左上の薄い雲の縦の筋が消える。
  塊の側面は柱でなく、上へ盛り上がる積雲の形。雲の底の横に続く縁は残る（変えない対象）

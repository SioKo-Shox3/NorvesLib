# 荒野の雲の形

荒野の雲を一様な薄い層から切れ間のある積雲へ変えるときの、比べの基準と形の量を置く文書。
雲の Cycles の比べの仕組みは `WastelandCloudBlender.md`、書き出しの契約は `WastelandCloudDump.md`、
前の正解（SCN-WLR-CLOUD-REF）との比べと差の一覧は `WastelandCloudRef.md`。

## 基準（2026-10-24 の密度場）

雲の形を変える前の、今のコードの書き出しとその Cycles の正解の組。後の雲の項目の比べ（`--cloud --expect` など）の基準の値はこの表。
値は測っただけで、エンジンの値と既定は変えていない。判定日: 2026-10-24。ブランチ `feature/scene-launcher`。タスク SCN-WLR-CLOUD-REF-CURRENT。

### 組と条件

- エンジン: `.harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump`（Game.exe ab0adb16…、RelWithDebInfo、入力の記録の
  コミット 1c32fa29）。cloud_sky（南南西を仰角 45° で見上げる、1920 × 1080）の雲の書き出し（`-GameArgs cloud-dump`）。
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

# 荒野の昼の表示の変換を、保存した HDR でオフラインに選ぶ（暫定）

SCN-WL-LOOK-MEASURE（`WastelandLook.md`）で保存した 6 視点の HDR に、表示の変換の候補をオフラインで掛け、晴れの写真
（far_01・cliff_01・ground_02）の `--look` の固定の領域との距離で並べて、荒野の昼のカメラの表示の変換を選んだ記録。
**この選択は暫定**で、最終の確認は植物・地面の後（確認 3）に置く。写真との距離は写真の現像・天気・材質の差を含むので、
作風を選ぶ参考に留め、物理の正しさの証拠にしない。

## 距離の式と選び方（評価の前に決めたもの）

- 候補（`Scripts/Terrain/LookSelect.py`。式は `Scripts/Terrain/DisplayTransform.py` の再現。ブルームと FXAA は入れず、ビネットは撮影の値のまま）:
  曲線 7 通り × 露出の補正 −1.0・−0.5・0・+0.5・+1.0・+1.5 EV（今の荒野の露出〔`kCameraOutputCompensationEV` 込み〕からの差。
  HDR の撮影のプリエクスポージャに 2^Δ を掛ける）= 42 候補。

| 曲線 | 中身 | 荒野の昼のカメラだけで選べるか |
|---|---|---|
| current | NeutralLinear・中立のグレーディング（コントラスト 1・彩度 1・色温度 0。今の荒野） | ○（`CameraToneMapCurve::NeutralLinear`） |
| neutral_old_grade | NeutralLinear・コントラスト 1.35・彩度 1.2・色温度 0.05（SCN-WLR-TONEMAP-EVAL の前の荒野） | ○ |
| neutral_mild | NeutralLinear・コントラスト 1.15・彩度 1.1・色温度 0.05（`WastelandToneMapEval.md` の候補） | ○ |
| aces20_lut | ACES 2.0 SDR の LUT（グレーディングを掛けない） | ×（View の演算子。全シーン共通の `--tone-map`） |
| aces_filmic | ACES Filmic の近似（肩のある filmic、View の既定の演算子）・中立のグレーディング | ○（`CameraToneMapCurve::ViewDefault`） |
| uncharted2 | Uncharted 2（肩のある filmic、白 11.2）・中立 | ×（View の演算子） |
| reinhard | Reinhard・中立 | ×（View の演算子） |

- 距離: 写真 p・領域 r（`LookRegions.json` の 青空〔雲を除く〕・近景の日なたの地面・崖・3 km より先の地形の帯。写真と撮影の雲は
  合っていないので雲は使わない。写真に無い領域は飛ばす）ごとに

  e(p, r) = |log2(Y_c ÷ Y_p)| + |log2(RG_c ÷ RG_p)| + |log2(BR_c ÷ BR_p)|

  Y は線形の平均の輝度、RG = R ÷ G、BR = B ÷ R（sRGB の 8 bit を線形に戻した表示の値の領域の平均）。c は候補を掛けた撮影、p は写真。
  重みは領域ごとに 1（地面と崖の明るさの差は植生・材質の差を含むので、空より重くしない）。d(p) は写真に在る領域の e の平均、
  **距離 D = 3 写真の d の平均**（小さいほど写真に近い）。
- 白飛び・黒つぶれ: 6 視点の画面全体で、どれかのチャンネルが 254 以上・すべてのチャンネルが 4 以下の画素の割合（視点の平均）。
- 選び方: 荒野の昼のカメラだけで選べる曲線で、白飛び・黒つぶれがどちらも今の設定（current・±0 EV）の値 + 0.01 % 以下の候補のうち
  D が最小の候補。その D が今の設定の D より 2 % 以上小さくなければ、今の設定のまま変えない。

## 結果

```
python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/display-check --look-select .harness/runs/scene-capture/SCN-WL-LOOK-MEASURE --verify
```

HDR は SCN-WL-LOOK-MEASURE の撮影（HEAD 1bdfc466、6 視点）。上位 12 候補（42 候補すべては `look_select.md`・`look_select.json`。
far_01・cliff_01・ground_02 の列は写真ごとの d、白飛び・黒つぶれは 6 視点の画面全体の平均）:

| 順位 | 候補 | 曲線 | 露出の補正 [EV] | 距離 D | far_01 | cliff_01 | ground_02 | 白飛び | 黒つぶれ | カメラで選べる | 選べる |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | aces_filmic-1.0EV | ACES Filmic の近似（肩のある filmic）・中立 | -1.0 | 1.471 | 0.978 | 1.465 | 1.970 | 0.000 % | 0.000 % | ○ | ○ |
| 2 | aces20_lut+0.0EV | ACES 2.0 SDR の LUT（グレーディングを掛けない） | +0.0 | 1.472 | 1.057 | 1.603 | 1.755 | 0.000 % | 0.000 % | × | × |
| 3 | current-0.5EV | NeutralLinear・中立のグレーディング（今の荒野） | -0.5 | 1.516 | 1.144 | 1.538 | 1.865 | 0.000 % | 0.000 % | ○ | ○ |
| 4 | current-1.0EV | NeutralLinear・中立のグレーディング（今の荒野） | -1.0 | 1.523 | 1.154 | 1.829 | 1.586 | 0.000 % | 0.000 % | ○ | ○ |
| 5 | aces20_lut-0.5EV | ACES 2.0 SDR の LUT（グレーディングを掛けない） | -0.5 | 1.538 | 1.054 | 1.898 | 1.662 | 0.000 % | 0.000 % | × | × |
| 6 | uncharted2+1.0EV | Uncharted 2（肩のある filmic、白 11.2）・中立 | +1.0 | 1.568 | 1.024 | 1.909 | 1.770 | 0.000 % | 0.000 % | × | × |
| 7 | aces_filmic-0.5EV | ACES Filmic の近似（肩のある filmic）・中立 | -0.5 | 1.573 | 1.185 | 1.132 | 2.403 | 0.000 % | 0.000 % | ○ | ○ |
| 8 | uncharted2+0.5EV | Uncharted 2（肩のある filmic、白 11.2）・中立 | +0.5 | 1.582 | 0.994 | 2.111 | 1.640 | 0.000 % | 0.000 % | × | × |
| 9 | current+0.0EV | NeutralLinear・中立のグレーディング（今の荒野） | +0.0 | 1.617 | 1.135 | 1.434 | 2.284 | 0.000 % | 0.000 % | ○ | ○ |
| 10 | reinhard-0.5EV | Reinhard（x ÷ (1 + x)）・中立 | -0.5 | 1.630 | 1.061 | 2.073 | 1.757 | 0.000 % | 0.000 % | × | × |
| 11 | neutral_mild-0.5EV | NeutralLinear・コントラスト 1.15・彩度 1.1・色温度 0.05（WastelandToneMapEval.md の候補） | -0.5 | 1.648 | 1.389 | 1.404 | 2.152 | 0.052 % | 0.000 % | ○ | × |
| 12 | neutral_mild+0.0EV | NeutralLinear・コントラスト 1.15・彩度 1.1・色温度 0.05（WastelandToneMapEval.md の候補） | +0.0 | 1.667 | 1.333 | 1.156 | 2.513 | 2.836 % | 0.000 % | ○ | × |

- 今の設定（current ±0 EV）は 42 候補中 9 位（D 1.617）。6 視点とも白飛び・黒つぶれは 0 %。
- 上位はどれも、今の撮影で写真より明るい地面と崖（`WastelandLook.md` の差 3）を暗くする側で、その代わりに青空が写真より暗くなる。
  領域ごとの e（Y・RG・BR の項）で見ると、ACES Filmic −1 EV は far_01 の地面の Y の項が 1.53 → 1.16、ground_02 の崖が 2.74 → 2.23、
  cliff_01 の崖が 0.79 → 0.20 に減り、青空の Y の項は far_01 0.17 → 0.44・cliff_01 0.53 → 1.13 に増える。
- 色の項（RG・BR）は曲線で少ししか動かない。ground_02 と cliff_01 の青空の BR の項（0.9〜1.3）は空気の散乱の色の差（差 6）で、
  どの候補でも残る。
- NeutralLinear にコントラスト・彩度を足した候補のうち、neutral_old_grade は露出を下げても白飛びが今より増え、選べない。
  neutral_mild は −1.0 EV（15 位、D 1.702、白飛び 0.001 %）だけが選べる条件を満たすが、今の設定より遠い。

## 選んだ変換と理由

**ACES Filmic（View の既定の演算子）・中立のグレーディング・露出の補正 −1 EV を採る**（`WastelandRoutine.cpp` の昼の分岐。
`kDayLookExposureOffsetEV = −1`、`CameraToneMapCurve::ViewDefault`）。

1. カメラだけで選べて白飛び・黒つぶれが今より増えない（どちらも 0 %）候補の中で D が最小（1.471）。今の設定の 1.617 より 9 % 小さく、
   変えない閾値（2 %）を超える。3 写真のどれでも今の設定より近いか同程度（far_01 1.135 → 0.978、cliff_01 1.434 → 1.465、ground_02 2.284 → 1.970）。
2. 次点の ACES 2.0 の LUT ±0 EV は D がほぼ同じ（1.472）だが、カメラごとに選ぶ口が無く、選ぶと全シーンの View の演算子が替わる。
   今の NeutralLinear のまま露出だけ −0.5 EV（D 1.516）は 3 位。
3. 見た目（`look_select_<写真>.png` と下の通常の 6 視点）: 地面の橙の飽和が収まり、崖の明暗の段が肩で丸まって読める。空は今より
   灰色に寄って暗くなる（写真の青空より暗い）。夜の集落は今までどおり NeutralLinear と自動露出（夜の分岐へ代入を移した）。

暫定の理由: D の改善は主に地面と崖を暗くすることによるもので、その差の主因は植生の被覆と崖の明るさ・遮蔽（`WastelandLook.md`）。
植物・地面の項目で中身が写真に近づいた後（確認 3）に、同じ `--look-select` で選び直す。

## エンジンとの照合（ブルームと FXAA を外した組）

`--display-check=scene -Hdr` で、選んだ設定を入れた HEAD の RelWithDebInfo（ブルームと FXAA を外す）の 6 視点を撮り、
選んだ候補をその HDR にオフラインで掛けた 8 bit の値と PNG を照らした（`.harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/display-check`）。
合否は `ReferenceCheck.py --display` の scene の組と同じ（3 成分とも ±1 段の中の画素が 99 % 以上）で、撮影の記録で
演算子（1 = ACES Filmic）・グレーディング（式 1、中立）・プリエクスポージャ（SCN-WL-LOOK-MEASURE の値 × 2^−1、相対 1e-4）が候補と
同じことも見る。

| 視点 | ±1 段の中 | 一致 | 最大の差 [段] | 差の平均（R, G, B）[段] |
|---|---|---|---|---|
| ref_far_01 | 100 % | 82.9 % | 1 | −0.037, −0.039, −0.032 |
| ref_far_02 | 100 % | 84.1 % | 1 | −0.030, −0.020, −0.034 |
| ref_cliff_01 | 100 % | 86.0 % | 1 | −0.015, −0.006, −0.010 |
| ref_cliff_02 | 100 % | 85.6 % | 1 | −0.014, −0.013, −0.030 |
| ref_ground_01 | 100 % | 83.8 % | 1 | −0.036, −0.025, −0.031 |
| ref_ground_02 | 100 % | 83.0 % | 1 | −0.031, −0.039, −0.042 |

ブルームと TAA を入れた通常の 6 視点（`scene-capture/SCN-WL-LOOK-CAMERA/views`、`--look --verify` は HDR の無い撮影として PNG だけを測る）は
`look_<写真>.png` を写真と並べて開いて見た。far_01 の青空の Y は写真 0.708・撮影 0.522、地面 0.129・0.289、崖 0.202・0.325。

既存の表示変換の照合（`ReferenceCheck.py --display`）は、`scene` の組の約束を ACES Filmic に、`ramp-neutral` の組の撮影を夜の集落に
改めて通した（`DisplayTransformCheck.md`。3 組とも全画素 1 段以内）。

## 夜の集落とほかのシーン（回帰）

`Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-LOOK-CAMERA` の before（変更の前の Game.exe）と after（変更の後）で、4 シーンとも
平均の差 0（PSNR night-village 71.32・mega-bananas 65.96・rendering3dtest 105.10・sandbox inf。前の項目の回帰と同じ水準の揺れ）。
夜の分岐は NeutralLinear・自動露出・夜のグレーディングのまま（曲線の代入を夜と昼の分岐へ分けた）。

## Cycles と比べる検査が読む HDR

`--gray`・`--lit`・`--light-final` は、表示の変換の前の HDR（`<視点>.hdr.pfm`。ブルームの前のシーンの色からプリエクスポージャを
外した cd/m²）を読み、表示の変換は比べる値に入らない（台本で確かめた。2026-10-22 の HEAD）:

- `--gray`: `Scripts/Terrain/ReferenceCheck.py` の `gray_engine_capture`（4650〜4652 行）が `hdr.json` の `source` が
  `scene_color_before_bloom`・`units` が `cd/m2` であることを確かめてから `<視点>.hdr.pfm` を読み、領域の平均（`gray_region_means`）を
  その HDR で取る。
- `--lit`: `lit_engine_hdr`（5727〜5729 行）が同じ確かめをして `<視点>.hdr.pfm` を読む。表示の変換（`DisplayTransform.py`）は
  見比べの絵（`write_lit_comparison`）を作るときだけ撮影の記録の値で掛ける。
- `--light-final`: `Scripts/Terrain/LightFinal.py` が組ごとに `<視点>.hdr.pfm` の有無を確かめ（70 行）、判定は `run_gray`・`run_lit`
  （108 行）に任せる。
- 露出の補正はプリエクスポージャを変えるが、HDR は書き出しでプリエクスポージャを外す（`FrameCaptureHdrWriter.cpp`）ので、
  比べる値は半精度の丸めの分しか変わらない。

## 既知の限界

- 写真との距離は、写真の現像（露出・トーンカーブ・ホワイトバランス）・天気・植生と材質の差を含む。差 3（地面と崖が明るい）の主因は
  植生の被覆と崖の明るさ・遮蔽（`WastelandLook.md`）で、表示の変換で暗くするのはその差を作風で寄せただけ。植物・地面の項目の後に見直す。
- 選べる曲線は荒野の昼のカメラで選べるものに限った（View の演算子は全シーン共通）。ACES 2.0 の LUT は距離がほぼ同じだが、
  カメラごとに選ぶ口がエンジンに無い。
- 昼の曲線は View の既定の演算子に従う（`ViewDefault`）ので、`--tone-map=aces20-lut` で起動した撮影では昼の荒野も LUT になる。

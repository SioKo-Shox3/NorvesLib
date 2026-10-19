# 比較の検査の合否の閾値（`--expect`、SCN-WLR-EXPECT-MATCH）

`ReferenceCheck.py` の `--gray`・`--lit`・`--blur-static`・`--blur-motion` に `--expect <仕様の JSON>` を足すと、比較の検査（入力の照合・収束・
投影など、もとの合否）に加えて、仕様の各行を報告の値で判定する。1 行でも範囲の外か、未判定を許さない行が未判定なら落とす。
判定は報告（`gray_check.json` など）の `expect` に書き、行ごとに `REFERENCE_CHECK expect row=...` を出す。本体は `Scripts/Terrain/ExpectSpec.py`、
仕様の置き場は `Scripts/Terrain/ExpectSpecs/`。

## 仕様の形

`{"description": "<説明>", "rows": [<行>, ...]}`（行の並びだけでもよい）。行の鍵は次だけで、ほかの鍵は書き間違いとして落とす。

| 鍵 | 中身 |
|---|---|
| `mode` | `gray`・`lit`・`blur_static`・`blur_motion`。走らせたモードと違う行は仕様の誤りとして落とす |
| `view` | 視点（`--gray` は ref_cliff_01・ref_far_01、`--lit` は 6 視点、`--blur-static` は ref_far_01・ref_cliff_02、`--blur-motion` は ref_far_01） |
| `region` | 領域。`gray`・`lit` は sunlit_ground・shadow_ground・sunlit_cliff・shadow_cliff・far、`blur_*` は far・cliff |
| `component` | 成分（`gray`・`lit` だけ）。total・sun・sky・indirect・visibility・chroma（RGB の色度） |
| `case` | 組。`blur_static` は条件の組（base・resident・aa-taa など）、`blur_motion` は fxaa・taa・base |
| `tag` | 撮影の名前（`blur_motion` だけ。static-0・static-8・static-30・static-60・motion-0・motion-8・motion-30） |
| `static` | 振りと止めの差の止めの撮影（`blur_motion` だけ。省くと `tag` の motion-k に対する static-k） |
| `quantity` | 量（下の表） |
| `min`・`max` | 下限・上限（両端を含む。どちらか一方は要る） |
| `allow_undetermined` | 未判定を許すか（既定は許さない） |
| `note` | 行の説明（判定には使わない） |

| モード | 量 | 値 |
|---|---|---|
| `gray`・`lit` | `ratio`（成分 total・sun・sky・indirect） | エンジン ÷ Cycles の領域の輝度の平均の比。sun・sky は太陽だけ・空だけのエンジンの撮影（`engine-sun`・`engine-sky`）と Cycles の太陽・空のパス |
| `gray`・`lit` | `chroma_diff`（成分 chroma） | 領域の線形の RGB の平均の色度 (r, g) = (R, G) / (R + G + B) の差の絶対値の大きい方 |
| `gray`・`lit` | `visibility_mismatch`（成分 visibility） | 太陽の可視率がエンジンと Cycles で食い違う画素の割合 |
| `blur_static` | `detail_loss`・`alias` | その組の細部の欠け・エイリアス（2 倍の解像度の正解と比べた値） |
| `blur_static` | `detail_loss_vs_resident`・`alias_vs_resident` | その組 − resident（base と resident の差は `case: base`） |
| `blur_motion` | `detail_loss`・`alias` | その組の `tag` の撮影の値 |
| `blur_motion` | `motion_static_detail_loss`・`motion_static_alias` | 振りと止めの差（振り − 止め） |

## 未判定

値が求まらない行は未判定になり、`allow_undetermined` が真でなければ落ちる。

- `gray`・`lit` の領域の判定・未判定は比較の検査の status に従う（画素が `GrayRegions.MIN_REGION_PX` に足りない、Cycles の 2 本が収束しない、
  `--lit` で Cycles のサンプルが下限に届かない）。
- `--lit` には太陽だけ・空だけの撮影が無いので、成分 sun・sky は未判定になる。
- 成分 indirect（エンジンの間接光の分）と visibility（エンジンの太陽の可視率）は、比較の検査の領域に `indirect_ratio`・`visibility_mismatch` の値がある撮影でだけ求まる。
  今の撮影にはエンジン側の値が無いので、未判定になる（可視率は SCN-WL-SHADOW-PAGES がエンジンの AOV を足す）。
- `blur_*` は、その組・撮影・領域の値が報告に無いとき（崖の画素が無い視点の cliff など）。

## selftest（`--expect-selftest [--verify]`）

保存した撮影（`SCN-WLR-CYCLES-GRAY`・`SCN-WLR-CYCLES-LIT`・`SCN-WLR-FAR-BLUR-STATIC`・`SCN-WLR-FAR-BLUR-MOTION`）で次を確かめ、
結果を `.harness/runs/scene-capture/SCN-WLR-EXPECT-MATCH/expect_selftest.json` に書く。

- 4 モードとも、今の値の前後で書いた仕様（`selftest-pass*.json`）で走らせると通り、全行が合格する。灰色の仕様には未判定を許した行
  （画素の無い領域）があり、未判定のまま合格になる。
- 外れる行のある仕様で落ちる。灰色は `selftest-fail.json`（影の崖の比 1 ± 0.1。今は 1.57）で走らせると、もとの検査は通ったまま期待の行だけで落ちる。
  実材質（日なたの地面の比 1 ± 0.08。ref_cliff_02 は 0.70）・遠景のぼけの静止（base と resident の欠けの差 0.01 以内。ref_cliff_02 の遠景は +0.046）・
  動き（TAA の振りと止めの欠けの差 0.01 以内。今は +0.097）は、走らせた報告に同じ判定を掛けて落ちることを確かめる。
- 未判定を許さない行（`selftest-fail-undetermined.json`。ref_far_01 の影の崖）と、今は測っていない量（エンジンの可視率）の行で落ちる。
- 仕様の誤り（知らない量・走らせたモードと違う行・閾値の無い行・知らない鍵・行の無い仕様）で落ちる。

`selftest-pass*.json` の値は 2026-10-19 の撮り直しの今の値の前後で、物理的な正しさの基準ではない（影の崖の明るすぎ・影の中の地面への太陽の漏れも
今の値として入っている）。描画を直してこれらの値が動いたら、selftest の通る仕様も今の値へ書き直す。

## 結果（2026-10-20）

- `--gray --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/selftest-pass.json --verify`: 14 行（判定 12・未判定を許した 2）すべて合格、result=pass。
- `--expect-selftest --verify`: 21 件すべて期待どおり。

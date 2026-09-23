# R7屋外受入れ判定

判定日: 2026-09-23。R7-O3の受入れは保留する。R7コアの必須条件が未完であり、空と霧を同時に含む屋外シーンの3時刻PT/raster比較値がない。`RenderingRoadmap: R7 complete`は付けない。

## 読み戻した証拠

| 証拠 | 確認結果 |
|---|---|
| `.harness/runs/20260923-095848/verify-R7-O3-1.txt` | 指定のGame Debug buildは`EXIT_CODE=0`。 |
| `.harness/runs/20260923-095848/verify-R7-O3-2.txt` | 指定の屋外GPU CTestは1/1 passed、`EXIT_CODE=0`。 |
| `.harness/runs/20260923-095848/verify-R7-O3-3.txt`、`verify-R7-O3-4.txt` | 関連R7 Debug buildは`EXIT_CODE=0`、既存PT・屋外・霧・カメラ・EXRのCTestは6/6 passed。朝/昼/夕のPT空miss合計は`0.0455075`/`0.0734877`/`0.0773978`。別の霧テストでは有限な透過率・単一散乱・無効時の戻りを確認。 |
| `.harness/runs/20260923-095848/verify-R7-O3-5.txt` | R7-P3の光輸送とR7-P4の収束専用CTestは登録0件。 |

`PathTracingOutdoorVulkanTest`は32×32の単一三角形を各時刻1試料ずつPTで描き、空と太陽照度を解析値と比べる。屋外テストに霧設定、同じシーンのraster capture、FLIP pool/max-pixel判定はない。霧のGPUテストは別の条件であり、3時刻の空と霧の同時比較を示さない。

## 未充足の受入れ条件

- R7-P1の承認済みOutdoor golden不一致、R7-P3のNEE/MISとPT材質・光源契約、R7-P4の16/64/256 spp収束と公開Cornell比較、R7-P7のR4/R6同一条件再照合が未解決。詳細は`R7CoreAcceptance.md`に記録済み。
- 同一camera・geometry・material・light・露出と有効な空・霧を用いた朝/昼/夕のPT/raster画像、有限値検査、事前に固定したFLIP pool/max-pixel閾値と数値レポートがない。既存のR1 golden閾値をこの異なる比較へ流用しない。

## 既知の方式差と再開条件

`R7FogTransport.md`に記録したとおり、rasterの霧はCSMを要する一次レイ合成であり、PTはRT可視性を使い二次交差区間にも適用する。複数方向光やCSM不在、間接光を含む画素は一致しない可能性がある。これらは画像比較後に差分の位置と量を分けて記録する。

R7-P3/P4/P1/P7の受入れを先に閉じ、R7-O3では同一入力の3時刻captureとFLIP二段判定を追加して数値を読戻す。全条件が成立するまでR7完了とは扱わない。

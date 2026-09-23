# R6 RTGI・テンポラルデノイザ受入れ記録

策定日: 2026-09-23

## 判定

R6の受入れは保留する。`RenderingRoadmap: R6 complete` trailerは付けない。

全体gateを阻んでいた2件は解消し、P5指定の全体`RenderingValidation`は49件中41件合格・8件は意図したskip契約・失敗0件になった。

- `DDGIProbeRadianceVulkanTest`: 1 pass内で非遮蔽→遮蔽を連続実行し、前ケースのprobe irradianceが後者の直接照明期待値へ混入していた。遮蔽ケースを独立passへ分離し（`d96d6fe`）、rendererの放射輝度計算・期待値・閾値は変更していない。
- `RenderingGoldenOutdoorVulkanTest`: R2の4カスケードCSM導入で球の自己影境界と接地影の輪郭459画素が変わっていた。正式な候補生成・承認・publish手順でOutdoor.pngだけを現行出力（SHA256=`9933B558…F3B954`）へ置き換えた（`c1474fc`、根拠は`R1Acceptance.md`の「R2 CSM後のOutdoor再承認」）。Indoor画像と閾値は変更していない。

一方、2026-09-23の独立評価で、完了条件の検証方法と実装に次の不足が見つかった。判定2〜5は2026-09-24に解消し、判定1だけが残る。判定1を解消するまでR6は完了にしない。

1. **静止収束画像の参照比較（未解消）**: 平均輝度・中央ROIのRGBなど5値の比較は、Roadmapが求める参照画像との知覚diffではない。R7の自前PTで同一条件の参照を作り、FLIP pool/max-pixelの二段判定で比較する（R6-P5-REF、R7-P3D以降に実施）。
2. **ライト追従の判定式（解消）**: 点光源を部屋の片側（offset −1.0）で16描画フレーム落ち着かせた後に反対側（+1.0）へ移し、デノイズ後間接光の色にじみ（R平均−G平均）を測る。収束後（移動から24〜32フレーム）の平均変化量を分母にし、4描画フレーム以内の到達率80%以上と、最終変化量が静止時の揺らぎの5倍以上であることを判定する。点光源は追従検証の段階だけ天井の面光源と同程度（200000 lm）にした。1200 lmでは面光源（約19万lm）の間接光に埋もれ、変化が揺らぎの2倍未満だった。5回の実行で到達率は0.84〜1.40、最終変化量は揺らぎの約25〜37倍だった。
3. **RTGIのサンプル更新（解消）**: `DiffuseIndirect.comp`は画素ごとに回転を加えたR2列を描画フレーム番号で進める（`3e5c374`）。旧実装の中央ROI輝度0.0305は、固定1方向の推定の偏りを含んでいた（修正後0.041前後）。
4. **履歴棄却の検証（解消）**: 実装側にも不具合があった。履歴棄却が非線形なNDC深度の相対5%で判定され、半精度で保存した遠景の深度差が閾値に埋もれていた。履歴へカメラからの線形距離を保存して相対差で棄却するよう直した（`3e5c374`）。検証は履歴ageのreadbackで行う（`e49cf5c`）。物体を移動前後とも見える位置（offset +0.55→+1.0）で動かし、エンジンと同じカメラ行列で投影した旧位置の円の内側かつ新位置の円の外側の170画素が、すべて移動からの経過フレーム数以下（最大0）のageになることを確認した。同時に奥の壁の静止領域は100%が最大age 8を保った。カメラ移動では静止領域の99.4%が履歴を保持し、残りは遮蔽物輪郭の視差による正当な棄却だった。停止後は物体が去った領域のageが10フレームで最大へ戻った。なおoffset 0の球は短いブロックの後ろにほぼ隠れており、以前の0→0.55の移動では露出領域がブロック面のまま変化しなかった。
5. **再オープン規則（解消）**: R7参照との再照合で閾値を超えた場合の扱いを、下の「R7参照との再照合条件」に明記した。

## 実装コミット

帳簿だけの保存コミットを除き、R6の機能範囲へ入るコードコミットを次に整理する。

| 工程 | コミット | 内容 |
|---|---|---|
| R6-M1 | `e2ed66c` | 1 bounce diffuse、ray query、テンポラル蓄積、3x3 cross-bilateral、8 rendered-frame warmup、性能gate保留を選定 |
| R6-P1 | `db612c0`, `190e812`, `0d40fc0` | RTGI結果形式、fallback、履歴revision、順序・カリング非依存のSceneRevisionを接続 |
| R6-P2 | `cc3dab4`, `a684739`, `326abc6`, `2be8e7b`, `81b5707` | ray-query compute、LightingPass、RTGI GPU陽性経路、hit/missとfallbackのreadbackを接続 |
| R6-P3 | `09fdbb0` | velocity再投影、current/history ping-pong、rendered FrameNumber連続性を接続 |
| R6-P4 | `2808077`, `db337ae` | temporal出力へ3x3 cross-bilateral denoiseを接続し、pre-exposure契約を維持 |
| R6-P5 | `b032e0d` | 動的GI、静止golden、移動、履歴棄却、fallbackの専用GPU受入れfixtureを追加 |
| 全体gate修正 | `d96d6fe`, `a41190e`, `c1474fc` | DDGI遮蔽ケースのpass分離、物理テストのFramePacket ABI契約同期、R2 CSM後のOutdoor基準再承認 |
| R6-P5再開 | `3e5c374`, `e49cf5c` | 描画フレームごとのRTGI試料更新、線形距離による履歴棄却、RTGI検証capture、履歴ageと間接光のreadbackによる受入れ |

## 方式と公開契約

- 既定GIは1 bounce diffuseのcompute ray queryとし、R5のTLAS/BLAS snapshotを共有する。R6用のRT pipeline/SBTは追加しない。
- 履歴はvelocity再投影、最大8 rendered frames、confidence、depth/normal/material/revision棄却、動的ライトrevisionの2-frame weight制限を使う。
- デノイザは1回の3x3 cross-bilateral filterとし、外部NRD、複数段SVGF、SSR/TAA置換、specular GI、透過、ReSTIRはR6の対象外とする。
- 公開優先順位は、完全なR6 RTGI、R6無効または失敗時のR4 DDGI、DDGIも無効・非対応・不完全な場合の既存IBL/直接照明・rasterである。不完全なRTGI履歴や出力は公開しない。
- 既定の`Rendering3DTest`起動経路、球、地面、ライト球、方向ライト、boulder、HDR環境は変更していない。

## golden・threshold

旧`R6RTGIStaticGolden.tsv`（平均値5個、許容0.001）は削除した。レイが描画フレームごとに変わると、実行時のフレーム番号の違いでmean_yが±0.0015程度揺れ、許容0.001の自己参照は意味を持たない。静止段階では、warmup直後との平均絶対差（時間方向の揺らぎ）が0.02以下であることだけを確認し（実測0.0085前後）、参照画像との比較はR6-P5-REFで行う。

最新のP5専用受入れ（`.harness/runs/20260924-r6-p5/verify-r6-acceptance.txt`）の主な値: warmup後mean_y=0.259374、静止中の揺らぎ0.00852、カメラ移動時の静止領域保持率0.994、物体移動の露出170画素の最大age 0、停止12フレーム後の去った領域の保持率1、ライト追従の到達率0.840（最終変化0.0154、揺らぎ0.00042）。

## 検証ログ

記録先はリポジトリrootからの相対パスである。ログは終了コード、CTest結果、readbackまたはcaptureの判定行を開いて確認した。

| 範囲 | 記録先 | 結果 |
|---|---|---|
| R6-P1 / revision契約 | `.harness/runs/20260922-200402/verify-R6-P1-1.txt`, `verify-R6-P1-2.txt`, `verify-R6-P1-FIX-1.txt`, `verify-R6-P1-FIX-2-1.txt`, `verify-R6-P1-FIX-2-2.txt` | Game buildと指定契約/GPUテストを通過。履歴側revision差は即時fallbackに使わず、構成変更だけを不採用条件にした。 |
| R6-P2 / RTGI陽性経路 | `.harness/runs/20260922-200402/verify-R6-P2-FIX-5.txt`〜`verify-R6-P2-FIX-7.txt` | Game build、指定GPU 3件、Vulkan 1.2 shader compileを通過。finite hit、miss zero、RTGI公開、disabled/incomplete fallbackを確認。 |
| R6-P3 | `.harness/runs/20260923-033012/verify-R6-P3-1.txt`, `verify-R6-P3-2.txt` | Game buildとcamera/object velocity・RenderGraphCompileTestの3/3を通過。 |
| R6-P4 | `.harness/runs/20260923-r6-p4-review2/verify-build.txt`, `verify-ctest.txt`, `verify-ctest-LastTest.txt` | Game/対象テストbuild、指定CTest、shader/denoise接続の読戻しを通過。 |
| R6-P5専用 | `.harness/runs/20260923-050747/verify-R6-P5-1.txt`, `verify-R6-P5-3.txt`, `verify-R6-P5-4.txt` | Game build、受入れfixture build、`R6RTGIAcceptanceVulkanTest` 1/1は通過。 |
| R6-P5全体gate（初回） | `.harness/runs/20260923-050747/verify-R6-P5-2.txt` | 44件中26 passed・8 skipped・10 failed、EXIT_CODE=8。その後の再実行で2 failedまで縮小した。 |
| R6-GATE-DDGI-ORACLE | `.harness/runs/20260923-r6-gate-ddgi/verify-build.txt`, `verify-ctest.txt`, `verify-ctest-LastTest.log` | 非遮蔽と点/スポット遮蔽を独立passで検証し、後者の期待値・実測値が全RGB channelで一致。対象CTest 1/1 passed。 |
| R6-GATE-OUTDOOR | `.harness/runs/20260923-resume/baseline-generate.txt`, `baseline-publish.txt`, `outdoor-ctest.txt` | 候補生成のIndoor/Outdoor hashは承認値と一致し、publishは`baseline_publish=PASS`、置換後のOutdoor golden CTestは1/1 passed。 |
| R6-P5全体gate（Outdoor再承認後） | `.harness/runs/20260923-resume/start-allbuild-2.txt`, `rv-full.txt` | targetless Debug buildはEXIT_CODE=0。`ctest -L RenderingValidation --timeout 180`は49件中41 passed・8 skipped・0 failed、EXIT_CODE=0。skipの8件はGPU skip契約テストである。 |
| R6-P5再開 | `.harness/runs/20260924-r6-p5/verify-allbuild-2.txt`, `verify-rv-2.txt`, `verify-r6-acceptance.txt`, `stability-1.txt`〜`-3.txt` | 全buildはEXIT_CODE=0。RenderingValidation 50件は失敗0件。R6受入れを計4回実行し、すべてEXIT_CODE=0。 |

## fallbackと既知の制限

- P5専用fixtureの`R6_FALLBACK_RT_DISABLED=PASS`はIBL、`R6_FALLBACK_R4=PASS`はR4 DDGIを確認した。RT capability不在、RT無効、TLAS不完全、resource/dispatch失敗時も、R6の不完全な結果を公開せず既存間接光へ戻す契約をP2/P2-FIXで固定している。
- 最新の全体gateログにも、Slang SDK未導入によるneural material decoder無効化のwarning/errorログが出る。既存の無効化fallbackであり、テストの終了コードには影響しない。
- denoised textureそのものの直接readback、遠景の非線形depth閾値、複数frame slotへ拡張した場合のsame-slot履歴境界は、R6の機能受入れを阻害しない追跡事項として`NEXT_FINDINGS.md`に残す。
- R6は1 bounce diffuseまでであり、2 bounce、specular GI、反射・透過、path tracing、ReSTIR、SSR/TAA、外部NRDは実装していない。

## R7参照との再照合条件

判定1の比較では、同一解像度、カメラ、geometry、material、light、HDR環境、exposure/pre-exposure、rendered-frame warmup条件を揃えたR7の自前PT出力を参照にする。比較指標と閾値は比較の実行前に固定し、差分の位置と量を記録する。

Roadmapの更新ルール5に従い、R7コア完了時の再照合で固定済み閾値を超えた場合は、R6をステータス表で「再オープン」とし、日付と理由を変更履歴へ記録する。PT方式との正当な輸送差と実装不具合は分けて記録し、R6の暫定goldenを上書きして合格させない。

R8についてはR6-aのvelocity契約だけを利用し、R8の未実装機能をR6の完了条件へ取り込まない。

## 性能gateと完了条件

GPU性能は`Deferred`とする。ray query、テンポラル蓄積、3x3 filter、fallbackの機能検証は行うが、パス別GPU時間はR6の合否へ混ぜず、将来のCI GPU性能回帰トラックで計測する。

判定1〜4を解消して全体gateを再実行した後にだけ、完了コードコミットへ`RenderingRoadmap: R6 complete` trailerを付ける。

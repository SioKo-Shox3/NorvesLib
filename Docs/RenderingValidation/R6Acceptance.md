# R6 RTGI・テンポラルデノイザ受入れ記録

策定日: 2026-09-23

## 判定

R6の受入れは保留する。`RenderingRoadmap: R6 complete` trailerは付けない。

全体gateを阻んでいた2件は解消し、P5指定の全体`RenderingValidation`は49件中41件合格・8件は意図したskip契約・失敗0件になった。

- `DDGIProbeRadianceVulkanTest`: 1 pass内で非遮蔽→遮蔽を連続実行し、前ケースのprobe irradianceが後者の直接照明期待値へ混入していた。遮蔽ケースを独立passへ分離し（`d96d6fe`）、rendererの放射輝度計算・期待値・閾値は変更していない。
- `RenderingGoldenOutdoorVulkanTest`: R2の4カスケードCSM導入で球の自己影境界と接地影の輪郭459画素が変わっていた。正式な候補生成・承認・publish手順でOutdoor.pngだけを現行出力（SHA256=`9933B558…F3B954`）へ置き換えた（`c1474fc`、根拠は`R1Acceptance.md`の「R2 CSM後のOutdoor再承認」）。Indoor画像と閾値は変更していない。

一方、2026-09-23の独立評価で、完了条件の検証方法と実装に次の不足が見つかった。判定2〜5は2026-09-24に解消し、判定1だけが残る。判定1を解消するまでR6は完了にしない。

1. **静止収束画像の参照比較（閾値超過・R6再オープン）**: R6-P5-REF（`98fd290`、`6a03e1b`）で自前PTの参照と比較し、事前に固定した閾値を大きく超えた。天井の面光源（発光三角形）の直接光が、ラスタではRTGIのコサイン標本1本/frameでしか入らず、収束後も斑点状の雑音のまま残る。詳細は下の「R7参照との再照合の結果」。
2. **ライト追従の判定式（解消）**: 点光源を部屋の片側（offset −1.0）で16描画フレーム落ち着かせた後に反対側（+1.0）へ移し、デノイズ後間接光の色にじみ（R平均−G平均）を測る。収束後（移動から24〜32フレーム）の平均変化量を分母にし、4描画フレーム以内の到達率80%以上と、最終変化量が静止時の揺らぎの5倍以上であることを判定する。点光源は追従検証の段階だけ天井の面光源と同程度（200000 lm）にし、面光源は追従検証の間だけ隠す。1200 lmでは面光源（約19万lm）の間接光に埋もれて変化が揺らぎの2倍未満だった。面光源を残すと、移動直後の少数試料の推定が面光源へ偶然当たるレイの外れ値で揺れ、到達率が0.69〜1.40にばらついた。中央値の指標は少数試料の偏った分布で低く出るため、偏りのない平均を使う。ライトrevisionは毎回、移動が描画に届いたフレームで変わり、その2フレームに重み制限がかかることをログで確認した。面光源を隠した後の到達率は0.87〜0.98（4回）、最終変化量は揺らぎの約80〜150倍。
3. **RTGIのサンプル更新（解消）**: `DiffuseIndirect.comp`は画素ごとに回転を加えたR2列を描画フレーム番号で進める（`3e5c374`）。旧実装の中央ROI輝度0.0305は、固定1方向の推定の偏りを含んでいた（修正後0.041前後）。
4. **履歴棄却の検証（解消）**: 実装側にも不具合があった。履歴棄却が非線形なNDC深度の相対5%で判定され、半精度で保存した遠景の深度差が閾値に埋もれていた。履歴へカメラからの線形距離を保存して相対差で棄却するよう直した（`3e5c374`）。さらに、保存値は前フレームのカメラから、現在値は今のカメラから測っていたため、前後移動で静止面も棄却していた。比較を前フレームのカメラから現在の表面点までの距離に揃えた（`ab86d83`）。横0.35・前進1.0のカメラ移動で静止領域の保持率は1、比較を今のカメラ基準へ戻すと0になる（負の対照）。検証は履歴ageのreadbackで行う（`e49cf5c`）。物体を移動前後とも見える位置（offset +0.55→+1.0）で動かし、エンジンと同じカメラ行列で投影した旧位置の円の内側かつ新位置の円の外側の170画素が、すべて移動からの経過フレーム数以下（最大0）のageになることを確認した。同時に奥の壁の静止領域は100%が最大age 8を保った。停止後は物体が去った領域のageが10フレームで最大へ戻り、その領域のデノイズ後間接光は、停止先と同じ物体なしの参照に対して物体の影響の0.1〜0.3%まで戻った（物体の影響は参照の前半・後半の差の約1000倍以上）。なおoffset 0の球は短いブロックの後ろにほぼ隠れており、以前の0→0.55の移動では露出領域がブロック面のまま変化しなかった。
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
| R6-P5再開 | `3e5c374`, `e49cf5c`, `ab86d83`, `c4a9d93` | 描画フレームごとのRTGI試料更新、線形距離による履歴棄却とその基準カメラの統一、RTGI検証capture、履歴ageと間接光のreadbackによる受入れ（前進カメラ・停止後の残留比較を含む） |

## 方式と公開契約

- 既定GIは1 bounce diffuseのcompute ray queryとし、R5のTLAS/BLAS snapshotを共有する。R6用のRT pipeline/SBTは追加しない。
- 履歴はvelocity再投影、最大8 rendered frames、confidence、depth/normal/material/revision棄却、動的ライトrevisionの2-frame weight制限を使う。
- デノイザは1回の3x3 cross-bilateral filterとし、外部NRD、複数段SVGF、SSR/TAA置換、specular GI、透過、ReSTIRはR6の対象外とする。
- 公開優先順位は、完全なR6 RTGI、R6無効または失敗時のR4 DDGI、DDGIも無効・非対応・不完全な場合の既存IBL/直接照明・rasterである。不完全なRTGI履歴や出力は公開しない。
- 既定の`Rendering3DTest`起動経路、球、地面、ライト球、方向ライト、boulder、HDR環境は変更していない。

## golden・threshold

旧`R6RTGIStaticGolden.tsv`（平均値5個、許容0.001）は削除した。レイが描画フレームごとに変わると、実行時のフレーム番号の違いでmean_yが±0.0015程度揺れ、許容0.001の自己参照は意味を持たない。静止段階では、warmup直後との平均絶対差（時間方向の揺らぎ）が0.02以下であることだけを確認し（実測0.0085前後）、参照画像との比較はR6-P5-REFで行う。

最新のP5専用受入れ（`.harness/runs/20260924-r6-p5b/verify-r6-acceptance.txt`）の主な値: warmup後mean_y=0.259374、静止中の揺らぎ0.00852、前進を含むカメラ移動時の静止領域保持率1、物体移動の露出170画素の最大age 0、停止12フレーム後の去った領域の保持率1、停止後の残差比0.001、ライト追従の到達率0.869（最終変化0.0196、揺らぎ0.00013）。

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
| R6-P5再開（2周目対応） | `.harness/runs/20260924-r6-p5b/verify-allbuild.txt`, `verify-rv.txt`, `verify-r6-acceptance.txt`, `ctest-noemit-1.txt`〜`-3.txt`, `negative-distance-baseline.txt` | 全buildはEXIT_CODE=0。RenderingValidation 50件は失敗0件。R6受入れは4回ともEXIT_CODE=0。距離基準を戻した負の対照はカメラ移動の保持率0でEXIT_CODE=8。 |

## fallbackと既知の制限

- P5専用fixtureの`R6_FALLBACK_RT_DISABLED=PASS`はIBL、`R6_FALLBACK_R4=PASS`はR4 DDGIを確認した。RT capability不在、RT無効、TLAS不完全、resource/dispatch失敗時も、R6の不完全な結果を公開せず既存間接光へ戻す契約をP2/P2-FIXで固定している。
- 最新の全体gateログにも、Slang SDK未導入によるneural material decoder無効化のwarning/errorログが出る。既存の無効化fallbackであり、テストの終了コードには影響しない。
- デノイズ後間接光の直接readback（検証captureの`RTGIDiffuseIndirect`）と、遠景の非線形depth閾値（線形距離による棄却へ変更）は2026-09-24に解消した。複数frame slotへ拡張した場合のsame-slot履歴境界は、R6の機能受入れを阻害しない追跡事項として`NEXT_FINDINGS.md`に残す。
- R6は1 bounce diffuseまでであり、2 bounce、specular GI、反射・透過、path tracing、ReSTIR、SSR/TAA、外部NRDは実装していない。

## R7参照との再照合の結果（R6-P5-REF、2026-09-24）

`R6RTGIPathTracingReferenceVulkanTest`は、R6受入れの静止段階と同じCornell fixture（camera、壁と箱、動的物体、点光源1200 lm、天井の面光源45000 nits、RTGI有効・DDGI無効、同じ環境マップとプリエクスポージャ）で次の4枚を256×256のSceneColorとして取得し、LDR-FLIP（x/(1+x)でトーンマップしたsRGB 8bit、PPD 67）で比べる。

| 画像 | 内容 | 平均輝度 |
|---|---|---|
| raster-rtgi | R6 RTGI。履歴の最大age（8 rendered frame）の3倍の24 frame後に取得 | 0.2612（R6受入れの静止段階の記録値0.2594と取得時刻の揺らぎの範囲で一致） |
| pt-direct | PT（`--path-tracing-transport=direct`、16384試料以上）。1次命中の発光と直接光だけ | 0.3341 |
| pt-single | PT（`single-diffuse-bounce`）。R6の申告範囲（拡散1バウンス）に合わせた参照 | 0.3842 |
| pt-full | PT（`full`）。多重散乱をすべて追う参照 | 0.4371 |

判定の参照はR6の申告範囲に合わせた`pt-single`とする。閾値の物差しは比較の実行前に`6a03e1b`で固定した: 参照の間接光成分（`pt-single`−`pt-direct`）を一様に±20%変えた画像と参照との知覚差のうち小さい方を上限とする。間接光の一様な20%の誤差を、R6の近似（1試料/frame・8 frameの履歴・3×3 filter）の残留誤差として許容できる上限とみなした。判定は原寸の全画素FLIP平均（pool）と原寸の画素単位FLIP最大の二段で、8×8区画平均画像のFLIP最大を補助の判定に加える（原寸の画素単位最大は評価の指摘を受けて`R6RTGIPathTracingReferenceVulkanTest`へ追加した判定で、閾値を緩める変更ではない）。±40%の変化が3つの判定のすべてで閾値の外にあること、参照の最も暗い3×3画素へ光漏れを加えた局所欠陥が、全体平均では閾値内でも原寸の画素単位最大で閾値の外に出ることを、比較のたびに確かめる。

| 比較 | FLIP平均 | 画素単位FLIP最大 | 8×8区画FLIP最大 | 判定 |
|---|---|---|---|---|
| 閾値（間接光+20%、−20%の小さい方） | 0.0816146 | 0.187391 | 0.166266 | — |
| 物差しの単調性（間接光+40% / −40%） | 0.130171 / 0.148026 | 0.289942 / 0.369503 | 0.260085 / 0.323719 | 閾値の外 |
| 負の対照: 局所の光漏れ（3×3画素） | 0.000565 | 0.975185 | 0.407733 | 画素単位最大で検出 |
| R6 RTGI 対 `pt-single` | 0.698827 | 0.964534（画素(114,44)） | 0.965608（区画(28,13)） | **超過** |
| 陽性対照: 147試料の`pt-single` 対 `pt-single` | 0.0267879 | 0.152988 | 0.0282305 | 閾値内 |
| 参考: `pt-full` 対 `pt-single`（R6の範囲外の多重散乱） | 0.254082 | 0.647641 | 0.570346 | — |
| 参考: R6 RTGI 対 `pt-full` | 0.779726 | 0.968425 | 0.968866 | — |

R6の画像の平均輝度はPTの直接光だけの画像より低い（間接光に当たる差が−0.0729）。ラスタのLightingPassは発光三角形を光源として扱わず、天井の面光源の直接光はRTGIのコサイン標本が偶然面光源へ当たったときだけ入る。面光源は小さく非常に明るいため、1試料/frame・8 frameの履歴・3×3 filterでは斑点状の雑音として残り、壁と箱の照明がほぼ欠ける（`.harness/runs/20260924-r6-p5-ref/r6-reference-images.png`）。RTGIの命中点の直接光も点・spot・方向光だけで、面光源に照らされた面からの1バウンスも入らない。これはPT方式との正当な輸送差（多重散乱）ではなく、R6の申告範囲内の欠落である。

Roadmap更新ルール5に従いR6をステータス表で「再オープン」とし、閾値とgoldenは緩めない。修正はR6-P7（RTGIで発光三角形を光源標本する）で行い、同じテストを同じ閾値で再実行する。

| 記録 | 内容 |
|---|---|
| `.harness/runs/20260924-r6-p5-ref/ctest-r1.txt` | 画素単位最大と局所欠陥の対照を加えた後の取得4回と比較の全出力、およびPT照明テスト。R6の比較はEXIT_CODE=8（閾値超過） |
| `.harness/runs/20260924-r6-p5-ref/ctest-r6-reference-1.txt` | 初回（平均と区画最大の判定）の比較出力 |
| `.harness/runs/20260924-r6-p5-ref/positive-control-pt-low-spp.txt` | 陽性対照の比較出力 |
| `.harness/runs/20260924-r6-p5-ref/ctest-lighting-transport.txt`、`ctest-r1.txt` | PTの輸送範囲の解析確認（平面1枚で直接光のみ=点光源の解析値、拡散1バウンス−直接光=反射率×環境で誤差5e-6。面光源と平面では直接光のみと拡散1バウンスが一致し、多角形光源の解析照度との差0.29%） |
| `.harness/runs/20260924-r6-p5-ref/*.nlrgba` | 比較に使った4枚のfloat画像 |

## R6-P7の修正後の再照合（2026-09-24）

R6-P7で、RTGIの1次面と命中点に発光三角形の光源標本を加え（`128294a`）、RTGIの拡散光へ画面空間AOを重ねるのをやめ（`81bbf24`。Cornellの壁ではSSAOがほぼ全遮蔽になり、正しく求めたRTGIの拡散光を消していた）、デノイズで中心の外れ値を近傍の最大の4倍までに抑えた（`3f4f39c`）。PT参照はラスタのGBufferと同じ画素中心から1次光線を出す（`a15c43a`）。同じ閾値の物差し（間接光±20%）で比較した結果（`.harness/runs/20260924-r6-p7/label-run.log`）:

| 比較 | FLIP平均 | 画素単位FLIP最大 | 8×8区画FLIP最大 |
|---|---|---|---|
| 閾値 | 0.0818904 | 0.186116 | 0.166972 |
| R6 RTGI 対 `pt-single`（R6-P5-REF時点） | 0.698827 | 0.964534 | 0.965608 |
| R6 RTGI 対 `pt-single`（R6-P7後） | 0.0589312（閾値内） | 0.283346（画素(140,29)、**超過**） | 0.0910554（閾値内） |
| R6 RTGI 対 `pt-single`（光源標本の影と切り詰めの修正後） | 0.0584145（閾値内） | 0.295121（画素(140,29)、**超過**） | 0.0896849（閾値内） |

R6の平均輝度0.385は参照0.384とほぼ等しく、R6の間接光は参照の拡散1バウンスの0.98倍になった。光源標本の影の問い合わせを発光面の直前まで調べ、露出前の中間値を切り詰めないようにした後（`02eaa7e`、`.harness/runs/20260924-r6-p7-r2/label-run.log`）も判定は変わらない。画素単位最大の超過の内訳は、作業中の一時的な診断から立てた次の3つの仮説で、保存した再現可能な診断はない。除外の根拠には使わず、R6の完了はまだ判定しない。

1. **ラスタの直接光のニューラルBRDF（R6の範囲外）**: SceneViewは常に`Data/disney.ns.bin`を読み、通常表示の直接光を学習済みのDisney BRDF近似で評価する。天井の点光源のすぐ上で2画素幅の明るい筋を作る（発光面を隠した点光源だけの画像でもラスタにだけ現れる）。lighting.fragを一時的に解析BRDFへ固定すると筋は消え、画素単位最大は0.299から0.261へ下がった。
2. **高コントラストの縁の標本の分かれ**: 箱の上面の縁などで、ラスタ化と光線交差の判定が画素中心の近くで1画素だけ分かれる（発光面の画素数はラスタ・PTとも390で、系統的なずれはない）。
3. **RTGIの残留外れ値**: 天井のすぐ下の点光源が作る明るい面へ1試料/frameの光線がまれに当たる。

画素単位最大をラスタとPTの相互比較でどう適用するか（縁で判定が分かれる画素とR6の範囲外の直接光の近似の扱い）は受入れ基準の方針であり、閾値を緩めずに決めるためユーザーの判断へ戻す。

## R6-P7の完了（2026-09-24）

ユーザーの判断で、ラスタとPTの相互比較の画素単位最大は、ラスタのGBufferとPTの1次命中の距離（1%以内）と法線（内積0.99以上）が一致する画素だけで判定する（一致しない画素は参照の値へ置き換えたFLIPで除き、数と最大を記録する。ラスタに物体IDがないため、物体の同一性は同じ位置と向きの一致で代える）。比較する両方の直接光を解析BRDFで揃える（`--raster-direct-brdf=analytic`、既定の起動はニューラルBRDFのまま）。閾値の物差し（間接光±20%）と数値は変えない（`c162366`・`f7d0b59`・`14dfc02`・`775261a`）。

仮説は保存した診断で次のとおり確かめた（`.harness/runs/20260924-r6-p7-mask/`、`.harness/runs/20260924-r6-p7-static/`）。

1. ニューラルBRDF: 既定のニューラルBRDFのラスタは画素(141,28)付近に画素単位最大0.280〜0.315の筋を持ち、解析BRDFのラスタではこの位置の差が消える（診断行`diagnostic_r6_neural_direct_vs_single_diffuse_bounce`）。修正はFIX-NEURAL-BRDF-STREAKで扱う。
2. 縁の判定の分かれ: 距離と法線が一致しない画素は2（0.003%）だけで、箱の凸の稜線でラスタが上面、PTが前面を取る画素（(127,170)）。
3. RTGIの残留雑音: 直接光を揃え不一致画素を除いた後も、閾値を超える一致画素が110（0.17%、8か所以上に散在、FLIP 0.21〜0.25）残った。RTGIは1 frame 1本の光線と8 frameの履歴（時間方向の重みは最大0.889）で、静止しても8 frameを超えて収束しなかった。

ユーザーの判断で「静止時だけ蓄積を伸ばす」ことにした。視点（逆ビュー射影・位置）・光源のrevision・レイトレーシングのinstance（変換・形状・材質）が変わらず履歴を再投影できるフレームが16を超えて続くと、画素ごとの履歴の年齢の上限を1 frameに1ずつ8から64まで上げ（時間方向の重みは累積平均の1-1/(年齢+1)で最大64/65）、何かが変わったフレームで8へ戻す。動いている間と静止の最初の16フレームは従来と同じ挙動で、R6受入れの動的追従・移動後停止・カメラ移動の判定は変わらず通る。デノイザは中心の年齢が8を超える画素で近傍の重みを8/年齢へ下げ、時間方向の累積で雑音が減った分、面光源の影の縁（R6-P7で面光源の直接光がRTGIの信号に入った）の空間方向のぼけを減らす。R6参照比較のラスタは、静止カメラで収束させた画像を比べるため、年齢の上限が64に達してからその4倍待って取得する。

| 比較（R6 RTGI、解析BRDFの直接光 対 `pt-single`） | FLIP平均 | 一致画素の画素単位最大 | 8×8区画FLIP最大 | 閾値を超える一致画素 |
|---|---|---|---|---|
| 閾値 | 0.0818912 | 0.186116 | 0.166972 | — |
| 幾何一致の判定だけ | 0.0581058 | 0.253440（画素(125,235)、超過） | 0.0909513 | 110 |
| ＋静止時の履歴延長 | 0.0466885 | 0.215286（画素(125,235)、超過） | 0.0992997 | 4 |
| ＋年齢に応じたデノイズ | **0.0466159** | **0.149767**（画素(121,186)） | **0.0999677** | **0** |

最後の行は`r6_reference_comparison=PASS sanity=PASS`（`.harness/runs/20260924-r6-p7-static/ctest-r6-reference-2.txt`）。1画素の光漏れの負の対照は一致画素で平均・区画では閾値内、画素単位最大だけで閾値外になる。RTGIのGPUテストで、連続27静止フレームで年齢の上限と画素の年齢が18、instanceを動かすと8へ戻ること、デノイズ結果が同じ面の近傍へ寄る割合が年齢8の0.249から年齢18の0.176へ下がることを確かめ、年齢による近傍の重みやシェーダーの年齢上限を戻すとテストが失敗することを負の対照で確かめた。

## R7参照との再照合条件

判定1の比較では、同一解像度、カメラ、geometry、material、light、HDR環境、exposure/pre-exposure、rendered-frame warmup条件を揃えたR7の自前PT出力を参照にする。比較指標と閾値は比較の実行前に固定し、差分の位置と量を記録する。

Roadmapの更新ルール5に従い、R7コア完了時の再照合で固定済み閾値を超えた場合は、R6をステータス表で「再オープン」とし、日付と理由を変更履歴へ記録する。PT方式との正当な輸送差と実装不具合は分けて記録し、R6の暫定goldenを上書きして合格させない。

R8についてはR6-aのvelocity契約だけを利用し、R8の未実装機能をR6の完了条件へ取り込まない。

## 性能gateと完了条件

GPU性能は`Deferred`とする。ray query、テンポラル蓄積、3x3 filter、fallbackの機能検証は行うが、パス別GPU時間はR6の合否へ混ぜず、将来のCI GPU性能回帰トラックで計測する。

判定1〜4を解消して全体gateを再実行した後にだけ、完了コードコミットへ`RenderingRoadmap: R6 complete` trailerを付ける。

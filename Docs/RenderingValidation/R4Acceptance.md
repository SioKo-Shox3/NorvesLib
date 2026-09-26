# R4 DDGI受入れ記録

## 判定と対象範囲

R4の方式S4はDynamic Diffuse Global Illumination（DDGI）を選定した。有限な3Dプローブ格子を手動配置し、既定は無効とする。更新はVulkanのray queryに対応する環境で行い、無効時・非対応時・資源作成失敗時は従来の描画へ戻す。atlasは同一frame slotでもcurrent/historyの2組を交互利用し、前回のprobe結果を読みながら次の結果へ更新する。

RenderWorldのvolume設定と材質値はSceneProxyを経てFramePacketへ値コピーする。RenderThreadは当該FramePacketの不変snapshotとR5のTLASを読み、プローブ資源と履歴はLightingPassが所有する。1 volumeは最大1024 probes、probeあたり64 raysとし、octahedral irradiance/distance atlasの各layerは8x8 texels、1 texel borderを持つ。irradianceはscene-linear RGBA16F、distanceの平均・2次モーメントはRG16Fで保存する。

受入れ対象はvolume内のopaque diffuse lightingである。鏡面GI、反射・透過、probe relocation/自動配置は対象外とする。面の裏を見るprobeの分類（無効化）は2026-09-25の再照合で加えた（下の節）。pre-exposureはLightingPassの最終合成で一度だけ適用する。R4-P1〜P7の実装契約とCornell実GPU受入れを完了し、R4を受入れ済みとする。

| タスク | 現在の台帳状態 |
|---|---|
| R4-P1〜P4、P3A | done |
| R4-P5 | done。完了コミット 0116e8f |
| R4-P6 | done。HDR Cornell static/dynamic capture、disabled A/B、VUIDを確認 |
| R4-P7 | done。受入れ記録、指定CTest、独立評価所見の処理を確定 |

## Cornell参照・ローカルアセット・閾値

参照元は[Cornell Computer Graphics Data](https://bowers.cornell.edu/computer-graphics/data)。公開ページのgeometry・reflectance・合成RGBEを参照する。ローカルアセットは Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe、解像度は512x512、SHA-256は D94EF786F6BBC70AF81CE700765A598F8AFF73157234B2F50EE5A42C3F9C9F22 である。

ROI座標は左上を原点とし、right/bottomは含まない。direct white ROIから露出scaleを一度だけ決定する。R4の実GPU受入れはHDR captureを要求する。

| ROI/判定 | 左 | 上 | 右 | 下 | 受入れ条件 |
|---|---:|---:|---:|---:|---|
| direct white（露出基準） | 226 | 90 | 286 | 106 | 露出scaleを一度決定 |
| shadow floor | 266 | 480 | 300 | 508 | 平均Y相対誤差25%以内 |
| red bounce | 144 | 270 | 156 | 390 | 平均Y相対誤差25%以内、優勢chroma比差0.10以内 |
| green bounce | 380 | 350 | 392 | 430 | 平均Y相対誤差25%以内、優勢chroma比差0.10以内 |
| DDGI無効A/B | 全画面 | — | — | — | 同一プロセス内の無効baseline→有効capture→無効verifyで、平均delta 0.002以下、最大pixel delta 0.01以下 |

動的更新はlight X offset 0.55を適用し、間接光ROIの最終変化量0.002以上、8実frame以内に最終変化の80%以上へ単調に収束することを条件とする。最初の4 sampleをwarmupとして基準値を安定化し、その後のupdate sampleは10件以上、進捗step上限は1.02とする。captureが2 frame間隔でも、期限は実際のFrameNumber差で判定する。

## build・CTest・GPU captureの読戻し

| 検証 | 読戻しログ/成果物 | 結果 |
|---|---|---|
| 対象Debug build | .harness/runs/20260922-r4-p7-final3/build.txt | RenderingDDGIVulkanTest、RenderingGoldenImageComparatorTest、DDGIProbeUpdateVulkanTest、RenderingDDGILightingContractTestのbuildは EXIT_CODE=0。MSBuildの既存third-party PDB warningは出力に残る |
| Cornell scene-color capture | .harness/runs/20260922-r4-p7-final3/cornell-reference.log | EXIT_CODE=0、HDR format=9。direct/shadow/red/green Yは0.393677/0.15236/0.0687421/0.0951394。露出補正後のshadow/red/green相対誤差は0.147881/0.152345/0.106696、chroma差は0.0194377/0.0229986。R4_CORNELL_REFERENCE=PASS、DDGI有効A/Bはmean/max delta=0.168949/6.5625、無効A/Bはmean/max delta=0/0、VUID_COUNT=0 |
| dynamic scene-color capture | .harness/runs/20260922-r4-p7-final3/dynamic-update.log | EXIT_CODE=0。4 warmup sample後の初期red/green Yは0.0688291/0.0951506、10 update sample後は0.0846982/0.0747947。FrameNumber差8のred/green進捗は0.971325/0.820146、R4_DYNAMIC_CONVERGENCE=PASS、VUID_COUNT=0 |
| P4期待値の再基準化と再確認 | .harness/runs/20260922-r4-p7-final3/ddgi-probe-update-direct.log | EXIT_CODE=0。現行のwrap重み床とhysteresis=0.6に合わせ、visibility-weighted one-bounce radiance 1.96391/0.981954/0.490977、unweighted 1.30247/0.651237/0.325619、VUID_COUNT=0 |
| focused 9件 CTest | .harness/runs/20260922-r4-p7-final3/ctest-r4-9.log | EXIT_CODE=0、9/9 passed。DDGIProbeRayQuery、DDGIProbeUpdate、RenderingDDGI、RenderingDDGI dynamic、LightingParamsLayout、DDGIVolume、DDGISnapshot、Golden comparator、atlas公開契約を含む |
| 差分衛生 | .harness/runs/20260922-r4-p7-final3/diff-check.txt | EXIT_CODE=0。`git diff --check`、行末差分比較、ignored-tracked確認を保存して読み戻す |

VUID_COUNT=0は各captureログに記録した。validation errorの陽性対照とGPU性能計測はこの受入れの対象外である。Slang SDK未導入によりneural material decoderが無効化される既存warningはcaptureログに残る。

## 独立評価と境界監査

独立評価ログは `.harness/runs/20260922-r4-p7-final3/evaluator-R4-P7-round2.txt` に保存する。FramePacketの値所有、RenderThreadのlive World/SceneView参照禁止、Rendering層から RHI/Vulkan へのinclude禁止、LightingPassのDDGI fallback/atlas公開契約、診断バイパスの除去、R4成果物の追跡状態を確認対象とした。評価で指摘された帳簿・行末・成果物追跡の不整合は修正し、最終成果物へ反映した。

P4から引き継いだRG16F storage、distance 2次モーメントのhalf範囲、visibility floor/normal bias、CPU期待式とGPU結果の独立性、validation陽性対照は、受入れを阻害しない追跡事項として NEXT_FINDINGS.md に残す。

## 既知の制限と保留

- probe relocation/自動配置、鏡面GI、反射・透過は対象外。
- GPU性能は未計測。性能gateはDeferredとする。

## 自前PTとの再照合と修正（2026-09-25、R4-REOPEN）

R7完了時の再照合（Roadmap更新ルール5）で、同じCornell状態のラスタを4096 sppの自前PT（全輸送）と同じ指標で比べ、赤ROIの相対輝度誤差0.257が上限0.25を超えた（`R4DDGIPathTracingReferenceVulkanTest`、`R7CoreAcceptance.md`）。画像にはprobe格子の位置の斑点、壁の角の暗い帯、縁の鋸歯があった。閾値・指標・承認済みgoldenは変えずに、次の原因を直した。

1. **壁の外と物体の内側のprobe**: probe格子（原点-0.1、間隔0.82、8×8×8）の外側の層は壁の4.8〜11 cm外にあり、ブロックの内側のprobeもある。これらが面の裏を見た照度・距離を持ち、面へ漏れていた。probe rayが面の裏に当たった割合が0.25を超えるprobeを無効とし（RTXGIのprobe分類）、補間から外す。面の表は、頂点順の外積の逆側を法線の変換（逆転置）でworldへ移した向きとする（鏡映のない変換ではラスタの裏面カリングと一致し、鏡映の変換でも閉じた物体の外向きを保つ）。
2. **発光面の直接光の粗さ**: Cornellのラスタには面光源の直接光がなく、発光面の直接光もDDGIが運ぶ。64本の固定rayでは小さな発光面に当たるrayが0〜2本で、probeごとの直接照度が全か無かになっていた。probeでの発光面の直接照度を、受け手の半球で切り取った三角形からLambertの式で厳密に求め、影は三角形を16個に分けた小三角形の重心への影のrayの重み付きの可視率で掛ける。rayが発光面に当たった放射は照度から除く。
3. **1バウンス目の直接光の二重計上**: probe rayのhit面は発光面の直接光を自分で数え、さらに前フレームのprobe照度（発光面の直接光を含む）も加えていた。照度atlasを全体と間接光だけの2組のlayerにし、hit面は間接光の組を読む。
4. **補間**: LightingPassの補間をRTXGIの手順に揃えた（法線方向へずらした点で区画・距離・可視を求め、向きの重みの下限0.2、trilinearの軸ごとの下限0.001、Chebyshevの下限なし、弱い重みの押しつぶし、平方根の空間での補間）。ずらし量はprobe間隔の0.225倍で、Majercikらの自己遮蔽のずらし量（0.3 × 0.75 × 最小間隔）と同じ大きさを法線の方向だけに置く（視点の側へずらすと拡散の照度がカメラの位置で変わる）。距離のモーメントのcosineの指数を32から8にし、面の角で可視の判定が途切れる鋸歯を抑えた。

コードは`2588b47`・`effd5e3`・`355f8df`・`3b1f3f4`・`ce9d10f`。危険地帯の独立評価（2周）で指摘された、鏡映の変換での表裏の食い違い、発光面のすぐ近くでの直接照度の過大評価、頂点法線が面に垂直でない発光面で頂点の順により放射の側が変わる不具合を直し、それぞれの回帰の場面をテストへ加えた。

### 結果

| 比較 | 露出scale | 影 | 赤 | 緑 | 色度差（赤/緑） | 判定 |
|---|---:|---:|---:|---:|---|---|
| 閾値 | — | 0.25 | 0.25 | 0.25 | 0.10 | — |
| 自前PT（修正前） | 0.846 | 0.162 | **0.257** | 0.0566 | 0.0091/0.0167 | FAIL |
| 自前PT（修正後） | 0.971 | 0.117 | 0.224 | 0.130 | 0.0301/0.0114 | PASS |
| 公開Cornell参照（修正前） | — | 0.148 | 0.152 | 0.107 | 0.0194/0.0230 | PASS |
| 公開Cornell参照（修正後） | — | 0.104 | 0.117 | 0.0192 | 0.0191/0.0286 | PASS |

動的更新（light X offset 0.55）はFrameNumber差8で赤/緑の進捗0.875/0.871、`R4_DYNAMIC_CONVERGENCE=PASS`。DDGI無効A/Bは平均/最大差0/0、VUID_COUNT=0。画像全体では、64画素区画ごとのラスタ/PT輝度比の中央値が0.07〜2.15から0.33〜1.58へ縮み、多くの区画は0.7〜1.3にある（`.harness/runs/20260925-r4-reopen/ratio-map-final.txt`、`pt-vs-ddgi-final.png`）。

法線方向のずらし量は比較の結果に影響するため、感度を記録する（同じPT参照、影/赤/緑、`bias-sweep.txt`）。視点の側へ0.2倍・法線の方向へ0.05倍で0.364/0.230/0.233（不合格）、視点の側0.05倍・法線0.05倍で0.280/0.203/0.126（不合格）。視点の側へずらさず法線の方向へ0.05/0.1/0.2/0.225倍では、影0.255/0.202/0.131/0.117・赤0.187/0.200/0.220/0.224・緑0.081/0.098/0.125/0.130で、0.05倍だけが影で不合格になる。0.225倍は画像全体の一致も最良だった（ラスタ/PT輝度比の|log|の中央値0.221、0.1倍で0.251）。

### 検証ログ

| ログ | 内容 |
|---|---|
| `.harness/runs/20260925-r4-reopen/ctest-renderingvalidation.txt` | `3b1f3f4`の全target Debug build（`build-all-2.txt`、BUILD_EXIT=0）後のRenderingValidationラベル。57件中0件失敗（GPUのskip契約8件は非実行）。R4・R6・R7のPT参照比較を含む |
| `.harness/runs/20260925-r4-reopen/ctest-ddgi-final2.txt` | `ce9d10f`の`R4DDGIPathTracingReferenceVulkanTest`とDDGI系9件、9/9 passed、上の数値 |
| `.harness/runs/20260925-r4-reopen/probe-update-8.txt` | `DDGIProbeUpdateVulkanTest`。発光面の直接照度（遮蔽込み）・2組のlayer・probeの状態・面の裏を見る無効なprobe・鏡映した発光面・1 cm上の発光面（直接照度24.39、上限π×8=25.13）・頂点法線が面に垂直でない発光面とその頂点の順の巡回をCPUの参照計算と照合。VUID_COUNT=0。修正前のshaderでは巡回させた場面が不一致になる（`probe-update-8-negative.txt`） |
| `.harness/runs/20260925-r4-reopen/bias-sweep.txt` | 表面のずらし量の感度 |

### 残る制限

- 奥の壁の左右の角は、背の高いブロックのすぐ後ろのprobe（そのprobeの半球の大部分をブロックの影の面が占める）から補間するため、PTより暗い。probeの配置による限界で、probeの移動（relocation）は対象外のまま。
- probeが面から離れているため、発光面の直接光は面の位置との距離の2乗の差の分だけずれる（面光源の直接光をラスタが持たないCornellに固有）。
- probe rayは64本の固定方向で、frameごとの回転はしない。

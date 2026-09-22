# R4 DDGI受入れ記録

## 判定と対象範囲

R4の方式S4はDynamic Diffuse Global Illumination（DDGI）を選定した。有限な3Dプローブ格子を手動配置し、既定は無効とする。更新はVulkanのray queryに対応する環境で行い、無効時・非対応時・資源作成失敗時は従来の描画へ戻す。atlasは同一frame slotでもcurrent/historyの2組を交互利用し、前回のprobe結果を読みながら次の結果へ更新する。

RenderWorldのvolume設定と材質値はSceneProxyを経てFramePacketへ値コピーする。RenderThreadは当該FramePacketの不変snapshotとR5のTLASを読み、プローブ資源と履歴はLightingPassが所有する。1 volumeは最大1024 probes、probeあたり64 raysとし、octahedral irradiance/distance atlasの各layerは8x8 texels、1 texel borderを持つ。irradianceはscene-linear RGBA16F、distanceの平均・2次モーメントはRG16Fで保存する。

受入れ対象はvolume内のopaque diffuse lightingである。鏡面GI、反射・透過、probe relocation/classification/自動配置は対象外とする。pre-exposureはLightingPassの最終合成で一度だけ適用する。R4-P1〜P7の実装契約とCornell実GPU受入れを完了し、R4を受入れ済みとする。

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

- probe relocation/classification/自動配置、鏡面GI、反射・透過は対象外。
- GPU性能は未計測。性能gateはDeferredとする。

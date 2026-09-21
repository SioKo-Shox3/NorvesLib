# R4 DDGI受入れ記録

## 判定と対象範囲

R4の方式S4はDynamic Diffuse Global Illumination（DDGI）を選定した。有限な3Dプローブ格子を手動配置し、既定は無効とする。更新はVulkanのray queryに対応する環境で行い、無効時・非対応時・資源作成失敗時は従来の描画へ戻す。

RenderWorldのvolume設定と材質値はSceneProxyを経てFramePacketへ値コピーする。RenderThreadは当該FramePacketの不変snapshotとR5のTLASを読み、プローブ資源と履歴はLightingPassが所有する。1 volumeは最大1024 probes、probeあたり64 raysとし、octahedral irradiance/distance atlasの各layerは8x8 texels、1 texel borderを持つ。irradianceはscene-linear RGBA16F、distanceの平均・2次モーメントはRG16Fで保存する。

受入れ対象はvolume内のopaque diffuse lightingである。鏡面GI、反射・透過、probe relocation/classification/自動配置は対象外とする。pre-exposureはLightingPassの最終合成で一度だけ適用する。R4-P1〜P4、P3A、P5の実装契約は完了範囲として記録するが、Cornell実GPU受入れが未完了のためR4全体は受入れない。

| タスク | 現在の台帳状態 |
|---|---|
| R4-P1〜P4、P3A | done |
| R4-P5 | done。完了コミット 0116e8f |
| R4-P6 | blocked。Cornell ROI/HDRとdisabled A/Bが未受入れ |
| R4-P7 | blocked。受入れ記録は確定したが、指定CTestと独立評価が未達 |

## Cornell参照・ローカルアセット・閾値

参照元は[Cornell Computer Graphics Data](https://bowers.cornell.edu/computer-graphics/data)。公開ページのgeometry・reflectance・合成RGBEを参照する。ローカルアセットは Test/Core/Rendering/Baselines/RenderingValidation/R4CornellReference.rgbe、解像度は512x512、SHA-256は D94EF786F6BBC70AF81CE700765A598F8AFF73157234B2F50EE5A42C3F9C9F22 である。

ROI座標は左上を原点とし、right/bottomは含まない。direct white ROIから露出scaleを一度だけ決定する。R4の実GPU受入れはHDR captureを要求する。

| ROI/判定 | 左 | 上 | 右 | 下 | 受入れ条件 |
|---|---:|---:|---:|---:|---|
| direct white（露出基準） | 226 | 90 | 286 | 106 | 露出scaleを一度決定 |
| shadow floor | 266 | 480 | 300 | 508 | 平均Y相対誤差25%以内 |
| red bounce | 144 | 270 | 156 | 390 | 平均Y相対誤差25%以内、優勢chroma比差0.10以内 |
| green bounce | 380 | 350 | 392 | 430 | 平均Y相対誤差25%以内、優勢chroma比差0.10以内 |
| DDGI無効A/B | 全画面 | — | — | — | 平均delta 0.002以下、最大pixel delta 0.01以下 |

動的更新はlight X offset 0.55を適用し、間接光ROIの最終変化量0.002以上、8実frame以内に最終変化の80%以上へ単調に収束することを条件とする。update sampleは10件以上、進捗step上限は1.02とする。captureが2 frame間隔でも、期限は実際のFrameNumber差で判定する。

## build・CTest・GPU captureの読戻し

| 検証 | 読戻しログ/成果物 | 結果 |
|---|---|---|
| 対象Debug build | .harness/runs/20260922-045823/verify-R4-P7-3.txt | Game、RenderingDDGIVulkanTest、RenderingGoldenImageComparatorTestのbuildは EXIT_CODE=0。MSBuildの既存warningは出力に残る |
| Cornell back-buffer capture | .harness/runs/20260922-045823/verify-R4-P7-6.txt、cornell-debug.png | EXIT_CODE=1。capture formatは 6（LDR）でHDR要件未達。disabled baselineはreadback形式確認のみ。frame 115〜137の12 samples後、shadow-floorはreference Y 0.00806246、measured Y 0.00808735、relative error 0.00308765。red/greenはmeasured Y 0、relative error 1、chroma差 0.860907 / 0.619895。R4_DIAGNOSTIC_PNG=PASSは診断画像の保存だけを示し、ROI合格を示さない |
| dynamic back-buffer capture | .harness/runs/20260922-045823/verify-R4-P7-5.txt | EXIT_CODE=0。initial frame 119、samples 121,123,...,139、elapsed_frames=8、frame8_progress=1、R4_DYNAMIC_CONVERGENCE=PASS。ただしROIはshadow-floorであり、sample 1以降 0.295671 が一定なので、間接光の更新を独立に証明する受入れ結果とは扱わない |
| focused 8件 CTest | .harness/runs/20260922-045823/verify-R4-P7-2.txt | EXIT_CODE=8。RenderingDDGIVulkanTestがCornell red/green ROIで失敗し、8件中7件成功。disabled A/B比較段へ到達していない |
| 差分衛生 | .harness/runs/20260922-045823/verify-R4-P7-1.txt | EXIT_CODE=0。git diff --checkの保存出力を読み戻して確認した |

VUID_COUNT=0は各captureログにあるが、validation errorを意図的に発生させる陽性対照を含まないため、validation有効性の証明とは扱わない。Slang SDK未導入によりneural material decoderが無効化されるwarningもcaptureログに残る。

## 独立評価と境界監査

独立評価ログ .harness/runs/20260922-045823/evaluator-R4-P7-2.txt の判定は NEEDS_WORK である。FramePacketの値所有、RenderThreadのlive World/SceneView参照禁止、Rendering層から RHI/Vulkan へのinclude禁止、LightingPassのDDGI fallback/atlas公開契約は blocking 指摘なしと確認した。

一方、blockingとして次を未処理で残す。

- Cornellのred/green ROIが閾値外で、captureがHDRではない。
- dynamic captureはframe期限の機械的判定には通るが、shadow-floor ROIを使い、sample 1以降が一定なのでDDGI間接更新の独立性を示さない。
- Cornell失敗によりDDGI無効A/Bの平均delta・最大pixel delta比較へ到達していない。
- RenderingDDGIVulkanTest.cpp、R4のRGBE/thresholdが未コミットであり、Cornell fixtureの変更は41948faに入ったものの、受入れ記録が確定成果物だけを対象にする状態へ戻っていない。

P4から引き継いだRG16F storage、distance 2次モーメントのhalf範囲、visibility floor/normal bias、CPU期待式とGPU結果の独立性、validation陽性対照は NEXT_FINDINGS.md に残す。

## 既知の制限と保留

- Cornell ROI、HDR capture、DDGI無効A/Bは未受入れであり、R4全体を受入れ済みとは扱わない。
- dynamic captureの2 frame間隔自体は検出できたが、現ROIではDDGI更新の独立性を判定できない。
- probe relocation/classification/自動配置、鏡面GI、反射・透過は対象外。
- GPU性能は未計測。性能gateはDeferredとする。

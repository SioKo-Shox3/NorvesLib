# R3 ボリュメトリックフォグ受入れ記録

## 受入れ範囲

R3-P4では、outdoor scene の実GPU BackBufferを使い、3段階のフォグ密度、CSM遮蔽のA/B、遠景の空・地平線と大気のブレンドを固定した。数値閾値とgoldenはR3専用とし、R1/R2の承認済み基準から分離する。

## R3専用基準資産

- 密度golden: `Test/Core/Rendering/Baselines/RenderingValidation/R3FogDensityLow.png`、`R3FogDensityMedium.png`、`R3FogDensityHigh.png`
- 密度閾値: `Test/Core/Rendering/Thresholds/RenderingValidation/R3DensitySweep.tsv`
- CSM遮蔽A/B閾値: `Test/Core/Rendering/Thresholds/RenderingValidation/R3OccluderAb.tsv`
- 遠景大気ブレンド閾値: `Test/Core/Rendering/Thresholds/RenderingValidation/R3DistantAtmosphereBlend.tsv`

密度は `0.0025`、`0.005`、`0.01` の3値とし、各段階のBackBuffer luma上限を245、飽和RGB画素数を0に固定する。密度増加にともなう側ROIの最小luma差は各段階0.25、lowからhighまでの最大差は64とする。golden比較はmean FLIP `0.02` 以下、最大チャンネル差8以下とする。

## GPU受入れ結果

### 密度sweepとgolden

| 密度 | 中央ROI mean luma | 側ROI mean luma | 最大luma | 飽和画素 |
|---:|---:|---:|---:|---:|
| 0.0025 | 134.286528 | 84.600905 | 163 | 0 |
| 0.0050 | 133.827900 | 106.497598 | 162 | 0 |
| 0.0100 | 133.098875 | 135.809703 | 161 | 0 |

側ROIのlow→medium→high差はそれぞれ21.896693、29.312105で、最小段階差0.25を満たした。出力sentinelが報告したlow→high差51.208799は最大差64以下だった。各差は丸め前のcapture値から算出している。3枚のdensity goldenはmean FLIP 0、最大チャンネル差0で比較に合格した。

### CSM遮蔽A/B

遮蔽あり／なしで同じfixture、ライト、カメラを保ち、遮蔽物のshadow castingだけを切り替えた。

| ケース | 中央ROI mean luma | 側ROI mean luma | 最大luma | 飽和画素 |
|---|---:|---:|---:|---:|
| 遮蔽あり | 133.098875 | 135.809703 | 161 | 0 |
| 遮蔽なし | 161.730178 | 147.890345 | 181 | 0 |

中央ROI差は28.631303（許容0.25〜48）、非遮蔽側ROI差は12.080642（上限16）、影コントラスト差は16.550661（下限8）で合格した。

### 遠景大気ブレンド

| ケース | 空ROI mean luma | 地平線ROI mean luma | 最大luma | 飽和画素 |
|---|---:|---:|---:|---:|
| フォグのみ | 0.000000 | 0.000000 | 0.000000 | 0 |
| 空のみ | 116.117341 | 2.248579 | 141.246200 | 0 |
| 空＋フォグ | 116.117341 | 132.163198 | 141.246200 | 0 |

空の差116.117341、地平線の大気差132.163198、地平線のフォグ差129.914619は各下限1を超えた。地平線差は上限160以下で、フォグ適用時の空ROI変化0は上限2以下だった。全ケースで飽和RGB画素は0だった。

`shadowed-shafts` の再確認では、中央の遮蔽差1.59418、非遮蔽散乱差98.4629、フォグ中央差3.12447、フォグ側ROI差15.3901を取得し、全3 captureが合格した。

## R1/R2基準の保持

R1の `Indoor.png`、`Outdoor.png`、`VisualThresholds.tsv` と、R2の `R2SkyMorning.png`、`R2SkyNoon.png`、`R2SkyEvening.png`、`R2SkyTimeSweep.tsv`、`R2CsmAcceptance.tsv` は、8ファイルすべてSHA256が `HEAD` と一致する。R1/R2の画像・閾値は変更していない。

## 検証ログ

指定検証の全文は `.harness/runs/20260920-124224/` に保存した。7コマンドはすべて終了コード0。

| ログ | 検証内容 | 結果 |
|---|---|---|
| `verify-R3-P4-1.txt` | 3対象のDebugビルド | exit 0 |
| `verify-R3-P4-2.txt` | density-sweep GPU capture | 3段階PASS、非飽和 |
| `verify-R3-P4-3.txt` | density-sweep golden比較 | 3段階PASS |
| `verify-R3-P4-4.txt` | shadowed-shafts GPU capture | PASS |
| `verify-R3-P4-5.txt` | CSM遮蔽A/B | PASS |
| `verify-R3-P4-6.txt` | 遠景大気ブレンド | PASS |
| `verify-R3-P4-7.txt` | 指定7件のCTest | 7/7 passed |

GPU実行ログには、Slang SDK未導入のため `neural_material_decode.slang` をコンパイルできないメッセージが含まれる。各ログでR3のPASS sentinelと終了コード0を確認した。

## 性能ゲート

GPU性能は未計測であり、後続のGPU性能ゲートで扱う。

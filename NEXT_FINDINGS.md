# 次の確認事項

## 反復 9 — 解決済み / 非blocking

- R5-P7 の独立レビュー2周目は `PASS`。前回のblocking指摘は解消され、P7 は `afe49df` で完了済み。
- `eval-9.out.txt` は本文 `PASS` だが1行目に前置きがあった。評価内容へのblockingではなく、runnerの出力形式注意として解決済みに整理する。

## R5-P10 — 解決済み / non-blocking

- `ResolvePipelineStageFlags`でRT pipeline capabilityに応じたstageを一元解決し、対応デバイスの`ShaderResource`と`RayTracingStorage` barrierがRay Tracing shaderを対象にする。対応しないデバイスには拡張stage bitを渡さない。対応GPUのR5テストは同期validation有効で1/1 passed、Raster/RT A/BとRT無効fallbackはmax_lsb=0。再リンク後のRenderingValidationは既知のOutdoor golden baseline以外が成功した。

## R5-P11 — blocking / 修正必須

- `Assets/Shaders/lighting.frag` の `ShouldApplySceneColorPreExposure()` はR5のRasterHardShadow、RayTracingHardShadow、RasterFallback modeを追加したが、`Test/Core/Rendering/LightingParamsLayoutTest.cpp` は比較式が4件のままとしているためCTestで失敗する。R5の3 modeを含めてshader契約テストを更新するか、プリエクスポージャ方針を見直し、`LightingParamsLayoutTest` を通してからP11を完了する。この失敗はR5導入で発生したものとして扱い、過去の全CTest失敗へ埋没させない。

## Vulkan同期validation — 別経路の追跡事項

- `VK_LAYER_VALIDATE_SYNC=1`をRenderingValidation全体へ設定したとき、NoCasterとDrawThenNoCasterのswapchain画像で`SYNC-HAZARD-WRITE-AFTER-READ`が報告された。通常validationでは両テストが成功し、RT影専用CTest/captureでは同期hazardが出ない。swapchain acquire/presentのstage依存を別件として追跡する。

## R5影capture — 非blocking

- 現在のfixtureは`shadow_luma=0`/`lit_luma=255`の高コントラスト出力で、影位置とRaster/RT出力一致を検査する。半影・階調差の検出は今後のfixture拡張が必要。

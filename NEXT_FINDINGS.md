# 次の確認事項

## 反復 9 — 解決済み / 非blocking

- R5-P7 の独立レビュー2周目は `PASS`。前回のblocking指摘は解消され、P7 は `afe49df` で完了済み。
- `eval-9.out.txt` は本文 `PASS` だが1行目に前置きがあった。評価内容へのblockingではなく、runnerの出力形式注意として解決済みに整理する。

## R5-P10 — blocking / remediation required

- RT visibility texture の書き込み後バリアが `UnorderedAccess` の compute-only stage に写像され、ray-tracing shader の `imageStore` とLighting fragment readの依存が成立しない。RT用storage state/barrier stageを追加し、Vulkan側の状態・アクセス・layout写像と合わせる。
- 通常描画でもRT影が常時有効になり、PCF/PCSSからの既定挙動変更が発生する。更新後のGPU test executableを再リンクして `ctest -L RenderingValidation` を実行し、R1/R2/R3・HDR回帰を判定する。既存基準を保てない場合は通常描画を既定offにする。
- RT visibilityは先頭の影キャスト方向光だけから生成される一方、Lightingが全方向光へ適用し、CSMが無効な複数灯構成でも影を有効化する。可視な方向光が一灯の場合に限りRT影を公開するか、ライトごとのvisibilityを分離する。

## R5-P11 — blocking / 修正必須

- `Assets/Shaders/lighting.frag` の `ShouldApplySceneColorPreExposure()` はR5のRasterHardShadow、RayTracingHardShadow、RasterFallback modeを追加したが、`Test/Core/Rendering/LightingParamsLayoutTest.cpp` は比較式が4件のままとしているためCTestで失敗する。R5の3 modeを含めてshader契約テストを更新するか、プリエクスポージャ方針を見直し、`LightingParamsLayoutTest` を通してからP11を完了する。この失敗はR5導入で発生したものとして扱い、過去の全CTest失敗へ埋没させない。

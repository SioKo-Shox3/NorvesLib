# NEXT_FINDINGS

- [R4-P5][blocking][独立評価1周目] `LightingPass.cpp` がbinding 4の `GPULightingParams` に64 byteのDDGI部分更新を行い、既存 `RenderGraphCompileTest` のFakeBufferでは最後の更新全体が64 byteとして記録される。`RenderGraphCompileTest.cpp:4270` は `sizeof(GPULightingParams)` を要求するため、`TestLightingNativeExecuteBindsExpandedLightStorageBuffer` が新規回帰で失敗する。全構造体へ値を合成して一括更新するか、FakeBufferにoffset付き部分更新の正しい合成を実装する。`RenderGraphCompileTest` をbuild/CTestゲートへ加え、R4-P6より先に修正・検証・再評価する。
- [R4-P6][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。

# NEXT_FINDINGS

- [R4-P5][先行修正] DDGIProbePassのatlas有効状態が早期returnで失効せず、同じframe slotに残った古いatlasを公開する可能性がある。P5がLightingPassへ接続する前に、無効化・失敗フレームでは`bAtlasValid`を解除し、直前の有効フレーム後にatlas取得が失敗する契約を検証する。必要パスは`Library/Core/Private/Rendering/DDGIProbePass.cpp`で、TASKS.mdのP5範囲へ追加済み。
- [R4-P6][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。

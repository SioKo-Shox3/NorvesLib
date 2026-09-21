# NEXT_FINDINGS

- [R4-P4][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。
- [R4-P6][blocking][Cornell geometry] back-buffer captureは画面の大半が黒で、red/green ROIに面が存在しない。fixtureのquad index windingと頂点法線の向きが一致しない可能性を修正し、実GPU captureでgeometry出現を確認する。証跡: `.harness/runs/20260922-003731/cornell-debug.ppm`、`.harness/runs/20260922-003731/verify-R4-P7-4.txt`。
- [R4-P6][blocking][diagnostic output] `RenderingDDGIVulkanTest.cpp` の診断画像保存先が特定run-idの`.harness/runs`へ固定されている。コードから固定パス出力を除去し、再実行時にgit管理外run artifactへ書き込まない。
- [R4-P6][diagnostic][temporal cadence] dynamic captureのFrameNumberは125, 127, 129, ...と2ずつ進む。検証アプリのcapture cadenceとFramePacket dropを区別し、8実フレーム以内の80%収束を判定する。連続sampleの要求だけを根拠にDDGIProbePassを変更しない。証跡: `.harness/runs/20260922-003731/verify-R4-P7-5.txt`。
- [R4-P7][blocking][record accuracy] build成功とCTest成功を区別する。Cornell disabled baselineはreadback形式確認のみでA/B一致判定には未到達。P3Aを完了範囲に含め、R4-P5完了コミットを`0116e8f`として記録する。P6受入れ後にP7を再評価する。

# NEXT_FINDINGS

- [R4-P4][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。
- [R4-P5][follow-up][台帳整合] 最終再評価はPASSでbinding 4の全構造体更新を確認しているが、TASKS.mdのstatusはblockedのまま。完了条件とstatusの整合を確認する。
- [R4-P6][blocking][Cornell受入れ] 2026-09-22のback-buffer captureはshadow-floor relative error 2.5766（上限0.25）、red/green measured Y=0、chroma差0.860907/0.619895（上限0.10）で失敗した。証跡: .harness/runs/20260922-003731/verify-R4-P7-4.txt。
- [R4-P6][blocking][動的更新] capture sampleのFrameNumberは125, 127, 129, ...と2ずつ進み、連続frameを要求する判定で失敗した。8 frame以内の収束とDDGI無効A/Bの最終比較は未受入れ。証跡: .harness/runs/20260922-003731/verify-R4-P7-5.txt。
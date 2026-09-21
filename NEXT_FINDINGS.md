# NEXT_FINDINGS

- [R4-P4][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。
- [R4-P6][blocking][Cornell ROI/HDR] 20260922-045823のCornell captureは診断画像にfixtureの画面を保存できたが、capture format=6（LDR）でHDR要件を満たさない。red/green ROIはmeasured Y=0、relative error=1、chroma差=0.860907/0.619895で失敗する。shadow-floorだけはrelative error=0.00308765。red/green ROIの対応面・色寄与とHDR readbackを確定し、閾値を変更せず再検証する。証跡: .harness/runs/20260922-045823/verify-R4-P7-6.txt、cornell-debug.png。
- [R4-P6][blocking][dynamic metric] dynamic captureはFrameNumber 119から121,123,...,139を読み、elapsed_frames=8・frame8_progress=1で機械判定は通るが、評価ROIはshadow-floorでsample 1以降0.295671が一定である。間接光の変化を直接測るROIへ差し替え、DDGI有効/無効差と単調収束を独立に確認する。証跡: .harness/runs/20260922-045823/verify-R4-P7-5.txt。
- [R4-P6][blocking][disabled A/B] Cornell ROI失敗でCornellDisabledVerifyへ到達せず、DDGI無効時の平均delta 0.002以下・最大pixel delta 0.01以下を未検証である。形式readbackのPASSをA/B一致とは扱わない。
- [R4-P7][blocking][未コミット成果物] RenderingDDGIVulkanTest.cpp、R4CornellReference.rgbe、R4CornellAcceptance.tsvが未コミットである。Cornell fixtureの変更は41948faに入った。診断出力のrun-id固定は現作業ツリーで環境変数指定へ修正済みだが、land前に残る成果物を確定し、受入れ記録がコミット済みコードを対象にする状態へ戻す。
- [R4-P7][blocking][record] 対象Debug buildはEXIT_CODE=0、focused CTestは8件中7件成功でRenderingDDGIVulkanTestが失敗、dynamic captureはEXIT_CODE=0だが間接光の独立証明ではない。P3Aを完了範囲に含め、R4-P5完了コミットは0116e8fとして記録した。R4-P6/P7をblockedのまま次の反復で再評価する。
- [R4][non-blocking][資源寿命] DDGIProbePassのframe slot資源resetと前slot atlas参照に対し、VulkanTexture側の遅延削除キューは無い。slot再利用前のfence待機条件を受入れ記録へ明記し、VUID陽性対照と合わせて確認する。現runではVUID_COUNT=0だが、陽性対照なしである。

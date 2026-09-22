# NEXT_FINDINGS

- [R6-a][resolved 2026-09-22] 最終ソース更新後にRenderingVelocityVulkanTestを再リンクし、GPU 6/6、関連CTest 17/17、Game build、Game 120フレームを再実行した。証拠は`.harness/runs/20260922-r6a-final/`と19:09:37更新の`Game.log`。
- [R6-a][resolved 2026-09-22] 解析サンプルの許容誤差を0.002へ締め、カメラのみ期待`0.0151554`/実測`0.015152`、物体のみ期待`-0.0649519`/実測`-0.0649414`、併用期待`-0.0497965`/実測`-0.0497742`、背景画素`(255,0)`の`0`/`0`を確認した。

- [R6-a][resolved 2026-09-22] `World::SyncEntityRecursive` が現フレームのproxyを毎フレーム更新し、proxy公開後に`MeshComponent`の変換履歴をcommitするよう修正した。`move-then-stop` GPU readbackで停止後 `max_magnitude=0` / `non_zero=0` を確認した。
- [R6-a][resolved 2026-09-22] GPU検証へ既知のカメラ/物体点の解析投影値、初回と安定静止の別frame、移動後停止を追加した。カメラのみは期待`0.0151554`/実測`0.015152`、カメラ＋物体は期待`-0.0497965`/実測`-0.0497742`だった。
- [R6-a][resolved 2026-09-22] 最終ソースでGame build、関連CTest 17/17、velocity GPU CTest 6/6、既定Game 120フレーム起動ログを再実行し、受入れ記録へ反映した。

- [R4-P4][follow-up] P4独立評価の非blocking事項をCornell/動的更新受入れで確認する。Vulkan validation captureの陽性対照、RG16F storage対応確認、distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限、visibility floor 0.05とnormal bias 0.002mの漏れ/自己遮蔽、CPU期待式とGPU結果の独立性を扱う。border fixtureはGPU出力がborder補間値をinterior-only値より近く選ぶことを確認済みだが、閾値0.01を下回る形状変更ではfixtureを再設計する。
- [R4][non-blocking][資源寿命] DDGIProbePassのframe slot資源resetと前slot atlas参照に対し、VulkanTexture側の遅延削除キューは無い。slot再利用前のfence待機条件を受入れ記録へ明記し、VUID陽性対照と合わせて確認する。R4受入れでは9件CTestと実GPU captureのVUID_COUNT=0を確認したが、陽性対照は別追跡とする。
- [R4][non-blocking][数値範囲] RG16F distanceの2次モーメントがhalf範囲を超えるProbeSpacing上限と、visibility floor/normal biasの漏れ・自己遮蔽を別の数値契約で固定する。
- [R4][non-blocking][性能] probe relocation/classification、自動配置、鏡面GI、反射・透過、GPU性能計測はR4の対象外であり、性能gateはDeferredとする。
- [R4][non-blocking][same-slot履歴] 現在のMAX_FRAMES_IN_FLIGHT=1では同一frame slotの履歴swapが実FrameNumberを保ったまま動作する。将来slot数を増やす場合は、same-slot履歴にも直前frame判定を加え、古いatlas chainを混在させない。
- [R4][non-blocking][受入れ運用] DDGI無効A/Bは同一プロセス内で無効baseline→有効capture→無効verifyを比較する経路であり、別プロセス間の再現性検証やvalidation陽性対照は別ゲートとする。Cornell threshold TSVのschema header統一も別整理とする。
- [R5-P5][non-blocking][容量] `VulkanDevice::CreateAccelerationStructure`/`Build`のBLAS size queryを実ジオメトリ条件で再問い合わせし、保存済みstorage/scratch容量との比較を追加する。現行の同期BuildはGPU queryとresource寿命を検証済み。
- [R5-P5][non-blocking][入力usage] triangle BLASのvertex/index入力がBDAだけでなく`VertexBuffer`/`IndexBuffer` usageを満たす契約を追加し、StorageBufferだけの入力を拒否するケースを固定する。
- [R5-P5][non-blocking][同期] 現行の同期Buildはqueue waitIdleで完了を保証する。将来非同期Buildへ拡張する場合はdst stageのRayTracingShaderを含むbarrierとfence依存を再検証する。
- [R5-P5][non-blocking][行末] `VulkanDevice.cpp`のP5範囲外に残るLF/CRLF混在を、対象範囲を分離した整理タスクとして扱う。
- [R5][non-blocking][swapchain同期] `VK_LAYER_VALIDATE_SYNC=1`で既存のNoCaster系swapchain画像に報告された`SYNC-HAZARD-WRITE-AFTER-READ`を、RT影専用経路とは分離したswapchain acquire/presentの同期課題として追跡する。
- [R5][non-blocking][テスト注入] `VulkanDevice.cpp`のGPU同期失敗注入フックを製品コードから分離またはビルド時ガードする。現行のRHITextureUpdateVulkanTestは注入経路を検証済み。
- [R5][non-blocking][全体build] Physics static assertを含む既知の全体build失敗と、R5対象外の既知CTest失敗を分離したまま、クリーンクローンのconfigure/ALL_BUILD再検証を別ゲートで行う。
- [R5][non-blocking][行末] `RayTracingSceneSubsystem.h`、TASKS/PROGRESSの既存運転履歴に残るLF/CRLF混在を、意味差分と分離して整理する。

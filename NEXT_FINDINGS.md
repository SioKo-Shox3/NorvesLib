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

## 反復 2 — 評価者(claude)の判定: RESOLVED

対象: R6-P1 RTGI結果形式とfallback契約を実装する

## 修正結果

R6-P1-FIXでSceneRevisionをシーン構成だけへ限定し、History側revision差を即時fallbackに使わず、構成revision不一致時だけ公開結果を不採用にした。履歴revisionが1つ前でもRTGI選択を維持し、構成変更時にfallbackへ戻る契約テストを追加した。証拠は`.harness/runs/20260922-200402/verify-R6-P1-FIX-1.txt`と`verify-R6-P1-FIX-2.txt`。

## 判定理由

証拠は実在し対象タスクのものです。`verify-R6-P1-1.txt` は Game build の EXIT_CODE=0、`verify-R6-P1-2.txt` と `recheck-R6-P1-2-2.txt` は指定 CTest 3/3 passed を示し、私の再実行でも `RenderingDDGILightingContractTest` と `RayTracingSceneSnapshotTest` は 2/2 passed でした。差分は `paths:` の内側に収まり、numstat と `--ignore-cr-at-eol --numstat` は一致、新規ヘッダは UTF-8 BOM + CRLF です。

しかし done-when の「FramePacket の scene/light revision」と「履歴 resource の current/history 公開」の**契約の意味**が、このタスクが前提とする R6-M1 の方式選定(`Docs/RenderingValidation/R6TechniquePlan.md`)と矛盾しています。

**未達条件: scene/light revision と履歴契約が R6-M1 の意味論を満たさない**

- 計画 88 行目は「環境、材質、TLAS 構成の変更は全画面履歴を無効化」、96〜97 行目は「カメラ・物体移動は R6-a velocity と depth/normal 棄却で pixel 単位に処理し、**全画面リセットを常用しない**」、95 行目は「動的ライトの revision 変化中は history weight の上限を 0.25 へ落とし、2 rendered frames 後に通常値へ戻す」と定めています。
- 実装の `HashSceneRevision`(`RenderingCoordinator.cpp:84-159`)は全 DrawCommand の `WorldMatrix`/`NormalMatrix`、全 `InstanceData` の `World`/`PreviousWorld`、全 RT インスタンスの `transform` をハッシュします。物体が 1 つでも動いている間は `PreviousWorld != World` なので **毎フレーム SceneRevision が進みます**。UI board の DrawCommand(`AppendRebasedDrawCommands` 経由で同じ配列に入る)の変化でも進みます。
- `RTGIHistoryResources::IsForFrame`(`RTGIContract.h:216-225`)は `History.SceneRevision == sceneRevision && History.LightRevision == lightRevision` を要求し、`ResolveRTGIIndirectLighting`(同 274-289 行)はそれが崩れると `ResourceUnavailable` で DDGI/IBL へ落とします。前フレームの履歴は定義上「前の revision」を持つため、物体が動いている限り履歴は常に不完全 → RTGI は選ばれず、8 フレーム履歴も「ライト移動時は weight 抑制」も成立しません。これは P2/P3 がこの契約を変えない限り実現不可能で、P1 の目的(契約固定)に反します。
- テスト `TestRTGIFallbackContract` はこの意味論を検証していません(不完全 result の拒否と fallback 順序のみ)。

**再現(静的読解)**: `RenderingCoordinator.cpp:1976` で毎フレーム `UpdateFrameRevisions` → `HashSceneRevision` が `instance.PreviousWorld` を含む → 移動中の物体で毎フレーム `m_SceneRevision` 増加 → `RTGIHistoryResources::IsForFrame` が偽。

**最小の直し方(候補)**

1. `HashSceneRevision` から per-object の変換(`WorldMatrix`/`NormalMatrix`/`World`/`PreviousWorld`/RT `transform`)を外し、計画どおり「構成」だけをハッシュする: DrawCommand/RT インスタンスの集合(Mesh/Material/ObjectId の並び)、材質パラメータ、Sky/DDGI/Fog、TLAS インスタンス数・opaque フラグ。ライト revision は現状の内容で可(位置変化を含めるのは計画に合致)。あわせて `RTGIHistoryResources::IsForFrame` は **History 側の revision 一致を要求せず** Current 側だけを見る形にし、History の revision 差は P3 が weight 抑制/棄却に使う入力として残す。
2. 代替: revision を「構成 revision」と「変換 revision」に分け、履歴契約の完全性判定は構成 revision のみで行う。こちらは FramePacket のフィールドが増えるので 1 案のほうが小さい。

いずれの案でも `TestRTGIFallbackContract` に「History の revision が 1 つ前でも `IsComplete()`/RTGI 選択が維持される」ケースと「Scene 構成変更で不採用になる」ケースを足してください。

## non-blocking

- `LightingPass::Declare` は `RTGIDiffuseIndirect` を読むだけで `Execute` は結果を消費しません。現状は誰も `PublishRTGI` を呼ばないので到達不能ですが、将来 `Source == RTGI` になると `bUseDDGILighting` が偽になり RTGI も DDGI も適用されない状態になります。P2 で束ねる前提なら PROGRESS か TASKS に明記してください。
- `HashSceneRevision` は毎フレーム全 DrawCommand・InstanceData・RT インスタンスを走査します(GameThread の CommandGeneration 内)。大規模シーンでの費用は R4/R5 と同じ性能 gate の追跡事項に入れてください。
- `m_LastSceneRevisionHash == 0u` を「初回」の番兵に使っているため、ハッシュがちょうど 0 になった場合に初回扱いされます。確率は無視できますが、`bool m_bRevisionHashValid` のほうが正確です。
- 英語コメント・文言の混入はありませんでした。

## 実行できなかったコマンド

- `cmake --build build --config Debug --target Game -- /m:1`(保存された `verify-R6-P1-1.txt` の EXIT_CODE=0 を採用)
- `RenderGraphCompileTest`(約 75 秒。保存された `verify-R6-P1-2.txt` と `recheck-R6-P1-2-2.txt` の Passed を採用)

`.harness/lessons/` は存在しないため教訓の選別は行っていません。

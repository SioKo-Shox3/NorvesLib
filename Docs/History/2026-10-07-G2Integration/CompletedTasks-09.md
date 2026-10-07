- stop-when: 既存のR6/R7のPT参照比較の結果が変わる場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Public/Rendering/PathTracingCamera.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8PathTracingSequenceFrame*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・PT・FramePacket）。評価者を通す。GameThread→RenderThreadはFramePacketのsnapshot越しだけ。

## R8-P3-FIX: 連番の1フレームの累積を、同じフレーム内のinstanceの並び替えで捨てない
- status: done
- done-when: R8-P3の評価（反復5）の指摘を直す。`HashPathGeometry`はinstanceの配列順で前後の変換・材質・customIndexを署名にするため、物体ごとの状態が同じでも同じSequenceFrameの中で`P,A,B`→`P,B,A`と並びが変わると累積が捨てられる。連番の経路のRTスナップショットを安定した物体キーの順に揃え、customIndex・材質texture表・発光instance表を整合させる（または署名を順序に依らない形にし、光源標本の表の順序も揃える）。`R8PathTracingSequenceFrameVulkanTest`へ、SequenceFrameと各物体の前後変換を固定したまま並びを変えた2回のdispatchで試料数が1→2と続き、固定順で描いた画像とbyte一致する検査を加える。既存のPTテストは変わらない。
- verify: `cmake --build build --config Debug --target Game R8PathTracingSequenceFrameVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8PathTracingSequenceFrameVulkanTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingVulkanTest)$"`
- stop-when: 並びの正規化がR6/R7のPT参照比較の結果を変える場合は理由を記録してユーザーへ戻す。
- paths: Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/FramePacket.h, Test/Core/Rendering/R8PathTracingSequenceFrame*, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・RenderThread）。評価者を通す。

## R8-P4: ラスタの被写界深度をPTの薄レンズ参照と比べる
- status: done
- done-when: ラスタに被写界深度のpass（深度からCoCを求め、PTと同じ薄レンズの式・撮像面高24 mm・FOVから焦点距離）を加える。focus distanceが0（ピンホール）なら働かず、承認済みgoldenは変わらない。新しい比較テストが、手前・焦点面・奥に物体を置いた静止シーンで、ラスタのDoFとPT参照（R8-P3の経路、独立な3組の画素ごとの中央値）を比べる。閾値は比較の前に固定する規則で作る: PT参照と、f値を±20%変えたPTとの知覚差（FLIP平均・一致画素の画素単位最大・8×8区画最大）のうち小さい方。±40%の変化が3つとも閾値の外にあることを比較のたびに確かめる。
- verify: `cmake --build build --config Debug --target Game R8DepthOfFieldPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8DepthOfFieldPathTracingReferenceVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。比較の入力のPTピンホール像と薄レンズ参照を画素内の一様標本（box）にそろえて取り直す（閾値の規則は不変）。それでも全画素の平均や光源の縁の区画最大がわずかに残る場合は、測定値と差の分類を既知の限界として記録してR8-P4を完了にする。
- paths: Assets/Shaders/*DepthOfField*, Library/Core/Public/Rendering/*DepthOfField*, Library/Core/Private/Rendering/*DepthOfField*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Component/CameraComponent.h, Library/Core/Private/Component/CameraComponent.cpp, Test/Core/Rendering/R8DepthOfField*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 取得（PTの3組の中央値と±20%/±40%の変種、約20分）は1反復で1回までにし、shaderやCPU比較の変更は取得済みのダンプに対する`--compare-dumps`だけで評価する（shaderは実行時に読む）。取得をやり直すのは、pass本体やC++の変更で画像が変わるときだけ。差の分類と現状は`blocked/R8-P4.md`。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: 比較入力のbox標本（`91b774b`）と既知の限界の範囲の判定（`7ca5332`。全画素の平均と光源の縁の区画最大に記録値+5%の上限、光源の縁の矩形の外は規則の閾値）。上限の値は中心標本の測定値のままで、box標本の取得の後に測定値へ合わせる。verifyは未実行。
- result: 比較の入力のPTのピンホール像と薄レンズ参照を画素内の一様標本にそろえた取得で、規則の判定PASS（平均0.0184≤0.0254、一致画素の画素最大0.356≤0.524、区画0.0529≤0.0567、±40%は外、負の対照は検出、VUID 0）。既知の限界の範囲の判定は不要になり外した（`b0f365b`）。記録は`Docs/RenderingValidation/R8Acceptance.md`。

## R8-P5: ラスタの動きぼけをPTのシャッター参照と比べる
- status: done
- done-when: ラスタに動きぼけのpass（R6-aのvelocityにシャッター時間/フレーム長を掛け、空の画素はカメラの動きから求める）を加える。シャッター時間が0なら働かず、承認済みgoldenは変わらない。新しい比較テストが、カメラのpanと既知の速度で動く物体のシーンで、ラスタの動きぼけとPT参照（R8-P3の経路、3組の中央値）を比べる。閾値の規則はR8-P4と同じで、シャッター時間を±20%変えたPTを物差しにし、±40%が閾値の外にあることを確かめる。
- verify: `cmake --build build --config Debug --target Game R8MotionBlurPathTracingReferenceVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8MotionBlurPathTracingReferenceVulkanTest|RenderingVelocityObjectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/*MotionBlur*, Library/Core/Public/Rendering/*MotionBlur*, Library/Core/Private/Rendering/*MotionBlur*, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/R8MotionBlur*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `1850803`（pass・比較テスト・動く球の床の影を既知の限界の範囲にする判定）。取得済みダンプ（`build/RenderingValidation/R8MotionBlurPathTracingReferenceRuns`）で平均0.0190（内）、影の矩形の内で画素0.794・区画0.1645。負の対照は画素の閾値が光源の縁で決まるため未検出で、既知の限界として記録する。verifyは未実行。
- result: `1850803`・`251fc92`。規則の判定FAIL（動く球の床の影で画素0.794・区画0.1646）、既知の限界の範囲の判定WITHIN（影の矩形の外は画素0.467・区画0.094で閾値内、平均0.0191は閾値内、2×2・4倍の負の対照は検出）。静止面の上を動く影は画面velocityで運べないため既知の限界として記録した。

## R8-P6: フィルムグレインを既定オフで加える
- status: done
- done-when: 出力変換の後（display空間）に、画素・フレーム番号・seedから決まるフィルムグレインを加える。強さは起動引数で指定し、既定はオフ（承認済みgoldenは変わらない）。新しいGPUテストが、オフでは出力が従来と一致し、オンでは中間グレーの平均の変化が0.5/255以内・標準偏差が指定の±10%以内、隣のフレームで模様が変わり、同じフレームの再実行でbyte一致することを確かめる。
- verify: `cmake --build build --config Debug --target Game R8FilmGrainVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R8FilmGrainVulkanTest|ToneMappingParamsLayoutTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Assets/Shaders/tonemapping.frag, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Rendering/R8FilmGrain*, Test/Core/Rendering/ToneMappingParamsLayoutTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `fce422d`。verifyは未実行。
- result: `fce422d`。強さ0で従来とbyte一致、4/255で平均の変化0.043/255・標準偏差3.999/255、隣のフレームの相関−0.004、再実行でbyte一致、VUID 0。golden屋内/屋外・ToneMappingParamsLayoutTestはpassed。

## R8-P7: EXR連番の検証exeを作る
- status: done
- done-when: C++の検証exe（TinyEXRとThirdPartyのFLIPを使う）が、連番のdirectoryと期待するフレーム数から、欠番・寸法の不一致・NaN/Inf画素（フレームと座標を出す）を検出し、R8-P1のLUT（CPU）でdisplayへ変換した隣接フレームのLDR-FLIP平均を並べ、中央値の3倍（かつ下限0.01）を超える組をポッピングとして失敗にする。規則は検査の前に固定する。単体テストが合成の連番で、正常な連番は合格、欠番・NaN・寸法違い・差し込んだ1フレームの跳びをそれぞれ検出することを確かめる。
- verify: `cmake --build build --config Debug --target R8ExrSequenceValidator R8ExrSequenceValidatorTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8ExrSequenceValidatorTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8ExrSequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `31d84e0`（単体テストはCPUだけで動く）。verifyは未実行。
- result: `31d84e0`・`2ef552d`。合成の連番で正常は合格、欠番・NaN・寸法違い・1フレームの跳び・期待と違う寸法をそれぞれ検出。

## R8-P8: 屋内・屋外の決定論的なアニメーションとEXR連番の書き出し、8フレームのCTestを加える
- status: done
- done-when: 検証アプリのhandlerが、フレーム番号iから時刻 i/24 のカメラと物体の変換を決め（屋内: Cornellで箱が滑りカメラがdollyする。屋外: 空・霧の屋外シーンでカメラが回り球が動く）、R8-P3の経路（絞り・シャッター1/48 s・フレーム長1/24 s）で各フレームを累積し、`WritePathTracingExrFrame`で書き出し、manifest（シーン・解像度・spp・フレーム範囲・seed）を残す。起動引数でシーン・フレーム範囲・解像度・spp・出力directoryを指定できる。CTest `R8SequenceSmokeTest` が屋内・屋外それぞれ8フレームを256×144・64 sppで書き、R8-P7の検証exeに合格する。
- verify: `cmake --build build --config Debug --target Game R8SequenceRenderer R8ExrSequenceValidator -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R8SequenceSmokeTest$"`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。
- paths: Test/Core/Rendering/R8Sequence*, Test/Core/Rendering/RenderingValidation/*, Test/Core/Rendering/CMakeLists.txt, Library/Core/Public/Rendering/PathTracingExrOutput.h, Library/Core/Private/Rendering/PathTracingExrOutput.cpp, TASKS.md, PROGRESS.md
- notes: GameThread→RenderThreadはFramePacket越しだけ。
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `e59e3aa`。verifyは未実行。
- result: `e59e3aa`・`2ef552d`。R8SequenceSmokeTestが屋内・屋外それぞれ8フレームを全フレームちょうど64試料で書き、検証exeに合格。

## R8-P9: 屋内の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene indoor` が屋内の240フレーム（24 fps・10秒）を1280×720・1024 sppで`build/R8Sequences/indoor/`へ書き、R8-P7の検証exeで欠番0・NaN/Inf画素0・ポッピング0を確かめ、所要時間・容量・検証結果を`.harness/runs/<日付>-r8-sequences/`へ残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene indoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（`Scripts/RenderR8Sequences.ps1`、欠けたフレームだけを24枚ずつ描き、描画のプロセスはCPUの優先度を下げる）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0108・最大0.0129・閾値0.0324）、全フレームちょうど1024試料、812 s・2.53 GB。記録は`.harness/runs/20260926-r8-sequences/`。

## R8-P10: 屋外の240フレームの連番を1280×720・1024 sppで書き出し、検査する
- status: done
- done-when: `Scripts/RenderR8Sequences.ps1 -Scene outdoor` が屋外の240フレームを同じ条件で`build/R8Sequences/outdoor/`へ書き、欠番0・NaN/Inf画素0・ポッピング0を確かめ、記録を残して0で終わる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RenderR8Sequences.ps1 -Scene outdoor`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。1反復で240フレームが書き終わらない場合は、書き出し済みの最後のフレームから続きを書く形で反復をまたいで進める。
- paths: Scripts/RenderR8Sequences.ps1, TASKS.md, PROGRESS.md
- notes: （2026-09-26 ユーザー指示: PCが重くなるため、GPUの取得と長いビルドはユーザーが「今PCを使ってよい」と言った時にまとめて回す。ループの無人実行もしない）実装: `4250b21`（R8-P9と同じスクリプト）。verifyは未実行。
- result: 240フレーム・欠番0・非有限0・ポッピング0（隣接FLIPの中央値0.0161・最大0.0207・閾値0.0483）、全フレームちょうど1024試料、685 s・1.65 GB。

## R8-P11: R8の受入れ記録を確定する
- status: done
- done-when: `Docs/RenderingValidation/R8Acceptance.md` に、ACES基準画像との一致（R8-P2）、DoF・動きぼけのPT参照比較（R8-P4/P5）、フィルムグレイン（R8-P6）、屋内・屋外の240フレームの連番の検査（R8-P9/P10）の結果とログ、既知の制限、GPU性能はDeferredを記録し、完了コミットの本文末尾に `RenderingRoadmap: R8 complete` trailerを付ける。PROGRESS.mdに完了を記録する。
- verify: `git diff --check`
- stop-when: （2026-09-26 ユーザー指示: 判断のたびに止めず、推奨の選択肢で最後まで進める）判断が要る場面では、選択肢と推奨をこのタスクのnotesへ1行で記録し、推奨の対処でそのまま進める。閾値の規則と承認済みgoldenは変えず、変えないと通らない差は測定値と分類を既知の限界として記録してタスクを完了にする。ユーザーへは戻さない。既知の限界として完了にしたタスクは、受入れ記録に測定値・閾値・分類を明記したうえでtrailerを付ける。
- paths: Docs/RenderingValidation/R8Acceptance.md, Docs/RenderingValidation/R8ColorManagement.md, TASKS.md, PROGRESS.md
- notes: 危険地帯を含む機能の完了判定。評価者を通す。
- result: `Docs/RenderingValidation/R8Acceptance.md`。R8の変更の独立評価は1周目NEEDS_WORK（4件）、対応差分の2周目PASS。

## R6-P7: RTGIで発光三角形を光源標本する
- status: done
- done-when: RTGIの1次面と命中点の直接光に、レイトレーシングシーンの発光三角形の光源標本（影の問い合わせ付き）を加え、面光源の直接光と面光源に照らされた面からの1バウンスを雑音の少ない推定にする。`R6RTGIPathTracingReferenceVulkanTest`がR6-P5-REFで固定した閾値（間接光±20%の物差し）内に入り、R6受入れと既存のRTGI・DDGI・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R6RTGIPathTracingReferenceVulkanTest R6RTGIAcceptanceVulkanTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R6RTGIAcceptanceVulkanTest|RTGIDiffuseIndirectVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR6-P5-REFの閾値を変えないと通らない場合は、方式の再選定としてユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI shader・LightingPass・RenderThread）。R6-P5-REFで発見。ラスタは発光三角形を光源として扱わず、面光源の直接光がRTGIの偶然の命中だけで入るため斑点状の雑音になる。
- result: 光源標本（`128294a`、評価対応`02eaa7e`）、RTGIへSSAOを重ねない（`81bbf24`）、デノイズの外れ値抑制（`3f4f39c`）、PTの画素中心標本（`a15c43a`）に加え、ユーザーの判断で画素単位最大を幾何が一致する画素で判定し直接光を解析BRDFで揃え（`c162366`・`f7d0b59`・`775261a`）、静止時だけRTGIの履歴を延長し年齢に応じてデノイズの近傍の重みを下げた。R6参照比較は平均0.0466・一致画素の最大0.150・区画0.100で閾値内（閾値と物差しは不変）。記録は`Docs/RenderingValidation/R6Acceptance.md`の「R6-P7の完了」。

## TEST-R6-RESIDUAL-TIMING: R6停止残留の判定が起動の早さで変わる原因を直す
- status: done
- done-when: `R6RTGIAcceptanceVulkanTest`の停止残留（`R6_STOP_RESIDUAL_CHECK`）が、単体実行とRenderingValidationラベル内の実行で同じ物体影響の大きさになり、ラベル全件を3回続けて実行して毎回通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^R6RTGIAcceptanceVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 物体影響の差がRTGI/DDGIの実装の不具合（履歴や更新順）による場合は、テストの待ち時間で隠さずR6の不具合として記録し直す。
- paths: Test/Core/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。初回取得は資産読み込みの完了（壁時計に依存）で始まり、以降の段階はフレーム数で進む。単体実行は最初の段階がframe 153前後で物体影響0.018〜0.06（残留比0.001〜0.003）、ラベル内はframe 76前後で物体影響が約1.1e-4まで落ち、残留比が閾値0.5付近（R7-P3Cのラベル実行で0.477、R7-P3Dで0.506）になる。R6-P6の前に直す。
- result: 物体影響の差はRTGIの不具合（面光源の直接光がRTGIの偶然の命中だけで入る）によるもので、R6-P7の光源標本で解消した。テストの待ち時間は変えていない。単体実行2回は最初の段階がframe 172〜175・物体影響0.0251〜0.0280、RenderingValidationラベル全件の3回連続実行は各回frame 166〜177・物体影響0.0267〜0.0289・残留0.0058前後（残留比約0.2、閾値0.5）で毎回通過した。ラベルの2・3回目に失敗した1件は、実行中に追加した未完成のR4再照合テスト（R4-REOPEN）で、R6系は3回とも通過。ログは`.harness/runs/20260924-test-r6-residual/`。

## R6-P6: R6受入れと性能gate保留を確定する
- status: done
- done-when: R6-P1〜P5のコードコミット、受入れログ、golden/threshold、fallback、既知の制限、R7暫定参照の再照合条件を記録し、完了コードコミットへ `RenderingRoadmap: R6 complete` trailerを付ける。GPU性能はDeferredとして残す。
- verify: `git diff --check`
- verify: `git log -1 --format=%B`
- verify: `rg -n "R6|RTGI|性能|Deferred|fallback" Docs/RenderingValidation/R6TechniquePlan.md Docs/RenderingValidation/R6Acceptance.md PROGRESS.md`
- stop-when: R6の機能gateが未完、またはR7/R8の実装を前提にしないと受入れできない場合は、完了trailerを付けず残課題を記録する。
- paths: Docs/RenderingValidation/R6TechniquePlan.md, Docs/RenderingValidation/R6Acceptance.md, TASKS.md, PROGRESS.md, NEXT_FINDINGS.md
- result: `R6Acceptance.md`の判定を受入れへ確定し、R6-P5-REF・R6-P7のコミットと検証ログ、既知の制限を加えた。全体gate（`.harness/runs/20260925-r6-p6/`）はtargetless Debug build BUILD_EXIT=0、RenderingValidation 56件中47 passed・8 skipped・1 failed（再オープン中のR4の再照合だけ）。R6の完了判定の独立評価はPASS（blockingなし。non-blocking 3件の記述の正確さは同じコミットで直した）。GPU性能はDeferred。

## R4-REOPEN: R4 DDGIのprobe由来の斑点と漏れを直し、自前PT参照で再照合する
- status: done
- done-when: `R4DDGIPathTracingReferenceVulkanTest`（R4の規定の指標: direct white ROIで露出を1回決め、影・赤・緑ROIの相対輝度誤差≤0.25、赤・緑ROIの優勢色度の差≤0.10）が同じ閾値で通り、R4の受入れ・DDGI系・R6・golden testが通る。
- verify: `cmake --build build --config Debug --target Game R4DDGIPathTracingReferenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R4DDGIPathTracingReferenceVulkanTest|DDGI.*|RenderingDDGILightingContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 閾値かR4の指標を変えないと通らない場合、または承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（DDGI shader・LightingPass）。R7-P7の再照合（更新ルール5、2026-09-25）で発見。R4受入れと同じCornell状態（DDGI有効、RTGI無効、点光源なし、天井の面光源あり、512×512、128フレーム後）のラスタを、4096 sppの自前PT（全輸送）と比べ、赤ROIの相対輝度誤差0.257（上限0.25）、影0.162、緑0.0566、色度差0.0091/0.0167、露出0.846。ラスタのROI平均はR4受入れ時の記録（direct 0.393677、shadow 0.15236、red 0.0687421、green 0.0951394）と同値で、劣化ではない。画像全体ではprobeの位置に格子状の明るい斑点、奥の壁の暗転、画面の縁の暗い帯があり、64画素区画のラスタ/PT輝度比は0.07〜2.15に散らばる（`.harness/runs/20260925-r7-p7-r4/pt-vs-ddgi.png`、`ratio-map.txt`）。probe格子（原点-0.1、間隔0.82、8×8×8）の外側の層は壁の裏と開口の外にある。
- result: `2588b47`・`effd5e3`・`355f8df`・`3b1f3f4`・`ce9d10f`。probeの分類（面の裏のhitが25%を超えるprobeを無効）、probeでの発光面の直接照度（Lambertの式と影の可視率）、照度atlasの全体/間接光だけの2組のlayer（hit面は間接光の組を読み、直接光の二重計上をなくす）、RTXGIの補間（法線方向0.225倍のずらし、押しつぶし、平方根の補間）、距離のcosineの指数8。`R4DDGIPathTracingReferenceVulkanTest`は影0.117・赤0.224・緑0.130・色度差0.030/0.011で合格（閾値不変）、公開Cornell参照0.104/0.117/0.019、動的0.875/0.871。全target build後のRenderingValidationラベル57件中0件失敗（8件はGPU skip契約）、DDGI系9/9。危険地帯の独立評価2周の指摘（鏡映の表裏、発光面の近くの過大評価、頂点の順による放射の側）は修正して回帰の場面を追加した。記録は`Docs/RenderingValidation/R4Acceptance.md`、ログは`.harness/runs/20260925-r4-reopen/`。

## R6-GATE-DDGI-ORACLE: DDGI放射輝度比較のシナリオ履歴を分離する
- status: done
- done-when: 非遮蔽と点/スポット遮蔽のreadbackがそれぞれ固定の直接照明期待値に一致し、二つ目のケースへ前ケースのprobe irradiance蓄積を持ち越さない。rendererのradiance計算・期待値・閾値は変更しない。
- verify: `cmake --build build --config Debug --target DDGIProbeRadianceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^DDGIProbeRadianceVulkanTest$"`
- stop-when: pass状態を分離しても期待値とreadbackが一致しない場合、radianceや閾値を調整せず追加原因を記録する。
- paths: Test/Core/Rendering/DDGIProbeRadianceVulkanTest.cpp, TASKS.md, PROGRESS.md

## R6-GATE-OUTDOOR: 承認済みOutdoor goldenとの差分を原因診断する
- status: done
- done-when: `RenderingGoldenOutdoorVulkanTest`が既存golden/thresholdを変更せず通過する。原因と修正を記録し、修正後の`RenderingValidation`全体で新規失敗がないことを確認する。既定のRendering3DTest起動経路と保存シーンは維持する。
- verify: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderingGoldenOutdoorVulkanTest$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 原因が既承認baselineの改定を要する場合はgoldenを書き換えず根拠を保存する。RHI image-layout等の独立不具合を検出した場合は範囲を分けて記録し、該当経路を隠さない。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering/RenderingValidation, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-M1: NEE/MISとEXR出力方式を選定する
- status: done
- done-when: power heuristic MIS、ReSTIR除外、TinyEXR C API version、EXR channel/precision/compression、seed決定論性、R7 core/outdoor分割を技術選定記録と実装計画に固定する。
- verify: `rg -n "S7|NEE|MIS|ReSTIR|TinyEXR|6f470c9|RenderingRoadmap: R7 complete" Docs/RenderingValidation/R7SamplingAndExrSelection.md Docs/RenderingValidation/R7TechniquePlan.md`
- verify: `git diff --check`
- stop-when: 選定したEXR APIが必要なWindows/CMake buildとfloat scanline出力を満たさない場合、実装開始前に代替を根拠付きで記録する。
- paths: Docs/RenderingValidation/R7SamplingAndExrSelection.md, Docs/RenderingValidation/R7TechniquePlan.md, TASKS.md, PROGRESS.md

## R7-P1: ラスタとPTのBRDF・texture評価を共有する
- status: done
- done-when: raster opaque/transparentとRT shaderが共通BRDF・texture evaluationを参照し、R1数値契約とIndoor/Outdoor goldenを維持する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|ForwardPassPipelinePlacementTest|LightingParamsLayoutTest)$"`
- stop-when: include機構が既存shader compilerで成立しない場合、全shaderを一括移行せず最小の共有境界を記録する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P2: 独立path-tracing pipelineとprogressive accumulationを実装する
- status: done
- done-when: 明示選択のPT pipelineがR5 RT pipeline/SBTとFramePacket snapshotだけで1 sample/frameを累積し、静止時に収束、camera/scene revision変更時に履歴をresetする。raster pipelineとRendering3DTest起動経路は不変。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingVulkanTest$"`
- stop-when: RHI/Vulkan APIやRenderThreadからWorldへの参照が不可避となった場合、境界を越えずsnapshot不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## R7-P3A: RHIに配列combined image samplerとnon-uniform indexingを追加する
- status: done
- done-when: `RHI::DescriptorBinding`に配列数`count`（既定1）と配列要素単位のcombined image sampler bindを追加し、Vulkanのset layout・pipeline layout・descriptor pool・writeが`count`を反映する。Vulkan 1.2の`shaderSampledImageArrayNonUniformIndexing`を対応時だけ有効化して`DeviceCapabilities`へ公開する。`count=1`の既存bindingのlayout・pool容量・writeは変えない。専用GPUテストで4要素配列を呼び出しごとに異なる添字で標本化し、各要素の既知色をreadbackで一致させる。範囲外要素・非配列bindingへのbindは拒否する。
- verify: `cmake --build build --config Debug --target Game RHIDescriptorArrayVulkanTest RHIRayTracingPipelineVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RHIDescriptorArrayVulkanTest|RHIRayTracingPipelineVulkanTest|RHIRayTracingApiContractTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation --timeout 180`
- stop-when: 対応GPUでnon-uniform indexingを有効化できない、または既存descriptor setの割り当て・更新が回帰する場合は、RHI変更を戻して不足した能力を記録する。
- paths: Library/Core/Public/RHI/IDescriptorSet.h, Library/Core/Public/RHI/DeviceCapabilities.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.h, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Test/Core/Rendering/RHIDescriptorArrayVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI公開API・Vulkan）。独立評価を通す。

## R7-P3B: PTの材質snapshot・texture・シェーディング法線を接続する
- status: done
- done-when: `RayTracingHitMaterialSnapshot`へGBufferと同じ規則のinstance色（custom dataの非0成分、既定1）とalbedo・normal・metallic・roughnessのtexture handleを値として加える。PTはRenderThreadでtexture handleを解決し、重複を除いた配列descriptorへ束ね、未設定はGBufferと同じ既定値（白・平坦法線・metallic 0・roughness中間灰）にする。closest-hitは`Mesh3DVertex`の法線・UVを重心補間し、共通shaderの材質評価でalbedo・法線マップ・metallic・roughnessを得る。発光はGBuffer同様`色×nits`にpre-exposureを掛ける。既存のposition-only頂点（stride 12）は幾何法線・UV 0へfallbackする。GPUテストでtexture UV標本化、metallic/roughness snapshot、既定値、instance色、発光のpre-exposureをreadbackで固定し、既存PT/屋外/霧/カメラ/EXRテストに回帰がない。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingMaterialVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest PathTracingCameraVulkanTest PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingCameraTest|PathTracingCameraVulkanTest|PathTracingExrOutputTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: texture資源の寿命をFramePacketとRenderResourcesの既存所有境界で保証できない、またはラスタgoldenが変わる場合は、材質APIを広げず不足を記録する。
- paths: Library/Core/Public/Rendering/MaterialTypes.h, Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Assets/Shaders/PathTracing, Assets/Shaders/Common/PbrMaterialEvaluation.glsl, Assets/Shaders/gbuffer.frag, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/PathTracingMaterialVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（Rendering公開API・RT資源寿命）。独立評価を通す。

## R7-P3C: NEE/MISとpoint・spot・directional・area light・環境光輸送を実装する
- status: done
- done-when: FramePacketのLightProxyからラスタと同じ単位・減衰・spot円錐の光源表をPTへ渡し、delta lightはNEEだけ（weight 1）、発光三角形と太陽円盤はlight sampleとBSDF sampleをpower heuristic β=2で合成する。BSDF sampleは拡散cosineとGGX可視法線分布の混合で、PDFと共通BRDF評価を同じ方向で整合させる。環境光（一様値またはHDR equirect）はBSDF sampleで評価し、固定0.05の仮背景を廃止する。GPUテストで、R1白炉と同じ15行（roughness 5段×metallic 3段、平均相対誤差1%・最大3%）、既知cdの点光源によるLambert面の解析輝度、面光源のNEEのみ・BSDFのみ・MISが同じ期待値へ収束すること、spot円錐外0、directional照度を固定seedで検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingLightingVulkanTest PathTracingVulkanTest PathTracingOutdoorVulkanTest PathTracingVolumetricTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingLightingVulkanTest|PathTracingVulkanTest|PathTracingMaterialVulkanTest|PathTracingOutdoorVulkanTest|PathTracingVolumetricTest)$"`
- stop-when: 白炉・解析照明が共通BRDFの近似に起因して閾値を超える場合は、閾値を緩めず、PT推定量の誤りと共通BRDFのエネルギー差を分けて記録する。
- paths: Library/Core/Public/Rendering/PathTracingPass.h, Library/Core/Private/Rendering/PathTracingPass.inl, Assets/Shaders/PathTracing, Assets/Shaders/Common, Test/Core/Rendering/PathTracingLightingVulkanTest.cpp, Test/Core/Rendering/PathTracingVulkanTest.cpp, Test/Core/Rendering/CMakeLists.txt, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（シェーダーパイプライン・RenderThread）。独立評価を通す。

## R7-P3D: 起動時PT選択とラスタ/PT同一シーン比較を固定する
- status: done
- done-when: RenderingCoordinatorの初期化設定でmain SceneViewをPT pipelineにでき（既定はraster、`Rendering3DTest`起動は不変）、PTはLightingPassと同じ環境マップ設定と検証用一様環境（debug mode 252）を使う。描画検証appに`--renderer=path-tracing`と累積試料数の指定を加え、capture時の実累積試料数をcapture結果へ記録する。R1の白炉と解析点光源の行をPTで実行して同じ評価関数に通し、同じ行のラスタSceneColorとPT SceneColorの差を事前に固定した閾値で比較する。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingRasterParityVulkanTest|RenderingHdrSceneCaptureVulkanTest.*)$"`
- verify: `build\\Game\\Debug\\Game.exe --imgui --exit-after-rendered-frames=120`
- stop-when: PT選択がRenderThreadのpass寿命やFramePacket境界を変えないと成立しない場合は、実行時切替を追加せず起動時選択の不足を記録する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread・pass寿命）。独立評価を通す。

## R7-P4: SPP収束とCornell参照を固定する
- status: done
- done-when: 同じseedのnested 16/64/256 spp prefixで256 spp自己収束画像に対するMSEが単調減少し、Cornell boxが固定公開参照と規定誤差内で一致する。
- verify: `cmake --build build --config Debug --target PathTracingConvergenceVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingConvergenceVulkanTest$"`
- stop-when: monotonic結果がseed探索だけに依存する場合、閾値を緩めずsample estimatorと測定手順を再検討する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- result: 収束は合格（入れ子の接頭列のMSEが全体と64区画すべてで単調、MSE比の中央値4.374、引き直しでbyte一致）。公開RGBEは色の符号化が公開されておらず色の壁の彩度を再現できないため、ユーザーの判断で輝度の判定を白い面・影・発光面の位置に限り、赤・緑の壁とR4の赤・緑ROIは優勢色度で判定する範囲へ見直した（数値の上限は事前固定のまま）。影ROI 0.0025、赤・緑ROIの色度差0.0044・0.0217、壁の優勢な成分が一致、発光面のずれ1画素、580区画の中央値0.0101・90%点0.0340で合格。記録は`Docs/RenderingValidation/R7CoreAcceptance.md`。

## R7-P5: thin-lensとshutter time samplingを実装する
- status: done
- done-when: FramePacketのcurrent/previous camera・geometry snapshotからshutter時刻を決定論的に評価し、薄レンズCoCの数値誤差と静止/移動goldenを固定する。
- verify: `cmake --build build --config Debug --target Game PathTracingCameraTest PathTracingCameraVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingCameraTest|PathTracingCameraVulkanTest)$"`
- stop-when: 変換補間が不正なshear/scaleを生む場合、live World参照や別のmotion systemを追加せず対応可能なtransform範囲を定義する。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P6: seed固定EXR sequence出力を実装する
- status: done
- done-when: TinyEXR v3 C APIでlinear RGB float/ZIP scanlineを出力し、NaN/Infを拒否する。同じscene/seed/SPP/frameの2出力がbyte一致または事前閾値内である。
- verify: `cmake --build build --config Debug --target Game PathTracingExrOutputTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingExrOutputTest$"`
- stop-when: independent EXR readerがchannel/precision/windowを一致して読めない場合、sequence APIを広げずwriter integrationを修正する。
- paths: CMakeLists.txt, Library/ThirdParty, Library/Core, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-P7: R7 coreを受入れ、R4/R6暫定参照を再照合する
- status: done
- result: `Docs/RenderingValidation/R7CoreAcceptance.md`（2026-09-25）。R6は自前PT（拡散2バウンス、median of means）と閾値内で合格、R4は赤ROI 0.257（上限0.25）で更新ルール5により再オープン（R4-REOPEN）。全体CTestは`.harness/runs/20260925-r7-gate/ctest-all.txt`。
- done-when: R7 coreの完了条件、公開Cornell参照、R1数値検証、CoC、決定論EXR、既知制限を集約し、同一条件のself PTでR4 DDGIとR6 RTGIを各1回再照合する。Outdoor extension完了前にR7 trailerは付けない。
- verify: R7 core関連Debug build/CTestとEXR finite-scanを実行し、全出力ログを開いて閾値結果を確認する。
- verify: `git diff --check`
- stop-when: R4/R6比較がRoadmap閾値を超えた場合、phaseを完了扱いにせず再オープン理由と再検証単位を記録する。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## FIX-CSM-MEGA-CASTER-BOUNDS: CSMの遮蔽物の境界球にShadowMapPassが描かないMegaGeometryを含めない
- status: done
- done-when: CSMの深度範囲に含める境界球が、影の地図に実際に描く物体と一致する（MegaGeometryを影に描くまでは含めない、または描くようにする）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(DirectionalShadowLightMatricesTest|CascadedShadowLightMatricesTest)$"`
- stop-when: MegaGeometryを影に描くかの判断が要る場合はユーザーへ戻す。
- paths: Library/Core/Private/Rendering, Test/Core/Rendering
- notes: `382489f`の評価のnon-blocking指摘。過大収集で深度範囲とPCSSの探索半径が広がるだけで、影は欠けない。2026-10-03 SS-CSM-MEGA-CASTERS で「描く」側で閉じた（CSMの深度範囲へ含めるMegaGeometryの境界球を、実際に描くキャスターの一覧から取る）。

## R7-O1: R2 SkyAtmosphereをPT miss radianceとsolar samplingへ接続する
- status: done
- done-when: rasterと同じSkyAtmosphereParametersからPTのsky miss radianceとsolar disk direct lightingを構成し、朝/昼/夕3時刻の有限性・parameter parityを検証する。
- verify: `cmake --build build --config Debug --target Game PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- stop-when: R2 parameter semanticsを変更しないと接続できない場合は、R2を先行修正しgoldenを書き換えない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## R7-O2: R3 volumetric fogをPT ray transportへ接続する
- status: done
- done-when: rasterと同じfog density/height/anisotropy parameterからfinite transmittanceとsingle-scatteringを評価し、fog/sky disabled fallbackを維持する。
- verify: `cmake --build build --config Debug --target Game PathTracingVolumetricTest PathTracingOutdoorVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingVolumetricTest|PathTracingOutdoorVulkanTest)$"`
- stop-when: R3 public parameter semanticsを拡張しないと一致しない場合はR3側契約差として記録し、RenderingからVulkanを参照しない。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md

## SKY-SUN-P1: 空の太陽の地表照度と方向光をCPUで一つに定める
- status: done
- result: `fa40e74`。透過率・地表照度・空の太陽の方向光をSkyAtmosphere/SkySunLightの公開関数にし、透過率LUTも同じ関数で作る（既存LUT値は不変）。
- done-when: 公開APIで、空のパラメータから地表での大気の透過率（RGB、透過率LUTと同じ式・同じ定数）と、太陽の地表照度（太陽円盤の照度×透過率、RGB）と、空の太陽を表す方向光（予約LightId、方向=太陽方向の逆、色=透過率、強度=照度の輝度、影あり）を求められる。空が無効なら方向光を作らない。透過率LUTの生成は同じ関数を使い、既存LUTの値は変わらない。
- verify: `cmake --build build --config Debug --target SkyAtmosphereModelTest SkyAtmospherePassContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(SkyAtmosphereModelTest|SkyAtmospherePassContractTest|SkyAtmosphereIblTest)$"`
- stop-when: LUTの既存値が変わる場合は共有化を止め、同じ式の複製とLUTとの一致テストへ切り替える。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: ユーザー決定（2026-09-25）「空の太陽に統一」。空が有効なら空の太陽からエンジンが方向光を作り、ラスタはCSMの影付きで照らし、PTは同じ太陽を円盤の光源標本で数える（二重に数えない）。シーンの方向光は追加の光として残す。既定の起動は空が無効で変わらない。

## SKY-SUN-P2: 空の太陽をFramePacketの光源へ加え、ラスタの影をその灯へ掛ける
- status: done
- result: `4abdbcd`。FramePacket作成で空の太陽を1つだけ加え、CSM・RT影・LightingPassが同じ選び方（`603f366`で専用ヘッダへ分離）で影の灯を決める。
- done-when: GameThreadのFramePacket作成で、空が有効なら空の太陽の方向光を光源表へ1つだけ加える（再利用するpacketでも重複しない）。影を落とす方向光の選択は、空の太陽があればそれを選び、なければ従来どおり「表示される方向光がちょうど1つで影あり」のときだけ選ぶ（CSM・RT影で共通の関数）。ラスタ（lighting.frag・forward_transparent.frag）はCSM/RT影をその灯にだけ掛け、他の方向光は影なしで照らす。空が無効なシーンの描画と既存golden・閾値は変わらない。
- verify: `cmake --build build --config Debug --target Game CascadedShadowLightMatricesTest DirectionalShadowLightMatricesTest LightingLightBufferTest LightingParamsLayoutTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(CascadedShadowLightMatricesTest|DirectionalShadowLightMatricesTest|LightingLightBufferTest|LightingParamsLayoutTest|RenderingRayTracingShadowVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なシーンのgoldenが変わる場合は止めて原因を直す。承認済みgoldenの更新が必要になったらユーザーへ戻す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（光源・CSM・RT影・RenderThread）。影の係数は現在すべての方向光に同じ値を掛けるため、方向光が2つになるとCSMもRT影も止まる。影を掛ける灯は光源データの未使用の成分で示し、UBOの配置は変えない。

## SKY-SUN-P3: PTの太陽を地表照度（透過率込み、RGB）で数え、方向光と二重にしない
- status: done
- result: `fc7e985`・`e42a185`。PTの太陽円盤はRGBの地表照度で数え、空の太陽の方向光は点・spot・方向光の評価から除く。
- done-when: PTの太陽円盤の光源標本と2次以降の円盤命中のMISが、SKY-SUN-P1の地表照度（RGB）を使う。PTの点・spot・方向光の評価は空の太陽の方向光を除き、霧の単一散乱はラスタと同じ灯（CSMの灯）を使う。空を有効にした同じシーンで、ラスタの空の太陽の方向光とPTの太陽が同じ照度になる。R7-O1の屋外テストの期待値は同じ公開関数から求める。
- verify: `cmake --build build --config Debug --target Game PathTracingVulkanTest PathTracingLightingVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(PathTracingOutdoorVulkanTest|PathTracingVolumetricTest|PathTracingLightingVulkanTest|PathTracingVulkanTest)$"`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 空が無効なPTの基準（R6/R7参照比較、R1白炉・解析照明）が変わる場合は止めて原因を直す。
- paths: Library/Core/Private/Rendering, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（PT・光源）。現状のPTの太陽照度はスカラーで大気の透過率を掛けていない（`PathTracingPass.inl`の`SkyState[1]`）。

## R7-O3: 屋外PT/raster相互比較を受入れR7を完了する
- status: done
- result: `Docs/RenderingValidation/R7OutdoorAcceptance.md`（2026-09-25）。3時刻とも閾値内（朝 平均0.0107/0.0274・一致画素最大0.0989/0.1293・区画0.0368/0.0682、昼 0.0153/0.0189・0.0937/0.1704・0.0576/0.0668、夕 0.0127/0.0238・0.0903/0.1367・0.0376/0.0583）。判定の参照はラスタが実装する輸送（拡散2バウンス）のPT、median of means、影の縁の除外と影の内側の負の対照（ユーザーの判断）。全輸送との差は既知差として記録。
- progress (2026-09-25): 比較テスト`6d7628f`。ラスタ側の修正: 低い太陽の影（`fdbc5dc`）、空の照明を地表から見た空へ（`6753496`）、CSMの深度範囲の向き（`6f1dc0b`）と遮蔽物の境界球（`382489f`）、影の余裕（`d3f254a`）、カスケード境界（`fa00d84`）、RTGI/DDGIの命中面の反射率（`3c80458`）、直接光のAO（`49116c3`）、RTGIの拡散2バウンス（`44d9a3a`、ユーザー決定）。Outdoor goldenは`efce943`で再承認。
- progress: 3時刻ともFLIP平均は閾値内（朝0.0127/0.0277、昼0.0168/0.0196、夕0.0163/0.0245）。8×8区画最大は朝のみ閾値内（昼0.0712/0.0709、夕0.0988/0.0736）、画素単位最大は3時刻とも超過（閾値を超える一致画素 朝17・昼33・夕182）。上位200画素の内訳（昼/夕）は影の縁134/152、3回目以降のバウンス64/21、その他2/27（夕の緑の球の影側の面）。ログは`.harness/runs/20260925-rtgi-2bounce/`。
- done-when: sky/volume込みの屋外sceneでPTとrasterを3時刻比較し、FLIP pool/max-pixel threshold内であり、既知近似差を記録する。R7 core + outdoor extension受入れcommit末尾に`RenderingRoadmap: R7 complete`を付ける。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingOutdoorVulkanTest$"`
- verify: relevant R7 Debug build/CTestと3時刻captureの数値レポートを開いて確認する。
- stop-when: 3時刻の一部で閾値超過、NaN/Inf、未解決のR7 core acceptanceが残る場合、完了trailerを付けない。
- paths: Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md

## G1-GR08-P1: カプセルの形状間分離距離を追加する
- status: done
- done-when: Capsule 対 Sphere/OBB/Capsule の分離距離・法線・表面点・侵入判定を解析解で確認し、既存接触判定を維持する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Math/GeometrySeparationTest.cpp Library/Core/Private/Math/GeometryIntersection.cpp -o /tmp/norves-separation && /tmp/norves-separation
- stop-when: 描画への変更、未承認判断、公開契約のblocking指摘が残る場合。
- paths: Library/Core/Public/Math, Library/Core/Private/Math/GeometryIntersection.cpp, Test/Core/Math, TASKS.md, PROGRESS.md
- notes: Windows/Core全体/CTest/GPUの検証は別。既存SS/R/FIX項目の状態と順序は変更しない。

## G1-GR08-P3M: 球とカプセルの並進掃引の数学部分を追加する
- status: done
- done-when: 球/カプセル対球/OBB/カプセルの掃引を解析例と独立接触oracleで確認し、初期重なり・未収束・無効入力を区別する。
- verify: g++ -std=c++23 -O2 -I Library/Core/Public Test/Core/Math/GeometrySweepTest.cpp Library/Core/Private/Math/GeometryIntersection.cpp -o /tmp/norves-sweep && /tmp/norves-sweep
- stop-when: 未承認の意味変更、描画変更、安全側の進行または検証が成立しない場合。
- paths: Library/Core/Public/Math, Library/Core/Private/Math/GeometryIntersection.cpp, Test/Core/Math, TASKS.md, PROGRESS.md
- notes: P1に依存。Physics統合前のCPU数学だけを先行する。未収束を確定ヒットへ変換しない。

## G1-GR08-P2A: 物理クエリの値型とフィルタ契約を用意する
- status: done
- done-when: 既存enum/handleを維持し、OS非依存の値型とレイヤー・trigger・ignore・ペアマスクの判定を試験する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/PhysicsQueryTypesTest.cpp -o /tmp/norves-query-types && /tmp/norves-query-types
- stop-when: 既存型の意味変更、循環依存、公開契約のblocking指摘が残る場合。
- paths: Library/Core/Public/Scene, Library/Core/CMakeLists.txt, Test/Core/Object, TASKS.md, PROGRESS.md
- notes: 実クエリ・コライダーへの接続はP2B。ゲーム固有のレイヤー名は定義しない。

## G1-GR08-P2B: 単一物理プロキシへ統合クエリを追加する
- status: done
- done-when: 実プロキシにray/overlap/sweepを適用し、フィルタ・UserData・法線方向・無効入力・未収束の契約を検証する。
- verify: g++ -std=c++23 -O2 -ffunction-sections -fdata-sections -I Library/Core/Public -I Library/Modules/Physics/Private Test/Modules/Physics/PhysicsProxyQueryTest.cpp Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp Library/Core/Private/Math/GeometryIntersection.cpp -Wl,--gc-sections -o /tmp/norves-proxy-query && /tmp/norves-proxy-query
- stop-when: 描画/OS本体の変更、未承認判断、公開契約のblockingが残る場合。
- paths: Library/Core/Public/Scene, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics, TASKS.md, PROGRESS.md
- notes: 単一proxyの実コードの検証。コライダー設定/複数proxy集約/SceneQuery接続は後続。

## G1-GR08-P2C: 物理クエリの複数ヒットを集約する
- status: done
- done-when: RaycastClosest/All、overlap/sweepの決定的順序、MaxHits、空/失敗/未収束の出力契約を実コードで検証する。
- verify: g++ -std=c++23 -O2 -ffunction-sections -fdata-sections -I Library/Core/Public -I Library/Modules/Physics/Private Test/Modules/Physics/PhysicsProxyQueryTest.cpp Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp Library/Core/Private/Math/GeometryIntersection.cpp -Wl,--gc-sections -o /tmp/norves-proxy-query && /tmp/norves-proxy-query
- stop-when: 既存動作変更、描画/未承認判断、blocking指摘が残る場合。
- paths: Library/Modules/Physics/Private/Physics/PhysicsBroadphase.h, Library/Modules/Physics/Private/Physics/PhysicsBroadphase.cpp, Test/Modules/Physics/PhysicsProxyQueryTest.cpp, TASKS.md, PROGRESS.md
- notes: spanの実集約コードを検証。VariableArrayの実メモリシステム結合とEngine起動は未検証。

## G1-GR01-P1: 固定更新群とComponentの更新設定を追加する
- status: done
- done-when: 固定8群、既定Default/priority0、群maskの不正値拒否、Componentの既定OnTickGroup契約を確認する。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/TickGroupConfigurationTest.cpp -o /tmp/norves-tick-group && /tmp/norves-tick-group
- stop-when: 未承認方式変更、既存Tickの意味変更、公開契約のblockingが残る場合。
- paths: Library/Core/Public/Component, Library/Core/Private/Component/Component.cpp, Library/Core/CMakeLists.txt, Test/Core/Object, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: World実行順への接続は後続。個別の依存連携はDelegateで扱う。

## G1-GR01-P2: Worldの段階更新と更新中の遅延破棄を接続する
- status: done
- done-when: 一回収集と前後段/Fixedの安全な実行、翌frame追加/設定反映、破棄前entry無効化、旧fixedstepcleanup順を保つ。
- verify: g++ -std=c++23 -I Library/Core/Public Test/Core/Object/TickDispatchTest.cpp -o /tmp/norves-tick-dispatch && /tmp/norves-tick-dispatch
- verify: WindowsでEntityTreeTestをビルドしWorldTickGroupTestを実行（現環境では未実行）。
- stop-when: 寿命の未解消問題、既存fixedstepcleanup回帰、scope外変更、blockingが残る場合。
- paths: Library/Core/Public/Component, Library/Core/Private/Component/Component.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Object/Entity.h, Library/Core/Private/Object/World.cpp, Library/Core/Private/Object/Entity.cpp, Library/Core/CMakeLists.txt, Test/Core/Object, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: ApplicationのLateTick配線とカメラ移設は後続。個別依存のgraphは作らない。

## G1-GR01-P3: シミュレーションの物理後更新を実フレームへ接続する
- status: done
- done-when: 固定0回でも後段が走り、pauseでは止まる。World→Module→Handlerの後段順をSync前に置き、既存fixedstep cleanup/外側処理を保つ。
- verify: WindowsでApplicationFixedStepPipelineTestをビルド・実行（現環境では未実行）。
- stop-when: 既存fixedstep/ポーズ/外側フレームの回帰、handler寿命の問題、blockingが残る場合。
- paths: Library/Core/Public/Application, Library/Core/Public/Module, Library/Core/Private/Module/ModuleRegistry.cpp, Library/Core/Public/Engine/ApplicationProcessor.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, Test/Core/Engine/ApplicationFixedStepPipelineTest.cpp, Docs/Architecture/TickStages.md, TASKS.md, PROGRESS.md
- notes: コードの順序・gateは静的確認、実統合の合格は未主張。

## G1-GR01-P4: 評価済みボーン姿勢をゲーム側へ公開する
- status: done
- done-when: Animation/PoseFinalizeで評価し、model/world行列と名前引き/serialを公開。旧paletteを保持し、無効資産や表現不能なTransformで成功扱いしない。
- verify: SkeletalAnimationSamplingTestの既存bundleへreadback/cache/lookupケース追加（Windows未実行）。
- stop-when: 行列規約不整合、未解決のcache寿命、資産形式変更、blockingが残る場合。

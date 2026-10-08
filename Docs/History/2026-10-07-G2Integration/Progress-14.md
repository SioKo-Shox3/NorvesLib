- 既知の限界: ページの要求は「非常駐の子」にだけ出るので、常駐したページの最後に要求されたフレームは進まず、LRU が読み込んだ順になる。作業集合が目標を少し超えると、使われているページを外して読み直す入れ替わりが続く（既定の枠では起きない）。`TASKS.md` に VTG5-PAGE-TOUCH を積んだ（VTG5-STRESS-GEOMETRY の前）。
- 検証: `verify-VTG5-PAGE-STREAMER-1-build.txt`（Debug の Game・RenderResourcesDomainContractTest・MegaGeometryResourcesTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（4/4 passed）、`-3-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-4-capture.txt`（既定の撮影 pass）、`-5-capture-off-compare.txt`（全常駐との PSNR）。`-6`・`-7` は狭い予算の撮影（完走せず、上記の記録用）。numstat は `git diff` と `--ignore-cr-at-eol` で一致。
- Notes: (1) 狭い予算の撮影は落ち着かないとき長くかかるので、Game.exe を止めて切り上げた。 (2) 次は TASKS.md の先頭の未完。

## 反復 4（run 20261005-154938）: VTG5-PAGE-STREAMER 差し戻し対応（done）
- 評価者の指摘 2 件。(1) 要求の発生フレームが、取り込んだフレームで上書きされ、複数フレーム分をまとめた集合で新旧が同順位になっていた。対応: 取り込みのとき、集合の中で最も新しい要求を今のストリーマのフレームとして、古い要求は同じだけ前のフレームへ写す（要求のリングの数え方とストリーマの Update の番号は別の時計なので、差だけを使う）。新しく作った記録は作成時のフレームでなく要求のフレームを持つ。優先度・LRU の両方がこれを使う。
- (2) 最初の 1 ページが読み・コピーの上限を超えて通っていた。対応: 累積が 0 でも上限を適用する。1 ページだけで上限を超えるページは、いつまでも通せないので `RejectOversizedLocked` で諦め（ログ・`OversizedPages`・永久失敗）、次のページへ進む。読みの量は、返すデータ（`DataBytes`）と別にファイルから読む範囲（`ReadBytes`。ヘッダ・クラスタを含む `page.Size`）で数える。`IGeometryPageSource::GetPageReadBytes`（既定 0）を足し、`CookedMeshPageSource` が答え、`GetPageDescriptor` が使う。
- 試験: `GeometryPageStreamerTest` に `TestRequestFrameKeepsRecencyAndLru`（発生フレームの違う要求の優先度・LRU）と `TestFirstPageObeysByteLimits`（100 バイトのページに上限 50 の読み・コピーで通さず諦めること、読みの量を範囲読みの大きさで数えること）を追加。
- 検証: `verify-VTG5-PAGE-STREAMER-8-build.txt`・`-9-ctest.txt` は新試験の失敗（作成時のフレームで要求のフレームが潰れていた）の記録で、直した後が `-10-build.txt`（Debug の Game・RenderResourcesDomainContractTest・MegaGeometryResourcesTest、BUILD_EXIT_CODE=0）、`-11-ctest.txt`（4/4 passed）、`-12-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-13-capture.txt`（既定の撮影 pass）、`-14-capture-off-compare.txt`（全常駐との PSNR: default 100 / near 60.72 / low 95.6 dB、いずれも 45 dB 以上）。numstat は `git diff` と `--ignore-cr-at-eol` で一致。
- Notes: 既定の上限（4 MB）に対し実際のページは小さいので、既定の撮影・常駐の数は変わらない（平均輝度も同じ）。

## 反復 5（run 20261005-154938）: VTG5-PAGE-TOUCH（done）
- 実装: 描いたクラスタ（2パス目で見えたもの・従来の経路で描いたもの）の自分のページを、`MegaGeometryCull.glsl` の `RequestPage` で要求の列へ積む（使用の印）。同じフレームの同じページは `requestStamp` で 1 件にまとまる。未常駐の子の要求と同じ列・同じ表の位置を使い、ホストが自分の記録で区別する（シェーダーの束縛・バッファの並びは変えていない）。ストリーマは、常駐の記録へ届いた要求で `LastRequestedFrame` を進めるので、LRU が読み込んだ順でなく使われた順になる。根のページの使用の印は黙って捨てる（`InvalidRequests` に数えない）。
- 追い出しの規則: 読みたいページの要求のフレーム「以降」に使われたページは外さない（`EvictOneLocked` の比較を `>=` へ）。同じフレームに使われているページを外すと、使っているページを外して読み直す入れ替わりが続くため。作業集合が目標を超えても、外せるページが無ければ予算で見送り（`BudgetBlockedFrames`）、`HasPendingWork` は落ち着いたとみなす。
- 時計の統一（VTG5-PAGE-STREAMER の差し戻しの指摘「別々の Update で取り込んだ要求の新旧が崩れる」への対処。使用の印で LRU を決めるうえで同じ問題だった）: ストリーマの `Update(frame)` を、要求の `LastRequestedFrame` と同じ時計（要求のリングのフレーム。`RenderResources::UpdateGeometryPageStreaming` が `GeometryPageFeedback->GetFrameCounter()` を渡す）にし、取り込みのときの時計の写しを廃止した。要求の新旧は取り込んだ Update に依らない。
- 試験（`GeometryPageStreamerTest`）: `TestUsedPagesAreNotEvicted`（使われたページは読み込んだ順で最も古くても残る／毎フレーム使われる 3 ページと足りない 1 ページの 40 フレームで追い出しが 0 で落ち着く／使われなくなったページだけが外れる）、`TestRequestFrameOrderAcrossUpdates`（評価者の再現手順どおり、複数の Update をまたぐ優先度 3→2→1 と LRU 3→1→2）。`Requests` の補助は Update のフレームを渡す形にした。
- 撮影（`-Deterministic`、RelWithDebInfo、`--vram-budget-mb 900` = geometry_target_mb 54）: 3 視点とも完了（`HasPendingPageStreaming` が false に戻る）。`geometry_evicted_pages` は default 1・near 130（目標が 62 → 54 へ下がった一度の分）・low 0 で、以後は増えない（前回は増え続けて完走しなかった）。near の平均輝度は 123.723（追い出しで細かい段が一部粗くなるため、全常駐の 123.952 より少し低い）。既定の枠の撮影は、前回のストリーマの撮影との比較で default 100 dB・near 100 dB（1 画素・最大差 1）・low 100 dB（`verify-VTG5-PAGE-TOUCH-5-...`）。
- 検証: `verify-VTG5-PAGE-TOUCH-1-build.txt`（Debug の Game・2 テスト、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（2/2 passed）、`-3-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-4-capture.txt`（900 MB の撮影 pass）、`-5-capture-default-compare.txt`。numstat は `git diff` と `--ignore-cr-at-eol` で一致。
- Notes: (1) 使用の印を返すカリングの時間は測っていない（GPU の計測は重い処理）。足した処理は、描いたクラスタごとの `atomicExchange` 1 回と、重複しなければ列への 1 書き込みで、要求の列は 4096 件に対し常駐ページ数は数百。stop-when（カリングの時間が予算を超える）に該当する兆候は無い。 (2) 評価者の差し戻しで blocked になっている VTG5-PAGE-STREAMER の指摘（要求の新旧）は、この反復の時計の統一と試験で解消した。人が確かめて `status:` を `todo` に戻せる。 (3) ストリーミングしないメッシュ（ページを複数持つが全て常駐）の使用の印は `InvalidRequests` に数えられる（統計のみ）。

## 反復 6（run 20261005-154938）: VTG5-PAGE-TOUCH 差し戻し対応（done）
- 評価者の指摘（反復 5）: 使用の印の出す条件が `bVisible` だけで、1パス目で描いたクラスタが2パス目で遮蔽と判定されると、実際には描かれているのに使用の印が出なかった。
- 修正（`MegaGeometryCull.glsl` の 2 パス目）: 子のページの要求は従来どおり `bVisible` のときだけ。自分のページの使用の印は `bDrawnInFirstPass || bVisible` のときに出す。1パス目で描かなかった遮蔽クラスタは出さない。
- 試験: `GeometryPageRequestVulkanTest`（GPU の実カリング）の期待が、前の反復の使用の印の追加で古くなって落ちていた（描いたクラスタの自分のページが要求に載る）ので、期待（要求の件数・集合、CPU の写し `CpuExpectation`）を使用の印を含む形に直した。2 パス目の 4 ケース（遮蔽×1パス目で描画済み／未描画、可視×同）を追加。旧条件へ戻すと「遮蔽・描画済み」だけが NG になることを確かめた。
- 検証: `verify-VTG5-PAGE-TOUCH-6-build.txt`（Debug の Game・3 テスト・RHITextureUpdateVulkanTest、BUILD_EXIT_CODE=0）、`-7-ctest.txt`（GeometryPageStreamerTest・MegaGeometryResourcesTest・GeometryPageRequestVulkanTest の 3/3 passed）、`-8-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-9-capture.txt`（900 MB の撮影 pass。evicted は default 1・near 130・low 0 で収束、平均輝度は 124.173 / 123.723 / 126.022 で前回と同じ）。TASKS.md の更新（コミット `6d852644`）は行末を全行 LF にして numstat が不一致だったのに、ここで「一致」と記録していた（誤り）。`bf253ddf` で CRLF へ戻した。
- Notes: カリング時間は測っていない（足した処理は条件の変更のみで、`atomicExchange` の回数は 1 パス目で描いた遮蔽クラスタの分だけ増える）。

## 反復 1（run 20261005-175023）: VTG5-PAGE-STREAMER（done）
- 差し戻し（別々の Update で取り込んだ要求の新旧が失われる）は、VTG5-PAGE-TOUCH の時計の統一（`Update(frame)` を要求の `LastRequestedFrame` と同じ時計にし、取り込み時の写しを廃止）で解消済み。コードの追加は無し。評価者の再現手順どおりの `TestRequestFrameOrderAcrossUpdates`（優先度 3→2→1、LRU 3→1→2）が現行コードで通ることを確かめた。
- 検証: `verify-VTG5-PAGE-STREAMER-1-build.txt`（Debug の Game・RenderResourcesDomainContractTest・MegaGeometryResourcesTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（4/4 passed）、`-3-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-4-capture.txt`（既定の撮影 pass）、`-5-capture-off-compare.txt`（全常駐 `-GeometryStreaming Off` との PSNR: default 100 / near 60.733 / low 96.527 dB、いずれも 45 dB 以上。平均輝度は 124.173 / 123.952 / 126.022 で一致）。
- Notes: near の 60.7 dB は移行前から記録している TAA・RTGI の揺れの水準（最大差 18）。次は TASKS.md の先頭の未完。
## 反復 2（run 20261005-175023）: VTG5-PAGE-TOUCH 差し戻し対応（done）
- 評価者の指摘（反復 6）: BVH の経路（`cluster_bvh_cull.comp`）で、2パス目に節ごと遮蔽されると枝を打ち切るため、1パス目で描いたクラスタが2パス目の `ProcessCluster` に届かず、自分のページの使用の印が出なかった（平坦の経路は前の反復で直っていた）。
- 修正（`MegaGeometryCull.glsl`）: 1パス目で描画のコマンドを積む時点で、自分のページの使用の印（`RequestPage(ownPage)`）を出す。子のページの要求は従来どおり2パス目の `bVisible` の条件のまま。2パス目の使用の印（`bDrawnInFirstPass || bVisible`）は残し、同じフレームの印で重複は省かれる。
- パスの間の同期（`MegaGeometryPass.cpp`）: 1パス目が書くようになったので、2パス目のカリングの前に、ページの表（要求の印）と要求の列へ UAV のバリアを入れた（印の交換による重複の省略と、列の件数の加算がパスをまたぐため）。要求の列の容量が 0 のときは入れない。BVH の段の間・平らな判定から BVH への間には前からバリアがある。
- 回帰試験（`GeometryPageRequestVulkanTest`）: `cluster_bvh_cull.comp` を実際に動かすケースを 4 つ足した。節 4 つ（根の内部の節 1 つ・家族ごとの葉 3 つ）の BVH で、1パス目 → 2パス目の順に、書き込み先を持ち越して実行する（節の判定の 2 段 + 葉のクラスタの判定を各パスで）。「2パス目で節ごと遮蔽（1パス目で描画済み）」が、描画 12・要求 2 件（ページ 2・3）になる。同じ条件で旧シェーダー（1パス目の使用の印なし）に戻すと、このケースだけが要求 0 件で NG になることを確かめた（他の 3 ケースは通る）。これまでの BVH のシェーダーはコンパイルの確認だけだった。`RunCase` は初期化・実行・読み戻しに分けた（平らな判定の既存ケースの内容は変えていない）。
- 検証: `verify-VTG5-PAGE-TOUCH-1-build.txt`（Debug の Game・RenderResourcesDomainContractTest・MegaGeometryResourcesTest・RHITextureUpdateVulkanTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（GeometryPageStreamerTest・MegaGeometryResourcesTest・GeometryPageRequestVulkanTest が 3/3 passed）、`-3-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-4-capture.txt`（900 MB の撮影 pass。平均輝度 124.173 / 123.723 / 126.022）、`-5-metrics.txt`。ログの `geometry_evicted_pages` は default 1・near 130・low 0 で、以後増えない（`HasPendingPageStreaming` が false に戻って撮影が完了）。near.png を開いて欠け・穴が無いことを確認した。
- 行末: `Assets/Shaders/Common/MegaGeometryCull.glsl`・`Test/.../GeometryPageRequestVulkanTest.cpp`・`TASKS.md` は CRLF のまま。`MegaGeometryPass.cpp` は元から LF 主体（CRLF が 26 行混在）で、編集ツールが混在を LF に直したので元の行末へ戻した。コミット前の numstat は `git diff` と `--ignore-cr-at-eol` で一致させる。
- Notes: カリング時間は測っていない（足した処理は、描いたクラスタごとの `atomicExchange` 1 回と重複しなければ列への 1 書き込みで、2パス目が出すものと同じ列・同じ印を使う。stop-when に該当する兆候は無い）。

## 反復 3（run 20261005-175023）: VTG5-STREAM-HANG（done）
- 結論: ハングでも異常終了でもなかった。再現（`--stress-geometry=300 --vram-budget-mb=1100 --startup-camera=20,-8,8 --orbit-degrees-per-second=20 --capture-deterministic`、RelWithDebInfo）では、プロセスは生存（CPU 時間は進み、スレッドは待ちか実行中）、WER の Event 1000 は無し、`Game.log` は10MBごとに `Game.1〜5.log` へローテーションして書き続けていた。「起動から約32秒でログが止まる」は、ローテーションの境目（10MiB 付近）で `Game.log` を写した断面の誤読。
- 本当の症状: 撮影が永遠に終わらない。ジオメトリの枠は `geometry_target_mb=71`（`VRAM_POOLS`）で、低い視点の作業集合がそれを超え、`geometry_evicted_pages` が毎秒7〜8ページずつ増え続ける入れ替わりが続く。`GeometryPageStreamer::HasPendingWork()` が、読み込み中のページが出るたびに true になり（`RenderWorld::HasPendingAsyncAssets` → 撮影の設定待ち）、false→true の変わり目ごとに決定的な撮影のエポックが約60フレームごとに巻き戻る（ログの `asset settle baseline` / `capture_deterministic epoch begin` が際限なく繰り返し、2分でレンダリング14000フレームを超えても `capture-sequence` の60フレームに届かない）。
- 直し方: `GeometryPageStreamer` に、入れ替わりの継続の判定を足した（`IsChurningLocked`）。追い出しの間隔が `ChurnGapFrames`（120 Update）以内で続き、その続きが `ChurnRunFrames`（300 Update）以上になったら、`HasPendingWork()` は false を返す（読み込み自体は止めない）。追い出しが `ChurnGapFrames` 止まれば数え直す。落ち着くのを待っても落ち着かない状態を、待ちとして数えないための判定で、既存の `StalledUploadFrames`・目標超過の判定と同じ考え方。
- 検証: 同じ条件の撮影を3回続けて完走（各20秒、終了コード0、6枚 PNG とも保存、ログが `capture_png saved` まで続く: `verify-VTG5-STREAM-HANG-4-capture3.txt`）。撮影スクリプト（`-StressGeometry -ViewNames low -VramBudgetMb 1100 -OrbitDegreesPerSecond 20 -Deterministic`）も `result=pass`（`verify-VTG5-STREAM-HANG-5-script-capture.txt`）。修正前は同条件で2分待っても設定待ちが終わらなかった。
- 回帰のテスト: `GeometryPageStreamerTest` の `TestChurnIsNotPendingWork`（CPU の偽物のバックエンド。目標3ページに対し視点が動いて追い出しが続くと、始まりは待ちに数え、`ChurnRunFrames` 後は読み込み中でも数えず、追い出しが止まれば数え直す）。判定を無効化すると落ちることを確かめた。
- 既知の限界: 入れ替わりの最中に撮るので、ページの常駐が撮影のフレームごとに変わりうる（画素の再現性は未確認）。追い出しが長く続きながら最終的に収束する場合（300 Update を超える収束）も、収束前に待ちを打ち切る。いずれも目標が作業集合に足りない負荷の撮影に限る。
- 補足: `Game.log` に `neural_material_decode.slang`（Slang SDK なし）のエラーが2行出るが、以前の撮影のログにもあり、この件とは無関係。
- 検証: `verify-VTG5-STREAM-HANG-1-build.txt`（RelWithDebInfo の Game）・`-2-build-debug.txt`・`-3-ctest.txt`（3本とも合格）・`-4-capture3.txt`・`-5-script-capture.txt`
- 触ったもの: `Library/Core/Private/Rendering/GeometryPageStreamer.h`・`Test/Core/Rendering/GeometryPageStreamerTest.cpp`・`TASKS.md`・`PROGRESS.md`
- Next: VTG5-STRESS-GEOMETRY（撮影の続き。1100 MB の low の旋回も完走する）。

## 反復 4（run 20261005-175023）: VTG5-STRESS-GEOMETRY（done）
- 実装は前の反復の `9abc9a06`（`--stress-geometry=<個数>` の配置、`CaptureStartupScene.ps1 -StressGeometry`）から変えていない。この反復は、ストリーマの直し（VTG5-PAGE-TOUCH・VTG5-STREAM-HANG）の後の現行コードで、done-when の撮影・計測をやり直して記録した。300 個（岩・小屋・大きな球・スキャン資産）が地面の外側へ並ぶ（`STRESS_GEOMETRY_PLACED`）。
- 全常駐（`-GeometryStreaming Off`）のジオメトリの量は `geometry_used_mb`=274 MB。`--vram-budget-mb 1100` の目標は `geometry_target_mb`=71 MB で、全常駐の約 3.9 倍（2 倍以上）。
- 1100 MB（`-Deterministic`、RelWithDebInfo、3 視点＋旋回 20 度/秒の 60/75/90 フレーム）:
  | 撮影 | geometry_used_mb | target | evicted_pages | 常駐ページ |
  |---|---|---|---|---|
  | default / low / top（静止） | 54 / 67 / 32 | 71 | 0 / 0 / 0 | 319 / 436 / 118 |
  | default 旋回 | 54 | 71 | 0 | 326 |
  | low 旋回 | 71 | 71 | 33 | 465 |
  | top 旋回 | 32 | 71 | 0 | 122 |
  どれも `geometry_used_mb` ≦ 目標。追い出し（`geometry_evicted_pages>0`）は低い視点の旋回で起き（最後の予算の照会時点で 33 ページ、最終のページ統計で 36 ページ。撮影の終わりも続き、収束は未確認）、失敗ページは 0、撮影は完走（`-VramBudgetMb 1100 -OrbitDegreesPerSecond 20` の low 旋回は前の反復までは終わらなかった）。静止画の視点は作業集合が目標に収まるので追い出しは起きない。
- 画の確認: 1100 MB の `low-orbit-f75.png`（低い視点。岩・球が地面すれすれに並ぶ）・`top-orbit-f75.png`（上から。300 個の格子全体）、6500 MB の `default.png`（斜め。小屋・岩・球）を開いて、穴・割れ目・欠けが無いことを確認した。全常駐との画素比較（PSNR、いずれも 45 dB 以上）: 静止 default 67.2 / low 82.9 / top 69.1 dB（最大差 20 / 4 / 18）、旋回 9 枚は 63.5〜66.1 dB（最大差 10〜24）で、撮影した 9 枚では欠けを認めず、差は TAA・RTGI の揺れの水準（連続フレームのちらつきは未確認）。
- 8GB 級（`--vram-budget-mb 6500`、`geometry_target_mb`=1421）: 3 視点とも溢れず（used 54 / 67 / 32、evicted 0、失敗 0）、全常駐と平均輝度が同じ（121.845 / 123.211 / 125.014）。
- GPU 時間（`-GpuTimingFrames 300`、RelWithDebInfo、中央値 ms）。`MegaGeometryPass` は 起動画面の既定 0.305（default）/ 0.267（low）に対し、300 個で 0.844（default）/ 0.744（low）/ 0.952（top）。フレーム全体の GPU 時間: 起動画面 2.771 / 2.603、1100 MB 3.672 / 3.415 / 2.849（top）、6500 MB 3.608 / 3.312 / 2.834。CPU 中央値は 10.6〜10.7 ms で、300 個でも 16.6 ms の予算内（over_budget 0）。stop-when（カリングが 16.6 ms を超える）に該当しない。
- 検証: `verify-VTG5-STRESS-GEOMETRY-1-build.txt`（RelWithDebInfo の Game、BUILD_EXIT_CODE=0）、`-2-capture.txt`（既定の起動画面の撮影 pass）、`-3-capture-1100.txt`・`-4-capture-resident.txt`・`-5-capture-6500.txt`・`-6-capture-orbit-1100.txt`・`-7-capture-orbit-resident.txt`（負荷モードの撮影、すべて pass・終了コード 0）、`-8-gpu-startup.txt`・`-9-gpu-stress-1100.txt`・`-10-gpu-stress-6500.txt`（GPU 時間）。画像は `.harness/runs/startup-capture/VTG5-STRESS-GEOMETRY-b0|b1100|b6500|b0-orbit|b1100-orbit|gpu-*`。TASKS.md は CRLF のまま、numstat は `git diff` と `--ignore-cr-at-eol` で一致。
- Notes: (1) 静止画の 1100 MB では追い出しが起きない（作業集合 ≦ 目標）。追い出しの確認は low 旋回で行った。 (2) 起動画面の既定の撮影（`-Deterministic`）はこの変更で変わらない（平均輝度 124.173 / 123.952 / 126.022）。

## 反復 5（run 20261005-175023）: VTG5-ACCEPT（done）
- 内容: `Docs/RenderingValidation/VirtualizationAcceptance.md` に段5の節を足した（結果の一覧・全常駐との PSNR・起動画面の所見・ジオメトリの量・負荷モード・GPU 時間と CPU の記録の時間・判定・既知の限界）。コードは変えていない。
- 判定: 受入れ（予算の上限で負荷モードが溢れずに描ける）を満たす。6500 MB（目標 1421 MB）・1100 MB（目標 71 MB = 全常駐の約 1/3.9）のどちらも `geometry_used_mb` ≦ 目標、失敗ページ 0、完走。1100 MB の低い視点の旋回で追い出し（最後の予算の照会時点で 33 ページ、最終のページ統計で 36 ページ。撮影の最後まで続き、収束は未確認）。
- 起動画面（`-Deterministic`、朝・昼・夕・夜 × 3視点）: 12視点とも pass、白飛び・黒つぶれ 0、平均輝度は段4と同じ。全常駐（`-GeometryStreaming Off`）との PSNR は 12視点とも 45 dB 以上（既定・低角度は大半が 100 dB、近接は 61.286〜78.361 dB）。ジオメトリの量は全常駐 274.6 MB（2 塊 512 MB）に対し、ストリーミングあり 54.3（既定）/ 68.9（近接）/ 39.5（低角度）MB（1 塊 256 MB）、追い出し 0、失敗 0。
- GPU 時間・CPU の記録の時間は前の反復（VTG5-STRESS-GEOMETRY）の測定を表にした（その後ストリーマ・シェーダー・Game のコードは変わっていない）。300 個で `MegaGeometryPass` 0.73〜0.95 ms、CPU の記録 0.15〜0.16 ms、フレームの CPU 10.6〜10.7 ms（予算 16.6 ms 内）。
- 検証: `verify-VTG5-ACCEPT-1-build.txt`（Debug の関係ターゲット、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（9/9 passed、golden の Indoor・Outdoor 含む）、`-3-build-rwdi.txt`（RelWithDebInfo の Game 0）、`-4-capture-day.txt`・`-5-capture-night.txt`（撮影 pass）、`-6-capture-off-day.txt`・`-7-capture-off-night.txt`（全常駐との PSNR、いずれも pass）。PNG は default-sun45・near-sun45・default-night と、負荷モードの low-orbit-f75（1100 MB）・top（6500 MB）を開いて穴・欠けが無いことを確認した。golden の基準画像・閾値の変更は無い。
- 既知の限界: 近接の PSNR の揺れとストリーミングの影響の切り分け、全常駐との GPU 時間の比較、追い出し中の画素の再現性は未確認（節に記載）。
- Notes: 次は TASKS.md の未完（VTG6 は backlog）。この段の後、親が main へマージしてプッシュする。
- 差し戻し対応（反復 6）: 評価者の指摘に沿って受入れの記述を証拠に合わせた。(1) 1100 MB の低い視点の旋回の追い出しは「33 ページで落ち着く」ではなく、最後の予算の照会時点で 33・最終のページ統計（`GEOMETRY_PAGES`）で 36 で、撮影の終わりも `uploading=2 reading=1` のまま続いており、収束は未確認と訂正（元ログ `low-orbit.Game.log` の 1863・1876 行）。(2) 「連続フレームのちらつきは無い」は、撮影した 9 枚（60・75・90 フレーム）では欠けを認めない、連続フレームのちらつきは未確認に限定。(3) 平均輝度差の「下限 0.1」を「上限 0.1」に直した。コード・測定は変えていない。保存済みの verify-VTG5-ACCEPT-1〜7 を開き直し、ビルド 0・ctest 9/9・撮影 4 本 pass を確認した。

## 反復 2（run 20261005-191130）: VTG6-RHI-INT-FORMATS（done）
- 内容: `RHI::Format` に `R32_UINT`・`R32G32_UINT` を足し、Vulkan の形式対応表（`VulkanDevice.cpp`・`VulkanTexture.cpp`）とバイト数（`IGPUResourceAllocator.h`）を対応させた。整数の添付は `AttachmentDesc::clearColorUint`（uint32）で消す（`IsUnsignedIntegerFormat`、`VulkanCommandList.cpp` が `clearValue.color.uint32` へ書く）。カラー添付・storage image の用途は既存の `ResourceUsage::RenderTarget`・`UnorderedAccess` がそのまま使える。
- 機能: `geometryShader`（フラグメントシェーダーの `gl_PrimitiveID` に要る。ジオメトリシェーダー自体は使わない）と `shaderStorageImageExtendedFormats`（RG16F などの storage image）を、対応するデバイスだけで有効化し、`DeviceCapabilities::bGeometryShader`・`bShaderStorageImageExtendedFormats` に載せた。開発機は両方とも 1。デバイス作成の失敗は無く、stop-when に該当しない。
- テスト: `IntegerAttachmentVulkanTest`（`RHITextureUpdateVulkanTest` の束の MEMBER。24x8 の添付に 1 回 6 三角形の描画を 2 行ぶん、描画の番号を開始インスタンスで渡す）。R32_UINT は `(描画の番号 << 7) | gl_PrimitiveID`、R32G32_UINT は R に同じ ID・G に描画の番号を書き、12 個の三角形すべてが期待の値、覆われない画素が整数のクリア値（0xDEADBEEF / 0xCAFEF00D）のまま、validation error 0 件を確かめる。`geometryShader` が無い環境は理由を出して 125。
- 検証: `verify-VTG6-RHI-INT-FORMATS-3.txt`（Debug の Game・RHITextureUpdateVulkanTest、BUILD_EXIT_CODE=0）、`-4.txt`（ctest 3/3 passed、IntegerAttachmentVulkanTest・RHIBlockCompressedTextureVulkanTest・SparseCapabilitiesVulkanTest）、`-5-direct.txt`（本体を直接実行: geometryShader=1、確認した三角形=12 を 2 形式、VUID_COUNT=0、RESULT=PASS）。`-1`・`-2` は失敗した試行（`-1` はビルド中の打ち切り、`-2` は Git Bash が `/m:1` を変換して MSBuild が拒否）。
- Notes: (1) テストの `DeviceCapabilities` は Windows の `DeviceCapabilitiesA` マクロと衝突するため `const auto&` で受けた。 (2) ビルドは PowerShell で実行する（Git Bash は `/m:1` を壊す）。 (3) 整数の添付のブレンドは無効にする必要がある（テストで `blendEnable=false`）。パイプライン作成側で整数形式のブレンドを強制的に切る処理は足していない（呼び出し側の責任。VTG6 の本体で使うときに確かめる）。
- Next: TASKS.md の次の未完（VTG6 の続き）。

## 反復 3（run 20261005-191130）: VTG6-RASTER-CHUNKS（blocked。実装はコミット済み）
- 内容: 三角形リストを128三角形以下の塊に分ける `Rendering/MeshIndexChunks.h`（`BuildMeshIndexChunks`。サブメッシュの境目で区切り、3で割り切れない余りは覆わない）を足し、手続きメッシュ（`ProceduralMeshGPUData::Chunks`。登録時にサブメッシュの始まり・終わりで区切る）とスキニング（`SkinnedMeshGpuStore::Entry::Chunks`。`SkinnedMeshResources::TryGetChunks`）が登録時に塊を持つようにした。手続きメッシュの頂点・インデックス、スキニングのインデックス（と頂点の BDA）のバッファに、storage・ShaderRead・BufferDeviceAddress の用途を足した（BDA が使えないデバイスでは RHI 側が無視する）。RT の経路はコピーから直接の利用に変わった（手続きメッシュのバッファが BDA を持ったので、`RenderingCoordinator` の `CreateAddressableMeshBuffer` がコピーを作らず元のバッファを返し、RT の BLAS の入力と RTGI・DDGI のインスタンスのアドレスが元のバッファを直接読む。撮影の画素比較で出力は一致）。
- テスト: `MeshResourcesProceduralGpuTest` に、三角形数 1・128・129・300・余り付き・サブメッシュ3つのケース（全三角形をちょうど1回、各塊128以下、境目をまたがない）と、バッファの用途の検査を足した（既存の「用途が VertexBuffer だけ」の assert は新しい契約に合わせて直した）。`SkinnedRenderPathContractTest` に `TestRegisteredMeshHasChunksAndComputeReadableBuffers`（同じ三角形数、バッファの用途、未登録は false）を足した。
- 検証: `verify-VTG6-RASTER-CHUNKS-3.txt`（Debug の Game・RenderResourcesDomainContractTest・SkinnedRenderPathContractTest、BUILD_EXIT_CODE=0。-1・-2 は constexpr と文字列の誤りで失敗した試行）、`-9-ctest-ok.txt`（MeshResourcesProceduralGpuTest・GeometryPoolAllocatorTest が 2/2 passed）、`-8-skinned-direct.txt`（スキニングの追加ケースが通った出力）。
- 止まった理由: verify の ctest の `SkinnedRenderPathContractTest` は、このタスクと無関係な既知の失敗（TEST-SKINNED。`TestInitializedPassesExecuteThroughFrameCommandsAndSceneRenderer` が `gBuffer.Initialize` で落ち、assert の後にプロセスが終了しない）で通らない。詳細・選択肢は `blocked/VTG6-RASTER-CHUNKS.md`。`TASKS.md` の status は変えていない。
- Notes: (1) このツール環境では python のヒアドキュメント内の `\n` が実際の改行になるので、文字列リテラルは `chr(92)` で作る。 (2) `ResourceUsage` の `operator|` は constexpr ではないので `const` で持つ。
- Next: TEST-SKINNED を直したあと、VTG6-RASTER-CHUNKS の status を `todo` に戻すと、ctest の確認だけで完了できる。

## 反復 4（run 20261005-191130）: VTG6-COMPUTE-SKINNING（blocked。実装はコミット済み）
- 内容: スキニングの頂点を計算シェーダーで変形し、今・前のフレームの頂点（位置・法線・UV。ワールド空間）をフレームごとのバッファへ書くパスを足した。`Assets/Shaders/skinning_compute.comp`（1 スレッド 1 頂点。今は `skinned_gbuffer.vert` の SkinVertex と同じ手順、前は直前のパレットの位置の行列で位置を、上 3x3 の逆転置で法線を変形）、`SkinningCompute`（パイプライン、飛行中のフレーム 2 枠の UBO・ディスクリプタセット、`Record` で 1 インスタンスぶんを記録）、`SkinningComputePass`（`IViewPass` + `IRenderGraphPass`）。
- RenderGraph: `Declare` が不透明描画のスキニングの 1 描画（非インスタンス）を集め、全インスタンスの頂点を詰めた 2 本のバッファ `Skinning.CurrentVertices`・`Skinning.PreviousVertices`（storage・ShaderRead・BDA。1 頂点 32 バイト。書き込み UnorderedAccess → 最終 GenericRead）を `WriteBuffer` で宣言する。`Execute` は GBuffer と同じく `SkinnedMeshResources::PrepareDraw`（前のパレット付き）で得た palette を使い、`MarkLastUse` で提出番号を結んでから dispatch する。`GetInstances()` が、インスタンスごとの頂点の範囲・BDA アドレス・インデックスのバッファを返す（ビジビリティバッファのラスタと材質の解決が読む）。
- 既定は無効: `SceneView::SetupDeferredPipeline` で GBufferPass の前に追加し `SetEnabled(false)`。今の GBuffer の経路は変えていない（頂点シェーダーのスキニングのまま、起動画面は変わらない）。VTG6-VIS-RASTER が `--visibility-buffer=on` のときに有効にする。有効にすると GBuffer の PrepareDraw とは別に palette を作る（インスタンスごとに 2 回のアップロード）。
- テスト: `ComputeSkinningVulkanTest`（`RHITextureUpdateVulkanTest` の束）。実際の `SkinnedMeshGpuStore::PrepareDraw` が作る palette を使い、A（150 頂点・骨 3、ワークグループ 64 を跨ぐ。骨 1・2・4 本、重みの合計 0.8、重み 0、範囲外の骨、乱数）と B（9 頂点・骨 2）を 1 本の出力の別の範囲へ書き、今・前の頂点（159 頂点 × 2）が CPU の独立した参照（行ベクトル規約を float 配列で組む）と許容 1e-4 で一致（最大誤差 9.5e-7）、範囲外へ書かない、収まらない範囲・頂点 0 は記録しない、validation error 0 件。前のフレームの片方を `current.world` に壊すとテストが NG になることを確かめた。
- 実エンジンでの確認: 一時的に有効にして `RenderingVelocitySkinnedVulkanTest` を走らせると、`Declare` が plan=1、`Execute` が 1 インスタンス（4 頂点）を記録し、BDA アドレスも取れ、テストは通った（`verify-VTG6-COMPUTE-SKINNING-4-temp-enabled-build.txt`・`-5-temp-enabled-run.txt`。一時の変更は戻した）。
- 検証: `verify-VTG6-COMPUTE-SKINNING-1.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderingVelocityVulkanTest・SkinnedRenderPathContractTest、BUILD_EXIT_CODE=0）、`-2-direct.txt`（ComputeSkinningVulkanTest 直接実行: RESULT=PASS、VUID_COUNT=0）、`-3-ctest.txt`（ComputeSkinningVulkanTest・RenderingVelocitySkinnedVulkanTest は passed、SkinnedRenderPathContractTest は既知の失敗で Timeout）、`-6-final-build.txt`・`-7-final-ctest.txt`（最終のソースで再ビルド。ComputeSkinningVulkanTest・RenderingVelocitySkinnedVulkanTest・RenderGraphCompileTest・IntegerAttachmentVulkanTest・GeometryPageRequestVulkanTest が 5/5 passed）。
- 止まった理由: verify の ctest に含まれる `SkinnedRenderPathContractTest` が、TEST-SKINNED（`gBuffer.Initialize` の assert、プロセスが終了せず ctest が待つ）の既知の失敗で通らない。詳細・選択肢は `blocked/VTG6-COMPUTE-SKINNING.md`。VTG6-RASTER-CHUNKS と同じ原因。`TASKS.md` の VTG6-COMPUTE-SKINNING の status は変えていない。
- Notes: (1) ビルドは PowerShell で実行する（Git Bash は `/m:1` を壊す）。ログの INFO は出力されない設定なので、確認用の一時ログは WARNING にする。 (2) 前のフレームの法線は、直前のパレットが位置の行列だけを持つので、シェーダーで上 3x3 の逆転置を求める（`NormalMatrixOf`。特異に近いときは単位行列で `MatrixUtils::CreateNormalMatrix` と同じ）。 (3) 出力は 1 頂点 32 バイトの詰めた float の列（位置 3・法線 3・UV 2）で、今・前とも同じ並び。ワールド空間なので、後のパスは変換を掛け直さない。 (4) 枠の 2 は MegaGeometryPass と同じ前提（同じ枠を再び使うのは 2 フレーム後で GPU の仕事は終わっている）。
- Next: TEST-SKINNED を直したあと、VTG6-COMPUTE-SKINNING と VTG6-RASTER-CHUNKS の status を `todo` に戻すと、ctest の確認だけで両方完了できる。それまでは VTG6-VISBUFFER-RESOURCES 以降を進められる（この 2 件の実装は入っている）。

## 反復 5（run 20261005-191130）: VTG6-VISBUFFER-RESOURCES（done）
- 内容: `Rendering/VisibilityBuffer.h`（`VisibilityBuffer` 名前空間）と `Assets/Shaders/Common/VisibilityBuffer.glsl` を足した。ID は `(記録の番号 << 7) | 三角形の番号`（`TryEncode`・`TryDecode`）、0 は空。表の 0 番は空の記録として予約し、実際の記録の番号は 1 から（枠の上限 2^25 = 33554432、使える記録は 2^25 - 1）。
- 記録 `DrawRecord`（64 バイト、GLSL の `VisibilityDrawRecord` は uvec4 4 つ）: 種類（MegaGeometryCluster・ProceduralChunk・SkinnedChunk）、インスタンスの番号、材質の番号、三角形数（1〜128）、最初のインデックス、頂点の基点、前のフレームの変換の番号、フラグ（16bit インデックス）、頂点・インデックスのアドレス、前のフレームの頂点のアドレス（スキニング用）。三角形 t の頂点 k は `インデックス[FirstIndex + 3t + k] + VertexBase`。
- 表 `RecordTable`: `Add`（使えない記録・容量超過は 0 を返して数える）・`AddAndEncode`・`TryResolve`・`Clear`、容量を小さくして負荷を測れる。RenderGraph 用に `MakeIdTextureDesc`（R32_UINT のカラー添付）・`MakeRecordTableBufferDesc`（storage buffer）と、名前 `RenderGraphResourceNames::VisBufferId`・`VisBufferDrawRecords` を足した。パスはまだ無い（VTG6-VIS-RASTER が宣言する）。
- テスト: `VisibilityBufferEncodingTest`（`RenderResourcesDomainContractTest` の束の MEMBER）が、記録 1..2048 × 三角形 0..127 の往復とビット配置、上限の境目（2^25 - 1 が最後、2^25・0・三角形 128 は不可、最後の ID は 0xFFFFFFFF）、空（0 は復号できない、1..127 は表から引けない）、表の追加・引き・容量・断り、RenderGraph の資源の記述、GLSL の定数（`const uint 名前 = 値u;`）と構造体の uvec4 数が C++ と一致することを確かめる。GLSL の定数を書き換えるとテストが落ちることを確かめた。`RenderGraphCompileTest` に `TestVisibilityBufferResourcesDeclareAndRead`（名前つきの書き込みと読み取りがコンパイルできる）を足した。
- GLSL は glslangValidator で、記録の表の宣言あり（compute）・なし（frag、`gl_PrimitiveID`）の2通りの試し書きがコンパイルできることを確かめた（スクラッチ。リポジトリには入れていない）。
- stop-when の確認: 記録の数の上限 2^25 に対し、MegaGeometry の 1 フレームの描画コマンド上限は `MegaGeometryPassSettings::MaxDrawCount` = 262144（2^18）。手続き・スキニングの塊は 128 三角形ごとなので、負荷モード（300 個）でも 2^25 に届く経路は無い。該当しない。
- 検証: `verify-VTG6-VISBUFFER-RESOURCES-1.txt`（Debug の Game・RenderResourcesDomainContractTest・RenderGraphCompileTest、BUILD_EXIT_CODE=0）、`-2.txt`（ctest 2/2 passed、VisibilityBufferEncodingTest・RenderGraphCompileTest）。
- Notes: (1) `RenderGraphResourceNames.h` は HEAD が行末混在なので、Edit ツールでなく HEAD のバイト列へ挿入して差分を 4 行に保った。 (2) 新規ファイルは BOM+CRLF。 (3) 次の VTG6-VIS-RASTER が、描画ごとに `RecordTable` へ記録を足し、`VisBufferDrawRecords` を書いて `VisBufferId` を宣言する。
- Next: VTG6-VIS-RASTER（TEST-SKINNED で blocked の VTG6-RASTER-CHUNKS・VTG6-COMPUTE-SKINNING の実装は入っている）。

## 反復 6（run 20261005-191130）: VTG6-VISBUFFER-RESOURCES 差し戻し対応（done）
- 指摘: `IsValidRecord` が `FirstIndex % 3 == 0` を要求していたが、MegaGeometry の共有プールの区画の基点（`ComputePageRegionBases`）は 3 の倍数に整列しない。頂点 10・インデックス 30・区画位置 512 バイトで `IndexBase` = (512 + 512) / 4 = 256（256 % 3 = 1）となり、正当なクラスタの記録が断られて `Add` が 0 を返していた。
- 修正: `VisibilityBuffer.h` の `IsValidRecord` から 3 の倍数の条件を外し、関連するコメント 2 か所を直した。三角形 t の頂点 k は `FirstIndex + 3t + k` で引くので、基点が 3 の倍数である必要はない。
- テスト: `VisibilityBufferEncodingTest` の「3 の倍数でない記録は足せない」を「足せる」に直し、`TestRecordFromMegaGeometryPageRegion`（上の配置式から作った記録を `AddAndEncode`・`TryResolve` で往復できる）を足した。断った数の期待は 5 から 4 になった。
- 検証: `verify-VTG6-VISBUFFER-RESOURCES-6.txt`（Debug の Game・RenderResourcesDomainContractTest・RenderGraphCompileTest、BUILD_EXIT_CODE=0）、`-7.txt`（ctest 2/2 passed）。行末は `git diff --numstat` と `--ignore-cr-at-eol` で一致。

## 反復 8（run 20261005-191130）: VTG6-VIS-RASTER（done）
- 内容: `VisibilityRasterPass`（`Rendering/VisibilityRasterPass.h`・`.cpp`）と `VisibilityBufferMode.h`、シェーダー `visbuffer_mega.vert`・`visbuffer_mesh.vert`・`visbuffer_skinned.vert`（位置だけを読む）・`visbuffer.frag`（記録の番号は開始インスタンスから渡す描画ごとの値、三角形は `gl_PrimitiveID`）・`visbuffer_records.comp`（MegaGeometry のコマンドから記録を GPU が書く）を足した。`--visibility-buffer=on` で、MegaGeometry のクラスタ（2パスの遮蔽・BVH・ページの経路が積んだ IndirectDraw をそのまま描き直す）・手続きメッシュの塊・スキニングの塊（SkinningComputePass の変形済みの頂点）を `VisBuffer.Id` と `GBuffer.Depth` へ描く。記録の表は 0 番が空、1 番から MegaGeometry のコマンド、その後ろに手続き・スキニングの塊。GBuffer への書き込みは今の経路のまま（`on` でも GBufferPass・MegaGeometryPass の GBuffer の描画は動く）。
- デバッグ表示: `--visibility-buffer=debug` で `VisibilityDebugPass`（`visbuffer_debug.frag`）が ID を色にして最後のシーンの色へ書く。種類ごとに色相の帯を分け、記録（描画）ごとに色相を散らす。撮影は `Scripts/CaptureStartupScene.ps1 -VisibilityBuffer On|Debug`。
- テスト: `RenderGraphCompileTest` に `TestVisibilityRasterOnRecordsMegaDrawsAndIdPass`・`...OnSinglePassMegaGeometry`・`...OffKeepsExistingMegaGeometryRecording`・`...WithoutGBufferDepthDoesNothing` を足した（`on` の記録の宣言、`off` は既存の記録のまま）。
- 目視確認: デバッグの撮影（`.harness/runs/startup-capture/VTG6-VIS-RASTER-debug/default.png`・`near.png`）を開いて、家・岩（MegaGeometry、橙〜赤で細かい塊に分かれる）・地面（手続き、緑〜シアンの三角形）・球の輪郭が欠けず出て、塊の境が見えることを確かめた。`on` の撮影は `off` と同一（default の PSNR 100、差の画素 4E-06）。
- 検証: `verify-VTG6-VIS-RASTER-15-build.txt`（Debug の Game・RenderGraphCompileTest・MegaGeometryResourcesTest、BUILD_EXIT_CODE=0）、`-16-ctest.txt`（4/4 passed）、`-17-build-rwdi.txt`（RelWithDebInfo の Game、BUILD_EXIT_CODE=0）、`-18-capture.txt`（既定の撮影 pass、平均輝度 124.173 / 123.952 / 126.022 で反復 7 の `off` の撮影と同じ）。
- stop-when: 該当しない。ビジビリティバッファの深度は GBuffer.Depth へ LessEqual で重ね描きするだけで、2パスの遮蔽の HZB は今の経路（GBuffer の1パス目の深度）から作ったまま変えていない。
- Notes: (1) 反復 7 が 150 ターンで止まり、未コミットだった新規 9 ファイルをこの反復で足した。 (2) 起動画面（スキニングを含まない）では `skinned_chunks=0`。スキニングの塊の描画は反復 7 のスキニング検証シーン（velocity の撮影）で実行して動作したが、ID の画素の目視は起動画面にスキニングが無いため行っていない。 (3) 新規ファイルは BOM+CRLF。
- Next: VTG6-MATERIAL-CLASSIFY（`VisBuffer.Id` から 8×8 の画素タイルの材質を分類する）。

## 反復 9（run 20261005-191130）: VTG6-VIS-RASTER 差し戻し対応（blocked。修正はコミット済み）
- 指摘1（スキニングの記録の頂点の基点の二重加算）: 記録の `VertexBase` を0にした（`VertexAddress` が先頭まで加算済み）。描画の `VertexOffset` は変えない。
- 指摘2（HZB をビジビリティの1パス目の深度から作る順序）: GBuffer の書き込みが残るこの項目では組んでも利得が無く、MegaGeometryPass の中へビジビリティの描画を差し込む大きな変更になるため、stop-when に従って止めた。理由・選択肢は `blocked/VTG6-VIS-RASTER.md`。推奨は VTG6-DEFAULT-ON で組む。
- 指摘3（スキニングの ID 表示の撮影）: 未確認のまま記録した（撮影スクリプトにスキニング検証シーンの経路が無い）。
- 検証: `verify-VTG6-VIS-RASTER-19-build.txt`（Debug、BUILD_EXIT_CODE=0）、`-20-ctest.txt`（4/4 passed）。

## 反復 10（run 20261005-191130）: VTG6-MATERIAL-CLASSIFY（done）
- 内容: `VisBuffer.Id` から 8×8 の画面のタイルごとの材質の集合を求め、材質ごとのタイルの一覧と間接 dispatch の引数を作る計算シェーダー `material_tile_classify.comp` と、その記録の本体 `MaterialTileClassify`、RenderGraph のパス `MaterialTileClassifyPass`（`Rendering/MaterialTileClassifyPass.h`・`.cpp`）を足した。
- 方式: 3 回の dispatch。段階 0（タイルごとに 1 ワークグループ。タイルの中で同じ材質の最初の画素だけが `argWords[材質*4]` を +1 する）→ 段階 1（1 ワークグループ。材質の番号の順に一覧の先頭位置を割り当て、引数を `(個数, 1, 1, 先頭位置)` で書く。一覧に入りきらない分は切り詰めて統計へ数える）→ 段階 2（同じ分類をやり直し、`カーソル` で一覧へタイルの番号を書く）。引数は材質ごとに 16 バイトで `VkDispatchIndirectCommand` と同じ並び（w は一覧の先頭位置で、dispatch の引数としては読まれない）。タイルの番号は `tileY * tilesX + tileX`。空の画素（ID = 0）と記録の表から引けない画素はどの材質にも数えない。
- 上限: 材質の番号の上限は 1024（`DEFAULT_MAX_MATERIALS`。引数 16 KiB）。番号がこれ以上の材質はどのタイルにも入れず、統計（`stats[1]`）に数える。一覧の大きさは既定でタイルの数 × 64（1 タイルが出せる材質の最大数）で、一覧が足りなくなることはない（1080p で約 8 MiB）。stop-when（材質の数が引数の上限を超える）は、起動画面・検証シーンの材質の数が数個で該当しない。
- RenderGraph: `VisBuffer.Id` を ShaderResource で読み、引数・一覧・カーソル・統計を `MaterialTile.Args/List/Cursors/Stats` として宣言して書く（終わりの状態は GenericRead。引数は IndirectBuffer の用途つき）。記録の表は `VisibilityRasterPass::GetRecordTable()` から受け取る。分類できないフレーム（記録の表が無い・パイプラインが作れない）は、dispatch せずに引数と統計を 0 にして終える（後の材質の解決がグループ数 0 として安全に読める）。終わりに 4 本とも `UnorderedAccess` から `GenericRead` へ遷移させる（RenderGraph は終わった状態を信じて後のパスの前にバリアを足さないため、書いたパスが遷移させる）。
- 既定は無効: `SceneView::SetupDeferredPipeline` で `--visibility-buffer=on|debug` のときだけ追加し、`SetEnabled(false)` のまま（`MaterialTileClassifyPass` の既定も無効）。VTG6-RESOLVE-GEOMETRY が使うときに有効にする。起動画面の描画は変わらない（GPU の撮影は行っていない）。
- テスト: `MaterialTileClassifyVulkanTest`（`RHITextureUpdateVulkanTest` の束）。36×20 の合成した ID の画像（5×3 のタイル。右端・下端は 8 に満たない部分タイル）と 11 個の記録の表で、ケース A: 3 材質が混じるタイル・同じ材質が別の記録で出るタイル・1 画素だけのタイル・8 材質すべてが出るタイル（+上限以上の材質 12）・引けない ID だけのタイル・空のタイルを、記録の表だけを使う CPU の参照と手で導いた一覧の両方と照合（引数 4 語・一覧の集合・統計 5 項目・見張りの語）。ケース B: 一覧を 15 にして、引数の切り詰め（4,4,4,2,1,0,0,0 と先頭位置）・落とした数 8・一覧の外へ書かないこと。ケース C: 全部が空。範囲の合わない入力は記録しない。`RecordClear` が引数・統計を 0 にする。validation error 0 件。シェーダーのタイル内の重複除去を壊すと（`other < 0u`）テストが落ちることを確かめた。
- `RenderGraphCompileTest` に 3 件を足した: `TestMaterialTileClassifyDispatchesAndPublishesArgs`（分類のパスが加わり dispatch が 10 → 13、タイルは 16×8、引数と統計の 0 埋め、引数が IndirectBuffer の用途で GenericRead へ遷移）・`...WithoutRecordTableClearsArgs`（記録の表が無いと dispatch せず 0 埋め）・`...AbsentByDefault`（足さなければ資源も dispatch も増えない）。
- 検証: `verify-VTG6-MATERIAL-CLASSIFY-5.txt`（Debug の RHITextureUpdateVulkanTest・RenderGraphCompileTest、BUILD_EXIT_CODE=0。-1〜-4 は途中の試行で、-3 は RenderGraph の最終状態の理解違いによる失敗）、`-6.txt`（ctest 2/2 passed）、`-7-direct.txt`（GPU のテストを直接実行: RESULT=PASS、VUID_COUNT=0。ケース A はタイル 15・一覧に書いた数 23・上限外 1・引けない画素 3、ケース B は落とした数 8）、`-8-mutation.txt`（シェーダーを壊すと RESULT=FAIL）。
- Notes: (1) `ICommandList` に `DispatchIndirect` が無い。この項目は引数を作るだけで、材質ごとの間接 dispatch の実行は VTG6-RESOLVE-GEOMETRY で要る。RHI は paths の外なので、`DispatchIndirect`（Vulkan は `vkCmdDispatchIndirect`、引数バッファは IndirectBuffer の用途）の追加は VTG6-RESOLVE-GEOMETRY の前に別件として決める必要がある（ここでは足していない）。 (2) 別件: `SkinningComputePass` が書いた頂点のバッファを、宣言した最終の状態 GenericRead へ遷移させていない（RenderGraph は終わった状態を信じるため、`VisibilityRasterPass` の頂点シェーダーが読む前の同期が保証されない）。`TASKS.md` に VTG6-SKINNING-FINAL-BARRIER として足した。 (3) Git Bash の `sed -i` は CRLF の行を LF にしてしまう（`RenderGraphCompileTest.cpp` で起きて、HEAD の行末へ戻した）。混在行末のファイルは python で編集する。 (4) 新規ファイルは BOM+CRLF。
- Next: VTG6-RESOLVE-GEOMETRY（上の Notes (1) の `DispatchIndirect` と、VTG6-SKINNING-FINAL-BARRIER を先に片づけるのが望ましい）。

## 反復 1（run 20261005-213012）: VTG6-MATERIAL-CLASSIFY 差し戻し対応（done）
- 指摘1（絶対規則1）: `MaterialTileClassifyVulkanTest.cpp` の `std::vector` を `Container::VariableArray` に置き換え、`#include <vector>` を外した。
- 指摘2（統計）: `material_tile_classify.comp` の `atomicMax`（見えた最大の材質の番号 + 1）を、上限の判定より前へ移した。上限以上の材質も測るので、ケース A（材質 12 が見える）の `stats[3]` は 13 になり、stop-when の値を統計から読める。CPU の参照も同じ定義に直した。
- 指摘3（間接 dispatch の x）: 引数を材質ごとに 8 語（32 バイト）にした: x = min(タイルの数, 65535)、y = ceil(タイルの数 / 65535)（0 個でも 1）、z = 1、一覧の先頭位置、タイルの数、予約 3 語。消費側は g = WorkGroupID.y * 65535 + WorkGroupID.x を求め、g >= タイルの数なら return する。この形を `MaterialTileClassifyPass.h` の冒頭とシェーダーの冒頭に書いた。上限は `Layout::GroupCountXLimit`（既定 `MAX_GROUP_COUNT_X` = 65535。`ComputeLayout` の末尾の引数で小さくでき、65535 超は抑える）で、シェーダーには `limits.w` で渡す。引数の大きさは 1024 材質で 16 KiB → 32 KiB になった（既定で無効の間は影響なし）。
- 指摘4（画面の端）: `Record` が、画面の幅・高さが ID のテクスチャより大きいときは記録しない（タイルの数の確認では断られない 40x20・36x24 でも断る）。テストにケース D（48x32 のテクスチャに 36x20 の画像を置き、外側を材質 7 の ID で埋める。画面の端のタイルに数えないこと）を足した。
- 追加テスト: ケース E（上限 3 と 1。材質 0〜2 は x=3・y=2 / x=1・y=4、消費側の番号が数より小さいグループがちょうど一覧の数だけ得られること。上限 65535 超の抑えと 0 の拒否）。ケース B・C・A の引数の確認を新しい並びに合わせた。
- 指摘5: `RenderGraphCompileTest` の `...AbsentByDefault` を `...AbsentWhenNotAdded`（足さない構成）に改名し、実際の既定を試す `TestMaterialTileClassifyAddedButDisabledByDefault` を足した。SceneView と同じく記録の表の取り出し元を渡して初期化し、有効にしない。`View::Render`（`View.cpp:208`）が無効なパスをグラフへ足さないので、テストの組み立てでも同じ規則にした。生成時の既定が無効でなくなると 4 パスになり落ちる。引数の大きさと `GroupCountXLimit` の確認も `TestMaterialTileClassifyDispatchesAndPublishesArgs` に足した。
- 反証の確認（`-6-mutation.txt`）: シェーダーを壊すといずれも RESULT=FAIL になる — (M1) 画面の端の判定 `pixel < screen` を外す、(M2) `atomicMax` を上限の判定の後ろへ戻す、(M3) 引数の x を上限で抑えない。確認後にシェーダーは元に戻した。
- 検証: `verify-VTG6-MATERIAL-CLASSIFY-3.txt`（Debug の RHITextureUpdateVulkanTest・RenderGraphCompileTest、BUILD_EXIT_CODE=0。-1・-2 は `RenderGraphCompileTest` の組み立てが `View::Render` の規則と違っていて落ちた途中の試行）、`-4.txt`・`-7-final-ctest.txt`（ctest 2/2 passed）、`-5-direct.txt`（GPU のテストを直接実行: RESULT=PASS、VUID_COUNT=0）。行末は `git diff --numstat` と `--ignore-cr-at-eol` で一致。
- 既知の限界（残す）: 一覧の大きさが最悪（1 タイル 64 材質）で取ってあり、1080p で約 8.3 MB・4K で約 33 MB。既定で無効の間は影響がなく、VTG6-DEFAULT-ON で予算と照らす。記録の材質の番号がフレームで一意でない件は VTG6-MATERIAL-TABLE で直す。
- Notes: (1) `Edit` ツールは混在行末のファイル（`RenderGraphCompileTest.cpp`）の全行を CRLF にしてしまうので、編集のあとに HEAD の行末へ戻すスクリプト（difflib）で直した。 (2) Git Bash のヒアドキュメントで python を書くと壊れるので、スクリプトをファイルに書いて実行した。
- Next: VTG6-RESOLVE-GEOMETRY（`DispatchIndirect` の RHI への追加と VTG6-SKINNING-FINAL-BARRIER を先に片づけるのが望ましい）。

## 反復 1（run 20261005-214551）: VTG6-RASTER-CHUNKS（done）
- 内容: 実装は 6b85c9bd でコミット済み（手続きメッシュ・スキニングのインデックスを 128 三角形以下の塊に分けて持ち、頂点・インデックスを storage・BDA の用途にする）。この反復は、改訂された verify（`SkinnedRenderPathContractTest` を ctest から外した版）を新しい run で走らせ直して証拠を保存し、done にした。
- 検証: `verify-VTG6-RASTER-CHUNKS-1-build.txt`（Debug の Game・RenderResourcesDomainContractTest・SkinnedRenderPathContractTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（MeshResourcesProceduralGpuTest・GeometryPoolAllocatorTest 2/2 passed）、`-4-ctest-verbose.txt`（「MeshIndexChunks cover every triangle once」）、`-3-skinned-direct.txt`（スキニングの追加ケースの出力「SkinnedMesh chunks cover every triangle once」。その後は既知の TEST-SKINNED（`gbuffer.vert` を読めず assert）で落ちて exit 3。今回は終わらずに abort した）。
- Notes: (1) `--test=` は絞り込みにならず、塊のケースが先頭で走って出力される。 (2) RT の経路はコピーから直接の利用に変わった（手続きメッシュのバッファが BDA を持ったので、`RenderingCoordinator` の `CreateAddressableMeshBuffer` がコピーを作らず元のバッファを返し、RT の BLAS の入力と RTGI・DDGI のインスタンスのアドレスが元のバッファを直接読む。撮影の画素比較で出力は一致）。 ラスタの経路は変えていない。 (3) Git Bash は `/m:1` をパスに変換するため、ビルドは PowerShell で走らせる。
- Next: VTG6-COMPUTE-SKINNING。

## 反復 1（run 20261005-214752）: VTG6-COMPUTE-SKINNING（done）
- 内容: 実装は 53a5448e でコミット済み（`SkinningComputePass`・`skinning_compute.comp`・`ComputeSkinningVulkanTest`。今と前のフレームのパレットで頂点を変形し、フレームごとの storage・BDA バッファへ書く）。この反復は、改訂された verify（`SkinnedRenderPathContractTest` を ctest から外した版）を新しい run で走らせ直して証拠を保存し、done にした。
- 検証: `verify-VTG6-COMPUTE-SKINNING-1-build.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderingVelocityVulkanTest・SkinnedRenderPathContractTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（ComputeSkinningVulkanTest・RenderingVelocitySkinnedVulkanTest 2/2 passed）、`-3-ctest-verbose.txt`（RESULT=PASS、VUID_COUNT=0）。
- Notes: (1) stop-when は該当しない（前のフレームのパレットは RenderThread 側で保持でき、FramePacket の契約は変えていない）。 (2) 描画経路は変えていない（パスは既定で無効）ので起動画面の撮影は未実施。 (3) `SkinnedRenderPathContractTest` の既知の失敗は TEST-SKINNED で別途。
- Next: TASKS.md の残りの未完（VTG6-SKINNING-FINAL-BARRIER など）。

## 反復 11（run 20261005-215042）: VTG6-VIS-RASTER（done）
- 内容: コードの変更なし。`blocked/VTG6-VIS-RASTER.md` の選択肢1（親が採用）に従い、今の実装（MegaGeometryPass の後に両パスの間接描画を `VisBuffer.Id`・`GBuffer.Depth` へ重ね描き）で完了とした。指摘1（スキニングの記録の基点の二重加算）は `31877048` で直っている。
- 持ち越し: 「ビジビリティの1パス目の深度で HZB を作る」順序と、スキニングを含む ID 表示の撮影は VTG6-DEFAULT-ON で行う（TASKS.md の notes に記載済み）。
- 検証: `verify-VTG6-VIS-RASTER-1-build.txt`（Debug の Game・RenderGraphCompileTest・MegaGeometryResourcesTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（4/4 passed）、`-3-build-rwdi.txt`（RelWithDebInfo の Game、BUILD_EXIT_CODE=0）、`-4-capture.txt`（既定の撮影 pass、平均輝度 124.173 / 123.952 / 126.022）。
- 目視確認: `startup-capture/VTG6-VIS-RASTER/default.png`（起動画面は変わらず、家・岩・球・地面が出る）と `VTG6-VIS-RASTER-debug/default.png`（ID の表示で物の輪郭と三角形の塊が出る）を開いた。
- Notes: bash から cmake に `/m:1` を渡すと MSYS のパス変換で壊れる。`MSYS_NO_PATHCONV=1` を付ける。
- Next: 次の未完の項目。

## 反復 1（run 20261005-215530）: VTG6-SKINNING-FINAL-BARRIER（done）
- 内容: `SkinningComputePass::Execute` が、dispatch の後に今・前の頂点のバッファ（`Skinning.CurrentVertices`・`Skinning.PreviousVertices`）を `UnorderedAccess` から宣言した最終の状態 `GenericRead` へ `BufferBarrier` で遷移させる（`MaterialTileClassifyPass` と同じ形）。遷移は `static SkinningComputePass::RecordFinalBarriers` に出した。
- 記録できなかったフレーム: 旧 `Execute` は計算パイプライン・`SkinnedMeshes`・スナップショットが無いと早期 return していたので、宣言したバッファが遷移しないままになった。dispatch の記録を `RecordInstances` に分け、バッファとコマンドリストが取れる限り、インスタンスが 0 でも遷移させる。
- テスト: `RenderGraphCompileTest` に `TestSkinningComputeFinalBarriersTransitionToGenericRead` を足した（2 本が `UnorderedAccess` → `GenericRead`・サイズはバッファ全体・今 → 前の順。コマンドリストやバッファが無いときは何も出さない）。
- 検証: `verify-VTG6-SKINNING-FINAL-BARRIER-1-build.txt`（Debug の Game・RHITextureUpdateVulkanTest・RenderGraphCompileTest、BUILD_EXIT_CODE=0）、`-2-ctest.txt`（ComputeSkinningVulkanTest・RenderGraphCompileTest 2/2 passed）。
- Notes: (1) stop-when は該当しない。`VisibilityRasterPass` は同じ資源を `Read` で宣言し、RenderGraph は書いたパスの最終の状態（GenericRead）を信じて読み取りの前にバリアを足さないので二重にならない（`TestWriteFinalStateSuppressesFollowupReadBarrier` と同じ規則）。 (2) パス自体を RenderGraph に載せた形（スキニングの描画コマンドと貸し出しが要る）のテストは足していない。ヘルパーの遷移だけを確かめた。 (3) 描画経路は変えていない（パスは既定で無効）ので起動画面の撮影は未実施。 (4) `RenderGraphCompileTest.cpp` は CRLF 主体の混在行末。python でバイト単位に挿入し、`git diff --numstat` と `--ignore-cr-at-eol` の一致を確かめた。
- Next: TASKS.md の残りの未完の項目。

## 反復 1（run 20261005-220157）: VTG6-CHUNKS-HARDEN（done）

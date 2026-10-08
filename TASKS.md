# TASKS — NorvesLib

起動画面（Rendering3DTest）の描画改善（2026-09-27、ユーザー決定）。物理空と空の太陽による昼の屋外、点光源のキューブシャドウ、自動露出・ミップチェーンのブルーム・TAA・GTAO・コンタクトシャドウ・色収差・レンズダート・グレーディングLUT・RTGIの既定化、展示物の追加、視差オクルージョンと影の不具合の修正を `SS-` の項目で進める。見た目の証拠は `Scripts/CaptureStartupScene.ps1` の撮影を開いて確かめる。起動画面の見た目を変えることはこの計画でユーザーが承認済み。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は、項目に書いた場合を除き結果を変えない。

2026-09-30 再開: 止めていた4項目（SS-DAYLIGHT-P1・P2、SS-POINT-SHADOW-P2、SS-EMISSIVE-GLOW）は各 `blocked/<ID>.md` の推奨の選択肢で再開し、空のモデル（SS-SKY-MODEL-P1・P2）と発光の露出（SS-EMISSIVE-PREEXPOSE）を足した。この3項目は、項目に書いたとおり空を使う検証（R2・R7屋外）とgoldenの結果を変えうる。

2026-10-03 続き（ブランチ `feature/startup-scene-detail`）: ユーザーの指摘（石のタイルが近づくと粗い）と要望（球をMegaGeometryで高ポリに）から、FIX-ASYNC-TEXTURE-MIPS → SS-CSM-MEGA-CASTERS → SS-MEGA-SPHERE → SS-MEGA-SPHERE-DISPLACE → SS-ACCEPT-DETAIL の順に進める。

2026-10-03 続き2（ブランチ `fix/showcase-sphere-reflection-ao`）: ユーザーの指摘（金属の見本の球がカメラの角度によって一部透けて見え、同じ角度でも影のかかり方がちらつく）から、FIX-GTAO-STATIC-NOISE → FIX-SSR-SPECULAR-COMPOSITE を直す。

2026-10-04 続き3（ブランチ `feature/ground-material-showcase`）: ユーザーの要望（地面のテクスチャを色々用意して質感を見比べられるように。テクスチャは Poly Haven から落とし、git に入れない）から、SS-GROUND-SWATCHES を足し、その途中で見つけた FIX-MESH-BOUNDS-CULLING を直す。

2026-10-04 続き4（段1はブランチ `feature/vtg-stage1-bc`）: ユーザーと決めた全体計画 `Docs/Plans/VirtualizedTextureGeometryPlan.md`（テクスチャとジオメトリの仮想化。BC圧縮・sparse のVT・2パスの遮蔽カリング・LODの階層の焼き込み・ページのストリーミング・ビジビリティバッファ・ソフトウェアラスタ・VSM）を `VTG<段>-` の項目で進める。決定事項・設計・段の受入れは計画書が正本。1段ずつ回し、今の段だけ `todo`、後の段は `backlog`。段が終わると親が受入れを確かめて main へマージ・プッシュし、次の段の項目を詳しくしてから `todo` にする。8GB級のGPUで収まるのが目標で、開発機では `--vram-budget-mb` の上限で確かめる。起動画面（天球・地面・球・岩）が見える状態は全段で保つ。

それより下はR0〜R8と関連の修正の記録。R8までの完了後に残った `todo` は、起動画面の作業を先に進めるため `backlog`（ループが拾わない）にしてある。再開するときは `todo` へ戻す。

## PT-NONUNIFORM-SAMPLER: パストレーサーの材質のテクスチャの添字の一様でない印を、標本の関数の中まで届ける
- status: backlog
- done-when: `PathTracingClosestHit.glsl`（36〜37 行付近）が `materialTextures[nonuniformEXT(i)]` で取った sampler を関数の引数で渡し、呼ばれる側（`SparseResidencySampling.glsl` の `SampleMaterialTextureLod`・`SampleSparseResidentLod`）で標本している。GL_EXT_nonuniform_qualifier の一様でない印は関数の引数を越えて伝わらないので、標本の命令の sampled image に `NonUniform` の装飾が付かない疑いがある（NVIDIA では表に出ないが、装飾に頼る装置では1つの wave の中で別の材質のテクスチャを取り違えうる。VT に限らず PT の全材質のテクスチャ）。コンパイルした rchit を `spirv-dis` して、`OpImageSampleExplicitLod`・`OpImageSparseSampleExplicitLod` が受け取る sampled image に `NonUniform` があるかを確かめ、無ければ添字を関数へ渡して呼ばれる側で `nonuniformEXT` を付ける形にする。CPU のテスト（SPIR-V の逆アセンブルの照合）か、記録で確かめる。
- verify: `cmake --build build --config Debug --target PathTracingMaterialVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^PathTracingMaterialVulkanTest$"`
- paths: Assets/Shaders/PathTracing, Assets/Shaders/Common, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-PT-VT-TEXTURES の評価で見つけた（段6の外）。

## PT-STARTUP-GEOMETRY: パストレーサーの起動画面の撮影で、ばらのテクスチャのとき岩が無く小屋が影絵になる件を調べる
- status: backlog
- done-when: パストレーサーを有効にした起動画面の撮影（`-LooseTextures`）で、左右奥の岩が無く小屋が平らな灰色の影絵になる、VT の near の撮影で球と空の上に白い三角形の粒が散る（VTG6-PT-VT-TEXTURES の前から）原因を特定し、直すか既知の限界として記録する。あわせて、パストレーサーのパイプライン（`SceneView.cpp` の `SetupPathTracingPipeline`）は GBuffer を持たないので VT の要求が出ず、VT のテクスチャは粗いミップのまま読む（記録する）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- paths: Assets/Shaders/PathTracing, Library/Core/Private/Rendering, Game, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG6-PT-VT-TEXTURES の評価で見つけた（段6の外）。撮影は `.harness/runs/startup-capture/VTG6-PT-VT-TEXTURES*`。

## TEST-ASSERT-NO-DIALOG: Debug のテストが assert の失敗で対話窓を出して ctest の打ち切りまで止まるのを、共通の仕組みで止める
- status: backlog
- done-when: `assert(` を使い、`_set_abort_behavior` も `_CRT_ERROR` の報告先の設定もしていないテストの実行ファイル（2026-10-06 の数え方で 158 件、Test/Core/Rendering で 96 件）が、Debug で assert が失敗すると対話窓を出して ctest の打ち切りまで待つ（TEST-SKINNED と同じ止まり方）。テストの実行ファイル全部に効く共通の仕組み（テストの共通のライブラリに静的初期化で `_set_error_mode(_OUT_TO_STDERR)` と `_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT)` を入れて CMake で全テストへリンクする等）で止める。わざと assert を落とすテストの実行ファイルが、対話窓を出さずにすぐ非 0 で終わることを確かめる。あわせて、`RenderGraphCompileTest` などが副作用を `assert(...)` の中に置いていて NDEBUG の構成（RelWithDebInfo）では初期化ごと消えて 0xC0000005 で落ちる件を記録する（直すのは別）。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- paths: Test, Library/Core/CMakeLists.txt, CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 2026-10-06 TEST-SKINNED の評価で見つけた（段6の外）。

## VTG7-DETERMINISM-SEED: 決定的な撮影が同じ構成でも run 間で一致しない種を見つける
- status: backlog
- done-when: 同じ構成（`-Deterministic -SunElevations 10 -ViewNames near -SwRaster Off`。TAA・RTGI・クック済みテクスチャ）で撮影を繰り返すと画像が一致する状態にする（4 回の PNG がすべて完全一致、または差が最大 1 の数画素以内）。直せない原因（ドライバ・ハードウェアの非決定性）と分かったときは、その証拠（どの資源・どのパスの出力が run 間で違うか）を PROGRESS に書く。VTG7-SW-PATH-DIFF の結果: 同じ構成を繰り返すと 2〜3 つの状態に分かれ、状態の間は 65.3 dB（8bit の ±1、不一致 4.9%、大きな球に集中）。FXAA（8 回）と `-LooseTextures`（6 回）は完全一致、`--virtual-texture=off`・`--geometry-streaming=off`・全バリア + 毎フレーム WaitIdle・エポックの遅延では消えない。RTGI のログの論理（フレーム・履歴・標本の列）は 8 回とも同一で、プリエクスポージャだけが float32 で 6〜7 ULP ずれる。SceneColor を読み戻すと、種は小屋の軒先の数画素（x 388〜394・y 211〜215、最大 0.003）。切り分けの順: (1) クック済みの小屋のテクスチャだけをばらの元画像へ替える（小屋の材質・クック済みのアルベド・法線・ORM のどれが効くか）、(2) 軒先の画素の GBuffer（深度・法線・アルベド・材質）を run 間で RGBA の生で比べ、最初に違う段を探す、(3) 圧縮形式（BC7・BC5）のミップの末尾（4x4 より小さいミップ）・アップロード・サンプラーの設定を見る、(4) 種が RTGI のレイクエリ（小屋の薄い面の縁）なら、加速構造の構築・更新の決定性を確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-1 -Configuration RelWithDebInfo -Deterministic -SunElevations 10 -ViewNames near -SwRaster Off`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-2 -Configuration RelWithDebInfo -Deterministic -SunElevations 10 -ViewNames near -SwRaster Off -CompareDeterministicWith .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-1 -DeterministicPsnrLimit 100`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-3 -Configuration RelWithDebInfo -Deterministic -SunElevations 10 -ViewNames near -SwRaster Off -CompareDeterministicWith .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-1 -DeterministicPsnrLimit 100`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-4 -Configuration RelWithDebInfo -Deterministic -SunElevations 10 -ViewNames near -SwRaster Off -CompareDeterministicWith .harness/runs/startup-capture/VTG7-DETERMINISM-SEED-1 -DeterministicPsnrLimit 100`
- stop-when: 原因がドライバ内（加速構造の構築など）でエンジン側から直せない場合は、証拠を記録して止める。直すのに 1 反復を超える場合は、切り分けの結果を記録して直す項目を足す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/RHI, Library/Core/Private/RHI, Library/Core/Private/Engine, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-06 VTG7-SW-PATH-DIFF から分けた（ソフトウェアラスタの経路が原因ではない）。判定は 4 回の完全一致。一時的な計装は同じ編集で戻し、作業ツリーを `git stash`・`git checkout --` で動かさない。VTG7-SW-DEFAULT-ON の on・off の比較は FXAA の撮影で行う（VTG7-SW-FXAA-COMPARE。PROGRESS の VTG7-SW-PATH-DIFF の節）。 2026-10-06 親: 段7のほかの項目（VTG7-SW-HARDEN-TESTS・VALIDATION・THRESHOLD）を先に回し、VTG7-SW-DEFAULT-ON の前に行う（off の経路にも元からある非決定性で、段7の変更が原因ではない）。 2026-10-06 親（VTG7-SW-PATH-DIFF の評価）: verify の比較に `-DeterministicPsnrLimit 100` を足した（既定の 45 では 65.3 dB の状態の差でも合格し、done-when の完全一致を確かめられない。スクリプトは PSNR を 100 で頭打ちにするので ±1 の数画素は合格する）。 2026-10-06 親（VTG7-SW-FXAA-COMPARE の結果）: 負荷モード 300 個の default は FXAA で RTGI を切っても、ハードだけの off 同士が揺れる（RTGI を切った丸めなしの 4 回 6 組で 0〜268 画素、最大 10〜50。画像は離散的な状態に分かれる。32〜277 画素は前の記録の on と off の組の値）。最後に適用される露出の最後の桁も off 同士で揺れる（EV の 16 進が `416a28ab`・`416a28ac` の 2 通り）。起動画面の種（クック済みテクスチャ × TAA × RTGI）とは別の源。候補: GPU のカリングの `atomicAdd` でコマンドの並びが run ごとに変わり、同じ深度で重なる三角形の勝者（ID のラスタは LessOrEqual で後に描いた側が勝つ）が入れ替わること。負荷モードの種も対象にし、起動画面と負荷モードのどちらの種かを分けて記録する。 2026-10-06 親: 1 反復（run 20261006-185554）は 60 分の上限で切れ、記録を書く前に止まった（撮影 171 組と `exp-*.txt` の測定は `.harness/runs/20261006-185554/` と `.harness/runs/startup-capture/VTG7-DETERMINISM-SEED-*` に残る。SceneColor の毎フレームのハッシュ・VisBuffer の安定な色づけの一時的な計装は `.harness/runs/20261006-185554/seed-instrumentation.patch`。コミットには入れていない）。段7で入った問題ではなく off の経路にも元からあり、段7の受入れの条件でもないので、段の外の後回しにした。再開するときは、測定を区切りごとに PROGRESS へ書いてから次の撮影へ進む。

## VTG9-VSM-SLICES: VSM の「段」を、太陽の段と点光源の面を同じ形で持てる「スライス」に一般化する
- status: done
- done-when: 段8の VSM のデータの形を、クリップマップの段に限らない「スライス」の表へ一般化する。画は変えない（太陽の結果は今と同じ）。(1) スライスの表（storage buffer）: スライスごとにページの表の先頭・ページの数（一辺）・投影の種類（正射影の段／透視の面）・行列と原点・texel の大きさを持ち、今の固定長の uniform（`levelInfo[16]`・`levelOrigin[16]`・`thresholds[4]` など）のうち段ごとの値をここへ移す（段を選ぶしきい値は太陽のものとして残してよい）。(2) ページの表の番地は「スライスの先頭 + ページの番号」。太陽の 10 段は今と同じ大きさ（128 × 128）で先頭から並べる。(3) 展開のインスタンスの段の欄（今は 4 bit）をスライスの番号（8 bit 以上、最大 256 スライス）に広げ、塊の段の印（今は 32 bit の `levelMask`）は、スライスが 32 を超えても扱える形（スライスの組ごとの印か、塊 × スライスの一覧）にする。(4) MegaGeometry の cull の `gl_WorkGroupID.z` の段・dirty の mip の階層もスライスで数える。(5) 印付け・割り当て・持ち越し・無効化・展開・描画・照明・影の測定（`ShadowProbePass`）・`vsm_sample_probe.comp` のすべてが新しい番地を使う。`VirtualShadowMapVulkanTest`・`VirtualShadowMapClipmapTest`・`RenderGraphCompileTest`・golden 4 本が期待値を変えずに通る（番地の式に合わせた書き換えは可。比べる値・しきい値は変えない）。スライスが 33 個以上あるときの展開・cull の場面を `VirtualShadowMapVulkanTest` に足す（合成の 40 スライス。太陽の段の後ろに置いた正射影のスライスで、印・割り当て・描画が先頭の 10 段と同じ texel になる）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- stop-when: 1 反復で閉じなければ、(1)(2)(5) を先にコミットし（段の欄は 4 bit のまま、スライスは 16 まで）、(3)(4) を `VTG9-VSM-SLICES-WIDE` として TASKS.md に足す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- 結果: 2026-10-08 stop-when に従い (1)(2)(5) を完了した（スライスの表 `GPUVsmSlice`・`Common/VirtualShadowMapSlice.glsl`。段の欄は 4 bit のまま、スライスは 16 まで）。(3)(4) と「スライスが 33 個以上の場面」は `VTG9-VSM-SLICES-WIDE` へ移した。
- notes: 2026-10-08 親（段9の開始時に詳しくした。計画書 §4.3「VSM（点光源）: 6面のキューブを同じ物理プールで持つ」、§5 の段9の受入れ「夜の電球の影、8GB 級の上限での全体の負荷モード」）。段8の VSM は段（クリップマップ）を前提にした固定長の配列・4 bit の段の欄・32 bit の段の印を持つ（`VirtualShadowMapPass.h`・`Common/VirtualShadowMapParams.glsl`・`vsm_expand.comp`・`vsm_draw.vert`・`Common/VirtualShadowMapMegaCull.glsl`）。点光源は最大 4 灯 × 6 面 × 解像度の段（mip）で 100 スライスを超えるので、先に器を広げる。危険地帯（描画パス・GPU の資源）。段9の撮影は段8と同じ 3 つに限る（速度の項目の GPU 時間、段の受入れの起動画面と影の測定、既定の描画経路を変える項目の golden の ctest と検証レイヤー付き Debug の実行）。項目ごとの決定的な撮影の繰り返しの比較はしない。テストのコードでも標準ライブラリの型を使わない。テストの実行ファイルは増やさない。

## VTG9-VSM-SLICES-WIDE: VSM のスライスを 33 個以上にできるよう、展開の段の欄・塊の段の印・cull と dirty の階層を広げる
- status: done
- done-when: VTG9-VSM-SLICES で作ったスライスの表（`GPUVsmSlice`。太陽の 10 段は先頭から 128 × 128 で並ぶ）の上で、スライスの数を 16 から 256 まで広げる。画は変えない（太陽の結果は今と同じ）。(1) 展開のインスタンス（`uvec4` の y）の段の欄を 4 bit から 8 bit にし（`スライス | 物理ページ << 8`。物理ページは 24 bit まで）、`vsm_expand.comp` の書き込みと `vsm_draw.vert` の読みを合わせる。(2) 塊の段の印（`VsmShadowChunk::levelMask`。32 bit）を、スライスの組ごとの印にする: 空いている `reserved` を「組の番号（スライス / 32）」にし、`levelMask` のビット b を組 g のスライス g × 32 + b とする。1 つの投影物が複数の組にまたがるときは、同じ描画の記録を持つ塊を組ごとに出す（CPU の `LevelMaskForBounds` の呼び出しと、`vsm_mega_chunks.comp` の `1u << entry.y` も組に合わせる）。展開は塊の組のスライスだけを処理する。(3) MegaGeometry の cull の `gl_WorkGroupID.z` と dirty の階層（`vsm_dirty_mips.comp` の `gl_GlobalInvocationID.z`・ビットの番号）をスライスで数え、ページの表・要求のビット列・dirty の階層の大きさ（`PageTableBytes`・`REQUEST_WORDS`・`MegaDirtyBitsBytes`）をスライスの数から決める（今の `LEVEL_COUNT` の固定をやめる）。割り当ての統計 `StatLevelsUsed`（32 bit の集合）は、スライスが 32 を超えても溢れない形（先頭 32 スライス分の集合と、それ以降の使用の有無）にする。(4) `Pages`・`Raster`・`MegaCull` の dispatch に、スライスの数と、外から渡すスライスの表（null ならクリップマップから作る）を足す。(5) `VirtualShadowMapVulkanTest` に、合成の 40 スライスの場面を足す: 太陽の段の後ろに置いた正射影のスライス（ページの表の先頭は連続する番地）で、印・割り当て・描画（展開と cull）が先頭の 10 段と同じ texel になる。`VirtualShadowMapVulkanTest`・`VirtualShadowMapClipmapTest`・`RenderGraphCompileTest`・golden 4 本が期待値を変えずに通る（番地の式に合わせた書き換えは可。比べる値・しきい値は変えない）。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- stop-when: なし（組ごとの印の形は、根拠を PROGRESS に書けば変えてよい）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 VTG9-VSM-SLICES の stop-when で分けた。点光源は最大 4 灯 × 6 面 × 解像度の段で 100 スライスを超えるので、VTG9-VSM-POINT-SETUP の前に済ませる。危険地帯（描画パス・GPU の資源）。段9の撮影は段8と同じ 3 つに限る。テストの実行ファイルは増やさず、標準ライブラリの型を使わない。
- 結果: 2026-10-08 完了。展開のインスタンスの段の欄を 8 bit（スライス | 物理ページ << 8）、塊の段の印を 32 スライスずつの組（`reserved` = 組の番号、複数の組にまたがる投影物は組ごとに塊を出す）にし、ページの表・要求のビット列・dirty の階層の大きさと Pages・Raster・MegaCull の dispatch をスライスの数と外から渡すスライスの表で決めた。`StatLevelsUsed` は先頭 32 スライスの集合、`StatLevelsUsedBeyond` が 33 番目以降の使用の有無。印付けが選ぶ太陽の段の先頭は `MarkFirstSlice`（段 L = スライス MarkFirstSlice + L）。40 スライスの合成の場面（ケース W・J4・CPU の組ごとの印）が先頭の 10 段と同じ texel・クラスタになる。

## VTG9-VSM-POINT-SETUP: 点光源の VSM の面と解像度の段を CPU で作り、切り替えの引数を足す
- status: done
- done-when: (1) 起動引数 `--point-shadow-method=cube|vsm`（既定 cube。`--shadow-method` と同じ経路）と `Scripts/CaptureStartupScene.ps1` の `-PointShadowMethod Cube|Vsm`（常に渡す。既定 Cube）。vsm は `--shadow-method=vsm`（VSM が使える装置）のときだけ効き、それ以外は cube（`VSM_FALLBACK reason=point_requires_vsm` を 1 回）。(2) 点光源の VSM の設定 `VirtualShadowMapPointLights`（Public/Rendering）: `FramePacket::PointShadows`（最大 4 灯、`BuildPointShadowSnapshot` の選び方と順）の各灯について 6 面 × 解像度の段（既定: 面の解像度 4096²、段 0〜5 = 4096・2048・1024・512・256・128、1 段の一辺のページは 32・16・8・4・2・1）を VTG9-VSM-SLICES のスライスとして並べる。面の行列は `PointShadowFaceMatrices` と同じ（90 度、near 0.05 m、far = Range）。深度は面の軸の向きの線形の距離 ÷ Range（[0,1]、0 が光源の側）。(3) 受け手の段の選び方: 受け手の面は光源からの向きの主軸、段は texel（面の軸の距離 z で 2z ÷ 段の解像度）が画素の大きさ p(d)·2^b 以下の最も粗い段（b は太陽と同じ既定 −0.5。段 0 より細かくは選ばない）。(4) CPU のテスト `VirtualShadowMapPointTest`（`CameraViewConstantsTest` の束の MEMBER）で、面の選び方が `PointShadowFaceMatricesTest` の面と一致する、すべての向きがどれかの面に入る、段の texel が p(d)·2^b 以下で距離について単調、ページの座標が面の範囲に収まる（面の縁・角の向きを含む）、を確かめる。変異（面の主軸の不等号を逆にする）で落ちることを記録する。(5) vsm のとき、起動後と灯の数・位置・Range が変わったときに `VSM_POINT lights=<n> slices=<n> face_res=<n> mips=<n>` を出す。描画はまだキューブのまま。 (6) `Scripts/CaptureStartupScene.ps1` に `-SphereSpin On|Off`（既定 On。Off のとき Game へ環境変数 `NORVES_STARTUP_SPHERE_SPIN=0` を渡し、起動画面の大きな球の自転を止める。影の測定は止まった物を前提にするため）を足す。
- verify: `cmake --build build --config Debug --target Game CameraViewConstantsTest RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapPointTest|PointShadowFaceMatricesTest|VirtualShadowMapClipmapTest|RenderGraphCompileTest)$"`
- stop-when: なし（面の解像度・段の数は、根拠を PROGRESS に書けば変えてよい）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Engine, Library/Core/Private/Engine, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。今の点光源の影はキューブの配列（面 512²、層 = 灯 × 6 + 面、値は距離 ÷ Range、`Common/PointShadow.glsl` の 16 タップ PCF）。起動画面の夜の電球は 1 灯（位置 (4,1,0)、Range 10 m、`Rendering3DTestRoutine.cpp`）で、地面まで約 2 m。キューブの texel は 2 m で約 7.8 mm、VSM の段 0 は約 1 mm。半透明（`forward_transparent.frag`）はキューブのまま（段8の太陽と同じ扱い）。

## VTG9-VSM-POINT-MARK: 点光源の面のページに印を付け、太陽と同じプールから割り当てる
- status: done
- done-when: `--point-shadow-method=vsm` のとき、印付けの計算で、深度の各画素について影を持つ点光源のうち Range の内側のものごとに、面・段・ページを VTG9-VSM-POINT-SETUP の選び方で求めて要求のビットを立てる（照明の PCF の核が面の中でページの境界をまたぐときは隣のページにも。面の縁をまたぐ核は隣の面の同じ段のページにも印を付ける）。割り当て・消去は太陽と同じプール・空きの一覧で行い、溢れは数える。`VSM_PAGES` に点光源の分（`point_requested=<n> point_allocated=<n>`）を足す。`VirtualShadowMapVulkanTest` に、合成の深度（光源の近くの床と壁）と 1 灯から印が付くページの集合が CPU で求めた集合と一致し、太陽の段のページと物理ページが重ならないことを確かめる場面を足す。変異（面の縁の隣の面への印を外す）で落ちることを記録する。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|RenderGraphCompileTest)$"`
- stop-when: なし。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。危険地帯（描画パス）。
- 結果: 2026-10-08 完了。`vsm_mark.comp` が太陽の印付けの後に点光源の印付けをする（binding 9 の `VsmPointParams`: 灯の数・先頭スライス・段の数・面の解像度・画素の大きさ・核の半径・灯の位置と Range）。灯ごとに Range の内側の画素から面（主軸）・段（CPU の `SelectVirtualShadowMapPointMip` と同じ式）を選び、核を受け手の面の接平面上の正方形で表して、6 面の錐台（近い面と 4 つの縁）で切り、残った多角形の頂点を面の NDC へ写した外接の範囲のページすべてに、その面の同じ段で印を付ける（面の縁をまたぐ核は隣の面のページにも付き、3 × 3 の標本では取りこぼす隣の面のページも覆う。受け手から届かない面は余裕つきの判定で飛ばす）。太陽の段の数が 0（太陽のクリップマップが無効。夜）のフレームも、深度があれば点光源の印付け・割り当てをする（`Pages` が太陽と点光源の入力を別々に検証し、シェーダーは太陽の印付けを飛ばす。キャッシュは太陽があるフレームだけ引き継ぐ）。割り当て・消去は太陽と同じ経路で、`vsm_allocate.comp` が投影の種類が透視のスライスの要求・割り当てを `StatPointRequested`・`StatPointAllocated`（`STATS_WORD_COUNT` 56）に数え、`VSM_PAGES` に `point_requested`・`point_allocated` を足した。点光源の VSM のとき Pass はページの表・要求のビット列を 154 スライス（太陽 10 + 4 灯 × 6 面 × 6 段）で作り、`PointLights` を Pages へ渡す。`VirtualShadowMapVulkanTest` のケース P（P1 全画素・P1b プール不足・P2 面の縁をまたぐ画素だけ・P3 標本だけでは隣の面のページを取りこぼす向きに 4 灯を置いた 1 画素・N1 太陽なしで全画素）は合成の深度と 2 灯（P3 は 4 灯）から印の集合が倍精度の参照（核の正方形を各面で切った外接の範囲。核の大きさとページ番号の境を少し縮めた場合と広げた場合で集合が変わる画素は曖昧として除く）と一致し、太陽と点光源の物理ページが重ならず、点光源の統計が合うことを確かめる。N1 は太陽の段に要求も割り当ても無い。変異（受け手の面以外の面に印を付けない = 隣の面への印を外す）で P1・P2・P3 が落ちる（`verify-VTG9-VSM-POINT-MARK-mutation.txt`。P3 は GPU 10 ページ対 参照 18 ページ）。

## VTG9-VSM-POINT-RASTER: 点光源の面へ、影の塊 × ページの単位で深度を描く
- status: done
- done-when: 段8の展開・描画を点光源の面のスライスへ広げる。(1) 展開: 塊の境界球が光源の Range の内側で面の錐台と交わるとき、球を面へ透視で写した矩形（球が近い平面 z ≤ near を越えるときは面全体）が覆うページのうち、割り当て済みで dirty のものへインスタンスを作る。(2) 描画: 頂点シェーダーはワールドの位置を面の透視の行列でクリップ座標へ写し、ページの局所座標の NDC へ移す（w は面の軸の距離のまま。透視の補間が正しくなる）。深度は面の軸の向きの線形の距離 ÷ Range を渡し、断片シェーダーが `atomicMin` で書く（太陽と同じ）。(3) 手続きメッシュ・スキニングの投影物は、CSM と同じ集め方で Range の内側の物を記録にする。(4) `VirtualShadowMapVulkanTest` に、点光源の前に置いた四角形の投影物を面の段 0 と段 2 に描き、読み戻した texel が四角形の深度（面の軸の距離 ÷ Range）で、四角形の外が 1.0、面の境界をまたぐ四角形が 2 つの面に切れ目なく描かれ、光源の後ろ（z ≤ near）を通る三角形で壊れないことを確かめる場面を足す。変異（w を 1 にする・深度を Euclid の距離にする）で落ちることを記録する。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|RenderGraphCompileTest)$"`
- stop-when: なし。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。キューブの経路は断片で距離を書くので早期 Z が効かない。VSM の描画は深度の比較を使わず `atomicMin` なので同じ。危険地帯（描画パス）。

## VTG9-VSM-POINT-MEGA: MegaGeometry の投影物を点光源の面ごとにカリングして描く
- status: done
- done-when: MegaGeometry の投影物（`bCastShadow`）を点光源の面のスライスごとに GPU で選んで描く。インスタンスの判定は境界球と Range・面の錐台・dirty のページの階層。クラスタの LOD は透視（自分の誤差 ÷ その距離の面の texel（2z ÷ 段の解像度）≤ 1 texel、親の誤差 ÷ texel > 1 texel）。HZB・法線の円錐・ソフトウェアラスタの振り分けは使わず、影のためのページの要求はしない。選んだクラスタは VTG9-VSM-POINT-RASTER の展開・描画で描く。1 面あたりの描画の上限（今のキューブの `PointShadowMaxMegaDrawsPerFace` = 8）は持たない。RenderGraphCompileTest で、点光源のスライスの cull が太陽の段の cull と同じ流れに入り、主の経路のバッファへ書かないことを確かめる。`VirtualShadowMapVulkanTest` か CPU の写しのテストで、透視の LOD の選び方（光源から遠いほど粗い段、選んだクラスタが一つの切り口）を確かめ、変異（親の条件を外す）で落ちることを記録する。`VSM_MEGA_CULL` に点光源の分を足す。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|GeometryPageRequestVulkanTest|RenderGraphCompileTest)$"`
- stop-when: 1 反復で閉じなければ、cull（選ぶまで）で一度コミットし、描画へのつなぎを `VTG9-VSM-POINT-MEGA-DRAW` として TASKS.md に足す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。負荷モード（`--stress-mega-instances=300`）でキューブの 1 面あたりの上限（8）を超えて省かれる 1032 回は、VSM の描画では起きない（半透明のために残すキューブの描画では残る）。危険地帯（描画パス）。
- 結果: 2026-10-08 完了。`vsm_mega_cull.comp` が透視のスライス（点光源の面）も太陽の段と同じ 1 回の dispatch で処理する（スライスの投影の種類で分岐）。インスタンス・クラスタの判定は展開と共有の `VsmPerspectivePageRange`（Common/VirtualShadowMapSlice.glsl へ移した。Range・近い平面・遠い平面・4 側面で球を切り、透視像が覆うページの矩形）で、dirty の階層は面の座標のページ（一辺 origin.w）を引く（`vsm_dirty_mips.comp` はページの表の一辺の外を読まない）。LOD は透視: texel = 球の最も近い点の面の軸の距離 z での 2z ÷ 段の解像度（`ShadowTexelMetersAtSphere`。自分の球・親の球・メッシュ共通の球でそれぞれ求める）で、自分の誤差 ÷ texel ≤ 1 かつ親の誤差 ÷ texel > 1。HZB・円錐・ソフトラスタ・ページの要求は無く、1 面あたりの描画の上限も無い。`VirtualShadowMapMegaCull` は外から渡すスライスの表があれば太陽が無効でも記録し、Pass は点光源の VSM のとき dirty の階層をスライスの数で作り、スライスの表を渡して太陽が無効な夜のフレームも記録する（選んだクラスタは VTG9-VSM-POINT-RASTER の展開・描画の流れに乗る）。統計の語 56・57（`StatMegaPointInstances`・`StatMegaPointClusters`。`STATS_WORD_COUNT` 58）を足し、`VSM_MEGA_CULL` に `point_instances` `point_clusters` を足した。`VirtualShadowMapVulkanTest` のケース J5（1 灯 × 6 面 × 6 段、太陽なし、面 0 の軸の上 12 m と 40 m に同じ木を置く）: 光源から遠いほど・段が粗いほど粗いクラスタ（12 m は葉 8・4・2・根 1×3、40 m は 2・根 1×5）、一つの切り口、面のページの表（一辺 32〜1）の dirty の階層が倍精度の参照と全語一致、面 0 以外・Range の外・影を落とさない物は選ばれない、溢れたクラスタの範囲のページに再描画の印が付く。変異（親の条件を外す）で J5-1 が落ちた（`verify-VTG9-VSM-POINT-MEGA-mutation.txt`）後、戻して全件通過（`-4.txt`）。RenderGraphCompileTest に `TestVirtualShadowMapPassCullsMegaCastersForPointFacesInTheSameFlow`（太陽あり・なしで、カリングの dispatch が 1 組・z がスライスの数・主の経路のバッファへ束縛もバリアもしない・点光源のスライスの表が渡る）を足した。期待値の書き換えは `VSM_Stats` の大きさ 224 → 232 と統計の語の並びの表明だけ。MegaGeometry のクラスタの記録から点光源の面の物理ページへ描く GPU の通しの検査は、ケース R（手続きの塊）とケース K（太陽）に分かれており、点光源 × MegaGeometry の通しは VTG9-VSM-POINT-GPU-TIME の撮影（`VSM_MEGA_CULL` の point_*）で初めて見る。

## VTG9-VSM-POINT-SAMPLE: 照明で点光源の VSM を読み、影の測定でも点光源を測る
- status: done
- done-when: `--point-shadow-method=vsm` のとき、`lighting.frag` の点光源の影を VSM で読む（半透明 `forward_transparent.frag` はキューブのまま）。(1) 受け手の面・段は印付けと同じ選び方。法線の向きへのずらしと深度の比較の余裕は `Common/PointShadow.glsl` と同じ考え方で、使う段の texel に比例させる。(2) PCF は 16 点で、半径はワールドで r = max(画素の大きさ × `PCF_MIN_RADIUS_PIXELS`, 使う段の 1 texel)（太陽と同じ連続な下限）。各標本は自分の位置の面・ページの表を引き、割り当てのないページは粗い段へ逃げ、どの段にも無ければキューブの値ではなく影なしとし、逃げた数を統計に数える。(3) 影の測定（`--shadow-probe`）に点光源の測り方を足す: 太陽が無い（夜）か `--shadow-probe=point` のとき、影を持つ最初の点光源について、同じ標本でキューブ（`SamplePointShadow`）と VSM の可視度を求め、`SHADOW_PROBE light=point method=cube|vsm ...`（太陽と同じ項目。mean_texel_mm はキューブは 2z ÷ 512、VSM は使った段の texel）と `SHADOW_PROBE_AGREE light=point ...` を出す。Range の外・光の当たらない向きの標本は数えない。(4) `VirtualShadowMapVulkanTest` に、VTG9-VSM-POINT-RASTER の四角形の場面で照明と同じ関数を受け手の点で評価し、影の中心で 0・外で 1・縁で途中の値、割り当てのないページは粗い段の値になることを確かめる場面を足す。変異（面の選び方を 1 つずらす・逃げ道を外す）で落ちることを記録する。golden 4 本は変わらない。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|RenderGraphCompileTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- stop-when: なし。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。電球の物理の半影（光源の半径）は段9では扱わず、今のキューブと同じ PCF の考え方にそろえる（今のキューブの見た目から大きく変えない。絶対規則7）。この項目では起動画面を撮らない（VTG9-VSM-POINT-GPU-TIME の撮影と VTG9-VSM-POINT-DEFAULT-ON の検証の実行で初めて画を見る）。危険地帯（照明のシェーダー）。

## VTG9-VSM-POINT-CACHE: 点光源の面のページを次のフレームへ持ち越す
- status: done
- done-when: 点光源の面のスライスでも段8の持ち越しを使う。(1) 灯の位置・Range が変わったら、その灯のスライスのページをすべて無効にする（灯の並びが変わったときは、灯の識別子で前のフレームのスライスと対応づけ、対応の無いスライスは空きへ戻す）。(2) 動いた投影物の前後の境界球を、Range の内側の灯の各面へ写した矩形のページを dirty にする。(3) `VSM_CACHE` に点光源の分を足す。(4) `VirtualShadowMapVulkanTest` に、止まった灯と投影物の 2 フレーム目に点光源のページが描かれない、投影物を動かすとその面の範囲だけが描き直され texel が毎フレーム描き直したときと一致する、灯を動かすとその灯の全ページが描き直される、を確かめる場面を足す。変異（灯の移動の判定を外す）で落ちることを記録する。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|RenderGraphCompileTest)$"`
- stop-when: なし。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。起動画面の電球（Range 10 m）の範囲に自転する大きな球が入るので、そのページは毎フレーム描き直しになる。危険地帯（GPU の資源の寿命）。 2026-10-08 親: 評価の 2 周目の差し戻し（`NEXT_FINDINGS.md` の反復 10。先頭の灯を除いて残る灯を詰めると、移し替えで捨てる旧領域の割り当て済みページが `released`・`point_released` に数えられない）を直すために todo に戻した。done-when の (3) の統計が返却数と一致すること、`VirtualShadowMapVulkanTest` に先頭の灯を除く場面（空きへの返却数と解放の統計の一致、残る灯の描き直し 0）を足すことが残り。

## VTG9-VSM-POINT-GPU-TIME: 夜のキューブと点光源の VSM の GPU 時間を測り、ページの数と溢れを確かめる
- status: done
- done-when: RelWithDebInfo の `-GpuTimingFrames 300` で、夜の起動画面（既定・近接・低角度）と夜の負荷モード 300 個（既定の視点）を `-PointShadowMethod Cube` と `-PointShadowMethod Vsm` の 2 通りで測り、フレーム GPU・`ShadowMapPass`・`VirtualShadowMapPass` とその内訳の区間・照明の区間の中央値（撮影の `metrics.json` の `gpu_timing[].gpu_frame_ms_median`・`pass_median_ms` から。trace.csv を自前で集計しない）と、`VSM_POINT`・`VSM_PAGES`（点光源の分）・`VSM_RASTER`・`VSM_MEGA_CULL`・`VSM_CACHE` の値を表にして PROGRESS に書く。キューブとの差は、フレーム全体の差（`gpu_frame_ms_median`）と、パスの合計の差（`pass_median_ms` の一番上の区間の中央値の合計）を並べて表で示す。撮影の直前に、Game を動かしていない状態の GPU の利用率（`nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader -lms 500` の 10 回の最大）を記録して表に入れる（他のアプリが GPU を使うとフレーム全体の差がぶれる）。VSM の 4 run で 3 種の overflow が全行で 0 であることを確かめる。VSM の撮影の PNG を開き、電球の影（球・岩・見本の球・小屋）が欠け・ずれ・面の継ぎ目・ページの継ぎ目なく見えることを確かめる（壊れて見えるときだけキューブの PNG との画素の差を調べる）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-cube -Configuration RelWithDebInfo -Night -GpuTimingFrames 300 -PointShadowMethod Cube`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-vsm -Configuration RelWithDebInfo -Night -GpuTimingFrames 300 -PointShadowMethod Vsm`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-cube-stress -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Cube -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-GPU-TIME-vsm-stress -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Vsm -ExtraGameArguments --stress-mega-instances=300`
- stop-when: VSM のフレーム GPU の中央値が、どれかの視点で 16.6 ms（60fps の 1 フレーム）以上の場合は、表と重い区間を記録して止める（対策の項目を TASKS.md に足す）。溢れが 0 にならない場合、影が壊れて見える場合は、記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。速度の項目の GPU 時間の撮影。`-GpuTimingFrames` は `-Deterministic` と併用できない。夜は太陽が無いので、太陽の VSM のページは 0 になるはず（値を表に入れる）。 2026-10-08 ユーザーの判断: キューブとの差の 2 ms は計画書・段の受入れに無い止め条件の目安だったので合否から外し、GPU 時間は表で示す（フレーム全体の差とパスの合計の差を並べる）。異常に重いときの見張りとして 16.6 ms（60fps の 1 フレーム）を残す。今のコード（CULL-PERF・DRAW-PERF の後）で 4 run を測り直す。
- 結果: 2026-10-08 完了（評価の差し戻しで、各視点の撮影の直前に Game を止めた状態の GPU の利用率を測って 8 run に撮り直した）。RelWithDebInfo・`-GpuTimingFrames 300`・夜の 8 run（キューブ・VSM × 既定・近接・低角度・負荷の既定）。VSM のフレーム GPU の中央値は 2.30〜2.54 ms（通常 3 視点）・5.41 ms（負荷）で 16.6 ms を大きく下回る。キューブとの差は、フレーム全体で通常 +0.22〜+0.47 ms・負荷 +1.15 ms、パスの合計で通常 +0.30〜+0.47 ms・負荷 +1.02 ms。撮影直前の利用率の最大は 23〜35%。overflow（`VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL`）は VSM の 4 run の全行で 0。VSM の PNG 4 枚に影の欠け・ずれ・継ぎ目は見えない。表と測定の条件は PROGRESS.md の「段9 VTG9-VSM-POINT-GPU-TIME」。

- 結果: 2026-10-08 完了。実装は fcaee6f0。新しい文面での測り直し（RelWithDebInfo・夜・負荷 300 個・既定の視点・`-GpuTimingFrames 300`）で `VsmCullMega` 0.174 ms（測定前 11.72）、フレーム GPU の中央値 6.627 ms（16.6 ms 未満）。VSM の 3 種の overflow は全行 0、`failures` は空。Debug ビルドと ctest の 4 件（VirtualShadowMapVulkanTest・VirtualShadowMapClipmapTest・VirtualShadowMapPointTest・RenderGraphCompileTest）が通った。

## VTG9-VSM-POINT-CULL-PERF: 負荷モードの点光源の VSM のカリング（VsmCullMega）を縮める
- status: done
- done-when: 夜の負荷モード 300 個（既定の視点）の RelWithDebInfo の `-GpuTimingFrames 300` で、`-PointShadowMethod Vsm` の `VsmCullMega` の中央値が測定前の 11.72 ms から縮み、フレーム GPU の中央値が 16.6 ms 未満（測定前は 17.30 ms。キューブは 3.68 ms）。キューブとの差は表で示す（通常の 3 視点は VTG9-VSM-POINT-GPU-TIME の表で示す）。(1) まず `VsmCullMega` の時間を支配するものを切り分ける（点光源の 36 スライスぶんのワークグループの数・インスタンスの判定で落ちる割合・クラスタごとの判定の中身。区間を足して測る）。(2) その上で縮める。案: インスタンスと面の組を先に 1 回で絞り、通った組のワークグループだけを間接 dispatch で出す・灯の Range と面の錐で面ごとにインスタンスを落とす・止まったインスタンスは前のフレームの選び方を引き継ぐ。選んだクラスタと描かれるページは今と同じ（`VirtualShadowMapVulkanTest` の J・J4・J5・K・R が書き換えなしで通る）。VSM の 4 run の 3 種の overflow が全行 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-CULL-PERF-vsm-stress -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Vsm -ExtraGameArguments --stress-mega-instances=300`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|VirtualShadowMapPointTest|RenderGraphCompileTest)$"`
- stop-when: フレーム GPU が 16.6 ms 未満に届かない場合は、切り分けの測定と試した案の結果を記録して止める。選ばれるクラスタ・描かれるページが変わる案は採らない（変わるなら止めて理由を記録する）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 VTG9-VSM-POINT-GPU-TIME の測定から追加（`PROGRESS.md` の同名の節に表）。直ったら VTG9-VSM-POINT-GPU-TIME を `todo` に戻して 4 run を測り直す。負荷モードは最悪の場面で、起動画面の既定・近接・低角度は +0.4〜0.5 ms で済んでいる。危険地帯（GPU のカリングのシェーダー）。 2026-10-08 ユーザーの判断: キューブとの差の 2 ms は計画書・段の受入れに無い止め条件の目安だったので合否から外し、GPU 時間は表で示す（フレーム全体の差とパスの合計の差を並べる）。異常に重いときの見張りとして 16.6 ms（60fps の 1 フレーム）を残す。実装は fcaee6f0 に入っていて、残りはこの文面での測り直しと評価。

## VTG9-VSM-POINT-DRAW-PERF: 負荷モードの点光源の VSM の描画（VsmDraw）と段の固定費を縮める
- status: done
- done-when: 夜の負荷モード 300 個（既定の視点）の RelWithDebInfo の `-GpuTimingFrames 300` を、`-PointShadowMethod Cube` と `-PointShadowMethod Vsm` の対で 2 回撮り、2 対のどちらでも VSM のフレーム GPU の中央値（撮影の `metrics.json` の `gpu_timing[].gpu_frame_ms_median`）が 16.6 ms 未満で、`VsmDraw` が開始時の 1.15 ms から縮む。キューブとの差は、フレーム全体の差（`gpu_frame_ms_median`）と、パスの合計の差（`pass_median_ms` の一番上の区間の中央値の合計）を並べて表で示す。（開始時は静かな対で +2.27 ms。`VirtualShadowMapPass` 1.82 ms のうち `VsmDraw` 1.15・`VsmAllocate` 0.23・`VsmCullMega` 0.17・`VsmMark` 0.14・`VsmExpand` 0.10）。(1) まず `VsmDraw` の時間を支配するものを切り分ける（頂点の処理か断片の `atomicMin` か。影の塊 × ページのインスタンス約 11.4 万のうち、塊の三角形が実際に触れるページの割合。一時的な変異の run や区間で測り、表にして PROGRESS に書く）。(2) その上で縮める。案: 塊の境界から描くページの範囲をきつくしてインスタンスを減らす・ページの外の三角形を頂点シェーダーで落とす・断片で手前でなければ `atomicMin` を出さない（他の断片がアトミックに書く語を通常の読み取りで読まない。VTG8-FIX-ATOMIC-READS の規則。原子的な読み取りを使う）・VSM の段の固定費（`VsmAllocate`・`VsmMark`・`VsmExpand` の dispatch）をまとめる。選ばれるクラスタ・LOD・描かれるページ・物理ページの texel の深度は今と同じ（`VirtualShadowMapVulkanTest` の J・J4・J5・J6・K・R が書き換えなしで通る）。通常の 3 視点（夜の既定・近接・低角度）の VSM のフレーム GPU とキューブとの差も表で示す。VSM の run の 3 種の overflow（`VSM_PAGES`・`VSM_RASTER`・`VSM_MEGA_CULL`）が全行 0。一時的な変異・区間は戻してからコミットする（区間を残す場合は RelWithDebInfo の計測の区間として残す）。撮影の直前に、Game を動かしていない状態の GPU の利用率（`nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader -lms 500` の 10 回の最大）を記録して表に入れる（他のアプリが GPU を使うとフレーム全体の差がぶれる）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-cube-stress-1 -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Cube -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-vsm-stress-1 -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Vsm -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-cube-stress-2 -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Cube -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-vsm-stress-2 -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -PointShadowMethod Vsm -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-cube -Configuration RelWithDebInfo -Night -GpuTimingFrames 300 -PointShadowMethod Cube`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DRAW-PERF-vsm -Configuration RelWithDebInfo -Night -GpuTimingFrames 300 -PointShadowMethod Vsm`
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|VirtualShadowMapPointTest|RenderGraphCompileTest)$"`
- stop-when: 2 対のどちらかで VSM のフレーム GPU が 16.6 ms 以上の場合は、切り分けの表と試した案の結果を記録して止める。選ばれるクラスタ・LOD・描かれるページ・texel の深度が変わる案は採らない（影の細かさとキューブとの一致に関わる。要るなら止めて理由を記録する）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- 結果: 2026-10-08 完了。実装は 933c1fdf。2 ms の条件を外した今の文面で測り直した（RelWithDebInfo・夜・負荷 300 個・既定の視点・300 フレーム）。VSM のフレーム GPU の中央値は 2 対とも 16.6 ms を大きく下回り（6.24・5.45 ms）、`VsmDraw` は 1.15 → 0.51 ms に縮んだ。キューブとの差は負荷の 2 対で +0.82・+1.16 ms（パスの合計の差 +0.91・+1.14 ms）、通常の 3 視点は +0.16〜+0.70 ms。overflow は全行 0。表は PROGRESS.md の「段9 VTG9-VSM-POINT-DRAW-PERF の測り直し」。
- notes: 2026-10-08 親（VTG9-VSM-POINT-CULL-PERF の停止から追加。カリングは 11.72 → 0.17 ms に縮んだが、残りは描画と段の固定費）。親の測り直し（`.harness/runs/startup-capture/VTG9-PARENT-stress-{Cube,Vsm}-1`）は +1.62 ms だったが、キューブの run の `MegaGeometry` が 0.63 ms 高く（VSM と無関係のぶれ）、ぶれを除くと約 +2.25 ms で変わらない。そのため 2 対で確かめる。負荷モードの描き直しは 183 ページのうち 155 ページで、電球の範囲の自転する大きな球が毎フレーム無効にする。VSM の構成でキューブを描かない案は ForwardPass（半透明）がキューブを読むので採らない。危険地帯（GPU の描画のシェーダー・ページの資源）。 2026-10-08 親: 1 回目の停止の推奨（条件を緩める）は採らない。実装は 933c1fdf に入っていて、残りは静かな GPU での測り直し。1 回目の測定は、17:54 以降に他のアプリが GPU を使っていた（Game を動かしていない状態で利用率 31〜40%）ので汚れている。フレームの区間の外の時間（`gpu_frame_ms_median` − 一番上の区間の中央値の合計）は、17:24〜17:54 の静かな run ではキューブ・VSM とも 0.02〜0.08 ms だったが、18 時以降は 0.3〜1.9 ms で run ごとに大きくぶれた。静かな run でも、VTG9-VSM-POINT-CULL-PERF の後の VSM は区間の外の時間が約 0.35 ms 多い（17:47〜17:51 の VSM 0.38 ms、17:48 のキューブ 0.03 ms）。この分は測り直しの表に記録する（既知の限界）。 2026-10-08 ユーザーの判断: キューブとの差の 2 ms は計画書・段の受入れに無い止め条件の目安だったので合否から外し、GPU 時間は表で示す（フレーム全体の差とパスの合計の差を並べる）。異常に重いときの見張りとして 16.6 ms（60fps の 1 フレーム）を残す。実装は 933c1fdf に入っていて、残りはこの文面での測り直しと評価。

## VTG9-VSM-POINT-DEFAULT-ON: 起動画面の点光源の影を既定で VSM にする
- status: done
- done-when: Game の起動画面の点光源の影を既定で VSM にする（`--point-shadow-method=cube` で戻せる。`Scripts/CaptureStartupScene.ps1` の `-PointShadowMethod` の既定も Vsm）。検証アプリは引数を変えずにキューブ（と CSM）のまま: golden 4 本が基準画像と閾値を動かさずに通る。検証レイヤー付きの Debug の Game で、夜の起動画面（既定・近接・低角度）と夜の負荷モード 300 個（既定の視点）を `-ShadowProbe` 付きで撮り（下の verify。球の自転を止める）、`vulkan_validation` の `error_count` が 0、点光源の `SHADOW_PROBE_AGREE` の ratio が 4 run すべてで 0.98 以上、VSM の `mean_texel_mm` が同じ run のキューブ以下、VSM の溢れが 0 であることを確かめる。撮影の PNG を開き、天球・地面・球・岩・小屋・見本の帯・発光の球と電球の影が欠けなく見えることを PROGRESS に書く。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest CameraViewConstantsTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|VirtualShadowMapPointTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DEFAULT-ON-validation -Configuration Debug -Deterministic -Night -ShadowProbe -SphereSpin Off`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT-DEFAULT-ON-validation-stress -Configuration Debug -Deterministic -Night -ViewNames default -ShadowProbe -SphereSpin Off -ExtraGameArguments --stress-mega-instances=300`
- stop-when: golden が基準を外れる場合は、差と原因を記録して止める。検証レイヤーのエラーが直せない場合、ratio が 0.98 に届かない場合、mean_texel_mm がキューブを超える場合は、測定値を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Private/Engine, Assets/Shaders, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。既定の描画経路を変える項目の golden の ctest と検証レイヤー付き Debug の実行。影の測定は止まった物を前提にするので、段8の受入れと同じく大きな球の自転を止めて測る（自転する球の細かい影は VSM だけが描き、球と一緒に動く）。Debug の GPU 時間は参考にしない。危険地帯（既定の描画経路）。
- 結果: 2026-10-08 完了。`BootConfig::DefaultPointShadowMethod`（構造体の既定はキューブ）を足し、`ApplicationProcessor` が `--point-shadow-method` の既定をこの値にする。`GameBoot.cpp` が VSM を設定する。検証アプリは引数を変えずにキューブのまま。`CaptureStartupScene.ps1` の `-PointShadowMethod` の既定を Vsm にした。ビルド・ctest 8 本（golden 4 本含む。基準画像・閾値は変更なし）・検証レイヤー付き Debug の撮影 4 run（`error_count` 0・溢れ 0）が通った。点光源の ratio は 0.9999 / 0.9949 / 0.9998 / 0.9935、VSM の mean_texel_mm は 7.7 / 3.0 / 2.8 / 8.1 mm（キューブは 19.8 / 16.6 / 18.7 / 19.9 mm）。詳細は PROGRESS.md。

## VTG9-STRESS-SHADOW-SKIPS: 負荷モードで残る CSM とキューブの影の描画の省略をなくす
- status: done
- done-when: 負荷モード（`--stress-mega-instances=300`）で、半透明・ボリュームのために残る CSM の描画の `DynamicUniformAllocator` の `Out of slots (1024/1024)` による MegaGeometry の影の省略（既定の視点で 343 回）と、キューブの 1 面あたりの上限（`PointShadowMaxMegaDrawsPerFace` = 8）による省略（1032 回）を 0 にする（スロットの数を投影物と面の数から決めて足りるようにする、または上限を投影物の数に合わせる）。省略の警告の数をログから数え、RelWithDebInfo の負荷モード 300 個（昼の既定の視点と夜の既定の視点）で 0 であること、フレーム GPU の中央値の変化（`-GpuTimingFrames 300`）を PROGRESS に書く。RenderGraphCompileTest か CPU のテストで、投影物が 300 個・4 カスケードでもスロットが足りることを確かめる。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-STRESS-SHADOW-SKIPS-day -Configuration RelWithDebInfo -SunElevations 45 -ViewNames default -GpuTimingFrames 300 -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-STRESS-SHADOW-SKIPS-night -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -ExtraGameArguments --stress-mega-instances=300`
- stop-when: 省略をなくすとフレーム GPU が 3 ms 以上増える場合は、測定値を記録して止める（省略を既知の限界に残すかは親が決める）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。段9の受入れ「8GB 級の上限での全体の負荷モード」で影の描画を省いたまま測らないため。速度の項目の GPU 時間の撮影。危険地帯（描画パス・GPU の資源）。

## VTG9-STRESS-ALL: 8GB 級の上限で、テクスチャ・ジオメトリ・影の全体の負荷モードを昼と夜に通す
- status: done
- done-when: `--vram-budget-mb=6500`（段2・段5と同じ 8GB 級の模し方）で、テクスチャの負荷（`-StressTextures`）・ジオメトリの負荷（`-StressGeometry`）・MegaGeometry の負荷（`--stress-mega-instances=300`）を同時に有効にした全体の負荷モードを、昼（太陽45°、太陽の VSM）と夜（点光源の VSM）の既定の視点で RelWithDebInfo の `-GpuTimingFrames 300` で撮る。3 つの負荷を同時に有効にできない（引数がぶつかる・カメラの軸が片方だけになる・起動が失敗する）場合は、同時に有効にできるように直す（カメラは両方の負荷の物が映る視点）。ログで、`VRAM_POOLS` の vt_used ≤ vt_target・geometry_used ≤ geometry_target（追い出しは起きてよい）、`VRAM_BUDGET` の heap_usage が cap（6500 MB）以下、VSM の 3 種の overflow が 0、影の描画の省略が 0 であることを確かめ、PNG を開いて穴・欠け・テクスチャの解像度の崩れが無いことを確かめる。フレーム GPU の中央値・p95 と、VT・ジオメトリ・VSM の VRAM を表にして PROGRESS に書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-STRESS-ALL-day -Configuration RelWithDebInfo -SunElevations 45 -ViewNames default -GpuTimingFrames 300 -VramBudgetMb 6500 -StressTextures -StressGeometry -ExtraGameArguments --stress-mega-instances=300`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-STRESS-ALL-night -Configuration RelWithDebInfo -Night -ViewNames default -GpuTimingFrames 300 -VramBudgetMb 6500 -StressTextures -StressGeometry -ExtraGameArguments --stress-mega-instances=300`
- stop-when: heap_usage が cap を超える、または目標を超えて溢れる場合は、溢れたプールと量を記録して止める（予算の割り振りの見直しの項目を足す）。同時に有効にするのに 1 反復を超える変更が要る場合は、要る変更を記録して `VTG9-STRESS-ALL-COMBINE` として TASKS.md に足す。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Private/Engine, Game, Scripts/CaptureStartupScene.ps1, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした。計画書 §1「8GB 級の GPU で収まる。開発機では `--vram-budget-mb` の人工的な上限で確かめる」、§5 の段9の受入れ）。段8で VSM の確保量（約 426 MB）は固定の取り置きとして割り振れる量から引くようにした（`VideoMemoryBudgetManager`）。速度の項目の GPU 時間の撮影を兼ねる。危険地帯（メモリ・予算）。

## VTG9-ACCEPT: 段9（VSM 点光源）と全体の受入れを記録する
- status: done
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` に「## 段9（VSM 点光源と全体）」の節を段8の節と同じ構成で足し、全体のまとめ（段1〜9 の受入れの数値の一覧）を書く。(1) 起動画面の朝・昼・夕・夜（既定の経路）の撮影を開いて確かめた所見。(2) 夜の電球の影: 夜の既定・近接・低角度を `-Deterministic -Night -OrbitDegreesPerSecond 20 -OrbitRenderedFrames 240,320,400 -ShadowProbe`（球の自転を止める）で撮り、同じ run のキューブと VSM の `mean_texel_mm`・`partial_ratio`・`mean_abs_delta`・`flip_ratio`・一致を表にする。判定: 細かさ = VSM の `mean_texel_mm` と `partial_ratio` が 3 組すべてでキューブ以下。ちらつき = VSM の `mean_abs_delta` と `flip_ratio` が 3 組すべてでキューブ以下（両方が 0.001 未満の組は同等とみなす）。一致 = 3 組すべてで 0.98 以上。(3) 全体の負荷モード: VTG9-STRESS-ALL の表（予算の内側・溢れ 0・影の省略 0・穴なし）。(4) GPU 時間は VTG9-VSM-POINT-GPU-TIME と VTG9-STRESS-ALL の表。(5) golden（基準画像を動かしていないこと）と関係する ctest。(6) 既知の限界。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest CameraViewConstantsTest RenderingGoldenImageTest RenderResourcesDomainContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VirtualShadowMapVulkanTest|VirtualShadowMapClipmapTest|VirtualShadowMapPointTest|VideoMemoryBudgetManagerTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-ACCEPT -Configuration RelWithDebInfo -Deterministic -SunElevations 10,45,3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-ACCEPT-night -Configuration RelWithDebInfo -Deterministic -Night`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-ACCEPT-night-orbit -Configuration RelWithDebInfo -Deterministic -Night -OrbitDegreesPerSecond 20 -OrbitRenderedFrames 240,320,400 -ShadowProbe -SphereSpin Off`
- stop-when: 判定の行が満たせない場合は、測定値を記録して止める（判定を緩めない）。
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段9の開始時に詳しくした）。ランナーが止まった後に親が行う（backlog）。段の区切りの評価（Sol）にかける。 2026-10-08 親: 受入れを記録した（`Docs/RenderingValidation/VirtualizationAcceptance.md` の「段9（VSM 点光源と全体）」と「段1〜9 の受入れのまとめ」）。検査の出力は `.harness/runs/vtg9-accept/`。

## VTG9-FIX-NIGHT-ENV-SUN: 夜の静的な環境光から夕日を除き、大きな球の暗い側の橙赤の粒をなくす
- status: done
- done-when: (1) 静的な環境光の放射輝度の上限 `StaticEnvironmentMaxRadiance`（HDR の値。0 は上限なしで既定）を、`StaticEnvironmentIntensityScale` と同じ経路（`RenderWorld` → `RenderingCoordinator` → FramePacket の `SceneProxy` → `LightingPass` の照明のパラメータ）で渡す。(2) 空が無効で静的な HDR を使うフレームで、照明（`lighting.frag`）と半透明（`forward_transparent.frag`）が静的な環境を読む所（鏡面の IBL の前計算の値と、背景に描く環境）では、倍率を掛ける前の色の最大の成分が上限を超えたら、色相を保ったまま上限まで縮める。上限なし（既定）・空が有効なフレーム・検証用の環境では何もしない。ほかに静的な環境を読むシェーダー（RTGI の外れた光線など）があれば同じ扱いにするか、読まないことを確かめ、どちらかを PROGRESS に書く。(3) Game の `--night` で上限を 10 にする（`Rendering3DTestRoutine.cpp` の `kNightStaticEnvironmentIntensityScale` の隣に根拠を書く: `grasslands_sunset_4k` の空の輝度の上位 0.1% が約 6.0、仰角 3.3° の太陽が約 15638（RGB 45824 : 8192 : 512）、10 を超える画素は 817 で全エネルギーの約 3.7%）。(4) 粒の数を測る: 夜の近接（`-Deterministic -Night -ViewNames near`）の PNG の、大きな球の暗い側 x 370〜699・y 100〜599 で、R − G > 40 の画素（8 ビットの値）の数。修正前は 223（`.harness/runs/startup-capture/VTG9-ACCEPT-night/near-night.png`）、粒の無い RTGI オフの画は 0（`VTG9-DOTS-rtgi-off/near-night.png`）。修正後は 10 未満にし、値を PROGRESS に書く。PNG を開いて、球の暗い側に粒が見えないこと、夜の既定・近接・低角度で天球・地面・球・岩・小屋・電球と電球の影が欠けなく見えることを確かめる。(5) 検証アプリは上限を使わないので golden 4 本が基準画像・閾値を動かさずに通る。検証レイヤー付きの Debug の夜で `vulkan_validation.error_count` が 0。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|VirtualShadowMapVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-FIX-NIGHT-ENV-SUN-validation -Configuration Debug -Deterministic -Night -ViewNames near`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-FIX-NIGHT-ENV-SUN-night -Configuration RelWithDebInfo -Deterministic -Night`
- stop-when: 上限を入れても粒が 10 未満にならない（別の原因が残る）場合は、切り分けの撮影と数を記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（ユーザーの指摘: 夜の近接の大きな球の暗い側の赤い点は許容できない）。切り分け: RTGI を切っても（`.harness/runs/startup-capture/VTG9-DOTS-rtgi-off`、3106）、レンズの効果を切っても（`VTG9-DOTS-lens-off`、3190）残り、キューブの影（段8 `VTG8-ACCEPT-r3-night/near-night.png`）でも同じ位置に出る。夜の環境光は夕焼けの HDRI（`SceneView.cpp` の `DefaultEnvironmentMapPath`）を 0.08 倍したもので、沈みかけの太陽が残っている。石の法線の凹凸で鏡面の反射が太陽を向く画素が粒になる（IBL の鏡面は影で隠さない）。拡散の IBL（前計算の放射照度）は太陽の分を含んだままでよい（既知の限界として記録する）。危険地帯（Public API・FramePacket・照明のシェーダー）。 2026-10-08 親: (4) の測り方を直した（done-when を書いた親の定義の誤り）。前の測り方（x 370〜909・y 100〜599、輝度が 9×9 の箱の平均より 10 以上高く R − G > 15）は、粒の無い RTGI オフの画でも 3105 を数え（レンガの凹凸の縁の橙色）、粒を測れていなかった。新しい測り方は修正前 223・粒の無い画 0 で、粒だけを数えることを確かめた（修正後の 2e74630c の画は 0）。粒の経路は RTGI の外れた光線が拾う HDRI の太陽で（RTGI を切ると 0）、IBL の鏡面ではなかった。

## VTG9-FIX-LIGHTING-PER-EXECUTE: 照明のパスが 1 フレームに複数回描くとき、Execute ごとに自分の descriptor set と定数・storage buffer を使う
- status: done
- done-when: `LightingPass` が 1 フレームの中で複数回 Execute される（同じ SceneView の複数のビューポート。`RenderFrameExecutor.cpp` のビューポートの繰り返し）とき、各 Execute が自分の descriptor set と、Execute ごとに書くバッファ（`m_LightDataBuffer`・`m_LightArrayBuffer`・`m_VsmSampleBuffer`・`m_VsmPointSampleBuffer`・`m_VsmSliceBuffer`）の組を使い、同じフレームで先に記録した描画が読む資源を上書き・再更新しない。組はフレームの通し番号（`ResolveRenderFrameSerial`）が変わったら先頭から使い直す（`VulkanCommandList::Begin` が前の提出のフェンスを待ってから記録する前提。前提をコードで確かめて PROGRESS に書く）。組は要るときに作り、上限（4）を超える Execute はエラーを 1 回出して描かない。1 ビューポートの描画（起動画面）は今と同じ結果。検証: Vulkan の装置を使うテスト（`RenderGraphCompileTest` か照明の既存のテスト）で、同じフレームに別のカメラの 2 つのビューポートぶん照明を記録し、2 回の描画が別の descriptor set を束ねること、各組のバッファの中身がそれぞれのカメラ・VSM の値であること、2 回目の後も 1 回目の組のバッファが 1 回目の値のままであることを確かめる。変異（組を常に先頭にする）でそのテストが落ちることを記録する。golden 4 本が基準画像・閾値を動かさずに通る。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RTGIDiffuseIndirectVulkanTest|VirtualShadowMapVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- stop-when: 1 フレームの記録が前のフレームの提出の完了を待たずに始まる（フレームをまたいで組が使われている）と分かった場合は、記録して止める（フレームをまたぐ版の持ち方が別に要る）。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-08 親（段の区切りの評価の指摘: 点光源の VSM のパラメータ・スライスの表が 1 本のバッファで Execute ごとに上書きされ、同じ View の 2 つ目のビューポートが提出前の 1 つ目のパラメータを上書きする。同じ descriptor set の再更新は先に記録したコマンドを無効にする）。照明の定数（`m_LightDataBuffer`）・ライトの配列も同じ形なので組に含める。SSAO・SSR・Bloom・トーンマップ・ボリュームも 1 本の定数バッファを Execute ごとに書く形だが、段9の差分ではないので直さず、既知の限界として PROGRESS に書く。危険地帯（RenderThread・GPU の資源の寿命・descriptor set）。

## VTG9-FIX-NIGHT-RTGI-FIREFLIES: 夜の見本の球の暗い側に残る RTGI の橙の粒をなくす
- status: done
- done-when: (1) 切り分け: 夜の低角度で、見本の小さな球の暗い側に出る橙の粒が、RTGI のどの光線の値から来るか（発光の球＝電球に当たった光線・照らされた地面や物に当たった光線・環境に抜けた光線）を、一時的な計装・変異の撮影で確かめ、PROGRESS に表で書く（計装・変異は戻してからコミットする）。(2) 原因に応じて直す。電球のように点光源と発光の物体が同じ光を表すとき、RTGI の光線が発光の面に当たって拾う値は、点光源の直接光と二重に数えることになる。そうであれば、点光源の代わりの発光の面の値を RTGI で拾わない（または点光源と重ならない形にする）。それで足りなければ、RTGI の 1 本の光線の値に上限（ホタルの抑え）を入れ、上限の値と根拠を PROGRESS に書く。(3) 粒の数を測る: 夜の低角度（`-Deterministic -Night -ViewNames low`）の PNG の見本の球の中 x 240〜519・y 370〜449 で、輝度（0.2126R + 0.7152G + 0.0722B）の 7×7 の中央値（PIL の `MedianFilter(7)`、8 ビットの輝度に掛ける）を m としたとき、m ≤ 25 かつ 輝度 − m > 12 かつ R − G > 20 の画素（暗い側の粒）の数を 10 未満にする。修正前は 32（`.harness/runs/startup-capture/VTG9-ACCEPT-r2-night/low-night.png`）、RTGI を切った画は 0（`VTG9-DOTS2-rtgi-off/low-night.png`）。同じ領域の m > 25 の画素（照り返しの面）の数と、球の下の地面の帯（y 450〜469）の同じ条件の画素の数も記録する（判定には使わない）。夜の近接の暗い側（x 370〜699・y 100〜599）の R − G > 40 の画素は 0 のまま。(4) 夜の 3 視点と昼の 3 視点（太陽 45°）の PNG を開き、見本の球に RTGI の照り返しが残り（RTGI を切った画のように真っ黒にならない）、天球・地面・球・岩・小屋・電球と電球の影が欠けなく見えることを確かめる。昼の 3 視点の画面の平均輝度の修正前（`VTG9-ACCEPT-r2` の `default-sun45`・`near-sun45`・`low-sun45`）からの変化を PROGRESS に書く。golden 4 本が基準画像・閾値を動かさずに通る。検証レイヤー付きの Debug の夜で `vulkan_validation.error_count` が 0。
- verify: `cmake --build build --config Debug --target Game RenderGraphCompileTest RHITextureUpdateVulkanTest RenderingGoldenImageTest RTGIDiffuseIndirectVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderGraphCompileTest|RTGIDiffuseIndirectVulkanTest|VirtualShadowMapVulkanTest|RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest|RenderingGoldenIndoorGBufferFallbackVulkanTest|RenderingGoldenOutdoorGBufferFallbackVulkanTest)$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-FIX-NIGHT-RTGI-FIREFLIES-validation -Configuration Debug -Deterministic -Night -ViewNames low`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-FIX-NIGHT-RTGI-FIREFLIES-night -Configuration RelWithDebInfo -Deterministic -Night`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-FIX-NIGHT-RTGI-FIREFLIES-day -Configuration RelWithDebInfo -Deterministic -SunElevations 45`
- stop-when: 粒が RTGI の光線の値から来ていない、または (2) の直し方で暗い側の粒が 10 未満にならず、照り返しを消さずに抑える方法が無い場合は、切り分けを記録して止める。
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Rendering3DTest, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-09 親（ユーザーの指摘「夜の赤い点は許容できない」の続き）。VTG9-FIX-NIGHT-ENV-SUN の後も、夜の低角度で見本の小さな球の暗い側に橙の点が残る（夕日の修正の前後で同じ）。RTGI を切ると消えるが、球の照り返しも消える（見本の球は大きな球の影の中にあり、RTGI の照り返しだけで見えている）。測り方は修正前 92・RTGI を切った画 0 で校正した（照り返しの面の粒立ちは数えない）。危険地帯（RTGI のシェーダー）。 2026-10-09 親: (3) の領域を球の中（y 370〜449）に直した（done-when を書いた親の定義の誤り）。前の領域（y 370〜469）は球の下の地面の帯を含み、球の間から見える照らされた地面の楔の先（RTGI の正しい照り返し。RTGI を切ると楔ごと消えるので、RTGI を切った画との校正では見分けられなかった）を粒として数えていた（修正前 92 のうち帯が 60）。球の中の粒は修正前 32・RTGI を切った画 0・0eb21517 の画 2。

## FIX-MEGA-LOD-SHADING: MegaGeometry の粗い段の陰影が LOD0 より暗く・柔らかくなるのを直す
- status: backlog
- done-when: 変位のある大きな球の LOD1〜LOD4 で、目地の陰影（法線）が LOD0 と見分けがつかない（既定・低角度の視点の拡大画像で、LOD0 の参照との球の領域の平均の差が撮り直しの雑音と同じ程度）。形の誤差の閾値（1画素）は変えない。
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-MEGA-LOD-PERF の撮り比べで見つけた。形の誤差が1画素未満でも、低角度（6 m）の LOD1（0.58画素）は LOD0 より目地が柔らかく（16超の画素 0.8%）、既定（10 m）の LOD4（0.87画素）は球の平均で 6〜10/255 暗い。変更前の段の選び方でも同じ段で、起こっていた差。粗い段の頂点の法線が変位の細部を平均してしまうのが原因と見られる。

## FIX-MEGA-SHADOW-LOD-LOADED: 読み込むモデルのMegaGeometryに影だけの粗い段を作る
- status: backlog
- done-when: 読み込むモデル（岩・小屋など）の MegaGeometry に、GBuffer のクラスタは元のまま保ったまま、影（CSM・点光源のキューブ）だけに使う粗い段を作り、カスケードの1テクセルに見合う段を選ぶ。読み込みの時間・メモリの増分を記録する。
- paths: Library/Core/Private/Resource, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SS-MEGA-LOD-PERF（2026-10-03）から分けた。`ModelStaging.cpp` は `bBuildLODHierarchy = false` で、階層を作ると GBuffer のクラスタも DAG に置き換わる（`MegaGeometryResourceStore.cpp` の `uploadClusters = &lodHierarchy.AllClusters`）ため、影だけの段には別の設計が要る。近接の ShadowMapPass は 0.643 ms で、岩（66,122三角形）・小屋（4,281三角形）×4カスケードの寄与は小さい。危険地帯（アセットの読み込み・リソースの寿命）。

## SS-ACCEPT-PERF-REMEASURE: 起動画面のGPUの時間を競合なしで12視点 × 2回測り直す
- status: backlog
- done-when: GPU を他のアプリと共有しない状態（Game が動いていない時点の GPU の使用率が数%以下であることを計測の前後に記録する）で、RelWithDebInfo の `-GpuTimingFrames 600` を朝・昼・夕・夜 × 3視点 × 2回回し、`Docs/RenderingValidation/StartupSceneAcceptance.md` の「テクスチャのミップ・MegaGeometry の影・高ポリの球」の節の GPU の表と予算の判定（超えたフレームがあればパスごとの内訳）を置き換える。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- paths: Docs/RenderingValidation, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-PERF の計測は、ユーザーの別のアプリ（javaw）が GPU を36〜40%使う中で回った。PC を占有する重い処理なので、ユーザーが PC を使ってよいと言ったときだけ todo に戻す。

## FIX-NIGHT-SPHERE-LONG-RUN: 夜の大きな球の光源と反対側が、長く描くと明るくなるのを調べる
- status: backlog
- done-when: 夜の近接視点で、大きな球の光源と反対側の明るさが60描画フレーム目と600描画フレーム目で物理的に説明できる範囲でそろう（原因が RTGI の履歴なら、間接光の大きさを光源側との比で確かめる）。原因と直し方を記録する。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-03 SS-ACCEPT-PERF で見つけた。近接・夜の球の反対側 / 光源側の8bit輝度は、60フレーム目の Debug・Release の撮影で 0.02〜0.03、600フレーム目の RelWithDebInfo の GPU 計測の撮影で 0.80〜0.84（`.harness/runs/20261003-181031/verify-SS-ACCEPT-PERF-14.txt`）。変更前（`b347eb7`）の `SS-ACCEPT-gpu-night` でも 0.48〜0.61 で、前からある。構成の違いかフレーム数の違いかはまだ切り分けていない。

## FIX-NEURAL-BRDF-STREAK: ニューラルBRDFの直接光が点光源の近くに作る筋を直す
- status: backlog
- done-when: 通常表示の直接光（ニューラルBRDF）と解析BRDFの差が、Cornellの天井のように点光源に近い粗い面でも筋を作らない。原因（学習範囲外の入力、かすめ角の鏡面項など）を特定し、学習データか評価の範囲を直すか、範囲外では解析BRDFへ戻す。
- verify: 同じCornell条件で、ニューラルBRDFの通常表示と解析BRDF（検証mode 254の直接光）の画素差を比べる専用テストを追加して通す。
- stop-when: 学習済み重みの再生成が必要でデータがない場合は、範囲外の入力で解析BRDFへ戻す方針をユーザーへ提案する。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。点光源だけの画像でラスタにだけ2画素幅の筋が出る。

## FIX-SSAO-ROOM-SCALE: 部屋の大きさのシーンでSSAOが壁をほぼ全遮蔽にする原因を直す
- status: backlog
- done-when: Cornell（5.5 m四方）の壁でSSAOがほぼ0になる原因（半径・bias・深度の再構成の尺度など）を特定して直し、IBL fallbackの間接光が壁で消えない。既存goldenは変えない。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenが変わる場合は基準の更新を提案して止まる。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R6-P7で発見。RTGIには`81bbf24`でSSAOを掛けないようにしたが、IBL fallbackは引き続きSSAOを掛ける。
- result: SS-GTAO（2026-10-02）で置き換わった。原因は旧SSAOがワールド空間の法線をビュー空間の標本へそのまま使い、開けた面も遮蔽に数えていたこと。GTAOはビュー空間の地平線で余弦重みの可視率を求め、半径1 mの外の面を数えないので、Cornellの壁の中ほどの可視率は0.97〜1.00（`RenderingGTAOCornellRoomVulkanTest`）。statusは人の確認のためbacklogのまま。

## FIX-NORMAL-MATRIX-SCALE: 法線行列の小スケール退化判定を尺度不変にする
- status: backlog
- done-when: `MatrixUtils::CreateNormalMatrix`が一様スケール約0.005未満の物体にも逆転置を返し（特異かどうかは尺度に対する比で判定）、R1室内フィクスチャの平面メッシュの頂点法線を幾何と一致させ、PTの閉包命中シェーダから同じ退化規則の写しを外す。R1数値検証、Indoor/Outdoor golden、`PathTracingRasterParityVulkanTest`が変わらず通る。
- verify: `cmake --build build --config Debug --target Game RenderingHdrSceneCaptureTest PathTracingRasterParityVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -L RenderingValidation`
- stop-when: 承認済みgoldenやR1の数値が変わる場合は、基準の更新を提案して止まる。
- paths: Library/Core/Public/Math, Assets/Shaders/PathTracing, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: R7-P3Dで発見。3x3の行列式の絶対値がFLT_EPSILON未満だと単位行列になり、回転した小さな物体の法線が回らない。R1室内の平面はこの規則を前提に頂点法線を+Zへ書き換えているため、PTも同じ規則でラスタと揃えている。

## RTGI-HIT-SPECULAR: RTGIの命中面を光沢のある反射でも照らす
- status: backlog
- done-when: RTGIの命中面の直接光が材質の粗さ・金属度の鏡面葉を含み、R7屋外比較の既知差（夕の緑の球の影側の面。命中点をLambertにしたPTとは一致）が全輸送との比較で縮む。R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RTGIDiffuseIndirectVulkanTest|R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 命中面の材質をRTのsnapshotへ持たせる方法（粗さ・金属度のtextureの扱い）に設計判断が要る場合はユーザーへ戻す。判定の参照の輸送範囲を変える場合もユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差2。命中面の反射率は`3c80458`でinstance色にした。

## RTGI-MULTI-BOUNCE: 接地部の3回目以降のバウンスをRTGIで扱う
- status: backlog
- done-when: 球の下の接地部で、ラスタの間接光と全輸送のPTの差（拡散2バウンスの約1.5倍）が縮む。RTGIの光線数の増え方を記録し、R6・R7の参照比較と既存goldenが通る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R6RTGIPathTracingReferenceVulkanTest|R7OutdoorPathTracingReferenceVulkanTest)$"`
- stop-when: 光線数の増加が大きい、または方式（放射輝度の再利用・probe併用など）の選択が要る場合はユーザーへ戻す。
- paths: Assets/Shaders/RTGI, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: 危険地帯（RTGI）。R7-O3の既知差1。

## FIX-CSM-TERMINATOR: 球の明暗境界でCSMの可視が数画素かけて下がるのを直す
- status: backlog
- done-when: 屋外シーンの球の明暗境界で、ラスタのCSMの可視（検証表示245）がPTの可視（1画素で1→0）に近づき、R7屋外比較で影の縁として除く画素が減る。承認済みgoldenが変わる場合はユーザーの承認を得る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(R7OutdoorPathTracingReferenceVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- stop-when: goldenの再承認が要る場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差3。例: 夕の緑の球で0.96→0.65→0.48→0.18（PTは1→0）。法線方向のずらし（normal offset）や比較の余裕の見直しが候補。

## FIX-GRAZING-IBL-SPECULAR: 斜めから見た地面のIBLの鏡面反射が強すぎる原因を調べる
- status: backlog
- done-when: 昼の屋外シーンの地平線近くの地面で、ラスタのIBLの鏡面反射による間接光（全輸送の約1.5倍）の原因（split-sumの近似、地平線より下の環境、DFGの補償など）を特定し、直すか既知差として根拠を記録する。
- verify: R7屋外比較の`_mean_luminance`と地平線近くの領域の比を開いて確認する。
- stop-when: 承認済みgoldenが変わる場合はユーザーへ戻す。
- paths: Assets/Shaders, Library/Core/Private/Rendering, Test/Core/Rendering
- notes: R7-O3の既知差4。朝・夕は1.07〜1.11倍。

## FIX-MEGAGEOMETRY-RECORD-TEST: RenderGraphCompileTestのMegaGeometryの記録の検査が落ちる原因を直す
- status: backlog
- done-when: `RenderGraphCompileTest`の`MegaGeometryPass::RecordFrameCommand`の検査（2つのMegaMeshを1回のRenderPassで2回描く）が通り、テスト全体が最後まで走る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- stop-when: 検査の期待値そのものを変える必要がある場合は理由を記録する。
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: SS-GTAOの反復（2026-10-02）で見つけた。`RenderGraphCompileTest.cpp:2620`の`BeginRenderPassCount == 1`で止まり、後ろのSSAO・Lightingの検査まで届かない。MegaGeometryの最後の変更は`f63b2fd`（SS-EMISSIVE-PREEXPOSE）で、`RecordFrameCommand`がパイプラインの未準備で早く戻っている可能性がある。

## FIX-R3-DENSITY-GOLDEN: R3のフォグ密度の基準画像が現在の描画と食い違う原因を調べて直す
- status: backlog
- done-when: `RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`の3段階が通るか、食い違いの原因（どの変更で変わったか）と再承認の根拠が`R3Acceptance.md`に記録される。
- verify: `build\Test\Core\Rendering\Debug\RenderingGoldenImageTest.exe --scene=outdoor --capture-source=back-buffer --r3-scenario=density-sweep`
- stop-when: R3の閾値そのものを変える必要がある場合は理由を記録する。
- paths: Test/Core/Rendering, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SS-CONTACT-SHADOWの反復（2026-10-02）で見つけた。lowの段で`mean_flip=0.396656647`（上限0.02）・`raw_max=163`・`maximum_channel=19`で落ちる。接触影を切ったシェーダーでも同じ値（`.harness/runs/20261002-084328/check-SS-CONTACT-SHADOW-r3-density-contact-off.txt`）なので、それより前の変更による。ctestには入っていない。

## TEST-FULL-CTEST-BASELINE: 全体CTestの既存の失敗を直す
- status: backlog
- done-when: 全体CTestで、R7の作業前（`c9a3e33`）から失敗している次のテストが通るか、失敗の理由と扱いが記録される: `VolumetricsPassContractTest`（霧の設定行の文字列）、`RenderResourcesDomainContractTest`（`WaitIdleWithoutResultCheck(`の数3、期待2）、`ViewportCameraIdRenderPlanTest`・`BoardComponentRoutingTest`・`SkeletalFramePacketSnapshotTest`（GPUデバイスのないテストでRenderingCoordinator::GenerateDrawCommandsが`m_Device->GetCapabilities()`をnull参照、`578236d`以来）、`FrameCaptureReadbackHelperTest`・`ComponentDataRegistryTest`・`WorldSyncDifferentialTest`（WorldTransformの777）・`CanvasViewRenderTest`・`RenderGraphTextureUsageContractTest`（ShadowMapPassの初期化失敗）（Debugのassertの対話窓で止まりtimeout）、`M9WorldAcceptanceTest`（負の対照の画素差）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --timeout 600`
- stop-when: テストの期待を変える必要がある場合は理由を記録してユーザーへ戻す。
- paths: Library/Core, Test/Core, Game
- notes: 2026-09-25の全体gate（`.harness/runs/20260925-r7-gate/`）で確認。SkinnedRenderPathContractTestはTEST-SKINNED、R4はR4-REOPEN。

## CORE-JSON-SURROGATE: JSONの非BMP文字列を整合させる
- status: todo
- done-when: JsonDocumentのsurrogate pairを単一Unicode scalarへ合成し、生UTF-8/escape表現が同じ名前になることを検証する。
- verify: escaped emoji/生UTF-8/孤立surrogate/文字列往復の実コード試験と独立レビュー。
- notes: ParseUnicodeEscapeは現状4桁単位、AppendUtf8は3byteまで。P5Aとは別件。既定game action名はASCIIで進め、汎用JSON整合として後続修正する。

## GAME-GR130-VFX: 剣のトレイル（リボン）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: CPU の試験で、固定ステップの回数が 0・1・2 のどのフレームでも帯の点列が連続で NaN が無いこと、1フレームで90度以上振っても補間で折れ目の角度が上限以下になること、寿命で点が消えて上限を超えないこと。GPU の試験で、既知の軌跡の帯の画素の位置が期待と一致すること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR13（ボーンソケット）、GR57（描画の経路と半透明のキューを共有）、GR136、GR02（剣は描画用の補間された Transform を読む。攻撃判定 GR16 はシミュレーションの Transform を読む。段階をまたぐ注意の G4 の項目どおり）、GR30（TAA の整合）。
- stage-proposal: G8（戦闘と進行）。垂直スライスの戦闘で使う。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR131-VFX: 当たりの火花・衝撃（ヒットエフェクト）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 当たり1回で放出の要求がちょうど1回積まれ、位置と法線が当たりの値と一致すること。表面の種類ごとに表のエフェクトが選ばれること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR16、GR57、GR136、GR115（軟い。無いうちは既定の1種類）、GR02。
- stage-proposal: G8。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR132-VFX: シ者を倒したときの赤い血を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 撃破の合図から、血・滲み・消滅が時間割どおりの時刻で始まること（固定刻みのクロックで2回撮って一致）。滲みのマスクが時間に対して単調に広がること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR60、GR65、GR26、GR133、GR136。
- stage-proposal: G10（シ者の表現と戦闘の拡張）。垂直スライス（G8）では、最小の形（血しぶきのパーティクルだけ）を入れるかを作者が決める。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR133-VFX: デカール（地面や体に残る跡）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: GPU の試験で、既知の箱のデカールが範囲内の GBuffer の色と法線だけを変え、範囲外の画素が変わらないこと。上限を超えると古いものから消えること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25（描画の拡張点）、GR26（材質の拡張）、GR37（地形）。草（GR43）の上の扱いは、草には描かないのを既定にする。
- stage-proposal: G10。足跡を垂直スライスで使うなら G7 に前倒し。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR134-VFX: メッシュのエフェクトを実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 粒子の数だけインスタンスが描かれ、時間の値で溶けの閾値が変わること（GPU の試験の画素で確かめる）。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR136、GR26、GR27（アルファテストと両面描画）、GR60。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR135-VFX: 空気の歪み（屈折）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 歪みのバッファが空のとき、出力が歪みのパスの有無で画素単位で一致すること。既知のずらしの値で、画素が期待の量だけ動くこと。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25、GR30、GR57、GR134、GR136。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR136-VFX: Niagara 相当の VFX システムを実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: スキーマに、エミッタとモジュールの型、値の範囲・単位・既定値が出ること。範囲外の値や型の合わない値の設定が拒否されること。クックしたバイナリを読んだ結果が、元の形式から読んだ結果と一致すること（同じ種と刻みで、粒子の位置の列が一致）。動いているゲームでの値の変更が、次のフレームから反映すること。イベントで起動したエミッタが、起動の位置と時刻どおりに生成すること。フリップブックの取り込み設定どおりの UV の矩形と再生の速さ。雨（GR58）を含む既存の要件のエフェクトを少なくとも1つ、このシステムで組めること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57（この要件は GR57 と範囲が重なる。GR57 をこの形で設計するか、GR57 の上の層として作るかは着手時に決める）、GR78、GR96、GR02、GR126（軟い）、GR72（軟い。音は後で足せる形）。
- stage-proposal: G7（GR57 と一緒）。雨（GR58）やしぶき（GR59）もこのシステムのエフェクトとして作るので、早めに入れる。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR137-VFX: 資産の編集・決定的な撮影・言語モデルの口（Bridge の拡張。汎用）を実装する
- status: todo
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: Bridge の試験で、資産を開いて値を設定し、保存して開き直すと値が一致すること。型の合わない値や範囲外の値が拒否されること。同じ引数で2回撮影した画像が一致すること（GPU の試験。GPU の無い環境では飛ばす）。MCP の口から、スキーマの一覧、値の設定、撮影が通ること（NorvesEditor の側の作業なら、NorvesLib の試験の対象外）。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR136（エフェクトの資産がリフレクションで公開されていること）、GR78、GR96、GR02、GR126。
- stage-proposal: G7（GR136 と一緒）。エフェクト以外の資産にも使えるので、早く欲しければ前倒しできる。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## G2-GR77-NATIVE-ACCEPTANCE: 実物GLBとnative統合の受入れを記録する
- status: blocked
- done-when: 実物GLBと既存glTFのcooker/loose/骨格の受入れを実行し、必要な範囲でgeometry/texture/hashと寿命を確認する。
- verify: CookedMeshTest、AssetCookGlbSmoke、実物大型GLBのcpu/cooked/表示結果を区別して記録する。
- stop-when: pure helperやfixture構文の成功をnative統合の成功として扱う。
- notes: 現cloudはWindows.h依存で本体compileできず、計画書が参照するDogGameの実物大型GLBも未配置。作者は非描画Windows検証を必須から外しているため、独立実装は継続する。

## G2-GR78-SURFACE-CENTROID: 表面重心の極端値精度を確認する
- status: blocked
- done-when: 面積重み原点の極端値相殺と循環/面列挙順依存を解消し、独立確認後にsurface_centroidを有効化する。
- verify: 2^100/2^-100の等面積2面、巨大正負座標/微小残差、面積/重心/実頂点結果を独立反証する。
- stop-when: 未確認実装をロードへ接続する、他の原点選択へ無言fallbackする。
- notes: 2026-10-03作者は他変換を先行し、この機能は未対応として明示拒否する方針を承認。以前の候補はa6391b2の履歴に保持、active実装からは除く。

## G2-GR82-V1-REST-POSE-GUARD: clip作成時骨格の差を束縛時に検査する
- status: blocked
- done-when: v1で各clipの作成時rest snapshotを保存し、現在骨格との差が許容超過なら既定拒否、明示許可時だけ通し、差量と超過関節を報告する。
- verify: 同名同階層・異なるrest、全関節Translationを含む古いclip、許容内/外/明示許可/q符号同値、欠落/不正snapshot、cook/parse/bind往復。
- stop-when: SkeletonIdからrestを除いたことを無条件互換と扱う、古いclipの歪みを黙認する。
- notes: 作者承認済みのStage B必須要件。blocked理由はv1形式とStage A等の前提待ちで、作者の再承認待ちではない。

## G2-GR32-GPU-ACCEPTANCE: 多材質と旧描画互換をWindows GPUで確認する
- status: blocked
- done-when: 2材質板のGPU readbackと旧NVSKEL/M9SkinnedのGBuffer全画素一致、既定Rendering3DTestを実機で確認する。
- verify: Windows/Vulkanで独立draw契約とGPU readback、起動画面を取得して実画像/結果を確認する。
- blocked-by: 現在のクラウドにWindows/Vulkan実行環境がない。非描画のWindows免除を描画へ拡張しない。
- stop-when: mock/source確認を実GPU合格と扱う、ユーザー指定を変えて別環境へ無断移動する。

## G2-GR79-IMPORT-POLICY-CONNECTION: 材質設定と出所付き診断を接続する
- status: todo
- done-when: material/asset/asset-setの設定を解決し、発光換算の未設定拒否に資産名・材質名・emissiveNitsPerUnitを表示。AI生成profileを明示的に適用し素材単位overrideを保持する。canonical/hash/cacheとJSON/CLIへ接続する。
- verify: asset-set単位指定/素材上書き/欠落・不正/発光textureのみ/診断名/設定差cache失効と旧非発光・v0互換を確認する。
- stop-when: provenanceを拡張子だけで決める、sidecarよりasset-set設定を無言優先、非発光を不必要に拒否、見た目未確認を受入れ済みとする。

## G2-S6-ASSET-SET: C++一括cookと増分判定を接続する
- status: doing
- done-when: AssetCook --asset-setへ一括cookと増分判定を集約。origin/main CookTextureAssetSet.ps1 + Rendering3DTestSilverTextures/Rendering3DTestSilverGltfTexturesに対してcooked/manifestのbyte一致を確認。glTF外部ファイルとsidecarを印に含む。
- verify: 単体CLIの分割前後比較、旧texture spec v1の2spec同値、外部buffer/画像/sidecarの変更・不在・復帰・破損で正しい再cook/拒否、失敗時出力保持。
- stop-when: 手元確認用CookAssets.ps1/StartupMaterialsを対象に戻す、PS側へ増分判定を重複実装、Windows実byte比較を未実施で完了とする。

## G2-MATERIAL-SELECTION-INTEGRATION: 共通照合を設定とslot名へ接続する
- status: todo
- done-when: GR79 ARM/発光、GR78 材質→SurfaceName、GR32 slot名が同じResolveMaterialSelectionを使う。元catalog/生成slotを明示し、全設定の未一致/二重指定をcookと増分preflight双方で拒否。同名GLBは元での改名推奨を資産名付きで警告する。
- verify: raw無名1/Blender Material_0、逆primitive順/同名/生成名衝突、name+index二重指定、不在、unicode名、incremental skipの検証迂回なし、設定値/SurfaceName/slotへの実到達。
- stop-when: 未実装SurfaceNameを受理して捨てる、共通核の存在だけで全接続完了とする、元indexとslotindexの混同、旧wire予約領域へ勝手に保存。

## CORE-STRING-REPLACE-TERMINATOR: 部分置換によるsuffixのNUL破損を修正する
- status: todo
- done-when: TString::replaceが同長/増加/縮小/末尾/自己参照の置換で意図したbyte列を保ち、終端は末尾だけに置く。
- verify: 実Coreのchar/wchar/member契約、部分置換直後のsuffix先頭と全size、関連文字列試験。
- stop-when: StringCopyの全呼出し規約を検証なしに変更、Windows CRTの動作を偽shimで合格扱い。

## G2-GR82-B4-STATIC-ROOT-FRAME128: 静的なArmature親と作者frameを安全に束縛する
- status: doing
- done-when: 明示profile2/128で、skin.skeleton省略と静的な非関節祖先を作者importから三role保存/同snapshot読込/既存runtime公開/実CPU poseまで通す。現在ROOTと全作者snapshotの必須AFRMを比較し、同local restでも異なるframeはrest overrideでも拒否する。profile2 clipには失効可能な束縛証明を持たせ、直接Sampleの迂回も拒否する。
- verify: 合成glTF/GLBのArmature/祖先chain/省略hint/並べ替え、非可換G・M・IBMとimport scale2の独立pose oracle、frame欠落/不正/混在profile/全snapshot差拒否、SetClip・Unload・別targetでproof失効、三NVPKから実runtime/名前指定Sample、確保前の境界予算と失敗out保持。既存profile1のwire/pose・旧71/固定Sampler/cook/CLIを維持し、新Debug/Release常時検査と独立oracleを使う。
- policy: profile1の既定/bytes/128/Identity ROOTを維持。profile2はroot上の静的直接TRS・正一様scale祖先だけ、joint自体は既存正TRS。joint間非joint/祖先animation/matrix/非一様祖先は拒否。SkeletonIdにprofile/rest/ROOTを混ぜず、別guardでprofile照合。frame全64byte一致、cross-profile自動束縛なし。
- stop-when: 作者frameを現在ROOT/IBM/t0から埋める、旧profileを緩める、独立pose期待を実装都合で変える、CPU拡張のため描画側を黙って変える、clip-only/256/CLI/GR96を同時に含める。実犬/DCC/GPU受入とCPU合成試験を分ける。

## G2-MERGE: 描画改善mainとの統合
- status: done
- done-when: 既存G2とmainの二形式cook・runtimeを保持し、統合の確認後mainへマージする
- verify: 意味上の衝突のCPU回帰と区切りの検査。毎変更CIなし
- next: G3-GR12。ロードマップの依存順で再生基盤から実装する

- result: 2親merge4da8031d。run95のCore/AssetCookビルド成功、追加テストの名前解決修正済み。統合後の全件実行は未検証。詳細はPROGRESS.md。

## 完了済みタスクの履歴

完了済み455件は[履歴一覧](Docs/History/2026-10-07-G2Integration/README.md)へ移動。todo・doing・blocked・backlogはこのファイルに残しています。

## G3-GR12: 姿勢評価を事前計算と再利用scratchへ整理する
- status: done
- done-when: legacy/splitの既存契約を維持し、resource派生cache、LocalPose/FK/palette/JointModelMatrices、二分キー探索、関節AABB境界、直接経路のウォームアップ後確保0を揃える。Sample wrapperと正確な境界oracle、純関数SkinPositionを残す
- verify: 既存SkeletalAnimationSamplingTest bundleの独立oracle・不正入力・キー境界・保守的bounds・scratch安定・実測をまとめる。変更ごとのCIなし。GPU/DCCは別枠
- notes: G3-S1/S2/S10の推奨A。split IBMはmesh所有。巨大な添字やtiny weight、legacyとsplitの異なる検証を同一化しない。詳細は管理外Docs/Plans/G3Implementation.md
- evidence: f5ec362a / run37699941320のCoreと関連CPU2/2 PASS。旧oracle・1000回reuse・baseline30・Release測定を確認。GPU/実素材は別枠

## G3-GR10: ブレンド・状態機械・Animatorの実行系を接続する
- status: done
- done-when: PoseOps/名前束縛済みパラメータ/JSONグラフ/Clip・BlendSpace・Layered・StateMachine・Select/UpdateとEvaluate/Animator外部駆動/スクリプト・debugの入口が接続される。位相同期はGR11のメタデータ後に同タスクで完成させる
- verify: 姿勢合成・グラフ拒否と遷移・外部駆動・modifier順・定常配列再利用を既存CPU bundleで確認する。CIは区切りにまとめる
- notes: GR12の検証待ちと並行して独立なPoseOps/パラメータから進める。素材の骨格や保持姿勢の判断は含めない

## G3-GR11: クリップメタデータ・イベント・ルートモーションを接続する
- status: done
- done-when: 基底metadataとruntime overlay、loose/cooked保存、有限queueと窓の終端配送、root差分の消費、読み取り専用接地・周期解析を接続し、合成CPU回帰を通す
- verify: SkeletalAnimationSamplingTest内のAnimGraphRuntimeTestとRootMotionMathTest、CookedClipBankV1Test、SkeletalClipBankBindingTest
- evidence: 4ae627f4 / run37714129004の実Windows4/4 PASS。実素材の解析品質は別枠で未検証
- paths: Library/Core/Private/Animation, Library/Core/Public/Animation, Library/Core/Private/Asset, Test/Core/Rendering, Test/Core/Asset

## G3-GR13: ソケット・保持slot・profile切替を接続する
- status: done
- done-when: 骨scale除去とEntity scale維持、正しい同フレーム追従、保持容量とtag、profile補間と解放速度、寿命清算、SOCK互換がCPU検証で通る
- verify: SkeletalAnimationSamplingTest内のsocket/attachment/hold/script回帰、CookedSkeletonV1Test。明示debug sceneのGameビルド
- notes: 6db0ac20実装済み。run37717801857のテスト側Delegate型指定を修正し、Windows再検証待ち。GPUと実rigの目視は別枠
- paths: Library/Core/Private/Component, Library/Core/Public/Component, Library/Core/Private/Animation, Library/Core/Public/Animation, Library/Core/Private/Asset, Game/Debug, Game/GameModes/Rendering3DTest, Test/Core

## G3-VISUAL-ACCEPTANCE: 合成調整画面と既定起動画面を確認する
- status: backlog
- done-when: 明示debug sceneでparameter/state/profileを操作し、当frame軸と追従を確認・録画する。既定startupを撮影し着手前と比較する
- verify: ImGui有効Game --animation-debug、既定Scripts/CaptureStartupScene.ps1。画像・動画を開いて確認する
- blocked-by: 現作業環境はLinuxでWindows Gameを実行できず、wine・vulkaninfo・/dev/driも確認できない。現在CIはWindowsでbuild/CPU実行のみ。実rig/clipも別途未検証
- stop-when: CPU結果やGameビルド成功をGPU実表示の確認として扱う。ユーザーのPCへ無断で切り替える

## G4-MOVEMENT-CAMERA: 移動・時間管理・カメラを接続する
- status: done
- done-when: GR07のカプセル移動、GR05の手続き/ルート移動、GR02の固定60/120Hz・可変比較・補間・時間倍率、GR06の追従/衝突/速度演出/ロックオン/設定をGameへ接続する。
- verify: 関係するソースの構文確認と既存スモーク。追加CIや細粒度テストを完了条件にしない。
- notes: 実際の見た目・動きは、ロードマップ全体の実装後にユーザーが確認する。

## G4-GR128: 動画由来モーションの調整をcookへ接続する
- status: done
- done-when: 既存時間軸/ループ/root抽出に外れ値除去・周期平滑/平均・接地情報・自然速度・時間補正・導出root・reportを接続する。
- verify: AssetCookとCoreの変更ソースの構文確認。調整値はprofileで変更できる。
- notes: role-profile processingで明示的に有効化する。足のIK固定や別骨格への再接地は後続項目。

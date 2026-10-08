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

## VTG8-VSM-POOL: VSMの物理ページのプールとページの表を作る
- status: backlog
- done-when: 太陽のクリップマップ（各段 16K×16K 仮想、ページ 128×128）のページの表と物理ページのプールを作る。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 段8の開始時に親が詳しくする（計画書 4.3）。

## VTG8-VSM-MARK-RENDER: 必要なページに印を付けて描く
- status: backlog
- done-when: 深度から必要なページに印を付け、印のページだけを物理プールへ割り当て、クラスタの経路でページへ深度を描く。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-VSM-CACHE: 動かない物のページをキャッシュする
- status: backlog
- done-when: 動かない物のページを次フレームへ持ち越し、動いた物の範囲だけ無効化する。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^RenderGraphCompileTest$"`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-VSM-SAMPLE: 照明でVSMを読み、起動画面の太陽の影にする
- status: backlog
- done-when: 照明が VSM を PCF で読む。起動画面の太陽の影を VSM にし（検証シーンは CSM のまま）、撮影で CSM 以上に細かくちらつかないことを確かめる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG8-VSM-SAMPLE -Configuration RelWithDebInfo -SunElevations 10,45,3`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md

## VTG8-ACCEPT: 段8（VSM 太陽）の受入れを記録する
- status: backlog
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段8の節。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md

## VTG9-VSM-POINT: 点光源の影をVSMにする
- status: backlog
- done-when: 点光源の6面のキューブを同じ物理プールの VSM で持ち、起動画面の夜の電球の影を VSM にする。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/VTG9-VSM-POINT -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Game, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 段9の開始時に親が詳しくする（計画書 4.3）。

## VTG9-ACCEPT: 段9（VSM 点光源）と全体の受入れを記録する
- status: backlog
- done-when: `Docs/RenderingValidation/VirtualizationAcceptance.md` の段9の節と全体のまとめ（8GB 級の上限での全体の負荷モード、各段の数値）。
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Docs/RenderingValidation, TASKS.md, PROGRESS.md

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
- status: doing
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
- status: doing
- done-when: 骨scale除去とEntity scale維持、正しい同フレーム追従、保持容量とtag、profile補間と解放速度、寿命清算、SOCK互換がCPU検証で通る
- verify: SkeletalAnimationSamplingTest内のsocket/attachment/hold/script回帰、CookedSkeletonV1Test。明示debug sceneのGameビルド
- notes: 6db0ac20実装済み。run37717801857のテスト側Delegate型指定を修正し、Windows再検証待ち。GPUと実rigの目視は別枠
- paths: Library/Core/Private/Component, Library/Core/Public/Component, Library/Core/Private/Animation, Library/Core/Public/Animation, Library/Core/Private/Asset, Game/Debug, Game/GameModes/Rendering3DTest, Test/Core

## G3-VISUAL-ACCEPTANCE: 合成調整画面と既定起動画面を確認する
- status: blocked
- done-when: 明示debug sceneでparameter/state/profileを操作し、当frame軸と追従を確認・録画する。既定startupを撮影し着手前と比較する
- verify: ImGui有効Game --animation-debug、既定Scripts/CaptureStartupScene.ps1。画像・動画を開いて確認する
- blocked-by: 現作業環境はLinuxでWindows Gameを実行できず、wine・vulkaninfo・/dev/driも確認できない。現在CIはWindowsでbuild/CPU実行のみ。実rig/clipも別途未検証
- stop-when: CPU結果やGameビルド成功をGPU実表示の確認として扱う。ユーザーのPCへ無断で切り替える

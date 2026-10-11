# TASKS — NorvesLib

2026-10-09 ランチャーと見せるシーン（ブランチ `feature/scene-launcher`、ユーザー決定）: 起動するとランチャー（シーン選択）が出て、Rendering3DTest・仮想化ジオメトリのバナナ1万本・荒野の前哨地・夜の集落・材質ギャラリー・室内の光へ入り、Esc のポーズメニューからランチャーへ戻れるようにする。決定事項・設計・資産の作り方・受入れは計画書 `Docs/Plans/SceneLauncherPlan.md` が正本で、`SCN-` の項目で上から順に進める。重い処理（ComfyUI での3D生成・クック・撮影と計測）はこの計画でユーザーが許可済み。GPU を使う処理は同時に1本（ComfyUI は生成スクリプトが止めてから Game を撮る）。Rendering3DTest の見た目は変えない（`CaptureStartupScene.ps1` の撮影で確かめる）。新しい描画機能はシーンごとに有効にし、既定では効かせない。新しいシーンの見た目は `Scripts/CaptureScene.ps1`（SCN-SCENE-ARG で入る）の撮影を開いて確かめる。 走行の間、他の系の未完の項目は `backlog` に退避している。main へマージする前に元の status へ戻す（todo: CORE-JSON-SURROGATE・GAME-GR130-VFX・GAME-GR131-VFX・GAME-GR132-VFX・GAME-GR133-VFX・GAME-GR134-VFX・GAME-GR135-VFX・GAME-GR136-VFX・GAME-GR137-VFX・G2-GR79-IMPORT-POLICY-CONNECTION・G2-MATERIAL-SELECTION-INTEGRATION・CORE-STRING-REPLACE-TERMINATOR、doing: G2-S6-ASSET-SET・G2-GR82-B4-STATIC-ROOT-FRAME128）。

2026-10-10 ユーザー不在の間の方針（ユーザー指示「何かあったら推奨の案で進めて」）: ユーザーにしか決められない判断が出ても BLOCKED にせず、推奨の案を採って進め、採った判断と理由を PROGRESS.md に書く。推奨の案が paths の外の変更を要するときは、その項目の paths に足して同じ反復で進める（危険地帯はランナーの評価で反証される）。破壊的操作（資産・ブランチの削除、履歴の書き換え）と push はしない。

2026-10-10 相談の方針（ユーザー指示「詰まったら GPT にも相談して」）: 同じ手法で 2 回失敗したとき、描画・GPU の設計の選択に迷ったとき、評価者の差し戻しの直し方が分からないときは、次の手を打つ前に `node ~/.agent-workflow/ask-advisor.mjs codex`（ブリーフは stdin。GPT に相談）で相談し、受けた助言と採った手を PROGRESS.md に書く。

2026-10-10 仮想化ジオメトリの規模の引き上げ（ユーザー決定）: バナナ 100万本（と一般のゲームの負荷 mega-stress）で GPU・ゲームスレッド・レンダースレッドの median と p95 が 16.6 ms 以下（RTX 4080・1920×1080・RelWithDebInfo）を `SCN-MEGA-` の項目で、荒野より先に進める。バナナ専用ではなく、席（GPU Scene）・変化の通知・差分の転送・ストリーミングで実際のゲームにも効く形にする。設計は計画者の計画を GPT（Sol）が 2 周批評したものを親がまとめた（計画書 `Docs/Plans/SceneLauncherPlan.md` の「仮想化ジオメトリの規模の引き上げ」）。準備の項目は新旧の一致（mismatch=0）が取れるまで切替へ進まない。切替の項目は旧経路へ戻す引数を残す。

2026-10-10 計測の方針（ユーザー指示「毎変更ごとにビルドしてゲームをつけて計測していたら終わらない。計測が必要なタイミングを考える」）: 実装の項目の検証は、RelWithDebInfo のビルド 1 回と、正しさを見る短い起動 1〜2 本（新旧の一致・溢れ・送った量などの検査のカウンタ。0 でなければ ERROR を出して撮影が落ちる）だけにし、GPU 時間（`-GpuTimingFrames`）は測らない。性能の数値と Debug の検証レイヤーは節目の計測の項目（`SCN-MEGA-MEASURE-*`・`SCN-MEGA-SCALE-300K`・`SCN-MEGA-ACCEPT-1M`。調べる段の PROBE と SETUP-LINEAR は計測そのものが目的）でまとめて見る。反復の中でも、小さな変更のたびにビルドしてゲームを起動しない。コードを読んで考え、変更をまとめてから起動 1 回で確かめる。CPU のテストで確かめられる契約はゲームを起動せずに確かめる。 計測の項目の verify は、反復の中で 1 回撮った結果を `Scripts/CheckCaptureMetrics.py`（結果が揃い、失敗・溢れが無く、計測の値があり、いまの Game.exe より後に撮られたこと）で検査し、ランナーは重い撮影をやり直さない。1 回の撮影が 10 分を超える規模（古い経路での 10万本以上など）は、ECS の大量の軽い実体が入るまで測らない。 2026-10-10 追記（ユーザー指摘「改善が確定している土台で測って意味があるのか」）: 作り変えが決まっている経路の改修前の計測はしない。節目の計測の合否は目標の絶対値（16.6 ms などの閾値）で決め、改修前の値との比は合否に使わない。改修の前に測るのは、結果で設計や作るかどうかが変わる診断（PROBE-ROOT・PROBE-100K のような）だけ。正しさの比較（描いたクラスタ数・画像の一致）は別で、これは続ける。ECS 前の mega-stress の値は、既に撮った分を PROGRESS に参考値として残した（そのために撮り直さない）。 2026-10-10 追記（ユーザー指摘「全体の負荷だけ測っても効果が分からない。GPU はパスごと、CPU は関数単位の内訳が要る」）: 節目の計測の項目は、問いと、それに答える区間（CPU）・パス（GPU）の名前を done-when に書き、その値で判断する。全体の gt・rt・GPU だけで合否を決めるのは受入れ（SCALE-300K・ACCEPT-1M）だけ。GPU のパスごとの時間は撮影が既に出している。CPU は ECS のスケジューラが系ごとの区間を自動で記録する（SCN-ECS-SCHEDULER）。区間の名前の決まり: 段 `Ecs.Stage.<段>`、系 `Ecs.Sys.<系>`、同期点 `Ecs.Sync.<段>`、同期点の中のコマンドの適用・破棄の回収・変換の伝播は `Ecs.Sync.Commands`・`Ecs.Sync.Cleanup`・`Ecs.Sync.TransformPropagate`、描画の変換の系は `Ecs.Sys.RenderTransform`、SceneQuery の更新は `Ecs.Sys.SceneQuery`、RenderGraph のパスの記録は `RG.Record.<パス>`。系や段を作る項目はこの名前を使う。タイムラインのビューア（Insights のようなもの）は今は作らない。 2026-10-11 追記（ユーザー指摘「計測は改修の後と決めたのに、何のための撮影か」→ ユーザー決定）: 各項目の検証でゲームを起動するのは正しさの確認だけで、回数を絞る。起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）。検査付きの起動（`-RequireChecks`・`engine-checks`）は 1 視点 1 本。シーンの出入りの周回は、寿命とシーンの出入りに触る項目だけで、混在 1 本。CPU だけの変更は CPU のテストと Game のビルドで確かめる。反復の中でも、確かめたいことが無い起動はしない。 検証の起動は窓を前面に出さない（撮影・周回のスクリプトは `--window-background` を付ける。反復の中で Game.exe を直接起動するときも付ける）。

2026-10-10 ECS の決定（ユーザー決定）: Entity はオブジェクト管理の器として残し、更新（Tick・変換・描画への同期・ゲームロジック）のデータと処理を ECS 型（自前のアーキタイプ型、値の実体は列に置き Object の読み書きはそこを通す、大量の物は Object を持たない軽い実体、ロジックはすべてシステムへ、ジョブで並列）に作り変え、レンダースレッドもコマンドの並列記録まで並列化する。100万本の改修に組み込む。設計の正本は `Docs/Plans/EcsUpdatePathPlan.md`（計画者の計画を GPT が 2 周批評し、親が追記した。項目の notes の「§」はこの文書の節）。`SCN-ECS-` の項目と、ECS 前提で置き換えた `SCN-MEGA-` の項目を、計画書の §2 の順に並べた。ワールドの全走査を直す旧案（WORLD-TICK-CACHE・WORLD-TRANSFORM-NOTIFY・WORLD-TRANSFORM-INTERP・WORLD-RENDER-DIRTY・INSTANCE-BATCH）は ECS の項目に置き換えて消した。戻すときは、戻す項目とそれに依存する後続の項目を新しい順に revert する（単独では戻さない）。

起動画面（Rendering3DTest）の描画改善（2026-09-27、ユーザー決定）。物理空と空の太陽による昼の屋外、点光源のキューブシャドウ、自動露出・ミップチェーンのブルーム・TAA・GTAO・コンタクトシャドウ・色収差・レンズダート・グレーディングLUT・RTGIの既定化、展示物の追加、視差オクルージョンと影の不具合の修正を `SS-` の項目で進める。見た目の証拠は `Scripts/CaptureStartupScene.ps1` の撮影を開いて確かめる。起動画面の見た目を変えることはこの計画でユーザーが承認済み。検証シーン（Indoor/Outdoorのgolden、R系の受入れ）は、項目に書いた場合を除き結果を変えない。

2026-09-30 再開: 止めていた4項目（SS-DAYLIGHT-P1・P2、SS-POINT-SHADOW-P2、SS-EMISSIVE-GLOW）は各 `blocked/<ID>.md` の推奨の選択肢で再開し、空のモデル（SS-SKY-MODEL-P1・P2）と発光の露出（SS-EMISSIVE-PREEXPOSE）を足した。この3項目は、項目に書いたとおり空を使う検証（R2・R7屋外）とgoldenの結果を変えうる。

2026-10-03 続き（ブランチ `feature/startup-scene-detail`）: ユーザーの指摘（石のタイルが近づくと粗い）と要望（球をMegaGeometryで高ポリに）から、FIX-ASYNC-TEXTURE-MIPS → SS-CSM-MEGA-CASTERS → SS-MEGA-SPHERE → SS-MEGA-SPHERE-DISPLACE → SS-ACCEPT-DETAIL の順に進める。

2026-10-03 続き2（ブランチ `fix/showcase-sphere-reflection-ao`）: ユーザーの指摘（金属の見本の球がカメラの角度によって一部透けて見え、同じ角度でも影のかかり方がちらつく）から、FIX-GTAO-STATIC-NOISE → FIX-SSR-SPECULAR-COMPOSITE を直す。

2026-10-04 続き3（ブランチ `feature/ground-material-showcase`）: ユーザーの要望（地面のテクスチャを色々用意して質感を見比べられるように。テクスチャは Poly Haven から落とし、git に入れない）から、SS-GROUND-SWATCHES を足し、その途中で見つけた FIX-MESH-BOUNDS-CULLING を直す。

2026-10-04 続き4（段1はブランチ `feature/vtg-stage1-bc`）: ユーザーと決めた全体計画 `Docs/Plans/VirtualizedTextureGeometryPlan.md`（テクスチャとジオメトリの仮想化。BC圧縮・sparse のVT・2パスの遮蔽カリング・LODの階層の焼き込み・ページのストリーミング・ビジビリティバッファ・ソフトウェアラスタ・VSM）を `VTG<段>-` の項目で進める。決定事項・設計・段の受入れは計画書が正本。1段ずつ回し、今の段だけ `todo`、後の段は `backlog`。段が終わると親が受入れを確かめて main へマージ・プッシュし、次の段の項目を詳しくしてから `todo` にする。8GB級のGPUで収まるのが目標で、開発機では `--vram-budget-mb` の上限で確かめる。起動画面（天球・地面・球・岩）が見える状態は全段で保つ。

それより下はR0〜R8と関連の修正の記録。R8までの完了後に残った `todo` は、起動画面の作業を先に進めるため `backlog`（ループが拾わない）にしてある。再開するときは `todo` へ戻す。

## SCN-SCENE-ARG: シーンの一覧と `--scene` 引数を足し、撮影スクリプトをシーン指定にする
- status: done
- done-when: Game にシーンの一覧（id・日本語の表示名・説明・見本画像のパス・対応する GameMode・名前付き視点・必要な資産）を1か所に定義し、`--scene=<id>` で起動直後にそのシーンへ入る（`rendering3dtest` は今の Rendering3DTest）。未知の id はエラーをログに出して既定へ落ちる。この項目の時点の既定（引数なし）はまだ Rendering3DTest。`Scripts/CaptureStartupScene.ps1` と、Game.exe を起動して Rendering3DTest を前提にする他のスクリプト（`Test-CameraWorld.ps1`・`Test-M6AngelScriptHotReload.ps1`・`Test-M8MinimalPhysics.ps1`・`Test-M9World.ps1`・`RunCookedTextureGameSmoke.ps1`・`RunCookedModelGameProfile.ps1` を読んで判断）が `--scene=rendering3dtest` を渡す。新しい `Scripts/CaptureScene.ps1 -Scene <id> -OutDir <dir> [-Views <名前,...>] [-Configuration Debug|RelWithDebInfo|Release] [-Width <px> -Height <px>] [-GameArgs <先頭の -- を除いた引数,...>] [-RequireAssets]` が指定のシーンを撮って視点ごとの PNG と `metrics.json`（平均輝度・白飛び・黒つぶれの画素率）を出し、Game の起動失敗・PNG の欠け・Game.log のエラー（シェーダーのコンパイル失敗・検証レイヤーのエラー・`-RequireAssets` のときの資産の欠け）で非 0、成功で `CAPTURE_SCENE result=pass` を出す（名前付き視点が入るまでは既定の1枚）。シーンの組み立てが終わった印（SceneReady）を `--capture-deterministic` で待つ仕組みを全シーン共通にする。窓の大きさを変える引数が無ければ足す（`-Width/-Height` が使う）。`CaptureStartupScene.ps1` の 3 視点を撮り、main の撮影と並べて開いて同じ見た目であることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-SCENE-ARG`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene rendering3dtest -OutDir .harness/runs/scene-capture/SCN-SCENE-ARG`
- paths: Game, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Engine, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/Rendering/NeuralMaterialDecodePass.cpp
- notes: 計画書 `Docs/Plans/SceneLauncherPlan.md`。モードの登録は `Game/GameApplicationHandler.cpp` の `CreateGameModeStateMachine`（2073〜2167 行付近。`Start` は 2165 行付近）で、起動フラグは static の `s_*` を Creator ラムダが `Rendering3DTestData` へ写している（2095〜2151 行付近）。`Test/Game/Bridge/CMakeLists.txt` と `Test/Core/Rendering/CMakeLists.txt` は Game のソースを直接取り込むので、`GameApplicationHandler.cpp` から参照する新しいソースはそちらにも足す。撮影は `ApplicationProcessor.cpp` の `--capture-png`（634〜660 行付近）と SceneReady（`Rendering3DTestRoutine.cpp` 3276・3865 行付近）。 2026-10-10 再開: `blocked/SCN-SCENE-ARG.md` の推奨の案1（Slang SDK が無いときは `CreateSlangShaderCompiler` が nullptr を返し、`NeuralMaterialDecodePass::Initialize` が読み込みを試みない。そのうえで `CaptureScene.ps1`・`CaptureStartupScene.ps1` のシェーダー失敗の例外を外す）で進める（ユーザー不在のため親が推奨を採った）。

## SCN-R3D-REENTRY: シーンの出入りで作った資源を返し、出入りを繰り返しても増えないことを確かめる
- status: done
- done-when: `--scene-cycle-smoke=<id,...>`・`--scene-cycle-count=<n>`（形は実装で決めてよい）で、Game が別のモード（この項目では空のモード、SCN-LAUNCHER の後はランチャー）とシーンの間を `RequestChange` で周回し、各周回の後に資源の数（材質・テクスチャ・メッシュ・Model・Entity）と GPU の割り当て量をログへ出し、2周目以降が1周目と同じ（GPU の割り当ては遅延解放が済むのを待って比べる）なら `SCENE_CYCLE_SMOKE result=pass`、増えたら `result=fail` と増えた種類を出す。`Rendering3DTestRoutine::Leave` が Enter で作った材質・チェッカーのテクスチャ・負荷用の資源などをすべて返し、`rendering3dtest` で3周して pass になる。材質とテクスチャもシーンの範囲（`GameModeScope`）で追跡して Leave で返せるようにし、後のシーンはそれを使う。`Scripts/RunSceneCycleSmoke.ps1 -Scenes <id,...> -Cycles <n> [-Configuration ...]` がこれを走らせて Game.log を照合し、pass で 0 を返す。Rendering3DTest の撮影の見た目は変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-R3D-REENTRY`
- paths: Game, Library/Core/Public/GameMode, Library/Core/Private/GameMode, Library/Core/Public/Rendering/RenderResources.h, Library/Core/Private/Rendering, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（Object/Resource の寿命）。`GameModeScope`（`Library/Core/Private/GameMode/GameModeScope.cpp` 79〜128 行付近）は Entity・Mesh・Model だけを返し、材質とテクスチャは対象外。`Rendering3DTestRoutine.cpp` の Leave（3869〜4035 行付近）は `materials.Create`（2200 行付近〜）とチェッカー（`m_CheckerTextureHandle`、2175 行付近）を返していない。`RequestChange` はキューに積まれ次の Update の先頭で Leave→Cleanup→Enter（`GameModeStateMachine.cpp` 39〜48 行付近）。RenderThread が使い終わるまで破棄を待つ仕組み（`RenderResources.h` 377〜385 行付近の retire）を守る。資源の数を返す API が無ければ足す。 2026-10-10 再開（親が推奨を採った）: `blocked/SCN-R3D-REENTRY.md` の推奨の案1（スレッドごとのコマンドプール）は SCN-RHI-UPLOAD-THREAD で入って評価者を通った。検証レイヤーの除外なしで周回スモークを再検証し、残る指摘があれば直す。

## SCN-RHI-UPLOAD-THREAD: 描画中のシーン入場で、テクスチャの初期データの転送が共有コマンドプールを競合しないようにする
- status: done
- done-when: `Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3` が Validation.log の `UNASSIGNED-Threading-MultipleThreads-Write` 0 件で pass する。`GpuResourceStore::UploadTextureData` が GameThread から共有のコマンドプールでコマンドリストを作る経路（ミップ生成・`CreateCommandList`）を、RenderThread が同じプールで記録していても競合しない形（専用プール・RenderThread への依頼など）にする。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3`
- paths: Library/Core/Private/Rendering, Library/RHI, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI/Vulkan・RenderThread）。SCN-R3D-REENTRY の周回スモークで見つかった。2周目以降の Enter（描画中）で、チェッカー等の `CreateTexture(info, data, size)` が `UploadTextureData` 内で `m_Device->CreateCommandList()` を GameThread から呼び、RenderThread が使う VkCommandPool と同時に触れる（検証レイヤーの Threading エラー、`vkFreeCommandBuffers` など）。ランチャーから各シーンへ入るたびに起きるので、SCN-LAUNCHER より前に直したい。評価者を通す。

## SCN-LAUNCHER: 起動するとランチャーが出て、カードから各シーンへ入れるようにする
- status: done
- done-when: 引数なしで Game を起動すると `launcher` モードに入り、ImGui の全画面のメニューが出る: 背景は物理空の夕景（地物は無くてよい）、上にタイトル「NorvesLib」と副題、シーンの一覧から作ったカード（見本画像の枠・表示名・説明 1〜2 行・必要な資産が足りないときは取得のコマンド）を並べ、選んでいるカードを強調する。矢印キー・WASD・マウス（ホバーとクリック）・ゲームパッド（十字キーと左スティック・A）で選び、Enter・クリック・A で `RequestChange` する。「ゲームを終了」がある。見本画像が無いカードは暗いグラデーションにシーン名を出す。ImGui は `--imgui` が無くてもランチャーのために使えるようにし（デバッグ用の窓は今どおり `--imgui` のときだけ）、Debug・RelWithDebInfo・Release で出る。日本語は NotoSansJP、字の大きさは窓の大きさから決めて 1280×720 と 1920×1080 で読める。`CaptureScene.ps1 -Scene launcher` を 1280×720 と 1920×1080 で撮って開き、デバッグ窓に見えない（ImGui の既定の灰色の窓・タイトルバー・スクロールバーが出ていない）ことを確かめる。`RunSceneCycleSmoke.ps1` の周回がランチャーを通る形になり、`rendering3dtest` の3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene launcher -OutDir .harness/runs/scene-capture/SCN-LAUNCHER`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-LAUNCHER`
- paths: Game, Library/Modules/ImGui, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Module/IRenderModule.h, Assets/Config, Assets/Textures/Launcher, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: ImGui の登録は `GameApplicationHandler.cpp` 1462〜1482 行付近（今は `--imgui` のときだけ）。`IImGuiView`（`Library/Modules/ImGui/Public/ImGuiModule/IImGuiView.h`）。NotoSansJP は `ImGuiModule.cpp` 632・671 行付近でマージ済み。入力の Menu の文脈（Confirm=Enter/パッド a、Cancel=Escape/パッド b）は `Assets/Config/DefaultInputBindings.json` 217 行付近。見本画像を ImGui のテクスチャとして描く手段が無ければ足す（画像は SCN-THUMBNAILS で作る）。Release にデバッグ機能（GPU 計測・ログ）を入れない。

## SCN-PAUSE-MENU: Esc のポーズメニューからランチャーへ戻れるようにする
- status: done
- done-when: ランチャー以外の全シーンで Esc（ゲームパッドの Start）を押すとポーズメニュー（続ける／ランチャーへ戻る／ゲームを終了）が出て、もう一度 Esc か「続ける」で閉じる。「ランチャーへ戻る」で `RequestChange(launcher)`。メニューが出ている間はカメラ操作などシーン側の入力を止めてマウスカーソルを出す。Rendering3DTest の F1〜F5 のデバッグ表示や Alt のカメラ操作と衝突しない。見た目はランチャーと同じ作り（半透明の暗幕と中央のパネル）。撮影（`--capture-png`）の起動では出さない。開発ビルドだけの `--open-pause-menu` で開いた状態を撮り（`CaptureScene.ps1 -GameArgs open-pause-menu`）、開いて確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene rendering3dtest -GameArgs open-pause-menu -OutDir .harness/runs/scene-capture/SCN-PAUSE-MENU`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-PAUSE-MENU`
- paths: Game, Assets/Config, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: Escape を処理する箇所は今の Game に無い（`Game/Input/GameInputRebindTypes.h` 36 行付近のリバインド取消のみ）。F1〜F5 は `Game/GameModes/Rendering3DTest/Rendering3DTestDebugInput.cpp`。 2026-10-10 再開（親が推奨を採った）: 評価者の指摘（`PauseMenu.cpp` 229 行付近の Start のラッチが `event.Slot` を見ずに解除される）を、ラッチをスロットごとに持ち、同じスロットの解放・サンプルだけで解除する形に直す。回帰の確認は InputSystem→Router→PauseMenu を通し、別スロットの未押下のサンプルを挟む。

## SCN-FREE-CAMERA: 自由飛行カメラと、シーンごとの名前付き視点を足す
- status: done
- done-when: 自由飛行カメラ（右ボタンを押している間マウスで視点、WASD で前後左右・QE で上下、Shift で速く、ホイールで移動の速さ、ゲームパッドの左スティックで移動・右スティックで視点・LT/RT で上下）を Library の入力の部品として足し、新しいシーンが既定で使う（Rendering3DTest は今の Maya 式のまま）。シーンの一覧の各シーンの名前付き視点（位置と向き。最低 `overview`）を `--scene-view=<name>` で起動時に選べ、`CaptureScene.ps1` の `-Views` 省略時はそのシーンの全視点を撮る。確認用に空と平らな地面だけの `sandbox` シーン（開発ビルドだけランチャーに出る）を足し、決まった入力を流す手段（例 開発ビルドだけの `--camera-script=<file>`）で自由飛行カメラを動かした前後の撮影を開いて、指定どおりに視点が動いたことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene sandbox -OutDir .harness/runs/scene-capture/SCN-FREE-CAMERA`
- paths: Library/Core/Public/Input, Library/Core/Private/Input, Library/Core/CMakeLists.txt, Game, Assets/Config, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 今のカメラは `Library/Core/Public/Input/MayaCameraController.h`（軌道カメラ）だけ。入力の収集は `Game/Input/CameraInputCollector.h`、入力の割り当ては `Assets/Config/DefaultInputBindings.json`（Debug の文脈に WASD の Move がある）。

## SCN-CAMERA-PATH: カメラパスのデモ飛行を足す
- status: done
- done-when: シーンごとにカメラパス（位置・注視点のキーと時刻。Catmull-Rom 等で滑らかにつなぐ）を定義でき、P キー（ゲームパッドの Y）で再生・停止、再生中に自由飛行の入力をすると止まる。`--camera-path-time=<秒>` で起動時にパスの途中の視点に置ける（撮影用）。`sandbox` に一周するパスを置き、時刻を変えた3枚の撮影を開いて、パスに沿った位置と向きであることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene sandbox -GameArgs camera-path-time=5 -OutDir .harness/runs/scene-capture/SCN-CAMERA-PATH`
- paths: Library/Core/Public/Input, Library/Core/Private/Input, Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/CMakeLists.txt, Game, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-GEN-PIPELINE: 手元の ComfyUI で3Dモデルを生成するスクリプトを作り、バナナを生成してクックする
- status: done
- done-when: `python Scripts/GenerateSceneModels.py --ids banana` が、ComfyUI（`Y:\ComfyUI\ComfyUI_windows_portable`）が動いていなければ起動し、Z-Image Turbo で定義のプロンプトから参照画像を作り、背景を除いて TRELLIS.2 で GLB（色・金属・粗さ・AO・法線の焼き込み付き）を作り、`Assets/Models/Generated/<id>/` に GLB・参照画像・manifest（プロンプト・seed・面数・MD5・生成日時）を置き、最後にモデルを降ろして自分が起動した ComfyUI を止める。`--preview` で Blender のヘッドレスが GLB を3方向から描いた PNG を出す。`--verify` は GPU を使わず、定義と manifest と実ファイルの MD5 の一致を確かめ、`--require-cooked` のときクック済みの出力があることも確かめる。定義は `Assets/AssetSets/GeneratedModels.json`（id・英語のプロンプト・seed・目標面数・テクスチャの大きさ・実寸の高さ m・用途）、ワークフローは `Tools/ComfyWorkflows/`（API 形式の JSON。文字→画像と画像→3D）に置いて追跡し、`.gitignore` に `/Assets/Models/Generated/` を足す。バナナ（約10万面、テクスチャ 2048、実寸 約 0.2 m）の参照画像とプレビューを開いて、バナナの形と色であることを確かめる（崩れていたら seed を変えて作り直し、定義の seed を更新する）。クックの一覧 `Assets/AssetSets/MegaBananasModels.json` を足し、`Scripts/CookAssets.ps1` でクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids banana`
- paths: Scripts, Tools/ComfyWorkflows, Assets/AssetSets, .gitignore, TASKS.md, PROGRESS.md
- notes: 計画書の「資産」の節。保存済みのワークフロー `Y:\ComfyUI\ComfyUI_windows_portable\ComfyUI\user\default\doggame\pixal3d_trellis2_api.json`（ノード 316 の真偽で TRELLIS.2/Pixal3D を切り替え、`LoadImage` 122 が入力、`RemoveBackground` 192、`DecimateMesh` 186 の面数、`PrimitiveInt` 288 がテクスチャの大きさ、`Save3DAdvanced` 322 が出力）を元にする。文字→画像は ComfyUI 0.38 の Z-Image のノードで組む（`Y:\ComfyUI\models` に `z_image_turbo_bf16`・`qwen_3_4b`・`ae`）。Python は `C:\Python314\python.exe`（標準ライブラリだけで書く）。生成は GPU を占有するので、ComfyUI を止めてから Game を撮る。Blender は `Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe`。クックの入口は `Scripts/CookAssets.ps1`（`-SpecDir`・`-RuntimeRoot`、出力は `build/CookedAssets`）。

## SCN-BANANAS-SCENE: 仮想化ジオメトリのバナナのシーンを作る
- status: done
- done-when: `mega-bananas` シーン: 昼の物理空と太陽の影の下、生成したバナナを MegaGeometry で `--banana-count=<n>`（この項目の既定は今の上限内の 4096。SCN-MEGA-10K で 1万へ上げる）本、広い床（Poly Haven の木の床か土）に向き・わずかな大きさをばらして敷き詰め、手前に数段の山を作る（物理で積まなくてよいが、重なりが不自然に見えない配置）。H キーの情報表示に本数・1本の三角形数・元の三角形の総数、開発ビルドでは MegaGeometry の GPU 時間と描いたクラスタ数も出す。名前付き視点 `overview`（全体を見渡す）・`close`（手前の山を数十 cm から）・`low`（床すれすれ）とカメラパスを持ち、ランチャーに載る。資産が無ければ警告し、ランチャーのカードに `python Scripts/GenerateSceneModels.py --ids banana` を出す。3視点の撮影を開いて、バナナが並ぶ・近くで高ポリの形に見える・影が落ちていることを確かめる。`mega-bananas` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views overview -OutDir .harness/runs/scene-capture/SCN-BANANAS-SCENE/overview`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views close -OutDir .harness/runs/scene-capture/SCN-BANANAS-SCENE/close`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views low -OutDir .harness/runs/scene-capture/SCN-BANANAS-SCENE/low`
- verify: `python Scripts/VerifyBananaPile.py .harness/runs/scene-capture/SCN-BANANAS-SCENE/low/low.Game.log`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes mega-bananas -Cycles 3 -GameArgs banana-count=1024`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 配置の手順は `Rendering3DTestRoutine.cpp` 889〜900・1208〜1242 行付近（CreateMegaMesh→RegisterModel→TrackModel→SpawnObject→CreateComponent<MegaGeometryComponent>→SetMegaMeshHandle→SetCastShadow）。同じバナナのインスタンスは1つの Model を共有する。 2026-10-10 再開（親が推奨を採った）: 評価者の指摘（山の上段が下段に支えられず浮く）を、物理を使わない「落とし込み」の配置で直す: 山の範囲に細かい高さの格子（1〜2 cm）を持ち、置いたバナナの変換後の頂点（間引いた点でよい）を格子へ最大の高さとして書き込み、次のバナナは向きを決めたあと、自分の下面の点が格子に初めて触れる高さに置く（少しめり込ませてよい）。3視点を撮り直し、上段の最下点と下の支えの隙間を数えて記録する。 あわせて、親が overview の撮影で見た「デバッグっぽさ」を直す: 敷き詰めが格子にそろって見える（位置のジッターかポアソン円盤で崩す）、山が四角い区画に見える（中央が盛り上がった塚の形にする）、木の床の端と外側の平らな地面の境目が地平線の手前に見える（床を視界の外まで広げるか、境目が見えない置き方にする）。

## SCN-MEGA-10K: バナナ1万本で、描かれないクラスタ・影の欠け・過大なメモリが無いようにする
- status: done
- done-when: `mega-bananas` を `--banana-count=10000`（引数は 65536 程度まで受ける）で起動し、`overview`・`close`・`low` の各視点で、区間ごとの描画コマンドの上限（`MaxDrawCount`）を超えて捨てたクラスタの数、影で省いたインスタンスの数（VSM。CSM に切り替えたときは UBO の枠）、MegaGeometry のバッファ（2パス遮蔽の可視ビットなど）の大きさを開発ビルドのログに出し、捨てた数・省いた数が 0。0 にならない上限は、必要な分だけ大きくするか、可視ビットを 1 クラスタ 1 bit に詰めるなどインスタンス数に比例して膨らむ量を抑えて直す。1万本の既定を `--banana-count` の既定にする。1万本の撮影を開いて、穴（地面が透けて見える欠け）や影の欠けが無いことを確かめる。Rendering3DTest の撮影と、`--stress-geometry=300` を付けた Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views overview -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-10K/overview`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views close -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-10K/close`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Views low -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-10K/low`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-MEGA-10K`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画・メモリ）。`MegaGeometryPass.h` 98〜105 行付近（`MaxDrawCount = 262144`。超えた可視クラスタは描かれない）、`MegaGeometryPass.cpp` 1488 行付近（Capacity）、3157〜3169・3296〜3299 行付近（可視ビット 4 B × 全インスタンスのクラスタ数の総和、0x3FFFFFFF の上限）、`DynamicUniformAllocator.cpp` 147 行付近（CSM の UBO 枠 1024）。引数の上限は `GameApplicationHandler.cpp` 134〜142 行付近。1フレームの GPU 転送は 8 MB 上限（`RenderResources.cpp` 66 行付近）。

## SCN-BANANAS-PERF: バナナ 1万・2万・3万本の GPU 時間とメモリを測って記録する
- status: done
- done-when: RelWithDebInfo・1920×1080 で、`mega-bananas` の `overview` と `close` を 1万・2万・3万本で測り、MegaGeometry の GPU 時間・フレームの GPU 時間・VRAM（MegaGeometry のプールとバッファ）・描いたクラスタ数を `Docs/RenderingValidation/SceneLauncherAcceptance.md`（無ければ作る）のバナナの節に表で書く。1万本の `overview` のフレームの GPU 時間が 16.6 ms を超えるときは内訳（どのパスが重いか）を書き、直せるものは直し、直さないものは既知の限界として書く。測った視点の撮影を開いて、本数が増えても穴が無いことを確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs banana-count=30000 -OutDir .harness/runs/scene-capture/SCN-BANANAS-PERF`
- paths: Docs/RenderingValidation, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 性能は RelWithDebInfo で測る（Release にデバッグ機能を入れない）。測り方は `Docs/RenderingValidation/VirtualizationAcceptance.md` の段5・段6の GPU 時間に合わせる。

## SCN-MEGA-CPU-FRAME: バナナ 1万本以上で CPU のフレームが GPU の 4 倍以上かかる原因を絞る
- status: backlog
- done-when: RelWithDebInfo・1920×1080 の `mega-bananas` 1万本 overview で、CPU のフレーム（1 万 72 ms・3 万 579 ms。`Docs/RenderingValidation/SceneLauncherAcceptance.md` のバナナの節）の内訳を、ゲームスレッドとレンダースレッドのスコープごとに測って絞り、直せる原因（インスタンスごとの毎フレームの処理・待ち）は直す。直さないものは既知の限界として同じ節に書く。起動画面（Rendering3DTest）と他シーンの見た目・GPU 時間は変えない。 目標は 1万本 overview の CPU のフレームを 16.6 ms 以下（本数に対して線形より速く伸びる部分を無くす）。届かないときは残りの内訳と理由を記録する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-CPU-FRAME --views overview --timing`
- paths: Docs/RenderingValidation, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Object, Library/Core/Private/Object, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画スレッド・メモリ）。トレースのスコープは `RenderGraph.Execute`（1 万 26 ms → 3 万 313 ms）と `MegaGeometryPass.RecordFrameCommand`（2.1 → 7.1 ms）だけで、ゲームスレッド（73 → 579 ms）の内訳が無い。まず `NORVES_TRACE_SCOPE` 相当のスコープを足して絞る。評価者を通す。 2026-10-10 親が backlog にした: 100万本の改修計画（`SCN-MEGA-` の項目）を作り直している間、古い版の項目に手を付けない。計画が入ったら置き換える。 2026-10-10 `SCN-MEGA-` の改修の項目（SETUP-LINEAR・WORLD-*・SCENE-*・GPU-SCENE-*）に置き換えた。この項目は回さない。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-CPU-FRAME`（ランナーは撮り直さず、保存した結果を検査する）。

## SCN-PH-WASTELAND: 荒野用の Poly Haven の岩・崖と地形のテクスチャを落とす
- status: done
- done-when: `Scripts/FetchPolyHavenModels.ps1 -Set wasteland` が乾燥地に合う岩・崖・枯れ木などのモデル 5〜8 種（Poly Haven の API のカテゴリとタグから選ぶ。CC0）を、`Scripts/FetchPolyHavenTextures.ps1 -Set wasteland` が地形の 3 層（砂・ひび割れた乾いた土・岩肌）と石の床（材質ギャラリー・夜の集落の道）のテクスチャを、既存と同じ形（4K、MD5 で照合、git 管理外）で落とす。引数なしの既定の組（起動画面の分）は変わらない。選んだ id と理由（色・形が荒野に合う）を計画書の資産の節に書き足す（計画書は git 管理外）。岩・崖のクックの一覧 `Assets/AssetSets/WastelandModels.json` を足してクックが通り、岩のプレビュー（`GenerateSceneModels.py --preview` の Blender の描画を流用してよい）を開いて荒野に合う見た目を確かめる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenModels.ps1 -Set wasteland`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenTextures.ps1 -Set wasteland`
- paths: Scripts, Assets/AssetSets, TASKS.md, PROGRESS.md

## SCN-MEGA-METRICS: 撮影の metrics にスレッドごとの CPU・パスの p95・基準との比較を足し、基準を撮る
- status: done
- done-when: `CaptureScene.ps1` の gpu_timing に `game_thread_ms`・`render_thread_ms` の median と p95、パスごとの median・p95・max、`cpu_scope_median_ms`（上位 15）が入り、`CAPTURE_SCENE gpu_timing` の行に gt/rt の median・p95 が出る。`-CompareWith <dir>` が視点ごとの平均輝度の差 ≤ 0.1・PSNR ≥ 45 dB で判定する（式は `CaptureStartupScene.ps1` 1736〜1755 行付近と同じ）。基準 `SCN-MEGA-BASE`（バナナ 1万本 overview と起動画面、どちらも決定的な撮影）と、遮蔽 Off の旋回の基準 `SCN-MEGA-ORBIT-OFF` を撮り、同じコードでの比較と、今の遮蔽 On の旋回が Off と一致することが pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-BASE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-METRICS/det`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Deterministic -OutDir .harness/runs/startup-capture/SCN-MEGA-BASE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -MegaOcclusion Off -OutDir .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF -OutDir .harness/runs/startup-capture/SCN-MEGA-METRICS-orbit`
- paths: Scripts/CaptureScene.ps1, TASKS.md, PROGRESS.md
- notes: C++ は変えない。バナナの決定的な撮影が 2 回で一致しなければ PROGRESS に記録し、以後のバナナの比較は MEGA_OCCLUSION の描いた数 ±1% に替える。旋回の On/Off が今一致しなければ、On の撮影を基準にして理由を書く。

## SCN-MEGA-PROBE-ROOT: 根の段の量と単価を測り、クックが根で止まる理由を突き止める
- status: done
- done-when: 開発ビルドの `--mega-lod-error-px=<px>` で LOD の閾値を替えられる（今は環境変数だけ、`MegaGeometryPass.cpp` 238 行付近）。1万本 overview の 1 px と 100000 px（全本が根）で、描いたクラスタ数・`VisRasterSw1`・1 クラスタあたりの ns を PROGRESS に書く。banana の段ごとのクラスタ数・三角形数（GPT の復号では LOD0 780/99,722、根 44/4,112）と、根で止まった理由（`CookMeshDag.cpp` 745〜756 行付近の MinProgressPercent 15% か、境界の固定か）をクックのログで示す。製品の挙動は変えない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000,mega-lod-error-px=100000 -OutDir .harness/runs/scene-capture/SCN-MEGA-PROBE-ROOT/root`
- paths: Game/GameApplicationHandler.cpp, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, Tools/AssetCook/CookMeshDag.cpp, TASKS.md, PROGRESS.md
- notes: 計測と調査だけ。

## SCN-MEGA-SETUP-LINEAR: MegaGeometryPass::Setup の、区画ごとに全インスタンスを探す二重ループを無くす
- status: done
- done-when: 3万本 overview で `RenderGraph.Execute` の中央値 ≤ 60 ms（前は 313.9）、`rt` の median の 3万/1万 比が 3.5 以下。バナナと起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=30000 -OutDir .harness/runs/scene-capture/SCN-MEGA-SETUP-LINEAR`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-SETUP-LINEAR/det`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE -OutDir .harness/runs/startup-capture/SCN-MEGA-SETUP-LINEAR`
- paths: Library/Core/Private/Rendering/MegaGeometryPass.cpp, TASKS.md, PROGRESS.md
- notes: 危険地帯（RenderThread）。対象は `MegaGeometryPass.cpp` 581〜599 行付近。鍵（1293 行付近と同じ式）の `Container::UnorderedSet` を 1 回だけ作って引く。

## SCN-MEGA-PROBE-100K: 10万本（1本1Entity のまま）で、目の高さの視点のインスタンス判定と描く量を測る
- status: done
- done-when: `kMaxBananaCount` を 1,048,576 にし（`MegaBananasMode.h` 49 行付近）、視点 `horizon`（目の高さ 1.7 m から地平線を見る）を足す。10万本 horizon で、BVH の段ごとの GPU 時間（段ごとのタイムスタンプを足す）・描いたクラスタ数・`VisRasterSw1`・gt/rt を記録する。根の球とメッシュの球の半径を開発ログに出す。段 0 が 0.5 ms を超えるかで、範囲（セル）の判定が要るかを PROGRESS に書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-PROBE-100K --views horizon --timing`
- paths: Game/GameModes/MegaBananas, Game/Scenes/SceneCatalog.h, Library/Core/Private/Rendering/MegaGeometryPass.cpp, TASKS.md, PROGRESS.md
- notes: 計測だけ。CPU は遅くてよい。この項目の結果では止めない（25 ms の打切りは改修後の規模の確認にだけ使う）。 撮影そのもの（約 19 分。ゲームスレッドが 1 フレーム約 7 秒で、ランナーの再検証の上限 10 分に入らない）は反復の中で次のコマンドを実走して `metrics.json` を作り、上の verify はその `metrics.json` を検査する: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 120 -TimeoutSeconds 2400 -GameArgs banana-count=100000 -OutDir .harness/runs/scene-capture/SCN-MEGA-PROBE-100K` 2026-10-10 親が完了にした: 計測の結果（10万本 horizon、描いたクラスタ 3,880,187、BVH の段 0 が 4.65/4.48 ms でセルの判定が要る、gt/rt、球の半径）は反復 15 の評価者が確認済み。古い経路の 10万本の撮影は約 19 分でランナーの再検証の上限 10 分を超えるため、計測の項目は保存した結果の検査（`Scripts/CheckCaptureMetrics.py`）で検証する方針にした。

## SCN-MEGA-BASE-RWDI: 起動画面の決定的な撮影の基準を RelWithDebInfo でも撮る
- status: done
- done-when: 以後の項目が RelWithDebInfo のビルド 1 つで起動画面の比較をできるよう、`SCN-MEGA-BASE-RWDI`（RelWithDebInfo・決定的な撮影）を撮る。Debug の基準 `SCN-MEGA-BASE` と並べて平均輝度の差と PSNR を PROGRESS に書く（構成の違いの差の大きさの記録。一致は求めない）。同じコードで撮り直した比較が pass。C++ は変えない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -Deterministic -OutDir .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI-check`
- paths: TASKS.md, PROGRESS.md
- notes: 撮影だけ。

## SCN-MEGA-ROOT-COOK: 指定した資産だけ、根の段を 1〜2 クラスタまで縮めて焼く
- status: done
- done-when: AssetSet の指定（例 `"dagRootMaxClusters": 2`）があるメッシュだけ簡略化を続け、根のクラスタ数・三角形数・誤差をログに出す。バナナの根が 2 クラスタ以下になり、PROBE-ROOT と同じ測り方で根だけの `VisRasterSw1` が 1/10 以下。1万本 close の撮影を開いて近景の形が変わらない。起動画面の比較が pass（他の資産は焼き直さない）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath Models/Generated/banana/banana.glb -Force`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ROOT-COOK/root --views overview --timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-ROOT-COOK`
- paths: Tools/AssetCook, Assets/AssetSets/MegaBananasModels.json, TASKS.md, PROGRESS.md
- notes: PROBE-ROOT で根の単価が問題でないと出たら、理由を書いて完了にする。縮まらない理由が UV の継ぎ目なら上の段だけ継ぎ目の固定を緩める。全体のクックが 10 分を超えるなら、バナナの指定だけを置いたディレクトリを `-SpecDir` に渡す。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000,mega-lod-error-px=100000 -OutDir .harness/runs/scene-capture/SCN-MEGA-ROOT-COOK/root`（ランナーは撮り直さず、保存した結果を検査する）。

## SCN-MEGA-STRESS-RESIDENT: 開発用シーン mega-stress を作り、多種のメッシュの個別 Entity を置く
- status: done
- done-when: `mega-stress`（開発ビルドだけランチャーに出る）が、メッシュの一覧の JSON（起動画面の Boulder・Cottage・BigCobbleSphere・ScanProps、生成した watchtower・banana、クック済みなら Poly Haven の岩）から個別の Entity を `--stress-entities=<n>` 個置く（既定 2000）。資源の数と区間の数をログに出す。5000 個の撮影を開いて多種のメッシュが並んでいる。cycle smoke が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000 -OutDir .harness/runs/scene-capture/SCN-MEGA-STRESS-RESIDENT`
- paths: Game, Assets/AssetSets, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 計測は SCN-MEGA-STRESS-STREAMING でまとめて行う。

## SCN-MEGA-STRESS-MOVING: mega-stress に一部の Entity が毎フレーム動く負荷を足す
- status: done
- done-when: `--stress-moving-percent=<p>` で、その割合の Entity を毎フレーム動かす（位置と回転。決定的な動き）。5000 個・10% 移動で、時刻を変えた 2 枚の撮影を開いて動いていることを確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=10 -OutDir .harness/runs/scene-capture/SCN-MEGA-STRESS-MOVING`
- paths: Game, TASKS.md, PROGRESS.md
- notes: 計測は SCN-MEGA-STRESS-STREAMING でまとめて行う。

## SCN-MEGA-STRESS-STREAMING: mega-stress に区画の読み込み・解放の負荷を足す
- status: done
- done-when: `--stress-stream-cell=<m>` で、その大きさの区画の Entity を一定の周期で読み込み・解放する（カメラの近くの区画が出入りする）。1周期ごとに、生きている Entity・World の Entity・材質・メッシュ・モデルの数が置き終えた直後の基準に戻ることをログ `MEGA_STRESS_STREAM_CYCLE ... result=pass` で確かめ、`result=fail` が1件も出ない。cycle smoke が素の引数と区画の引数の両方で pass。基準の計測は SCN-MEGA-STRESS-BASELINE で行い、この項目では GPU 時間を撮らない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo -GameArgs stress-entities=2000,stress-stream-cell=64,stress-stream-period=0.05 -OutDir .harness/runs/scene-cycle-smoke-stream`
- verify: `python -c "import re,sys; t=open('.harness/runs/scene-cycle-smoke-stream/Game.log',encoding='utf-8',errors='replace').read(); r=re.findall(r'MEGA_STRESS_STREAM_CYCLE [^\n]*result=(\w+)',t); print('STREAM_CYCLES',r); sys.exit(0 if r and set(r)=={'pass'} else 1)"`
- paths: Game, TASKS.md, PROGRESS.md
- notes: 実装は途中保存のコミット a005038e・403e93b1 に入っている（403e93b1 で足した材質・メッシュ・モデルの数の照合はまだビルドしていない）。続きから仕上げる。滞在中に区画の1周期が終わらないときは、区画の引数の verify の GameArgs の数値（個数・周期）だけをこの項目の中で調整してよい（検査の中身は弱めない）。2026-10-10 親: 2回続けて40分を超えたため、基準の計測を SCN-MEGA-STRESS-BASELINE へ分けた（2万個の今の経路はゲームスレッドが約480 ms/フレームで、3視点×300フレームの撮影1本に約12分かかり、3本で反復に収まらない）。Game の起動と撮影は1本ずつ順に行い、並行させない（並行した撮影で trace.csv を別のプロセスが使っていて読めずに失敗した）。

## SCN-MEGA-STRESS-BASELINE: mega-stress の今の経路の基準を測り、PROGRESS の表に書く
- status: backlog
- done-when: 2万個の静止・1% 移動・1% 移動 + 64 m 区画の3本を、既定の視点だけ・`-GpuTimingFrames 240`（有効な標本180）で撮り、3本とも CheckCaptureMetrics が pass。gt・rt・GPU の median と p95、`MEGA_CAPACITY` を PROGRESS の表に書く（以後の節目の計測はこの表と比べる）。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/static --views default --timing --min-frames 180`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/moving --views default --timing --min-frames 180`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/stream --views default --timing --min-frames 180`
- paths: TASKS.md, PROGRESS.md
- notes: コードを変えない（変えると Game.exe が新しくなり、撮影が古いと判定される）。撮影は次の3本を1本ずつ順に（並行させない。1本あたり約4分）。ランナーは撮り直さず、保存した結果を検査する。static: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views default -GpuTimingFrames 240 -TimeoutSeconds 900 -GameArgs stress-entities=20000 -OutDir .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/static`。 moving: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views default -GpuTimingFrames 240 -TimeoutSeconds 900 -GameArgs stress-entities=20000,stress-moving-percent=1 -OutDir .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/moving`。 stream: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views default -GpuTimingFrames 240 -TimeoutSeconds 900 -GameArgs stress-entities=20000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-STRESS-BASELINE/stream`。 2026-10-10 親が backlog にした: ECS で作り変えが決まっている経路の基準は、比べても判断が変わらない（合否は目標の絶対値で決める）。既に撮った 2万個の値は PROGRESS に参考値として残した。
## SCN-ECS-CHECK-GATE: 開発ビルドの検査の結果と、撮る構成のビルドを撮影の門にする
- status: done
- done-when: `Debug/EngineChecks.h` の口が、`--engine-checks=<名前,...>`（開発ビルドだけ）で有効にした検査について `ENGINE_CHECK name=<名前> result=pass|fail count=<n>` を出す。`CaptureScene.ps1` と `CaptureStartupScene.ps1` に `-RequireChecks` を足す（各名前の pass が 1 回以上あり、fail が 0 でなければ失敗）。3 つのスクリプトに `-Build` を足す（撮る構成の Game をビルドし、失敗したら撮らずに失敗）。`CaptureStartupScene.ps1` に `-GameArgs` を足す。`RunSceneCycleSmoke.ps1` は fail の行で落ちる。`ENGINE_CHECK_SELFTEST` の pass の撮影が通る。`--engine-checks-selftest-fail` で落ちる出力は、反復の中で 1 回撮って PROGRESS に貼る。 あわせて、`CaptureScene.ps1` の計測（`-GpuTimingFrames`）は初期配置と公開の完了（SceneReady）の後に始め、ウォームアップと末尾の除外を別に取って、有効な標本を指定の数だけ得る（今は「指定 − 60」件に制限している、315 行付近）。配置した数（シーンが出す数）と計測の開始のフレームを metrics.json に記録し、`CheckCaptureMetrics.py` の `--min-frames` が有効な標本の数を見る。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks ENGINE_CHECK_SELFTEST -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-CHECK-GATE`
- paths: Library/Core/Public/Debug, Library/Core/Private/Debug, Library/Core/CMakeLists.txt, Game/GameApplicationHandler.cpp, Scripts/CaptureScene.ps1, Scripts/CaptureStartupScene.ps1, Scripts/RunSceneCycleSmoke.ps1, TASKS.md, PROGRESS.md

## SCN-ECS-CORE-CHUNKS: 実体の表・アーキタイプ・16 KB のチャンク・行の作成と削除を足す
- status: done
- done-when: 設計 1.2・1.3 の実体の表・アーキタイプ・チャンク・行の作成と削除（末尾との入れ替え）・まとめての作成が入る。`EcsStorageTest`（EntityTreeTest の束）が次を確かめる: Generation で古い識別子を拒む、チャンクを解放して再利用できる、100万行を作って消すと行とチャンクの数が元に戻る。まだどこからも使わない。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^EcsStorageTest$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/CMakeLists.txt, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsStorageTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-CORE-MOVE: 列の追加・削除（アーキタイプ間の移動）と、列の型の事前登録を足す
- status: done
- done-when: 列を足したり外したりすると行が別のアーキタイプへ移り、値と EcsEntity が保たれる。列の型は `EcsRegisterColumn<T>()` で登録して番号を固定し、未登録の型を並列の中で使うと開発ビルドで assert する（設計 1.2）。`EcsMoveTest` が、移動の前後の値・移動元の入れ替え・未登録の検出を確かめる。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsStorageTest|EcsMoveTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/CMakeLists.txt, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsMoveTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-CORE-CHANGES: クエリと、版・変更ログを足す
- status: done
- done-when: 設計 1.2 の版と変更ログ、クエリ（All/Any/None・タグ・孤児の除外）、チャンクの版による絞り込み、消費者ごとの既読が入る。`EcsChangeTest` が次を確かめる: 書いた行だけが 1 回ずつログに出る、段の外の書き込みが新しい版になって読み落としが無い、移動や入れ替えで動いた行が「変わった」になる、2 つの消費者の既読が独立している。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsStorageTest|EcsMoveTest|EcsChangeTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/CMakeLists.txt, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsChangeTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-CORE-COMMANDS: コマンドバッファと、理由つきの作成・削除のフックを足す
- status: done
- done-when: `EcsCommandBuffer` が入る（仕事ごとの束、決定的な再生、Generation の照合で古いものを捨てる、作成の予約札の取り消し）。列の型ごとの OnCreate/OnDestroy は、理由 Destroy と Move を区別し、Move では Destroy を呼ばない。`EcsCommandTest` が次を確かめる: 再生の順がスレッドの数によらない、取り消した予約は再生されない、Move で Destroy のフックが呼ばれない。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsStorageTest|EcsChangeTest|EcsCommandTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/CMakeLists.txt, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsCommandTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-JOBS: JobSystem に、バッチ専用のカーソルで働く ParallelFor を足す
- status: done
- done-when: 設計 1.7 の ParallelFor が入る。`JobParallelForTest`（JobSystemShutdownTest の束）が次を確かめる: 全ワーカーを長いタスクで塞いでも呼び手だけで終わる、各番号がちょうど 1 回走る、返った後に起きた補助が関数を呼ばない、StopAcceptingTasks の後でも終わる、入れ子、例外の投げ直し。既存の JobSystemShutdownTest も通る。
- verify: `cmake --build build --config Debug --target JobSystemShutdownTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(JobParallelForTest|JobSystemShutdownTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Thread/JobSystem.h, Library/Core/Private/Thread/JobSystem.cpp, Test/Thread/CMakeLists.txt, Test/Thread/JobParallelForTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-SCHEDULER: 段・波・順序付きの GameThread 系のスケジューラを足す
- status: done
- done-when: 設計 1.7 の SystemDesc・波・順序付きの GameThread 系・参加の番号が入る。`EcsSchedulerTest` が次を確かめる: 衝突するシステムが同時に走らない、波の仕事が全システムのチャンクにまたがる、GameThreadOnly が呼び手のスレッドで単独で走る、RenderThreadOnly と循環する After を拒む、段の中の構造変更が同期点まで見えない、同じフレームに足した行が次の Tick まで呼ばれない、順序付きの系の順が TickDispatchLess と一致する。統計が有効な構成（Debug・RelWithDebInfo）では、スケジューラが段・系・同期点の実行を、冒頭の方針の名前（`Ecs.Stage.<段>`・`Ecs.Sys.<系>`・`Ecs.Sync.<段>`）で `Debug/Stats.h` の区間としてトレースに記録する。ワーカーで走ったチャンクの仕事は、そのワーカーのスレッドの番号で系の名前の区間に入る。名前は系の登録時に決めた文字列を使い、フレームごとに文字列を作らない。統計が無効な構成（Release）では何も記録しない。`EcsSchedulerTest` が、区間の名前・入れ子・ワーカーのスレッドの番号が記録されることを確かめる。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsSchedulerTest|TickDispatchTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Public/Debug/Stats.h, Library/Core/Private/Debug, Library/Core/CMakeLists.txt, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsSchedulerTest.cpp, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: 更新への参加の番号は「次の収集」の番号とし、通常の Tick と単独で回る `DispatchFixedTick`（World.cpp 1703 行付近）の両方で進める。同じフレームの複数の固定ステップでは番号を共有する。

## SCN-ECS-WORLD-SCHEDULE: World の更新を段で回す（中身は今の処理のまま）
- status: done
- done-when: World が EcsWorld と SystemScheduler を持つ。Tick・LateTick・DispatchFixedTick・SyncToSceneView と固定ステップのループ（ApplicationProcessor.cpp:2665-2676）が段を走らせる。今の処理（群ごとの配信・UpdateWorldTransforms・補間・同期・cleanup）は、段の GameThreadOnly のシステムとして同じ順・同じ位置で走る。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|WorldTimeChannelTest|WorldRenderInterpolationTest|ApplicationFixedStepPipelineTest|RenderInterpolationCadenceTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-WORLD-SCHEDULE`
- paths: Library/Core/Public/Object/World.h, Library/Core/Private/Object/World.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: 更新への参加の番号は「次の収集」の番号とし、通常の Tick と単独で回る `DispatchFixedTick`（World.cpp 1703 行付近）の両方で進める。同じフレームの複数の固定ステップでは番号を共有する。



## SCN-ECS-SCOPE-METRICS: 撮影が CPU の区間と GPU のパスの値を名前ごとに出し、名前で検査できるようにする
- status: done
- done-when: `CaptureScene.ps1` の計測（`-GpuTimingFrames`）が、トレースの全部の区間について、名前ごとのフレームあたりの合計（全スレッド）の median・p95・max と、区間が出たスレッドの数を metrics.json の `cpu_scope_ms` に書く（今の上位 15 だけをやめる）。名前の先頭 2 つの部分ごとの合計（`Ecs.Stage.*`・`Ecs.Sys.*`・`Ecs.Sync.*`・`RG.Record.*`）も書く。GPU のパスは今の median・p95・max を `gpu_pass_ms` に全部書く。`CheckCaptureMetrics.py` に `--require-scope <名前>`・`--require-pass <名前>`（無ければ失敗）と `--max-scope-median-ms <名前>=<ms>`・`--max-scope-p95-ms`・`--max-pass-median-ms`・`--max-pass-p95-ms`（繰り返し可）を足す。2000 個の短い計測で `Ecs.Stage.*` と各段の区間が出る。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 120 -GameArgs stress-entities=2000 -OutDir .harness/runs/scene-capture/SCN-ECS-SCOPE-METRICS`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-SCOPE-METRICS --views overview --timing --min-frames 120 --require-scope Ecs.Stage.* --require-scope Ecs.Sync.* --require-pass MegaGeometry`
- verify: `python -c "import subprocess,sys; r=subprocess.run([sys.executable,'Scripts/CheckCaptureMetrics.py','.harness/runs/scene-capture/SCN-ECS-SCOPE-METRICS','--views','overview','--timing','--require-scope','Ecs.Sys.NoSuchSystem']); sys.exit(0 if r.returncode==1 else 1)"`
- paths: Scripts/CaptureScene.ps1, Scripts/CheckCaptureMetrics.py, TASKS.md, PROGRESS.md
- notes: 2026-10-10 親が足した（ユーザー指摘: 全体の負荷だけでは効果が分からない。CPU は関数単位の内訳が要る）。区間の記録はスケジューラ（SCN-ECS-SCHEDULER）と World の段（SCN-ECS-WORLD-SCHEDULE）が入れる。この項目はスクリプトだけを変える。1 本目の verify の撮影は短い（2000 個・120 フレーム）。
## SCN-ECS-CLEANUP-LIST: 破棄の回収を、破棄予約の一覧だけにする
- status: done
- done-when: `MarkForDestroy`・`Object::Destroy`・Component の破棄予約を World の一覧に積み、`CleanupDestroyedObjects` は一覧だけを処理する（World.cpp:1781-1828 の全木の 2 回の走査をやめる）。検査 `WORLD_CLEANUP`（全木を走査して見つけた予約と一覧が一致）が pass する。破棄の契約のテストと周回が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|EntityTreeTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=WORLD_CLEANUP`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=WORLD_CLEANUP`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=WORLD_CLEANUP`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Library/Core/Private/Component/Component.cpp, Test/Core/Object/WorldTickGroupTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-PROP-STORAGE: 値を列に置くプロパティ（StorageKind::EcsColumn）を足す
- status: done
- done-when: 設計 1.5 の EcsColumn・GetStorageKind・TEcsColumnProperty・ECS_PROPERTY（EcsPropertyRef）・予備の値が入る。BuildClassInfoSnapshot は Storage を性質から取り、Get/Set を埋める。`EcsPropertyStorageTest`（ObjectReflectionTest の束）が、テスト用の Component で次を確かめる: 値が予備→列→予備と移る、ApplyValue・CopyValueFrom・既定値の適用が版を付ける、可変の GetValuePtr が nullptr を返す、BuildObjectSnapshot と保存の往復で値と Stable ID が変わらない。実際の型はまだ移さない。
- verify: `cmake --build build --config Debug --target ObjectReflectionTest SceneSerializerTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsPropertyStorageTest|RuntimeSchemaSnapshotTest|SchemaProjectionTest|SceneSerializerTest|SceneFileRoundTripTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Object/RuntimeSchema.h, Library/Core/Public/Object/IClass.h, Library/Core/Public/Object/Reflection.h, Library/Core/Private/Object/SchemaProjection.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsPropertyStorageTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-BIND-ENTITY: World に付いた全 Entity に行を持たせ、寿命の契約を入れる
- status: done
- done-when: 設計 1.3・1.4 のとおり、付け替え・破棄（cleanup・RemoveObject・ObjectHeap の DestroyNow）・シーンの出入りで行が作られて消え、解放の前に孤児になる。値はまだ Object にある。検査 `ECS_BIND`（生きた Entity と行が 1 対 1、Object の照合、Hierarchy が Outer/Inner と一致、孤児が残らない）が pass する。`EcsLifetimeTest` が、Tick と LateTick の間の DestroyNow・予約の直後の破棄・部分木の連鎖を確かめる。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsLifetimeTest|WorldTickGroupTest|EntityTreeTest|TransformHierarchyTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=ECS_BIND`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=ECS_BIND`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=ECS_BIND`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsLifetimeTest.cpp, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: `DestroyEntitySubtree`（World.cpp 1974 行付近）は `RemoveInner` を通らないので、切り離す前に部分木の行を無効化する共通の処理をここにも入れる。寿命のテストに通常の `RemoveObject` と Heap が持つ部分木の遅延回収を含める。

## SCN-ECS-BIND-FLAGS: Component の行と、flags・時間倍率・Tick の設定を列へ移す
- status: done
- done-when: 値を列に持つ Component の行（所有者の子の葉）を作る。Entity の bActive・bTickEnabled・bPendingDestroy・CustomTimeDilation と、Component の bEnabled・bTickEnabled・TickGroupConfiguration を列にする（設計 1.3。プロパティのものは ECS_PROPERTY）。旧の配信はこの列を読む。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|WorldTimeChannelTest|EntityTreeTest|TickGroupConfigurationTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-BIND-FLAGS`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Library/Core/Public/Component/Component.h, Library/Core/Private/Component/Component.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Object/EcsLifetimeTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-TRANSFORM-VALUES: Entity の変換の値を列へ移す
- status: done
- done-when: Position・Rotation・Scale を ECS_PROPERTY（EcsLocalTransform）にし、ワールドの変換のキャッシュを EcsWorldTransform にして、重複（Entity.h:370-371）を消す。変換の取得は値で返す。今の再帰（World.cpp:1399-1409）は、列を読み書きする形で残す。ブリッジ・プレハブ・保存の往復・スクリプトから見える位置が今と同じ値になる。起動画面とバナナの比較が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest SceneSerializerTest ScriptComponentTickTest NorvesLibBridgeAdapterScriptComponentTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(TransformHierarchyTest|EntityWorldAABBTest|EntitySubtreeSnapshotTest|PrefabRoundTripTest|SceneFileRoundTripTest|ScriptComponentTickTest|NorvesLibBridgeAdapterScriptComponentTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-TRANSFORM-VALUES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-ECS-TRANSFORM-VALUES/det`
- paths: Library/Core/Public/Object/Entity.h, Library/Core/Private/Object/Entity.cpp, Library/Core/Private/Object/World.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Game, Test/Core/Object, TASKS.md, PROGRESS.md

## SCN-ECS-TRANSFORM-PREP: 階層のシステムを旧の再帰と並べ、一致を確かめる（準備）
- status: done
- done-when: 設計 1.9 の階層のシステムが比較用の列へ求め、検査 `ECS_TRANSFORM` で旧の再帰と全行が一致する（移動・付け替え・Active の切り替え・シーンの出入りを含む）。まだ旧の再帰が正。`EcsHierarchyTest` が次を確かめる: 深い木、付け替え、非アクティブの伝播、並列と直列の結果の一致、出力の順の決定性。
- verify: `cmake --build build --config Debug --target EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(EcsHierarchyTest|TransformHierarchyTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks ECS_TRANSFORM -OutDir .harness/runs/scene-capture/SCN-ECS-TRANSFORM-PREP/check`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Private/Object/World.cpp, Test/Core/Object/CMakeLists.txt, Test/Core/Object/EcsHierarchyTest.cpp, TASKS.md, PROGRESS.md

## SCN-SCENE-CYCLE-SHARED-CACHE: 複数のシーンの周回で、シーンをまたいで残るキャッシュを基準に含めない
- status: done
- done-when: `SceneCycleSmoke` が、2 つ以上のシーンを回すときは、各シーンの 2 周目（全シーンを 1 回ずつ訪れた後）を基準にして 3 周目以降と比べる（1 周目は、後のシーンが読むテクスチャのキャッシュがまだ無いので基準にしない）。このとき周回数が 3 未満なら `SCENE_CYCLE_TOO_FEW` で失敗する。1 つのシーンだけのときは今のとおり 1 周目を基準にする。使った基準の周回を `SCENE_CYCLE_BASELINE cycle=<n> scenes=<数>` でログに出す。3 周目以降で増え続けるもの（漏れ）は今のとおり失敗になる。 3 シーンの混在の周回と、シーン 1 つの周回が pass する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Game/Scenes/SceneCycleSmoke.cpp, Game/Scenes/SceneCycleSmoke.h, Scripts/RunSceneCycleSmoke.ps1, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。夜の間に SCN-ECS-RENDER-INVALIDATION・RENDER-SYNC-PREP・RENDER-SYNC-SWITCH・SCN-MEGA-SCENE-CONFIRM・SCENE-SLOTS の 5 つが、混在の周回の `SCENE_CYCLE_DIFF field=textures,texture_bytes`（別シーンが先に読んだテクスチャのキャッシュ。1 周目 0 → 2 周目 9 など）で止まった（各 `blocked/<項目>.md` の推奨 1）。変更前でも同じで、ECS の変更とは関係ない。5 つは実装済みで、この項目の後に status を todo に戻してあり、verify と評価だけをやり直す。


## SCN-ECS-FIX-DESTROY-FLAGS: 公開の Destroy() と Heap の破棄予約でも、ECS の破棄の印を立てる
- status: done
- done-when: `Entity::Destroy()`（`Object.cpp` 197 行付近の Object のフラグだけを変える経路）・World の予約登録（`World.cpp` 2336 行付近）・`ObjectHeap::EnqueueDestroy()` のどれから破棄を予約しても、`MarkForDestroy()` と同じ処理で ECS の破棄の印と変更ログが更新され、祖先の破棄予約で子・孫の Tick が呼ばれない（`World.cpp` 1431 行付近が古い `EffectiveActive=true` を使わない）。`EcsHierarchyTest` に、直接の `Destroy()` で子・孫が呼ばれない（count=0）回帰の検査と、Heap 経由の予約（`EnqueueDestroy`）が同じ入口で破棄の印を立てる検査を足す（Heap が持つ Entity は World の根にも子にも入らない＝`World.cpp` 4845・4879 行付近で拒否するので、Heap 経由の予約が Tick の配信に届く経路は無い）。通ったら SCN-ECS-TRANSFORM-SWITCH の status を done にし、`blocked/SCN-ECS-TRANSFORM-SWITCH.md` に解消を書き足す（残りの指摘はこの 1 点だけ）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(TransformHierarchyTest|EcsHierarchyTest|WorldTickGroupTest|SpringArmComponentTest|ApplicationFixedStepPipelineTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Object, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。SCN-ECS-TRANSFORM-SWITCH の評価の 2 周目の指摘（`blocked/SCN-ECS-TRANSFORM-SWITCH.md`）。評価者の最小の修正候補は、`Entity::Destroy()` をオーバーライドして ECS の破棄の印も更新し、`MarkForDestroy()` を同じ処理へまとめること。新しいテストの実行ファイルは足さない（既存の束に入れる）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: 評価の 2 周目の残りは「Heap 経由で子・孫の count=0」だったが、Heap が持つ Entity は World に入らないので起き得ない（親の完了条件の誤り）。完了条件をその所有権の規則に合わせて直し、指摘 1（World の登録の経路）は評価者が解消を確認済みなので done にした。


## SCN-ECS-FIX-SCENEQUERY-HOOKS: SceneQuery が切り替えた前の World と、破棄後に残る行の削除フックを外す
- status: done
- done-when: SceneQuery が World を切り替えるとき（`SceneQuery.cpp` 927 行付近）に前の World の行の削除フックを外し、SceneQuery の破棄でも登録したすべての World のフックを外す（または、フックのデータを World 側が登録の間だけ持つ）。World A・B を生かしたまま `Update(A)` → `Update(B)` → `Clear()` → SceneQuery を破棄 → A の Entity を削除、の順で解放後の参照が起きないことを、既存の SceneQuery の試験の束に足した検査で確かめる（Debug で走らせる）。通ったら SCN-ECS-SCENEQUERY の status を done にし、`blocked/SCN-ECS-SCENEQUERY.md` に解消を書き足す（残りの指摘はこの 1 点だけ）。
- verify: `cmake --build build --config Debug --target SceneQueryRaycastTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SceneQueryRaycastTest|SceneQueryOverlapTest|SceneQueryPhysicsFacadeTest|SceneCameraProbeTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks ECS_SCENEQUERY -OutDir .harness/runs/scene-capture/SCN-ECS-FIX-SCENEQUERY-HOOKS/check`
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Public/Object/World.h, Library/Core/Private/Object/World.cpp, Test/Core/Object, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。SCN-ECS-SCENEQUERY の評価の 2 周目の指摘（`blocked/SCN-ECS-SCENEQUERY.md`。前回の指摘 3 が未解決）。新しいテストの実行ファイルは足さない（既存の束に入れる）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。


## SCN-ECS-TRANSFORM-SWITCH: 階層のシステムを正にする（切替）
- status: done
- done-when: 旧の再帰の呼び出し（World.cpp:920・942・1094・1356、ApplicationProcessor.cpp:2668・2673）を、段の終わりと固定ステップの前後の伝播に替える。公開している `UpdateWorldTransforms` は伝播を呼ぶ形で残す（SpringArmComponentTest.cpp:312 が呼んでいる）。`CanDispatchEntity` は EffectiveActive を読む。`--ecs-legacy-transform` で旧経路に戻せる。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(TransformHierarchyTest|EcsHierarchyTest|WorldTickGroupTest|SpringArmComponentTest|ApplicationFixedStepPipelineTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-TRANSFORM-SWITCH`
- paths: Library/Core/Public/Object/World.h, Library/Core/Private/Object/World.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md

## SCN-ECS-INTERP: 描画の変換の補間をシステムにする
- status: done
- done-when: 設計 1.9 の補間（タグ・履歴・描画の変換の列・Prepare/Capture・履歴を消すきっかけ）にし、World.cpp:1314-1397 の全走査をやめる。処理するのは、補間する行とその子孫と、変わった行だけ。検査 `ECS_RENDER_TRANSFORM`（旧の式で全行を求めた値と一致）が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest PhysicsDynamicsTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldRenderInterpolationTest|RenderInterpolationCadenceTest|PhysicsFixedStepPipelineTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks ECS_RENDER_TRANSFORM -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-INTERP`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/Entity.h, Library/Core/Private/Object/Entity.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md

## SCN-ECS-RENDER-COLUMNS-MESH: Mesh・MegaGeometry・SkinnedMesh の描画の値を Component の行の列へ移す
- status: done
- done-when: 可視・影・レイヤー・メッシュの handle（Object の参照でないもの）を ECS_PROPERTY にし、Component の行を所有者の子の葉にする。同期はまだ全走査で、読む先だけが列になる。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target SceneViewProxyLookupTest WorldSyncDifferentialTest SceneSerializerTest SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SceneViewProxyLookupTest|WorldSyncDifferentialTest|SceneFileRoundTripTest|SkinnedRenderPathContractTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RENDER-COLUMNS-MESH`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Private/Object/World.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md

## SCN-ECS-RENDER-COLUMNS-LIGHT: Light 系と Board の描画の値を Component の行の列へ移す
- status: done
- done-when: RENDER-COLUMNS-MESH と同じ形で、光の値と Board の値を列にする。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target EntityTreeTest BoardComponentRoutingTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(LightComponentBuildProxyTest|BoardComponentRoutingTest|BillboardComponentTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RENDER-COLUMNS-LIGHT`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md

## SCN-MEGA-FIX-DELTA-SKIP: 描画の見送りの後の空の差分で、変わった席と前の変換の付け替えを取り逃がさない
- status: done
- done-when: (1) 描画を見送ったフレームの変わった席は、消費者（MegaGeometryPass）が取り込むまで保つ。または、変わった席と版が対応しないときは全量に戻す（`MegaGeometryMirror.h` 71 行付近は空の差分でも変わった席を消し、`MegaGeometryPass.cpp` 633 行付近がそれを適用済みとして扱う。再現: 影ありで描画 → 影なしの差分を適用して見送り → 空の差分で描画 で、写しは影なし・Pass は影ありのまま）。(2) 写しの前の変換の付け替え（`MegaGeometryMirror.h` 176 行付近）はそのフレームだけにし、次のフレームで履歴が落ち着く席も消費者へ知らせる（再現: 10 で x=0 を描画 → 11 で x=10 を適用して見送り → 12 で静止した x=10 を描画 → 13 で空の差分を描画 で、13 の前の変換が 10 になる）。2 つの再現を `WorldSyncDifferentialTest` の検査として足し（新しい実行ファイルは足さない）、Debug で pass。通ったら SCN-MEGA-SCENE-DELTA の status を done にし、`blocked/SCN-MEGA-SCENE-DELTA.md` に解消を書き足す。
- verify: `cmake --build build --config Debug --target WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^WorldSyncDifferentialTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-DELTA-SKIP`
- paths: Library/Core/Public/Rendering/MegaGeometryMirror.h, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Test/Core/Rendering/WorldSyncDifferentialTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。SCN-MEGA-SCENE-DELTA の評価の 2 周目の指摘 2・3（`blocked/SCN-MEGA-SCENE-DELTA.md`）。前の変換を最後に描いたフレームで保つ本格の直しは SCN-MEGA-GPU-SCENE-HISTORY。この項目は今の付け替えの退行だけを直す。


## SCN-ECS-RENDER-INVALIDATION: 列の値の変化以外で描画が古くなる場合を通知にする
- status: done
- done-when: 設計 1.10 の無効化の通知（資源の handle → 使っている行の逆引き、ポーズの版、接続先の交換での全再公開）と、公開の状態の列・再試行の一覧が入る。同期はまだ全走査のまま。検査 `ECS_RENDER_INVALIDATION`（全走査で proxy が変わった行が、必ず通知か変更ログに載っている）が、読み込み中のシーンへの入場と周回で pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks ECS_RENDER_INVALIDATION -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RENDER-INVALIDATION`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=ECS_RENDER_INVALIDATION`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Private/Object/World.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が todo に戻した: 実装はコミット済み。止まった理由の混在の周回は SCN-SCENE-CYCLE-SHARED-CACHE で直すので、その後に verify を通し直して評価を受ける（作り直さない）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RENDER-SYNC-PREP: 変わった行と Remove の一覧を作り、全走査と一致を確かめる（準備）
- status: done
- done-when: 段 RenderSync のシステムが、変更ログ・通知・公開の状態から「変わった行」と Remove の一覧を作る。同期はまだ全走査。検査 `ECS_RENDER_SYNC`（一覧と全走査で見つけた変化が一致。移動・ストリーミング・非表示・シーンの切り替えを含む）が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64,ecs-legacy-sync -RequireChecks ECS_RENDER_SYNC -OutDir .harness/runs/scene-capture/SCN-ECS-RENDER-SYNC-PREP/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=ECS_RENDER_SYNC,ecs-legacy-sync`
- paths: Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Private/Object/World.cpp, Library/Core/Public/Component, Library/Core/Private/Component, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が todo に戻した: 実装はコミット済み。止まった理由の混在の周回は SCN-SCENE-CYCLE-SHARED-CACHE で直すので、その後に verify を通し直して評価を受ける（作り直さない）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RENDER-SYNC-SWITCH: Mesh・Skinned・Light・Board の同期を、変わった行だけにする（切替）
- status: done
- done-when: 次をやめる: 生存集合と `RemoveStale*`（World.cpp:1225-1261）、dirty でない行の作り直し（1497-1511）、ComponentDataRegistry への毎フレームの公開（1448-1451・1542-1551）。MegaGeometry は SCENE-DELTA まで旧経路のまま。`--ecs-legacy-sync` で戻せる。下のテストと起動画面の比較・周回が pass する。
- verify: `cmake --build build --config Debug --target SceneViewProxyLookupTest WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SceneViewProxyLookupTest|SceneViewProxyReconcileTest|WorldSyncDifferentialTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RENDER-SYNC-SWITCH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が todo に戻した: 実装はコミット済み。止まった理由の混在の周回は SCN-SCENE-CYCLE-SHARED-CACHE で直すので、その後に verify を通し直して評価を受ける（作り直さない）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-SCENEQUERY: SceneQuery を、境界の変化からの refit と追加・削除にする
- status: done
- done-when: 毎フレームの Rebuild（ApplicationProcessor.cpp:2123）をやめ、境界の変更ログからの refit と追加・削除にする。作り直すときの分割は O(N log N) にする（SceneQuery.cpp:465-475 の挿入ソートをやめる）。静止では更新 0 件、1% 移動では移動数に比例する（開発ログ `SCENEQUERY updated=`）。検査 `ECS_SCENEQUERY`（全量で作った結果と照会の結果が一致）が pass する。
- verify: `cmake --build build --config Debug --target SceneQueryRaycastTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SceneQueryRaycastTest|SceneQueryOverlapTest|SceneQueryPhysicsFacadeTest|SceneCameraProbeTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks ECS_SCENEQUERY -OutDir .harness/runs/scene-capture/SCN-ECS-SCENEQUERY/check`
- paths: Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md

## SCN-MEGA-SCENE-CONFIRM: パケットの番号・世代と、RenderThread の適用の確認を先に入れる（準備）
- status: done
- done-when: パケットがシーンの世代と送った番号を持つ。RenderThread はマルチ・シングルのどちらの経路でも（RenderWorld.cpp 353 行付近の直接の経路を含む）、共通の場所で次を行う: 適用済みより古いパケットを捨てる、適用した番号と GPU の提出番号の対応を持つ、適用した番号・世代・完了した提出番号を atomic で公開する（描画を見送っても、適用したら確認する）。全量の写しもまだ運ぶ。検査 `MEGA_SCENE_CONFIRM`（確認が単調、飛んだパケットの差分は積み直される、世代が替わったら古い差分を拒む）が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks MEGA_SCENE_CONFIRM -OutDir .harness/runs/scene-capture/SCN-MEGA-SCENE-CONFIRM/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-stress,mega-bananas -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=MEGA_SCENE_CONFIRM`
- paths: Library/Core/Public/Rendering/FramePacket.h, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が todo に戻した: 実装はコミット済み。止まった理由の混在の周回は SCN-SCENE-CYCLE-SHARED-CACHE で直すので、その後に verify を通し直して評価を受ける（作り直さない）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-SCENE-SLOTS: MegaGeometry の行に席の列を持たせ、席の寿命の規則を入れる（準備）
- status: done
- done-when: Object の行も軽い行も `EcsGpuSceneSlot` を持つ。席の解放は、理由が Destroy の削除のフックでだけ行い、移動・チャンクの返却・入れ替えでは解放しない（設計 1.10）。空きリストは範囲単位で、範囲の AABB は所属する全席を包む。解放した席の再利用は、CONFIRM の確認と、その席を最後に使った提出の完了の両方を待つ。検査 `MEGA_SLOTS`（重複・包含の外れ・早すぎる再利用が 0）が、ストリーミングと周回で pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks MEGA_SLOTS -OutDir .harness/runs/scene-capture/SCN-MEGA-SCENE-SLOTS/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-stress,mega-bananas -Cycles 3 -Configuration RelWithDebInfo -GameArgs engine-checks=MEGA_SLOTS`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Rendering/SceneView.h, Library/Core/Private/Rendering/SceneView.cpp, Library/Core/Public/Rendering/SceneProxy.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Public/Component, Library/Core/Private/Component, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が todo に戻した: 実装はコミット済み。止まった理由の混在の周回は SCN-SCENE-CYCLE-SHARED-CACHE で直すので、その後に verify を通し直して評価を受ける（作り直さない）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-SCENE-DELTA: MegaGeometry の同期を、変わった行と Free だけにする（切替）
- status: done
- done-when: 次をやめる: `RemoveStaleMegaGeometryProxies`、全量の写し（RenderingCoordinator.cpp:2119）、`ApplyPreviousObjectStates` の毎回の作り直し。消費者のうち Pass と VSM は、RenderThread の写しと「今フレーム変わった席」を読む（CSM は SCN-MEGA-SHADOW-DIRTY、RT は SCN-MEGA-RT-SELECT で切り替える）。`--mega-scene-full-sync` で全量に戻せる（ECS の全行を数える）。両方の経路で起動画面の比較が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-SCENE-DELTA`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -Build -Configuration RelWithDebInfo -Deterministic -GameArgs mega-scene-full-sync -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-SCENE-DELTA-full`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Game, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親: 評価の 2 周目の指摘（`blocked/SCN-MEGA-SCENE-DELTA.md`）のうち、CSM（ShadowMapPass）と RT の消費者の切り替えは、それぞれを直す SHADOW-DIRTY・RT-SELECT へ移した（計画の順の判断）。残る 2 点（見送りの後の空の差分、TAA の付け替えの残り）は SCN-MEGA-FIX-DELTA-SKIP で直し、通ったらこの項目を done にする。

## SCN-ECS-PACKET-DELTA: Mesh・Skinned・Light の全量の写しと、構成の hash の全走査をやめる
- status: done
- done-when: SCENE-DELTA と同じ確認の経路で、Mesh・Skinned・Light の proxy を差分で運び、RenderThread の写しへ当てる（RenderingCoordinator.cpp:2116-2118 の全量の写しをやめる）。構成の hash（189-195）は差分のたびに増分で更新する。検査 `PACKET_DELTA`（差分を当てた写しと全量が一致）と、起動画面の比較が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks PACKET_DELTA -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-PACKET-DELTA`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -RequireChecks PACKET_DELTA -OutDir .harness/runs/scene-capture/SCN-ECS-PACKET-DELTA/check`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-MASS-ROWS: 散布の Object と軽い実体でバナナを置く
- status: done
- done-when: 設計 1.11 の `MegaGeometryScatterComponent` が入る（入力の番号の表、範囲の連続の予約、Morton 順、範囲ごとの公開、Active・可視の範囲の印、破棄で全行と範囲を解放）。バナナが Object 1 つで置かれ（`MEGA_BANANAS_PLACED ... batches=1`。Entity の数が本数によらない）、配置の値と順は今と同じで、1万本の比較が pass する。周回で行・席・資源の数が戻る。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-ECS-MASS-ROWS/det`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/CMakeLists.txt, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameModes/MegaBananas, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親: 評価の指摘 1（散布の proxy を ECS_RENDER_SYNC・RENDER_INVALIDATION の照合が残留と数える）の直し先の World.cpp（VerifyRenderSyncDelta の viewMega と全走査の scanStale の 2 か所）を paths に足して todo に戻した（blocked の推奨 1）。

## SCN-ECS-MASS-STREAM: 散布の作成・反映・解放を予算で分け、mega-stress の区画を散布で出し入れする
- status: done
- done-when: 作成、所有者の変換とメッシュの反映、解放を、1 フレームの行数と ms の予算で分け、範囲ごとに公開・解放する（設計 1.11）。mega-stress の区画が `--stress-batch-cells` で散布として出し入れされる。検査 `MASS_RANGES`（公開中の範囲は全行が揃っている、解放した範囲は描かない）が pass する。3万個 + 64 m 区画で gt の p95 を PROGRESS に書き、予算の既定値を決める（5万個は描画側の BVH の列が VRAM に載らず落ちるので、BVH の改修の後の節目の計測で測る）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64,stress-batch-cells -RequireChecks MASS_RANGES -OutDir .harness/runs/scene-capture/SCN-ECS-MASS-STREAM/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-stress,mega-bananas -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Game, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: 5万個は MegaGeometryPass の BVH の列（leaf_capacity 7,514 万・queue 1 億 6,517 万）が VRAM を超えて RenderThread が落ちる（`.harness/runs/scene-capture/SCN-ECS-MASS-STREAM/measure-default`）。このタスクの範囲の外なので、計測を 3万個に直して todo に戻した（blocked の推奨 1。3万個の値は反復 12 で測り済み: gt median 2.424 ms・p95 3.935 ms）。反復 12 の直しを verify に通して評価を受ける。

## SCN-MEGA-MIXED-LOAD: バナナ 100万本と mega-stress の個別 Entity を 1 回の起動で同時に置く
- status: done
- done-when: 1 回の起動で、散布の Object によるバナナ（`banana-count`）と mega-stress の個別 Entity（`stress-entities`・`stress-moving-percent`・`stress-stream-cell`）を同時に置ける（今は GameApplicationHandler.cpp 2721 行付近のモードの登録で別々のシーンへ渡している）。静止・カメラパスの再生・1% 移動・64 m 区画のストリーミングの 4 つの起動の引数を決めて PROGRESS に書き、実際に置いた数（バナナの本数・Entity の数）をログと metrics.json に出して、引数どおりであることを確かめる（3万本 + 5000 個の短い起動で確かめる。計測はしない。10万本以上の混在は BVH の改修の後の節目の計測で確かめる）。開発ビルドの `--camera-path-play` で、シーンのカメラパスを再生したまま撮れる（`--camera-path-time` は止めた視点。再生の開始の時刻はそれに合わせる。ログに再生中の時刻を出す）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs banana-count=30000,stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-MIXED-LOAD`
- paths: Game, Library/Core/Public/Input, Library/Core/Private/Input, Scripts/CaptureScene.ps1, TASKS.md, PROGRESS.md
- notes: GPT の 2 周目の指摘（ACCEPT-1M の混在の負荷を作る実装が無い）への対応。 2026-10-10 親: `--camera-path-play` を SCN-MEGA-PROBE-VSM（backlog にした）から移した。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: 10万本 + 5000 個は描画側の BVH の列が VRAM を超えて落ちる（leaf_capacity 5,759 万・queue 1 億 2,388 万、非プール 14.1 GB / 予算 11.7 GB）。この項目の範囲の外なので、verify を 3万本 + 5000 個に下げて todo に戻した（blocked の推奨 A）。

## SCN-MEGA-PROBE-VSM: 10万本で、読み込み直後とカメラが動くときの VSM の投影物のカリングを測る
- status: backlog
- done-when: 開発ビルドの `--camera-path-play` でカメラパスを再生したまま計測できる。静止と再生の 2 回について、`VsmCullPairs`・`VsmCullMega` の median・p95・max、`VSM_MEGA_CULL overflow` の最大、読み込み直後のフレームの値を PROGRESS に書き、100万本への外挿を書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-PROBE-VSM --views horizon --timing`
- paths: Game/GameModes/MegaBananas, Game/GameApplicationHandler.cpp, Library/Core/Public/Input, Library/Core/Private/Input, TASKS.md, PROGRESS.md
- notes: VSM のコードは変えない。静止の値は PROBE-100K の撮影を使ってよい。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 300 -GameArgs banana-count=100000,camera-path-play -OutDir .harness/runs/scene-capture/SCN-MEGA-PROBE-VSM`（ランナーは撮り直さず、保存した結果を検査する）。 2026-10-10 親が backlog にした: 古い経路（1本1Entity）では 10万本の撮影 1 回が約 19 分かかるため、ECS の大量の軽い実体で読み込みが速くなってから測る。 2026-10-10 ECS の大量の軽い実体（MASS-ROWS）の後に置いて todo に戻した。 2026-10-10 親が backlog にした: VSM の改修（VSM-RANGES・VSM-BVH-SUN/POINT）は結果によらず行うので、改修前の値は判断を変えない。`--camera-path-play` は SCN-MEGA-MIXED-LOAD へ移した。MEASURE-VSM の合否は絶対値にした。

## SCN-ECS-TICK-SIMPLE: Mesh・Board・Camera・Light 系・SpringArm・HoldSlot・SocketAttachment の Tick を順序付きの系へ移す
- status: done
- done-when: これらの Tick/OnTickGroup の上書きを消し、設計 1.12 のとおり登録する（順・channel・群は今と同じ）。Tick を直接呼んでいるテスト（FlipbookAdvanceTest.cpp:65、SpringArmComponentTest.cpp:311 など）も同じ項目で直す。検査 `ECS_TICK_DUP` が 0。
- verify: `cmake --build build --config Debug --target EntityTreeTest FlipbookAdvanceTest BoardComponentRoutingTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SpringArmComponentTest|FlipbookAdvanceTest|BoardComponentRoutingTest|BillboardComponentTest|WorldTickGroupTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks ECS_TICK_DUP -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-TICK-SIMPLE`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Private/Object/World.cpp, Test/Core/Object, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-TICK-ANIM: Animator と SkinnedMesh の更新を順序付きの系へ移す
- status: done
- done-when: OnTickGroup/Tick を消して登録する（Animation・PoseFinalize。イベントの配信の位置は今と同じ）。直接呼んでいるテストも直す。`-SkinnedProbe` の決定的な撮影が、最初の編集の前に `.harness/runs/startup-capture/SCN-ECS-TICK-ANIM-base` へ同じ引数で撮った基準と一致する。
- verify: `cmake --build build --config Debug --target SkeletalAnimationSamplingTest SkinnedRenderPathContractTest EntityTreeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(SkeletalAnimationSamplingTest|SkinnedRenderPathContractTest|WorldTimeChannelTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -SkinnedProbe -RequireChecks ECS_TICK_DUP -CompareDeterministicWith .harness/runs/startup-capture/SCN-ECS-TICK-ANIM-base -OutDir .harness/runs/startup-capture/SCN-ECS-TICK-ANIM`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Object, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-TICK-PHYSICS: CharacterBody と物理の一歩を、固定ステップの段へ移す
- status: done
- done-when: CharacterBody の 3 つの上書きを消して登録し、PhysicsModule の PreFixedTick と FixedTick を Fixed* の段のシステムにする（物理モジュールが登録、GameThreadOnly）。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target PhysicsDynamicsTest ApplicationFixedStepPipelineTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(PhysicsFixedStepPipelineTest|PhysicsCharacterMoveTest|CharacterPlatformMotionTest|PhysicsLifecycleEventTest|ApplicationFixedStepPipelineTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Modules/Physics, Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Modules/Physics, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-TICK-SCRIPT: ScriptComponent の Tick を順序付きの系へ移す
- status: done
- done-when: Tick を消して登録する（順は今と同じ）。スクリプトからの生成と破棄は、Object は即時、行は同期点で作られる。下のテストが pass する。
- verify: `cmake --build build --config Debug --target ScriptComponentTickTest ScriptApplicationLifecycleContractTest ScriptHotReloadTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(ScriptComponentTickTest|ScriptComponentSceneRoundTripTest|M6AngelScriptAcceptanceTest|ScriptApplicationLifecycleContractTest|ScriptHotReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Component/ScriptComponent.h, Library/Core/Private/Component/ScriptComponent.cpp, Library/Core/Public/Scripting, Library/Core/Private/Scripting, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Scripting, TASKS.md, PROGRESS.md

## SCN-ECS-TICK-GAME: Game の FollowCamera と QuadrupedLocomotion を順序付きの系へ移す
- status: todo
- done-when: OnTickGroup を消し、Game が起動時に登録する（TryGetComponentTickDelta と GetTickSerial の使い方は保つ）。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target LoggerSinkTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(QuadrupedLocomotionModelTest|CameraCollisionSolverTest|LocomotionProfileReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Game/Gameplay, Game/GameApplicationHandler.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-FRAME-HOOKS: GameMode・粒子・モジュール・handler の更新を段のシステムにする
- status: todo
- done-when: 次を設計 1.7 の段の GameThreadOnly のシステムとして、今と同じ順で走らせる（中身は持ち主に残す）: `UpdateGameModeStateMachine`、handler の OnUpdate/OnLateUpdate、`ParticleSystem::Tick`、モジュールの TickAll/LateTick/PreFixedTick/FixedTick、スクリプトの前後処理（ApplicationProcessor.cpp:2096-2132・2576-2590・2670-2672）。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target ApplicationFixedStepPipelineTest GameModeStateMachineTest LoggerSinkTest CanvasTransientBoardSnapshotTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(ApplicationFixedStepPipelineTest|GameModeStateMachineTest|GameModeRegistryTest|SubRoutineTest|ModuleRegistryTest|ParticleFrameSnapshotTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FRAME-HOOKS`
- paths: Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/Public/Module, Library/Core/Private/Module, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: 段は `ApplicationProcessor.cpp` 2096 行付近の実際の順（handler の更新は GameMode の更新より前、粒子の更新は固定ステップより前、`Module::TickAll` は RenderSync より後）で定義し、シミュレーションが止まっているときにどの呼び出しが走るかを表にして PROGRESS に残す。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-TICK-REMOVE: 仮想の Tick と、毎フレームの Tick の一覧を消す
- status: todo
- done-when: 次を消す: Component.h:82-89・102 の仮想（Tick・GetTimeChannel・OnTickGroup・FixedTick）、`Entity::Tick`/`TickComponents`、`TickDispatch.h`、`BuildTickSnapshot` と一覧（World.h:286-306）、旧の配信のシステム。Tick の設定の API は、BIND-FLAGS の列に書く形で残す。残っているテストを直し、全部 pass させる。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest PhysicsDynamicsTest ScriptComponentTickTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|WorldTimeChannelTest|EntityTreeTest|TickGroupConfigurationTest|ApplicationFixedStepPipelineTest|PhysicsFixedStepPipelineTest|ScriptComponentTickTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-TICK-REMOVE`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Object, Library/Core/Private/Object, Library/Modules/Physics, Game, Test, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-MEASURE-CPU: 節目の計測: ECS 化した CPU（ワールド・同期・ロジック）の効果を測る
- status: todo
- done-when: - 測る負荷: 5万個の静止・1% 移動・1% 移動 + 64 m 区画と、バナナ 1万本 overview・10万本 horizon。 - 問い: ECS 化した段・系・同期点のそれぞれが、本数と変化の数に対してどれだけかかり、並列の系がワーカーに広がっているか。 - 測る値: 段・系・同期点の区間（冒頭の方針の名前）ごとの median と p95、並列の系の区間が出たスレッドの数。全体の gt・rt・GPU は添える。PROGRESS の表に書く（合否は下の閾値で決め、改修前の値は撮らない）。 - 合格の条件: - 5万個の静止で、ゲームスレッドの段の合計 ≤ 2 ms（変換の伝播・描画の変換・RenderSync・SceneQuery・cleanup はそれぞれ ≤ 0.1 ms）。 - 1% 移動での増分が「変わった行 + 影響する子孫 + 補間の部分木」の数に比例する（数をログに出し、比を表に書く）。 - 10万本と 1万本の gt の差 ≤ 0.5 ms。 - 届かないスコープは `SCN-MEGA-FIX-<名前>` として足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/static --timing --min-frames 300 --max-gt-p95-ms 16.6 --require-scope Ecs.Stage.* --require-scope Ecs.Sync.TransformPropagate --require-scope Ecs.Sys.RenderTransform --require-scope Ecs.Stage.RenderSync --require-scope Ecs.Sys.SceneQuery --require-scope Ecs.Sync.Cleanup`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-CPU-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。反復の中で 1 回撮る: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/static`（他の負荷も同じ形で、出力先だけ変える） 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: 5万個の mega-stress は、SCN-MEGA-BVH-INDIRECT の前は BVH の列が VRAM を超えて落ちる（SCN-ECS-MASS-STREAM の記録）。落ちるときは CPU の段の値を 3万個で測って表に書き、5万個の値は BVH-INDIRECT の後の節目の計測（MEASURE-CULL）で足す。

## SCN-ECS-RETIRE-LEGACY: 旧経路と ComponentDataRegistry を消す
- status: todo
- done-when: 次を消す: `--ecs-legacy-transform`/`--ecs-legacy-sync`、旧の再帰（UpdateEntityTransformRecursive・SyncEntityRecursive・UpdateRenderTransformRecursive ほか）、ComponentDataRegistry とそのテスト・ベンチマーク。先に利用者を数え、残る利用者がいれば消さずに BLOCKED にする。`--mega-scene-full-sync` は残す。
- verify: `cmake --build build --config Debug --target EntityTreeTest SceneViewProxyLookupTest WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "World|SceneViewProxy|EntityWorldAABB|TransformHierarchy" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/CMakeLists.txt, Test/Core/Rendering, TASKS.md, PROGRESS.md

## SCN-MEGA-GPU-SCENE-PREP: シェーダーのインスタンスの読み方を 1 つの関数とメッシュ表へ寄せ、表の更新を共通の前段に置く（準備・見た目は不変）
- status: todo
- done-when: cull・visbuffer・vsm_mega・megageometry.vert の MegaInstance の直接の読みを `LoadMegaInstance(index)` に寄せる。メッシュ表（binding を追加）を書き、関数はメッシュ表から補って同じ値を返す。インスタンス表・メッシュ表の更新を、最初にそれを読むパス（影のパスを含む）より前の共通の前段に移す。起動画面・バナナの比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-PREP/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: GPUMegaInstance は `MegaGeometryPass.h` 437〜455 行付近、影の表は 37〜48 行付近。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-GPU-SCENE-SCATTER: インスタンス表を席ごとに GPU へ常駐させ、変わった席だけを散在更新する（切替）
- status: todo
- done-when: 表が席ごと 128 B の device-local になり、変わった席だけを staging から compute で散在書き込みする。インスタンス表・メッシュ表・常駐（ページ表）の 3 つの更新の条件を分け、常駐の表は両方の描画パスと VSM で同じ版を使う（今の `SyncPageTable` の frame slot ごとの版、`MegaGeometryPass.cpp` 2832 行付近）。インスタンスも常駐も変わらないフレームは `MEGA_SCENE_UPLOAD bytes=0`（ログで確かめる）。`--mega-scene-full-sync` との起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-GPU-SCENE-SCATTER`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-SCATTER/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（GPU メモリ）。転送の道は `RenderResources.cpp` 66・1561 行付近のリングと別の予算にする（最初に 1540〜1580 行付近を読んで決める）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-GPU-SCENE-GROW: 席の表の拡張を GPU のコピーで行い、古いバッファとメッシュ表の資源を GPU が使い終わるまで保つ
- status: todo
- done-when: 席の数が容量を超えたら新しい表へ GPU でコピーし、古いバッファは `m_RetiredBuffers` へ回す。メッシュ表が指す資源（頂点・インデックス・クラスタ・BVH・ページ表）は、その資源を最後に使った提出が終わるまで解放しない。ストリーミングで拡張と縮小が起きても `mismatch=0`、cycle smoke が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-GROW/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（寿命）。検証レイヤーは節目の計測の Debug で見る。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-GPU-SCENE-HISTORY: 前の変換（prevWorld）を、最後に描いたフレームを基準に保つ
- status: todo
- done-when: 席の prevWorld は `RenderedObjectHistory.h` 29 行付近の契約（最後に描いたフレームの変換）に従い、RenderThread 側で描画の履歴を持つ。動いて止まった席の 1 回だけの更新も描画の基準で行う（パケットの破棄・描画の見送りを挟んでも速度が 0 や二重にならない）。1% 移動で、開発ビルドの速度の検査（全量の経路の速度と一致）が `MEGA_HISTORY mismatch=0`。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-HISTORY/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-GPU-SCENE-HISTORY`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）席は行と一緒に動く。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-RT-SELECT: TLAS に入れるインスタンスを席ごとの配列で持ち、選ぶ規則を決める
- status: todo
- done-when: RT の instance 配列を席ごとに持ち、差分で直す。RT の入力（`RenderingCoordinator.cpp` 805 行付近の全量の読みと、連番のパストレーシングが使う GameThread の配列）を、RenderThread の写しと変わった席にする（SCN-MEGA-SCENE-DELTA から移した）。TLAS に入れるのは、カメラから 120 m 以内のインスタンスを近い順に上限 65,536 本まで（規則と外れた数を `RT_SCENE instances= excluded=` に出す）。この項目では Update は毎フレーム呼んでよい。起動画面の比較（RT の影・RTGI・DDGI を含む見た目）が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-RT-SELECT`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-RT-SELECT/check`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, TASKS.md, PROGRESS.md
- notes: 対象は 784〜850 行付近。RHI/Vulkan は変えない（`VulkanCommandList.cpp` 1302 行付近は Update のたびに全量を変換・転送する）。共有の TLAS は RT の影・DDGI も使うので、遠方の影響を RTGI だけと見なさない。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-RT-SKIP: TLAS の更新を、集合・変換・BLAS の版が変わったフレームだけにする
- status: todo
- done-when: frame slot ごとに、選んだ集合・変換・BLAS の版（準備完了・交換）を覚え、全部一致するフレームは Update を呼ばない（ログに `RT_SCENE update_skipped=` を出す）。カメラの移動で 120 m 内の集合が変わる・追加削除・BLAS の交換では呼ぶ。バナナと起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-RT-SKIP/det`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, TASKS.md, PROGRESS.md
- notes: 対象は 1037〜1085 行付近（同数なら毎フレーム Update、1043 行付近）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-SHADOW-DIRTY: VSM・CSM の無効化を、変わった席の旧・新の境界と、常駐・メッシュの変化の通知で行う
- status: todo
- done-when: VSM の動きの記録（`VirtualShadowMapPass.cpp` 736〜753 行付近）が、本数ではなく変わった席の数に比例する。バッチの一部が動いても無効化はその旧・新の境界だけ。静止した席でも、そのメッシュのページの常駐・メッシュの世代・影の設定が変わったら、使っている席・範囲へ通知して無効化する（今の署名に入っている `PageTableVersion` の役目を保つ）。光源・クリップマップ側の既存の無効化は残す。CSM の投影物の範囲（`DirectionalShadowLightMatrices.cpp` 283〜450 行付近）は範囲の AABB で求める。影の表の毎フレームの FNV（`MegaGeometryPass.cpp` 1331 行付近）をやめる。ShadowMapPass（CSM。`ShadowMapPass.cpp` 203 行付近）の MegaGeometry の全量の走査を、RenderThread の写しと変わった席にする（SCN-MEGA-SCENE-DELTA から移した）。起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-SHADOW-DIRTY`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-SHADOW-DIRTY/check`
- paths: Library/Core/Private/Rendering/VirtualShadowMapPass.cpp, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.cpp, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-MEASURE-UPLOAD: 節目の計測: GPU に常駐する表・RT・影の改修の効果を測る
- status: todo
- done-when: MEASURE-CPU と同じ 3 つの負荷とバナナ 3万本 overview で、gt・rt・GPU の median と p95、`MegaGeometryPass.RecordFrameCommand`、`MEGA_SCENE_UPLOAD bytes`、`AccelerationStructureBuild`、VSM の無効化のページ数を測って PROGRESS の表に書く。目標: 5万個で `RecordFrameCommand` ≤ 1 ms、静止で upload 0 と `AccelerationStructureBuild` ≤ 0.05 ms、1% 移動で upload・VSM の無効化が移動数に比例。カメラパスの再生中に TLAS の更新があるフレームの p95 も書く。1% 移動の連続撮影を開いて動く物に残像が増えていない（TAA と prevWorld）。届かないものは `SCN-MEGA-FIX-<名前>` を次へ足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/static --timing`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-UPLOAD-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。 verify はビルド・代表の計測 1 本・Debug の検証だけにしてある（ランナーの再検証を軽くするため）。done-when の他の計測は反復の中で行い、PROGRESS に証拠の場所を書く。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/static`（ランナーは撮り直さず、保存した結果を検査する）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-PLAN: 記録の前に、確保・保留コマンド・バリアの layout を直列で決める
- status: todo
- done-when: 設計 1.13 の記録の前の計画と、RHI の明示の old/new のバリア、最終の layout の書き戻しが入る。記録はまだ 1 本のリスト。`RenderGraphParallelPlanTest`（RenderGraphCompileTest の束）が、計画した layout が今の逐次の実行の layout と全バリアで一致することを確かめる。Execute の中で自分でバリアを張るパスの一覧を PROGRESS に書く。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RenderGraphParallelPlanTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PLAN`
- paths: Library/Core/Public/Rendering/RenderGraph, Library/Core/Private/Rendering/RenderGraph, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RenderGraphParallelPlanTest.cpp, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: `EndRenderPass`（VulkanCommandList.cpp 903 行付近）も共有の layout を書き換える。attachment の最終の layout とパスの内部の遷移も計画の対象にし、記録の最中に共有の layout を更新しない。計画できない Serial の区間は記録の後に状態を確定して次の区間を計画する。両方の経路を検証する。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-RHI-SUBMIT: 複数のコマンドリストを、同じ提出番号と完了通知で 1 回に提出する
- status: todo
- done-when: 次が入る: `ISwapChain::EndFrame` の複数リストの版、全リストへの提出番号・完了通知・中止、同じ slot が完了するまで記録し直さないこと、RenderingCoordinator が仕事の番号ごとに持つリスト。`RHIParallelRecordVulkanTest`（RHITextureUpdateVulkanTest の束）が次を確かめる: 2 つのスレッドで記録した「書き込み → バリア → 読み出し」の 2 本を 1 回で提出して結果が正しい、検証レイヤーのエラーが 0、中止の後に古いリストを再利用しない。描画はまだ 1 本。起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RHIParallelRecordVulkanTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-RHI-SUBMIT`
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.cpp, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RHIParallelRecordVulkanTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-RHI-TIMESTAMPS: GPU の時間をリストをまたいで集め、FrameGPU を先頭の開始から末尾の終了までにする
- status: todo
- done-when: リストごとに取った GPU の時間をパスの名前で合わせ、FrameGPU を先頭のリストの開始から末尾のリストの終了までで求める（RenderingCoordinator.cpp:1134-1145・2734-2764）。`RHIParallelRecordVulkanTest` に、2 本にまたがる FrameGPU が各区間の時間以上で、開始と終了の順が正しいことの検査を足す。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RHIParallelRecordVulkanTest$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Public/RHI, Test/Core/Rendering/RHIParallelRecordVulkanTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-RT-GRAPH: RenderGraph をパスの区間ごとに記録できるようにする
- status: todo
- done-when: 記録の方針（既定は Serial）と区間ごとの記録（区間ごとの context・保留コマンド・リスト、RT-PLAN の計画を使う、提出はグラフの順）が入る。`RenderGraphParallelRecordTest` が、偽のリストで次を確かめる: 区間の分け方、バリアが入る区間、Serial だけのグラフが今と同じ 1 本の記録になる。どのパスもまだ Parallel を宣言しない。各パスの記録（Execute）を、記録したスレッドの番号で `RG.Record.<パス>` の区間として残す（Serial でも区間ごとの記録でも。名前はパスの登録時の文字列）。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(RenderGraphParallelRecordTest|RenderGraphParallelPlanTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-GRAPH`
- paths: Library/Core/Public/Rendering/RenderGraph, Library/Core/Private/Rendering/RenderGraph, Library/Core/Public/Rendering/ViewRenderContext.h, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RenderGraphParallelRecordTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-PASSES: 監査したパスを並列に記録する
- status: todo
- done-when: MEASURE-CPU の rt の上位から、次の 3 つを満たすと監査したパスに Parallel を宣言する: Execute が記録だけ、自分でバリアを張らない、共有の CPU の状態を持たない（監査の表を PROGRESS に書く。共有のアロケータは区間ごとに分ける）。`--render-parallel-record=off` で 1 本に戻せる。on と off の両方で起動画面の比較が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PASSES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=off -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PASSES-off`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameApplicationHandler.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-MEASURE-RT: 節目の計測: 並列の記録の効果と安全を測る
- status: todo
- done-when: バナナ 1万本 overview と mega-stress 5万個で、on と off の rt の median と p95、`RenderGraph.Execute`、GPU、ワーカーの統計（JobSystem.h:133）、`RG.Record.*` の区間ごとの時間と記録したスレッドの数を PROGRESS の表に書く。合格は、on の rt の p95 が off 以下で、Debug の起動画面（on）で検証レイヤーのエラーが 0（Threading を含む）。外れたら off を既定にし、`SCN-ECS-FIX-<名前>` を足す。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-MEASURE-RT/on --timing --min-frames 300 --require-scope RG.Record.*`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-MEASURE-RT-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。反復の中で on と off を撮る（`CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000[,render-parallel-record=off] -OutDir .harness/runs/scene-capture/SCN-ECS-MEASURE-RT/<on|off>`）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-BVH-LEAF-SPLIT: BVH を持たないメッシュに、葉 8 クラスタ以下の階層の BVH を作る（準備）
- status: todo
- done-when: MegaGeometryResourceStore が、v1.0・手続き・実行時に作るメッシュ（BVH が無いもの）に、葉の上限 8 クラスタ（`CookedMeshFormat.h` 71 行付近）を守る多段の BVH を作る（空間で分けて葉を作り、上の段を組む）。作った BVH の検査（全クラスタが 1 回ずつ葉にある・節の境界が子を包む）を開発ビルドで行い、`MEGA_BVH_BUILD violations=0`。まだ描画には使わない。起動画面の比較と ctest が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-BVH-LEAF-SPLIT`
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest MegaGeometryResourcesTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "GeometryPageRequestVulkanTest|HiZOcclusionTestVulkanTest|MegaGeometryResourcesTest|MegaGeometryFrameCommandDebugModeTest" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Private/Rendering/MegaGeometryResourceStore.cpp, Library/Core/Private/Rendering/MegaGeometryResourceStore.h, Library/Core/Public/Rendering/MegaGeometry, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-FLAT-TO-BVH: 全インスタンスを BVH の経路で描く（平らな経路は残すが使わない）
- status: todo
- done-when: BVH-LEAF-SPLIT で作った BVH を使い、全インスタンスを BVH の経路へ流す。平らな経路（`cluster_cull.comp`・FirstGroup の接頭和、`MegaGeometryPass.cpp` 1264 行付近）は `--mega-flat-path` で戻せるように残す。起動画面・旋回の比較と ctest が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FLAT-TO-BVH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF -OutDir .harness/runs/startup-capture/SCN-MEGA-FLAT-TO-BVH-orbit`
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest MegaGeometryResourcesTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "GeometryPageRequestVulkanTest|HiZOcclusionTestVulkanTest|MegaGeometryResourcesTest|MegaGeometryFrameCommandDebugModeTest" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 起動画面の球・岩が通る経路。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-BVH-INDIRECT: BVH の段を間接 dispatch・固定容量の列にする
- status: todo
- done-when: 段 1 以降を GPU のカウンタから書いた引数で dispatch する（`ICommandList.h` 382 行付近）。列は固定容量で、書く前に境界を検査し、件数は頭打ち、引数は min(count, cap)、溢れを `bvh_overflow` に数える。2 パス目用の入力は別のバッファに持つ（`MegaGeometryPass.cpp` 1979 行付近の各パスの頭の消去と分ける）。`bvh_queue_bytes` が本数によらず一定（ログ）。バナナの比較と ctest が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CULL）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-BVH-INDIRECT/det`
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest MegaGeometryResourcesTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "GeometryPageRequestVulkanTest|HiZOcclusionTestVulkanTest|MegaGeometryResourcesTest|MegaGeometryFrameCommandDebugModeTest" --no-tests=error --output-on-failure --timeout 180`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 今は最悪値で dispatch（1983〜2009 行付近）、列は全節の総和（1351〜1373 行付近）。 2026-10-11 親: 今の BVH の列はインスタンスの数とメッシュから最大の容量を取るので、mega-stress 5万個で leaf_capacity 7,514 万・queue 1 億 6,517 万になり、VRAM（使用 14,975 MB / 予算 11,847 MB）を超えて RenderThread がバッファの割り当てに失敗して落ちる。この項目の固定容量の列で、5万個の mega-stress が落ちずに描けることを確かめる。 2026-10-11 親: バナナ 10万本 + mega-stress 5000 個の混在でも同じく VRAM を超える（SCN-MEGA-MIXED-LOAD の記録）。そのとき `VulkanBuffer.cpp` の割り当て失敗の例外が描画スレッドで捕まらず、Game が 0xC0000409 で落ちる。固定容量の列にしたうえで、列の割り当てに失敗したら落ちずにその描画を見送り、ERROR を出す形にする。10万本 + 5000 個の混在が落ちずに描けることも確かめる。

## SCN-MEGA-INSTANCE-CULL: 範囲とインスタンスの専用のカリングを、BVH の段 0 の前に置く
- status: todo
- done-when: 範囲（AABB）→ 席の順に判定する（1 席 1 スレッド。メッシュの球 × 倍率の締まった球で、視錐台・HZB・距離・画素の大きさ）。通った席だけを BVH の段 1 へ渡す。バナナの比較と起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CULL）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-INSTANCE-CULL/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 範囲の段は PROBE-100K が要ると判断したときだけ作る（要らなければ席の判定だけ）。 （ECS 計画の対応）範囲は、散布の連続した席の範囲。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-BINNING-PREP: 可視クラスタの全体の列と、区間 × ハード/ソフトの数え・接頭和を足す（準備・描画は旧経路）
- status: todo
- done-when: カリングが今の書き込みに加えて全体の列へも積む。数えと接頭和を計算し、開発ビルドで新旧を「席・メッシュ・クラスタ・LOD の集合」として比べる（順番では比べない。`MEGA_BIN mismatch=0`）。全体の枠と溢れを `MEGA_CAPACITY` に出す。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-PREP/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-PREP/check`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, TASKS.md, PROGRESS.md
- notes: 区間ごとに事前に容量を取る形（1526 行付近）は区間の数 × 419 万件まで膨らむ。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-BINNING-SWITCH: 描画・記録・ソフトの一覧を binning の結果で詰めて書く（切替）
- status: todo
- done-when: ハードの間接描画はハードの分だけになる（`SW_RASTER_BIN zeroed=0`）。records_args・records・ソフトの一覧を詰めた範囲で作り、記録の番号（25 bit、`VisibilityBuffer.h` 37 行付近）は両パスを通じて保ち、ハード・ソフトの一覧は同じ記録を参照する（2 パス目の詰め直しで 1 パス目が ID のバッファへ書いた番号を上書きしない）。`--mega-binning=off` で旧経路に戻せる。起動画面・バナナの比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CULL）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-BINNING-SWITCH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-SWITCH/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Private/Rendering/VisibilityRasterPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。GBuffer の経路（sink 無し）も同じ詰めた列を描く。今の番号の対応は `visbuffer_records.comp` 113 行付近。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-PREV-HIZ: HZB を 2 枚にし、描いた番号・VP（ジッタ込み）・寸法・シーンの世代を契約として持たせる（準備）
- status: todo
- done-when: 2 フレーム目から `MEGA_PREV_HIZ ready=1` が出る。エポックの先頭・解像度の変更・カメラの切れ目・全量の再同期・パケットの飛びでは `ready=0` と理由が出る。判定にはまだ使わない。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-PREV-HIZ`
- paths: Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Assets/Shaders/Common/MegaGeometryCull.glsl, TASKS.md, PROGRESS.md
- notes: HiZPyramid（`HiZPyramidPass.h` 28〜65 行付近）を 2 つ持つ。VP は自分で保存し、`PreviousMainCamera`（前のゲームパケット基準、`RenderingCoordinator.cpp` 2091 行付近）には頼らない。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-TWOPASS-INSTANCES: 範囲とインスタンスの 1 パス目を前フレームの HZB で判定し、隠れたものを 2 パス目へ回す（切替）
- status: todo
- done-when: 範囲・インスタンスの 1 パス目が前の HZB で判定し、隠れたものは 2 パス目の別の列へ積む。HZB は 1 パス目の深度（ソフトの合流の後、`VisibilityRasterPass.cpp` 1554 行付近）から作る。契約が合わないフレームは遮蔽を使わない。2 パス目の列が溢れたら、溢れた範囲を遮蔽なしで走査し直して描く（取りこぼさない）。`--mega-twopass=legacy` で旧経路に戻せる。旋回（遮蔽 Off との一致）と起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CULL）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF -OutDir .harness/runs/startup-capture/SCN-MEGA-TWOPASS-INSTANCES-orbit`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-TWOPASS-INSTANCES`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Library/Core/Private/Rendering/VisibilityRasterPass.cpp, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-TWOPASS-CLUSTERS: 節とクラスタの 2 パス遮蔽も同じ形にし、見えたビットを消す
- status: todo
- done-when: 節・葉・クラスタの 1 パス目も前の HZB で判定し、隠れたものを 2 パス目の列へ回す（溢れは TWOPASS-INSTANCES と同じ規則）。見えたビットと `UpdateVisibilityLayout`（`MegaGeometryPass.cpp` 3194〜3372 行付近）を消し、ページ要求のフレームの印（`visibleWriteStamp` 相当）は残す。旋回・バナナの比較と ctest が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CULL）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-TWOPASS-CLUSTERS/det`
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest MegaGeometryResourcesTest ViewportSnapshotDebugWiringTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "GeometryPageRequestVulkanTest|HiZOcclusionTestVulkanTest|MegaGeometryResourcesTest|MegaGeometryFrameCommandDebugModeTest" --no-tests=error --output-on-failure --timeout 180`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。GeometryPageRequestVulkanTest は `cluster_cull.comp` を直接使うので定数の変更に追従させる。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-MEASURE-CULL: 節目の計測: カリング・描画の改修の効果を測る
- status: todo
- done-when: バナナ 1万本 overview と 10万本 horizon で、GPU・gt・rt の median と p95、パスごとの時間、描いたクラスタ数、`visibility_bytes`・`bvh_queue_bytes`・溢れを測って PROGRESS の表に書く。目標: 1万本の Cull1+Cull2 ≤ 2.5 ms、`MegaGeometryDraw1` ≤ 0.8 ms、10万本の範囲 + インスタンスの判定 ≤ 0.3 ms、1万本の描いたクラスタ数が基準の ±1%、`visibility_bytes` のバナナの分が 0、溢れ 0。mega-stress 5万個で `command_bytes` が区間の数によらない。届かないものは `SCN-MEGA-FIX-<名前>` を次へ足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-CULL/10k --views overview --timing`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-CULL-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。 verify はビルド・代表の計測 1 本・Debug の検証だけにしてある（ランナーの再検証を軽くするため）。done-when の他の計測は反復の中で行い、PROGRESS に証拠の場所を書く。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-CULL/10k`（ランナーは撮り直さず、保存した結果を検査する）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-VSM-RANGES: VSM の組の判定を、受け手距離内の範囲とインスタンスに絞る
- status: todo
- done-when: 組の事前の絞り込み（`VirtualShadowMapRaster.cpp` 978 行付近）の前に範囲（AABB）の判定を置き、受け手距離 + 太陽方向の延長の外の範囲は組を作らない。起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-VSM）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-RANGES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-VSM-RANGES/check`
- paths: Assets/Shaders/vsm_mega_cull_pairs.comp, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering/VirtualShadowMapRaster.h, TASKS.md, PROGRESS.md
- notes: （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-VSM-BVH-SUN: 太陽の VSM のクラスタの選択を、BVH の段を正射影の LOD でたどる形にする
- status: todo
- done-when: 通った組ごとにクラスタを全部見る形（`vsm_mega_cull.comp` 155〜175 行付近）を、太陽のクリップマップについて BVH の段（BVH-INDIRECT と同じ列の安全策）でたどる形にする。点光源の面は旧経路のまま。`VSM_MEGA_CULL overflow=0`。起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-VSM）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-BVH-SUN`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-VSM-BVH-SUN/check`
- paths: Assets/Shaders, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-VSM-BVH-POINT: 点光源の VSM のクラスタの選択も BVH の段でたどる
- status: todo
- done-when: 点光源の面（立方体の各面の透視投影の LOD）でも BVH の段でたどる。夜の起動画面の撮影を開いて点光源の影が変わらない。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-VSM）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -Night -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-BVH-POINT`
- paths: Assets/Shaders, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: `CaptureStartupScene.ps1` の `-Night` が既存の `--night` を渡さないなら直してよい。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-MEASURE-VSM: 節目の計測: 影の改修の効果を測る
- status: todo
- done-when: 10万本 horizon の静止とカメラパスの再生で、`VsmCullPairs`・`VsmCullMega` の median・p95・max、`VSM_MEGA_CULL overflow` の最大、読み込み直後のフレームを測って PROGRESS に書く。目標: 再生の `VsmCullPairs` の p95 ≤ 1.0 ms、`VsmCullMega` の p95 ≤ 1.5 ms、overflow 0。点光源の影の計測（`VirtualizationAcceptance.md` の段9の測り方）が悪化しない。撮影を開いて影の欠けが無い。届かないものは `SCN-MEGA-FIX-<名前>` を次へ足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-VSM --views horizon --timing`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-VSM-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。 verify はビルド・代表の計測 1 本・Debug の検証だけにしてある（ランナーの再検証を軽くするため）。done-when の他の計測は反復の中で行い、PROGRESS に証拠の場所を書く。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 300 -GameArgs banana-count=100000,camera-path-play -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-VSM`（ランナーは撮り直さず、保存した結果を検査する）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-SCALE-300K: 30万本と mega-stress 10万個を、静止と移動で測る
- status: todo
- done-when: 30万本 horizon（静止・再生）と mega-stress 10万個（1% 移動・ストリーミング）について、GPU・gt・rt の median と p95、描いたクラスタ数、全溢れ、メモリ（設計の 3 GB の範囲）を `SceneLauncherAcceptance.md` に書く。全部 16.6 ms 以下、溢れは 0。超えたら最上位のパスを名指す。25 ms を超えたらループを止めてユーザーへ渡す。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-SCALE-300K/bananas --views horizon --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。 verify はビルド・代表の計測 1 本・Debug の検証だけにしてある（ランナーの再検証を軽くするため）。done-when の他の計測は反復の中で行い、PROGRESS に証拠の場所を書く。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 300 -GameArgs banana-count=300000,camera-path-play -OutDir .harness/runs/scene-capture/SCN-MEGA-SCALE-300K/bananas`（ランナーは撮り直さず、保存した結果を検査する）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。

## SCN-MEGA-LAYOUT-SEA: 地平線までのバナナの海を作る
- status: todo
- done-when: 距離で密度を落とす円盤（近 50/m²・中 10/m²・遠 2/m² 程度、半径 300〜400 m、半径と内訳を `MEGA_BANANAS_LAYOUT` に出す）をバッチで置く。配置の計算は O(N) で 3 s 以内（床全体の格子は持たない）。`horizon`・`overview` の撮影を開いて、床の帯・床の端・格子の並びが見えない。カメラの far が半径を超える。1万本では同じ形の小さな円盤。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon,overview -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-LAYOUT-SEA`
- paths: Game/GameModes/MegaBananas, Game/Scenes/SceneCatalog.h, TASKS.md, PROGRESS.md
- notes: （ECS 計画の対応）散布の Object で置き、予算つきで作る。

## SCN-MEGA-LAYOUT-MOUNDS: 落とし込みで積んだ大きな山を数個置く
- status: todo
- done-when: 3〜5 個の塚（合計 5〜15 万本）。全部の山について `MEGA_BANANAS_MOUND_GAP max_gap_mm=` を出す。検算用の山 1 つ（約 2 万本）を `VerifyBananaPile.py --decimate 30` が独立に数え、浮き（> 5 mm）が 0。撮影を開いて浮いて見える個体が無い。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-LAYOUT-MOUNDS`
- verify: `python Scripts/VerifyBananaPile.py .harness/runs/scene-capture/SCN-MEGA-LAYOUT-MOUNDS/horizon.Game.log --decimate 30`
- paths: Game/GameModes/MegaBananas, Scripts/VerifyBananaPile.py, TASKS.md, PROGRESS.md
- notes: 1 本ずつのログは検算用の山だけに絞る。 （ECS 計画の対応）散布の Object で置き、予算つきで作る。

## SCN-MEGA-ACCEPT-1M: 100万本と混在の負荷を、固定した 4 つの負荷と閾値で判定する
- status: todo
- done-when: - 固定した 4 つの負荷: 100万本 horizon の静止、カメラパスの再生、100万本 + mega-stress 5万個の 1% 移動、同じく 64 m 区画のストリーミング。 - 各負荷で 300 フレーム以上、GPU・gt・rt の median と p95 がすべて 16.6 ms 以下（RTX 4080・1920×1080・RelWithDebInfo）。 - 溢れがすべて 0、メモリ ≤ 3 GB、Entity の数が本数によらない。PNG を開いて穴と影の欠けが無いことを見る。 - 合格したら既定の本数を 100万にする。受入れ文書の書き方は前と同じ。25 ms を超えたらループを止めてユーザーへ渡す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/static --views horizon --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/path --views horizon --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/moving --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/streaming --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-ACCEPT-1M-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, Game/GameModes/MegaBananas/MegaBananasMode.h, TASKS.md, PROGRESS.md
- notes: 4 つの撮影は反復の中で 1 回ずつ `-Build -GpuTimingFrames 300` で撮る。混在の負荷を作る引数は UNCERTAIN U12。status は前のとおり、規模の段で todo に上げる。 混在の負荷は SCN-MEGA-MIXED-LOAD の引数で作る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-FAR-SPLAT: 画素より小さいインスタンスを ID のバッファへ 1 画素で書く
- status: backlog
- done-when: 1M horizon で、ソフトのラスタと Cull の合計が 30% 以上減る。遠景の平均輝度の差 ≤ 0.5。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FAR-SPLAT --views horizon --timing`
- paths: Assets/Shaders, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: 条件: ROOT-COOK の後も描くクラスタが 150 万を超えるとき。`ImpostorBake.h` が使えるかを先に読む。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 300 -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FAR-SPLAT`（ランナーは撮り直さず、保存した結果を検査する）。

## SCN-MEGA-WAVE-ATOMICS: カウンタ・一覧・ページ要求の共有アトミックを wave 単位でまとめる
- status: backlog
- done-when: 1M で Cull が 20% 以上減り、全比較が pass。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-WAVE-ATOMICS --views horizon --timing`
- paths: Assets/Shaders, Library/Core/Private/Rendering, TASKS.md, PROGRESS.md
- notes: 条件: Cull1 と Cull2 の合計が 2 ms を超えるとき。subgroup の対応は装置の機能で確かめる。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GpuTimingFrames 300 -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-WAVE-ATOMICS`（ランナーは撮り直さず、保存した結果を検査する）。

## SCN-GEN-BUILDINGS-1: 荒野の建物を3種生成する（崩れた日干し煉瓦の家・見張り塔・石のアーチ）
- status: todo
- done-when: `GeneratedModels.json` に3種（荒野の前哨地に合う、単体で地面に立つ形の英語のプロンプト。50〜70 万面、テクスチャ 4096、実寸の高さ）を足して生成し、参照画像とプレビューを開いて、建物として形が崩れていない（壁が抜けていない・浮いた破片が少ない・底が平ら）ことを確かめる。崩れたものは seed かプロンプトを変えて作り直す。クックの一覧 `Assets/AssetSets/WastelandBuildingsModels.json` に足し、クックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids adobe_ruin,watchtower,stone_arch`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: 2026-10-10 親の調べ: `adobe_ruin` の 3D 化が 2 反復とも 2000 秒を超えて終わらなかった（`build/ComfyUI.log` で形の段の出力が 4440 万面、続く質感の段で止まる。見張り塔は 222 秒）。生成スクリプトに、定義ごとの `upsample_resolution`（既定 1536。`Trellis2UpsampleStage` の `target_resolution`）と、1体あたりの上限時間（既定 15 分。超えたら ComfyUI の `/interrupt` で止め、`upsample_resolution` を 1024 にして 1 回だけやり直す。やり直しも超えたら失敗として次の id へ進む）を足す。1 反復の壁時計（40 分）を超えないよう、ComfyUI の起動を含めて 1 反復の生成は合計 30 分以内に収め、収まらない分は manifest のあるものを飛ばして次の反復で続ける。 2026-10-10 再開（親が推奨を採った）: 3 種の資産は生成・確認・クックが済んでいる（評価者も確認）。残りは評価者の指摘どおり、生成スクリプトの締切を絶対時刻 1 つにして、参照画像・3D・GLB の取得・復旧（`/queue`）・後処理・終了処理のすべてに効かせ、終了処理の時間を先に取っておき、収まらない生成は次へ繰り越すことだけ。資産は作り直さない。

## SCN-GEN-BUILDINGS-2: 荒野の建物を3種生成する（風車・給水塔・木造の小屋）
- status: todo
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、風車（`windmill`）・給水塔（`water_tower`）・木造の小屋（`wooden_shack`）を生成・確認・クックする。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids windmill,water_tower,wooden_shack`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-TERRAIN-GEN: 起伏のある地形の高さ場を作り、MegaGeometry に焼いて描く
- status: todo
- done-when: 共有の高さ場（seed と関数で決まる。Game が同じ関数で任意の (x,z) の高さと法線を引ける）を Library に足し、AssetCook の `--generate` で約2km四方・0.5〜1m 間隔の地形メッシュ（中央に前哨地の平らな盆地 約200m、周りに砂丘とメサ、外周に稜線。UV はワールドの XZ から）を NVMESH v1（クラスタ・LOD の階層）に焼く。`wasteland` シーン（この項目では地形・物理空・太陽・高さフォグだけ。材質は Poly Haven の砂 1 種でよい）に置き、名前付き視点 `overview`（盆地を見下ろし遠くの稜線まで）・`ground`（人の目の高さ）・`ridge`（稜線から盆地）を持たせる。撮影を開いて、地平線が平らな板でなく起伏がある・遠くの地形に LOD の切り替えの穴やクラックが無いことを確かめる。高さ場に CPU のテスト `TerrainHeightfieldTest`（同じ seed で同じ高さ・盆地が平ら・焼いたメッシュの頂点と関数の高さの一致）を既存の束へ足す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure --no-tests=error -R "^TerrainHeightfieldTest$"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-GEN`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/CMakeLists.txt, Tools/AssetCook, Assets/AssetSets, Game, Scripts, Test/Core/Rendering, Test/TestBundle, Test/Game/Bridge/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットのクック）。手本は球の手続き生成（`Library/Core/Public/Rendering/MegaGeometry/ProceduralMegaSphere.h` 34・234 行付近）と `Assets/AssetSets/Rendering3DTestStartupModels.json` の `generate=displaced-sphere`。地形は1つのメッシュでも区画に分けてもよい（クラスタ単位で外される）。カメラの遠クリップは既定 1000 m（`CameraComponent.cpp` 320 行付近）なので、シーンで延ばす。

## SCN-TERRAIN-BLEND: 地形の材質を 3 層（砂・乾いた土・岩肌）の混ぜ合わせにする
- status: todo
- done-when: 地形用の材質（3 層のアルベド・法線・ORM と、傾斜・高さ・低周波ノイズで決める混ぜ方の値）を足し、MegaGeometry の描画経路（使っている経路すべて。ハードのラスタと可視バッファ・ソフトラスタ）で効く。平らな所は砂、少し傾いた所は乾いた土、急な斜面とメサの崖は岩肌になり、境目はノイズで揺らぎ、高さ（凹凸）で重みを付けて砂が岩のくぼみに溜まるように見える。`wasteland` の地形にこの材質を使う。他の材質（Rendering3DTest・バナナ）の見た目は変わらない。`wasteland` の撮影を開いて層の分かれ方を確かめ、Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-BLEND`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-TERRAIN-BLEND`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今の材質は1材質1組（`MaterialTypes.h` 288〜310 行付近）で、シェーダーは各テクスチャ1枚ずつ（`gbuffer.frag` 30〜35 行付近）。テクスチャの焼き込みは既存の usage（albedo|normal|orm）、VT と束縛数の上限に注意。

## SCN-TERRAIN-DETAIL: 地形の岩肌をトライプラナーにし、タイルの繰り返しを見せない
- status: todo
- done-when: 岩肌の層を急斜面でトライプラナー（引き伸ばされない）にし、広い範囲の色・明るさの揺らぎ（数十〜数百 m の低周波）と、距離で UV の倍率を変える混ぜ方（近くは細かく、遠くは大きく）で、`overview` の撮影で同じ模様の格子状の繰り返しが目立たない。メサの崖を見る名前付き視点 `cliff` を足し、縦に伸びた縞が無いことを撮影を開いて確かめる。開発ビルドの GPU 計測で地形の描画の時間の増分を PROGRESS に書く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-DETAIL`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-TERRAIN-DETAIL`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md

## SCN-SHADOW-DISTANCE: シーンごとに太陽の影の到達距離を設定できるようにする
- status: todo
- done-when: GameThread 側（RenderWorld。RenderThread へは FramePacket 越し）に、シーンごとの太陽の影の到達距離を設定する API を足し、VSM（クリップマップの使う段）と CSM（`MaxShadowDistance`）の両方に効く。既定は今の 80 m で、Rendering3DTest の撮影は変わらない。`wasteland` で 500 m 程度にし、`overview` の撮影を開いて、80 m より先の地形の起伏・メサ・岩にも影が落ちている、近くの影の細かさが目に見えて落ちていないことを確かめる。VSM の GPU 時間の増分を PROGRESS に書く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-SHADOW-DISTANCE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-SHADOW-DISTANCE`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画・RenderThread）。`ShadowMapPass.h` 47・134 行付近（`MaxShadowDistance` 既定 80、`SetMaxShadowDistance` はどこからも呼ばれていない）、`CascadedShadowLightMatrices.cpp` 669 行付近、`VirtualShadowMapClipmap.h` 32〜40 行付近（10 段、第1段 4 m、段ごとに 2 倍、DepthRange 1000 m）。

## SCN-WASTELAND-LAYOUT: 荒野の前哨地に岩・崖・建物を置き、夕方の光を決める
- status: todo
- done-when: `wasteland` に Poly Haven の岩・崖（大きさと向きをばらし、地形に少し埋めて、数十〜百数十個。遠くのメサの足元にも）と生成した建物 6 種（盆地の中央に小さな前哨地として道に沿って向きを揃え、地面に埋めて浮かせない）を高さ場の関数で置く。太陽は夕方寄り（仰角 15〜25° 程度）で長い影、露出とグレーディングを夕景に合わせる（Rendering3DTest のグレーディングは変えない）。名前付き視点 `outpost`（前哨地の建物の間）を足す。`overview`・`ground`・`outpost`・`ridge` の撮影を開いて、ゲームの風景に見える（建物が浮いていない・岩が地形に馴染む・白飛びや黒つぶれが広がっていない）ことを確かめる。`wasteland` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WASTELAND-LAYOUT`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland -Cycles 3`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-AERIAL-PERSPECTIVE: 遠景に空気遠近の霞を掛ける
- status: todo
- done-when: 物理空の LUT（透過率と散乱）から、カメラからの距離に応じた散乱光と透過率を不透明物に掛ける処理（GBuffer の合成・可視バッファの両方の経路）を足してシーンごとに有効にでき、`wasteland` で有効にする。遠くの稜線とメサが空の色に溶け、近くは変わらない。`wasteland` の高さフォグの値も霞として馴染むように調整する（Rendering3DTest の霧は変えない）。`overview`・`ridge` の撮影を開いて遠景の霞を確かめ、Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-AERIAL-PERSPECTIVE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-AERIAL-PERSPECTIVE`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。`SkyAtmosphere.h` 24〜36 行付近、`RenderWorld::SetSkyAtmosphere`（`RenderWorld.cpp` 311 行付近）。高さフォグは `VolumetricFog.h` 11〜17 行付近（Rendering3DTest は密度 0.02・falloff 1.0）。

## SCN-CLOUDS: 空に雲の層を足す
- status: todo
- done-when: 空の描画に、高度帯（例 1.5〜4 km）の雲の層を 3D ノイズ（Perlin-Worley 等。起動時に生成）の密度で光線を進めて描く処理を足してシーンごとに有効にでき、`wasteland` で有効にする。太陽側の縁が明るく（前方散乱）、底面が影で暗く、空の環境光で持ち上がり、時間でゆっくり流れる。雲の影は範囲外（太陽を遮らない）。雲を空からの IBL に入れるかを決めて記録する。`overview`・`ridge` の撮影を開いて、地平線近くまで雲が自然に並ぶ（板やタイルの繰り返しに見えない）ことを確かめる。開発ビルドの GPU 計測で雲の時間を PROGRESS に書く。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-CLOUDS`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-CLOUDS`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。空から IBL を作る箇所は `LightingPass.cpp` 4653 行付近。雲は今は無い。

## SCN-CLOUDS-TEMPORAL: 雲を低解像度と時間方向の蓄積で軽くする
- status: todo
- done-when: 雲を画面の 1/4〜1/2 の解像度で描き、前のフレームへの再投影で蓄積して、1920×1080・RelWithDebInfo の `wasteland` `overview` で雲の GPU 時間が 1.5 ms 以下（超えるなら計測値と理由を記録）。カメラパスの再生中の連続撮影（`--capture-sequence`）の数枚を開いて、カメラを動かしても雲の縁がちらつかず残像を引かないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-CLOUDS-TEMPORAL`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md

## SCN-GRASS-RENDER: 枯れ草の株をインスタンスで描き、風で揺らす
- status: todo
- done-when: 曲がった細い葉を数〜十数枚束ねた枯れ草の株のメッシュ（手続き生成。アルファ抜きを使わない）を、インスタンスの配列（位置・向き・大きさ・色の揺らぎ）から少ない回数の描画（間接描画かインスタンス描画）で描く経路を足し、頂点シェーダーで風（時間と低周波ノイズ。根元は動かず先ほど揺れる）で揺らす。色は乾いた黄土〜灰褐色で、裏面も描く（両面）。影は受ける。`wasteland` の一角に数千株を置いた名前付き視点 `grass` を足し、撮影を開いて草に見える（板やトゲに見えない）こと、時刻を変えた2枚で揺れていることを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Views grass -OutDir .harness/runs/scene-capture/SCN-GRASS-RENDER`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-GRASS-RENDER`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/CMakeLists.txt, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今のシェーダーに discard が無く、`bTwoSided`（`RenderMaterialStore.cpp` 50 行付近）がカリングに効いているかは未確認。インスタンスを束ねる既存の仕組みは `DrawCommand::AddInstance`（`DrawCommand.h` 421 行付近）。

## SCN-GRASS-SCATTER: 地形に枯れ草を撒き、区画ごとに間引く
- status: todo
- done-when: 高さ場の傾斜・高さ・ノイズで密度を決めて `wasteland` の盆地と砂丘の麓に枯れ草を撒き（建物の足元・道・急斜面には生えない）、区画ごとに視錐台で外し、距離で密度を下げて 80〜120 m 先で消す（消え方が線にならないよう株ごとのしきい値でフェード）。`ground`・`outpost`・`overview` の撮影を開いて、近くは草むら、遠くは自然に薄くなることを確かめる。1920×1080・RelWithDebInfo の `ground` で草の GPU 時間を測って PROGRESS に書く（目安 2 ms 以下）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-GRASS-SCATTER`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md

## SCN-DUST: ワールド空間の砂塵を足す
- status: todo
- done-when: 粒子の仕組みにワールド空間のビルボード（カメラを向く板、深度で柔らかく消える、太陽と環境光で明るさが決まる、風で流れる、寿命で薄れる）を足し、`wasteland` に地面すれすれを流れる砂塵と、遠くで舞い上がる砂の柱（数本、ゆっくり）を置く。画面空間の既存の粒子（Rendering3DTest の橙の粒）は変わらない。`ground`・`overview` の撮影と時刻を変えた2枚を開いて、砂塵が地形に溶け込み（板の縁の切れ目が見えない）流れていることを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-DUST`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-DUST`
- paths: Library/Core/Public/Particle, Library/Core/Private/Particle, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今の粒子は画面空間のボードだけ（`ParticleSystem.cpp` 166〜191 行付近）。半透明の描画順と TAA との兼ね合いに注意。

## SCN-WASTELAND-POLISH: 荒野をゲームの風景として仕上げ、カメラパスと性能を決める
- status: todo
- done-when: `wasteland` のカメラパス（前哨地へ近づいて建物の間を抜け、稜線へ上がる 40〜60 秒）を決め、グレーディング・露出・霧・雲・草・砂塵の量を整える。全視点とカメラパスの途中 6 時刻の撮影を開いて、デバッグ画面に見えない（格子・単色の板・浮いた物・タイルの繰り返し・LOD の穴が目立たない）ことを確かめ、気になったものを直す。1920×1080・RelWithDebInfo で `overview`・`ground`・`outpost` のフレームの GPU 時間とパスごとの内訳を測って受入れ文書の荒野の節に書く（目安 16.6 ms。超えるなら重いパスを下げる設定を探し、残りは既知の限界として書く）。`wasteland` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WASTELAND-POLISH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland -Cycles 3`
- paths: Game, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md

## SCN-GEN-VILLAGE-PROPS-1: 夜の集落の小物を生成する（街灯・松明の柱・焚き火の囲い・井戸）
- status: todo
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`street_lamp`・`torch_post`・`campfire_ring`・`well` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/NightVillageModels.json` に足してクックが通る。 灯りの部分（ランタンのガラス・火）は発光させたい部分としてプレビューで位置を確かめ、計画書に書く。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids street_lamp,torch_post,campfire_ring,well`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-GEN-VILLAGE-PROPS-2: 夜の集落の小物を生成する（樽・木箱・荷車）
- status: todo
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`barrel`・`crate`・`cart` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/NightVillageModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids barrel,crate,cart`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-NIGHT-VILLAGE: 夜の集落を組む
- status: todo
- done-when: `night-village` シーン: 荒野の高さ場の一角に、生成した建物と小物で道沿いの集落（建物 8〜12 棟。同じ建物は向きと大きさを変えて使い回す）を組む。太陽は地平線の下、月明かり（青白い弱い平行光と影）、夜の空、露出は自動露出で夜に合わせる（Rendering3DTest の `--night` の値の扱いを参考にする）。名前付き視点 `overview`・`street`（道の上の目の高さ）・`square`（焚き火の広場）。撮影を開いて、暗すぎず明るすぎず夜に見えることを確かめる。`night-village` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene night-village -RequireAssets -OutDir .harness/runs/scene-capture/SCN-NIGHT-VILLAGE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes night-village -Cycles 3`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-NIGHT-LIGHTS: 夜の集落に灯りを入れる
- status: todo
- done-when: 街灯・松明・焚き火の点光源（暖色。影付きは VSM の点光源の上限の 4 灯までを手前の主役に、残りは影無し）、灯りのガラスと窓の発光（ブルーム）、焚き火の明るさの揺らぎ（時間で揺れる）、RTGI による壁や地面への照り返しを入れる。`street`・`square` の撮影を開いて、灯りの周りが照らされ影が落ちる・窓が光る・白飛びが広がりすぎないことを確かめる。1920×1080・RelWithDebInfo の `street` の GPU 時間を受入れ文書に書く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene night-village -RequireAssets -OutDir .harness/runs/scene-capture/SCN-NIGHT-LIGHTS`
- paths: Game, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 点光源の影は 4 灯まで（`PointShadowSnapshot.h` 19 行付近）。

## SCN-MATERIAL-GALLERY: 材質ギャラリーを作る
- status: todo
- done-when: `material-gallery` シーン: 石の床（Poly Haven）の展示場（屋外か、壁の無い屋根付きの回廊）に台座の列を並べ、手元の Poly Haven の地面・壁のテクスチャ（約30種と SCN-PH-WASTELAND の分）を台座の上の傾けた板（視差オクルージョン付き）と球で見せ、別の一角に金属度 0〜1 × 粗さ 0〜1 の格子（6×6 程度）の球を並べる。各台座に名札（テクスチャの id）を付ける（ワールド空間の文字か、画面に投影した ImGui の文字）。光は物理空の昼。名前付き視点 `overview`・`textures`・`spheres`・`close`（板の視差）。撮影を開いて、テクスチャが正しく貼られ（色・法線の向き・ORM の並び）、名札が読めることを確かめる。`material-gallery` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene material-gallery -RequireAssets -OutDir .harness/runs/scene-capture/SCN-MATERIAL-GALLERY`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes material-gallery -Cycles 3`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-GEN-FURNITURE-1: 室内の家具を生成する（机・椅子・本棚・ソファ）
- status: todo
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`wooden_table`・`wooden_chair`・`bookshelf`・`sofa` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/InteriorLightModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids wooden_table,wooden_chair,bookshelf,sofa`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-GEN-FURNITURE-2: 室内の家具を生成する（植木鉢・ランプ・ラグ）
- status: todo
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`potted_plant`・`floor_lamp`・`rug` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/InteriorLightModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids potted_plant,floor_lamp,rug`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-INTERIOR-ROOM: 室内の光のシーンを組む
- status: todo
- done-when: `interior-light` シーン: 約 12×8×5 m の部屋（床は木の板、壁は漆喰かレンガ、天井の梁。手続きの箱と Poly Haven のテクスチャ）の一面に大きな窓（開口）を開け、低い夕日が差し込む。生成した家具を置き、赤いラグか色の強い壁で色移り（RTGI）が分かる配置にする。床は少し艶があり反射（SSR）が見える。窓の外に地面と空が見える。名前付き視点 `overview`・`window`（窓へ向かう逆光）・`corner`（奥の隅）。撮影を開いて、窓から入る光と影の形、奥の隅の照り返し、白飛び・黒つぶれを確かめる。`interior-light` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -RequireAssets -OutDir .harness/runs/scene-capture/SCN-INTERIOR-ROOM`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes interior-light -Cycles 3`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-INTERIOR-SHAFTS: 窓から入る光の筋を出す
- status: todo
- done-when: ボリュメトリックフォグの散乱が太陽の影（既定の VSM）で遮られ、窓から入る光の筋が見える。シーンごとにフォグの密度・範囲を設定でき、`interior-light` で室内に薄い霧を入れる（Rendering3DTest の霧は変えない）。`overview`・`window` の撮影を開いて光の筋を確かめ、Rendering3DTest の撮影が変わらない。GPU 時間の増分を PROGRESS に書く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -RequireAssets -OutDir .harness/runs/scene-capture/SCN-INTERIOR-SHAFTS`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-INTERIOR-SHAFTS`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。`Assets/Shaders/volumetrics.frag` 103〜135 行付近は CSM の影の地図（`cascadedShadowMap`）を読む。太陽の影が VSM のときに霧の影が効いているかを先に確かめる。

## SCN-THUMBNAILS: ランチャーのカードに各シーンの見本画像を出す
- status: todo
- done-when: `Scripts/CaptureLauncherThumbnails.ps1` が各シーンの見本用の視点（シーンの一覧に書く。既定は `overview`）を撮って 640×360 程度に縮め、`Assets/Textures/Launcher/<id>.png` に書く（追跡する）。ランチャーが読み込んでカードに出す。ランチャーの撮影を開いて、見本画像が正しいカードに出て、選択の強調が分かることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene launcher -OutDir .harness/runs/scene-capture/SCN-THUMBNAILS`
- paths: Game, Scripts, Assets/Textures/Launcher, TASKS.md, PROGRESS.md
- notes: 2026-10-10 親が見た点: 1920×1080 のランチャーでカードの説明文が右の縁からはみ出す（折り返しの幅がカードの内側の余白を引いていない）。カードが増えたときの並び（1 行に収まらないときの折り返しかスクロール）も確かめる。

## SCN-ACCEPT: ランチャーと全シーンの受入れを記録する
- status: todo
- done-when: 計画書の受入れの節の全項目を確かめて `Docs/RenderingValidation/SceneLauncherAcceptance.md` に書く: Debug・RelWithDebInfo・Release で引数なしの起動がランチャー（撮影を開いて確認）、全シーンの cycle smoke（各3周）が pass、全シーンの全視点の撮影を開いた所見、1920×1080・RelWithDebInfo の各シーンの GPU 時間、Rendering3DTest の撮影が main と見た目で変わらないこと、生成と Poly Haven の資産の一覧（id・出典・ライセンス・作り方）。見つけた不具合は直すか、`SCN-FIX-<名前>` の項目として TASKS.md に足す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,wasteland,night-village,material-gallery,interior-light -Cycles 3`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene launcher -Configuration Release -OutDir .harness/runs/scene-capture/SCN-ACCEPT-release`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-ACCEPT`
- paths: Docs/RenderingValidation, Game, Library, Assets, Scripts, Test, TASKS.md, PROGRESS.md

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
- notes: 2026-10-08 親（段9の開始時に詳しくした）。ランナーが止まった後に親が行う（backlog）。段の区切りの評価（Sol）にかける。 2026-10-08 親: 受入れを記録した（`Docs/RenderingValidation/VirtualizationAcceptance.md` の「段9（VSM 点光源と全体）」と「段1〜9 の受入れのまとめ」）。検査の出力は `.harness/runs/vtg9-accept/`。2026-10-09 親: 夜の橙赤の粒の修正（VTG9-FIX-NIGHT-ENV-SUN・VTG9-FIX-NIGHT-RTGI-FIREFLIES）と照明のパスの修正（VTG9-FIX-LIGHTING-PER-EXECUTE）、main（`1b5e925c`）の取り込みの後の最終のコードで撮り直し（`.harness/runs/vtg9-accept-r3/`、`VTG9-ACCEPT-r3*`）、記録を最終の値に直した。

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
- status: backlog
- done-when: JsonDocumentのsurrogate pairを単一Unicode scalarへ合成し、生UTF-8/escape表現が同じ名前になることを検証する。
- verify: escaped emoji/生UTF-8/孤立surrogate/文字列往復の実コード試験と独立レビュー。
- notes: ParseUnicodeEscapeは現状4桁単位、AppendUtf8は3byteまで。P5Aとは別件。既定game action名はASCIIで進め、汎用JSON整合として後続修正する。

## GAME-GR130-VFX: 剣のトレイル（リボン）を実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: CPU の試験で、固定ステップの回数が 0・1・2 のどのフレームでも帯の点列が連続で NaN が無いこと、1フレームで90度以上振っても補間で折れ目の角度が上限以下になること、寿命で点が消えて上限を超えないこと。GPU の試験で、既知の軌跡の帯の画素の位置が期待と一致すること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR13（ボーンソケット）、GR57（描画の経路と半透明のキューを共有）、GR136、GR02（剣は描画用の補間された Transform を読む。攻撃判定 GR16 はシミュレーションの Transform を読む。段階をまたぐ注意の G4 の項目どおり）、GR30（TAA の整合）。
- stage-proposal: G8（戦闘と進行）。垂直スライスの戦闘で使う。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR131-VFX: 当たりの火花・衝撃（ヒットエフェクト）を実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 当たり1回で放出の要求がちょうど1回積まれ、位置と法線が当たりの値と一致すること。表面の種類ごとに表のエフェクトが選ばれること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR16、GR57、GR136、GR115（軟い。無いうちは既定の1種類）、GR02。
- stage-proposal: G8。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR132-VFX: シ者を倒したときの赤い血を実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 撃破の合図から、血・滲み・消滅が時間割どおりの時刻で始まること（固定刻みのクロックで2回撮って一致）。滲みのマスクが時間に対して単調に広がること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR60、GR65、GR26、GR133、GR136。
- stage-proposal: G10（シ者の表現と戦闘の拡張）。垂直スライス（G8）では、最小の形（血しぶきのパーティクルだけ）を入れるかを作者が決める。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR133-VFX: デカール（地面や体に残る跡）を実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: GPU の試験で、既知の箱のデカールが範囲内の GBuffer の色と法線だけを変え、範囲外の画素が変わらないこと。上限を超えると古いものから消えること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25（描画の拡張点）、GR26（材質の拡張）、GR37（地形）。草（GR43）の上の扱いは、草には描かないのを既定にする。
- stage-proposal: G10。足跡を垂直スライスで使うなら G7 に前倒し。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR134-VFX: メッシュのエフェクトを実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 粒子の数だけインスタンスが描かれ、時間の値で溶けの閾値が変わること（GPU の試験の画素で確かめる）。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57、GR136、GR26、GR27（アルファテストと両面描画）、GR60。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR135-VFX: 空気の歪み（屈折）を実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: 歪みのバッファが空のとき、出力が歪みのパスの有無で画素単位で一致すること。既知のずらしの値で、画素が期待の量だけ動くこと。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR25、GR30、GR57、GR134、GR136。
- stage-proposal: G10。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR136-VFX: Niagara 相当の VFX システムを実装する
- status: backlog
- done-when: 着手時に完了条件案を確定し、実装と検証を完了する。原文の案: スキーマに、エミッタとモジュールの型、値の範囲・単位・既定値が出ること。範囲外の値や型の合わない値の設定が拒否されること。クックしたバイナリを読んだ結果が、元の形式から読んだ結果と一致すること（同じ種と刻みで、粒子の位置の列が一致）。動いているゲームでの値の変更が、次のフレームから反映すること。イベントで起動したエミッタが、起動の位置と時刻どおりに生成すること。フリップブックの取り込み設定どおりの UV の矩形と再生の速さ。雨（GR58）を含む既存の要件のエフェクトを少なくとも1つ、このシステムで組めること。
- verify: 要件原文のCPU/GPU/Bridge試験を着手計画へ分解し、実行済みと未実行を分離して記録。実機/SDKが必要な確認は代替stubで合格扱いしない。
- stop-when: 作者判断が未決の方式を確定扱いする、前提GR未完のまま完了宣言、範囲外repoの無承認変更、blocking未解消。
- paths: Docs/Plans/GameFeatureRoadmap.md, Docs/Plans/GameFeatureRequirements.md, Docs/Plans/NORVESLIB_ROADMAP_ADDITION_VFX_2026-10-02.md（非追跡の計画参照。実装pathsは着手時に確定）
- dependencies: GR57（この要件は GR57 と範囲が重なる。GR57 をこの形で設計するか、GR57 の上の層として作るかは着手時に決める）、GR78、GR96、GR02、GR126（軟い）、GR72（軟い。音は後で足せる形）。
- stage-proposal: G7（GR57 と一緒）。雨（GR58）やしぶき（GR59）もこのシステムのエフェクトとして作るので、早めに入れる。
- notes: 2026-10-02追加。未着手の後続作業で、前提と選定課題を解決してから実装タスクへ細分化する。NorvesLib/Editorの境界とVFX-S1〜S5はロードマップ参照。

## GAME-GR137-VFX: 資産の編集・決定的な撮影・言語モデルの口（Bridge の拡張。汎用）を実装する
- status: backlog
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
- status: backlog
- done-when: material/asset/asset-setの設定を解決し、発光換算の未設定拒否に資産名・材質名・emissiveNitsPerUnitを表示。AI生成profileを明示的に適用し素材単位overrideを保持する。canonical/hash/cacheとJSON/CLIへ接続する。
- verify: asset-set単位指定/素材上書き/欠落・不正/発光textureのみ/診断名/設定差cache失効と旧非発光・v0互換を確認する。
- stop-when: provenanceを拡張子だけで決める、sidecarよりasset-set設定を無言優先、非発光を不必要に拒否、見た目未確認を受入れ済みとする。

## G2-S6-ASSET-SET: C++一括cookと増分判定を接続する
- status: backlog
- done-when: AssetCook --asset-setへ一括cookと増分判定を集約。origin/main CookTextureAssetSet.ps1 + Rendering3DTestSilverTextures/Rendering3DTestSilverGltfTexturesに対してcooked/manifestのbyte一致を確認。glTF外部ファイルとsidecarを印に含む。
- verify: 単体CLIの分割前後比較、旧texture spec v1の2spec同値、外部buffer/画像/sidecarの変更・不在・復帰・破損で正しい再cook/拒否、失敗時出力保持。
- stop-when: 手元確認用CookAssets.ps1/StartupMaterialsを対象に戻す、PS側へ増分判定を重複実装、Windows実byte比較を未実施で完了とする。

## G2-MATERIAL-SELECTION-INTEGRATION: 共通照合を設定とslot名へ接続する
- status: backlog
- done-when: GR79 ARM/発光、GR78 材質→SurfaceName、GR32 slot名が同じResolveMaterialSelectionを使う。元catalog/生成slotを明示し、全設定の未一致/二重指定をcookと増分preflight双方で拒否。同名GLBは元での改名推奨を資産名付きで警告する。
- verify: raw無名1/Blender Material_0、逆primitive順/同名/生成名衝突、name+index二重指定、不在、unicode名、incremental skipの検証迂回なし、設定値/SurfaceName/slotへの実到達。
- stop-when: 未実装SurfaceNameを受理して捨てる、共通核の存在だけで全接続完了とする、元indexとslotindexの混同、旧wire予約領域へ勝手に保存。

## CORE-STRING-REPLACE-TERMINATOR: 部分置換によるsuffixのNUL破損を修正する
- status: backlog
- done-when: TString::replaceが同長/増加/縮小/末尾/自己参照の置換で意図したbyte列を保ち、終端は末尾だけに置く。
- verify: 実Coreのchar/wchar/member契約、部分置換直後のsuffix先頭と全size、関連文字列試験。
- stop-when: StringCopyの全呼出し規約を検証なしに変更、Windows CRTの動作を偽shimで合格扱い。

## G2-GR82-B4-STATIC-ROOT-FRAME128: 静的なArmature親と作者frameを安全に束縛する
- status: backlog
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

## SCN-MEGA-COMMAND-CAPACITY: MegaGeometry のコマンドバッファの容量が 2^26 件を超えると何も描かれなくなる原因を調べる
- status: backlog
- done-when: `mega-stress` を `--stress-entities=3300` 以上、環境変数 `NORVES_MEGA_MAX_DRAW_COUNT` なしで起動したとき、`MEGA_CAPACITY command_capacity=67108864`（`indirect_bytes` 1.34 GB）で描画が壊れる原因を特定する。原因が装置の上限なら、容量を超える手前で警告して分割する。コード中の判定（`MegaGeometryPass.cpp` の `0x7FFFFFFF / sizeof(DrawIndexedIndirectCommand)`）が実際の上限と合っているかを確かめる。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1280 -Height 720 -Views overview -GameArgs stress-entities=5000 -OutDir .harness/runs/scene-capture/SCN-MEGA-COMMAND-CAPACITY`
- notes: 2026-10-10 SCN-MEGA-STRESS-RESIDENT の撮影で見つかった。14 種類のメッシュ（1 種類が約 1〜19 千クラスタ）を 2500 個置くと `command_capacity=33554432` で描かれ、3300 個以上（5000 個で同じ）では `command_capacity=67108864` に跳ねて、空と床だけが写る（`draw_dropped=0`、検証レイヤーのエラーなし、`MEGA_OCCLUSION` は 44 万クラスタを描いたと報告する）。`NORVES_MEGA_MAX_DRAW_COUNT=1000000` で区間ごとの上限を下げると 5000 個で描かれる（`command_capacity=33554432`）。mega-stress は `--stress-entities` を指定したとき（開発ビルド）、この環境変数が無ければ 1000000 を渡して回避している（`GameApplicationHandler.cpp`）。この回避は区間ごとの容量の上限を縮めるだけで、原因ではない。

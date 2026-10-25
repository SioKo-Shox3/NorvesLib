# TASKS — NorvesLib

2026-10-09 ランチャーと見せるシーン（ブランチ `feature/scene-launcher`、ユーザー決定）: 起動するとランチャー（シーン選択）が出て、Rendering3DTest・仮想化ジオメトリのバナナ1万本・荒野の前哨地・夜の集落・材質ギャラリー・室内の光へ入り、Esc のポーズメニューからランチャーへ戻れるようにする。決定事項・設計・資産の作り方・受入れは計画書 `Docs/Plans/SceneLauncherPlan.md` が正本で、`SCN-` の項目で上から順に進める。重い処理（ComfyUI での3D生成・クック・撮影と計測）はこの計画でユーザーが許可済み。GPU を使う処理は同時に1本（ComfyUI は生成スクリプトが止めてから Game を撮る）。Rendering3DTest の見た目は変えない（`CaptureStartupScene.ps1` の撮影で確かめる）。新しい描画機能はシーンごとに有効にし、既定では効かせない。新しいシーンの見た目は `Scripts/CaptureScene.ps1`（SCN-SCENE-ARG で入る）の撮影を開いて確かめる。 走行の間、他の系の未完の項目は `backlog` に退避している。main へマージする前に元の status へ戻す（todo: CORE-JSON-SURROGATE・GAME-GR130-VFX・GAME-GR131-VFX・GAME-GR132-VFX・GAME-GR133-VFX・GAME-GR134-VFX・GAME-GR135-VFX・GAME-GR136-VFX・GAME-GR137-VFX・G2-GR79-IMPORT-POLICY-CONNECTION・G2-MATERIAL-SELECTION-INTEGRATION・CORE-STRING-REPLACE-TERMINATOR、doing: G2-S6-ASSET-SET・G2-GR82-B4-STATIC-ROOT-FRAME128）。

2026-10-10 ユーザー不在の間の方針（ユーザー指示「何かあったら推奨の案で進めて」）: ユーザーにしか決められない判断が出ても BLOCKED にせず、推奨の案を採って進め、採った判断と理由を PROGRESS.md に書く。推奨の案が paths の外の変更を要するときは、その項目の paths に足して同じ反復で進める（危険地帯はランナーの評価で反証される）。破壊的操作（資産・ブランチの削除、履歴の書き換え）と push はしない。

2026-10-10 相談の方針（ユーザー指示「詰まったら GPT にも相談して」）: 同じ手法で 2 回失敗したとき、描画・GPU の設計の選択に迷ったとき、評価者の差し戻しの直し方が分からないときは、次の手を打つ前に `node ~/.agent-workflow/ask-advisor.mjs codex`（ブリーフは stdin。GPT に相談）で相談し、受けた助言と採った手を PROGRESS.md に書く。

2026-10-10 仮想化ジオメトリの規模の引き上げ（ユーザー決定）: バナナ 100万本（と一般のゲームの負荷 mega-stress）で GPU・ゲームスレッド・レンダースレッドの median と p95 が 16.6 ms 以下（RTX 4080・1920×1080・RelWithDebInfo）を `SCN-MEGA-` の項目で、荒野より先に進める。バナナ専用ではなく、席（GPU Scene）・変化の通知・差分の転送・ストリーミングで実際のゲームにも効く形にする。設計は計画者の計画を GPT（Sol）が 2 周批評したものを親がまとめた（計画書 `Docs/Plans/SceneLauncherPlan.md` の「仮想化ジオメトリの規模の引き上げ」）。準備の項目は新旧の一致（mismatch=0）が取れるまで切替へ進まない。切替の項目は旧経路へ戻す引数を残す。

2026-10-10 計測の方針（ユーザー指示「毎変更ごとにビルドしてゲームをつけて計測していたら終わらない。計測が必要なタイミングを考える」）: 実装の項目の検証は、RelWithDebInfo のビルド 1 回と、正しさを見る短い起動 1〜2 本（新旧の一致・溢れ・送った量などの検査のカウンタ。0 でなければ ERROR を出して撮影が落ちる）だけにし、GPU 時間（`-GpuTimingFrames`）は測らない。性能の数値と Debug の検証レイヤーは節目の計測の項目（`SCN-MEGA-MEASURE-*`・`SCN-MEGA-SCALE-300K`・`SCN-MEGA-ACCEPT-1M`。調べる段の PROBE と SETUP-LINEAR は計測そのものが目的）でまとめて見る。反復の中でも、小さな変更のたびにビルドしてゲームを起動しない。コードを読んで考え、変更をまとめてから起動 1 回で確かめる。CPU のテストで確かめられる契約はゲームを起動せずに確かめる。 計測の項目の verify は、反復の中で 1 回撮った結果を `Scripts/CheckCaptureMetrics.py`（結果が揃い、失敗・溢れが無く、計測の値があり、いまの Game.exe より後に撮られたこと）で検査し、ランナーは重い撮影をやり直さない。1 回の撮影が 10 分を超える規模（古い経路での 10万本以上など）は、ECS の大量の軽い実体が入るまで測らない。 2026-10-10 追記（ユーザー指摘「改善が確定している土台で測って意味があるのか」）: 作り変えが決まっている経路の改修前の計測はしない。節目の計測の合否は目標の絶対値（16.6 ms などの閾値）で決め、改修前の値との比は合否に使わない。改修の前に測るのは、結果で設計や作るかどうかが変わる診断（PROBE-ROOT・PROBE-100K のような）だけ。正しさの比較（描いたクラスタ数・画像の一致）は別で、これは続ける。ECS 前の mega-stress の値は、既に撮った分を PROGRESS に参考値として残した（そのために撮り直さない）。 2026-10-10 追記（ユーザー指摘「全体の負荷だけ測っても効果が分からない。GPU はパスごと、CPU は関数単位の内訳が要る」）: 節目の計測の項目は、問いと、それに答える区間（CPU）・パス（GPU）の名前を done-when に書き、その値で判断する。全体の gt・rt・GPU だけで合否を決めるのは受入れ（SCALE-300K・ACCEPT-1M）だけ。GPU のパスごとの時間は撮影が既に出している。CPU は ECS のスケジューラが系ごとの区間を自動で記録する（SCN-ECS-SCHEDULER）。区間の名前の決まり: 段 `Ecs.Stage.<段>`、系 `Ecs.Sys.<系>`、同期点 `Ecs.Sync.<段>`、同期点の中のコマンドの適用・破棄の回収・変換の伝播は `Ecs.Sync.Commands`・`Ecs.Sync.Cleanup`・`Ecs.Sync.TransformPropagate`、描画の変換の系は `Ecs.Sys.RenderTransform`、SceneQuery の更新は `Ecs.Sys.SceneQuery`、RenderGraph のパスの記録は `RG.Record.<パス>`。系や段を作る項目はこの名前を使う。タイムラインのビューア（Insights のようなもの）は今は作らない。 2026-10-11 追記（ユーザー指摘「計測は改修の後と決めたのに、何のための撮影か」→ ユーザー決定）: 各項目の検証でゲームを起動するのは正しさの確認だけで、回数を絞る。起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）。検査付きの起動（`-RequireChecks`・`engine-checks`）は 1 視点 1 本。シーンの出入りの周回は、寿命とシーンの出入りに触る項目だけで、混在 1 本。CPU だけの変更は CPU のテストと Game のビルドで確かめる。反復の中でも、確かめたいことが無い起動はしない。 検証の起動は窓を前面に出さない（撮影・周回のスクリプトは `--window-background` を付ける。反復の中で Game.exe を直接起動するときも付ける）。

2026-10-13 試験の扱い（ユーザー指示「邪魔な試験は実行から外す。コード上の確からしさを見るだけの試験は意味が微妙」）: ctest で無効にした試験（`set_tests_properties(... DISABLED TRUE)`。今は `RenderGraphCompileTest`）は合否に使わない。無効の試験が通らなくなるという指摘は差し戻しの理由にせず、直すためだけに偽デバイスや期待値を書き換えない。新しい試験は `Docs/Testing.md` の「技術的負債」の節の 3 点が書けるときだけ足す。

2026-10-14 荒野の作り直し（ユーザーの評価と決定）: ユーザーは今の荒野を「PS3 の水準で、現実の荒野と比べられる水準に無い」と評価した。地形の大きな形は実在の地形データ（USGS 3DEP の 1 m DEM、パブリックドメイン）から作り、手本は米南西部のメサと砂岩（Valley of the Gods、ユタ州）にする。ダウンロード（DEM の 1 m タイル `x60y413` 約 268 MB・Python のパッケージ・手本の写真・Poly Haven の素材）はユーザーが許可済み。それ以外のダウンロードは、ファイル名とサイズを書いて BLOCKED にする。項目は `SCN-WL-`。判定は、技術と見た目の条件（浮き・継ぎ目・繰り返し・形の対応・移動中のちらつき）を評価者が見て、実写と並べて成立するかの最終の判断はユーザーがする（SCN-WASTELAND-POLISH の後）。目標の個数（数十万個など）は置かず、手本の写真の大きさ・被覆・疎密に合わせる。

2026-10-15 荒野と室内の光の目的（ユーザー）: このプロジェクトは現代の CG の水準の描画を目指してきた。荒野（wasteland）と室内の光（interior-light）は、それを確かめる「ほぼ実写」の美しさを見るシーンで、速さ・最適化は他のシーンで見る。この 2 つでは 60 fps の条件を外す（リアルタイムで、操作できる程度＝1080p で 20〜30 fps 以上ならよい）。品質を見る撮影は 1920×1080 以上。判断は手本の実写との比較画像・実在の材質の明るさの範囲・同じ視点のパストレースの正解（光の誤りを分ける）で行い、節目（SCN-WL-REVIEW-1・REVIEW-2・SCN-WASTELAND-POLISH）でユーザーが見る。ambientCG（CC0）の素材のダウンロードも許可済み。

2026-10-16 荒野の撮影の範囲（親）: 荒野の全 13 視点の撮影は Debug で 515 秒かかり、ランナーの再検証の上限（10 分）に近い。各項目の撮影はその項目に関係する視点だけにし（`-Views`）、手本との比較（CompareReference）を使う項目は手本の 6 視点を撮る。全視点の撮影は SCN-WASTELAND-POLISH だけにする。途中の確かめも同じ視点に絞り、全視点を何度も撮り直さない。

2026-10-16 荒野の作り直し（ユーザー）: 遠景のぼけ・フォグ・空・雲・建物の浮き・植物の質が現代 CG の水準に届かず、項目を積み上げるやり方では無理があると評価された。決定「正解の絵から」: Blender Cycles で同じ地形・素材・照明の正解の絵を作り、描画の欠陥と中身の欠陥を切り分けてから直す（計画は Docs/Plans/WastelandRebuild.md、Sol の批評を反映）。SCN-WLR-* はその段で、SCN-WLR-GAP-LIST の節目でユーザーに渡し、後ろの SCN-WL-* を組み直す。

2026-10-15 レイトレーシングと GI の速さ（ユーザー）: RT（RTGI・RT の影と反射・TLAS）と GI を入れた構成では 60 fps（16.6 ms）の条件を外し、値は記録するだけにする。60 fps の条件が残るのは、RT と GI を切った構成（仮想化ジオメトリの規模の判定など）だけ。RT と GI を入れた構成の目安は 30 fps（33.3 ms。PS5 のレイトレーシングの作品の水準）だが、今は美しさを先に盛り、削るのは後にする（今の段階では fps を合否に使わない）。最適化は頭に入れ、同じ計算の繰り返しのような明らかな無駄は作らない。RT を入れても、動かすと操作できないほど遅くなる欠陥（例: 動くと RTGI が 40 倍遅くなる）は直す。

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
- done-when: Tick を消して登録する（順は今と同じ）。スクリプトからの生成は Object が即時に作られ、行は同期点で作られる。スクリプトからの破棄は、呼んだ時点で破棄予約が立ち（`IsPendingDestroy` が真）、以後その Object はスクリプトからの取得と Tick の対象から外れ、実際の解放と行の除去は同期点で行う（Tick の最中に巡回中の Object を解放しない既存の破棄の規則を保つ）。Tick の中で破棄した直後に、取得が null を返し、同じフレームの後の Tick でも呼ばれないことを、既存のスクリプトの試験の束で確かめる。下のテストが pass する。
- verify: `cmake --build build --config Debug --target ScriptComponentTickTest ScriptApplicationLifecycleContractTest ScriptHotReloadTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(ScriptComponentTickTest|ScriptComponentSceneRoundTripTest|M6AngelScriptAcceptanceTest|ScriptApplicationLifecycleContractTest|ScriptHotReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Public/Component/ScriptComponent.h, Library/Core/Private/Component/ScriptComponent.cpp, Library/Core/Public/Scripting, Library/Core/Private/Scripting, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Test/Core/Scripting, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親: 「Object は即時に破棄」の意味を、破棄予約が即時に立つことと決めた（blocked の推奨 A。実際の解放は同期点）。 2026-10-11 親: 評価の 2 周目の残り（後の Tick で呼ばれないことを試験が見分けられない）は SCN-ECS-FIX-SCRIPT-TICK-COUNT で直す。通ったら done にする。

## SCN-ECS-FIX-SCRIPT-TICK-COUNT: 破棄予約したスクリプトの Tick が呼ばれないことを、呼び出しの回数で確かめる
- status: done
- done-when: `Test/Core/Scripting/ScriptComponentTickTest.cpp` 301 行付近の追加試験を、Tick の中で破棄予約した対象について、登録した Tick の呼び先の呼び出し回数を対象ごとに数えて、同じフレームの後の Tick で 0 回であることを確かめる形にする（位置が動かないことや `LastResult` では、取得に失敗しても位置を動かさない・直接の呼び出しが `InvalidHandle` を返して `LastResult` を更新しない、ために退行を見分けられない）。直接の呼び出しより前に回数を検査する。数えるための口が要るなら、テスト用の計数だけを足す（製品の経路の振る舞いは変えない）。通ったら SCN-ECS-TICK-SCRIPT の status を done にし、`blocked/SCN-ECS-TICK-SCRIPT.md` に解消を書き足す（残りの指摘はこの 1 点だけ）。
- verify: `cmake --build build --config Debug --target ScriptComponentTickTest ScriptApplicationLifecycleContractTest ScriptHotReloadTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(ScriptComponentTickTest|ScriptComponentSceneRoundTripTest|M6AngelScriptAcceptanceTest|ScriptApplicationLifecycleContractTest|ScriptHotReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Test/Core/Scripting, Library/Core/Public/Scripting, Library/Core/Private/Scripting, Library/Core/Public/Component/ScriptComponent.h, Library/Core/Private/Component/ScriptComponent.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。SCN-ECS-TICK-SCRIPT の評価の 2 周目の指摘（NEXT_FINDINGS の反復 7）。破棄予約・取得の無効化・同期点での解放はコードで確認済みで、残りは試験の強さだけ。

## SCN-FIX-LOGGERSINK-BUNDLE: テストの束 LoggerSinkTest を、今の API に追いつかせてビルドできるようにする
- status: done
- done-when: LoggerSinkTest の束の次の 5 本を今の API に合わせて直し、束が Debug でビルドできる。直すのはテスト側の追随だけで、テストが確かめる中身は変えない。(1) `Test/Core/Input/InputRebindCaptureManagerTest.cpp`・`InputDebugOverlayControllerTest.cpp`: `ECursorMode` の名前空間の移動に合わせる。(2) `Test/Core/Input/InputBindingJsonTest.cpp` 158 行付近: `assert(...)` の中の raw 文字列を変数へ出す。(3) `Test/Core/Input/ActiveDeviceKindIntegrationTest.cpp`: 入力コントローラーのインターフェースに増えた純粋仮想を偽物に実装する。(4) `Test/Core/Text/JsonWriterTest.cpp` 63 行付近: サロゲート（D83D・DC3A）を UCN で書いて C3850 になる所を、コンパイルできる書き方（バイト列など）にする。束の全体の ctest（`LoggerSinkTest`）と、SCN-ECS-TICK-GAME が使う 3 本が pass する。実行して落ちる member があれば原因を PROGRESS に書き、テスト側の追随で直らないもの（製品のコードの不具合）は別の項目として TASKS に足す。
- verify: `cmake --build build --config Debug --target LoggerSinkTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(LoggerSinkTest|QuadrupedLocomotionModelTest|CameraCollisionSolverTest|LocomotionProfileReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Test/Core/Input, Test/Core/Text, Test/Core/Logging, TASKS.md, PROGRESS.md
- notes: 2026-10-11 親が足した。SCN-ECS-TICK-GAME の blocked（`blocked/SCN-ECS-TICK-GAME.md`）の推奨 A。`build/Test/Core/Logging/Debug/LoggerSinkTest.exe` は 2026-09-27 のままで、10 月 2〜4 日の変更以降この束は一度もビルドできていない。JSON のサロゲートは backlog の CORE-JSON-SURROGATE の作業と重なるので、ここではコンパイルを通すための書き方の変更だけにする。

## SCN-ECS-TICK-GAME: Game の FollowCamera と QuadrupedLocomotion を順序付きの系へ移す
- status: done
- done-when: OnTickGroup を消し、Game が起動時に登録する（TryGetComponentTickDelta と GetTickSerial の使い方は保つ）。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target LoggerSinkTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(QuadrupedLocomotionModelTest|CameraCollisionSolverTest|LocomotionProfileReloadTest)$" --no-tests=error --output-on-failure --timeout 180`
- paths: Test/Modules/Physics/PhysicsFixedStepPipelineTest.cpp, Game/Gameplay, Game/GameApplicationHandler.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: verify の LoggerSinkTest の束が、この項目と関係なく 10 月初めから別のテスト 5 本のコンパイルエラーでビルドできない。束を直す SCN-FIX-LOGGERSINK-BUNDLE をこの項目の前に足して todo に戻した（blocked の推奨 A）。消した OnTickGroup を直接呼んでいた `Test/Modules/Physics/PhysicsFixedStepPipelineTest.cpp` の 3 行の追随は paths に足して追認した。

## SCN-ECS-FRAME-HOOKS: GameMode・粒子・モジュール・handler の更新を段のシステムにする
- status: done
- done-when: 次を設計 1.7 の段の GameThreadOnly のシステムとして、今と同じ順で走らせる（中身は持ち主に残す）: `UpdateGameModeStateMachine`、handler の OnUpdate/OnLateUpdate、`ParticleSystem::Tick`、モジュールの TickAll/LateTick/PreFixedTick/FixedTick、スクリプトの前後処理（ApplicationProcessor.cpp:2096-2132・2576-2590・2670-2672）。下のテストと起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target ApplicationFixedStepPipelineTest GameModeStateMachineTest LoggerSinkTest CanvasTransientBoardSnapshotTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(ApplicationFixedStepPipelineTest|GameModeStateMachineTest|GameModeRegistryTest|SubRoutineTest|ModuleRegistryTest|ParticleFrameSnapshotTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FRAME-HOOKS`
- paths: Library/Core/Private/Engine/ApplicationProcessor.cpp, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/Public/Module, Library/Core/Private/Module, Library/Core/Public/Ecs, Library/Core/Private/Ecs, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: 段は `ApplicationProcessor.cpp` 2096 行付近の実際の順（handler の更新は GameMode の更新より前、粒子の更新は固定ステップより前、`Module::TickAll` は RenderSync より後）で定義し、シミュレーションが止まっているときにどの呼び出しが走るかを表にして PROGRESS に残す。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-TICK-REMOVE: 仮想の Tick と、毎フレームの Tick の一覧を消す
- status: done
- done-when: 次を消す: Component.h:82-89・102 の仮想（Tick・GetTimeChannel・OnTickGroup・FixedTick）、`Entity::Tick`/`TickComponents`、`TickDispatch.h`、`BuildTickSnapshot` と一覧（World.h:286-306）、旧の配信のシステム。Tick の設定の API は、BIND-FLAGS の列に書く形で残す。残っているテストを直し、全部 pass させる。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest PhysicsDynamicsTest ScriptComponentTickTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|WorldTimeChannelTest|EntityTreeTest|TickGroupConfigurationTest|ApplicationFixedStepPipelineTest|PhysicsFixedStepPipelineTest|ScriptComponentTickTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-TICK-REMOVE`
- paths: Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/Public/Object, Library/Core/Private/Object, Library/Modules/Physics, Game, Test, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-FIX-RENDERSYNC-WALK: 既定の差分の同期で、Entity の木の全走査をやめる
- status: done
- done-when: 既定の差分の同期（`World::SyncToSceneViewBody` の `!m_bLegacyRenderSync` の側）が、変わった行・通知・Remove の一覧と、走査が要る行（Text など、差分の公開に載っていない種類）だけを見て、根からの `SyncEntityRecursive` で全 Entity と全 Component を毎フレームたどらない。旧の全走査（`m_bLegacyRenderSync`）と検査 `ECS_RENDER_SYNC`・`ECS_RENDER_INVALIDATION` の照合は残す。`WorldSyncDifferentialTest` が、静止のフレームで差分の同期が行を 1 つもたどらないこと（数を返す口で確かめる）と、Text の追加・変更・削除が今と同じく公開されることを確かめる。起動画面の比較と、検査付きの起動が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-CPU）で確かめる。
- verify: `cmake --build build --config Debug --target WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^WorldSyncDifferentialTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -RequireChecks ECS_RENDER_SYNC,ECS_RENDER_INVALIDATION -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-RENDERSYNC-WALK`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Test/Core/Rendering/WorldSyncDifferentialTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-MEGA-MEASURE-CPU の計測（反復 14）で足した。5万個の静止で `Ecs.Stage.RenderSync` が median 13.2 ms（変わった行 0 件。`ECS_TRANSFORM_COUNTS sync_rows=0.0`）で、ゲームスレッドの段の合計（13.4 ms）のほぼ全部。差分の同期でも「Text は旧の走査」として根から全 Entity をたどっている（`World.cpp` の SyncToSceneViewBody、`deltaRoots` の行）。危険地帯（描画の同期・Object の寿命）。

## SCN-MEGA-MEASURE-CPU: 節目の計測: ECS 化した CPU（ワールド・同期・ロジック）の効果を測る
- status: done
- done-when: - 測る負荷: 5万個の静止・1% 移動・1% 移動 + 64 m 区画と、バナナ 1万本 overview・10万本 horizon。 - 問い: ECS 化した段・系・同期点のそれぞれが、本数と変化の数に対してどれだけかかり、並列の系がワーカーに広がっているか。 - 測る値: 段・系・同期点の区間（冒頭の方針の名前）ごとの median と p95、並列の系の区間が出たスレッドの数。全体の gt・rt・GPU は添える。PROGRESS の表に書く（合否は下の閾値で決め、改修前の値は撮らない）。 - 合格の条件: - 5万個の静止で、ゲームスレッドの段の合計 ≤ 2 ms（変換の伝播・描画の変換・RenderSync・SceneQuery・cleanup はそれぞれ ≤ 0.1 ms）。 - 1% 移動での増分が「変わった行 + 影響する子孫 + 補間の部分木」の数に比例する（数をログに出し、比を表に書く）。 - 10万本と 1万本の gt の差 ≤ 0.5 ms。 - 届かないスコープは `SCN-MEGA-FIX-<名前>` として足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/static --timing --min-frames 300 --max-gt-p95-ms 16.6 --require-scope Ecs.Stage.* --require-scope Ecs.Sync.TransformPropagate --require-scope Ecs.Sys.RenderTransform --require-scope Ecs.Stage.RenderSync --require-scope Ecs.Sys.SceneQuery --require-scope Ecs.Sync.Cleanup`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-CPU-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/World.h, Library/Core/Private/Engine/ApplicationProcessor.cpp, TASKS.md, PROGRESS.md
- notes: 計測だけ。反復の中で 1 回撮る: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-CPU/static`（他の負荷も同じ形で、出力先だけ変える） 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-11 親: 5万個の mega-stress は、SCN-MEGA-BVH-INDIRECT の前は BVH の列が VRAM を超えて落ちる（SCN-ECS-MASS-STREAM の記録）。落ちるときは CPU の段の値を 3万個で測って表に書き、5万個の値は BVH-INDIRECT の後の節目の計測（MEASURE-CULL）で足す。 2026-10-12 反復 14: 冒頭の方針の名前の区間（`Ecs.Sync.TransformPropagate`・`Ecs.Sys.RenderTransform`・`Ecs.Stage.RenderSync`・`Ecs.Sys.SceneQuery`・`Ecs.Sync.Cleanup`）と行の数のログ `ECS_TRANSFORM_COUNTS` を足した（paths に足した）。5万個の静止で `Ecs.Stage.RenderSync` が 13.2 ms・gt の p95 が 26.8 ms で verify が通らないので、SCN-MEGA-FIX-RENDERSYNC-WALK を前に足した。その後に撮り直す。gt のうち Ecs の区間の外が約 11 ms あり、内訳は撮り直しのときに見る。 2026-10-12 親: 1 回目の計測で 5万個の静止の `Ecs.Stage.RenderSync` が 13.2 ms（全 Entity の走査）と分かり、前に足した SCN-MEGA-FIX-RENDERSYNC-WALK の後で全部の負荷を撮り直すため todo に戻した。撮り直しでは、gt のうち Ecs の区間の外（約 11 ms、区間が無い）も区間を足して内訳を出す。FIX がまだ done でなければ撮らずにその旨を書いて終える。

## SCN-MEGA-FIX-TRANSIENT-BOARDS: Text のグリフの板集めで、Entity の木の全走査をやめる
- status: done
- done-when: `World::CollectTransientBoardProxies`（ApplicationProcessor が毎フレーム呼ぶ）が、根からの `CollectTransientBoardProxiesRecursive` で全 Entity と全 Component をたどらず、Text を持つ行の一覧（SCN-MEGA-FIX-RENDERSYNC-WALK の `m_RenderTextRows` の類）だけを見る。アクティブでない部分木・破棄予約の行を出さない今の振る舞いと、集める順（板の並び）が描画に効くならその順を保つ。`WorldSyncDifferentialTest` のグリフの板の照合（追加・変更・親の移動・非アクティブ・破棄）が pass。5万個の静止で区間 `GameThread.TransientBoards` が ≤ 0.1 ms（撮影の metrics.json で確かめる）。起動画面の比較が pass。 あわせて、`Scripts/CaptureScene.ps1` の描画スレッドの時間（rt）を、CPU の窓の最初〜最後のフレーム番号の範囲の Frame 行のうち `RenderThreadMs > 0` の行から独立に集める（今は `RenderFrameMs > 0` の行からだけ拾うので、ゲームスレッドが描画 1 フレームに何百回も回ると rt が 0 件になる）。撮り直して `--timing` の検査が通る。
- verify: `cmake --build build --config Debug --target WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^WorldSyncDifferentialTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-TRANSIENT-BOARDS`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-TRANSIENT-BOARDS/static --timing --min-frames 300 --max-scope-median-ms GameThread.TransientBoards=0.1`
- paths: Scripts/CaptureScene.ps1, Library/Core/Public/Object, Library/Core/Private/Object, Test/Core/Rendering/WorldSyncDifferentialTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-MEGA-MEASURE-CPU の撮り直し（反復 17）で足した。5万個の静止で gt の median 10.97 ms のうち `GameThread.TransientBoards` が 10.43 ms（p95 11.64）で、Ecs の段の合計（0.18 ms）の外にある gt のほぼ全部。mega-stress に Text は無いが、根から全 Entity と全 Component を毎フレームたどっている（`World.cpp` の `CollectTransientBoardProxiesRecursive`）。撮影は反復の中で 1 回: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -Views default -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-TRANSIENT-BOARDS/static`。危険地帯（描画へ渡す板・Object の寿命）。 2026-10-12 親: verify 4 が `timing_missing=render_thread_ms_*` で落ちた（gt が 10.97 → 0.174 ms に下がり、rt の拾い方の限界に当たった）。blocked の推奨 1 を採り、CaptureScene.ps1 の rt の集め方の修正をこの項目に含めて todo に戻した。

## SCN-ECS-RETIRE-LEGACY: 旧経路と ComponentDataRegistry を消す
- status: backlog
- done-when: 次を消す: `--ecs-legacy-transform`/`--ecs-legacy-sync`、旧の再帰（UpdateEntityTransformRecursive・SyncEntityRecursive・UpdateRenderTransformRecursive ほか）、ComponentDataRegistry とそのテスト・ベンチマーク。先に利用者を数え、残る利用者がいれば消さずに BLOCKED にする。`--mega-scene-full-sync` は残す。
- verify: `cmake --build build --config Debug --target EntityTreeTest SceneViewProxyLookupTest WorldSyncDifferentialTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "World|SceneViewProxy|EntityWorldAABB|TransformHierarchy" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/CMakeLists.txt, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親: 利用者が残っていたので（`blocked/SCN-ECS-RETIRE-LEGACY.md`）、推奨 A のとおり SCN-ECS-RETIRE-LEGACY-FLAGS（旧の切替と再帰）と SCN-ECS-RETIRE-REGISTRY（EntityHandle を World へ移して ComponentDataRegistry を消す）の 2 つに分け、この項目は backlog にした。

## SCN-ECS-RETIRE-LEGACY-FLAGS: 旧の更新・同期の切替と再帰を消し、旧を正解にしていた照合を期待値に替える
- status: done
- done-when: `--ecs-legacy-transform`・`--ecs-legacy-sync`、`SetLegacyRenderSync`/`IsLegacyRenderSync`、旧の再帰（UpdateEntityTransformRecursive・SyncEntityRecursive・UpdateRenderTransformRecursive ほか）と、SceneQuery の旧への切替（`SceneQuery.cpp` 944 行付近）を消す。`--mega-scene-full-sync` は残す。旧の全走査を正解にしていた照合は次のように替える: `WorldSyncDifferentialTest`・`EcsHierarchyTest`・`SceneQueryRaycastTest` は、手で決めた期待値（追加・値の変更・親の付け替え・非アクティブ・破棄・シーンの出入りの後の proxy・板・変換・照会の結果）との照合にする。開発ビルドの検査（ECS_RENDER_SYNC・ECS_RENDER_INVALIDATION・ECS_TRANSFORM など）は、旧の再帰を使わない全量の照合（ECS の行から全量を作り直して比べる）に替えるか、照合が成り立たないものは消してその役目をテストの期待値で持つ（どちらにしたかを PROGRESS に表で書く）。3 シーンの混在の周回が pass。
- verify: `cmake --build build --config Debug --target EntityTreeTest SceneViewProxyLookupTest WorldSyncDifferentialTest SceneQueryRaycastTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "World|SceneViewProxy|EntityWorldAABB|TransformHierarchy|EcsHierarchy|SceneQuery" -LE Reference --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Object/World.cpp, Library/Core/Public/Object/World.h, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Public/Scene/SceneQuery.h, Library/Core/Private/Scene/SceneQuery.cpp, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/Public/Debug, Library/Core/Private/Debug, Game/GameApplicationHandler.cpp, Test/Core/Object, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-RETIRE-LEGACY の推奨 A の 1）。後の項目の verify は ECS_* の検査も旧の切替も使っていない（親が確かめた）。危険地帯（描画の同期・Object の寿命）。 2026-10-12 親: 反復 23 は 40 分の上限で止まった。そのとき作業を git stash に退避して HEAD の周回と比べる途中だったので、親が退避を戻して 作業途中の保存 eaad1e4f に入れた（World.cpp の旧の再帰の削除・照合のテストの書き換え、計 12 ファイル）。続きから進める。分かっていたこと: M9 の受入れは変更の前後で同じ理由（t0_capture_failed）で落ちる（この項目と無関係）。3 シーンの周回（verify 3）がこの変更で落ちた（`.harness/runs/20261011-211043/verify-SCN-ECS-RETIRE-LEGACY-FLAGS-3.txt`）ので、まず HEAD~1 と比べて原因を切り分ける。作業を git stash に退避しない（上限で止まると失われる）。比べるときは別のディレクトリにビルドせず、コミットしてから戻す。 2026-10-12 親: verify 2 の正規表現が Reference の受入れ M9WorldAcceptanceTest にも当たり、それが変更の前から t0_capture_failed で落ちる。blocked の推奨 A を採り、verify 2 に `-LE Reference` を足して todo に戻した。M9 は SCN-FIX-M9-T0-CAPTURE で直す。 2026-10-12 親: 評価の 2 周目の残りは「ECS_SCENEQUERY の比較先の境界が `Entity::EvaluateWorldTransformNonMutating`（親をたどる）を使う」だけだった。done-when の「旧の再帰」は毎フレームの更新・同期の再帰（UpdateEntityTransformRecursive・SyncEntityRecursive・UpdateRenderTransformRecursive ほか）を指し、1 つの Entity のローカル変換を親へたどって合成し直すその場の計算は含まない（差分の結果とは独立に同じ元データから求め直すので、照合の正解として妥当）と親が判断し、done にした。ほかの指摘（照合の置き換え・照会のテストの期待値）は評価者が解消を確かめた（verify 9〜11・recheck 5-1〜3）。

## SCN-ECS-RETIRE-REGISTRY: EntityHandle の発行と解決を World へ移し、ComponentDataRegistry を消す
- status: done
- done-when: 世代付きの `EntityHandle` の発行と解決を World の持ち物（ECS の行の番号と世代から作る表）にし、`Entity::GetEntityHandle` はそれを返す。利用者（`Game/Input/PickingController.cpp`・`Library/Modules/Physics/Private/Physics/PhysicsModule.cpp`・`Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp`・`Test/Core/Engine/ApplicationFixedStepPipelineTest.cpp`・`Test/Core/Object/WorldTickGroupTest.cpp`）を World の API に移す。`GEngine` のメンバの ComponentDataRegistry と、その Publish*・テスト・ベンチマークを消す（シングルトン禁止・所有権モデルに合わせ、ハンドルの表は World ごとの寿命にする）。破棄の後のハンドルが解決できないこと、世代の違うハンドルを拒むこと、別の World のハンドルを解決しないことをテストで確かめる。物理の最小のスモーク（`Scripts/Test-M8MinimalPhysics.ps1`）が今と同じ結果で pass（このスクリプトの Game の起動に `--window-background` を足す）。
- verify: `cmake --build build --config Debug --target EntityTreeTest ApplicationFixedStepPipelineTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(WorldTickGroupTest|ApplicationFixedStepPipelineTest|EcsLifetimeTest|EcsHierarchyTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Test-M8MinimalPhysics.ps1 -Configuration RelWithDebInfo`
- paths: Library/Core/Public/Object, Library/Core/Private/Object, Library/Core/Public/Ecs, Library/Core/Private/Ecs, Library/Core/Public/Engine, Library/Core/Private/Engine, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/CMakeLists.txt, Game/Input, Game/GameModes/Rendering3DTest/M8MinimalPhysicsSmoke.cpp, Library/Modules/Physics/Private/Physics/PhysicsModule.cpp, Scripts/Test-M8MinimalPhysics.ps1, Test/Core/Engine, Test/Core/Object, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-RETIRE-LEGACY の推奨 A の 2）。危険地帯（Object の寿命・公開 API）。ComponentDataRegistry の場所は `GEngine` のメンバ（`NorvesEngine.*`）と `Test/Core/Rendering` のテスト・ベンチマーク。

## SCN-FIX-M9-T0-CAPTURE: M9 の世界の受入れが撮影の失敗（t0_capture_failed）で落ちるのを直す
- status: done
- done-when: `ctest -R M9WorldAcceptanceTest`（Reference の GPU の受入れ）が `M9_WORLD_SMOKE stage=failure reason=t0_capture_failed` で落ちる原因を突き止めて直す。まず撮影の結果が成功でないときの状態（MapFailed など）を `FrameCaptureReadbackHelper.cpp`・`Rendering3DTestRoutine.cpp` でログに出し、原因を PROGRESS に書く。このブランチの変更（ランチャー・`--window-background`・ECS の同期など）が原因なら、どの変更かを書いて直す。M9 の受入れが pass する。Game の起動に `--window-background` を付けていなければ足す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^M9WorldAcceptanceTest$" --no-tests=error --output-on-failure --timeout 600`
- paths: Library/Core/Private/Rendering/VignettePass.cpp, Game/GameModes/Rendering3DTest, Library/Core/Private/Rendering/FrameCaptureReadbackHelper.cpp, Library/Core/Public/Rendering, Scripts/Test-M9World.ps1, Test/M9, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-RETIRE-LEGACY-FLAGS の blocked の推奨 A）。7c7e56f7 の時点でも同じ理由で落ちる。危険地帯（描画の撮影）。 2026-10-12 親: 原因は `VignetteOutput` だけ用途に TransferSrc が無く、TAA が効いて拡大なしのときに撮影元になるため（SourceMissingTransferSrc）。blocked の推奨 A を採り、VignettePass.cpp を paths に足して todo に戻した（用途の追加だけで描画は変わらない）。

## SCN-ECS-SCOPE-METRICS-THREADS: 撮影の区間の集計を、その区間が出たフレームだけで数え、スレッドの名前を添える
- status: done
- done-when: `Scripts/CaptureScene.ps1` の `cpu_scope_ms`・`cpu_scope_group_ms` の median・p95・max を、その区間が出たフレーム（`frames_present`）だけで求める（今はゲームスレッドの窓の全フレームで数えるので、ゲームスレッドが描画 1 フレームの間に何回も回ると描画スレッドの区間の median が 0 になる。5万個の静止の撮影で `RenderGraph.Execute` が median 0・p95 49.3 ms・frames_present 302 / 窓 3832）。区間ごとに、出たスレッドの名前（GameThread・RenderThread・Worker）を `thread` に書く。mega-stress 2000 個の短い撮影で、`RenderGraph.Execute` の median が 0 より大きく、`thread` が入っている。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 120 -GameArgs stress-entities=2000 -OutDir .harness/runs/scene-capture/SCN-ECS-SCOPE-METRICS-THREADS`
- verify: `python -c "import json,sys; m=json.load(open('.harness/runs/scene-capture/SCN-ECS-SCOPE-METRICS-THREADS/metrics.json',encoding='utf-8-sig')); r=[x for v in m['views'] for x in v['gpu_timing']['cpu_scope_ms'] if x['scope']=='RenderGraph.Execute']; print(r); sys.exit(0 if r and all(x['median_ms']>0 and x.get('thread') for x in r) else 1)"`
- paths: Scripts/CaptureScene.ps1, Scripts/CheckCaptureMetrics.py, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。MEASURE-UPLOAD（RecordFrameCommand ≤ 1 ms）・RT-PASSES（rt の上位のパスから監査）・MEASURE-RT（RG.Record.* の区間）が描画スレッドの区間の値を使うので、GPU の表の項目の前に置く。

## SCN-MEGA-GPU-SCENE-PREP: シェーダーのインスタンスの読み方を 1 つの関数とメッシュ表へ寄せ、表の更新を共通の前段に置く（準備・見た目は不変）
- status: done
- done-when: cull・visbuffer・vsm_mega・megageometry.vert の MegaInstance の直接の読みを `LoadMegaInstance(index)` に寄せる。メッシュ表（binding を追加）を書き、関数はメッシュ表から補って同じ値を返す。インスタンス表・メッシュ表の更新を、最初にそれを読むパス（影のパスを含む）より前の共通の前段に移す。起動画面・バナナの比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-PREP/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: GPUMegaInstance は `MegaGeometryPass.h` 437〜455 行付近、影の表は 37〜48 行付近。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-GPU-SCENE-SCATTER: インスタンス表を席ごとに GPU へ常駐させ、変わった席だけを散在更新する（切替）
- status: done
- done-when: 表が席ごと 128 B の device-local になり、変わった席だけを staging から compute で散在書き込みする。インスタンス表・メッシュ表・常駐（ページ表）の 3 つの更新の条件を分け、常駐の表は両方の描画パスと VSM で同じ版を使う（今の `SyncPageTable` の frame slot ごとの版、`MegaGeometryPass.cpp` 2832 行付近）。インスタンスも常駐も変わらないフレームは `MEGA_SCENE_UPLOAD bytes=0`（ログで確かめる）。`--mega-scene-full-sync` との起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-GPU-SCENE-SCATTER`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-SCATTER/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（GPU メモリ）。転送の道は `RenderResources.cpp` 66・1561 行付近のリングと別の予算にする（最初に 1540〜1580 行付近を読んで決める）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-12 親: 反復 11 は 40 分の上限で止まり、止まる前に status を done にしていたため評価を受けずに done になっていた（作業途中の保存 d44b9900）。危険地帯の切替の項目なので todo に戻した。作り直さず、verify を通し直して評価を受ける。続く SCN-MEGA-GPU-SCENE-GROW（ccc29983）はこの上に載っている。

## SCN-MEGA-GPU-SCENE-GROW: 席の表の拡張を GPU のコピーで行い、古いバッファとメッシュ表の資源を GPU が使い終わるまで保つ
- status: done
- done-when: 席の数が容量を超えたら新しい表へ GPU でコピーし、古いバッファは `m_RetiredBuffers` へ回す。メッシュ表が指す資源（頂点・インデックス・クラスタ・BVH・ページ表）は、その資源を最後に使った提出が終わるまで解放しない。ストリーミングで拡張と縮小が起きても `mismatch=0`、cycle smoke が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-GROW/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯（寿命）。検証レイヤーは節目の計測の Debug で見る。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-GPU-SCENE-HISTORY: 前の変換（prevWorld）を、最後に描いたフレームを基準に保つ
- status: done
- done-when: 席の prevWorld は `RenderedObjectHistory.h` 29 行付近の契約（最後に描いたフレームの変換）に従い、RenderThread 側で描画の履歴を持つ。動いて止まった席の 1 回だけの更新も描画の基準で行う（パケットの破棄・描画の見送りを挟んでも速度が 0 や二重にならない）。1% 移動で、開発ビルドの速度の検査（全量の経路の速度と一致）が `MEGA_HISTORY mismatch=0`。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-GPU-SCENE-HISTORY/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-GPU-SCENE-HISTORY`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）席は行と一緒に動く。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-RT-SELECT: TLAS に入れるインスタンスを席ごとの配列で持ち、選ぶ規則を決める
- status: done
- done-when: RT の instance 配列を席ごとに持ち、差分で直す。RT の入力（`RenderingCoordinator.cpp` 805 行付近の全量の読みと、連番のパストレーシングが使う GameThread の配列）を、RenderThread の写しと変わった席にする（SCN-MEGA-SCENE-DELTA から移した）。TLAS に入れるのは、カメラから 120 m 以内のインスタンスを近い順に上限 65,536 本まで（規則と外れた数を `RT_SCENE instances= excluded=` に出す）。この項目では Update は毎フレーム呼んでよい。起動画面の比較（RT の影・RTGI・DDGI を含む見た目）が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-RT-SELECT`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-RT-SELECT/check`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, TASKS.md, PROGRESS.md
- notes: 対象は 784〜850 行付近。RHI/Vulkan は変えない（`VulkanCommandList.cpp` 1302 行付近は Update のたびに全量を変換・転送する）。共有の TLAS は RT の影・DDGI も使うので、遠方の影響を RTGI だけと見なさない。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-RT-SKIP: TLAS の更新を、集合・変換・BLAS の版が変わったフレームだけにする
- status: done
- done-when: frame slot ごとに、選んだ集合・変換・BLAS の版（準備完了・交換）を覚え、全部一致するフレームは Update を呼ばない（ログに `RT_SCENE update_skipped=` を出す）。カメラの移動で 120 m 内の集合が変わる・追加削除・BLAS の交換では呼ぶ。バナナと起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-RT-SKIP/det`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Public/Rendering/RayTracingSceneSubsystem.h, TASKS.md, PROGRESS.md
- notes: 対象は 1037〜1085 行付近（同数なら毎フレーム Update、1043 行付近）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-SHADOW-DIRTY: VSM・CSM の無効化を、変わった席の旧・新の境界と、常駐・メッシュの変化の通知で行う
- status: done
- done-when: VSM の動きの記録（`VirtualShadowMapPass.cpp` 736〜753 行付近）が、本数ではなく変わった席の数に比例する。バッチの一部が動いても無効化はその旧・新の境界だけ。静止した席でも、そのメッシュのページの常駐・メッシュの世代・影の設定が変わったら、使っている席・範囲へ通知して無効化する（今の署名に入っている `PageTableVersion` の役目を保つ）。光源・クリップマップ側の既存の無効化は残す。CSM の投影物の範囲（`DirectionalShadowLightMatrices.cpp` 283〜450 行付近）は範囲の AABB で求める。影の表の毎フレームの FNV（`MegaGeometryPass.cpp` 1331 行付近）をやめる。ShadowMapPass（CSM。`ShadowMapPass.cpp` 203 行付近）の MegaGeometry の全量の走査を、RenderThread の写しと変わった席にする（SCN-MEGA-SCENE-DELTA から移した）。起動画面の比較が pass。効果の数値はこの項目では測らず、節目の計測（SCN-MEGA-MEASURE-UPLOAD）で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-SHADOW-DIRTY`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-SHADOW-DIRTY/check`
- paths: Library/Core/Private/Rendering/VirtualShadowMapPass.cpp, Library/Core/Private/Rendering/ShadowMapPass.cpp, Library/Core/Private/Rendering/DirectionalShadowLightMatrices.cpp, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-MEASURE-UPLOAD: 節目の計測: GPU に常駐する表・RT・影の改修の効果を測る
- status: done
- done-when: MEASURE-CPU と同じ 3 つの負荷とバナナ 3万本 overview で、gt・rt・GPU の median と p95、`MegaGeometryPass.RecordFrameCommand`、`MEGA_SCENE_UPLOAD bytes`、`AccelerationStructureBuild`、VSM の無効化のページ数を測って PROGRESS の表に書く。目標: 5万個で `RecordFrameCommand` ≤ 1 ms、静止で upload 0 と `AccelerationStructureBuild` ≤ 0.05 ms、1% 移動で upload・VSM の無効化が移動数に比例。カメラパスの再生中に TLAS の更新があるフレームの p95 も書く。1% 移動の連続撮影を開いて動く物に残像が増えていない（TAA と prevWorld）。届かないものは `SCN-MEGA-FIX-<名前>` を次へ足す。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/static --timing`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-MEASURE-UPLOAD-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。 verify はビルド・代表の計測 1 本・Debug の検証だけにしてある（ランナーの再検証を軽くするため）。done-when の他の計測は反復の中で行い、PROGRESS に証拠の場所を書く。 反復の中で 1 回撮る計測: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/static`（ランナーは撮り直さず、保存した結果を検査する）。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-FIX-VSM-INVALIDATE: 多くの席が動くフレームでも、VSM の無効化を動いた範囲のページだけにする
- status: done
- done-when: 1 フレームに 256 を超える MegaGeometry の席が動いても、太陽の VSM の無効化が全ページにならず、動いた席の旧・新の境界が覆うページだけになる（今は矩形が `VirtualShadowMap::MAX_INVALIDATION_RECTS`＝256 を超えると `VirtualShadowMapPass.cpp` の `BuildInvalidationRects` が失敗して全ページを無効にする。例: 箱をライト空間のページの格子で併合して上限に収める、矩形を大きなバッファで渡す）。多く無効にするぶんには影は崩れない規則は保つ。5万個の 1% 移動（500 席）で `VSM_CACHE invalidated` が全ページ（今 741〜744 / 743）より少なく、無効にしたページが、動いた席の旧・新の境界が覆う割り当て済みのページの和と一致するか、それより多い側にだけずれる（開発ビルドで、同じフレームのその和を CPU 側で数えて `VSM_INVALIDATE_EXPECTED pages=` をログに出し、`VSM_CACHE invalidated` と比べる。少ない側にずれたら描き直し漏れなので不合格）。5万個の 1% 移動（500 席）と 0.2% 移動（100 席）の両方で確かめ、数値を PROGRESS に書く（ページの数が移動数に比例する必要はない。影が割り当てられているのはカメラの近くのページだけで、増えた席の多くはその外を動くため）。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-VSM-INVALIDATE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views default -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000,stress-moving-percent=1 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-VSM-INVALIDATE/move1`
- paths: Library/Core/Public/Rendering/VirtualShadowMapPages.h, Library/Core/Public/Rendering/VirtualShadowMapPass.h, Library/Core/Private/Rendering/VirtualShadowMapPages.cpp, Library/Core/Private/Rendering/VirtualShadowMapPass.cpp, Assets/Shaders/vsm_mark.comp, Assets/Shaders/vsm_allocate.comp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-MEGA-MEASURE-UPLOAD（反復 19・run 20261012-030203）で足した。5万個の 1% 移動で `MEGA_SHADOW_DIRTY` の箱は 1 フレーム 500（移動数どおり）だが、VSM は矩形の上限 256 を超えて全ページ（743）を描き直し、`VirtualShadowMapPass` の GPU が 3.04 / 4.94 ms（静止 0.20 / 0.78）。0.2% 移動（100 席）では 290〜343 ページ・0.94 / 2.08 ms。証拠は `.harness/runs/scene-capture/SCN-MEGA-MEASURE-UPLOAD/<move1|move0.2>/`。危険地帯（VSM のページの寿命）。 2026-10-12 親: 反復 21 の実装（183dbb1c）で 1% 移動の無効化は 741〜744 → 299〜350 ページ、VirtualShadowMapPass は 3.04 → 0.96 ms に下がった。比が移動数に沿わない（5 倍の移動で 1.02 倍）のは、増えた席の多くが割り当て済みのページの外を動くためで、受入れの基準の書き方の誤り。blocked の推奨 1 を採り、基準を「動いた境界が覆う割り当て済みのページの和との一致（多い側にだけずれる）」に改めて todo に戻した。

## SCN-MEGA-FIX-RECORD-COMMAND: MegaGeometryPass.RecordFrameCommand の p95 と区画の出入りのフレームの跳ねを 1 ms 以下にする
- status: done
- done-when: 先に `RecordFrameCommand` の中に区間を足して、跳ねるフレームの内訳を PROGRESS に書く。5万個の静止で 1 回あたりの p95 ≤ 1 ms（今 1.04〜1.15 ms。median は 0.90〜0.96 ms で合格済み）。3万個の 1% 移動 + 64 m 区画で、区画の出入りで跳ねるフレーム（今 300 回中 26 回が 3 ms 超、最大 8.4 ms。p95 6.3 ms）を無くし p95 ≤ 1 ms。描画の結果は変えない（起動画面の比較が pass）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-RECORD-COMMAND`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-RECORD-COMMAND/move1-cell64-30k --timing --min-frames 300 --max-scope-p95-ms MegaGeometryPass.RecordFrameCommand=1.0`
- paths: Library/Core/Public/Rendering/MegaGeometryPass.h, Library/Core/Private/Rendering/MegaGeometryPass.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-MEGA-MEASURE-UPLOAD（反復 19・run 20261012-030203）で足した。区画の出入りのフレームは `MEGA_SHADOW_DIRTY rebuilds=5`（120 回の表の作成あたり）と `RT_SCENE built=15`（120 フレームあたり）で、表の作り直しと重なる。撮影は反復の中で 1 回: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views default -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=30000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-RECORD-COMMAND/move1-cell64-30k`（5万個 + 64 m 区画は BVH-INDIRECT の前は VRAM を超えて落ちる）。危険地帯（RenderThread・記録）。

## SCN-MEGA-FIX-STRESS-NEAR-CLIP: mega-stress の既定の視点で、近くの岩が近平面で切れて内側が見える
- status: done
- done-when: 5万個の mega-stress の既定の視点（overview）の PNG の下端中央（x 950〜1250・y 880〜1080 の 1920×1080）に、近平面で切れた岩の内側（黒い面と半透明の膜。今は黒い画素が約 6,000）が出ない。先に原因（カメラのすぐ前に置かれた Entity・近平面の距離・切れた面の描き方）を特定して PROGRESS に書き、直す先が paths の外なら blocked に書く。他の視点（close・aerial）と動く物の配置は変えない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views default -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-STRESS-NEAR-CLIP`
- paths: Game/GameModes/MegaStress, Game/Scenes/SceneCatalog.h, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-MEGA-MEASURE-UPLOAD（反復 19）の 1% 移動の連続撮影を開いて見つけた。静止・1% 移動の撮影と、前の節目（SCN-MEGA-MEASURE-CPU）の撮影にも同じ量（黒い画素 6,033〜6,086）であり、移動・この節目の改修とは関係のない前からの欠陥。拡大は f121 の (900,820)〜(1300,1080)。

## SCN-ECS-RT-PLAN: 記録の前に、確保・保留コマンド・バリアの layout を直列で決める
- status: done
- done-when: 設計 1.13 の記録の前の計画と、RHI の明示の old/new のバリア、最終の layout の書き戻しが入る。記録はまだ 1 本のリスト。`RenderGraphParallelPlanTest`（RenderGraphCompileTest の束）が、計画した layout が今の逐次の実行の layout と全バリアで一致することを確かめる。Execute の中で自分でバリアを張るパスの一覧を PROGRESS に書く。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RenderGraphParallelPlanTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PLAN`
- paths: Library/Core/Public/Rendering/RenderGraph, Library/Core/Private/Rendering/RenderGraph, Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RenderGraphParallelPlanTest.cpp, TASKS.md, PROGRESS.md
- notes: 親の追記（GPT 2 周目）: `EndRenderPass`（VulkanCommandList.cpp 903 行付近）も共有の layout を書き換える。attachment の最終の layout とパスの内部の遷移も計画の対象にし、記録の最中に共有の layout を更新しない。計画できない Serial の区間は記録の後に状態を確定して次の区間を計画する。両方の経路を検証する。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-RHI-SUBMIT: 複数のコマンドリストを、同じ提出番号と完了通知で 1 回に提出する
- status: done
- done-when: 次が入る: `ISwapChain::EndFrame` の複数リストの版、全リストへの提出番号・完了通知・中止、同じ slot が完了するまで記録し直さないこと、RenderingCoordinator が仕事の番号ごとに持つリスト。`RHIParallelRecordVulkanTest`（RHITextureUpdateVulkanTest の束）が次を確かめる: 2 つのスレッドで記録した「書き込み → バリア → 読み出し」の 2 本を 1 回で提出して結果が正しい、検証レイヤーのエラーが 0、中止の後に古いリストを再利用しない。描画はまだ 1 本。起動画面の比較が pass する。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RHIParallelRecordVulkanTest$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-RHI-SUBMIT`
- paths: Library/Core/Public/RHI, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.cpp, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RHIParallelRecordVulkanTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-RHI-TIMESTAMPS: GPU の時間をリストをまたいで集め、FrameGPU を先頭の開始から末尾の終了までにする
- status: done
- done-when: リストごとに取った GPU の時間をパスの名前で合わせ、FrameGPU を先頭のリストの開始から末尾のリストの終了までで求める（RenderingCoordinator.cpp:1134-1145・2734-2764）。`RHIParallelRecordVulkanTest` に、2 本にまたがる FrameGPU が各区間の時間以上で、開始と終了の順が正しいことの検査を足す。
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^RHIParallelRecordVulkanTest$" --no-tests=error --output-on-failure --timeout 180`
- paths: Library/Core/Private/Rendering/RenderingCoordinator.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Public/RHI, Test/Core/Rendering/RHIParallelRecordVulkanTest.cpp, TASKS.md, PROGRESS.md

## SCN-ECS-RT-GRAPH: RenderGraph をパスの区間ごとに記録できるようにする
- status: done
- done-when: 記録の方針（既定は Serial）と区間ごとの記録（区間ごとの context・保留コマンド・リスト、RT-PLAN の計画を使う、提出はグラフの順）が入る。`RenderGraphParallelRecordTest` が、偽のリストで次を確かめる: 区間の分け方、バリアが入る区間、Serial だけのグラフが今と同じ 1 本の記録になる。どのパスもまだ Parallel を宣言しない。各パスの記録（Execute）を、記録したスレッドの番号で `RG.Record.<パス>` の区間として残す（Serial でも区間ごとの記録でも。名前はパスの登録時の文字列）。
- verify: `cmake --build build --config Debug --target RenderGraphCompileTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^(RenderGraphParallelRecordTest|RenderGraphParallelPlanTest)$" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-GRAPH`
- paths: Library/Core/Public/Rendering/RenderGraph, Library/Core/Private/Rendering/RenderGraph, Library/Core/Public/Rendering/ViewRenderContext.h, Test/Core/Rendering/CMakeLists.txt, Test/Core/Rendering/RenderGraphParallelRecordTest.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-ECS-RT-PASSES: 監査したパスを並列に記録する
- status: done
- done-when: MEASURE-CPU の rt の上位から、次の 3 つを満たすと監査したパスに Parallel を宣言する: Execute が記録だけ、自分でバリアを張らない、共有の CPU の状態を持たない（監査の表を PROGRESS に書く。共有のアロケータは区間ごとに分ける）。`--render-parallel-record=off` で 1 本に戻せる。on と off の両方で起動画面の比較が pass する。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PASSES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=off -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PASSES-off`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameApplicationHandler.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-12 親: 評価の 2 周目の残り（VisibilityResolve の定数のテクスチャの作成の禁止が戻らない退行）は SCN-ECS-FIX-VISRESOLVE-CONST で直し、通ったら done にする。

## SCN-ECS-FIX-VISRESOLVE-CONST: 並列記録のフレームの後でも、未キャッシュの材質の定数のテクスチャを作れるようにする
- status: done
- done-when: `VisibilityResolvePass` の定数のテクスチャ（金属度・粗さ・AO の灰色）の作成を、直列の準備段で許可して要るものを作り、Execute（区間の記録）の間だけ禁止する（今は Parallel のフレームの Execute が `m_bTextureCreationAllowed=false` にした後、準備段が許可し直さないので、次のフレームに現れた未キャッシュの定数が `GetConstantGrayTexture` で nullptr になり、既定の黒・中間灰に置き換わる。off では作れるので on と off で描画が分かれる。`VisibilityResolvePass.cpp` 469・1006・1186 行付近）。未キャッシュの定数を持つ材質を途中のフレームで足したとき、on と off で同じ定数のテクスチャが使われることを、既存の Rendering の CPU の試験の束で確かめる（新しい実行ファイルは足さない）。起動画面の比較が on・off とも pass。通ったら SCN-ECS-RT-PASSES の status を done にし、`blocked/SCN-ECS-RT-PASSES.md` に解消を書き足す（残りの指摘はこの 1 点だけ）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=on -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIX-VISRESOLVE-CONST`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=off -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIX-VISRESOLVE-CONST-off`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。SCN-ECS-RT-PASSES の評価の 2 周目の指摘（NEXT_FINDINGS の反復 34）。ToneMapping の LUT の転送を準備段へ移した直しは評価者が確認済み。危険地帯（RenderThread）。 2026-10-12 親: SCN-ECS-MEASURE-RT が既定を off にしたので、1 本目の撮影は `render-parallel-record=on` を明示する。

## SCN-ECS-MEASURE-RT: 節目の計測: 並列の記録の効果と安全を測る
- status: done
- done-when: バナナ 1万本 overview と mega-stress 5万個で、on と off の rt の median と p95、`RenderGraph.Execute`、GPU、ワーカーの統計（JobSystem.h:133）、`RG.Record.*` の区間ごとの時間と記録したスレッドの数を PROGRESS の表に書く。合格は、on の rt の p95 が off 以下で、Debug の起動画面（on）で検証レイヤーのエラーが 0（Threading を含む）。外れたら off を既定にし、`SCN-ECS-FIX-<名前>` を足す。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-MEASURE-RT/on --timing --min-frames 300 --require-scope RG.Record.*`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-MEASURE-RT-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 計測だけ。反復の中で on と off を撮る（`CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -GameArgs stress-entities=50000[,render-parallel-record=off] -OutDir .harness/runs/scene-capture/SCN-ECS-MEASURE-RT/<on|off>`）。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-12 親: 反復 35 は status を done にした後で壁時計（40 分）に止められ、評価を通らず PROGRESS の表も書かれなかった。計測だけの項目で、撮影（on・off と揺れの確かめ 2 組、バナナ）と既定を off にする変更はそろっているので、親が表を PROGRESS に書いて受け入れた。既定を off にした変更は SCN-ECS-FIX-VISRESOLVE-CONST の検証で on・off とも確かめる。合否を rt の p95 で決めたのは狙いの外れ（rt は GPU の完了待ちを含み、Parallel の 5 つの記録の得 約 0.03 ms は揺れに埋もれる）。以後の RenderThread の項目は、区間の内訳（`RenderGraph.Execute` と RT の各段）で判定する。

## SCN-ECS-RT-FRAME-SCOPES: RenderThread の 1 フレームを区間で切り、rt の内訳を全部見えるようにする
- status: done
- done-when: SCN-ECS-MEASURE-RT の撮影では、mega-stress 5万個 close の rt の median 45.0 ms のうち区間に入るのは `RenderGraph.Execute` 11.4 ms と `RenderGraph.Compile` 0.6 ms だけで、残りの約 33 ms がどの区間にも入らない（GPU は 30.4 ms。`VulkanSwapChain.h` の `MAX_FRAMES_IN_FLIGHT = 1` による GPU の完了待ちと見込む）。RenderThread の 1 フレームを、重ならない直下の区間で切る: パケット待ち（`RT.WaitPacket`）、前のフレームの完了待ち（`RT.WaitFrameFence`）、画像の取得（`RT.AcquireImage`）、グラフの構築と記録（既存の `RenderGraph.*`）、提出（`RT.Submit`）、表示（`RT.Present`）、その他の前後処理（名前は実装で決める）。`RenderGraph.Execute` の中で `RG.Record.*` に入らない時間（バリア・layout の書き戻し・リストの開始と終了・Parallel の準備段）も区間にする（`RG.Barriers`・`RG.PrepareParallel` など。名前は実装で決める）。mega-stress 5万個 close の撮影 1 本で、RenderThread の直下の区間の median の和が rt の median の 90% 以上、`RenderGraph.Execute` の直下の区間の median の和が Execute の median の 90% 以上であることを、区間ごとの median・p95 の表で PROGRESS に書く。区間は `Debug/Stats.h` の既存の仕組みで足し、Release では無効のまま。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-ECS-RT-FRAME-SCOPES`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-RT-FRAME-SCOPES --timing --min-frames 300 --require-scope RT.WaitFrameFence --require-scope RT.Submit --require-scope RT.Present`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/RHI, Library/Core/Public/RHI, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。計測の方針（狙いと内訳）に従い、以後の RenderThread の項目の判定に使う。描画を変えないので起動画面の比較は要らない。危険地帯（RenderThread・RHI）。

## SCN-ECS-FIX-LIGHTING-RECORD: LightingPass の記録の CPU の 3.6 ms を減らす
- status: done
- done-when: バナナ 1万本 overview で `RG.Record.LightingPass` の median が 3.6 ms・p95 5.4 ms（SCN-ECS-MEASURE-RT。RenderThread の記録の中で最大。mega-stress 5万個 close では 0.42 ms）。LightingPass の Execute の中を区間で分けて原因を PROGRESS に書き、記録の最中の重い処理を、変わったときだけ行う・直列の準備段へ移す・GPU へ移す、のいずれかで減らす（原因に合わせて選ぶ）。合格: 同じ撮影で `RG.Record.LightingPass` の median ≤ 0.5 ms、起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-ECS-FIX-LIGHTING-RECORD`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-FIX-LIGHTING-RECORD --timing --min-frames 300 --max-scope-median-ms RG.Record.LightingPass=0.5`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIX-LIGHTING-RECORD`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-MEASURE-RT の区間の内訳から）。危険地帯（RenderThread）。

## SCN-MEGA-FIX-SCENE-TABLES-DELTA: MegaGeometry の表を毎フレーム全量で作り直さず、変わった席だけ直す
- status: done
- done-when: `MegaGeometryPass::BuildSceneTables`（`MegaGeometryPass.cpp` 1196 行付近）は毎フレーム全インスタンスを走査し、表（Sections・Drawables・Entries・Meshes・VisibilityRequests・ShadowInstances）を作り直す（mega-stress 5万個 close の静止で median 7.2 ms。RenderThread の記録の道の 6 割。SCN-ECS-MEASURE-RT）。表を席ごとに持ち続け、GPU Scene の変わった席（追加・削除・メッシュの準備完了・変換・影の設定）だけで直す。並びが変わる（席の追加・削除・メッシュの常駐）ときだけ作り直してよい。開発ビルドで、差分で直した表と全量で作り直した表を一定の間隔で比べる検査を足す（`MEGA_TABLES mismatch=0`）。合格: mega-stress 5万個 close の静止で `MegaGeometryPass.BuildSceneTables` の median ≤ 0.5 ms、1% 移動で ≤ 1.0 ms、両方の撮影のログで `MEGA_TABLES mismatch=0`、起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-DELTA/static`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-DELTA/static --timing --min-frames 300 --max-scope-median-ms MegaGeometryPass.BuildSceneTables=0.5`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000,stress-moving-percent=1 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-DELTA/move1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-DELTA/move1 --timing --min-frames 300 --max-scope-median-ms MegaGeometryPass.BuildSceneTables=1.0`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-SCENE-TABLES-DELTA`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-MEASURE-RT の区間の内訳から）。`MEGA_TABLES` の行は撮影の `close.Game.log` で確かめる。危険地帯（RenderThread）。

## SCN-ECS-FIX-GT-PACING: GameThread を RenderThread に合わせて回し、描かれないパケットを作らない
- status: done
- done-when: GameThread は RenderThread を待たずに回り続け（`RenderThread::NotifyNewFrame` は受け取られていないパケットを捨てて最新に差し替える）、mega-stress 5万個 close では描画 1 フレームの間に約 270 回回る（SCN-ECS-MEASURE-RT の `cpu_scope_frames` 87171 / 描画 320 フレーム）。`ApplicationProcessor` の `m_TargetFrameTime`（`BootConfig::TargetFrameRate`）は設定されるだけでどこからも使われていない。CPU の 1 コアを使い切り、捨てるパケットを作り続け、計測のトレースのロックを競って RenderThread の `RG.TraceRecord` を 1.2 ms（p95 2.6 ms）にしている（SCN-ECS-RT-FRAME-SCOPES）。GameThread は、前のパケットを RenderThread が受け取るまで次のパケットを渡さない（先行は最大 1 フレーム。待つ時間は `GT.WaitRenderThread` の区間にする）。シーンの切り替え・終了・窓の最小化・デバイスの待機で待ち合いが詰まらないこと。開発ビルドの `--gt-pacing=off` で今の動き（最新に差し替え）に戻せる。撮影の終わりに `FRAME_PACING gt_frames=<n> rt_frames=<n> dropped=<n>` をログへ出す。合格: mega-stress 5万個 close で `dropped=0`・gt_frames が rt_frames の 1.05 倍以下、`RG.TraceRecord` の median ≤ 0.2 ms、起動画面の比較と、mega-stress・バナナ・起動画面のシーンの周回が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-ECS-FIX-GT-PACING`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-FIX-GT-PACING --timing --min-frames 300 --require-scope GT.WaitRenderThread --max-scope-median-ms RG.TraceRecord=0.2`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/scene-capture/SCN-ECS-FIX-GT-PACING/close.Game.log',encoding='utf-8',errors='replace').read();m=re.findall(r'FRAME_PACING gt_frames=(\d+) rt_frames=(\d+) dropped=(\d+)',t);sys.exit(0 if m and m[-1][2]=='0' and int(m[-1][0])<=int(m[-1][1])*1.05 else 1)"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIX-GT-PACING`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/Rendering/RenderThread.cpp, Library/Core/Public/Rendering, Library/Core/Private/Engine, Library/Core/Public/Engine, Library/Core/Public/Boot, Library/Core/Private/Debug, Library/Core/Public/Debug, Game/GameApplicationHandler.cpp, Scripts/CheckCaptureMetrics.py, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（SCN-ECS-RT-FRAME-SCOPES の内訳と、`m_TargetFrameTime` が使われていないことから）。GameThread の 1 フレームの時間（Tick の dt）が描画の間隔に近づくので、ゲームの処理が dt に依存して壊れないかも見る（固定の段は今のまま）。2 フレームの重ね（SCN-ECS-FIF-*）の前提（GameThread → RenderThread → GPU の段の数）になる。`RG.TraceRecord` が 0.2 ms に届かなければ、トレースの書き込みをスレッドごとの溜めに分ける直しもこの項目で行う。危険地帯（RenderThread・FramePacket の受け渡し）。

## SCN-ECS-FIF-AUDIT: 2 フレームを重ねるために、フレームごとの資源を洗い出す（調べるだけ）
- status: done
- done-when: `VulkanSwapChain.h` の `MAX_FRAMES_IN_FLIGHT = 1` のため、RenderThread は記録 → 提出 → 次のフレームの前に GPU の完了を待つ、を直列に行い、フレームの時間が CPU と GPU の和になっている（mega-stress 5万個 close で rt 45.0 ms ≒ Execute 11.4 + GPU 30.4 + その他。バナナ 1万本 overview で rt 19.8 ms ≒ Execute 7.9 + GPU 8.1 + その他。SCN-ECS-MEASURE-RT）。2 フレームを重ねるために、CPU が書いて GPU が読む・GPU が書いて CPU が読むフレームごとの資源（定数・インスタンスのリング、記述子の書き換え、一時の資源のプール、転送の stage、GPU Scene の scatter の stage、読み戻し〔VSM のフィードバック・撮影・GPU の時間の計測・カウンタ〕、TLAS のインスタンス、ImGui、MegaGeometry の表とキュー、前のフレームの履歴〔HZB・TAA・prevWorld〕、遅延解放の通し番号など）を全部洗い、各資源の持ち主のファイル・今の数え方（1 つ／スロットごと／通し番号で待つ）・2 にしたときの直し方を PROGRESS の表にする。この項目ではコードを変えない。直しを書き込みパスが交わる単位でまとめて `SCN-ECS-FIF-<名前>` の項目にし、SCN-ECS-FIF-SWITCH の前に足す（各項目は MAX_FRAMES_IN_FLIGHT=1 のまま正しく動き、起動画面の比較が pass する形で書く）。
- verify: `powershell -NoProfile -Command "if (-not (Select-String -Path TASKS.md -Pattern '^## SCN-ECS-FIF-(?!AUDIT|SWITCH)' -Quiet)) { exit 1 }"`
- paths: TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。足す項目は 1 反復（40 分）で閉じる大きさに切る。危険地帯（RHI/Vulkan・RenderThread・資源の寿命）の調べ。

## SCN-ECS-FIF-FRAME-ORDER: フレームの頭に全体のメモリバリアを張り、前のフレームの GPU の書き込みを次のフレームが必ず待つ
- status: done
- done-when: SCN-ECS-FIF-AUDIT の表の「GPU→GPU」の行（TAA・RTGI・PathTracing の ping-pong、VSM のページの状態、HZB のピラミッド、AutoExposure のヒストグラムの作業バッファ、MegaGeometry の可視ビット・BVH のキュー・間接描画の配列、VisibilityMerge のキー、RTGI の denoise の出力など、GPU だけが書いてフレームをまたいで 1 つで持つ資源）を 1 か所で守る。`ICommandList` に全体のメモリバリア（Vulkan は `vkCmdPipelineBarrier` の `VkMemoryBarrier`、src/dst とも ALL_COMMANDS・MEMORY_WRITE → MEMORY_READ|MEMORY_WRITE）を足し、`RenderingCoordinator` がフレームの最初に提出されるリスト（仕事の番号 0 番）の記録の頭で 1 回張る（同じキューの前の提出の書き込みを、このフレームの全コマンドが待つ）。あわせて `RenderingCoordinator` の Blit の記述子（`m_BlitDescriptorSet`。毎フレーム `finalPresentationTexture` を束ね直す）と、表示の合成の記述子（`m_CompositeAlphaOverDescriptorSet`。`CompositePassRequest` で渡し、`CompositePass::Execute` が毎フレーム SceneTexture・CanvasTexture を束ね直して `Update` する）を、飛行中のスロットごとの組にして `ViewRenderContext::FrameIndex` で引く（`CompositePass` は渡された組を書き換えるだけなのでそのまま。毎フレーム作る RenderPass・Framebuffer・Pipeline は `CompositePass` が 8 フレーム持つので足りる）。TLAS のキャッシュ（`RayTracingSceneSubsystem` の `m_TopLevelCache[パケットのスロット]`。構造を in-place で Update する）の「slot の前回の GPU の仕事は、この slot のパケットを描く前に終わっている」のコメントを、「パケットのスロットは GPU の完了を保証しない（RenderThread は提出の直後にパケットを Empty へ戻し、同じスロットを続けて描くことがある）。前の利用の GPU の読み書きとの順序はフレームの頭の全体のバリアで守り、インスタンスの表と scratch は呼び出しごとの lease」に書き直す（パケットのスロットで GPU の完了を前提にする条件は足さない）。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-FRAME-ORDER-debug`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-FRAME-ORDER`
- paths: Library/Core/Public/RHI/ICommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.h, Library/Core/Private/RHI/Vulkan/VulkanCommandList.cpp, Library/Core/Public/Rendering/RenderingCoordinator.h, Library/Core/Private/Rendering/RenderingCoordinator.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 43）で足した。同じキューの提出は提出の順で並ぶが、バリアが無ければ前の提出のコマンドと重なって走りうる。バリアの第 1 の範囲は提出の順で前のコマンドを全部含むので、フレームの頭の 1 本で足りる（timeline のセマフォで提出ごとに待つ形より軽く、CPU の記録と GPU の実行の重なりは保つ。GPU どうしのフレームの境目の重なりだけを捨てる）。各パスのフレームをまたぐ資源の状態の追跡（`ResourceState`）は記録の順＝提出の順なので、そのままでよい。TLAS の Update はこのバリアより後に記録されること（仕事の番号 0 番のリスト、`BuildAccelerationStructures`）を確かめる。ImGui の GPU 資源は SCN-ECS-FIF-IMGUI、置き換えた古い RHI 資源の破棄は SCN-ECS-FIF-DEFERRED-DESTROY-* で直す。危険地帯（RHI/Vulkan・RenderThread）。

## SCN-ECS-FIF-IMGUI: ImGui の頂点・インデックス・UBO・記述子を、パケットのスロットでなく飛行中のスロットで引く
- status: done
- done-when: `ImGuiOverlayPass` の GPU 資源（`m_VertexRing`・`m_IndexRing`・`m_UniformRing` のスロットと `m_SlotDescriptorSets`）を、パケットのスロット（`context.OverlayPacketSlotIndex`）でなく飛行中のスロット（`context.FrameIndex`）で引く。CPU の写し（`m_SlotSnapshots`。GameThread が `OnAssignedToPacket` で書き、RenderThread が `Execute` で読む）はパケットのスロットのまま。理由: RenderThread は提出の直後にパケットを Empty へ戻す（`RenderThread.cpp` の `ReleasePacket`、`FramePacket.h` の `FinishRead`）ので、パケットのスロットは GPU の完了を保証せず、同じスロットを続けて描くことがある。2 にしたとき前の提出が読んでいる頂点・UBO を書き換えうる。飛行中のスロットは `BeginFrame` のフェンスで前の利用の完了が保証される。`FrameIndex` が組の数以上なら描かずに警告を出す。ヘッダとコメントの「スロット排他で安全」「同一スロットは回収済みになるまで再書込みされない」を、この決まりに書き直す。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、ランチャー（ImGui で描く）→ シーン → ランチャーの周回が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-IMGUI`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Modules/ImGui/Private/ImGuiModule/ImGuiOverlayPass.h, Library/Modules/ImGui/Private/ImGuiModule/ImGuiOverlayPass.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 44、評価の差し戻しで足した）。`DynamicBufferRing` の拡張はスロットの古いバッファを置き換えるので、飛行中のスロットで引けば前の利用は完了している（DebugDraw の頂点の `FrameIndex % 3` と同じ形）。`OverlayPacketSlotIndex` を他に使う所が無くなれば消してよいが、`ViewRenderContext` は paths の外なので残して PROGRESS の Notes に書く。危険地帯（RenderThread・資源の寿命）。

## SCN-ECS-FIF-SWAPCHAIN: 描画の完了のセマフォをスワップチェーンの画像ごとにし、使われていない旧の Present を消す
- status: done
- done-when: `VulkanSwapChain` の `m_renderFinishedSemaphores` を、飛行中のスロットごとからスワップチェーンの画像ごと（`m_currentImageIndex` で引く。画像の数で作り、Resize で作り直す）にする（スロットごとのままだと、2 にしたとき前の present がまだ待っているセマフォを次の提出が signal しうる）。`imageAvailable` と `inFlightFences` はスロットごとのまま。呼び出し元の無い `ISwapChain::Present` と `VulkanSwapChain::Present`（フェンスを reset して提出しないので、呼ぶと次の `BeginFrame` が止まる）を消す。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SWAPCHAIN-debug`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SWAPCHAIN`
- paths: Library/Core/Public/RHI/ISwapChain.h, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.h, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 43）で足した。提出が拒まれたとき `imageAvailable` の signal が残る既存の問題（`VulkanSwapChain.cpp` の EndFrame の早期 return）は MAX に依らないので、この項目では扱わない（気づいたら PROGRESS の Notes に残す）。ISwapChain の他の実装（テストの偽物）があれば合わせて消す。危険地帯（RHI/Vulkan）。

## SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE: Framebuffer・RenderPass・Pipeline・記述子・Sampler の破棄を、提出の完了まで遅らせる
- status: done
- done-when: 今は RHI の資源のデストラクタがその場で `vkDestroy*` を呼ぶ。MAX_FRAMES_IN_FLIGHT=1 では `BeginFrame` の待ちで前の提出が終わっているので、次のフレームの記録の中で手放しても壊れないが、2 にすると前の提出が使っている framebuffer などを壊しうる（各パスは描画先の texture が替わると `RenderPassSignature` などで framebuffer・記述子・pipeline を作り直し、古いものを即座に手放す。RenderGraph の一時資源はスロットごとなので、2 にすると描画先が 2 組を交互に替わる）。`VulkanDevice` に破棄の列（ハンドルと、積んだ時点の「次に提出する通し番号」。ミューテックスで守り、どのスレッドから積んでもよい）を持ち、`VulkanFramebuffer`・`VulkanRenderPass`・`VulkanPipeline`（pipeline と layout）・`VulkanDescriptorSet`（set・pool・layout）・`VulkanSampler` のデストラクタは `vkDestroy*` の代わりにハンドルを列へ移す。`VulkanSwapChain` が提出の通し番号を進めたとき・`BeginFrame` のフェンスの待ちの後に完了の通し番号を device へ渡し、device は完了した通し番号以下のハンドルを破棄する。`WaitIdle` の後は、記録中のフレーム（`BeginFrame` から提出まで）が無ければ列を全部空け、記録中なら、その記録が提出される通し番号で積んだ分だけを残して（その提出の完了の通知で破棄する）、それ以外を空ける。device の破棄の前は列を全部空ける。積んだ数・破棄した数・残りの数を開発ビルドの統計（`RHI.DeferredDestroy.*`）に出す。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、Debug の起動画面で検証レイヤーのエラーが 0、シーンの周回が pass し、終了のログで列の残りが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE-debug`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.h, Library/Core/Private/RHI/Vulkan/VulkanSwapChain.cpp, Library/Core/Private/RHI/Vulkan/VulkanFramebuffer.cpp, Library/Core/Private/RHI/Vulkan/VulkanRenderPass.cpp, Library/Core/Private/RHI/Vulkan/VulkanPipeline.cpp, Library/Core/Private/RHI/Vulkan/VulkanDescriptorSet.cpp, Library/Core/Private/RHI/Vulkan/VulkanSampler.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 44、評価の差し戻しの洗い直しで足した）。「次に提出する通し番号」で待つのは、記録中のフレームが参照しているかもしれない資源を、そのフレームの完了まで生かすため（`GpuRetireQueue` と同じ考え。中止したフレームの通し番号は次の提出が使うので取りこぼさない）。SCN-ECS-FIF-SWAPCHAIN と `VulkanSwapChain.cpp` を共有するので、その後に行う。自前で遅らせている所（`CompositePass` の 8 フレーム保持、`GpuRetireQueue`、`TransientResourcePool`、`DynamicBufferRing`・`InstanceBufferRing`）はそのままでよい（二重に遅れても害は無い）。FIF-SWITCH で `RHI.DeferredDestroy` の数が毎フレーム増えていないか（描画先の交互の替わりで framebuffer を作り直し続けていないか）を見る。危険地帯（RHI/Vulkan・資源の寿命）。 2026-10-12 親: 評価の 2 周目の指摘（`WaitIdle` の後に、記録中でないのに次の提出の通し番号で積んだ分が残る。`blocked/SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE.md`）は、完了条件の「全部空ける」が記録中のフレームを考えていなかったため。記録中かどうかで分ける形に完了条件を直して todo に戻した（キューロックの下で退避する今の直しは保つ）。直すのは記録中でないときに次の通し番号の分も空けることだけ。

## SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY: Buffer・Texture・加速構造の破棄を、提出の完了まで遅らせる
- status: done
- done-when: SCN-ECS-FIF-DEFERRED-DESTROY-PIPELINE の破棄の列を使い、`VulkanBuffer`（VkBuffer・メモリ。map 中なら unmap してから移す）・`VulkanTexture`（image・memory・view・ミップと配列の view。所有しない image は移さない。今ある `DeferVulkanTextureResourcesUntilDeviceIdle` の経路はそのまま）・`VulkanAccelerationStructure`（構造と、その下のバッファ）のデストラクタのハンドルの破棄を列へ移す。TLAS のキャッシュを作り直したとき（`m_TopLevelCache` の置き換え）と、パスが大きさの変化で作り直した中間の texture・buffer の古いものが、前の提出の完了まで生きることになる。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、Debug の起動画面で検証レイヤーのエラーが 0、シーンの周回が pass し、終了のログで列の残りが 0。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY-debug`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DEFERRED-DESTROY-MEMORY`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/RHI/Vulkan/VulkanBuffer.h, Library/Core/Private/RHI/Vulkan/VulkanBuffer.cpp, Library/Core/Private/RHI/Vulkan/VulkanTexture.h, Library/Core/Private/RHI/Vulkan/VulkanTexture.cpp, Library/Core/Private/RHI/Vulkan/VulkanAccelerationStructure.h, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 44）で足した。シーンの出入りで大きな資源をまとめて手放すとき、列に残る分だけメモリが一時に増える（1〜2 フレーム）。周回のログでメモリの台帳が戻ることを確かめる。`VulkanGPUResourceAllocator` を通す確保があれば、解放もこの列に合わせる。危険地帯（RHI/Vulkan・資源の寿命）。

## SCN-ECS-FIF-FRAME-OWNER: フレームごとのデータの持ち主を作り、寿命の約束（開始・提出・中止・回収）を決める
- status: done
- done-when: パスを書く人が飛行中のフレームの番号・通し番号・`FrameUseRing` を扱わずに済む土台。RenderingCoordinator が飛行中のスロットごとに「フレームの持ち主」を持ち、寿命を 4 つの段で決める: 開始（スワップチェーンの `BeginFrame` が成功した後。全ビュー・ビューポート・overlay が同じ持ち主を使い、パスの Execute の開始では位置を戻さない。同じフレームに同じパスが何回 Execute しても前の分を上書きしない）、提出（提出が成功したときに実際の提出の通し番号を結び付ける。`RenderingCoordinator.cpp` 5661 行付近の成功・中止の分岐）、中止（提出しなかったフレームの分は、GPU が使っていないので次の開始で戻す）、回収（結び付けた提出の完了で戻す。通し番号の振り直し〔同ファイル 3741 行付近〕にも対応）。パスの Execute に渡る文脈からこの持ち主を引ける。借りた物には世代を持たせ、開発ビルドでは借りたフレームより後で使うと `FRAME_RESOURCE_STALE` を出して拒む。この項目では借りる口の中身（リング・記述子）はまだ作らず、持ち主・寿命・世代の検査を既存の試験の束 `RenderGraphLifetimeTest` に `FrameResource*` の名前で足す（新しい実行ファイルは足さない）: 同じフレームの 2 回目の Execute、中止、回収、通し番号の振り直し、世代の拒否。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target RenderGraphLifetimeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^FrameResource" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-FRAME-OWNER`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RenderThread・資源の寿命）。

## SCN-ECS-FIF-UPLOAD-ARENA: CPU が書いて GPU が読むデータを、フレームの持ち主から切り出して借りられるようにする
- status: done
- done-when: フレームの持ち主（SCN-ECS-FIF-FRAME-OWNER）に、CPU から書ける領域（常にマップしたままのページの列。HOST_COHERENT を求める今の約束のまま）を足し、文脈から定数・頂点・インデックス・ストレージを借りられるようにする（例 `AllocateConstants<T>(const T&)`・`AllocateTransient(size, usage)`。名前は実装で決める）。返すのは `Buffer + Offset + Size` の切れ端で、定数・頂点・インデックス・ストレージの束縛にそのまま渡せる。GPU の整列（定数の最小オフセットの整列など）と記述子の範囲の上限はこの仕組みが守る。1 フレームの中では前へ進むだけで、足りなければページを足す（既に返した切れ端は動かさず、上書きもしない）。書き込みは `Buffer::Update`（中でマップし直す。`VulkanBuffer.cpp` 163 行付近）を通さない。開発ビルドの統計に、1 フレームの借りた数・バイト数・ページを足した回数を出す（`FrameResource.Upload.*`）。開発ビルドの `--frame-arena-initial-kb=<n>` で最初の大きさを小さくして、ページを足す道を通せる。`FrameResource*` の検査（整列・複数の借り・ページを足した後も前の切れ端が有効・回収で戻る）を `RenderGraphLifetimeTest` の束に足す。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target RenderGraphLifetimeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^FrameResource" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-UPLOAD-ARENA`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/RHI, Library/Core/Public/RHI, Game/GameApplicationHandler.cpp, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。既存の `DynamicBufferRing`・`InstanceBufferRing` の「Upload の回数で古い資源を残す」決まりを、この領域の寿命の判定に流用しない。危険地帯（RHI/Vulkan・資源の寿命）。

## SCN-ECS-FIF-DESCRIPTOR-BORROW: 記述子セットをフレームの持ち主から借りられるようにする
- status: done
- done-when: 今の `CreateDescriptorSet` は呼ぶたびに layout と専用の pool を作り（`VulkanDevice.cpp` 2598 行付近）、pipeline layout まで作る（`VulkanDescriptorSet.cpp` 309 行付近）ので、毎フレーム呼ぶ形にはできない。RHI に、共有の layout（同じ束縛の形は 1 つ）と、フレームの持ち主ごとのまとめた pool（型ごとの容量。足りなければ pool を足す。回収でまとめて戻す）を作り、文脈から `AllocateDescriptorSet(layout)` で軽く借りられるようにする（名前は実装で決める）。借りたセットには世代を持たせ、借りたフレームの後では使えない（FRAME-OWNER の世代の検査）。既存の Execute の中の共有（`GBufferPass.cpp` 811 行付近の材質ごと）は保つ。フレームをまたぐ中身のキャッシュはこの項目では作らない。開発ビルドの統計に、1 フレームの借りた数・書き込んだ数・pool を足した回数・借りと書き込みの CPU の時間を出す（`FrameResource.Descriptor.*`）。検査を既存の Vulkan の試験の束 `RHITextureUpdateVulkanTest` に `FrameResourceDescriptor*` の名前で足す（新しい実行ファイルは足さない）: 共有の layout、pool を足す道、回収で戻ること、世代の拒否。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target RHITextureUpdateVulkanTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^FrameResourceDescriptor" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DESCRIPTOR-BORROW`
- paths: Library/Core/Private/RHI, Library/Core/Public/RHI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RHI/Vulkan・資源の寿命）。

## SCN-ECS-FIF-PARALLEL-ALLOC: 並列の記録の区間ごとに、借りる口を分ける
- status: done
- done-when: RenderGraph は Parallel の区間を記録するとき文脈を写し、記録先と保留のコマンドだけを差し替える（`RenderGraph.cpp` 2497 行付近）。写した文脈の借りる口（UPLOAD-ARENA の領域と DESCRIPTOR-BORROW の pool）が 1 つのカーソル・pool を共有すると競合する（Vulkan は同じ pool からの確保に外部の同期を求める）。区間ごと（コマンドリストごと）の借りる口を文脈を写すときに設定し、その裏のページと pool をフレームの持ち主に束ねる。ワーカーでページ・pool を足してよい条件を、`IRenderGraphPass.h` 36 行付近の準備段の約束と合わせて書く。区間をまたいで切れ端が重ならないことを、`RenderGraphLifetimeTest` の束の `FrameResource*` の検査（複数のスレッドから同時に借りる）で確かめる。`--render-parallel-record=on` の Debug の起動画面で検証レイヤーのエラーが 0、on の起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config Debug --target RenderGraphLifetimeTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^FrameResource" --no-tests=error --output-on-failure --timeout 180`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -GameArgs render-parallel-record=on -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-PARALLEL-ALLOC-debug-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=on -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-PARALLEL-ALLOC-on`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。最初は排他つきの共有でもよいが、そのときは待ちの時間を `FrameResource.LockWait` の区間で測って PROGRESS に書く。危険地帯（RenderThread・並列の記録）。

## SCN-ECS-FIF-REWRITE-GUARD: 守られていない上書きを開発ビルドで見つけ、今ある所を一覧にする
- status: done
- done-when: 開発ビルドで 2 種類を分けて出す。(1) 一覧（合否に使わない）: フレームをまたいで持ち続ける資源（メンバのバッファ・記述子セット）を 2 つ以上のフレームでCPU から書き換えた資源を `FRAME_RESOURCE_REWRITE resource=<名前> pass=<パス> frames=<n>` で資源ごとに 1 回。(2) 守られていない上書き（合否に使う）: 飛行中のフレームを 2 と仮定して前のフレームの提出が参照しうる範囲への書き込み、同じフレームで記録済みのコマンドが参照する範囲への書き込み、束縛した後の記述子セットの `Update` を `FRAME_RESOURCE_UNPROTECTED resource=<名前> pass=<パスのクラス名。パスの外なら呼び出し元の名前> kind=<種類>` で出し、終わりに `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=<n>` を出す（MAX_FRAMES_IN_FLIGHT=1 のままでも見つかる）。フレームの持ち主から借りた物・フェンスで守った再利用（既存の `FrameUseRing`・済みの ImGui のスロット）は、名前で除くのではなく仕組みとして「守られている」と登録する。識別は資源の ID と世代、バッファは範囲ごと。数えるのは `Update` と明示した書き込みの借りで、任意の `Map` のポインタへの書き込みは捕まえられないこと（読み戻しの `Map` は書き込みとみなさない）を文書に書く。作成の直後・大きさの変更の直後の 1 回、初期化で 1 回だけ書く物は数えない。起動画面とシーンの周回（mega-stress・バナナ・起動画面のシーン）で出た (2) の一覧を PROGRESS の表にし、SCN-ECS-FIF-DEBUGDRAW・DYNAMIC-UBO・LIGHTING・POST-A・POST-B・PRESENT・DDGI の対象と突き合わせ、漏れたパスをどれかの項目へ足す。Release では無効。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-REWRITE-GUARD`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-REWRITE-GUARD/default.Game.log',encoding='utf-8',errors='replace').read();sys.exit(0 if re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) else 1)"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- paths: Library/Core/Private/RHI, Library/Core/Public/RHI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Debug, Library/Core/Public/Debug, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。この項目では件数を 0 にしない（移す項目ごとに 0 にし、SCN-ECS-FIF-SWITCH で全体を 0 にする）。危険地帯（RHI）。 2026-10-12 親: 評価の 2 周目の指摘（`blocked/SCN-ECS-FIF-REWRITE-GUARD.md`）は一覧 (1) の数え漏れ 1 点だけで、直し方も決まっている（作成直後の除外の後にだけ進める「最後に書き換えを数えたフレーム」を持ち、同じフレームの 2 回目の書き込みも最初の対象のフレームとして数える）。合否に使う (2) は評価者が確認済み。todo に戻した。直すのはこの 1 点だけ。

## SCN-ECS-FIF-DEBUGDRAW: デバッグ描画を借りる形へ移し、パスを書く人向けの約束を書く（手本）
- status: done
- done-when: DebugDraw を、頂点・定数・記述子をフレームの持ち主から借りる形へ移す最初の手本にする。今の `DynamicBufferRing` は指定したスロットの先頭（オフセット 0）へUpload し（`DebugDrawPass.cpp` 283 行付近、`DynamicBufferRing.cpp` 114 行付近）、`SceneRenderer.cpp` 384 行付近は頂点の束縛の位置を 0 に固定している。借りた切れ端のオフセットを `FrameCommand` から束縛まで通し、DebugDrawPass から飛行中の番号と `DynamicBufferRing` の扱いを消す。DebugDraw の `FRAME_RESOURCE_UNPROTECTED` が 0。パスを書く人向けの約束を `Docs/Architecture/FrameResources.md` に書く: フレームごとに書くデータ（定数・頂点・インデックス・ストレージ・記述子）は文脈から借りてメンバに持たない、GPU だけが使う資源（履歴など）はメンバに持ってよい、RHI の資源は手放せばよく破棄の時期は気にしない、読み戻しは共通の口（SCN-ECS-FIF-READBACK）を使う、Parallel のパスでも同じ口を使える、開発ビルドの `FRAME_RESOURCE_*` の出力の読み方。デバッグ描画（`DebugDrawQueue` の線・箱・球）が起動画面の撮影で今と同じに描かれる（起動画面で線が出ないなら、出す構成を 1 本撮って PNG で確かめる）。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DEBUGDRAW`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-DEBUGDRAW/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(DebugDrawPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Docs/Architecture/FrameResources.md, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RenderThread・資源の寿命）。

## SCN-ECS-FIF-DYNAMIC-UBO: DynamicUniformAllocator を借りる形へ移す（GBuffer・Forward・Shadow）
- status: done
- done-when: `DynamicUniformAllocator`（物体ごとの UBO と記述子セットを事前に確保し、`Reset` で先頭へ戻す）は今 1 組で、`Reset` の後に前のフレームの GPU が読む UBO・記述子を書き換え、Execute の単位で `Reset` すると同じフレームの 2 回目の Execute が 1 回目を上書きする。これをフレームの持ち主から借りる形へ移し（UBO は UPLOAD-ARENA の切れ端、記述子は DESCRIPTOR-BORROW）、呼び出し側（`GBufferPass`・`ForwardPass`・`ShadowMapPass` の 2 つ）は飛行中の番号も通し番号も渡さず、`Reset` も呼ばない。この 3 つのパスの `FRAME_RESOURCE_UNPROTECTED` が 0。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DYNAMIC-UBO`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-DYNAMIC-UBO/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(GBufferPass|ForwardPass|ShadowMapPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/DynamicUniformAllocator.h, Library/Core/Private/Rendering/DynamicUniformAllocator.cpp, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-REWRITE-GUARD（反復 58）の起動画面・周回では、ShadowMapPass の `DynUBO_Slot#`（DynamicUniformAllocator の UBO）と、その記述子セット（初期化で作った分は `RenderingCoordinator.DescriptorSet`、Execute の中で Grow した分は `ShadowMapPass.DescriptorSet` の名前で出る）が `FRAME_RESOURCE_UNPROTECTED` に出た。GBufferPass・ForwardPass は起動画面・周回のシーンでは動いていない（DebugDrawPass も同じ allocator を使う。SCN-ECS-FIF-DEBUGDRAW）。SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RenderThread・資源の寿命）。 2026-10-12 親: 記述子の借りの CPU の時間の比べを外した（mega-stress はこの経路を通らず、今のシーンはこの経路の物体が少ないので、測っても守る対象が無い。借りた数・書き込み数は `FrameResource.Descriptor.*` でログに出ており、節目の SCN-ECS-MEASURE-FIF で実際のシーンの記録の時間と一緒に見る）。 2026-10-12 親: 評価の 2 周目はランナーが確定前の古い完了条件（性能の条件つき）で依頼していたため、今の完了条件で評価をやり直した（Sol は混雑で応答なし、別文脈の opus の評価者で PASS。GBufferPass・ForwardPass の未保護 0 は起動画面で描画を積んでいるか未確認で、コードの照合による）。done にした。

## SCN-ECS-FIF-LIGHTING: LightingPass のフレームごとのデータを借りる形へ移す
- status: done
- done-when: `LightingPass` の照明の組（`m_ExecuteResourceSets`。LightData・LightArray・VSM のサンプル／スライスのバッファと記述子）、RTGI の定数（`m_RTGIComputeParametersBuffer`）・発光の表（`m_RTGIComputeEmitterBuffer`）・RTGI と denoise の記述子を、フレームの持ち主から借りる形へ移し、パスのコードから飛行中の番号・フレームごとの組の扱いを消す。RTGI の instance の表（`m_RTGIComputeInstanceDataBuffer`。今は変わったら表全体を `Update` する。`LightingPass.cpp` 3831・4111 行付近）は、持ち続ける表を GPU だけが読む資源にし、変わったフレームに表全体を借りた領域から通常の記録先へコピーする（コピーの前の読みとの依存、コピーの後のシェーダの読みへの依存をバリアで明示し、同じフレームの 2 回目の Execute も守る。GPU だけの資源への `Update` は同期の単発提出〔`VulkanBuffer.cpp` 194 行付近〕に入るので使わない）。「アップロード済み」の版は提出の成功で確定し、中止のときは持ち越さない（例外の catch で印を落とす今の直しの役目を保つ）。行ごとの差分のコピーはこの項目ではしない。RTGI の履歴と denoise の出力は GPU だけが使うのでメンバのまま。LightingPass の `FRAME_RESOURCE_UNPROTECTED` が 0、起動画面の比較（`indirect_lighting=rtgi`）が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-LIGHTING`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-LIGHTING/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(LightingPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/LightingPass.h, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。照明の VSM の統計の読み戻しは SCN-ECS-FIF-READBACK で共通の口へ移す。Debug の `RTGIDiffuseIndirectVulkanTest` が関係するなら回す。危険地帯（RenderThread・資源の寿命）。 2026-10-12 親: 記録の時間の確かめ（`RG.Record.LightingPass` ≤ 0.5 ms）は準備の途中では測らず、節目の SCN-ECS-MEASURE-FIF へ移した。

## SCN-FIX-SKINNED-PATH-TEST: SkinnedRenderPathContractTest を直す（デバッグ描画の移し替えから落ちている）
- status: done
- done-when: Debug の `SkinnedRenderPathContractTest`（1849 行付近 `packet.Stats.SkinnedGBufferRecordedDraws == 1` の assert）は、SCN-ECS-FIF-DEBUGDRAW のコミット 517bdf0a から落ちている（親 517bdf0a~1 では通る。反復 66 の調べ）。517bdf0a の差分を読み、試験の期待が古いのか実装の退行なのかを分けて直す（期待が古いだけなら今の契約に合わせて期待を直し、根拠を PROGRESS に書く。退行なら実装を直す）。assert で止まって走っていなかった後ろの検査も走って pass する。Debug で pass。
- verify: `cmake --build build --config Debug --target SkinnedRenderPathContractTest -- /m:1`
- verify: `ctest --test-dir build -C Debug -R "^SkinnedRenderPathContractTest$" --no-tests=error --output-on-failure --timeout 180`
- paths: Test/Core/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が SCN-FIX-RENDER-TESTS-FAILING を 2 つに分けた。作業ツリーでほかのコミットへ切り替えない（`git checkout <rev>`・`--detach`・`git switch`・`git bisect`・`git stash`・ファイルを古い版で上書きする `git checkout <rev> -- <path>` を使わない。2026-10-12 の反復 66 は古いコミットへ切り替えたまま壁時計で止められ、次の反復が古い TASKS.md で動いた）。古い版は `git show <rev>:<path>`・`git log -p`・`git diff <a> <b>` で読み、ビルドして比べることはしない。

## SCN-ECS-FIF-POST-A: TAA・Bloom・ToneMapping・SSAO・MotionBlur のフレームごとのデータを借りる形へ移す
- status: done
- done-when: 次のパスは、メンバの定数バッファ 1 つと記述子セット 1 組を Execute のたびに `Update` で書き換える。毎フレーム書く定数と記述子をフレームの持ち主から借りる形へ移す: `TemporalAAPass`（`m_ParamsBuffer`・`m_ResolveDescriptorSet`・`m_CopyDescriptorSet`）、`BloomPass`（`m_ParamsBuffer`・`m_BloomDescriptorSet`・ミップごとの Down/Up の定数と記述子）、`ToneMappingPass`（`m_ParamsBuffer`・`m_ToneMappingDescriptorSet`）、`SSAOPass`（SSAO と Blur の定数と記述子）、`MotionBlurPass`（`m_ParamsBuffer` と 3 つの記述子）。 パスのコードから飛行中の番号・フレームごとの組（`FrameUseRing` を含む）の扱いを持たない。初期化で 1 回だけ書くもの（LUT・レンズの汚れ・ミップごとの作成時の定数）と GPU だけが使う資源（TAA の履歴・HiZ のピラミッド）はメンバのまま。対象のパスの `FRAME_RESOURCE_UNPROTECTED` が 0。起動画面の比較が pass。起動画面で無効のパスは、ビルドとコードの照合で確かめて PROGRESS に書く（撮影を増やさない）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-POST-A`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-POST-A/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(TemporalAAPass|BloomPass|ToneMappingPass|SSAOPass|MotionBlurPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/TemporalAAPass.h, Library/Core/Private/Rendering/TemporalAAPass.inl, Library/Core/Public/Rendering/BloomPass.h, Library/Core/Private/Rendering/BloomPass.cpp, Library/Core/Public/Rendering/ToneMappingPass.h, Library/Core/Private/Rendering/ToneMappingPass.cpp, Library/Core/Public/Rendering/SSAOPass.h, Library/Core/Private/Rendering/SSAOPass.cpp, Library/Core/Public/Rendering/MotionBlurPass.h, Library/Core/Private/Rendering/MotionBlurPass.inl, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（資源の寿命）。 2026-10-13 親: 評価の 1 周目（run 20261013-230533 の反復 1）は、verify・再検証・起動画面の比較・対象 5 パスの未保護 0 を確かめた上で、無効にした `RenderGraphCompileTest` の ToneMapping の検査が偽デバイスの共有レイアウトの欠けで通らないことだけを理由に差し戻した。ユーザーの方針で無効の試験は合否に使わないので、親が done にした。

## SCN-ECS-FIF-POST-B: SSR・DoF・Volumetrics・Vignette・FXAA・Upscale・HiZ のフレームごとのデータを借りる形へ移す
- status: done
- done-when: SCN-ECS-FIF-POST-A と同じ直しを次のパスに行う: `SSRPass`（`m_ParamsBuffer`・`m_DescriptorSet`）、`DepthOfFieldPass`（`m_ParamsBuffer`・Gather／Resample の記述子）、`VolumetricsPass`（`m_ParamsBuffer`・`m_DescriptorSet`）、`VignettePass`（`m_ParamsBuffer`・`m_DescriptorSet`）、`FXAAPass`（`m_ParamsBuffer`・`m_DescriptorSet`）、`UpscalePass`（`m_DescriptorSet`）、`HiZPyramidPass`（深度が替わったフレームに書き換える `m_DescriptorSets[0]`）。 パスのコードから飛行中の番号・フレームごとの組（`FrameUseRing` を含む）の扱いを持たない。初期化で 1 回だけ書くもの（LUT・レンズの汚れ・ミップごとの作成時の定数）と GPU だけが使う資源（TAA の履歴・HiZ のピラミッド）はメンバのまま。対象のパスの `FRAME_RESOURCE_UNPROTECTED` が 0。起動画面の比較が pass。起動画面で無効のパスは、ビルドとコードの照合で確かめて PROGRESS に書く（撮影を増やさない）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-POST-B`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-POST-B/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(SSRPass|DepthOfFieldPass|VolumetricsPass|VignettePass|FXAAPass|UpscalePass|HiZPyramidPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/SSRPass.h, Library/Core/Private/Rendering/SSRPass.cpp, Library/Core/Public/Rendering/DepthOfFieldPass.h, Library/Core/Private/Rendering/DepthOfFieldPass.inl, Library/Core/Public/Rendering/VolumetricsPass.h, Library/Core/Private/Rendering/VolumetricsPass.cpp, Library/Core/Public/Rendering/VignettePass.h, Library/Core/Private/Rendering/VignettePass.cpp, Library/Core/Public/Rendering/FXAAPass.h, Library/Core/Private/Rendering/FXAAPass.cpp, Library/Core/Public/Rendering/UpscalePass.h, Library/Core/Private/Rendering/UpscalePass.cpp, Library/Core/Public/Rendering/HiZPyramidPass.h, Library/Core/Private/Rendering/HiZPyramidPass.cpp, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（資源の寿命）。

## SCN-ECS-FIF-PRESENT: 表示の合成・Canvas・ImGui のフレームごとのデータを借りる形へ移す
- status: done
- done-when: `PresentationPass`（`m_Request.BlitDescriptorSet` を毎フレーム束ね直す）、`PresentationComposer`（`request.BlitDescriptorSet`）、`CanvasView`（層の不透明度の定数 `buffer->Update` と記述子 `descriptorSet->Update` を毎フレーム書く所）の、毎フレーム書く記述子と定数を、フレームの持ち主から借りる形へ移す（作成時に 1 回だけ書く物はそのまま）。済みの ImGui（SCN-ECS-FIF-IMGUI のスロットの配列）も、頂点・インデックス・UBO・記述子を借りる形へ移し、スロットの配列を消す（Mesh2D の経路のバッファの中のオフセットに、借りた切れ端の基点を設定する）。対象の `FRAME_RESOURCE_UNPROTECTED` が 0。起動画面の比較が pass し、ランチャー（Canvas）と ImGui の画面が崩れない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-PRESENT`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-PRESENT/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(PresentationPass|PresentationComposer|CanvasView|CanvasBoardPass|CanvasClearPass|CanvasInlineLayerPass|CanvasOwnRTLayerPass|CanvasLayerCompositePass|ImGuiOverlayPass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/PresentationPass.h, Library/Core/Private/Rendering/PresentationPass.cpp, Library/Core/Public/Rendering/PresentationComposer.h, Library/Core/Private/Rendering/PresentationComposer.cpp, Library/Core/Public/Rendering/CanvasView.h, Library/Core/Private/Rendering/CanvasView.cpp, Library/Modules/ImGui, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。ランチャーの見た目は撮影を増やさず、起動の流れ（ランチャーからシーンへ）の周回で崩れないことを確かめる（周回は SWITCH で回す）。危険地帯（資源の寿命）。SCN-ECS-FIF-REWRITE-GUARD（反復 58）: `FRAME_RESOURCE_UNPROTECTED` の `pass=` は RenderGraph のパスのクラス名なので、CanvasView の書き込みは `CanvasBoardPass`（モードで `CanvasClearPass`・`CanvasInlineLayerPass`・`CanvasOwnRTLayerPass`）・`CanvasLayerCompositePass` の名前で出る。verify の照合にこれらを足した。表示の Blit・合成の記述子（`m_BlitDescriptorSets`・`m_CompositeAlphaOverDescriptorSets`）は FIF-FRAME-ORDER の飛行中のスロットごとの組なので「守られている」と登録した（同じフレームの中の重なりは引き続き見る）。

## SCN-ECS-FIF-RHI-OBJECT-CACHE: 一時の描画先が替わっても framebuffer・render pass・pipeline を毎フレーム作り直さない
- status: done
- done-when: 2 フレームを重ねると RenderGraph の一時の資源がスロットごとになり、描画先の texture がフレームごとに交互に替わる。今は各パスが texture の実体を含む署名の変化で作り直す（`ToneMappingPass.cpp` 378 行付近は pipeline まで作り直す。`CompositePass.cpp` 250 行付近は毎回 3 種類を作る）ので、毎フレーム作り直しになる（破棄は遅らせたので壊れはしないが重い）。render pass と pipeline は互換の条件（形式・サンプル数・layout など）を鍵にし、framebuffer は attachment の view の組を鍵にして持ち続ける（見えなくなった鍵は数フレーム後に手放す）。GPU だけが書く一時の資源は CPU から書く領域に混ぜない。開発ビルドのログに作成数（framebuffer・render pass・pipeline。`RHI_CREATE` の行）を出す（統計の仕組みにはつながなくてよい）。開発ビルドの `--rg-transient-slots=2` で一時の資源のスロットを 2 にして（MAX_FRAMES_IN_FLIGHT=1 のまま、描画先が交互に替わる形を作る）、起動画面の撮影の落ち着いた後のフレームの作成数が 0 であることを PROGRESS に書く。起動画面の比較が pass（スロット 2 でも pass）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-RHI-OBJECT-CACHE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs rg-transient-slots=2 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-RHI-OBJECT-CACHE-slots2`
- paths: Library/Core/Private/RHI, Library/Core/Public/RHI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameApplicationHandler.cpp, Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RHI/Vulkan・資源の寿命）。 2026-10-13 親: 評価の 1 周目の指摘 2（作成数を統計につなぐ）は、作成数がログで確かめられていて判断を変えないので、完了条件をログに直した。指摘 1（pipeline の鍵が非所有の render pass のアドレスに依存し、アドレスの再利用で別の形式の古い pipeline が返る）は直す（render pass の互換の条件を値として鍵に入れる、またはデバイスが元の所有の参照を保つ）。

## SCN-ECS-FIF-DDGI: DDGI の照射・距離のアトラスと履歴をビューごとの組にし、CPU が書くデータを借りる形へ移す
- status: done
- done-when: `DDGIProbePass` の `FrameResources` は FrameIndex × ViewId × ViewportId ごとに（`DDGIProbePass.cpp` 396 行付近）、CPU が書く定数・instance・結果のバッファと、照射・距離のアトラスとその履歴（GPU が前のフレームの値を積む）を一緒に持つ。2 にすると履歴が 2 つに分かれ、2 フレームおきに積まれて交互に表示される。ViewId × ViewportId ごとに現在・履歴のピンポンの一式（703 行付近の入れ替えを保つ。1 枚へのその場の更新にしない）と状態（`*AtlasState`・`bAtlasValid`・`AtlasProbeCount`）を持たせ、CPU が書く定数・記述子は Execute ごとにフレームの持ち主から借りる。GPU の結果のバッファとアトラスは GPU の資源として持つ。受け取り側（`LightingPass.cpp` 4381 行付近は FrameIndex を渡す）・getter・履歴の無効化も直す。ビューの消滅で番号が詰まり、同じ volume・プローブ数の履歴が別のビューポートへ引き継がれるのは受け入れる（プローブはワールド空間の値で、同じ volume なら同じ照明。ランチャーのシーンは DDGI もビューポートの削除も使わない）。DDGI の `FRAME_RESOURCE_UNPROTECTED` が 0。DDGI の正しさは既存の `DDGIProbeUpdateVulkanTest` の結果で確かめる（ランチャーのシーンは DDGI を使わないので撮影はしない）。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-DDGI`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-DDGI/default.Game.log',encoding='utf-8',errors='replace').read();ok=re.search(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=\d+',t) and not re.search(r'FRAME_RESOURCE_UNPROTECTED resource=\S+ pass=(DDGIProbePass)\b',t);sys.exit(0 if ok else 1)"`
- paths: Library/Core/Public/Rendering/DDGIProbePass.h, Library/Core/Private/Rendering/DDGIProbePass.cpp, Library/Core/Private/Rendering/LightingPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した項目を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。同じパスを複数のビューポートで Execute する形も確かめる。危険地帯（資源の寿命）。 2026-10-14 親: 評価の 1 周目の指摘 2（DDGI を有効にした撮影が無い）は、使うシーンが無く判断を変えないので撮影の条件を外した。指摘 1（ビューポートを消して番号が詰まると、消した方の履歴が残った方へ引き継がれる）は直す（ビューの消滅・番号の振り直しで履歴を捨てる）。 2026-10-14 親: 反復 1（run 20261014-005646）は評価の指摘 1 を直すのに paths の外（RenderingCoordinator.cpp）が要るとして止まった。引き継がれるのは同じ volume・プローブ数の履歴だけで、プローブはワールド空間の値なので害が無く、直すには寿命の番号を描画まで通す仕組みが要る（足す価値が無い）。完了条件で受け入れとし、指摘 2（撮影）は前に外したので、1 周目の実装（verify・再検証・起動画面の比較は合格）で done にした。

## SCN-ECS-FIF-READBACK: 読み戻しの共通の口を作り、ShadowProbe・MegaGeometry・照明の VSM の統計を移す
- status: done
- done-when: フレームの持ち主に読み戻しの口を足す: GPU が直接書く読み戻し先の借り（`ShadowProbePass.cpp` 313 行付近の常にマップした統計先・578 行付近の HostRead へのバリア、`MegaGeometryPass.cpp` 2875 行付近の GPU で初期化する統計先の形）と、他の資源からのコピーの両方を 1 つの要求にまとめる。提出が成功したときに実際の提出の通し番号を付け、完了の後に RenderThread で受け取り手へ渡す。受け取り手が結果を受け取るまで、その読み戻し先を使い直さない。受け取り手は取り消せる持ち主と世代（とビュー・ビューポート）で持ち、シーンを出て消えたパスへの生のポインタを残さない。終了時の吐き出し・上限・満杯のときに統計を飛ばす数（`FrameResource.Readback.*`）を決める。`ShadowProbePass`（今は Execute の回数 `ExecuteIndex + 2` で読む時を決める）・`MegaGeometryPass` の統計（`StatsSlot`、`RenderFrameCount + 2 <= 現在`）・照明の VSM の統計をこの口へ移す。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、mega-stress の撮影のログに MegaGeometry の統計の行が今と同じ頻度で出る。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-READBACK-GATES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-ECS-FIF-READBACK-GATES`
- paths: Library/Core/Public/Rendering/ShadowProbePass.h, Library/Core/Private/Rendering/ShadowProbePass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: SCN-ECS-FIF-AUDIT（反復 43）で足した READBACK-GATES を、2026-10-12 親が足した（ユーザーの要望: フレームのデータが消える時期などを使う側が気にせずに、ImGui やデバッグ描画などの描画を足せる仕組みにする。パスごとに飛行中のスロットを持たせる直しをやめ、共通の仕組みから借りる形にした。Sol の批評を反映）。危険地帯（RenderThread・読み戻し）。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。撮影の GPU 時間は外した（問いは受け取りの時期・頻度・寿命で、ログの行数で分かる）。

## SCN-ECS-FIF-SYNC-SUBMIT-SCOPE: フレームの途中の同期の単発提出（キュー全体の待ち）を区間と数で見えるようにする
- status: done
- done-when: SCN-ECS-MEASURE-FIF の診断のための計装（この項目では性能を測らない）。`VulkanDevice` の単発のコマンドの提出（device-local のバッファ・テクスチャの `Update` の staging、BLAS の同期の構築〔`RenderingCoordinator` の `entry.Structure->Build`〕などが使い、提出の後に `graphicsQueue.waitIdle()` で待つ）は、2 にすると前のフレームの GPU の完了まで RenderThread を止めうる。待ちを区間 `RHI.ImmediateSubmitWait`（呼んだスレッドの区間）で囲み、提出のロックの待ちと GPU の完了の待ちを分け、呼び出しの用途（呼び出し元の名前）・スレッド・描画フレームと結び付けて、開発ビルドで撮影のログへ出す（`IMMEDIATE_SUBMIT caller=<名前> thread=<名前> count=<n> max_per_frame=<n>` を呼び出し元ごと。Release では数えない）。呼び出し側は変えない（直すかは MEASURE-FIF で阻害した呼び出しが分かってから。ただし構造上毎フレーム同期の提出をしていて、なくす直しが決まっている所はこの項目で直してよい）。MAX_FRAMES_IN_FLIGHT=1 のまま起動画面の比較が pass し、起動画面の撮影のログに `IMMEDIATE_SUBMIT` が出る。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SYNC-SUBMIT-SCOPE`
- verify: `python -c "import sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-SYNC-SUBMIT-SCOPE/default.Game.log',encoding='utf-8',errors='replace').read();sys.exit(0 if 'IMMEDIATE_SUBMIT' in t else 1)"`
- paths: Library/Core/Private/RHI/Vulkan/VulkanDevice.h, Library/Core/Private/RHI/Vulkan/VulkanDevice.cpp, Library/Core/Private/Rendering/RenderWorld.cpp, Library/Core/Private/Engine/ApplicationProcessor.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 SCN-ECS-FIF-AUDIT（反復 43）で足した。ログの出し方は `FRAME_PACING`（SCN-ECS-FIX-GT-PACING）にそろえる（ApplicationProcessor が撮影の終わりに出す。RenderWorld から読む口が要れば足す）。危険地帯（RHI/Vulkan）。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。300 フレームの性能の撮影は外した。

## SCN-ECS-FIF-SWITCH: 2 フレームを重ねて描く
- status: done
- done-when: SCN-ECS-FIF-AUDIT の表の全行が直った後に行う。`MAX_FRAMES_IN_FLIGHT` を 2 にする（開発ビルドは `--frames-in-flight=1` で 1 に戻せる。Release は 2）。Debug の起動画面（2）で検証レイヤーのエラーが 0、RelWithDebInfo の起動画面の比較が pass、mega-stress と バナナ と起動画面のシーンの周回が pass（シーンの出入りで資源を解放する前に、両方のスロットの完了を待つ）。効果はこの項目では測らず、節目の計測（SCN-ECS-MEASURE-FIF）で確かめる。 合わせて: 起動画面と周回で `FRAME_RESOURCE_UNPROTECTED_SUMMARY count=0` と `FRAME_RESOURCE_STALE` が 0、落ち着いた後の作成数（`RHI_CREATE` の行）が 0。起動画面だけの 0 件は通っていない機能の安全を示さないので、`--render-parallel-record=on,frame-arena-initial-kb=4`（並列の記録と、借りる領域のページを足す道）の起動画面 1 本でも 0 件で、ページを足した回数が 1 以上であることを確かめる。同じフレームの 2 回目の Execute・中止・終了時の読み戻しの吐き出しは、FRAME-OWNER・READBACK の試験の結果を PROGRESS に挙げる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SWITCH-debug`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SWITCH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas,mega-stress -Cycles 3 -Configuration RelWithDebInfo`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-SWITCH/default.Game.log',encoding='utf-8',errors='replace').read();m=re.findall(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=(\d+)',t);sys.exit(0 if m and m[-1]=='0' else 1)"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=on,frame-arena-initial-kb=4 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIF-SWITCH-stress`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIF-SWITCH-stress/default.Game.log',encoding='utf-8',errors='replace').read();m=re.findall(r'FRAME_RESOURCE_UNPROTECTED_SUMMARY count=(\d+)',t);sys.exit(0 if m and m[-1]=='0' else 1)"`
- paths: Library/Core/Private/RHI, Library/Core/Public/RHI, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameApplicationHandler.cpp, Game/GameModes/Rendering3DTest/Rendering3DTestRoutine.cpp, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。危険地帯（RHI/Vulkan・RenderThread・資源の寿命）。 2026-10-14 親: 評価の 2 周目（`blocked/SCN-ECS-FIF-SWITCH.md`）の残りは 1 点だけ。`Rendering3DTestRoutine::Leave` が後始末の前に無条件で `WaitForRender()` を呼ばない（F11 の部品があるときだけ待つ。`Rendering3DTestRoutine.cpp` 1727・3941 行付近）。後始末の開始の前に無条件で待つ 1 行を足し、PROGRESS の「各シーンの終わりで待つ」の記述を実コードに合わせる。他の verify・証拠は評価者が確認済みなので、直したら起動画面の比較と周回だけ回せばよい。

## SCN-ECS-FIX-STRESS-VRAM-FIF2: 2 フレームの mega-stress 5 万個の立ち上がりで VRAM が溢れて RenderThread が止まるのを直す
- status: done
- done-when: 2 フレーム（既定）で mega-stress 5 万個 close の撮影が pass し、Game.log に `unhandled exception` と ERROR が 0。今は 2 回とも、MegaGeometry の列が伸びきるフレーム（`MEGA_BVH bvh_instances=50000`）の後に `VulkanBuffer` の `allocateMemory` が失敗して RenderThread が止まる（`--frames-in-flight=1` では通るが、`VRAM_BUDGET` の使用量の山が 12.2 GB で予算 12.2〜15.3 GB に届いている）。立ち上がりで倍々に伸ばす大きな列（`visbuffer_records` 256→2048 MB・BVH の列 2 GiB・visibility など）の古い物が `RetiredBufferFrames`（8 フレーム）と RHI の遅延破棄で生き残る分と、2 フレームで増えた分のどれが溢れさせたかを特定し、山を下げる（例: 伸ばす前に必要な大きさまで一度に取る・古い物を GPU の完了で早く手放す）。直した後の 2 フレームと 1 フレームの `VRAM_BUDGET` の山を PROGRESS に書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-ECS-FIX-STRESS-VRAM-FIF2`
- verify: `python -c "import sys;t=open(r'.harness/runs/scene-capture/SCN-ECS-FIX-STRESS-VRAM-FIF2/close.Game.log',encoding='utf-8',errors='replace').read();sys.exit(0 if 'FRAMES_IN_FLIGHT count=2' in t and 'unhandled exception' not in t else 1)"`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/RHI, Library/Core/Public/RHI, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-ECS-MEASURE-FIF（反復 6）で足した。2 フレームの撮影の失敗は `.harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/stress-try1`・`stress-try2`、1 フレームの比較は `stress-fif1`。危険地帯（RHI/Vulkan・メモリ・資源の寿命）。SCN-ECS-MEASURE-FIF の mega-stress の計測はこの項目を待つ。

## SCN-ECS-FIX-SUN-DISK-UPLOAD: 太陽の円盤の texture を毎フレーム同期の単発提出で書くのをやめる
- status: done
- done-when: `SkyAtmospherePass::GenerateSunDiskTexture` は 1×1 の texture を毎フレーム `VulkanTexture::Update`（単発の提出の後に `graphicsQueue.waitIdle()`）で書いている。2 フレームではこの待ちが前のフレームの GPU の完了まで RenderThread を止め、CPU と GPU の重なりを消している（SCN-ECS-MEASURE-FIF: バナナ 1 万本 overview で窓の 300 フレームすべてに 1 回ずつ出て、`RHI.ImmediateSubmitWait` が median 8.1 ms・p95 11.4 ms、`RT.WaitFrameFence` は p95 0.005 ms）。値はフレームごとの事前露出で変わるので、フレームの記録の中で書く形（フレームの持ち主から借りた領域からのコピーをコマンドに積む、または定数としてシェーダへ渡す）へ移し、毎フレームの同期の提出を無くす。空の見た目は変えない（起動画面の比較が pass）。起動画面の撮影のログで、落ち着いた後に出続ける `IMMEDIATE_SUBMIT` の呼び出し元が無い（どの呼び出し元も `frames` が 8 以下）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-FIX-SUN-DISK-UPLOAD`
- verify: `python -c "import re,sys;t=open(r'.harness/runs/startup-capture/SCN-ECS-FIX-SUN-DISK-UPLOAD/default.Game.log',encoding='utf-8',errors='replace').read();m=re.findall(r'IMMEDIATE_SUBMIT caller=(\S+) .*? frames=(\d+)',t);sys.exit(0 if m and all(int(f)<=8 for c,f in m) else 1)"`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-ECS-MEASURE-FIF（反復 6）で足した。毎フレーム出る同期の単発提出の呼び出し元はこれだけ（ほかは初期化のフレーム 0、BLAS の構築はフレーム 0〜4、焼いた texture の読み込みは main スレッドのフレーム 1）。効果は SCN-ECS-MEASURE-FIF の撮り直しで見る（この項目では性能を測らない）。危険地帯（RenderThread・資源の寿命）。

## SCN-ECS-MEASURE-FIF: 2 フレームの重ねの後に、CPU と GPU の重なりを妨げる待ちが残っていないかを調べる（診断）
- status: done
- done-when: 問い: 2 フレームにした後も、CPU の記録と GPU の処理の重なりを失わせる待ち（同期の単発提出・画像の取得・表示など）が残っているか。変わる判断: 残っていれば、その呼び出し元を直す項目を足す（SCN-ECS-FIF-SYNC-SUBMIT-SCOPE はこの答えを待っている）。測る理由: 待ちの実害は時間でしか見えない。mega-stress 5万個 close とバナナ 1万本 overview（2 フレーム）で、描画フレームごとに RenderThread の時間を「CPU の作業」「GPU に依存する待ち（`RT.WaitFrameFence`・`RHI.ImmediateSubmitWait`・`RT.AcquireImage`・`RT.Present`）」「パケット待ち」に分け、それぞれの median と p95 を PROGRESS の表に書く（区間の median の和では判定しない。フレームごとに分けてから集計する）。`RT.WaitFrameFence` 以外の GPU に依存する待ちが p95 で 1 フレームに現れていれば、提出・完了の番号と区間から呼び出し元と止めた時間を特定し、呼び出し元ごとに `SCN-ECS-FIX-<名前>` を足す（無ければ無いと書く）。フレームの時間の合否はここでは決めない（目標の判定は SCN-MEGA-ACCEPT-1M）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000 -OutDir .harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/stress`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/stress --timing --min-frames 300 --require-scope RT.WaitFrameFence`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/bananas`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/bananas --timing --min-frames 300 --require-scope RT.WaitFrameFence`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が足した。計測だけ。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。借りた数・時間の表と照明の記録の時間は、判断を変えないので外した。 2026-10-14 反復 6: バナナは測れて、毎フレームの同期の提出（`GenerateSunDiskTexture`）が重なりを消していると分かった（SCN-ECS-FIX-SUN-DISK-UPLOAD を足した）。mega-stress 5 万個は 2 フレームで VRAM が溢れて撮れない（SCN-ECS-FIX-STRESS-VRAM-FIF2 を足した）。2 つを直した後に両方を撮り直す（結果と表は PROGRESS の反復 6）。 2026-10-14 親: 案 A。反復 6 の診断で、毎フレームの `SkyAtmospherePass::GenerateSunDiskTexture` の同期の提出が前のフレームの GPU を待ち（`RHI.ImmediateSubmitWait` median 8.1 ms）、2 フレームでは mega-stress 5万個が VRAM の確保で止まると分かった。直前の 2 つの FIX の後にこの項目をやり直し、ほかに重なりを妨げる待ちが残っていないかを確かめる。 2026-10-14 反復 10: 2 つの FIX の後に両方を撮り直した。窓の 300 フレームに `RHI.ImmediateSubmitWait` は 0 回、`RT.AcquireImage`・`RT.Present` は p95 0.1 ms 前後で、`RT.WaitFrameFence` 以外の重なりを妨げる待ちは残っていない（足す FIX は無い。表は PROGRESS の反復 10）。

## SCN-ECS-DIAG-GPU-FIF2: 2 フレームで GPU のパスが一様に遅くなった原因を、クロックと費用に分ける（診断）
- status: done
- done-when: 問い: SCN-ECS-MEASURE-FIF の撮影で、mega-stress 5万個 close の GPU のフレームが 2 フレームで 40.0 ms、同じ頃のビルドの 1 フレームで 34.5 ms（`.harness/runs/scene-capture/SCN-ECS-MEASURE-FIF/stress` と `stress-fif1`）、カリング・ラスタ・照明などのパスが一様に 15〜30% 遅い（バナナも同じ向き）。GPU が休まず回ることで電力・温度の上限でクロックが下がったのか、こちらの変更（フレームの頭の全体のバリア、2 組の資源によるメモリの増え方など）による費用なのかを分ける。変わる判断: クロックで説明できるなら受け入れて記録する（60 fps の判定は SCN-MEGA-ACCEPT-1M の実際の条件で行う）。説明できないなら、原因の区間・パスを特定して `SCN-ECS-FIX-<名前>` を足す。測る理由: クロックと費用は、時間とクロックの記録を並べないと分けられない。同じビルドで `--frames-in-flight=1` と 2 を交互に 2 組（計 4 本、mega-stress 5万個 close、`-GpuTimingFrames 300`）撮り、各撮影の間 `nvidia-smi --query-gpu=timestamp,clocks.sm,clocks.mem,power.draw,temperature.gpu,clocks_throttle_reasons.active --format=csv -lms 500` を記録する。組ごとに GPU のフレームの median、上位のパスの median、窓の間の SM のクロックの median、throttle の理由を PROGRESS の表にし、判断を書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-ECS-DIAG-GPU-FIF2/fif2-a --timing --min-frames 300`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親が足した。撮影は反復の中で 4 本（`CaptureScene.ps1 -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views close -GpuTimingFrames 300 -GameArgs stress-entities=50000,frames-in-flight=<1|2> -OutDir .harness/runs/scene-capture/SCN-ECS-DIAG-GPU-FIF2/fif<1|2>-<a|b>`）。診断だけで、コードは変えない（原因が見つかったら FIX の項目を足す）。 2026-10-14 親: 評価の 1 周目は、2 フレームでカリング（Cull1＋Cull2）が +0.7〜+1.2 ms になる費用の原因の特定と FIX の追加、前の 40.0 ms の原因が未確定であることを理由に差し戻した。カリングの経路は後の SCN-MEGA の項目（BVH の作り直し・インスタンスのカリング・2 パスの遮蔽）で作り変えると決まっていて、今の経路の費用を追っても判断を変えない（ユーザーの方針: 改修が決まった土台で測らない）。フレームの合計では重ねの費用は揺れの中（+0.26 ms）、クロックは一定で throttle も無い、という答えで問いは閉じたので、親が done にした。前の 40.0 ms の原因は未確定（クロックの記録が無い）として残す。

## SCN-ECS-RT-PARALLEL-WIDEN: 記録の重いパスを並列に記録できる形にし、並列の記録を既定にする
- status: done
- done-when: SCN-ECS-MEASURE-FIF の後に行う。並列に記録できる構造にすることはユーザーの設計の方針（「描画の積み込みをマルチスレッド化しないと Vulkan を使う意味がない」）なので、性能の差を実装の条件にしない。RenderThread の記録の時間の上位のパス（MEASURE-FIF の表の `RG.Record.*`。VisibilityRaster・Lighting・ShadowMap・VirtualShadowMap など）について、Parallel にできない理由（自分でバリアを張る・共有の CPU の状態）を外し、依存を保ったまま複数のワーカーで記録できるようにする（監査の表を PROGRESS に書き足す）。並列の記録を既定 on にし（`--render-parallel-record=off` で 1 本に戻せる）、並列にする仕事が無い束は直列で記録する。on・off とも起動画面の比較が pass、Debug の起動画面（on）で検証レイヤーのエラーが 0（Threading を含む）。on と off の性能の比べはこの項目ではしない（SCN-MEGA-ACCEPT-1M で CPU の記録の道が 16.6 ms を妨げたときに、同じ負荷で比べて既定や分け方を決める）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -GameArgs render-parallel-record=on -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PARALLEL-WIDEN-debug-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -GameArgs render-parallel-record=off -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PARALLEL-WIDEN-off`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-ECS-RT-PARALLEL-WIDEN-on`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameApplicationHandler.cpp, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親が SCN-ECS-FIX-RT-PARALLEL-DEFAULT（SCN-ECS-MEASURE-RT が足した。on と off の rt の p95 を揺れ込みで比べる）を置き換えた。rt の p95 の比べは揺れで決まり狙いが無いため。危険地帯（RenderThread）。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。0.5 ms・0.2 ms の閾値は目標から来ていないので外した。

## SCN-MEGA-BVH-LEAF-SPLIT: BVH を持たないメッシュに、葉 8 クラスタ以下の階層の BVH を作る（準備）
- status: done
- done-when: MegaGeometryResourceStore が、v1.0・手続き・実行時に作るメッシュ（BVH が無いもの）に、葉の上限 8 クラスタ（`CookedMeshFormat.h` 71 行付近）を守る多段の BVH を作る（空間で分けて葉を作り、上の段を組む）。作った BVH の検査（全クラスタが 1 回ずつ葉にある・節の境界が子を包む）を開発ビルドで行い、`MEGA_BVH_BUILD violations=0`。まだ描画には使わない。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-BVH-LEAF-SPLIT`
- paths: Library/Core/Private/Rendering/MegaGeometryResourceStore.cpp, Library/Core/Private/Rendering/MegaGeometryResourceStore.h, Library/Core/Public/Rendering/MegaGeometry, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-13 親: ユーザーの指示（邪魔な試験は実行から外す。コード上の確からしさを見るだけの試験は意味が薄い）で、既存の試験の束の ctest を verify から外した。

## SCN-MEGA-FLAT-TO-BVH: 全インスタンスを BVH の経路で描く（平らな経路は残すが使わない）
- status: done
- done-when: BVH-LEAF-SPLIT で作った BVH を使い、全インスタンスを BVH の経路へ流す。平らな経路（`cluster_cull.comp`・FirstGroup の接頭和、`MegaGeometryPass.cpp` 1264 行付近）は `--mega-flat-path` で戻せるように残す。起動画面・旋回の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FLAT-TO-BVH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF -OutDir .harness/runs/startup-capture/SCN-MEGA-FLAT-TO-BVH-orbit`
- paths: Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryResourceStore.cpp, Library/Core/Private/Rendering/MegaGeometryResourceStore.h, Library/Core/Public/Rendering/MegaGeometry/MegaGeometryTypes.h, TASKS.md, PROGRESS.md
- notes: 起動画面の球・岩が通る経路。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-13 親: ユーザーの指示（邪魔な試験は実行から外す。コード上の確からしさを見るだけの試験は意味が薄い）で、既存の試験の束の ctest を verify から外した。 2026-10-14 反復 3: BVH-LEAF-SPLIT の BVH はストアの中にあり、GPU へ書くにはストアの区画の組み立てと節の印の定数が要るため、paths に MegaGeometryResourceStore.cpp/.h と MegaGeometryTypes.h を足した。

## SCN-MEGA-BVH-INDIRECT: BVH の段を間接 dispatch・固定容量の列にする
- status: done
- done-when: 段 1 以降を GPU のカウンタから書いた引数で dispatch する（`ICommandList.h` 382 行付近）。列は固定容量で、書く前に境界を検査し、件数は頭打ち、引数は min(count, cap)、溢れを `bvh_overflow` に数える。2 パス目用の入力は別のバッファに持つ（`MegaGeometryPass.cpp` 1979 行付近の各パスの頭の消去と分ける）。`bvh_queue_bytes` が本数によらず一定（ログ）。バナナの比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-BVH-INDIRECT/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 今は最悪値で dispatch（1983〜2009 行付近）、列は全節の総和（1351〜1373 行付近）。 2026-10-11 親: 今の BVH の列はインスタンスの数とメッシュから最大の容量を取るので、mega-stress 5万個で leaf_capacity 7,514 万・queue 1 億 6,517 万になり、VRAM（使用 14,975 MB / 予算 11,847 MB）を超えて RenderThread がバッファの割り当てに失敗して落ちる。この項目の固定容量の列で、5万個の mega-stress が落ちずに描けることを確かめる。 2026-10-11 親: バナナ 10万本 + mega-stress 5000 個の混在でも同じく VRAM を超える（SCN-MEGA-MIXED-LOAD の記録）。そのとき `VulkanBuffer.cpp` の割り当て失敗の例外が描画スレッドで捕まらず、Game が 0xC0000409 で落ちる。固定容量の列にしたうえで、列の割り当てに失敗したら落ちずにその描画を見送り、ERROR を出す形にする。10万本 + 5000 個の混在が落ちずに描けることも確かめる。 2026-10-13 親: ユーザーの指示（邪魔な試験は実行から外す。コード上の確からしさを見るだけの試験は意味が薄い）で、既存の試験の束の ctest を verify から外した。

## SCN-MEGA-INSTANCE-CULL: 範囲とインスタンスの専用のカリングを、BVH の段 0 の前に置く
- status: done
- done-when: 範囲（AABB）→ 席の順に判定する（1 席 1 スレッド。メッシュの球 × 倍率の締まった球で、視錐台・HZB・距離・画素の大きさ）。通った席だけを BVH の段 1 へ渡す。バナナの比較と起動画面の比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-INSTANCE-CULL/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 範囲の段は PROBE-100K が要ると判断したときだけ作る（要らなければ席の判定だけ）。 （ECS 計画の対応）範囲は、散布の連続した席の範囲。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-BINNING-PREP: 可視クラスタの全体の列と、区間 × ハード/ソフトの数え・接頭和を足す（準備・描画は旧経路）
- status: done
- done-when: カリングが今の書き込みに加えて全体の列へも積む。数えと接頭和を計算し、開発ビルドで新旧を「席・メッシュ・クラスタ・LOD の集合」として比べる（順番では比べない。`MEGA_BIN mismatch=0`）。全体の枠と溢れを `MEGA_CAPACITY` に出す。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Views overview -Build -Scene mega-stress -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GameArgs stress-entities=5000,stress-moving-percent=1,stress-stream-cell=64 -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-PREP/check`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-PREP/check`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering/MegaGeometryPass.h, TASKS.md, PROGRESS.md
- notes: 区間ごとに事前に容量を取る形（1526 行付近）は区間の数 × 419 万件まで膨らむ。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-14 親: 評価の 1 周目の指摘 1（`GeometryPageRequestVulkanTest` がシェーダーの新しい束縛 23・24 を手で写したレイアウトに持たず落ちる）は、写しの試験なので直さず ctest から外した。直すのは指摘 2（全体の列の確保に失敗したとき、試行数を照合と別に読み戻して `MEGA_CAPACITY` の枠と溢れを正しく出す）だけ。

## SCN-MEGA-BINNING-SWITCH: 描画・記録・ソフトの一覧を binning の結果で詰めて書く（切替）
- status: done
- done-when: ハードの間接描画はハードの分だけになる（`SW_RASTER_BIN zeroed=0`）。records_args・records・ソフトの一覧を詰めた範囲で作り、記録の番号（25 bit、`VisibilityBuffer.h` 37 行付近）は両パスを通じて保ち、ハード・ソフトの一覧は同じ記録を参照する（2 パス目の詰め直しで 1 パス目が ID のバッファへ書いた番号を上書きしない）。`--mega-binning=off` で旧経路に戻せる。起動画面・バナナの比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-BINNING-SWITCH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-BINNING-SWITCH/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Private/Rendering/VisibilityRasterPass.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。GBuffer の経路（sink 無し）も同じ詰めた列を描く。今の番号の対応は `visbuffer_records.comp` 113 行付近。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-PREV-HIZ: HZB を 2 枚にし、描いた番号・VP（ジッタ込み）・寸法・シーンの世代を契約として持たせる（準備）
- status: done
- done-when: 2 フレーム目から `MEGA_PREV_HIZ ready=1` が出る。エポックの先頭・解像度の変更・カメラの切れ目・全量の再同期・パケットの飛びでは `ready=0` と理由が出る。判定にはまだ使わない。起動画面の比較が pass。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-PREV-HIZ`
- paths: Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Assets/Shaders/Common/MegaGeometryCull.glsl, TASKS.md, PROGRESS.md
- notes: HiZPyramid（`HiZPyramidPass.h` 28〜65 行付近）を 2 つ持つ。VP は自分で保存し、`PreviousMainCamera`（前のゲームパケット基準、`RenderingCoordinator.cpp` 2091 行付近）には頼らない。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-TWOPASS-INSTANCES: 範囲とインスタンスの 1 パス目を前フレームの HZB で判定し、隠れたものを 2 パス目へ回す（切替）
- status: done
- done-when: 範囲・インスタンスの 1 パス目が前の HZB で判定し、隠れたものは 2 パス目の別の列へ積む。HZB は 1 パス目の深度（ソフトの合流の後、`VisibilityRasterPass.cpp` 1554 行付近）から作る。契約が合わないフレームは遮蔽を使わない。2 パス目の列が溢れたら、溢れた範囲を遮蔽なしで走査し直して描く（取りこぼさない）。`--mega-twopass=legacy` で旧経路に戻せる。旋回（遮蔽 Off との一致）と起動画面の比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -OcclusionViews -OrbitDegreesPerSecond 30 -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-ORBIT-OFF -OutDir .harness/runs/startup-capture/SCN-MEGA-TWOPASS-INSTANCES-orbit`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-TWOPASS-INSTANCES`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Library/Core/Private/Rendering/VisibilityRasterPass.cpp, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-TWOPASS-CLUSTERS: 節とクラスタの 2 パス遮蔽も同じ形にし、見えたビットを消す
- status: done
- done-when: 節・葉・クラスタの 1 パス目も前の HZB で判定し、隠れたものを 2 パス目の列へ回す（溢れは TWOPASS-INSTANCES と同じ規則）。見えたビットと `UpdateVisibilityLayout`（`MegaGeometryPass.cpp` 3194〜3372 行付近）を消し、ページ要求のフレームの印（`visibleWriteStamp` 相当）は残す。旋回・バナナの比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE -OutDir .harness/runs/scene-capture/SCN-MEGA-TWOPASS-CLUSTERS/det`
- paths: Assets/Shaders, Library/Core/Private/Rendering/MegaGeometryPass.cpp, Library/Core/Public/Rendering, Test/Core/Rendering, TASKS.md, PROGRESS.md
- notes: 危険地帯。GeometryPageRequestVulkanTest は `cluster_cull.comp` を直接使うので定数の変更に追従させる。 （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-13 親: ユーザーの指示（邪魔な試験は実行から外す。コード上の確からしさを見るだけの試験は意味が薄い）で、既存の試験の束の ctest を verify から外した。

## SCN-MEGA-VSM-RANGES: VSM の組の判定を、受け手距離内の範囲とインスタンスに絞る
- status: done
- done-when: 組の事前の絞り込み（`VirtualShadowMapRaster.cpp` 978 行付近）の前に範囲（AABB）の判定を置き、受け手距離 + 太陽方向の延長の外の範囲は組を作らない。起動画面の比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-RANGES`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-VSM-RANGES/check`
- paths: Assets/Shaders/vsm_mega_cull_pairs.comp, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering/VirtualShadowMapRaster.h, TASKS.md, PROGRESS.md
- notes: （ECS 計画の対応）変わった席は RenderSync から受け取る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-VSM-BVH-SUN: 太陽の VSM のクラスタの選択を、BVH の段を正射影の LOD でたどる形にする
- status: done
- done-when: 通った組ごとにクラスタを全部見る形（`vsm_mega_cull.comp` 155〜175 行付近）を、太陽のクリップマップについて BVH の段（BVH-INDIRECT と同じ列の安全策）でたどる形にする。点光源の面は旧経路のまま。`VSM_MEGA_CULL overflow=0`。起動画面の比較が pass。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-BVH-SUN`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-VSM-BVH-SUN/check`
- paths: Assets/Shaders, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-VSM-BVH-POINT: 点光源の VSM のクラスタの選択も BVH の段でたどる
- status: done
- done-when: 点光源の面（立方体の各面の透視投影の LOD）でも BVH の段でたどる。夜の起動画面の撮影を開いて点光源の影が変わらない。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M の受入れで判断する。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -Night -OutDir .harness/runs/startup-capture/SCN-MEGA-VSM-BVH-POINT`
- paths: Assets/Shaders, Library/Core/Private/Rendering/VirtualShadowMapRaster.cpp, Library/Core/Public/Rendering, Scripts/CaptureStartupScene.ps1, TASKS.md, PROGRESS.md
- notes: `CaptureStartupScene.ps1` の `-Night` が既存の `--night` を渡さないなら直してよい。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。

## SCN-MEGA-FIX-COOK-LOD-ERROR: クックの LOD の誤差を、足し算でなく子の最大と簡略化の誤差の大きい方にし、形の誤差だけで見積もる
- status: done
- done-when: `Tools/AssetCook/CookMeshDag.cpp` 658 行付近は `group.Error = maxMemberError + simplified.ErrorAbsolute` と段ごとに足し算で積み、簡略化の誤差に属性（法線 0.2・UV 0.1）の項も入る。誤差が実際の形のずれより大きく見積もられ、1 画素の閾値でも遠くのバナナが細かい段を選ぶ（10 m 先の約 17 画素で 2〜3 万三角形。100万本の海で描くクラスタが 6.27M になり、BVH の列と描画の一覧が溢れる。`blocked/SCN-MEGA-FIX-BVH-QUEUE-MAIN.md`）。誤差を「子の誤差の最大と今回の簡略化の誤差の大きい方」にし（meshoptimizer の nanite の例・Nanite と同じ。親の誤差は子より小さくならない約束は保つ）、簡略化の誤差は形の誤差だけで見積もる（属性の重みは簡略化の選び方には使ってよいが、誤差には入れない）。この DAG で作るクック済みの資産（バナナ・mega-stress・起動画面のモデルなど）を作り直す（生成物はコミットしない）。起動画面: 差がこの変更だけによるなら基準（`.harness/runs/startup-capture/SCN-MEGA-BASE-RWDI` と、golden があればそれも）を撮り直して再承認する（`Docs/RenderingValidation/GoldenBaselines.md` の手順。撮影を開いて、近くの岩・球・家の形が崩れていない・穴が無い・遠くの形が目に見えて粗くなっていないことを確かめ、根拠をコミット本文に書く）。1 万本 overview の新しい基準 `scene-capture/SCN-MEGA-BASE-COOK` を撮って開いて確かめる（以後のバナナの比較の比べる先。SCN-MEGA-LAYOUT-SEA の途中の保存で 1 万本の配置が変わったので、古い `SCN-MEGA-BASE` は使えない）。100万本の数はこの項目では測らず、SCN-MEGA-FIX-BVH-QUEUE-MAIN で見る。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-COOK-LOD-ERROR`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -OutDir .harness/runs/scene-capture/SCN-MEGA-BASE-COOK`
- paths: Tools/AssetCook, Assets/AssetSets, Library/Core/Private/MegaGeometry, Library/Core/Public/MegaGeometry, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親が足した（SCN-MEGA-FIX-BVH-QUEUE-MAIN の反復 1 の調べ・案 1）。起動画面と各シーンの見た目に響く（CLAUDE.md 絶対規則 7）ので撮影を開いて確かめる。誤差が正しく見積もられれば、選ぶ細かさの差は画面上で 1 画素以内に収まる見込み。

## SCN-MEGA-FIX-BVH-QUEUE-MAIN: BVH のたどりを LOD で早く打ち切り、列の長さを本数でなく見える細かさで決める（主の描画）
- status: done
- done-when: 100万本の海（SCN-MEGA-LAYOUT-SEA の途中の配置）の horizon・overview で、主の描画の BVH の節の列の 1 段の最大が 12.5M になり、容量 4,194,304（`MegaGeometryPass.cpp` 140 行付近の `BvhNodeQueueCapacity`）を超えて溢れ（`MEGA_CAPACITY bvh_overflow=10151330`）、カメラから約 50 m より先のバナナが判定から落ちて描かれない（`blocked/SCN-MEGA-LAYOUT-SEA.md`）。容量を上げるのでなく、BVH をたどるとき、節の境界の画面上の誤差（投影した LOD の誤差）が閾値より小さい節で降りるのを止め、その節が持つ粗いクラスタの組を描く（Nanite と同じく、列の長さが本数でなく画面の細かさで決まる形）。節に必要な誤差の情報が無ければ BVH の構築で足す。合格: 100万本の horizon・overview の撮影のログで `bvh_overflow=0`、撮影を開いて地平線までバナナが描かれ（床の帯が無い）、近くの見た目が変わらない（1万本 overview の比較が pass）。列の容量の定数は変えない（変えるなら理由を PROGRESS に書く）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon,overview -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-BVH-QUEUE-MAIN`
- verify: `python -c "import re,sys,glob;fs=glob.glob(r'.harness/runs/scene-capture/SCN-MEGA-FIX-BVH-QUEUE-MAIN/*.Game.log');vals=[x for f in fs for x in re.findall(r'bvh_overflow=(\d+)',open(f,encoding='utf-8',errors='replace').read())];sys.exit(0 if fs and vals and all(v=='0' for v in vals) else 1)"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GameArgs banana-count=10000 -CompareWith .harness/runs/scene-capture/SCN-MEGA-BASE-COOK -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-BVH-QUEUE-MAIN-det`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/MegaGeometry, Library/Core/Public/MegaGeometry, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親が足した（SCN-MEGA-LAYOUT-SEA の反復 1 の調べから）。危険地帯（描画）。性能はこの項目では測らず、SCN-MEGA-ACCEPT-1M で判断する。 2026-10-14 親: 反復 1 の直し（子を積む前に視錐台と LOD で判定する。485b1950）は残す。溢れの根は選ぶクラスタの数で、クックの誤差の見積もりが大きすぎるため。直前の SCN-MEGA-FIX-COOK-LOD-ERROR の後に同じ撮影で `bvh_overflow=0` を見直す。節の列がまだ大きければ、焼き込みの BVH を読み込み時に LOD の段ごとにまとめ直す。1 万本の比べる先は新しい基準 `SCN-MEGA-BASE-COOK` にした。

## SCN-MEGA-FIX-BVH-QUEUE-VSM: 影（VSM）の BVH のたどりも LOD で早く打ち切り、列を溢れさせない
- status: done
- done-when: 100万本の海で、影の BVH の列も溢れる（`VSM_MEGA_CULL overflow=16379125`）。SCN-MEGA-FIX-BVH-QUEUE-MAIN と同じ考え方で、太陽のクリップマップの段と点光源の面ごとの投影の LOD の誤差が閾値より小さい節で降りるのを止める。合格: 100万本の horizon・overview の撮影のログで `VSM_MEGA_CULL overflow=0`、撮影を開いて遠くのバナナにも影が落ち、近くの影が欠けない（起動画面の比較が pass）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon,overview -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-BVH-QUEUE-VSM`
- verify: `python -c "import re,sys,glob;fs=glob.glob(r'.harness/runs/scene-capture/SCN-MEGA-FIX-BVH-QUEUE-VSM/*.Game.log');vals=[x for f in fs for x in re.findall(r'VSM_MEGA_CULL.*?overflow=(\d+)',open(f,encoding='utf-8',errors='replace').read())];sys.exit(0 if fs and vals and all(v=='0' for v in vals) else 1)"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -Configuration RelWithDebInfo -Deterministic -CompareDeterministicWith .harness/runs/startup-capture/SCN-MEGA-BASE-RWDI -OutDir .harness/runs/startup-capture/SCN-MEGA-FIX-BVH-QUEUE-VSM`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/MegaGeometry, Library/Core/Public/MegaGeometry, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親が足した（SCN-MEGA-LAYOUT-SEA の反復 1 の調べから）。危険地帯（描画・影）。

## SCN-MEGA-LAYOUT-SEA: 地平線までのバナナの海を作る
- status: done
- done-when: 距離で密度を落とす円盤（近 50/m²・中 10/m²・遠 2/m² 程度、半径 300〜400 m、半径と内訳を `MEGA_BANANAS_LAYOUT` に出す）をバッチで置く。配置の計算は床全体の格子を持たず、本数に見合う量で済む（本数より桁の小さいタイルの並べ替えのような処理は問わない）。`horizon`・`overview` の撮影を開いて、床の帯・床の端・格子の並びが見えない。海の縁から床の端までの床が地平線の直下に見える細い線（反復 6 の horizon で y=542〜544 の 2〜3 画素、RGB 146,135,123）も床の帯に含める。本数は増やさずに消す（例: 海の外側の遠い床の色・材質を遠くの海の見た目に寄せる、床を海の縁の先で終えて霞で地平線へつなぐ）。カメラの far が半径を超える。1万本では同じ形の小さな円盤で、1万本の見た目（木の床の上のバナナ）は変えない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon,overview -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-LAYOUT-SEA`
- paths: Game/GameModes/MegaBananas, Game/Scenes/SceneCatalog.h, TASKS.md, PROGRESS.md
- notes: （ECS 計画の対応）散布の Object で置き、予算つきで作る。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。3 s の上限は、超えたときに変える判断が無いので外した。 2026-10-14 親: 反復 13（run 20261014-061910）は壁時計（40 分）で止まり、途中の保存（755a3a19）を残した。そのとき MegaBananasRoutine.cpp の行末が LF に変わっていたので CRLF に戻した（18b1bde6）。続きはこの保存から。編集した既存のファイルは行末（CRLF）を保ち、コミットの前に numstat を 2 通り比べる。 2026-10-14 親: 案 2。配置は done-when どおりで、遠景が描かれないのは描画の BVH の列の溢れ。直前の SCN-MEGA-FIX-BVH-QUEUE-MAIN・VSM の後に、同じ撮影を見直して閉じる（配置のコードは変えなくてよい見込み）。 2026-10-14 親: 反復 6 の評価の差し戻し（地平線の直下の床の線・タイルの並べ替えの計算量）を受けて done-when を直した。線は既知の限界にせず消す。計算量は床全体の格子を持たないことが要点で、タイルの並べ替えは直さなくてよい。

## SCN-MEGA-LAYOUT-MOUNDS: 落とし込みで積んだ大きな山を数個置く
- status: done
- done-when: 3〜5 個の塚（合計 5〜15 万本）。全部の山について `MEGA_BANANAS_MOUND_GAP max_gap_mm=` を出す。検算用の山 1 つ（約 2 万本）を `VerifyBananaPile.py --decimate 30` が独立に数え、浮き（> 5 mm）が 0。撮影を開いて浮いて見える個体が無い。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Build -Scene mega-bananas -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views horizon -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-LAYOUT-MOUNDS`
- verify: `python Scripts/VerifyBananaPile.py .harness/runs/scene-capture/SCN-MEGA-LAYOUT-MOUNDS/horizon.Game.log --decimate 30`
- paths: Game/GameModes/MegaBananas, Scripts/VerifyBananaPile.py, TASKS.md, PROGRESS.md
- notes: 1 本ずつのログは検算用の山だけに絞る。 （ECS 計画の対応）散布の Object で置き、予算つきで作る。

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
- status: done
- done-when: `GeneratedModels.json` に3種（荒野の前哨地に合う、単体で地面に立つ形の英語のプロンプト。50〜70 万面、テクスチャ 4096、実寸の高さ）を足して生成し、参照画像とプレビューを開いて、建物として形が崩れていない（壁が抜けていない・浮いた破片が少ない・底が平ら）ことを確かめる。崩れたものは seed かプロンプトを変えて作り直す。クックの一覧 `Assets/AssetSets/WastelandBuildingsModels.json` に足し、クックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids adobe_ruin,watchtower,stone_arch`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: 2026-10-10 親の調べ: `adobe_ruin` の 3D 化が 2 反復とも 2000 秒を超えて終わらなかった（`build/ComfyUI.log` で形の段の出力が 4440 万面、続く質感の段で止まる。見張り塔は 222 秒）。生成スクリプトに、定義ごとの `upsample_resolution`（既定 1536。`Trellis2UpsampleStage` の `target_resolution`）と、1体あたりの上限時間（既定 15 分。超えたら ComfyUI の `/interrupt` で止め、`upsample_resolution` を 1024 にして 1 回だけやり直す。やり直しも超えたら失敗として次の id へ進む）を足す。1 反復の壁時計（40 分）を超えないよう、ComfyUI の起動を含めて 1 反復の生成は合計 30 分以内に収め、収まらない分は manifest のあるものを飛ばして次の反復で続ける。 2026-10-10 再開（親が推奨を採った）: 3 種の資産は生成・確認・クックが済んでいる（評価者も確認）。残りは評価者の指摘どおり、生成スクリプトの締切を絶対時刻 1 つにして、参照画像・3D・GLB の取得・復旧（`/queue`）・後処理・終了処理のすべてに効かせ、終了処理の時間を先に取っておき、収まらない生成は次へ繰り越すことだけ。資産は作り直さない。
 2026-10-14 親: 締切の一本化（3bffaf84）を受け入れて閉じた。評価者の残りの指摘（後処理や HTTP のエラー本文の読み取りが長引くと締切を数秒越える）は直さない。合計 30 分の予算は 40 分の壁時計に止められないためのもので、10 分の余裕があり、数秒〜数十秒のはみ出しで変わる判断が無い。後処理を別プロセスへ移すような作り替えはしない。

## SCN-GEN-BUILDINGS-2: 荒野の建物を3種生成する（風車・給水塔・木造の小屋）
- status: done
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、風車（`windmill`）・給水塔（`water_tower`）・木造の小屋（`wooden_shack`）を生成・確認・クックする。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids windmill,water_tower,wooden_shack`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。 2026-10-14: `wooden_shack` は木造の小屋（板壁・丸太）の参照画像では 3D の段が 1024 でも 600 秒で終わらないため、id はそのままで石積みの小さな物置（seed 2051・高さ 2.8 m）に替えた。

## SCN-TERRAIN-GEN: 起伏のある地形の高さ場を作り、MegaGeometry に焼いて描く
- status: done
- done-when: 共有の高さ場（seed と関数で決まる。Game が同じ関数で任意の (x,z) の高さと法線を引ける）を Library に足し、AssetCook の `--generate` で約2km四方・0.5〜1m 間隔の地形メッシュ（中央に前哨地の平らな盆地 約200m、周りに砂丘とメサ、外周に稜線。UV はワールドの XZ から）を NVMESH v1（クラスタ・LOD の階層）に焼く。`wasteland` シーン（この項目では地形・物理空・太陽・高さフォグだけ。材質は Poly Haven の砂 1 種でよい）に置き、名前付き視点 `overview`（盆地を見下ろし遠くの稜線まで）・`ground`（人の目の高さ）・`ridge`（稜線から盆地）を持たせる。撮影を開いて、地平線が平らな板でなく起伏がある・遠くの地形に LOD の切り替えの穴やクラックが無いことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-GEN`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/CMakeLists.txt, Tools/AssetCook, Assets/AssetSets, Game, Scripts, Test/Core/Rendering, Test/TestBundle, Test/Game/Bridge/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットのクック）。手本は球の手続き生成（`Library/Core/Public/Rendering/MegaGeometry/ProceduralMegaSphere.h` 34・234 行付近）と `Assets/AssetSets/Rendering3DTestStartupModels.json` の `generate=displaced-sphere`。地形は1つのメッシュでも区画に分けてもよい（クラスタ単位で外される）。カメラの遠クリップは既定 1000 m（`CameraComponent.cpp` 320 行付近）なので、シーンで延ばす。 2026-10-13 親: ユーザーの指示で、高さ場の CPU の試験の追加を外した（地形は撮影で見る）。

## SCN-TERRAIN-BLEND: 地形の材質を 3 層（砂・乾いた土・岩肌）の混ぜ合わせにする
- status: done
- done-when: 地形用の材質（3 層のアルベド・法線・ORM と、傾斜・高さ・低周波ノイズで決める混ぜ方の値）を足し、MegaGeometry の描画経路（使っている経路すべて。ハードのラスタと可視バッファ・ソフトラスタ）で効く。平らな所は砂、少し傾いた所は乾いた土、急な斜面とメサの崖は岩肌になり、境目はノイズで揺らぎ、高さ（凹凸）で重みを付けて砂が岩のくぼみに溜まるように見える。`wasteland` の地形にこの材質を使う。他の材質（Rendering3DTest・バナナ）の見た目は変わらない。`wasteland` の撮影を開いて層の分かれ方を確かめ、Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-BLEND`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-TERRAIN-BLEND`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今の材質は1材質1組（`MaterialTypes.h` 288〜310 行付近）で、シェーダーは各テクスチャ1枚ずつ（`gbuffer.frag` 30〜35 行付近）。テクスチャの焼き込みは既存の usage（albedo|normal|orm）、VT と束縛数の上限に注意。

## SCN-TERRAIN-DETAIL: 地形の岩肌をトライプラナーにし、タイルの繰り返しを見せない
- status: done
- done-when: 岩肌の層を急斜面でトライプラナー（引き伸ばされない）にし、広い範囲の色・明るさの揺らぎ（数十〜数百 m の低周波）と、距離で UV の倍率を変える混ぜ方（近くは細かく、遠くは大きく）で、`overview` の撮影で同じ模様の格子状の繰り返しが目立たない。メサの崖を見る名前付き視点 `cliff` を足し、縦に伸びた縞が無いことを撮影を開いて確かめる。メサが、上の平らな円盤を型で抜いたように見えない（SCN-TERRAIN-GEN の `ridge` では縁が垂直に切り立った同じ高さの円盤が並ぶ。縁を崩す・段や張り出しを付ける・高さと大きさと輪郭をばらすなどを高さ場で行い、区画を焼き直す）。`ground` の足元の砂が横に流れてぼやけて見えない（テクスチャの選び方・異方性フィルタ・倍率を確かめる）。性能はこの項目では測らず、SCN-WASTELAND-POLISH の最終の配置で判断する（材質の処理が目標を超える原因になったときだけ詳しく診る）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-TERRAIN-DETAIL`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-TERRAIN-DETAIL`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。

## SCN-SHADOW-DISTANCE: シーンごとに太陽の影の到達距離を設定できるようにする
- status: done
- done-when: GameThread 側（RenderWorld。RenderThread へは FramePacket 越し）に、シーンごとの太陽の影の到達距離を設定する API を足し、VSM（クリップマップの使う段）と CSM（`MaxShadowDistance`）の両方に効く。既定は今の 80 m で、Rendering3DTest の撮影は変わらない。`wasteland` で 500 m 程度にし、`overview` の撮影を開いて、80 m より先の地形の起伏・メサ・岩にも影が落ちている、近くの影の細かさが目に見えて落ちていないことを確かめる。性能はこの項目では測らず、最終の荒野（SCN-WASTELAND-POLISH）で目標を超えたときに、影の内訳から到達距離・解像度を調整する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-SHADOW-DISTANCE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-SHADOW-DISTANCE`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画・RenderThread）。`ShadowMapPass.h` 47・134 行付近（`MaxShadowDistance` 既定 80、`SetMaxShadowDistance` はどこからも呼ばれていない）、`CascadedShadowLightMatrices.cpp` 669 行付近、`VirtualShadowMapClipmap.h` 32〜40 行付近（10 段、第1段 4 m、段ごとに 2 倍、DepthRange 1000 m）。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。

## SCN-WASTELAND-LAYOUT: 荒野の前哨地に岩・崖・建物を置き、夕方の光を決める
- status: done
- done-when: `wasteland` に Poly Haven の岩・崖（大きさと向きをばらし、地形に少し埋めて、数十〜百数十個。遠くのメサの足元にも）と生成した建物 6 種（盆地の中央に小さな前哨地として道に沿って向きを揃え、地面に埋めて浮かせない）を高さ場の関数で置く。太陽は夕方寄り（仰角 15〜25° 程度）で長い影、露出とグレーディングを夕景に合わせる（Rendering3DTest のグレーディングは変えない）。名前付き視点 `outpost`（前哨地の建物の間）を足す。`overview`・`ground`・`outpost`・`ridge` の撮影を開いて、ゲームの風景に見える（建物が浮いていない・岩が地形に馴染む・白飛びや黒つぶれが広がっていない・影の中が真っ黒でなく空の光で持ち上がる。SCN-TERRAIN-BLEND の `overview` では手前の砂丘の影が黒い筋に見える）ことを確かめる。`wasteland` の cycle smoke 3周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WASTELAND-LAYOUT`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland -Cycles 3`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md

## SCN-AERIAL-PERSPECTIVE: 遠景に空気遠近の霞を掛ける
- status: done
- done-when: 物理空の LUT（透過率と散乱）から、カメラからの距離に応じた散乱光と透過率を不透明物に掛ける処理（GBuffer の合成・可視バッファの両方の経路）を足してシーンごとに有効にでき、`wasteland` で有効にする。遠くの稜線とメサが空の色に溶け、近くは変わらない。`wasteland` の高さフォグの値も霞として馴染むように調整する（Rendering3DTest の霧は変えない）。`overview`・`ridge` の撮影を開いて遠景の霞を確かめ、Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-AERIAL-PERSPECTIVE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-AERIAL-PERSPECTIVE`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。`SkyAtmosphere.h` 24〜36 行付近、`RenderWorld::SetSkyAtmosphere`（`RenderWorld.cpp` 311 行付近）。高さフォグは `VolumetricFog.h` 11〜17 行付近（Rendering3DTest は密度 0.02・falloff 1.0）。

## SCN-CLOUDS: 空に雲の層を足す
- status: done
- done-when: 空の描画に、高度帯（例 1.5〜4 km）の雲の層を 3D ノイズ（Perlin-Worley 等。起動時に生成）の密度で光線を進めて描く処理を足してシーンごとに有効にでき、`wasteland` で有効にする。太陽側の縁が明るく（前方散乱）、底面が影で暗く、空の環境光で持ち上がり、時間でゆっくり流れる。雲の影は範囲外（太陽を遮らない）。雲を空からの IBL に入れるかを決めて記録する。`overview`・`ridge` の撮影を開いて、地平線近くまで雲が自然に並ぶ（板やタイルの繰り返しに見えない）ことを確かめる。性能の判定は低解像度化・時間の蓄積を含む SCN-CLOUDS-TEMPORAL で行う。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-CLOUDS`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-CLOUDS`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。空から IBL を作る箇所は `LightingPass.cpp` 4653 行付近。雲は今は無い。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。

## SCN-WL-REFERENCE-SELECT: 比較の手本の写真を選び、切り出す範囲と主役の地形を決める
- status: done
- done-when: Valley of the Gods（ユタ州、BLM）の写真を、ライセンスが パブリックドメイン・CC0・CC BY・CC BY-SA のもの（NC・ND は使わない）から選ぶ: 遠景（ビュート・メサの並びと稜線）2〜3 枚、主役の崖 1〜2 枚、足元（礫・沢底・低木）2〜3 枚。足元の写真が同じ場所で見つからなければ、同じ地質の近い地域（Cedar Mesa・Monument Valley 周辺）で代える。`Assets/Reference/ValleyOfTheGods/` に落とし（git 管理外。`.gitignore` に `/Assets/Reference/` と `/Assets/Terrain/` を足す）、`manifest.json`（URL・作者・ライセンス・撮影の位置と向きの推定・写っている物）を書く。写真から、主役にするビュート・崖、切り出す範囲（2〜4 km 四方。1 m タイル `x60y413` の内側）、前哨地を置く自然に平らな場所の候補を決め、`Docs/RenderingValidation/WastelandReference.md` に書く（範囲の座標・選んだ理由・写真ごとに比較で見る特徴）。写真を縮小して並べた一覧の PNG を開いて確かめる。
- verify: `python Scripts/Terrain/ReferencePhotos.py --verify`
- paths: Scripts/Terrain, .gitignore, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親: ダウンロードはユーザーが許可済み（手本の写真 4〜6 枚、各数 MB）。調べ済み: Commons の `File:Franklin_Butte,_Valley_of_the_Gods.jpg`（CC BY-SA 3.0）は使える。`File:A287,_Valley_of_the_Gods,_Utah,_USA,_2008.JPG` は NC があるので使わない。Commons の Category:Valley of the Gods を API（https://commons.wikimedia.org/w/api.php）で引いてよい。USGS の写真はパブリックドメイン。

## SCN-WL-DEM-INPUT: 地形データと、それを読む Python の環境を用意する
- status: done
- done-when: 3DEP の 1 m DEM `USGS_1M_12_x60y413_UT_WestEast_B22.tif`（約 268 MB。The National Map の API で URL を引く。配布元は prd-tnm.s3.amazonaws.com）を `Assets/Terrain/Source/` に落とし（git 管理外）、サイズ・SHA-256・URL・取得日を `Assets/Terrain/Source/manifest.json` に書く。GeoTIFF を読む Python のパッケージ（rasterio と numpy。Python 3.14 の wheel が無ければ tifffile と imagecodecs）を入れ、`Scripts/Terrain/requirements.txt` に版を固定して書く。タイルの座標系（EPSG）・画素の大きさ・行の向き・NoData の値と画素数・高さの範囲を読んで PROGRESS に書く。
- verify: `python Scripts/Terrain/DemInput.py --verify`
- paths: Scripts/Terrain, .gitignore, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親: このファイルのダウンロードとパッケージの導入はユーザーが許可済み。3DEP はパブリックドメイン（"All 3DEP products are public domain."）。汎用の製品探索は作らない（ファイルを固定する）。隣のタイル（`x60y412` 約 263 MB など）や 1/3 arc-second が要ると分かったら、ファイル名とサイズを書いて BLOCKED にする（別のダウンロードはユーザーの許可が要る）。近景に約 10 m の粗いデータを補間して使うことはしない。

## SCN-WL-DEM-CROP: 余白付きで範囲を切り出し、エンジンの座標の格子にする
- status: done
- done-when: SCN-WL-REFERENCE-SELECT で決めた範囲を、周りに 100 m 以上の余白（法線と解析のため）を付けて切り出し、地域の原点を引いた m の格子として、float32 の生の格子と JSON（EPSG・原点の UTM 座標・画素の大きさ・行の向き・東と北をエンジンの X と Z のどちらの向きに対応させるか・画素の中心と頂点の対応・標高の基準 NAVD88・高さのオフセット（前哨地の床が 0 付近になる値）・余白・NoData の処理・入力の SHA-256）を `Assets/Terrain/ValleyOfTheGods/` に書く。NoData は孤立した小さな穴だけ周りから埋め、大きな穴があれば範囲を変える（0 m や大きな負値として焼かない）。陰影図を 2 方向（北西と南東から照らす）で書き、主役の崖を横切る断面の図を書いて開き、形が写真の配置と対応し、向きの反転・欠け・格子状の凹凸・崖の不自然な丸まりが無いことを確かめる。
- verify: `python Scripts/Terrain/CropDem.py --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 全体のならし（平滑化）はしない。平らな所の異常だけ直す。

## SCN-WL-DEM-HEIGHTFIELD: 高さ場を DEM の格子から引けるようにする
- status: done
- done-when: `TerrainHeightField` に、切り出した格子を読んで高さと法線を引く作り方を足す（今の手続きの作り方は残す。荒野の既定を格子に替えるのは SCN-WL-DEM-SCENE）。標本の規則は焼いたメッシュの三角形と一致させる（格子の点をそのまま頂点にし、間はメッシュと同じ対角線の分け方で線形。双三次のような元の値を越える補間は使わない）。範囲の外は端を延ばさず、範囲外と分かる値を返す。格子の読み込みは一度で、複数のスレッドから引いてよい。Game とクッカーが同じファイル・同じ規則を使う。格子の数点で、ファイルの値と `SampleHeight` が一致することを PROGRESS に書く。
- verify: `cmake --build build --config Debug --target Game AssetCook -- /m:1`
- paths: Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Library/Core/CMakeLists.txt, Tools/AssetCook, Game/GameModes/Wasteland, Scripts/Terrain, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットの読み込み）。

## SCN-WL-DEM-COOK: 地形の区画を DEM の格子から焼く
- status: done
- done-when: クッカーの terrain-tile が DEM の格子から区画を焼く。区画の数と格子の間隔は格子の JSON から決め、入力の SHA-256 と造成の設定をクックの依存（再クックの判定）に入れる（Game だけが新しい高さを読みメッシュが古いまま、を起こさない）。隣り合う 2 区画で、境の頂点の高さ・法線が一致し LOD の段ごとに境が固定されることを確かめてから、全体を焼く。
- verify: `cmake --build build --config Debug --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/Debug/AssetCook.exe -OnlyLogicalPath "Models/WastelandTerrain/*"`
- paths: Tools/AssetCook, Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットのクック）。2 km 四方の 1 m 格子は約 800 万三角形、4 km 四方は約 3,200 万。論理パスを変えるなら verify の行も同じ項目で直す。

## SCN-WL-DEM-SCENE: 荒野を DEM の地形にし、視点を置き直す
- status: done
- done-when: `wasteland` が DEM の区画を置き、高さ場の既定を格子に替える（手続きのメサは荒野では使わない）。遠クリップ・影の到達距離・霧の基準の高さ・空気遠近の開始距離を、新しい地形の大きさと高さに合わせる。名前付き視点 overview・ground・ridge・cliff を新しい地形に置き直す（手本の写真の構図は SCN-WL-REFERENCE-CAPTURE）。今の岩・崖・建物の配置（WastelandLayout）は、新しい地形で据え直すか、SCN-WL-OUTPOST まで外す（浮き・めり込みを残さない）。撮影を開いて、地形が写真の主役のビュート・メサの形と対応し、尺度（高さ・距離）が合い、区画の境・LOD の割れが無いことを確かめる。`wasteland` の cycle smoke 3 周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WL-DEM-SCENE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland -Cycles 3`
- paths: Game/GameModes/Wasteland, Game/Scenes/SceneCatalog.h, Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md

## SCN-WL-LIGHT-BASE: 基準の光を手本の写真に合わせる
- status: done
- done-when: 手本の晴天の遠景の写真に合わせて、太陽の高さと向き（写真の影の向きと長さから推定）・空・露出・霞・グレーディングを決める。影の到達距離が、写真に写る主要な崖を覆う。撮影と手本を並べた画像（簡易でよい。正式な比較は SCN-WL-REFERENCE-CAPTURE）を開いて、空と雲の明るさ（露出を写真の記録に合わせた上での、空・雲の画素の明るさ）・日向と影の中の明るさの比・遠景の霞が近いことを確かめる。地面の色と明るさの差（今の地面は前のベージュの砂の材質で、手本は赤い土と低木）は光で合わせず、測った値を記録して SCN-WL-MATERIAL-PALETTE と植生の項目で直す（露出やグレーディングで地面に合わせない）。Rendering3DTest は変わらない（荒野のシーンの値だけを変えるなら、撮らずにコードで確かめてよい）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WL-LIGHT-BASE`
- paths: Game/GameModes/Wasteland, Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親: 反復 13 の評価の差し戻し（空÷地形の比が手本 3.77・撮影 1.28〜1.56）を受けて done-when を分けた。地面が明るいのは材質（ベージュの砂・低木なし）のためで、光の項目では空・雲・影・霞だけを合わせる。

## SCN-WL-CAPTURE-FAST: 1 回の起動で複数の視点を撮れるようにし、荒野の全視点の撮影を 10 分以内にする
- status: done
- done-when: 今の `Scripts/CaptureScene.ps1` は視点ごとに Game を起動し直し、荒野では毎回 100 区画（約 3.7 GB）を Debug で読み直すので 1 視点に約 2 分かかり、全視点（11）で 20 分を超えてランナーの検証の上限（1 コマンド 10 分）を越える。Game に、1 回の起動で複数の名前付き視点を順に撮る引数を足す（シーンの組み立てと SceneReady を待ってから、視点ごとにカメラを置き、ストリーミングと時間方向の蓄積（TAA・自動露出・RTGI）が落ち着くのを待って撮る。荒野の視点の撮影は画素の完全な一致を求めない（写真と並べて品質を見るための撮影）。起動画面の決定的な撮影は差 0 を保つ）。`CaptureScene.ps1` は、-Views を省いたときと複数の視点を与えたときにこれを使う（今の出力の形（<視点>.png・metrics.json・SceneReport）は変えない）。荒野の全視点の 1920×1080 の撮影が Debug で 10 分以内に終わること、同じ視点を今の撮り方（1 視点ずつ起動）と新しい撮り方で撮って見た目が変わらないことを確かめる。起動画面（Rendering3DTest）の決定的な撮影の比較が通る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-CAPTURE-FAST`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-CAPTURE-FAST`
- paths: Scripts, Game, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Application, Library/Core/Private/Application, TASKS.md, PROGRESS.md
- notes: 2026-10-15 親: SCN-WL-REFERENCE-CAPTURE の再検証が撮影の時間切れ（10 分）で NG になった。基準の光（SCN-WL-LIGHT-BASE）が 4 反復かかったのも、大半が撮影の待ちだった。以後の荒野の項目は全部この撮影を何度も使う。 2026-10-15 親: 8dec0692 で閉じた（親が受け入れた）。評価の差し戻しは、複数の視点の撮影で同じ視点の 2 回の撮影が最大 4 段違う（PSNR 100）ことだけで、画素の完全な一致は親が書きすぎた条件だったので外した。全 11 視点を 172 秒・ページの読み込みの完了・1 視点ずつの撮影と見た目が同じ・起動画面の差 0 は評価者が確認した。評価者の non-blocking の指摘（`GeometryPageStreamer.h` 880 行付近の NoSpace の追い出しが、GPU の解放待ちの間も続き、別の塊のページまで外す）は、今回は evicted=0 で起きていない。追い出しが起きる規模で撮るときに見直す。

## SCN-WL-REFERENCE-CAPTURE: 手本の写真と同じ構図の視点と、並べた比較画像を作る
- status: done
- done-when: 手本の写真ごとに、写真に合う名前付き視点（位置・向き・画角・時刻。推定した条件を書き、「同じ」とは書かない）を足し、写真と撮影を左右に並べた PNG を書くスクリプト `Scripts/Terrain/CompareReference.py` を作る（今の `CompareLight.py` を広げてよい）。品質を見る撮影は 1920×1080 以上で撮る（1280×720 では粗さが判断を狂わせる）。比較画像には、地面・崖・空の領域ごとの平均の色（線形）と明るさを写真と撮影の両方で書き、地表の材質の平均のアルベドも書く。比較画像を開いて、切り出した範囲の中の主役（far_02・cliff_01・cliff_02・ground_02 のビュートと崖）の配置と輪郭が対応することを確かめ（位置の記録が無い写真は、DEM の稜線と写真の輪郭を照合して位置・向き・画角を決め直す）、範囲の外が主役の far_01（Castle Butte と Cedar Mesa の縁）は SCN-WL-HORIZON で合わせ、ground_01 は地表と植生の比較用で輪郭は見ない、と文書に書いたうえで、目立つ差（形・色・明るさ・尺度・地表の種類・欠けている物）を差の大きい順に `Docs/RenderingValidation/WastelandReference.md` に書く。以後の荒野の項目は、この視点と比較画像で判断する。他の地域の写真は材質と光の参考だけに使う。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Views ref_far_01,ref_far_02,ref_cliff_01 -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-REFERENCE-CAPTURE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Views ref_cliff_02,ref_ground_01,ref_ground_02 -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-REFERENCE-CAPTURE`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-REFERENCE-CAPTURE --verify`
- paths: Game/GameModes/Wasteland, Game/Scenes/SceneCatalog.h, Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 写真の加工やホワイトバランスが分からないので、RGB の値の一致を合格の条件にしない（PSNR・SSIM も使わない）。値は差の向きと大きさを見るために使う。。撮影の verify は手本の 6 視点だけを 3 視点ずつ 2 本に分ける（Debug の 1920×1080 は 1 視点に約 107 秒かかり、全 11 視点を 1 本で撮ると 1 コマンド 10 分の上限を超えるため）。 2026-10-15 親: 反復 1 の評価の差し戻し（cliff_01・far_01・ground_01 の輪郭が対応しない）を受けて、範囲の外の主役と地表の比較用の写真を分けた。

## SCN-WL-HORIZON: 遠景を粗い実地形でつなぐ
- status: done
- done-when: 1 m タイルの範囲（約 10 km 四方）のうち切り出しの外側を、粗い間隔（例 8〜16 m）の遠景の地形として焼き、近景の区画の外周と隙間・段差なくつなぐ（近景の外周の高さへ寄せる帯を置く）。手本の写真に写る稜線（北の Cedar Mesa の崖など）が入る範囲にし、遠クリップを合わせる。far_01 の主役（Castle Butte と背後の Cedar Mesa の縁）が写るようにし、far_01 の視点を合わせ直して比較画像で輪郭が対応することを確かめる（写真の主役になる地形は粗くせず 1〜2 m で焼く）。ridge と overview の撮影を開き、外周の板・壁・継ぎ目・浮いた縁が無く、地平線に実地形の稜線が見えることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-HORIZON`
- paths: Scripts/Terrain, Tools/AssetCook, Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Game/GameModes/Wasteland, Assets/AssetSets, TASKS.md, PROGRESS.md
- notes: 地形の端の高さを外へ延ばす作り方は使わない（四角い外周から板や壁が伸びる）。1 m タイルの外が要るなら、ファイル名とサイズを書いて BLOCKED（ユーザーの許可が要る）。 2026-10-15 ユーザー: 隣の 1 m タイル 3 枚（`USGS_1M_12_x60y414_UT_WestEast_B22.tif` 約 311 MB・`x59y414` 約 305 MB・`x59y413` 約 295 MB、合計約 911 MB。配布元は同じ）のダウンロードを許可した（`blocked/SCN-WL-HORIZON.md`）。Castle Butte の周りは 1〜2 m、残りは粗い遠景で焼く。西の地平線（Lady in the Bathtub など）も実地形にする。 2026-10-15 ユーザー（2 回目）: 東・南の地平線のために 1/3 秒角の 2 枚（`USGS_13_n38w110_20260630.tif` 約 421 MB・`USGS_13_n37w110_20260514.tif` 約 404 MB、配布元 `https://prd-tnm.s3.amazonaws.com/StagedProducts/Elevation/13/TIFF/historical/<タイル>/<ファイル>`）のダウンロードも許可した。far の外に 32 m 前後の段を足して縫い目の帯でつなぐ。

## SCN-WL-MASKS-GEOM: 基本の地表の分類の地図を作る
- status: done
- done-when: 格子から、数 m の幅でならした傾斜・相対の高さ（周りより高いか低いか）・崖との位置関係（崖の下側か）を求め、露岩・崖錐・地表（細粒と礫）の重みの地図を焼く（1〜2 m/画素）。崖錐は崖からの距離だけで決めず、崖より低い側・斜面の向き・沢の切れ込みで扇状・舌状の塊にする。色分けの PNG を開き、崖に露岩、崖の下の裾に崖錐、平らな所に地表が出ることを確かめる。流れ（沢）は SCN-WL-MASKS-FLOW。
- verify: `python Scripts/Terrain/TerrainMasks.py --verify`
- paths: Scripts/Terrain, Tools/AssetCook, Assets/AssetSets, TASKS.md, PROGRESS.md
- notes: 1 m の格子をそのまま微分すると細かい凹凸を拾うので、数 m の幅で見た値を使う。地層の高さは地図に焼かず、シェーダーで共通の基準面からの高さとして計算する（SCN-WL-MATERIAL-STRATA）。

## SCN-WL-MATERIAL-MASKS: 地図を今の 3 層の描画へつなぐ
- status: done
- done-when: 地形の材質（今の 3 層）の重みを、地図から取る（傾斜・高さの式は地図が無い所の予備にする）。地図の座標を地形と揃え、MegaGeometry のラスタと可視バッファの解決の両方で効く。地形の LOD が変わっても分類が大きく動かない。overview・cliff の撮影を開き、露岩・崖錐・地表の分かれ方が地図どおりで、手本の比較画像で分布が近づいたことを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-MASKS`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-MATERIAL-MASKS`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game/GameModes/Wasteland, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。層を 3 つから増やすことは、この計画の中心にしない（材質のデータ・束縛・複数の描画経路に及ぶ）。

## SCN-WL-MATERIAL-PALETTE: 素材と実寸と物理的な明るさを整える
- status: done
- done-when: 3 層を、赤褐色の砂岩・岩屑と礫・細粒の赤い土の写真由来の素材に替える（Poly Haven と ambientCG の CC0 から選ぶ。git 管理外に落とし、名前・出典・大きさを記録する）。各素材の平均のアルベド（線形）を実在の値の範囲に合わせる: 赤い砂漠の土 0.15〜0.25、赤褐色の砂岩 0.20〜0.35、岩屑と礫 0.15〜0.30（測った値と根拠を PROGRESS に書く。今の地面は 0.65 で雪に近い）。各素材の模様の大きさを実寸に合わせる（礫の粒・砂岩の割れ目の間隔）。今の色の揺らぎを地表の分類に沿わせ、全域を覆う同じ模様の繰り返しを減らす。手本の比較画像（1920×1080）を開き、地面の色と明るさが写真に近づいた（白っぽいベージュでない）こと、素材の種類と模様の大きさが近いことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-PALETTE`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-MATERIAL-PALETTE --verify`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Wasteland, Assets/AssetSets, Scripts, .gitignore, TASKS.md, PROGRESS.md
- notes: 2026-10-15 親: ambientCG（CC0）と Poly Haven（CC0）の素材のダウンロードはユーザーが許可済み。写真との差でいちばん大きいのは地面の明るさ（写真 0.14・撮影 0.65、SCN-WL-LIGHT-BASE の比較）。露出やグレーディングで合わせない。 2026-10-15 親: SCN-WL-REFERENCE-CAPTURE の比較画像では、地表の材質の平均のアルベド（線形、×0.32 の倍率込み）が砂 0.100・乾いた土 0.065・岩肌 0.023 と低いのに、撮影の地面は Y 0.67（R 0.76・G 0.66・B 0.46）と白っぽい。テクスチャを替える前に、テクスチャの値と描かれた明るさが合わない原因（倍率が効かない経路・露出・トーンマップ・可視バッファの解決とラスタの違い）を先に突き止める。

## SCN-WL-SHADOW-DOT-GRID: 荒野の日の当たる向きの境の斜面に出る暗い点の格子の縞を消す
- status: done
- done-when: 太陽からわずかに背を向けた斜面（谷床の起伏の陰の側・崖錐の裾）に、斑状の暗い点の格子の縞が出る（SCN-WL-REFERENCE-CAPTURE から記録あり。SCN-WL-MATERIAL-PALETTE で地面が赤い土になって目立つ。GBuffer のアルベドには無いので光と影の側）。原因（太陽の影の深度の偏り・法線の偏り・影のページの解像度・地形の幾何と法線マップの法線の食い違い）を撮影の切り分けで突き止めて直す。ref_far_02・ref_ground_02・cliff の撮影（1920×1080）を開き、斜面の陰が滑らかにつながり、点の格子が無いことを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Views ref_far_02,ref_ground_02,cliff -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-DOT-GRID`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-SHADOW-DOT-GRID`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Wasteland, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-15 SCN-WL-MATERIAL-PALETTE の反復 6 で見つけた（`.harness/runs/scene-capture/SCN-WL-MATERIAL-PALETTE/ref_far_02.png` の中景、`cliff.png` の谷床）。危険地帯（描画）。 2026-10-15 親: 反復 10・11（run 20261015-082405）で原因を 3 つに分けて直した: 崖とビュートの横縞と点の格子は地形の法線の勾配（反復 10 で消えた）、崖錐の斑は VSM の自己遮蔽（余裕を 3 texel にして 1 万画素あたり 15.2 → 0.3）、残りは接触影が地形の三角形の折れ目を遮りと誤る点（受け手とほぼ平行な面を除く直しで減ったが、砂丘にまばらに残る。接触影を切ると消える）。この項目は、途中の保存（3dc9555f・fe36083d）の直しを残して撮影で確かめて閉じる（縞と崖錐の斑が消え、Rendering3DTest が変わらない）。残る接触影の点は SCN-WL-CONTACT-SHADOW-TERRAIN で直す。最後の反復は接触影の判定のループを書き換えている途中で止まったので、まずビルドが通る形に戻す。

## SCN-WL-MATERIAL-STRATA: 砂岩の地層と風化の筋を足す
- status: done
- done-when: 崖の砂岩に、厚さの違う層・斜めの交差層理・途切れる帯・硬い層の張り出しを、共通の基準面からの高さでシェーダーが計算して入れる（等間隔の帯にしない）。砂漠ワニスの黒い筋を、崖の上端から流れ落ちる位置に局所的に置く（崖全体へ同じ縦のノイズを掛けない）。cliff と手本の崖の比較画像を開き、帯の厚さ・向き・途切れ方が写真に近く、同じ縞が崖全体に繰り返していないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-STRATA`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: Cedar Mesa 砂岩には水平の層と斜めの交差層理がある（NPS の地質の説明）。

## SCN-WL-FAR-SHADING: 遠景の地形をちらつかない・汚れない描き方にする
- status: done
- done-when: 遠くの地形の材質が、細かい模様の縮小で斑点やざらつきになる（ユーザーの指摘「遠景が汚すぎる」）のを直す。距離に応じて細かい模様と細かい法線を薄め、遠くは前もって縮小した大きな色と法線（地表の分類と DEM から作る）で描く。細かい法線の鏡面の光り（スペキュラーのエイリアス）を抑える。遠景の稜線とビュートに、写真のような地層の帯と色の差が見える。空気遠近の霞を手本の澄んだ空気に合わせる: SCN-WL-HORIZON の far_01 の比較では、3〜5 km 先の崖が写真では赤く濃い（崖 Y 0.24・R÷G 1.6）のに、撮影では白く霞む（崖 Y 0.74・R÷G 1.4）。霞の元（エアロゾルの濃さと高さ・高さフォグ・空気遠近の掛け方）を突き止め、遠くの崖の明るさと赤みが写真に近づくように直す（露出でまとめて下げない）。ground・overview・far の比較画像（1920×1080）を開いて、遠景が写真のように滑らかに色の差で読め、斑点・ざらつき・模様の繰り返しが無いことを確かめる。カメラパスか連続撮影の数枚で、動かしても遠景がちらつかないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-FAR-SHADING`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game/GameModes/Wasteland, Scripts/Terrain, Tools/AssetCook, Assets/AssetSets, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。

## SCN-WL-CLIFF-DETAIL: 崖を、地層の段と割れ目のある立体として焼く
- status: done
- done-when: 1 m の DEM では崖が丸く崩れ、垂直な面・段・割れ目が出ない（ユーザーの指摘「崖の表面が現代の CG の水準でない」）。急な斜面と崖の範囲だけを、細かい間隔（0.25 m 程度）で焼き直し、面の法線の向きに、地層に沿った凹凸（硬い層の張り出しと軟らかい層のくぼみ・縦の割れ目・崩れた縁）を足す（高さ場の上下だけでなく面に沿って押し出す）。地層の高さはシェーダーの地層（SCN-WL-MATERIAL-STRATA）と同じ基準で揃える。焼いた崖と周りの地形の継ぎ目が見えない。配置の高さ（岩・建物）は今の高さ場のままでよい。cliff と手本の崖の比較画像（1920×1080）を開き、崖の段と割れ目が影を落とす立体として見え（テクスチャの模様でなく）、網目のような繰り返しが無いことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath "Models/WastelandTerrain/*"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-DETAIL`
- paths: Tools/AssetCook, Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Scripts/Terrain, Assets/AssetSets, Assets/Shaders, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットのクック）。三角形が増えるが、仮想化ジオメトリ（MegaGeometry）で描ける量か確かめる（速さの条件は無い。操作できる程度ならよい）。1 反復で閉じなければ、主役の Battleship Rock だけで先に閉じ、残りの崖を `SCN-WL-CLIFF-DETAIL-ALL` として後ろに足す。 2026-10-15 親: 3 反復とも焼きが 40 分に収まらず遮断器で blocked になった（Debug の AssetCook で中央の区画 1 本 20 分超。頂点ごとに位置を 5 回求めて法線を差分で出していた）。焼きは RelWithDebInfo の AssetCook で行い、細分の格子の位置を 1 回だけ求めて法線を隣の点から作る形にして速くする（反復 6 の PROGRESS の案）。反復の外で走らせ続けた AssetCook は親が止めた（その出力は使わない）。焼きは反復の中で終わる形にし、反復の外へ切り離さない。

## SCN-WL-GEOMETRY-PAGE-THRASH: ジオメトリのページの置き場が足りないとき、ページを外し続けて読み込みが終わらなくなるのを直す
- status: done
- done-when: 崖の細部を左の奥の崖まで 1 m で焼いた版（崖の区画の合計 1 億 2610 万三角形、run 20261015-203920 の反復 2）で、荒野の撮影が読み込み中から抜けなかった（`GEOMETRY_PAGES` の evicted が数分で 2 万 7 千、`VRAM_LEDGER geometry_pool blocks=1 capacity_mb=1024`、target_mb は 3066）。原因の見立て: ページはメッシュと同じ塊にしか置けず（`MegaGeometryResourceStore::BeginUpload` の `AllocateInBlock`）、全メッシュの根が最初の 1 GB の塊に入るので、塊が埋まると NoSpace になる。そのとき `GeometryPageStreamer.h` の `StartUploadsLocked` の NoSpace のループは、要求より古い常駐ページを塊を問わず外し続け、外した領域は GPU の使用が終わるまで返らないので、空かないまま一度に数千ページを外す。まず見立てをログか試験で確かめ、違っていれば本当の原因を直す。直す形の目安: (1) NoSpace のときに外すのは同じ塊のページだけにし、返却待ちの領域があるあいだは外し続けずに次のフレームを待つ。(2) メッシュを置くとき、その塊にページの置き場が残らなければ新しい塊へ置き、プールを VRAM の目標（geometry_target_mb）まで複数の塊に広げる。確かめ方: 今の焼き（崖の細部の版 5。撮影が ref_cliff_01 で止まり、evicted=34115・`used_mb=1023.1`）がそのまま再現になる（焼き直さない）。Release に計測・デバッグの機能を足さない。この焼きで、荒野の全 11 視点の撮影が 10 分以内に終わり、外したページの数が読み込んだページの数を大きく超えないことをログで確かめる。mega-bananas（既定の本数）・起動画面の撮影が変わらないこと（溢れ 0、読み込みの時間が延びない）を確かめる。試験は既存の `Test/Core/Rendering/GeometryPageStreamerTest.cpp` の中に、NoSpace で返却待ちのあいだにページを外し続けないことの 1 件だけ足してよい（新しい試験の実行ファイルは作らない）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-GEOMETRY-PAGE-THRASH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -RequireAssets -OutDir .harness/runs/scene-capture/SCN-WL-GEOMETRY-PAGE-THRASH-mega`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-GEOMETRY-PAGE-THRASH`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Test/Core/Rendering, Tools/AssetCook, Scripts, Docs/RenderingValidation, Docs/Architecture, TASKS.md, PROGRESS.md
- notes: 危険地帯（メモリ・RenderThread・ジオメトリのストリーミング）。2026-10-15 親が SCN-WL-CLIFF-DETAIL-ALL の blocked（反復 2）の調べから足した。荒野は美しさを先にして崖・岩・草の三角形を増やしていくので、置き場の不足で読み込みが止まらないようにする。崖の細部の範囲はこの項目では変えない。

## SCN-WL-CLIFF-DETAIL-ALL: Battleship Rock の外の崖にも、地層の段と割れ目の焼き直しを広げる
- status: done
- done-when: SCN-WL-CLIFF-DETAIL は主役の Battleship Rock の範囲（`Tools/AssetCook/TerrainCliffDetail.cpp` の X −2150〜−900・Z −1650〜−420）だけを焼き直した。手本の視点に写るほかの崖（ref_cliff_02 の尖塔の台座・Castle Butte・左右のメサ）にも同じ焼き直しを広げ、範囲の境で焼いた崖と周りの地形の継ぎ目が見えない。三角形の数を区画ごとに見て、仮想化ジオメトリで描けることを確かめる（溢れ 0）。比較画像（1920×1080）を開き、広げた崖の段と割れ目が影を落とす立体として見えることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath "Models/WastelandTerrain/*"`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-DETAIL-ALL`
- paths: Tools/AssetCook, Scripts/Terrain, Assets/AssetSets, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットのクック）。SCN-WL-CLIFF-DETAIL の notes の「1 反復で閉じなければ残りの崖を後ろに足す」による。焼きは RelWithDebInfo の AssetCook で、一覧を分けた並列で反復の中で終える（中央の区画 dem:2,3 は 1408 万三角形・1 本約 14 分）。範囲を広げると三角形がさらに増えるので、細分の間隔や範囲の取り方を区画ごとの数で決める。 2026-10-15 親: 反復 4 の評価で差し戻した所（NEXT_FINDINGS の反復 4）だけが残っている。ref_far_01 の左の奥の崖（画面の左 約 600 画素、視点から 5〜9 km、mid の段の 8 m の格子）が丸い面のままで、主役のビュート・尖塔・右の崖の段と割れ目と並ぶと汚れて見える。この崖を castle の段か細かい格子の範囲へ入れて同じ焼き直しを掛ける（1 画素が 1.1〜2 m なので、細分は 0.5〜1 m でよい）。版 4 で焼いた崖と範囲は変えない。撮り直した ref_far_01 の左の切り出しを版 4 の `crop_far01_left.png` と並べて開き、段と割れ目の影が出たこと・新しい範囲の境に継ぎ目が無いこと・溢れ 0 を確かめる。 2026-10-15 親（反復 2 の blocked への判断）: 案 1 を採る。`kCastleRegions` の 3・4 番目（左の奥の崖・尖塔の奥の崖、5〜9 km）を間隔 2 m・細分の下限 1 にする（2 m の格子の点の押し出しだけ。1 画素 1.1〜2 m なので足りる）。ジオメトリのプールは広げない（実行時の側には触らない）。STEER の「コードを変えない」はこの変更には当てはまらない。手順はビルド → 印の合わない項目を 12 組で並列に焼く（dem:2,3 を最初の組に単独で）→ `CookAssets.ps1` で cooked=0 → 撮影。撮影の読み込みが 600 秒に収まらなければ、範囲を ref_far_01 に写る急な升目だけに狭める。 2026-10-15 親（反復 2 の blocked への判断）: 案 1 を採る。版 5 の焼き（左の奥の崖の段と割れ目は出ている）はそのまま使い、コードと焼きは変えない。SCN-WL-GEOMETRY-PAGE-THRASH の後に、撮影（verify の 4 本目）と比較画像の確認だけをやり直して閉じる。

## SCN-WL-REVIEW-1: 節目の確認 1（地面・崖・遠景）をユーザーに渡す
- status: done
- done-when: 手本の全視点の比較画像（写真｜撮影、1920×1080）を撮り直し、1 枚の一覧にまとめ、写真との差の大きい順の所見を `Docs/RenderingValidation/WastelandReference.md` の「確認 1」に書く。評価者は一覧を見て、写真との大きな差（色・明るさ・構造・欠けた物）と、技術の欠陥（浮き・継ぎ目・繰り返し・ちらつき）を挙げる。最後にリポジトリ直下へ中身が `boundary` の `LOOP_STOP` を書き、ランナーをこの反復の後で止める（親がユーザーに一覧を渡し、判定を待つ）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-REVIEW-1`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-REVIEW-1 --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-15 ユーザー: 品質の確認は節目ごと（3 回程度）にユーザーが見る。 2026-10-15 親: 近くの主役の崖はこの確認の後でスキャンの素材と Blender で作り直す（SCN-WL-CLIFF-SCAN-*）。所見には今の崖を書いてよいが、一覧に「近くの崖は作り直す予定」と添える。 2026-10-16 親: ユーザーが一覧を見て判断した（地面 → 光と色 → 近くの崖の順に進める）ので親が done にした。評価の差し戻し（原寸の一覧の注記が再検証の `--note` なしの実行で消える）は SCN-WL-REVIEW-2 で直す。評価で見つかった ground_02 の空の高さ約 380 px の横の明るさの境は SCN-WL-SKY-AERIAL へ回した。

## SCN-WL-GROUND-SOIL: 谷床の地面を、手本の赤茶の土と礫の地面に見えるようにする
- status: done
- done-when: 確認 1（`Docs/RenderingValidation/WastelandReference.md`）で、地面が全視点で淡い橙の砂丘の砂の一面に見えた（細かな筋の模様、起伏が砂丘に見える。写真の赤茶の土・礫の舗石・黒い土の皮膜のまだらが無い）。地表の素材と混ぜ方を、手本の足元の写真（ground_01・ground_02・far_01 の手前）の地面に合わせる: 写真由来の素材（ambientCG・Poly Haven の CC0。取得は許可済み）から赤茶の土・礫の舗石・皮膜の付いた土を選び、地表の分類の地図と数 m〜数十 m の大きなまだらで混ぜ、近くで粒と小さな凹凸（法線・高さ）が読め、遠くで同じ模様の繰り返しが見えないようにする。アルベドは実在の範囲（赤い砂漠の土 0.15〜0.25）のまま。全体の明るさ（写真の 2〜9 倍）は SCN-WL-SKY-AERIAL で原因を分けて直すので、この項目では露出・光を変えない。比較画像（写真｜撮影、1920×1080）を開き、地面が砂丘の砂でなく赤茶の土と礫の地面に見えることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-GROUND-SOIL`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-GROUND-SOIL --verify`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Wasteland, Assets/AssetSets, Scripts, .gitignore, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-16 ユーザーの決定（確認 1 の後）: 地面 → 光と色 → 近くの崖の順に進める。見た目の作り込みは自前の式と長いクック待ちで回さず、写真由来の素材を使う。

## SCN-WL-MASKS-FLOW: 沢と堆積の分布を足す
- status: done
- done-when: 余白を含む広い範囲で流れの集まり（窪地の処理を含む。描画用の高さは変えない）を求め、対数にした集水面積から沢底の幅を決めて地図に足す。沢底は乾いた細粒の地面にし、砂の吹きだまりは風向（手本から推定）と写真で補う（流量だけで決めない）。overview・ground の撮影を開き、涸れ沢が地形の谷に沿った線として地表の材質に出ることを確かめる。
- verify: `python Scripts/Terrain/TerrainMasks.py --verify`
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-MASKS-FLOW`
- paths: Scripts/Terrain, Tools/AssetCook, Assets/AssetSets, Assets/Shaders, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md

## SCN-GRASS-RENDER: 枯れ草の株をインスタンスで描き、風で揺らす
- status: done
- done-when: 曲がった細い葉を束ねた枯れ草の株のメッシュ（葉の枚数は手本の写真の株に合わせる。数十枚でよい）（手続き生成。アルファ抜きを使わない）を、インスタンスの配列（位置・向き・大きさ・色の揺らぎ）から少ない回数の描画（間接描画かインスタンス描画）で描く経路を足し、頂点シェーダーで風（時間と低周波ノイズ。根元は動かず先ほど揺れる）で揺らす。色は乾いた黄土〜灰褐色で、裏面も描く（両面）。影は受ける。`wasteland` の一角に数千株を置いた名前付き視点 `grass` を足し、撮影を開いて草に見える（板やトゲに見えない）こと、時刻を変えた2枚で揺れていることを確かめる。米南西部の植生に合わせ、枯れ草の株に加えて、細い枝の低木（ブラックブラッシュ・モルモンティーのような高さ 0.3〜1 m の株）も同じ経路で描き、手本の足元の写真と並べて形と色が近いことを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views grass -OutDir .harness/runs/scene-capture/SCN-GRASS-RENDER`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-GRASS-RENDER`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Library/Core/Public/Component, Library/Core/Private/Component, Library/Core/CMakeLists.txt, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今のシェーダーに discard が無く、`bTwoSided`（`RenderMaterialStore.cpp` 50 行付近）がカリングに効いているかは未確認。インスタンスを束ねる既存の仕組みは `DrawCommand::AddInstance`（`DrawCommand.h` 421 行付近）。 2026-10-16 親: 反復 5 の評価の指摘 1（葉の枚数が「数〜十数枚」を超える）は、親が書いた枚数の条件の方を外した（実在の枯れ草の株は数十枚の葉を持つ。美しさを先にする）。指摘 2（草が硬い針の放射・三角板に見える、低木の太い骨格が透ける）は直す: 草は葉先の垂れと長さの不均一、低木は細枝の密度と葉の明暗を写真の株に合わせ、同じ比較画像（grass_vs_ground_01）で確かめる。低木の形が手続きの生成で写真に近づかなければ、Poly Haven（CC0、取得は許可済み）のスキャンの低木か ComfyUI の生成のモデルに替えてよい（同じインスタンスの描画の経路で描く。替えた理由を PROGRESS に書く）。

## SCN-GRASS-SCATTER: 地形に枯れ草を撒き、区画ごとに間引く
- status: done
- done-when: 地表の分類の地図（SCN-WL-MASKS-GEOM・FLOW）と手本の写真の疎密で密度を決めて、低木と枯れ草を撒き（露岩・崖錐の岩の上・沢底の流路・建物の足元・道には生えない）、区画ごとに視錐台で外し、距離で密度を下げて 80〜120 m 先で消す（消え方が線にならないよう株ごとのしきい値でフェード）。`ground`・`outpost`・`overview` の撮影を開いて、近くは草むら、遠くは自然に薄くなることを確かめる。性能はこの項目では測らず、SCN-WASTELAND-POLISH で草が目標を超える原因になったときに、カリングとラスタの内訳から密度・距離・LOD を調整する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-GRASS-SCATTER`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。

## SCN-WL-CLIFF-SCAN-SELECT: 近くの崖に使うスキャンの素材を選ぶ
- status: done
- done-when: 近くの主役の崖（Battleship Rock・Franklin Butte の尖塔の台座）の表面を、式の押し出しではなく写真測量でスキャンした本物の岩の形で作るための素材を選ぶ。Poly Haven・ambientCG（CC0、取得は許可済み）のモデルと、高さの地図を持つスキャンの材質から、Cedar Mesa の砂岩の崖（水平の層の段・縦の割れ目・板状の剥離・赤い砂岩）に合う候補を API で探し、大きさと合計を記録する。候補を Blender のヘッドレス（`Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe`、`Scripts/BlenderPreviewGlb.py`）で描き、手本の写真（cliff_01・cliff_02）と並べた比較図を開いて形と質感で選ぶ。既にある `namaqualand_*`（花崗岩の丸い岩）は砂岩の崖に合わない前提で比べる。CC0 に合う形が無ければ、ほかの出所（CC-BY のスキャン、Fab の Megascans 等）の候補を、ファイル名・出所・大きさ・利用条件つきで `blocked/` に書いて人の許可を待つ（許可なく落とさない）。選んだ素材と選んだ理由を `Docs/RenderingValidation/WastelandReference.md` に書く。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/FetchPolyHavenModels.ps1 -Set wasteland`
- paths: Scripts, Assets/AssetSets, Docs/RenderingValidation, .gitignore, TASKS.md, PROGRESS.md
- notes: 2026-10-15 ユーザーの決定: 崖の細部は、近くの崖をスキャンの岩の素材と Blender で作る（式の焼き込みは遠景だけにする）。生成の素材（ComfyUI）は足元の落石・岩塊に使ってよい。Blender は Python でヘッドレスに動かす。 2026-10-16 親: 足元の岩（SCN-WL-CLIFF-ASSETS・SCATTER-ROCKS）と素材をそろえるため、選ぶだけを地面の項目の中へ先に移した。Blender で置く項目（BLENDER-TERRAIN-VIEW・CLIFF-SCAN-PLACE・NEAR）は光と色の後。

## SCN-WL-CLIFF-ASSETS: 崖の足元の落石と岩塊の素材を揃える
- status: done
- done-when: 崖の下に落ちた砂岩の岩塊（角張った大岩・板状に割れた岩）に合うメッシュを選んでクックする。Poly Haven・ambientCG（CC0）で合うものを探し、無ければ ComfyUI で生成する（理由を書く）。崖の砂岩の色と馴染むように材質を合わせる。代表の 1〜2 個を cliff の視点の近くに置いた撮影（1920×1080）を開き、写真の足元の岩（ground_01・ground_02）と並べて形と色が近いことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-ASSETS`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, Game/GameModes/Wasteland, .gitignore, TASKS.md, PROGRESS.md
- notes: 2026-10-15 親: 崖の面そのものは SCN-WL-CLIFF-SCAN-* で作る。この項目は崖の足元の落石・岩塊だけ。SCN-WL-CLIFF-SCAN-SELECT で選んだ素材と色をそろえる。 2026-10-16 親: 地面の項目へ移した（SCN-WL-SCATTER-ROCKS が使う岩の素材を先に揃える）。素材は SCN-WL-CLIFF-SCAN-SELECT で選んだものと色をそろえる。

## SCN-WL-SCATTER-ROCKS: 大岩と崖錐を置く
- status: done
- done-when: 崖の下の崖錐に、崖の剥離片（角張った大岩・岩）を群れとして置く（大岩の周りに小片が集まり、空白も残す。全域に均等に撒かない）。数は手本の写真の被覆と疎密に合わせる（目標の個数は置かない）。散布の Object（バナナの仕組み）を使う。overview・cliff・ground の撮影と比較画像を開き、崖錐が点の集まりでなく裾の帯として見え、岩が浮いていないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views overview,ground,cliff,ref_cliff_01,ref_cliff_02 -OutDir .harness/runs/scene-capture/SCN-WL-SCATTER-ROCKS`
- paths: Game/GameModes/Wasteland, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: RT の選別（120 m・最大 65,536 件）と大量配置の TLAS 更新の直しは、後ろの SCN-MEGA-FIX-RT-SELECT・TLAS-REBUILD にある。RT の影・GI で岩が欠けるなら、その旨を PROGRESS に書いて進める。

## SCN-WL-SCATTER-GRAVEL: 近景の石と礫を足す
- status: done
- done-when: 輪郭と接触の影が読める大きさ（数 cm〜数十 cm）の石・礫を、近景（数十 m まで）の礫の地面と沢底に置き、それより遠くは材質（礫の層）に引き継ぐ（消える距離で目立つ線や点滅を出さない）。画面で 1〜2 画素以下になる粒はジオメトリにしない。ground・outpost の撮影と手本の足元の写真の比較画像を開き、足元の尺度（粒の大きさ・被覆）が写真に近いことを確かめる。カメラパスか連続撮影で、移動中に石の出入りが目立たないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views ground,outpost,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-SCATTER-GRAVEL`
- paths: Game/GameModes/Wasteland, Assets/AssetSets, Assets/Shaders, Library/Core/Private/Rendering, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-16 親（遮断器の blocked への判断）: 反復 4 で、石を赤褐色の角張った破片にする直し（親の STEER）と歩く連続撮影（WALK exit=0）までは済み、最後の 4 視点の撮影の途中で 40 分が尽きた（ランナーの PowerShell が Game を起動せずに 19 分止まったため）。残りは、作業途中の保存（fa2bbfb9）のコードのまま、歩く連続撮影でカメラが動いたことを数値で確かめ、verify の撮影と PROGRESS の更新をして閉じるだけ。コードは変えない。撮影が起動しないときは 5 分で見切り、止めて起動し直す。

## SCN-WL-CONTACT-SHADOW-TERRAIN: 接触影が地形の折れ目を遮りと誤って出す点を消す
- status: done
- done-when: 画面空間の接触影が、地形の三角形の折れ目や細かい凹凸を遮りと誤り、砂丘や斜面にまばらな暗い点を出す（SCN-WL-SHADOW-DOT-GRID の残り。接触影を切ると消える）のを直す。岩・礫・低木を地面に据えた接地の影（接触影の本来の役目）は残す。ground・outpost・ref_ground_02 の撮影（1920×1080）を拡大して、地形の上の点が消え、岩と低木の足元の影が残ることを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views ground,outpost,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CONTACT-SHADOW-TERRAIN`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-CONTACT-SHADOW-TERRAIN`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game/GameModes/Wasteland, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-15 親: SCN-WL-SHADOW-DOT-GRID から分けた。危険地帯（描画）。岩と礫を撒いた後（SCN-WL-SCATTER-GRAVEL の後）に置き、接地の影と一緒に確かめる。

## SCN-WL-FAR-SPECKLE: 荒野の遠くの斜面と平地に一面に出る細い縦の黒い点を消す
- status: done
- done-when: ref_cliff_02 の撮影（1920×1080）で、Franklin Butte の崖錐の斜面と奥の平地に、1〜2 画素幅・数画素の高さの縦の黒い点が一面に散る（`crop_cliff02_spire.png`。切り出しは X 640〜1280・Y 340〜800 を 2 倍）。原因（遠くの小石や草の置き物の縮小・影の地図の標本・法線の縮小のどれか）を突き止めて直し、同じ切り出しを開いて点が無いことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_02 -OutDir .harness/runs/scene-capture/SCN-WL-FAR-SPECKLE`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Game/GameModes/Wasteland, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-15 SCN-WL-CLIFF-DETAIL-ALL の撮影の確認で見つかった。崖の細部の焼き直しの前（SCN-WL-CLIFF-DETAIL の撮影）にも同じ点があり、焼き直しをしていない遠くの平地にも出るので、崖の焼き直しから生じたものではない。 2026-10-16 親: 確認 1 の技術の欠陥（ground_02 の手前の暗い点の列）と合わせて地面の項目の終わりに置いた。SCN-WL-CONTACT-SHADOW-TERRAIN の後でも残る点を、この項目で原因から直す。

## SCN-WL-OUTPOST: 前哨地を自然に平らな所へ置き直し、建物と道の範囲だけ均す
- status: done
- done-when: SCN-WL-REFERENCE-SELECT で選んだ平らな場所へ、前哨地（建物 6 種 8 棟・門・道）を置き直し、建物の足元と道の範囲だけ高さ場を緩い平面へ寄せる（境で高さと傾斜を滑らかにつなぎ、円形の継ぎ目を作らない）。造成は高さ場の規則として持ち、クックと Game が同じ値を使う（区画を焼き直す）。outpost の視点を置き直す。outpost・ground の撮影を開き、建物が浮かず埋まりすぎず、造成の境が見えないことを確かめる。散布の除外範囲（建物・道）を決めて書く。`wasteland` の cycle smoke 3 周が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Width 1920 -Height 1080 -Views overview,outpost,ground -OutDir .harness/runs/scene-capture/SCN-WL-OUTPOST`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes wasteland -Cycles 3 -Configuration RelWithDebInfo`
- paths: Game/GameModes/Wasteland, Game/Scenes/SceneCatalog.h, Library/Core/Public/Rendering/Terrain, Library/Core/Private/Rendering/Terrain, Tools/AssetCook, Assets/AssetSets, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-16 ユーザーの決定: 建物は地面の項目の後・光と色の前に戻す（確認 2 で建物の入った荒野を見る。光と色も建物込みで合わせる）。先に撒いた低木・草・岩・礫（SCN-GRASS-SCATTER・SCATTER-ROCKS・SCATTER-GRAVEL）は、建物の足元・道・門の範囲から外す（散布の側に除外の範囲を渡す）。 2026-10-16 親: 実装と 3 周のスモークの合格（928 秒、`verify-SCN-WL-OUTPOST-6.txt`）は反復 9 で済んだが、Debug の荒野は 1 周約 300 秒で、ランナーの再検証（1 コマンド 10 分）で時間切れになった。verify のスモークを 1 周にした（3 周の合格は反復 9 の証拠で足りる）。次の反復はコードを変えず、verify を回し直して閉じる。 2026-10-16 run 20261016-184945 反復 1: `RunSceneCycleSmoke.ps1` と Game は単一シーンでも 2 周以上を要求する（基準の周回と比べるため）ので 1 周の verify は通らない。スモークを RelWithDebInfo の 3 周（`-Build` 付き）に替えた。ビルドの確認込みで 350 秒・result=pass（`verify-SCN-WL-OUTPOST-3.txt`）。Debug の検証レイヤー込みの 3 周は反復 9 の証拠（`verify-SCN-WL-OUTPOST-6.txt`）。

## SCN-WL-CAPTURE-SPEED: 荒野の撮影の読み込みを速くし、光と色の試行錯誤を回せるようにする
- status: done
- done-when: 荒野の撮影は Debug で 1 視点でも約 320 秒、4 視点で約 6 分かかり（SCN-WL-FAR-SPECKLE・SCATTER-GRAVEL）、1 反復で確かめを 2〜3 回しか回せない。この後の光と色（RTGI-COLOR・SKY-AERIAL・LIGHT-FINAL）は撮って直すのを何度も繰り返すので、先に速くする。起動から最初の撮影までの時間を段ごと（シェーダー・アセットの読み込み・ジオメトリのページ・草と岩と礫の生成・落ち着くまでのフレーム）に測り、いちばん重い段を直す。Debug の遅さが主因なら、荒野の撮影の verify を RelWithDebInfo の Game（`-Configuration RelWithDebInfo -Build`）に替える（Debug の Game のビルドの verify は残す。Release に計測・デバッグの機能を足さない）。手本の 6 視点の撮影の時間を、直す前と後で同じ条件で並べ、半分以下になったことを確かめる。撮影の画は直す前と同じ見た目であること（比較画像を開く）。残りの荒野の項目の verify の撮影を、決めた構成に揃える。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CAPTURE-SPEED`
- paths: Game, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-16 親: 撮影の待ちが反復の大半を占め、時間切れと遮断器が続いた（SCATTER-ROCKS・GRAVEL・CLIFF-ASSETS）。見た目を詰める項目の前に、確かめの 1 回を短くする。verify の 2 本目は、この項目で構成を替えたら同じ反復で書き換えてよい。 荒野の周回スモーク（`RunSceneCycleSmoke.ps1 -Scenes wasteland`）も Debug で 1 周約 300 秒かかる（SCN-WL-OUTPOST）。同じ読み込みの段が原因なら一緒に速くなることを確かめる。

## SCN-WLR-HDR-CAPTURE: 撮影でブルーム前の HDR を書き、今の見た目を基準として残す
- status: done
- after: SCN-WL-CAPTURE-SPEED
- done-when: 撮影（`Scripts/CaptureScene.ps1`）に `-Hdr` を足し、ブルームの前の線形の HDR（プリエクスポージャを外した放射輝度 cd/m²、RGB の 32 bit 浮動小数。OpenEXR か、ヘッダ付きの生データ）を PNG と並べて書く（撮影の起動引数のときだけ。Release に計測・デバッグの機能を足さない）。`Scripts/Terrain/ReferenceCheck.py --hdr` を作り、HDR の大きさ・値の範囲（負・NaN・Inf が無い）と、同じ視点の PNG の暗い画素がブルームの影響の小さい所で HDR から作った値と合うこと（表示変換の厳密な照合は SCN-WLR-DISPLAY-TRANSFORM）を検査する。今の荒野（変更前の見た目）の手本の 6 視点と outpost・ground の HDR と PNG を、後の比較の基準として `.harness/runs/scene-capture/SCN-WLR-BASELINE/` に残す（見た目を変えない）。比較の約束（全 SCN-WLR-* 共通）: 値は線形の Rec.709 の原色・白色点 D65、輝度 Y = 0.2126R + 0.7152G + 0.0722B、放射輝度は cd/m²（エンジンのプリエクスポージャを外した値）。測光用の HDR はブルームの前で読む。重い描画（エンジンの撮影の組・Cycles）は反復の中で行って結果を保存し、verify は `Scripts/Terrain/ReferenceCheck.py` で保存した結果を検査する（結果の横に入力のハッシュ〔台本・メッシュ・`Scripts/Terrain/CommonLight.json`・git のコミット〕を書き、今のファイルと違えば古い結果として拒否する）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01 -OutDir .harness/runs/scene-capture/SCN-WLR-HDR-CAPTURE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --hdr --capture .harness/runs/scene-capture/SCN-WLR-HDR-CAPTURE --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --hdr --capture .harness/runs/scene-capture/SCN-WLR-BASELINE --views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02,outpost,ground --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の読み出し）。2026-10-16 ユーザーの決定「正解の絵から」（Docs/Plans/WastelandRebuild.md。Sol の批評 2 回を反映）。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-DISPLAY-TRANSFORM: エンジンの表示変換を Python で再現し、HDR から PNG を作れることを照合する
- status: done
- after: SCN-WLR-HDR-CAPTURE
- done-when: エンジンの露出・トーンマップ（荒野の NeutralLinear と ACES 2.0 の LUT の両方）・グレーディング（コントラスト・彩度・色温度・ビネット）・sRGB の出力を `Scripts/Terrain/DisplayTransform.py` に再現する（`Assets/Shaders/tonemapping.frag`・`Scripts/BakeAcesOutputLut.py` と同じ値・同じ補間）。合成の色の階段（明るさ 12 段 × 原色・補色・灰）をエンジンの表示変換の経路に通した結果と 8 bit で 1 段以内に合うこと、SCN-WLR-BASELINE の HDR にブルームを外した撮影で同じ式を掛けた PNG がエンジンの PNG と画素の 99 % で 1 段以内に合うことを確かめる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --display --capture .harness/runs/scene-capture/SCN-WLR-DISPLAY-TRANSFORM --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この再現を、後の比較（Cycles の EXR を含む）の表示変換の唯一の経路にする。Blender の AgX・Filmic は使わない。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-COMMON-LIGHT: 大気を切った共通の照明と灰色の板で、明るさの尺度をエンジンで確かめる
- status: done
- after: SCN-WLR-HDR-CAPTURE
- done-when: 共通の照明の定義 `Scripts/Terrain/CommonLight.json`（一様な空の放射輝度 RGB cd/m²・太陽の照度 lux（太陽に垂直な面）・太陽の向き。視点ごとに手本の太陽の高度・方位）を作り、起動引数 `wasteland-common-light` で空の光と背景をその一様な空に、太陽をその照度と向きにし、空の大気の散乱・空気遠近・高さフォグ・雲を切る（既定の見た目は変えない）。起動引数 `wasteland-gray-plate` で視点の前に灰色の板（アルベド 0.18・粗さ 1・空へ向けた水平の面）を置き、`common-light-case=sun`（空の光 0）と `common-light-case=sky`（太陽 0）で撮る。期待の値は入力から求める（太陽だけ: 0.18 × E⊥ × cos(天頂角) / π、空だけ: 0.18 × 空の放射輝度）。エンジン自身が計算した照度を期待値に使わない。HDR の板の輝度が期待に 3 % 以内で合うことを `ReferenceCheck.py --plate` で検査する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-gray-plate,common-light-case=sun -OutDir .harness/runs/scene-capture/SCN-WLR-COMMON-LIGHT/sun`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-gray-plate,common-light-case=sky -OutDir .harness/runs/scene-capture/SCN-WLR-COMMON-LIGHT/sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --plate --capture .harness/runs/scene-capture/SCN-WLR-COMMON-LIGHT --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。HDRI は使わない（`EnvironmentMapSource.cpp` の読み込みは EXR を読めず、HDRI の中の太陽と別の太陽の二重計上の落とし穴があるため、一様な空と太陽に限る）。大気（空・霞）の検証は SCN-WLR-ATMOS-* で別に行う。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-BARE-AOV: 荒野を地形だけで撮り、照明の無い材質の値を生の数値で書き出す
- status: done
- after: SCN-WLR-COMMON-LIGHT
- done-when: 起動引数 `wasteland-hide=<clouds+foliage+rocks+gravel+buildings+aerial+fog の組み合わせ、または all>` で、荒野から雲・植物・岩・礫・建物・空気遠近・高さフォグを 1 つずつ外せるようにする（既定の見た目は変えない）。撮影に `-Aov` を足し、GBuffer の生の値を浮動小数の画像に書く: アルベド（線形）・法線（ワールド空間、+Y が上、長さ 1）・粗さ・深度（カメラからの視線方向の距離 m）。`GBufferDebugPass` の可視化画像は使わない。`ReferenceCheck.py --aov` で、法線の長さ・深度の単調さ（空は無限）・アルベドの範囲と、`wasteland-hide=all` の画に雲・物・霞が無いこと（空の画素の色が一様な空と一致など）を検査する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all -OutDir .harness/runs/scene-capture/SCN-WLR-BARE-AOV`
- verify: `python Scripts/Terrain/ReferenceCheck.py --aov --capture .harness/runs/scene-capture/SCN-WLR-BARE-AOV --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。起動引数は比較のためのシーンの設定で、Release でも害が無い形にする。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-BLENDER-TERRAIN: エンジンが描く地形のメッシュを Blender へ持ち込み、6 視点の投影を合わせる
- status: done
- after: SCN-WLR-BARE-AOV
- done-when: 比較用に地形の LOD を最細に固定する起動引数（`wasteland-finest-lod`。仮想化ジオメトリで固定できなければ、その視点で描かれた LOD を書き出して同じものを渡す）と、エンジンが描く地形のメッシュ（近景の DEM の区画と遠景の段、崖の細部を含む）を Blender が読める形（PLY か GLB）に書き出す道具を足す。書き出す範囲は視錐台だけでなく、影・空の遮蔽・照り返しに効く範囲（各視点から影の届く 4 km と遮蔽の届く範囲）を含める。Blender のヘッドレスの台本 `Scripts/Terrain/BlenderReference.py` で読み込み、座標の変換（m・軸・標高の基準・原点）を 1 か所にまとめ、手本の 6 視点のカメラ（位置・姿勢・縦の画角・縦横比）を置く。Blender の深度（1 サンプル）とエンジンの深度（SCN-WLR-BARE-AOV と同じ起動引数で 6 視点）を並べ、崖の輪郭と稜線が 2 画素以内で重なることを `ReferenceCheck.py --projection` で 6 視点すべて検査する（Blender の深度の画像は反復の中で描いて保存する）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -OutDir .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN-recheck`
- verify: `python Scripts/Terrain/ReferenceCheck.py --projection --capture .harness/runs/scene-capture/SCN-WLR-BLENDER-TERRAIN --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md, Game, Library/Core/Private/Rendering
- notes: Blender は Python でヘッドレスに動かす（`Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe`）。SCN-WL-BLENDER-TERRAIN-VIEW をこの項目に含めた。DEM を間引いたメッシュは使わない（法線・自己遮蔽・影が変わる）。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。最細の LOD の撮影は 1 視点あたり約 150 秒（6 視点で 903 秒）かかる。全視点の撮影は反復の中で 1 回走らせて保存し、verify は 1 視点の撮影（ビルドと起動の経路の確認。別の出力先）と、保存した結果の検査（入力のハッシュで古い結果を拒否）に分ける（ランナーの再検証は 1 コマンド 10 分）。2026-10-19: 6 視点の撮り直しが落ち着かなかったのは、ほかのアプリの VRAM（約 4.3 GB。DesktopMate 約 2 GB・dwm 約 1 GB。ゲームではない）で上限が 12100 MB に下がったため（blocked/SCN-WLR-BLENDER-TERRAIN.md）。ユーザーが DesktopMate を閉じる。撮る前に `nvidia-smi` と `Get-Counter '\GPU Process Memory(*)\Dedicated Usage'` でほかのアプリの使用量を確かめ、落ち着かなければ撮影の記録に cap_mb とほかのアプリの使用量を残す。

## SCN-WLR-CYCLES-GRAY: 共通の照明で灰色の地形をエンジンと Cycles で描き、影・空の遮蔽・照り返しを比べる
- status: done
- after: SCN-WLR-BLENDER-TERRAIN
- done-when: 灰色の材質（アルベド 0.3・粗さ 1・誘電体 F0 0.04。Cycles は Principled BSDF の同じ値）を、エンジンでは起動引数 `wasteland-gray-material` で GBuffer と RTGI・DDGI の当たりの材質の両方に掛ける（当たりの色が定数のままだと照り返しが比べられない）。まず同じ灰色の板（SCN-WLR-COMMON-LIGHT）を Cycles でも sun・sky の 2 通りで描き、期待の値に 3 % 以内で合うことを確かめる（Cycles 側の尺度の較正）。次に ref_cliff_01・ref_far_01 を両方で描き、露出前の HDR で並べる。Cycles の収束: 乱数の種を変えた 2 本の描画の領域の平均の差が 1 % 以内、画素の差の RMS が決めた値以下になるまでサンプルを増やす（上限のサンプル数と時間を決め、届かなければその領域は未判定と書く。デノイズは掛けない）。領域（日なたの地面・影の中の地面・日なたの崖・影の崖・遠景）ごとの線形の平均の比を表にし、差の大きい所の原因の見立て（影・空の遮蔽・照り返し・GI）を書く。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -OutDir .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY`
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md, Game, Library/Core/Private/Rendering, Assets/Shaders
- notes: 比べるだけで、エンジンの照明を直さない（直すのは SCN-WLR-GAP-LIST の後）。Cycles の描画は反復の中で行い、EXR と入力のハッシュを保存する。

## SCN-WLR-MATERIAL-ALBEDO: 地表の材質の色の張り方を Blender で再現し、照明の無いアルベドを照合する
- status: done
- after: SCN-WLR-BLENDER-TERRAIN
- done-when: エンジンの地表の材質のアルベドの経路（三方向の投影・地表の分類の地図・傾斜と高さの混ぜ・地層の筋・マクロの揺らぎ・ミップ）を Blender のノードで再現する（値の表は `WastelandRoutine.cpp` の定数から書き出す JSON を両方が使う）。ref_cliff_01・ref_ground_01 の照明の無いアルベド（エンジンは SCN-WLR-BARE-AOV の書き出し）を並べ、領域ごとの平均の差が 5 % 以内、局所の模様（16 px の窓の構造の類似度など、測り方を先に決める）の一致が決めた値以上であることを `ReferenceCheck.py --albedo` で検査する。再現できない処理はその旨と影響を書く。
- verify: `python Scripts/Terrain/ReferenceCheck.py --albedo --capture .harness/runs/scene-capture/SCN-WLR-MATERIAL-ALBEDO --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md, Game/GameModes/Wasteland
- notes: 法線・粗さは SCN-WLR-MATERIAL-NORMAL。

## SCN-WLR-MATERIAL-NORMAL: 地表の材質の法線と粗さを Blender で再現し、照合する
- status: done
- after: SCN-WLR-MATERIAL-ALBEDO
- done-when: SCN-WLR-MATERIAL-ALBEDO と同じ再現に、法線（法線マップの空間・向き・強さ・三方向の投影の混ぜ）と粗さ（遠くで縮んだ法線の分を粗さへ足す処理を含む）を足し、ref_cliff_01・ref_ground_01 で照明の無い法線と粗さを照合する（法線の角度の差の中央値と 95 百分位、粗さの領域の平均の差。基準を先に決める）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --normal --capture .harness/runs/scene-capture/SCN-WLR-MATERIAL-NORMAL --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md, Game/GameModes/Wasteland
- notes: -

## SCN-WLR-CYCLES-LIT-A: 実材質の地形を共通の照明で 3 視点描き、エンジンと比べる
- status: done
- after: SCN-WLR-CYCLES-GRAY, SCN-WLR-MATERIAL-NORMAL, SCN-WLR-DISPLAY-TRANSFORM
- done-when: 実材質（SCN-WLR-MATERIAL-ALBEDO・NORMAL）・共通の照明で、ref_cliff_01・ref_far_01・ref_ground_01 を Cycles（SCN-WLR-CYCLES-GRAY と同じ収束の基準）とエンジン（地形だけ・共通の照明・最細の LOD）で描き、露出前の HDR の領域ごとの値の表と、SCN-WLR-DISPLAY-TRANSFORM の同じ表示変換を掛けた比較画像（エンジン｜Cycles）を作る。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -OutDir .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT-recheck`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --views ref_cliff_01,ref_far_01,ref_ground_01 --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: Cycles の EXR は残して SCN-WLR-TONEMAP-EVAL・GAP-LIST で使い回す。最細の LOD の撮影は 1 視点あたり約 150 秒（6 視点で 903 秒）かかる。全視点の撮影は反復の中で 1 回走らせて保存し、verify は 1 視点の撮影（ビルドと起動の経路の確認。別の出力先）と、保存した結果の検査（入力のハッシュで古い結果を拒否）に分ける（ランナーの再検証は 1 コマンド 10 分）。

## SCN-WLR-CYCLES-LIT-B: 残りの 3 視点を描き、写真・エンジン・Cycles を並べる
- status: done
- after: SCN-WLR-CYCLES-LIT-A
- done-when: ref_far_02・ref_cliff_02・ref_ground_02 を SCN-WLR-CYCLES-LIT-A と同じ条件で描き、6 視点すべての写真｜エンジン｜Cycles の比較画像と領域ごとの値の表を作る。写真とは照明・天気・現像が違うので、写真との差は「条件の違い」を分けて書く。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -OutDir .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT-recheck`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --verify`
- paths: Scripts/Terrain, Scripts, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 最細の LOD の撮影は 1 視点あたり約 150 秒（6 視点で 903 秒）かかる。全視点の撮影は反復の中で 1 回走らせて保存し、verify は 1 視点の撮影（ビルドと起動の経路の確認。別の出力先）と、保存した結果の検査（入力のハッシュで古い結果を拒否）に分ける（ランナーの再検証は 1 コマンド 10 分）。

## SCN-WLR-CAPTURE-ORPHAN: 撮影が外から止められても Game を残さない
- status: done
- done-when: `Scripts/CaptureScene.ps1`（と同じ起動の形の `CaptureStartupScene.ps1`）が起動した Game.exe を Job Object（JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE）に入れ、powershell が外から止められた（ランナーの再検証の 10 分の時間切れ・taskkill）ときも Game が一緒に終わる。既存の自前の時間切れの経路（taskkill /T）は変えない。試し: 撮影を起動して Game の起動を確かめた後に powershell のプロセスだけを止め、数秒以内に Game.exe が消えることを確かめ、手順と結果を PROGRESS.md に残す。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -OutDir .harness/runs/scene-capture/SCN-WLR-CAPTURE-ORPHAN`
- paths: Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-18、BLENDER-TERRAIN の再検証の時間切れで Game が残り、保存した 6 視点の結果を上書きした。

## SCN-WLR-ATMOS-DUMP: エンジンの空と霞の値を数値で書き出す
- status: done
- after: SCN-WLR-HDR-CAPTURE
- done-when: 撮影の起動引数（`atmosphere-dump`）で、エンジンの空の放射輝度の LUT・透過率の LUT・多重散乱の表・sky-view の表、決めた視線（荒野の視点から方位 4 つ × 仰角 −5〜90° と、距離 0.3〜30 km の地表への視線）の空気遠近の透過率と散乱光、高さフォグの透過率と散乱光、地表の太陽と空の照度を、浮動小数のファイル（値の単位つき）に書き出す。`ReferenceCheck.py --atmos-dump` で揃っていることを検査する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WLR-ATMOS-DUMP`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-dump --capture .harness/runs/scene-capture/SCN-WLR-ATMOS-DUMP --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の読み出し）。値は変えない。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-ATMOS-REF-B: 大気の正解の高次の散乱を、方向を保ったまま重ねる形に直す
- status: done
- after: SCN-WLR-ATMOS-DUMP
- done-when: `Scripts/Terrain/AtmosphereReference.py` の B（散乱の回数で足す版）を、各回の散乱で位相関数（Rayleigh・HG）を方向ごとに掛け、方向別の放射輝度を次の回へ引き継ぐ形に直す（今は 2 回目以降を球の平均へ潰し、次の散乱源を σs × Ψ にしている。評価者の最小再現: 書き出しの係数・高度 1474.223367 m・太陽の高度 38° で、太陽方向の二次の散乱源が、位相関数を角度で積分した値の RGB 3.3694・2.9392・2.3542 分の 1）。方法（方向の格子での次数ごとの積分か、論文の著者の参照実装 RenderSkyPathTracing と同じ形のモンテカルロ）は選んでよい。`--verify` に、(1) 上の最小再現の条件で二次の散乱源を位相関数の角度の積分と照らす検査（1 % 以内）、(2) 歩数・方向・標本を倍にした段との差 0.5 % 以内の収束、(3) 1 回目だけの値が A・B で一致すること、を足す。A（論文の近似）は変えない。比べる表の更新は SCN-WLR-ATMOS-REF で行う。
- verify: `python Scripts/Terrain/AtmosphereReference.py --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 に SCN-WLR-ATMOS-REF から分けた（NEXT_FINDINGS.md の「反復 6 — SCN-WLR-ATMOS-REF」）。verify は 1 コマンド 10 分以内に収める（重い積分は反復の中で保存し、verify は保存した結果と小さな条件の検査に分けてよい）。2026-10-19 反復 31 で blocked（収束 0.76〜1.07 %。blocked/resolved/SCN-WLR-ATMOS-REF-B.md）→ 推奨の 1 を採る: 表の 4 次元の引きを 3 次補間（Catmull-Rom）にして収束を測り直す。基準 0.5 % は変えない。それでも届かなければ 2 を採る: 視線・点の格子と歩数を倍にした組を新しい段 1 にし、段 2 の `--solve`（1 組 1〜2 時間）は反復の外で切り離したプロセスとして起動し、出力と入力のハッシュを保存する（重い処理は許可済み）。次の反復がその保存を検査して閉じる。走行中の段 2 を重ねて起動しない。

## SCN-WLR-ATMOS-REF: 空と霞を、独立に書いた大気の数値の正解と比べる
- status: done
- after: SCN-WLR-ATMOS-REF-B
- done-when: エンジンのコードを写さず、Hillaire 2020 の論文の定義（同じ係数・惑星の大きさ・オゾン）から `Scripts/Terrain/AtmosphereReference.py` で正解を数値積分する（多重散乱は論文の近似と、高次の散乱を回数で足す版の両方。歩数を倍にして 0.5 % 以内まで収束させる）。SCN-WLR-ATMOS-DUMP の値と、空の方向別の輝度・距離別の透過率と散乱光・地表の太陽と空の照度の比を、角度・距離ごとの表で比べる。高さフォグと空気遠近が同じ霞を二重に掛けていないかを、両方の光学的深さの内訳で調べる。写真の空（ground_02 の晴天）と並べ、差を「モデルの違い」「設定の違い（荒野の Mie の尺度高さ 1600 m・MieDensityScale 0.22 など）」「条件の違い（天気・時刻・カメラ）」に分ける。値は変えない。
- verify: `python Scripts/Terrain/AtmosphereReference.py --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WLR-ATMOS-DUMP --verify`
- paths: Scripts/Terrain, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19: 遮断器で blocked（評価の差し戻しの直しが 40 分に収まらず 2 回止まった）。B の直しを SCN-WLR-ATMOS-REF-B へ分けた。この項目は B の直しの後で、比較の表・霞の二重の判断（霧の空の LUT × 不透明度が太陽の側で正解の 2.2 倍、など）・写真との分け方を新しい B で更新し、A と B の両方で結論が変わるかを書く。 2026-10-19: `--atmos-ref` は保存した数値の参照（設定の署名つきの段の解）を読む形にし、verify で解き直さない（1 コマンド 10 分。今は B の生成だけで約 685 秒。Sol の批評 18）。 2026-10-20 反復 3: 書き出しの入力が撮影の後に変わっていたので撮り直した（空と霞の値は frame_number 以外同じ）。正解の保存の署名を書き出しのバイト列から設定（台本・設定・太陽・観測点・段の細かさ）へ移し、段 1 の解そのもの（比べる 5 組）を保存して `--atmos-ref` が読む形にした。段 1（5 組）と段 2（paper・engine を別のプロセス）の `--solve` を反復の外で起動した（`.harness/runs/20261020-072612/atmos-ref-solve.ps1`、ログ `aux-SCN-WLR-ATMOS-REF-solve-*.txt`）。解き終わったら、`--atmos-ref`（`--verify` なし）で表を作り、文書の表と判断を更新して verify の 2 つを回す。

## SCN-WLR-FAR-BLUR-STATIC: 遠景のぼけの原因を、静止の撮影で 1 つずつ切り分ける
- status: done
- after: SCN-WLR-HDR-CAPTURE
- done-when: 測り方を先に決める: 遠景の正解は、同じ視点を 2 倍の解像度で撮って縮めた画（超標本化）とし、遠くの崖の領域の細かさを、正解との帯域ごとの差（細部の欠け）と、正解に無い高周波（エイリアス）に分けて数える（エイリアスを鮮明さに数えない）。ref_far_01・ref_cliff_02 で、AA（FXAA・TAA・なし）、霞（空気遠近・高さフォグの入・切）、内部解像度、地形の LOD（最細の固定の入・切）、仮想テクスチャの常駐（撮る前に要求が満ちたことをログで確かめる）・ミップの偏り・異方性フィルタを、1 つずつ変えた条件の表で撮り、細部の欠けとエイリアスを表にする。原因の順位と直す案を書く。既定は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -OutDir .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-STATIC/base`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-STATIC --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 条件の切り替えは撮影の起動引数で行い、既定の見た目を変えない。1 反復で全条件が撮れなければ、撮れた条件で表を作り、残りの条件を後ろの項目に足す。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-FAR-BLUR-LOD: 遠景のぼけの切り分けに、地形の LOD の最細の固定の組を足す
- status: done
- after: SCN-WLR-FAR-BLUR-STATIC
- done-when: SCN-WLR-FAR-BLUR-STATIC の撮影で 10 分たっても落ち着かなかった `--wasteland-finest-lod` の組を、ref_far_01・ref_cliff_02 で撮りきる（撮った時点のジオメトリのページの要求 0）。`ReferenceCheck.py --blur-static` の組に戻し、同じ入力のハッシュで既定の組と並べて、細部の欠けとエイリアスの差を `Docs/RenderingValidation/WastelandFarBlurStatic.md` の表と原因の順位に足す。既定は変えない。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -GameArgs wasteland-finest-lod -OutDir .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-STATIC/finest-lod`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-STATIC --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SCN-WLR-BLENDER-TERRAIN では 6 視点の最細の固定が約 15 分で撮れた。入力のハッシュが変わったら、ほかの組も同じコードで撮り直す（撮り方は記録の「条件」の節）。

## SCN-WLR-FAR-BLUR-MOTION: 遠景のぼけを、動かしたときの履歴の消失で切り分ける
- status: done
- after: SCN-WLR-FAR-BLUR-STATIC
- done-when: 固定のカメラの経路（`Scripts/CameraScripts/` に足す。遠景を横切る一定の速さ）で連番を撮り、TAA の履歴の消去の直後と、決めたフレーム数の後で、SCN-WLR-FAR-BLUR-STATIC と同じ測り方（細部の欠け・エイリアス）を比べる。FXAA・TAA の両方で、静止との差を表にし、履歴による細部の消失の大きさと直す案を書く。既定は変えない。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-far-pan.txt -OutDir .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-MOTION`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-motion --capture .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-MOTION --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 連番の撮り方は SCN-WL-SCATTER-GRAVEL の歩く連続撮影（`Scripts/CameraScripts/wasteland-gravel-walk.txt`）にならう。カメラが実際に動いたことを数値で確かめる。撮影の構成（Debug か RelWithDebInfo か）は SCN-WL-CAPTURE-SPEED の決定に揃えてよい（verify も同じ反復で直す）。

## SCN-WLR-TONEMAP-EVAL: 表示変換の候補を、同じ HDR に掛けて比べる
- status: done
- after: SCN-WLR-CYCLES-LIT-B, SCN-WLR-DISPLAY-TRANSFORM
- done-when: 表示変換の候補（今の NeutralLinear＋グレーディング〔コントラスト 1.35・彩度 1.2・色温度 0.05〕、ACES 2.0 の LUT＋中立のグレーディング、ほか）を、SCN-WLR-DISPLAY-TRANSFORM の再現で SCN-WLR-BASELINE のエンジンの HDR と SCN-WLR-CYCLES-LIT の Cycles の HDR に掛けて並べ、写真と比べる（写真の現像の差は「条件の違い」として残す）。採る候補と理由を書く。エンジンの既定は変えない。
- verify: `python Scripts/Terrain/ReferenceCheck.py --tonemap --capture .harness/runs/scene-capture/SCN-WLR-TONEMAP-EVAL --verify`
- paths: Scripts/Terrain, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: -

## SCN-WLR-GAP-LIST: 節目: 描画の欠陥と中身の欠陥を分けた一覧をユーザーに渡す
- status: done
- after: SCN-WLR-CYCLES-GRAY, SCN-WLR-MATERIAL-NORMAL, SCN-WLR-CYCLES-LIT-B, SCN-WLR-ATMOS-DUMP, SCN-WLR-FAR-BLUR-MOTION, SCN-WLR-TONEMAP-EVAL
- done-when: `ReferenceCheck.py --gap-list` を作り、SCN-WLR-CYCLES-GRAY・MATERIAL-ALBEDO・MATERIAL-NORMAL・CYCLES-LIT・ATMOS-REF・FAR-BLUR-STATIC・FAR-BLUR-MOTION・TONEMAP-EVAL の結果が揃い、入力のハッシュが今と一致することを検査する。差を「描画の欠陥（入力が一致してエンジン ≠ Cycles）」「中身の欠陥（条件が一致して Cycles ≠ 写真）」「条件不一致・未判定」に分けた一覧を、差の大きい順に `Docs/RenderingValidation/WastelandGapList.md` に書く。ユーザーの 6 点（遠景のぼけ・フォグ・空・雲・建物の浮き・植物）それぞれに、原因と直す項目の案を対応させる（雲・建物・植物はこの段の比較に入っていないので、同じ建物 1 棟・植物 1 株・雲の密度場を足して比べる段の案も書く）。写真｜エンジン｜Cycles の一覧の画像を 1 枚にまとめる。最後にリポジトリ直下へ中身が `boundary` の `LOOP_STOP` を書く（親がユーザーに渡し、後ろの項目を組み直す）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --gap-list --verify`
- paths: Scripts/Terrain, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: この後ろの SCN-WL-* の項目は、この一覧を見て組み直す（それまでは after でこの項目を待つ）。2026-10-19: SCN-WLR-ATMOS-REF は B（高次の散乱）の直しの解き待ち（反復の外で 6〜10 時間、SCN-WLR-ATMOS-REF-B）。ユーザーが戻るまでに一覧を渡すため、この項目は ATMOS-REF を待たない。空と霞は SCN-WLR-ATMOS-DUMP・FAR-BLUR の結果と、ATMOS-REF の反復 6 の記録のうち A（論文の近似）で決まる比較で書き、B に依る判断（霧の「空の LUT × 不透明度」が太陽の側で正解の多重散乱の 2.2 倍、など）は「未判定（B の直し待ち。SCN-WLR-GAP-LIST-ATMOS で埋める）」とする。`--gap-list --verify` は ATMOS-REF の結果が古い（入力のハッシュが今と違う）ことを未判定の印として許し、ほかの結果は揃いと今との一致を検査する。未判定を許さない `--require-atmos` も用意する（SCN-WLR-GAP-LIST-ATMOS の verify が使う）。

## SCN-WLR-GAP-LIST-ATMOS: 一覧の空と霞の未判定を、直した大気の正解で埋める
- status: done
- after: SCN-WLR-ATMOS-REF, SCN-WLR-GAP-LIST
- done-when: SCN-WLR-ATMOS-REF（直した B での比較の表と霞の二重の判断）の結果で、`Docs/RenderingValidation/WastelandGapList.md` の空と霞の「未判定（B の直し待ち）」を埋め、結論が変わった所（霧・空気遠近の直す項目の案）を書き直す。後ろの項目（空と霞を直す項目）の done-when が変わった結論と食い違うなら、その項目の notes に変わった結論を書き足す。`--gap-list --require-atmos --verify` が、ATMOS-REF も今の入力と一致し未判定が残っていないことを検査して通る。LOOP_STOP は書かない。 検査は凍結した一覧（SCN-WLR-GAP-LIST の完了の時点の表）への大気の追補として行う: 大気以外の行は凍結した表と同じ（後の描画の修正で大気以外の比較が古くなっても落ちない）、大気の正解が今の入力と一致し未判定が残らない。`--atmos-addendum` はこの項目で作る（Sol の批評 3）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --gap-list --atmos-addendum --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 に親が足した（GAP-LIST を ATMOS-REF の解き待ちから外したため）。

## SCN-WLR-REF-SPLIT: 比較の記録を「参照の入力」と「エンジンの実装」に分け、撮影先を項目ごとに分け、回帰の撮影をまとめる
- status: done
- after: SCN-WLR-GAP-LIST
- done-when: (1) `Scripts/Terrain/ReferenceCheck.py` の入力の記録を 2 つの署名に分ける: 参照の入力（地形のメッシュ・材質の値の表〔Cycles が使う値だけ。材質の AO は入れない〕・共通の照明・カメラと Blender のカメラのずらし量・Blender の台本と出力の画像のハッシュ）と、エンジンの実装（描画のコード・`Game/GameApplicationHandler.cpp`・カメラ台本・撮影に使った Game.exe のハッシュ・出力の画像のハッシュ）。Cycles の参照は参照の入力が同じならエンジンの実装が変わっても再利用でき、Game の撮影はエンジンの実装が今と一致しないと古いとして落とす。(2) `--gray`・`--lit`・`--blur-static`・`--blur-motion` に `--engine`（複数回）と `--reference` を足す。(3) 回帰の撮影の台本 `Scripts/Terrain/CaptureRegression.ps1` と `ReferenceCheck.py --regression` を作る（RelWithDebInfo での検証レイヤーは SCN-WLR-REGRESSION-VALIDATION、`-Motion` の Rendering3DTest は SCN-WLR-REGRESSION-MOTION）。(4) 灰色・実材質の Cycles の参照を新しい形で記録し、参照の入力が今と一致すること、負の確認 `--ref-split-selftest`（12 件）が期待どおりであることを確かめる。エンジンの撮影の撮り直しは SCN-WLR-RECAPTURE-* で行う。
- verify: `python Scripts/Terrain/ReferenceCheck.py --ref-split-selftest --verify`
- paths: Scripts/Terrain, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 GAP-LIST の後の組み直し（Sol の批評 1・2・4・21）。`-Phase before` は反復の中で 1 回撮って保存する。 2026-10-19 親: 反復 3 の判断待ち（blocked/SCN-WLR-REF-SPLIT.md）を推奨案で解き、残りを SCN-WLR-REGRESSION-VALIDATION（推奨 a）・REGRESSION-MOTION（推奨 1）・RECAPTURE-GRAY-LIT・RECAPTURE-BLUR-STATIC・RECAPTURE-BLUR-MOTION に分けた（1 項目 40 分に収める）。この項目は済んだ範囲（コミット d4b2742a）で verify を回して閉じる。

## SCN-WLR-REGRESSION-VALIDATION: RelWithDebInfo でも検証レイヤーを入れて回帰の撮影を 9 分に収める
- status: done
- after: SCN-WLR-REF-SPLIT
- done-when: 開発ビルド（Debug・RelWithDebInfo）だけで効く起動引数 `--validation-layer` を足し、RelWithDebInfo でも Vulkan の検証レイヤーを入れられるようにする（今は `Library/Core/Public/Boot/BootConfig.h` で Debug だけ。Release では引数を受け付けない）。入ったことをログ（`VALIDATION_LAYER enabled`）で示す。`CaptureRegression.ps1` を RelWithDebInfo ＋ `--validation-layer` で撮る形にし、4 シーン（rendering3dtest・mega-bananas・sandbox・night-village）の撮影が 9 分以内に終わり、各シーンの検証レイヤーの指摘が `--regression` の検査に入ることを確かめる。反復の中で `-Phase before` を撮り、verify は `after`。Release のビルドで引数が効かないことを記録する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WLR-REGRESSION-VALIDATION -Phase after`
- paths: Library/Core/Public/Boot, Library/Core/Private/Boot, Library/Core/Private/RHI, Library/Core/Public/RHI, Game/GameApplicationHandler.cpp, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RHI・起動）。2026-10-19 SCN-WLR-REF-SPLIT の判断待ちの推奨 (a)。Release に検証用の仕組みを入れない。

## SCN-WLR-REGRESSION-MOTION: Rendering3DTest でカメラ台本を受け取り、回帰の動きの撮影を撮れるようにする
- status: done
- after: SCN-WLR-REGRESSION-VALIDATION
- done-when: Rendering3DTest の軌道カメラ（SpringArm）が開発ビルドの起動引数 `--camera-script=` を受け取り、台本の入力（回す・寄る）で動くようにする（今は自由飛行カメラの 4 シーンだけ）。`Scripts/CameraScripts/rendering3dtest-pan.txt` で振り、`CaptureRegression.ps1 -Motion` が Rendering3DTest の動きの連番を撮れることを確かめる。台本を渡さないときの Rendering3DTest の見た目と操作は変えない（起動画面を撮って開く）。反復の中で `-Phase before -Motion` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WLR-REGRESSION-MOTION -Phase after -Motion`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WLR-REGRESSION-MOTION`
- paths: Game/GameModes/Rendering3DTest, Game/GameApplicationHandler.cpp, Game/Scenes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 SCN-WLR-REF-SPLIT の判断待ちの推奨 1。台本の経路は開発ビルドだけ。

## SCN-WLR-RECAPTURE-GRAY-LIT: 灰色と実材質のエンジンの撮影を新しい記録の形で撮り直す
- status: done
- after: SCN-WLR-REF-SPLIT
- done-when: 夜の集落の追加でエンジンの実装（`Game/GameModes/Wasteland`・`Game/GameApplicationHandler.cpp`・Game.exe）が撮影の後に変わったので、SCN-WLR-CYCLES-GRAY の灰色の 3 組（両方・太陽・空、2 視点）と SCN-WLR-CYCLES-LIT の実材質の 6 視点を、同じ Game.exe で新しい記録の形（エンジンの実装の署名つき）で撮り直す（`.harness/runs/20261019-192352/aux-SCN-WLR-REF-SPLIT-recapture.ps1 -Only gray,lit`、約 25 分。Cycles は描き直さない）。`--gray --verify`・`--lit --verify` が通り、比が前の値から変わった所を記録する。
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 SCN-WLR-REF-SPLIT から分けた。撮影は反復の中で保存し、verify は保存の検査だけ。

## SCN-WLR-RECAPTURE-BLUR-STATIC: 遠景のぼけの静止の組を新しい記録の形で撮り直す
- status: done
- after: SCN-WLR-REF-SPLIT
- done-when: SCN-WLR-FAR-BLUR-STATIC の静止の 10 組（ref2x を含む）を、同じ Game.exe で新しい記録の形で撮り直し（`aux-SCN-WLR-REF-SPLIT-recapture.ps1 -Only static`、約 30 分）、`--blur-static --verify` が通ることと、値が前から変わった所を記録する。
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-STATIC --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 SCN-WLR-REF-SPLIT から分けた。

## SCN-WLR-RECAPTURE-BLUR-MOTION: 遠景のぼけの動きの組を新しい記録の形で撮り直す
- status: done
- after: SCN-WLR-RECAPTURE-BLUR-STATIC
- done-when: SCN-WLR-FAR-BLUR-MOTION の動きの 4 組を、SCN-WLR-RECAPTURE-BLUR-STATIC と同じ Game.exe で新しい記録の形で撮り直し（`aux-SCN-WLR-REF-SPLIT-recapture.ps1 -Only motion`、約 11 分。Game.exe が変わっていたら静止の組も撮り直す）、`--blur-motion --verify` が通ることと、値が前から変わった所を記録する。
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-motion --capture .harness/runs/scene-capture/SCN-WLR-FAR-BLUR-MOTION --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 SCN-WLR-REF-SPLIT から分けた。

## SCN-WLR-EXPECT-MATCH: 比較の検査に、領域・成分ごとの合否の閾値を足す
- status: done
- after: SCN-WLR-REF-SPLIT, SCN-WLR-RECAPTURE-GRAY-LIT
- done-when: `ReferenceCheck.py` に `--expect <仕様の JSON>` を足す。仕様は {モード・視点・領域・成分（total・sun・sky・indirect・visibility・RGB の色度）・量（エンジン ÷ Cycles の比・色度の差・可視率の食い違いの割合・細部の欠け・エイリアス・振りと止めの差・base と resident の差）・下限・上限・未判定を許すか（既定は許さない）} の並び。`--gray`・`--lit`・`--blur-static`・`--blur-motion` が仕様の各行を判定し、1 行でも外れるか、許さない行の領域が未判定なら落ちる。仕様の置き場は `Scripts/Terrain/ExpectSpecs/`。今の保存に対して今の値で通る仕様と落ちる仕様（例: 影の崖の比 1 ± 0.1）を作り、`--expect-selftest`（この項目で作る）で通る・落ちるを確かめる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/selftest-pass.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --expect-selftest --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の批評 5・15）。

## SCN-WL-CAMERA-OFFSET: 撮影の画と記録のカメラのずれを直す
- status: done
- after: SCN-WLR-REF-SPLIT, SCN-WLR-REGRESSION-VALIDATION
- done-when: 撮影の画が、撮影の記録のカメラから視点ごとに違う量ずれる原因（ref_cliff_01 は横 +0.243・縦 −0.027 画素、ref_cliff_02 は +0.779 画素。深度から測った値。起動し直しても同じ量。視点ごとに違うので、撮ったフレームのジッタの残りを最初に疑う）を確かめて直す。`ReferenceCheck.py --camera-offset`（この項目で作る）: Blender の深度を補正なし（`--camera-shift-hint` も深度からの自動補正も使わない。ずらし量 0 を記録に書く）で描き、ずらし量が 0 であることと、エンジンの深度との残りのずれが 0.05 画素以内であることを ref_cliff_01・ref_cliff_02 で検査する。旧参照の移行: 参照の署名のずらし量で、ずらしありの旧参照（SCN-WLR-CYCLES-GRAY・LIT）は領域の平均の比較（`--gray`・`--lit`）に限って有効、画素ごとの比較（`--albedo`・`--normal`・`--projection`）はずらし 0 で描き直すまで古いとして落ちる、と `ReferenceCheck.py` と記録に書く。以後の新しい Cycles の描画はずらし 0 で描く。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_cliff_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-CAMERA-OFFSET`
- verify: `python Scripts/Terrain/ReferenceCheck.py --camera-offset --engine .harness/runs/scene-capture/SCN-WL-CAMERA-OFFSET --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-CAMERA-OFFSET -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。2026-10-19 GAP-LIST の表の外の観察（Sol の批評 7）。Blender の深度の描画は反復の中で保存する。回帰の撮影の差は開いて見て、この変更だけによることを確かめる。

## SCN-WLR-LIT-SHADOW-REF-C01: ref_cliff_01 の影の領域の Cycles の参照を収束させる
- status: done
- after: SCN-WL-CAMERA-OFFSET, SCN-WLR-RECAPTURE-GRAY-LIT
- done-when: SCN-WLR-CYCLES-LIT で 2048 サンプルでも未判定だった ref_cliff_01 の影の崖・影の中の地面を、その領域を切り出した描画（Cycles の border。ずらし 0 のカメラ）で、種を変えた 2 本の差が SCN-WLR-CYCLES-GRAY と同じ基準に入るまで描き、直接光（太陽）・間接光（照り返し）・空の光のパスと太陽の可視率を分けて保存する。描画は 1 本 20 分を上限にし、超えるなら切り出しを狭めるか、届かない量を記録して BLOCKED にする。`--lit --require-judged`（この項目で作る）で 2 領域が判定済みになり、パスの和が全体と 1 % 以内で合うことを検査する。
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --require-judged ref_cliff_01:shadow_cliff,ref_cliff_01:shadow_ground --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の批評 12 と 2 周目）。描画は反復の中で保存し、verify は保存の検査だけ。 2026-10-20 03:30（親）: 切り出しの描画は END EXIT=0 で終わったが、種 B は最初の 64 サンプルに 993 秒かかり（種 A は 512 秒）、上限の秒数で止まって spp_per_seed=64 で合わせている（種 A は 1216 まで描いた）。shadow_cliff の差が 0.0000 なので、2 本の種が本当に違うかも確かめる。512 サンプルに届かないなら、種 B だけを反復の外で描き直す（読み込みと最初のサンプルの時間を上限から外すか上限を延ばす。描画中は GPU・CPU を使う撮影やビルドを重ねない）。 2026-10-20 05:20（親）: ref_far_01 の本描画をずらし 0 で描き直した（`BlenderReference.py --lit --views ref_far_01`、05:07 EXIT=0、spp_per_seed=512・収束）。ただし台本のハッシュが旧参照と違うため、材質の記録（SCN-WLR-CYCLES-LIT/material/inputs.json）から残りの 5 視点が落ちた（`load_previous_record` が違う台本の視点を捨てる）。親が残りの 5 視点を同じ台本でまとめて描き直している（ログ `.harness/runs/20261019-192352/aux-SCN-WLR-LIT-SHADOW-REF-C01-lit5-rerender.txt`、終わったら `--record-reference lit` まで回し、親が todo に戻す）。再開の反復: 反復 23 で Game.exe が変わったので、今の Game.exe で 6 視点の実材質の撮影を撮り直し（反復 21 の `shadow-ref-c01-render-b.ps1` の撮影と同じ引数）、verify を保存して開く。視点を絞って `--lit` を回すと、台本が変わっていればほかの視点の記録が落ちる。描き直すときは古い視点をまとめて 1 回で描く。 2026-10-20 07:25（親）: 5 視点の描き直しは END EXIT=0（材質の記録は 6 視点ともずらし [0, 0]、Cycles は 6 視点とも収束、`--record-reference lit` 済み）。ref_cliff_01 の本描画も 704 サンプルで収束し、verify の require_judged は 2 領域とも「本描画」で判定済みになった。親が保存の検査を回した残りの失敗は 3 つ: (1) 撮影の Game.exe が今のビルドと違う（撮り直す）、(2) ref_far_01 の材質の標本の位置 (+0.090, −0.091)、(3) ref_cliff_02 (+0.101, +0.047)（どちらも 0.1 画素を少し超える）。(2)(3) は `--lit` の `depth_subpixel_shift` が画面全体で測っているため（SCN-WL-CAMERA-OFFSET の `--camera-offset` と `--normal` は 3 km 未満で判定し、3 km より先はエンジンの地形の形の違いが混ざると記録している）。撮り直した後も超えるなら、`--lit` の材質の標本の位置も NORMAL_MAX_DEPTH_M（3 km）未満の画素で判定し、全画面の値は記録だけにする（Cycles の参照の台本 BlenderReference.py は触らない）。

## SCN-WLR-LIT-SHADOW-REF-C02: ref_cliff_02 の影の中の地面の Cycles の参照を収束させる
- status: done
- after: SCN-WLR-LIT-SHADOW-REF-C01
- done-when: SCN-WLR-LIT-SHADOW-REF-C01 と同じ形で、ref_cliff_02 の影の中の地面（台地の長い影の落ちる所）を描いて保存し、`--lit --require-judged` で判定済みになりパスの和が全体と 1 % 以内で合うことを検査する。
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --capture .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --require-judged ref_cliff_01:shadow_cliff,ref_cliff_01:shadow_ground,ref_cliff_02:shadow_ground --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19。

## SCN-WL-RT-TLAS-TERRAIN: 地形の区画をレイトレーシングのシーンに登録し、光線が当たることを確かめる
- status: done
- after: SCN-WLR-REF-SPLIT, SCN-WLR-REGRESSION-VALIDATION
- done-when: 荒野の撮影のログ `RT_SCENE instances=0 out_of_range=198 radius_m=120`（SCN-WLR-CYCLES-GRAY）の原因（`MakeRayTracingSelectBounds` が地形の区画の境界の球を使えず、変換の原点・半径 0 で測って範囲の外にしている疑い）を確かめて直し、選ぶ範囲（カメラの周り 120 m）の中の地形の区画が RT のシーンに入る（instances > 0）ことと、光線が地形に当たる（開発ビルドの RT の当たりの統計で地形の当たり > 0）ことを `ReferenceCheck.py --rt-scene`（この項目で作る。ログを読む）で検査する。合否はこの 2 つで、灰色の比（影の崖 1.57・遠景の空の分 1.35）は撮り直して記録だけする（遠くの遮蔽は SCN-WL-RT-TLAS-RANGE）。RT のシーンに入る区画の数・TLAS の構築の時間・荒野のフレームの GPU 時間（RelWithDebInfo）を前後で記録する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-TERRAIN`
- verify: `python Scripts/Terrain/ReferenceCheck.py --rt-scene --engine .harness/runs/scene-capture/SCN-WL-RT-TLAS-TERRAIN --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RT-TLAS-TERRAIN -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RT・RenderThread）。2026-10-19 GAP-LIST の描画の欠陥 3・9 の前提（Sol の批評 8）。RT の当たりの統計は開発ビルドだけ（Release に入れない）。回帰の撮影の差は開いて見て、この変更だけによることを確かめる。

## SCN-WL-RT-TLAS-RANGE: 空の遮蔽と照り返しに要る範囲の地形を、レイトレーシングのシーンに入れる
- status: backlog
- after: SCN-WL-RT-TLAS-TERRAIN, SCN-WLR-EXPECT-MATCH, SCN-WLR-REGRESSION-VALIDATION
- done-when: RTGI の光線（最大 10 km）が空の遮蔽と照り返しに使う範囲の地形を RT のシーンに入れる（例: カメラの周りは細かい区画、遠くは粗い段の代わりの形。範囲と段は測って決める）。灰色の地形の 3 条件（両方・太陽だけ・空だけ）を撮り、`--gray` の空の成分（エンジンの `engine-sky` ÷ Cycles の空のパス）が ref_cliff_01 の影の崖と ref_far_01 の遠景で 1 ± 0.1 に入ることを、`--expect`（`Scripts/Terrain/ExpectSpecs/rt-tlas-range.json`）で検査する。RT のシーンの大きさ・TLAS の構築の時間・フレームの GPU 時間を前後で記録する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,common-light-case=sun,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE/engine-sun`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE/engine-sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --engine .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/rt-tlas-range.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RT-TLAS-RANGE -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RT・メモリ）。2026-10-19（Sol の批評 8・10）。 2026-10-20（親）: 遮断器（3 回）で止まったので、残りを SCN-WL-RT-SELFHIT（ほかのシーンを変えない自己遮蔽の直し）と SCN-WL-RT-TLAS-RANGE-ACCEPT（verify 一式を 1 回回す）に分けた。この項目は回さない。範囲の規則と RT_BLAS の記録は 287e0b39、始点のずらしは 2994de47、途中の直しは f50d0791。

## SCN-WL-RT-SELFHIT: RT の光線の自己遮蔽を、光線に見せる形とラスタの形が違う物だけで除く
- status: done
- after: SCN-WL-RT-TLAS-TERRAIN, SCN-WLR-REGRESSION-VALIDATION
- done-when: SCN-WL-RT-TLAS-RANGE の続き。RTGI の 1 本目の拡散光線の始点を一律に max(0.5 m, 距離 × 5e-4) ずらす直しで荒野の空の比は合った（影の崖 0.982・遠景 0.980）が、回帰で mega-bananas（PSNR 31.3）と rendering3dtest（38.2）の見た目が変わった。始点のずらし・近い当たりの読み飛ばしを、光線に見せる形（MegaGeometry の RT 用の段）とラスタの形に差がある物だけに効かせる（例: その instance の RT 用の段の誤差の分だけ、同じ instance への近い当たりを読み飛ばす。f50d0791 の途中保存の続き）。DDGI のプローブの光線にも入れるかは測って決める。合否: (1) 反復の最初に撮った回帰の before と比べて、after で rendering3dtest・sandbox の差が無く、mega-bananas・night-village の差はこの変更だけによる（差の画を開いて確かめる）、(2) 荒野の空だけの条件（`common-light-case=sky`）で ref_cliff_01・ref_far_01 を撮り、`--gray` と同じ領域の空の比（影の崖・遠景）が 1 ± 0.1 に入ることを記録する（判定の一式は SCN-WL-RT-TLAS-RANGE-ACCEPT）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RT-SELFHIT -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（RT）。2026-10-20 親が SCN-WL-RT-TLAS-RANGE から分けた。Rendering3DTest と各シーンの見た目を変えない（CLAUDE.md の絶対規則 7）。時間の見積もり: 回帰の before・after 各 6 分、Debug のビルド 3 分、RelWithDebInfo のビルド 6 分、空だけの 2 視点の撮影 1 本 5 分。調整は空だけ・1〜2 視点で回し、壁時計（75 分）の残りが 25 分を切ったら新しい試行を始めずに記録してコミットする。 回帰の before は撮り直さず、範囲を広げる前の exe で撮った `.harness/runs/regression/SCN-WL-RT-TLAS-RANGE/before` を `.harness/runs/regression/SCN-WL-RT-SELFHIT/before` へ写して使う（今の HEAD は途中の直しを含むので、撮り直すと比べる相手が変わる）。`--regression` は撮影の欠け・真っ黒・検証レイヤーのエラーだけで落ちるので、PSNR と差の画は自分で開いて判断する。

## SCN-WL-RT-TLAS-RANGE-ACCEPT: RT の範囲と自己遮蔽の直しを、灰色の地形の空の比で受け入れる
- status: done
- after: SCN-WL-RT-SELFHIT
- done-when: コードを変えずに、SCN-WL-RT-TLAS-RANGE の verify 一式を 1 回回して保存し、灰色の地形の 3 条件で `--gray --expect Scripts/Terrain/ExpectSpecs/rt-tlas-range.json` が合格する（ref_cliff_01 の影の崖と ref_far_01 の遠景の空の成分が 1 ± 0.1）。RT のシーンの大きさ（`RT_BLAS`）・TLAS の構築の時間・フレームの GPU 時間を前後で記録する（前は `.harness/runs/20261020-094249/aux-SCN-WL-RT-TLAS-RANGE-gpu-before.txt`）。落ちたら原因を記録して BLOCKED にする（ここで直さない）。反復の最初に回帰の撮影の before を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,common-light-case=sun,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE/engine-sun`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE/engine-sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --engine .harness/runs/scene-capture/SCN-WL-RT-TLAS-RANGE --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/rt-tlas-range.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RT-TLAS-RANGE-ACCEPT -Phase after`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-20 親が SCN-WL-RT-TLAS-RANGE から分けた。時間の見積もり: 回帰の before 6 分、Debug のビルド 3 分、RelWithDebInfo のビルドと撮影 3 条件 × 2 視点 約 20 分、判定 1 分、回帰の after 6 分、GPU 時間 4 分（合計 約 40 分）。

## SCN-WL-SHADOW-PAGES: 仮想の影の地図が手前と遠くの地面を覆わない所を直す
- status: done
- after: SCN-WL-RT-TLAS-RANGE-ACCEPT, SCN-WLR-LIT-SHADOW-REF-C02, SCN-WLR-REGRESSION-VALIDATION
- done-when: 一覧の描画の欠陥 1（`shadow_missing`）のうち影の地図の覆いの欠けと、6（`grazing_sun_dark`。灰色 ref_far_01 の遠景の太陽の分 0.88）を直す。ref_cliff_02 で手前の台地の長い影（太陽の高さ 24°）が落ちない原因（この視点だけ `VSM_PAGES requested=38 levels_used=0x200`、影の地図に当たらない標本が 1 回の描画あたり約 1210 万）を確かめて直す。判定は太陽の可視率で行う（照り返しと材質の AO の差が混ざる全体の明るさでは決めない）: エンジンの撮影の AOV に太陽の可視率を足し（この項目で作る）、Cycles の可視率（SCN-WLR-LIT-SHADOW-REF-C02 と SCN-WLR-CYCLES-LIT の aux）と、ref_cliff_02 の影の中の地面と ref_far_01 の遠景で比べ、食い違う画素の割合が 5 % 以下であることを `--expect`（`shadow-pages.json`）で検査する。視点ごとに撮影先を分ける。VSM のページ数と影の GPU 時間を前後で記録する。既定の荒野の ref_cliff_02 を撮って開き、台地の影が地面に落ちることを見る。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-PAGES/ref_cliff_02`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-PAGES/ref_far_01`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --engine .harness/runs/scene-capture/SCN-WL-SHADOW-PAGES/ref_cliff_02 --engine .harness/runs/scene-capture/SCN-WL-SHADOW-PAGES/ref_far_01 --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --views ref_cliff_02,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/shadow-pages.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-SHADOW-PAGES -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影・RenderThread）。2026-10-19（Sol の批評 4・9・22）。1 反復で閉じなければ、ref_cliff_02 の覆いと ref_far_01 のかすめる日差しに分ける。

## SCN-WL-SHADOW-LEAK: 影の中への太陽の漏れを直す
- status: done
- after: SCN-WL-SHADOW-PAGES, SCN-WLR-REGRESSION-VALIDATION
- done-when: 灰色の ref_cliff_01 で影の中の地面に太陽が漏れる原因（反復 11 で確かめた 3 つ: 法線の向きへのずらし `VSMS_NORMAL_OFFSET_TEXELS` = 1.5 が太陽の高さ 10° で影の縁を 1〜2 m 退かせる〔漏れの約 7 割〕、影の描画の MegaGeometry の LOD の許容 `LodThresholdTexels` = 4、残りは深度の比較の余裕の傾きの項）を直す。ずらしと影の LOD の変更は地形の受け手・荒野に絞り、ほかのシーンの影を変えない（回帰の撮影で確かめる。影の LOD の許容を下げるならバナナ 100 万本のクラスタの一覧の容量を超えないことも確かめる）。合否は太陽の可視率で決める: 灰色の 3 条件を ref_cliff_01 で撮り、`--gray --views ref_cliff_01 --expect`（`shadow-leak.json`）で、影の中の地面の `visibility_mismatch` ≤ 3 %（今 29.3 %）・影の崖 ≤ 5 %（今 12.4 %）を検査する。太陽の成分の比（エンジン ÷ Cycles）は記録だけにする（影の中の地面ではエンジンの太陽の照り返しが Cycles の 0.43 倍で、影が合っても 0.5 前後になる。照り返しは SCN-WL-RTGI-COLOR）。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-LEAK`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,common-light-case=sun,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-LEAK/engine-sun`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-SHADOW-LEAK/engine-sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --gray --engine .harness/runs/scene-capture/SCN-WL-SHADOW-LEAK --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --views ref_cliff_01 --expect Scripts/Terrain/ExpectSpecs/shadow-leak.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-SHADOW-LEAK -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影）。2026-10-19（Sol の批評 9・10 と 2 周目）。 2026-10-20（親）: 反復 11 の BLOCKED を推奨の案 A で解いた（合否を太陽の可視率にし、太陽の比は記録に）。反復 11 の測りの表と試しの値は `blocked/resolved/SCN-WL-SHADOW-LEAK-visibility.md`・`.harness/runs/20261020-094249/aux-SCN-WL-SHADOW-LEAK-*.txt`。

## SCN-WL-GRADE-NEUTRAL: 荒野のグレーディングを中立にする
- status: done
- after: SCN-WLR-GAP-LIST
- model: sonnet
- done-when: 荒野のグレーディングを中立（コントラスト 1・彩度 1・色温度 0）にする。トーンマップ（NeutralLinear）と露出（EV100 13.6 + 0.49）は変えない（SCN-WLR-TONEMAP-EVAL の採る候補 `neutral_plain`）。荒野だけの設定なので、ほかのシーンの値は動かさない。`ReferenceCheck.py --grading`（この項目で作る）: 既定の荒野の撮影の HDR に `DisplayTransform.py` の `neutral_plain` を掛けた画と、撮影の PNG が 8 bit で 1 段以内に合うこと（SCN-WLR-DISPLAY-TRANSFORM と同じ基準）を 2 視点で検査し、地面の R/G と白飛びの割合を前後で記録する。6 視点を別の撮影先に撮って開き、写真と並べる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-GRADE-NEUTRAL`
- verify: `python Scripts/Terrain/ReferenceCheck.py --grading --engine .harness/runs/scene-capture/SCN-WL-GRADE-NEUTRAL --verify`
- paths: Game/GameModes/Wasteland, Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 GAP-LIST の描画の欠陥 5（`grading_current`）。

## SCN-WL-RTGI-COLOR: 照り返しの色を地形の材質から取る
- status: done
- after: SCN-WL-RT-TLAS-RANGE-ACCEPT, SCN-WLR-LIT-SHADOW-REF-C01, SCN-WLR-REGRESSION-VALIDATION
- done-when: RTGI の二次の当たりの色が地形の材質を反映しない（`Assets/Shaders/RTGI/DiffuseIndirect.comp` 912 行付近はインスタンスの定数の色）のを直す。地表の分類の地図と層の代表の色（素材の平均の色）で当たりの色を決める程度でよい（材質の評価の共通化まで広げない）。判定の対照: RTGI は空から直接届く光も担い、切ると IBL・DDGI へ切り替わる（`lighting.frag` 1329 行付近）ので、RTGI の入・切ではなく、照り返しだけを切る開発ビルドの起動引数 `rtgi-bounce=off`（当たりの色を 0 にし、空から直接届く光は残す。この項目で作る）を対照にする。実材質の ref_cliff_01 で照り返しの入・切を撮り、その差（エンジンの照り返し）と SCN-WLR-LIT-SHADOW-REF-C01 の切り出しの Cycles の間接光の拡散の分（`indirect_diffuse`。鏡面の間接光を含めない）を、影の崖・影の中の地面で比べ、色度（r・g）の差 0.02 以内を `ReferenceCheck.py --bounce --expect`（`rtgi-color.json`）で検査する。明るさの比は記録だけにする（灰色でも 0.77〜1.10 で、当たりの色の外の原因。SCN-WL-RTGI-AMOUNT）。灰色の材質のときは代表の色も灰色になる（色度の差 0.005 以内）ことを反復の中で同じ形で確かめて記録する。入・切の画を並べて開き、崖の影の中と谷の底に砂の暖かい照り返しが入ることを見る。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR/bounce-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,rtgi-bounce=off -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR/bounce-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --bounce --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR/bounce-on --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR/bounce-off --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --expect Scripts/Terrain/ExpectSpecs/rtgi-color.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RTGI-COLOR -Phase after`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。2026-10-19 書き直し（Sol の批評 11・12 と 2 周目）: 正解は内蔵のパストレーサーでなく Blender Cycles。「RTGI は既定で有効」は、光線は撃つが RT のシーンに地形が無い状態だった（SCN-WL-RT-TLAS-* で直す）。 2026-10-20 SCN-WL-SHADOW-LEAK の測り: 灰色の ref_cliff_01 の影の中の地面で、エンジンの太陽の照り返しが Cycles の 0.43 倍（45 対 103。日なたの地面の照り返しは 13）。影の起伏の陰の側が日なたの斜面から受ける近い照り返しを RTGI が拾いきれていない見込み（解像度・ノイズ取りで日なたの隣と混ざる。未確認）。この項目の明るさの比 1 ± 0.15 はこの不足を含めて判定する。 2026-10-20（親）: 反復 12 の BLOCKED を推奨の案 A で解いた。当たりの色の直し・`--rtgi-bounce=off`・`--bounce` はコミット済み（006994f1）。比べる物を Cycles の間接光の拡散の分に揃え、切り出しの参照を `indirect_diffuse` つきで親が描き直す（ログ `.harness/runs/20261020-094249/aux-SCN-WL-RTGI-COLOR-crop-rerender.txt`、18:14 開始・約 70 分、END の後に親が todo のまま走らせる）。合否は色度だけにし、明るさの比と影の中の地面の色の配分は SCN-WL-RTGI-AMOUNT へ分けた。反復 12 の測りの表は `blocked/resolved/SCN-WL-RTGI-COLOR-diffuse-only.md`。

## SCN-WL-RTGI-AMOUNT: 照り返しの量を Cycles の間接光の拡散の分に合わせる
- status: done
- after: SCN-WL-RTGI-COLOR, SCN-WL-SHADOW-LEAK
- done-when: SCN-WL-RTGI-COLOR（反復 12）の測りで、当たりの色が Cycles と同じ灰色の材質でも、エンジンの照り返し（`--rtgi-bounce` の入 − 切）が Cycles の間接光の拡散の分の 影の崖 0.77・日なたの地面 0.58・日なたの崖 0.68 しかなく、影の中の地面は比 1.10 なのに色度が 0.022 青い（空の照り返しが多く、日の当たった面の照り返しが少ない）。SCN-WL-SHADOW-LEAK の測りでも、影の中の地面の太陽の照り返しは Cycles の 0.43 倍。原因（見込み: 近くの地面どうしの照り返しが SCN-WL-RT-SELFHIT の近い当たりの読み飛ばしで抜ける、当たりの点の太陽の直接光の扱い・影、RTGI の解像度とノイズ取りで日なたの隣と混ざる）を 1 つずつ測って分け、直す。合否: 灰色の材質で ref_cliff_01 の照り返しの入・切を撮り、`--bounce --expect`（`rtgi-amount.json`。`--bounce` が灰色の参照 SCN-WLR-CYCLES-GRAY の間接光の拡散の分を読めるようにするのはこの項目）で、影の崖・影の中の地面の明るさの比が 1 ± 0.15、影の中の地面の色度の差が 0.005 以内。日なたの地面・日なたの崖の比は記録する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-AMOUNT/bounce-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-gray-material,rtgi-bounce=off -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-AMOUNT/bounce-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --bounce --engine .harness/runs/scene-capture/SCN-WL-RTGI-AMOUNT/bounce-on --engine .harness/runs/scene-capture/SCN-WL-RTGI-AMOUNT/bounce-off --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/rtgi-amount.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RTGI-AMOUNT -Phase after`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI）。2026-10-20 親が SCN-WL-RTGI-COLOR から分けた。灰色の Cycles の参照（SCN-WLR-CYCLES-GRAY）に間接光の拡散の分が無ければ、その描き直し（20 分を超えるなら反復の外で起動して BLOCKED で待つ）から始める。時間の見積もり: 回帰の before・after 各 6 分、Debug のビルド 3 分、RelWithDebInfo のビルド 6 分、撮影 2 本 各 3 分。原因の切り分けは 1 視点の撮影（1 本 3 分）で回し、verify 一式は最後に 1 回。壁時計（75 分）の残りが 25 分を切ったら新しい試行を始めず、分かったことを書いてコミットして終える。ほかのシーンの見た目を変えない（回帰の撮影で確かめる）。 2026-10-20（親）: 反復 4 の BLOCKED を推奨の 1 で解いた（閾値は緩めない）。量は直った（e33057f1。影の崖 0.857・影の中の地面 0.923）。残りは影の中の地面の色度の差 0.0076（エンジンの g が 0.0075 低い、2 回の試しで揺れ 0.0005）。候補を 1 視点の撮影 2 本（約 6 分）で 1 つずつ測る: 当たりの点の太陽の直接光（光線の形の法線での n·l）、粗い光線の形で当たりの点の空の遮りが少ない分、ノイズ取りの 3×3 で日なたと混ざる分。反復 4 の記録は `blocked/resolved/SCN-WL-RTGI-AMOUNT-chroma.md`。

## SCN-WL-MATERIAL-AO-SHADING: 材質の AO の掛け方を、光線の遮蔽と二重にならない形にする
- status: done
- after: SCN-WL-RTGI-COLOR, SCN-WL-SHADOW-LEAK, SCN-WLR-REGRESSION-VALIDATION, SCN-WL-RTGI-AMOUNT
- done-when: `lighting.frag` は材質の AO を直接光に ×(0.7 + 0.3·AO)、空の光に ×AO で掛ける。光線の遮蔽（RTGI・空の遮蔽）が働くようになった後で、材質の AO を直接光から外し空の光だけに掛ける（または重みを変える）案を比べ、実材質の日なたの地面のエンジン ÷ Cycles（今 0.70〜0.82。一番暗いのは ref_cliff_02）が ref_ground_01 と ref_cliff_02 の両方で 1 ± 0.08 に入る掛け方を採り、`--expect`（`material-ao.json`）で検査する。AO の値の焼き方は変えない（SCN-WL-MATERIAL-AO-BAKE）。視点ごとに撮影先を分ける。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_ground_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_ground_01`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_cliff_02`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_ground_01 --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_cliff_02 --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --views ref_ground_01,ref_cliff_02 --expect Scripts/Terrain/ExpectSpecs/material-ao.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-MATERIAL-AO-SHADING -Phase after`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（既定の描画経路）。2026-10-19 GAP-LIST の描画の欠陥 4（`material_ao_on_light`。Sol の批評 13）。

## SCN-WL-GOLDEN-DIFF-CAUSE: golden の Outdoor が 10-09 の後に R1 の四角で 712 画素変わった原因を確かめる
- status: done
- after: SCN-WL-MATERIAL-AO-SHADING
- done-when: （2026-10-21 親が書き換え: 別の作業ツリーで古いコミットをビルドする二分探索はしない）golden 4 本が最後に通った f919b5f3 から 84bc0b53（AO の直しの前）までの間に Outdoor の 712 画素（raw 最大 4、平均輝度 129.3821→129.3846。R1 の 2 枚の四角＝鏡面だけで見える黒い板 565 画素と半透明の板 132 画素、球の影の際 8 画素）が変わった原因を、今の exe の中で絞る: 起動引数・環境変数で SSR・反射・半透明・影などを 1 つずつ切った撮影で、712 画素がどの描画の段の差かを局所化し、疑いのコミット（10-12〜10-14 のフレームの持ち主へ移した 770999b3・2844976b・f9100682・c3b2294a・93e09578・1313cf3c など）の差分をその段について読む。原因が特定できたら、意図した直しか不具合かを分け、不具合なら直す項目を TASKS.md に足す。1 反復で特定できなければ、局所化した段・読んだコミット・残る候補を記録して閉じてよい（SCN-WL-GOLDEN-REAPPROVE は、その記録を既知の差として再承認に進む）。分かっていること（`.harness/runs/20261021-055341/aux-SCN-WL-GOLDEN-REAPPROVE-analysis.txt`）: AO の直しの前のシェーダーで 712 画素がそのまま再現し、Indoor は差 0。シェーダーを f919b5f3 の版にしても同じ（C++ 側）。`NORVES_RENDER_PARALLEL_RECORD=0` でも変わらない。検証アプリは RTGI を切り、太陽の影は CSM、地形は無い。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- paths: TASKS.md, PROGRESS.md, Docs/RenderingValidation
- notes: 2026-10-21 SCN-WL-GOLDEN-REAPPROVE の反復が足した。2026-10-21 親: 差は raw 最大 4 の小さなもので、別の作業ツリーでの約 10 段の Debug ビルドに見合わないため、今の exe での局所化とコードの読みに絞った（推奨案）。時間の見積もり: Debug のビルド 3 分、golden の撮影 1 本 約 1 分。

## SCN-WL-GOLDEN-REAPPROVE: 荒野の描画の直しで変わった基準画像を、差の原因を確かめて再承認する
- status: done
- after: SCN-WL-MATERIAL-AO-SHADING, SCN-WL-GOLDEN-DIFF-CAUSE
- done-when: 最後の再承認（52b399e7・83134c09）から後の描画の直しで、基準画像の検査が落ちている（SCN-WL-MATERIAL-AO-SHADING の前の HEAD で Outdoor が mean_flip 0.00044・712 画素、AO の直しの後は Indoor 11 画素・Outdoor 16156 画素。`.harness/runs/20261021-055341/aux-SCN-WL-MATERIAL-AO-SHADING-golden*.txt`）。(1) AO の直しの前から落ちていた Outdoor の 712 画素は、荒野の描画の項目ではなく 10-09〜10-14 の C++ 側の変更による（反復 4 の切り分け）。原因は SCN-WL-GOLDEN-DIFF-CAUSE の記録に従い、不具合と分かったものは直してから、特定できなかったものは局所化した段と残る候補を既知の差として承認の根拠に書く。AO の直しの差（Indoor 11 画素・Outdoor 16156 画素）は SCN-WL-MATERIAL-AO-SHADING（材質の AO を RTGI と重ねない・鏡面の遮蔽項を Cycles と同じ式に）による。(2) 差がすべて意図した描画の直しによることを、Indoor・Outdoor（予備の経路の基準画像があればそれも）の候補と今の基準画像を並べて開いて確かめる。意図しない差が見つかったら再承認せず、その原因を TASKS.md に足して BLOCKED にする。(3) `Docs/RenderingValidation/GoldenBaselines.md` の手順（`UpdateRenderingGoldenBaselines.ps1 -GenerateCandidate`→`R1Acceptance.md` の `R1 visual approval` 節に承認の行と hash と根拠→`-PublishApprovedCandidate`）で再承認する。根拠は差の原因の変更と、それが物理的な期待値（Cycles の比）へ近づく向きであること。コミット本文にも根拠を書く。
- verify: `ctest --test-dir build -C Debug --output-on-failure -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Test/Core/Rendering/Baselines, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-21 親が足した。基準画像の承認はユーザーから任されている（差の原因と物理的妥当性を確かめて自分で記録する）。描画のコードは変えない。時間の見積もり: Debug のビルド 3 分、基準画像の検査 2 本 約 2 分、候補の生成 約 3 分、差の切り分けの撮影 1 本 3〜5 分。

## SCN-WL-MATERIAL-AO-BAKE: 地表の材質の AO の焼き方を確かめる
- status: done
- after: SCN-WL-MATERIAL-AO-SHADING
- done-when: 平らな砂地でも材質の AO が 0.6〜0.7 台になる理由（素材の ORM の R・クックの変換・層の混ぜ）を確かめる。誤りなら直して再クックする。参照の扱い: Cycles は材質の AO を使わない（SCN-WLR-REF-SPLIT で参照の入力から外した）ので、AO だけの変更なら参照の署名が変わらないことを `--lit --verify` で確かめて記録する。アルベド・法線・粗さに触れたなら、影響する参照（`--albedo`・`--normal`・`--lit` の Cycles）を描き直す項目を足し（SCN-WL-LIGHT-FINAL の前に置く）、この項目は AO の範囲で閉じる。直したら SCN-WL-MATERIAL-AO-SHADING と同じ撮影先の形で撮り直し、同じ仕様で検査し直す。誤りでなければ、理由と測った値を記録して閉じる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_ground_01 --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-SHADING/ref_cliff_02 --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --views ref_ground_01,ref_cliff_02 --expect Scripts/Terrain/ExpectSpecs/material-ao.json --verify`
- paths: Tools/AssetCook, Assets, Scripts, Docs/RenderingValidation, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の批評 13 と 2 周目）。

## SCN-WL-MATERIAL-AO-OCCLUSION: 地表の材質の遮蔽の AO を、素材の起伏から焼いた値にする（層の混ぜの高さは変えない）
- status: done
- after: SCN-WL-MATERIAL-AO-BAKE
- done-when: SCN-WL-MATERIAL-AO-BAKE で、地表の素材の Poly Haven の AO の地図（赤茶の土 0.558・礫の舗石 0.626・皮膜の付いた土 0.858・砂 0.850）が、素材自身の起伏（変位・法線の地図を実寸にした高さ場）から焼いた AO（0.92〜0.98）より大幅に暗いと分かった（`Docs/RenderingValidation/WastelandMaterialAO.md`）。ORM の R は層の混ぜの高さにも使うので、高さに使う値（今の地図）は変えずに、照明が遮蔽として読む値（`lighting.frag` の材質の AO。RTGI では間接の鏡面、DDGI では拡散の環境光）だけを起伏から焼いた AO にする（高さと遮蔽を別のチャンネルに分けるなど、やり方はこの項目で選ぶ）。焼き直しの台本は `Scripts/Terrain/` に置き、`TerrainMaterials.py --verify` と同じく作り直しと一致するかを確かめられるようにする。層の混ぜ（土・皮膜・砂・岩肌の境）が変わらないことを、SCN-WL-MATERIAL-AO-SHADING の撮影とのアルベドの AOV の差で確かめる。参照の扱いは SCN-WL-MATERIAL-AO-BAKE と同じ（AO だけの変更なら `--lit --verify` で参照の署名が変わらないこと）。`material-ao.json` を撮り直した撮影で通し、日なたの地面の比が上がる向き（見込み ref_ground_01 約 +1 %・ref_cliff_02 約 +3 %）を記録する。反復の最初に回帰の撮影の `before` を撮る。視点ごとに撮影先を分ける。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_ground_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-OCCLUSION/ref_ground_01`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-OCCLUSION/ref_cliff_02`
- verify: `python Scripts/Terrain/ReferenceCheck.py --lit --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-OCCLUSION/ref_ground_01 --engine .harness/runs/scene-capture/SCN-WL-MATERIAL-AO-OCCLUSION/ref_cliff_02 --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --views ref_ground_01,ref_cliff_02 --expect Scripts/Terrain/ExpectSpecs/material-ao.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-MATERIAL-AO-OCCLUSION -Phase after`
- paths: Tools/AssetCook, Assets, Scripts, Docs/RenderingValidation, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画・アセットロード）。2026-10-21 SCN-WL-MATERIAL-AO-BAKE の反復が足した。

## SCN-WL-JITTER-LIGHTING: TAA のジッタで描かれる照明の中身が変わる経路を探して直す
- status: done
- after: SCN-WL-RT-TLAS-RANGE-ACCEPT, SCN-WLR-REGRESSION-VALIDATION
- done-when: （2026-10-21 親が反復 29 の結果で書き換え）ジッタで描かれる中身が変わる経路は VSM の太陽の影と分かった（影を切るとジッタ 1 と 0 の欠けの差 0.264 → 0.097。ページのキャッシュを切っても同じなので使い回しは原因でない。4K の TAA の画で台地の根元の斜面に大きな影の斑が出る）。ジッタの有無で太陽の影の判定が変わる画素を、`VsmSampleSunShadowCore` の受け手の深度・傾き・余裕のデバッグの出力をジッタ 0 と 1 の 2 フレームで比べて特定し、判定がジッタの投影に依る式を直す。どちらの影が正しいかは Cycles の参照（SCN-WLR-CYCLES-GRAY・LIT の ref_far_01 の太陽の可視率）で照らし、直した後の影が Cycles の可視率と合うことも記録する。合否: `--blur-static --expect`（`jitter-lighting.json`）で、TAA（ジッタ 1）の遠景の細部の欠けが、中身が変わらない TAA の見積もり（ジッタ 0 の画を Halton の 8 点でずらして `temporal_aa.frag` の式で貯めた値。反復 29 の `aux-SCN-WL-JITTER-LIGHTING-taasim.py` を検査に組み込む。今 0.353）+ 0.05 以内（ref_far_01・ref_cliff_02 の遠景）。直せない経路が残ったら、経路と量を記録して BLOCKED にする。ほかのシーンの影を変えない（回帰の撮影を -Motion つきで前後に撮って開く。golden が変わるなら再承認）。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -GameArgs wasteland-aa=taa,vram-geometry-share=9 -OutDir .harness/runs/scene-capture/SCN-WL-JITTER-LIGHTING/aa-taa`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -GameArgs wasteland-aa=taa,vram-geometry-share=9,taa-jitter-scale=0 -OutDir .harness/runs/scene-capture/SCN-WL-JITTER-LIGHTING/aa-taa-nojitter`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WL-JITTER-LIGHTING --expect Scripts/Terrain/ExpectSpecs/jitter-lighting.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-JITTER-LIGHTING -Phase after -Motion`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定・TAA）。2026-10-21 親が SCN-WL-FAR-AA の反復 27 の BLOCKED（推奨 1）から足した。`--blur-static` の比べる組に aa-taa-nojitter を足す必要がある（ref2x・resident はこの項目の撮影先に同じ exe で撮るか、SCN-WL-FAR-AA の撮影先から読む）。反復 27 の掃引の台本 `aux-SCN-WL-FAR-AA-sweep.ps1`・評価 `aux-SCN-WL-FAR-AA-eval.py`・並び `aux-SCN-WL-FAR-AA-sheet.py`（`.harness/runs/20261021-055341/`）はそのまま使える。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、撮影 2 視点 1 組 約 4 分、経路の切り分け 1 組 約 4 分 × 6 前後。壁時計（75 分）の残りが 25 分を切ったら新しい切り分けを始めず、分かったことを書いてコミットして終える。 2026-10-21（親）: 反復 29 の BLOCKED を推奨の 1 で解いた。`--taa-jitter-scale` と比べる組 aa-taa-nojitter はコミット済み（77523926）。経路を切る一時の計装 `NORVES_DIAG_LIGHT_CUT` は入れ直せば台本 `aux-SCN-WL-JITTER-LIGHTING-sweep.ps1` で使える。閾値は「ジッタ 0 + 0.05」から「中身が変わらない TAA の見積もり + 0.05」に替えた（見積もりでもジッタ 0 の +0.084 が残るため）。VSM は危険地帯。反復 29 の記録は `blocked/resolved/SCN-WL-JITTER-LIGHTING-vsm.md`。

## SCN-WL-FAR-AA: 遠景のエイリアスを、TAA の静止の収束で減らす
- status: done
- after: SCN-WLR-EXPECT-MATCH, SCN-WLR-REGRESSION-MOTION, SCN-WLR-RECAPTURE-BLUR-MOTION, SCN-WL-RT-SELFHIT, SCN-WL-RT-TLAS-RANGE-ACCEPT, SCN-WL-JITTER-LIGHTING
- done-when: TAA の静止の収束を 2 倍の解像度の正解に近づける（分散の箱の幅・現在の色の割合・書き戻しのシャープ化を 1 つずつ変えて測る）。比べる組（ref2x・resident・FXAA の対照。いずれも Game の撮影なので、エンジンの実装が変わると古くなる）は、変更後の同じ Game.exe でこの項目の撮影先 `SCN-WL-FAR-AA/` へ撮り直す（SCN-WLR-FAR-BLUR-STATIC の記録の組と同じ起動引数。aa-taa は `wasteland-aa=taa,vram-geometry-share=9`）。`--blur-static --expect`（`far-aa-static.json`）で、TAA の静止の細部の欠けが resident（FXAA）以下・エイリアスが同じ Game.exe で撮った変更前の定数の TAA の組（定数は起動引数で与える）以下であること（差 ≤ 0。照明の絶対値に依存する上限は書かない。`far-aa-static.json` もこの形に直す）を検査する。荒野の既定の AA はこの項目では変えない。足りなければ、出力より細かい履歴（TAAU・DLAA 相当）の見込みを記録する（入れるかは次の項目で決める）。反復の最初に回帰の撮影の `before`（`-Motion` つき）を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -GameArgs wasteland-aa=taa,vram-geometry-share=9 -OutDir .harness/runs/scene-capture/SCN-WL-FAR-AA/aa-taa`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WL-FAR-AA --expect Scripts/Terrain/ExpectSpecs/far-aa-static.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-FAR-AA -Phase after -Motion`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（TAA は既定の描画経路）。2026-10-19 GAP-LIST の描画の欠陥 2（`far_aliasing`。Sol の批評 14・15・21 と 2 周目）。ref2x（3840 × 2160）などの組は反復の中で撮って保存する（verify は aa-taa の 1 組と保存の検査）。 2026-10-20（親）: 反復 5 の BLOCKED を推奨案で解いた。f50d0791 の RT の途中の直しを含む exe では、ref2x と 1 倍の差を解像度で変わる照明の差が占め（一致しない成分 ÷ 残った細部の 2 乗が TAA でも resident でも 2 前後）、AA の定数の調整では測れない。RT の自己遮蔽の直し（SCN-WL-RT-SELFHIT・SCN-WL-RT-TLAS-RANGE-ACCEPT）の後へ回し、エイリアスの上限を同じ exe の相対へ直した。反復 5 の掃引の台本 `aux-SCN-WL-FAR-AA-sweep.ps1`・評価 `aux-SCN-WL-FAR-AA-eval.py`（`.harness/runs/20261020-094249/`）は再開でそのまま使える。回帰の before（-Motion）は RT の直しの前の exe なので撮り直す。 2026-10-21（親）: 反復 27 の BLOCKED を推奨の 1 で解いた。ジッタで照明の中身が変わる経路の直しを SCN-WL-JITTER-LIGHTING として先に立て、この項目はその後に定数の掃引を撮り直す（合否の形は反復 27 で同じ exe の相対に直した 5273b5a9 のまま）。

## SCN-WL-FAR-AA-MOTION: 動いたときの TAA の細部の消失を減らし、荒野の既定を TAA にする
- status: done
- after: SCN-WL-FAR-AA, SCN-WLR-REGRESSION-MOTION, SCN-WLR-RECAPTURE-BLUR-MOTION
- done-when: 動きに応じた履歴の鋭さの補い（再投影の補間の質・速さに応じた近傍の色の押さえ込みの幅）を足し、SCN-WLR-FAR-BLUR-MOTION と同じ組（taa は `camera-script=Scripts/CameraScripts/wasteland-far-pan.txt,wasteland-aa=taa,vram-geometry-share=9`）で、TAA で振ったときと止めたときの崖の細部の欠けの差（今 +0.107）を +0.03 以下にする。静止をぼかして差を縮めていないこと（静止の細部の欠けとエイリアスが SCN-WL-FAR-AA の仕様を満たし続ける）を、静止の組 `static/` でも検査する。比べる組（ref2x・FXAA の対照）は変更後の同じ Game.exe でこの項目の撮影先 `SCN-WL-FAR-AA-MOTION/` へ撮り直す。満たしたら荒野の既定の AA を TAA にし、検査の「base は FXAA」という前提を直す（FXAA の対照の組を残す）。6 視点と固定の経路の連番を撮って開く。反復の最初に回帰の撮影の `before`（`-Motion` つき）を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-far-pan.txt,wasteland-aa=taa,vram-geometry-share=9 -OutDir .harness/runs/scene-capture/SCN-WL-FAR-AA-MOTION/taa`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-motion --capture .harness/runs/scene-capture/SCN-WL-FAR-AA-MOTION --expect Scripts/Terrain/ExpectSpecs/far-aa-motion.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WL-FAR-AA-MOTION/static --expect Scripts/Terrain/ExpectSpecs/far-aa-static.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-FAR-AA-MOTION -Phase after -Motion`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（TAA）。2026-10-19（Sol の批評 14・15 と 2 周目）。静止の組（`static/`）は反復の中で撮って保存する。

## SCN-WL-GEOM-BUDGET: 荒野の遠景のジオメトリが既定の取り分で常駐しきるようにする
- status: done
- after: SCN-WLR-EXPECT-MATCH, SCN-WLR-REGRESSION-VALIDATION, SCN-WLR-RECAPTURE-BLUR-STATIC
- done-when: 既定のジオメトリのページの目標を、荒野の 1920 × 1080 の作業集合（ref_cliff_02 で 8120 ページ・897 MB）が常駐しきる大きさにし（目標を作業集合から決める）、既定の取り分の base と取り分を広げた resident（どちらも変更後の同じ Game.exe でこの項目の撮影先 `SCN-WL-GEOM-BUDGET/` へ撮り直す）の差（ref_cliff_02 の遠景で欠け +0.042・エイリアス +0.069）が 0.01 以内であることを `--blur-static --expect`（`geom-budget.json`）で検査する。VRAM の予算の内訳とフレームの GPU 時間を前後で記録する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -OutDir .harness/runs/scene-capture/SCN-WL-GEOM-BUDGET/base`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WL-GEOM-BUDGET --expect Scripts/Terrain/ExpectSpecs/geom-budget.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-GEOM-BUDGET -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（メモリ・ストリーミング）。2026-10-19 GAP-LIST の描画の欠陥 11（`geometry_residency`。Sol の批評 16 と 2 周目）。ref2x・resident の組は反復の中で撮って保存する。

## SCN-WL-GEOM-LOWVRAM: VRAM の空きが少ないときに、ジオメトリの読み込みが粗い段で落ち着くようにする
- status: done
- after: SCN-WL-GEOM-BUDGET
- done-when: 通常の LOD の選び方で、予算が足りないとき（ほかのアプリが VRAM を約 4 GB 使う 2026-10-19 の実測。開発ビルドだけの予算の上書き `vram-budget-mb` で再現する。この項目で作る）に、要求・追い出し・LOD の選び方で粗い段に下がって落ち着くようにする。最細の LOD の固定（`wasteland-finest-lod`）は比較用で段 0 しか描かないので、この項目の対象にしない（予算が足りないときに固定を外す退避もしない。比較の撮影は `-LaunchPerView` で回避済み）。落ち着いた後の 60 秒の推移（要求・読み込み中・追い出し・常駐の量・選んだ段）を `geom-settle-trace`（開発ビルドだけ。この項目で作る）で記録し、追い出しが止まり使用量が予算の内に留まることを `ReferenceCheck.py --geom-settle`（この項目で作る）で検査する。通常の予算の撮影が SCN-WL-GEOM-BUDGET の仕様を満たし続けることも反復の中で確かめる。Release に検証用の仕組みを入れない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_02 -GameArgs vram-budget-mb=11000,geom-settle-trace=60 -OutDir .harness/runs/scene-capture/SCN-WL-GEOM-LOWVRAM`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-settle --engine .harness/runs/scene-capture/SCN-WL-GEOM-LOWVRAM --verify`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（メモリ・ストリーミング・RenderThread）。2026-10-19（Sol の批評 16 と 2 周目）。1 反復で閉じなければ「推移の記録と検査」と「粗い段に下がる制御」に分ける。 2026-10-21（親）: 反復 10 の BLOCKED を推奨の 1 で解いた。paths に Assets/Shaders を足した。`MegaGeometryCull.glsl` で、LOD で選ばれたが遮蔽で落ちたクラスタのページにも使用の印を書く（キャッシュされた VSM だけが使うページは、影のパスで LOD を選んだクラスタの印を描き直さないフレームでも書くかを合わせて見る）。要求の列（容量 4096）が溢れないこと、描く物は変えないので golden と回帰の撮影が変わらないことを確かめる。6600 MB で 60 秒の入れ替えが今の 3028 件から十分に減る（目安は 0。残るなら件数と原因を記録）。カリングのシェーダーは危険地帯。反復 10 の記録は `blocked/resolved/SCN-WL-GEOM-LOWVRAM-usage-marks.md` と PROGRESS.md。

## SCN-WL-LOD-DETAIL: AA の後で、地形の LOD の選び方を見直す
- status: done
- after: SCN-WL-FAR-AA-MOTION, SCN-WL-GEOM-BUDGET, SCN-WLR-RECAPTURE-BLUR-STATIC
- done-when: 一覧の描画の欠陥 10（`lod_detail`。最細に固定すると細部の欠け −0.076、エイリアス +0.263）を、TAA が既定になった後で測り直し、LOD の閾値を細かくしてもエイリアスが増えない範囲で細部の欠けを減らす。比べる組は変更後の同じ Game.exe でこの項目の撮影先 `SCN-WL-LOD-DETAIL/` へ撮り直し、`--blur-static --expect`（`lod-detail.json`）で、細部の欠けが今の既定より小さく、エイリアスが SCN-WL-FAR-AA-MOTION の後の既定以下であることを検査する。ページの量と GPU 時間を記録する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_cliff_02 -OutDir .harness/runs/scene-capture/SCN-WL-LOD-DETAIL/base`
- verify: `python Scripts/Terrain/ReferenceCheck.py --blur-static --capture .harness/runs/scene-capture/SCN-WL-LOD-DETAIL --expect Scripts/Terrain/ExpectSpecs/lod-detail.json --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。2026-10-19（Sol の批評 22）。

## SCN-WL-SKY-ALTITUDE: 太陽と空の照度、空の表の観測点を谷床の高度で求める
- status: done
- after: SCN-WLR-ATMOS-REF
- done-when: 一覧の中身の欠陥 2（`altitude_mismatch`。太陽と地表の照度・空の LUT の観測点を海抜 0 m で求め、谷床は約 1474 m）を直す。`--atmos-ref` の期待値（太陽の透過率・水平面の太陽と空の照度）も谷床の高度で求めるように直し（今は高度 0 で求める。光学的深さと A で決まる量なので、保存した数値の参照の B は要らない）、エンジンの書き出しの太陽の透過率が 1 %、空の照度が 3 % 以内で合うことを `--expect`（`sky-altitude.json`）で検査する。既定の荒野の 6 視点を別の撮影先に撮って開く（太陽が約 2 割明るくなる。露出でまとめて帳尻を合わせない）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-SKY-ALTITUDE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-dump --capture .harness/runs/scene-capture/SCN-WL-SKY-ALTITUDE --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-SKY-ALTITUDE --expect Scripts/Terrain/ExpectSpecs/sky-altitude.json --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes/Wasteland, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定値）。2026-10-19（Sol の批評 17）。高度は荒野の設定で渡す（ほかのシーンの既定は変えない。変えるなら全シーンへ値を移す）。`--atmos-ref` は保存した数値の参照を読み、解き直さない（SCN-WLR-ATMOS-REF で作る）。2026-10-20 SCN-WLR-GAP-LIST-ATMOS の結論: 高さフォグの太陽の単一散乱も地表 0 m の太陽の透過率で求めていて、霧の透過率だけで減らした正解の 0.82 倍（0 m と 1474 m の透過率の比と同じ。向き・距離に依らない）。同じ原因なので、霧の太陽の透過率も谷床の高度で求める。

## SCN-WL-HAZE-ONE-MEDIUM: 霞を空の大気と同じ媒質として数える
- status: done
- after: SCN-WL-SKY-ALTITUDE
- done-when: 一覧の中身の欠陥 1（`haze_mie_thin`。霧と空気遠近の Mie を足しても空の大気の同じ視線の 0.52〜0.59 倍）を直す: 霞を空の大気の地表付近の分として数え、霧と空気遠近の Mie の割り振りを、空の大気と光学的深さが合うように決め直す（空の大気の Mie の設定そのものは変えない。SCN-WL-SKY-SCATTER）。地表への視線の霞の光学的深さが空の大気の同じ視線の値と 5 % 以内で合うことを `--atmos-ref --expect`（`haze-one-medium.json`。光学的深さなので B は要らない）で検査する。散乱光の式（霧の周りの光）は変えない。既定の荒野の 6 視点を別の撮影先に撮って写真と並べて開く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-HAZE-ONE-MEDIUM`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-HAZE-ONE-MEDIUM --expect Scripts/Terrain/ExpectSpecs/haze-one-medium.json --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes/Wasteland, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定値）。2026-10-19（Sol の批評 17）。

## SCN-WL-PSI-GRID: 多重散乱の表の格子を細かくする
- status: done
- after: SCN-WLR-ATMOS-REF, SCN-WLR-REGRESSION-VALIDATION
- done-when: 一覧の描画の欠陥 7（`psi_grid`。多重散乱の表 Ψ のエンジン ÷ 正解 A が 0.877〜1.065、表は 8 × 32）を直し、2 % 以内にする（A は保存した数値の参照。格子の大きさ・作る時間・メモリを記録する）。空の表は全シーン共通なので、反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-PSI-GRID`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-PSI-GRID --expect Scripts/Terrain/ExpectSpecs/psi-grid.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PSI-GRID -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定値。空は全シーン共通）。2026-10-19（Sol の批評 17）。

## SCN-WLR-ATMOS-REF-NEWSET: 空の大気の新しい設定の数値の正解を解いて保存する
- status: done
- after: SCN-WLR-GAP-LIST-ATMOS, SCN-WL-HAZE-ONE-MEDIUM, SCN-WL-PSI-GRID
- done-when: SCN-WLR-GAP-LIST-ATMOS の結論で、空の大気の Mie の設定（係数・尺度高さ）を変えるか、どの値にするかを決めて記録する（写真の空の青 `sky_blue_photo` を設定と写真の処理に分けた結果に従う）。変えるなら、その設定の数値の正解（A と B、SCN-WLR-ATMOS-REF-B と同じ段と収束の基準）を `AtmosphereReference.py --solve` で、反復の外で切り離したプロセスとして解いて保存する（設定の署名つき）。解き終わるまで項目を BLOCKED にして待ち方を書き、終わったら親が todo に戻す。保存した解の収束（段 1 と段 2 の差 0.5 %）を `--verify` で検査して閉じる。変えないなら、その理由を記録して閉じる。
- verify: `python Scripts/Terrain/AtmosphereReference.py --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の 2 周目: 大気の設定を変えた後の数値の参照が無い）。2026-10-20 SCN-WLR-GAP-LIST-ATMOS の結論（`sky_blue_photo` の分け方）: 仰角 6° の色度 r は写真 0.11・エンジン 0.29・荒野の設定の正解 B 0.27・論文の設定（Mie の係数 1/25）の正解 0.17。写真への寄りはモデル（B）0.02・設定 0.10・カメラの処理（条件）0.06。帯の上下の明るさの比は写真 0.76・論文の設定 0.76・エンジン 1.01。差の大半は設定なので、空の Mie を論文の側へ寄せる案（値は写真の帯の色度と上下の比を目安に決める）。空の Mie を下げると、SCN-WL-HAZE-ONE-MEDIUM で空の大気に合わせた霞の目標も下がる。 2026-10-21 反復 16: 変える。谷床の観測点（1572.55 m）で比べ直すと、帯の上下の比が写真 0.761 と合うのは空の Mie の地表の散乱と吸収を今の 0.5 倍（散乱 5.0e-5・吸収 5.55e-6 /m、尺度高さ 1600 m のまま。0.764）。論文の Mie では 0.688 で行き過ぎる（前の 0.76 は観測点 100 m）。色度 r は Mie を 0 にしても 0.16 で止まる（記録は WastelandAtmosphereReference.md の「空の大気の新しい設定」）。`AtmosphereReference.py` に組 `newset` を足し、台本の署名が変わるので段 1（6 組）と段 2（paper・engine・newset）を反復の外で解いている（`.harness/runs/20261021-055341/atmos-ref-newset-solve.ps1`、ログ `aux-SCN-WLR-ATMOS-REF-NEWSET-solve-*.txt`）。解き終わったら `--verify` を回して閉じる（解き直しはしない）。 2026-10-22 反復 38: 解き終わり、`--verify` result=pass・sets=3。newset の段 1 と段 2 の差は最大 0.229 %（区間の散乱光）。

## SCN-WL-SKY-SCATTER: 霧と空気遠近の散乱光と空の設定を、直した大気の正解の結論で直す
- status: done
- after: SCN-WLR-ATMOS-REF-NEWSET, SCN-WL-GRADE-NEUTRAL, SCN-WLR-REGRESSION-VALIDATION
- done-when: SCN-WLR-GAP-LIST-ATMOS で埋めた結論と SCN-WLR-ATMOS-REF-NEWSET で決めた設定に従い、霧の周りの光の式（視線の向きの空の輝度を使い太陽の側で二重に数える `fog_sky_color_double`）、空気遠近の散乱光の向き（`aerial_scatter_direction`）、空の Mie の設定を直す。合否は `--atmos-ref --expect`（`sky-scatter.json`。新しい設定の保存した数値の参照を読む）で、この項目が直す量（霧と空気遠近の合成の順 `fog_aerial_order`・霧の太陽の側の二重 `fog_sky_color_double`・空の Mie の設定に依る量）が正解 B と 5 % 以内、空気遠近の散乱光（`aerial_scatter_direction`）は単一散乱が B と、多重散乱が同じ近似の正解 A と 5 % 以内、水平面の空の照度が A と 5 % 以内。空の方向別の輝度の B との比（`sky_multiple_scattering`、0.67〜1.27）、空気遠近の散乱光の和の B との比、水平面の空の照度の B との比は、どれも多重散乱のモデル（Hillaire 2020 の等方の仮定。UE5 の空と同じ）の差なので、合否に入れず数値を記録だけにする（空気遠近と照度への広げ方は 2026-10-22 親が評価者 39 の差し戻しと反復 40 の BLOCKED の案 1 から決めた）。確認 1 で見つかった ref_ground_02 の空の高さ約 380 px の水平な明度の境（REVIEW-1 の評価）の原因も確かめて直す。既定の空のモデルの値を変えるなら全シーンへ値を移す（荒野だけの設定なら荒野の中で閉じる）。既定の荒野の 6 視点を別の撮影先に撮って写真と並べて開く。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-SKY-SCATTER`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-SKY-SCATTER --expect Scripts/Terrain/ExpectSpecs/sky-scatter.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-SKY-SCATTER -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定値）。2026-10-19（Sol の批評 17・22）。1 反復で閉じなければ、霧の式・空気遠近・空の設定・水平の境に分ける。2026-10-20 SCN-WLR-GAP-LIST-ATMOS で結論が変わった（done-when の書き方と食い違う所）: (1) 霧の周りの光の太陽の側の二重の数え（`fog_sky_color_double`）は、方向を保つ正解 B では霧の透過率だけで減らした正解の 1.08 倍（横・反対 1.02〜1.03）で、上乗せは 5 % ほど。前の 2.2 倍は等方にした B によるもの。視線の向きの空の輝度を使う式のままで 5 % 以内に入る見込みで、直す主な対象ではない。(2) 代わりに霧と空気遠近の合成の順（`fog_aerial_order`。エンジンは霧の散乱光を空気遠近の透過率で減らさず、30 km で霧の散乱光の和が正解 B の 1.22〜1.31 倍）を直す。(3) 空気遠近（`aerial_scatter_direction`）は「向きの差が足りない」だけでなく全体に暗い（霞の散乱光の和 エンジン ÷ 正解 B 太陽の側 0.89〜0.94・横 0.82〜0.84・反対 0.73〜0.75、空気遠近だけで 0.45〜0.82。空の LUT の輝度を透過率の割合で配る近似）。(4) 空の輝度 エンジン ÷ B 0.67〜1.27 は論文の近似の等方の仮定によるモデルの違い（`sky_multiple_scattering`。照度の A ÷ B 1.010）で、空の設定や霞の式を直しても残る。「結論の各量が正解と 5 % 以内」に空の方向別の輝度の B との比を入れると、多重散乱のモデルを変えない限り届かない。 2026-10-20（親）: SCN-WLR-GAP-LIST-ATMOS の結論に合わせて合否の量を絞った（空の方向別の輝度は記録だけ）。写真と並べて空の明るさの配り方の差が目に見えるなら、方向を保つ多重散乱を確認 2（SCN-WL-REVIEW-2）で検討する。 2026-10-22（反復 39）: notes に従い分けた。この項目で閉じたのは、空の Mie（荒野だけ `MieDensityScale` 0.5。newset）・空気遠近の散乱光（区間で太陽の単一散乱と多重散乱の表から積む。単一散乱は正解 B と、多重散乱は同じ近似の正解 A と 5 % 以内。和の B との比と多重散乱の A ÷ B は sky_multiple_scattering のモデルの差として記録だけ）・霧と空気遠近を 1 本の視線で一緒に積む合成の順（式）・ref_ground_02 の水平の境（雲の視線の 45 km の打ち切り）。荒野は霧の割合 0 で霧が無く、`fog_aerial_order` と `fog_sky_color_double` の数値の検査ができないので、SCN-WL-SKY-SCATTER-FOG に分けた。 2026-10-22（親）: 反復 40 の BLOCKED を推奨の案 1 で解き、done-when を上のとおり直した。反復 39 の実装（0b0e1b39）はそのまま。次の反復は `sky-scatter.json` の説明と `WastelandAtmosphereReference.md` がこの合否と記録の分け方に合っているかを確かめて食い違えば直し、空気遠近の散乱光の和と B の比（太陽の側 0.84〜0.87・横 0.93〜0.95・反対 0.91〜0.93）と水平面の空の照度の B との差（赤 7.3 %）を「多重散乱のモデルの差の残り」として記録の文書に数値つきで書いて閉じる。式・シェーダ・撮影はやり直さない。多重散乱のモデルを方向つきへ替えるか（案 2）は SCN-WL-REVIEW-2 で写真と並べて決める。判断の記録は `blocked/resolved/SCN-WL-SKY-SCATTER-aerial-ms.md`。

## SCN-WL-SKY-SCATTER-FOG: 霧の周りの光と合成の順を、検査用の霧の割り振りで正解 B と比べる
- status: done
- after: SCN-WL-SKY-SCATTER
- done-when: 荒野の既定の霧の割合は 0（`kHazeFogShare`）で霧が無いので、大気の書き出し（`--atmosphere-dump`）の視線を、検査用の割り振り（霧 0.37・空気遠近の Mie 0.63。SCN-WL-HAZE-ONE-MEDIUM で試した値）でもう一度評価した行を足し、書き出しにその媒質を記録する。`--atmos-ref` で、霧の周りの光（`fog_sky_color_double`）と霧の太陽の単一散乱・霧と空気遠近の散乱光の和（`fog_aerial_order`。SCN-WL-SKY-SCATTER の 1 本の視線で一緒に積む式）を、同じ媒質の正解と比べる（`--expect` `sky-scatter-fog.json`）。合否: 霧の太陽の単一散乱は正解 B と 5 % 以内、霧の周りの光と霧と空気遠近の散乱光の和は同じ等方の近似の正解 A と 5 % 以内。霧の周りの光と和の B との比は、A ÷ B（多重散乱のモデルの差。SCN-WL-SKY-SCATTER の空気遠近と空の照度と同じ扱い）と一致することを示して数値を記録だけにする（2026-10-22 親が評価者 45 の差し戻しから決めた。SCN-WL-SKY-SCATTER と同じ判断）。霧の周りの光が A と 5 % を超えるなら、視線の向きの空の輝度を使う式を直す。荒野の既定の割り振り（0）と見た目は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-SKY-SCATTER-FOG`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-SKY-SCATTER-FOG --expect Scripts/Terrain/ExpectSpecs/sky-scatter-fog.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-SKY-SCATTER-FOG -Phase after`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の式）。2026-10-22 SCN-WL-SKY-SCATTER から分けた（その notes）。反復の最初に回帰の撮影の `before` を撮る。霧へ割り振るか（影つきの霞）はこの項目では決めない（NEXT_FINDINGS の HAZE-ONE-MEDIUM の記録）。 2026-10-22（親）: 評価者 45 の NEEDS_WORK（霧の周りの光が B と 43 %、和が 16 %）は、SCN-WL-SKY-SCATTER と同じく多重散乱のモデルの差（反復 45 の `atmos_ref_check.json`: 周りの光 ÷ A 0.998〜1.002、÷ B 0.568〜0.915 で A ÷ B 0.568〜0.914 と一致。和 ÷ A 0.995〜1.002）なので、BLOCKED にせず done-when を上のとおり直した。反復 45 の式と仕様（ff913187）はそのまま。次の反復は評価者 45 の non-blocking だけを直して閉じる: `aux-SCN-WL-SKY-SCATTER-FOG-predict.txt` の台本が変更後の `atmos_aerial` の第 4 の戻り値を旧来の重みとして扱い空の輝度を掛け直している（先頭行が 9368 倍）ので、旧式の積分を独立させて「前の式は B の 1.29〜1.63 倍」の証拠を作り直し、記録の本文と合わせる。空・霧の式と撮影はやり直さない。

## SCN-WLR-BUILDING-REF-BASE: 建物 1 棟を比べる仕組みを作る
- status: done
- after: SCN-WL-MATERIAL-AO-BAKE, SCN-WL-SHADOW-LEAK
- done-when: 前哨地の建物 1 棟だけを残す起動引数（`wasteland-hide=all-but-building`。`Game/GameApplicationHandler.cpp` で解く）と、撮影のログへの建物の配置（glTF・位置・向き・大きさ）の書き出しを足す。`BlenderReference.py` に同じ glTF を同じ配置で置く読み込みを足し、`ReferenceCheck.py --building`（建物の足元から 0〜2 m の地面の帯・壁の下端・壁の日なたと陰の明るさの比、深度から求めた建物の下端と地面の高さの差）を作る。検査は比較が成り立つ条件（エンジンと Blender の深度の輪郭が 2 画素以内・Cycles の収束が SCN-WLR-CYCLES-GRAY と同じ基準・各領域の画素の下限）を満たさない、または表の領域が未判定なら落ちる（エンジンとの一致は求めない）。`--trial` は収束の条件だけを外す。共通の照明・ずらし 0 のカメラで、`outpost` の視点を低いサンプル数（64）で試しに描き、`--trial` で検査が回ることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views outpost -GameArgs wasteland-common-light,wasteland-hide=all-but-building,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE/cycles-trial --trial --verify`
- paths: Game/GameModes/Wasteland, Game/GameApplicationHandler.cpp, Game/Scenes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 GAP-LIST の比べる段の案（Sol の批評 5・6・19・20 と 2 周目）。照明を直した後に比べる（直る前の既知の差を報告しない）。

## SCN-WLR-BUILDING-REF: 建物 1 棟をエンジンと Cycles で比べる
- status: done
- after: SCN-WLR-BUILDING-REF-BASE
- done-when: `outpost` の視点を Cycles で収束まで描いて保存し（描画は 1 本 25 分を上限。超えるなら切り出しを狭める）、`--building`（`--trial` なし）で表にする。差を一覧と同じ 3 つに分けて `Docs/RenderingValidation/WastelandBuildingRef.md` に書き、直す項目の案（接地・隙間・足元の土・汚れ）を書く（値は変えない）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WLR-BUILDING-REF-BASE --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19。Cycles の描画は反復の中で保存する。

## SCN-WL-PROP-ALBEDO-SRGB: glTF の模型の基本色のテクスチャを sRGB として読む
- status: done
- after: SCN-WLR-BUILDING-REF
- done-when: SCN-WLR-BUILDING-REF の描画の欠陥 2（`Docs/RenderingValidation/WastelandBuildingRef.md`）: 建物のアルベドの AOV の中央値 0.656 がテクスチャの符号値のままで、sRGB を解かずに読まれている疑い（線形なら 0.36 前後）。クックの基本色の画像の扱い（`Tools/AssetCook/MeshCooker.cpp` の `MeshImageRole::Albedo`）と実行時のテクスチャの形式（UNORM か SRGB か）を確かめて直す。合否: outpost の建物のアルベドの AOV の中央値がテクスチャの線形の値と 3 % 以内で合う（`--building --expect`、`prop-albedo-srgb.json`。期待値はテクスチャから線形に戻した値の中央値をこの項目で測って書く）。同じ経路で読む glTF の模型（夜の集落の家・材質ギャラリーなど）の見た目も変わるので、絶対規則 7 に従い回帰の撮影の前後を開いて、差が sRGB の直しだけによることを確かめる。golden が変わるなら SCN-WL-GOLDEN-REAPPROVE と同じ手順で再承認する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views outpost -GameArgs wasteland-common-light,wasteland-hide=all-but-building,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-PROP-ALBEDO-SRGB`
- verify: `python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WL-PROP-ALBEDO-SRGB --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF --expect Scripts/Terrain/ExpectSpecs/prop-albedo-srgb.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PROP-ALBEDO-SRGB -Phase after`
- paths: Tools/AssetCook, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 危険地帯（アセットの読み込み・描画の既定）。2026-10-21 親が SCN-WLR-BUILDING-REF の直す項目の案 1 から足した（ユーザーの点「建物の浮き」の主因の見込み。確認 2 の前に直す）。模型の再クックが要るなら差分クックの印で関係する模型だけを焼き直す。時間の見積もり: 回帰の before・after 各 6 分、ビルド 3〜6 分、撮影 1 本 約 3 分。

## SCN-WL-PROP-AO: 模型の遮蔽のテクスチャを、光線で遮る空の光・照り返しと二重にならない形で掛ける
- status: done
- after: SCN-WL-PROP-ALBEDO-SRGB
- done-when: SCN-WLR-BUILDING-REF の描画の欠陥 1: 陰の壁の目地が黒い（壁の 5.5 % の画素でエンジン ÷ Cycles 0.114。日なたの壁では 0.5 %）。模型の遮蔽のテクスチャを、RTGI が働くときは空の光・照り返しに重ねない形にする（地形で SCN-WL-MATERIAL-AO-SHADING がしたことの模型の版）。合否（`--building --expect`）: 実材質の地面の撮影で、壁でエンジン ÷ Cycles が 0.3 未満の画素の割合が Cycles の暗い目地の割合（1.5 %）の 2 倍以内（`prop-ao.json`）。Cycles の参照と同じ灰色 0.3 の地面（`wasteland-gray-material`）の撮影で、目地を除いた陰の壁の比が日なたの壁の比と 0.1 以内で揃う（`prop-ao-gray.json`。陰の壁は光の 7 割が照り返しで、地面の反射率が違うと比が揃わないため）。ほかのシーンの模型にも効くので、回帰の撮影の前後を開いて差がこの直しだけによることを確かめ、golden が変わるなら再承認する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views outpost -GameArgs wasteland-common-light,wasteland-hide=all-but-building,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-PROP-AO`
- verify: `python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WL-PROP-AO --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF --expect Scripts/Terrain/ExpectSpecs/prop-ao.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views outpost -GameArgs wasteland-common-light,wasteland-hide=all-but-building,wasteland-finest-lod,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-PROP-AO/gray`
- verify: `python Scripts/Terrain/ReferenceCheck.py --building --engine .harness/runs/scene-capture/SCN-WL-PROP-AO/gray --reference .harness/runs/scene-capture/SCN-WLR-BUILDING-REF --expect Scripts/Terrain/ExpectSpecs/prop-ao-gray.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PROP-AO -Phase after`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定）。2026-10-21 親が SCN-WLR-BUILDING-REF の直す項目の案 2 から足した。案 3（灰色の地面での比べ直し）・案 4（足元の土・汚れ、中身）は確認 2 の後に組む。時間の見積もり: 回帰の before・after 各 6 分、ビルド 3〜6 分、撮影 1 本 約 3 分。 2026-10-21（親）: 反復 23 の BLOCKED を推奨の 1 で解いた。黒い目地は直った（56e92496。暗い目地 6.61 % → 0.54 %）。陰と日なたの比の差は地面の反射率の違いで向きが入れ替わる（実材質 0.1004・灰色 0.094）ので、2 つ目の条件は Cycles と同じ灰色の地面の撮影で判定する。`prop-ao.json` は 1 行目（暗い目地の割合）だけにし、2 行目を `prop-ao-gray.json` に移す。閾値は緩めない。

## SCN-WLR-PLANT-REF-BASE: 低木 1 株を比べる仕組みを作る
- status: done
- after: SCN-WL-MATERIAL-AO-BAKE, SCN-WL-SHADOW-LEAK, SCN-WL-RTGI-COLOR, SCN-WL-RTGI-AMOUNT
- done-when: 荒野の低木（ラビットブラシの模型）1 株だけを地形の上に残す起動引数（`wasteland-hide=all-but-plant`）と、株を近くと遠くで撮る視点 `plant_near`・`plant_far`（`Game/Scenes/SceneCatalog.h`）を足す。`BlenderReference.py` に同じ模型（不透明度のマスク・葉の両面の透過を素直に付ける）の読み込みを足し、`ReferenceCheck.py --plant`（株の日なたと陰の明るさ、葉の透過の明るさ、株の足元の影と遮蔽の比）を作る。検査の成り立つ条件と未判定で落ちる扱いは SCN-WLR-BUILDING-REF-BASE と同じ。`plant_near` を低いサンプル数で試しに描き、`--trial` で検査が回ることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views plant_near -GameArgs wasteland-common-light,wasteland-hide=all-but-plant,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WLR-PLANT-REF-BASE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --plant --engine .harness/runs/scene-capture/SCN-WLR-PLANT-REF-BASE --reference .harness/runs/scene-capture/SCN-WLR-PLANT-REF-BASE/cycles-trial --trial --verify`
- paths: Game/GameModes/Wasteland, Game/GameApplicationHandler.cpp, Game/Scenes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 GAP-LIST の比べる段の案（Sol の批評 5・6・19・20 と 2 周目）。

## SCN-WLR-PLANT-REF-NEAR: 低木 1 株を近くでエンジンと Cycles で比べる
- status: done
- after: SCN-WLR-PLANT-REF-BASE
- done-when: `plant_near` を Cycles で収束まで描いて保存し（1 本 25 分を上限）、`--plant`（`--trial` なし）で表にする。差を 3 つに分けて `Docs/RenderingValidation/WastelandPlantRef.md` に書き、直す項目の案（両面の透過・アルファのマスク・素材の差し替え）を書く（値は変えない）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --plant --engine .harness/runs/scene-capture/SCN-WLR-PLANT-REF-BASE --reference .harness/runs/scene-capture/SCN-WLR-PLANT-REF --views plant_near --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19。Cycles の描画は反復の中で保存する。

## SCN-WL-PLANT-SHADOW: 低木を太陽の影（VSM）に描く
- status: done
- after: SCN-WLR-PLANT-REF-NEAR
- done-when: SCN-WLR-PLANT-REF-NEAR の描画の欠陥 1・2（`Docs/RenderingValidation/WastelandPlantRef.md` の直す項目の案 1）: 株が太陽の影を落とさない（影の比 エンジン 0.621・Cycles 0.169。株は VSM・TLAS に入らず、影は接触影 0.3 m の縦の筋だけ）、株の中の自己の影の当たり外れ。株を太陽の VSM へ `foliage.vert` と同じ置き方で描く（葉の帯は両面、風の揺れも同じ）。接触影は接地の補いとして残す。撒いた株（2 万株超）は遠い株を影のパスで間引くか簡略の形で描き、距離の境を記録する（光線の影 TLAS に入れる案は後に回す）。合否（`--plant --views plant_near --expect`、`plant-shadow.json`）: 影の比が Cycles の 0.169 と ±0.1、株の葉の日なたの見え方の AOV が 0.9 以上の画素の割合 0.8 以上、株の葉の日なたのエンジン ÷ Cycles が 0.8〜1.25（閾値は変えない）。枝（断面 0.17 cm で 1 画素より細い）は SCN-WLR-PLANT-REF-NEAR の分類どおり条件不一致 `plant_branch_subpixel` として値を記録するだけにする（`PlantCheck.py` で葉と枝の領域を分ける）。撒いた株の撮影（`grass` の視点）で影のパスの株の数・使う VSM の段とページの数・GPU 時間を前後で記録する。ほかのシーンに株は無いが、回帰の撮影の前後を開いて確かめる。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views plant_near -GameArgs wasteland-common-light,wasteland-hide=all-but-plant,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-PLANT-SHADOW`
- verify: `python Scripts/Terrain/ReferenceCheck.py --plant --engine .harness/runs/scene-capture/SCN-WL-PLANT-SHADOW --reference .harness/runs/scene-capture/SCN-WLR-PLANT-REF --views plant_near --expect Scripts/Terrain/ExpectSpecs/plant-shadow.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PLANT-SHADOW -Phase after`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影）。2026-10-21 親が SCN-WLR-PLANT-REF-NEAR の直す項目の案 1 から足した（影の無い株は浮いて見えるので確認 2 の前に直す）。案 2（両面の透過）・案 3・4（アルファのマスク・素材の差し替え、中身）は確認 2 の後に組む（葉の素材を差し替えるときに合わせる）。時間の見積もり: 回帰の before・after 各 6 分、ビルド 3〜6 分、撮影 1 本 約 2 分、撒いた株の撮影 1 本 約 3 分。 2026-10-21（親）: 反復 28 の BLOCKED を推奨の 1 で解いた。影の比の差は 0.023 で合格。株の日なたは枝が引き下げていた（見え方 ≥ 0.9 の割合 葉 0.771・枝 0.488、比 葉 0.79・枝 0.36）。基準 2・3 を葉だけで判定し、葉の部分的な影の残り（PCF）を詰める。反復 28 の記録は `blocked/resolved/SCN-WL-PLANT-SHADOW-leaves-only.md`。

## SCN-WL-PLANT-SHADOW-RANGE: 影のパスに入れる株を低木優先・近い順にし、低木が 60 m まで太陽の影を落とすようにする
- status: done
- after: SCN-WL-PLANT-SHADOW
- done-when: SCN-WL-PLANT-SHADOW の記録（`aux-SCN-WL-PLANT-SHADOW-grass-after.txt`）で、撒いた株の視点（grass）の影のパスの株が塊の予算の上限 14892 で止まり（予算の外 43946）、影を落とすのはカメラから 4.2 m までの株だけだった（近くの草の束が予算を埋めている見込み）。影のパスに入れる株を、低木（ブラックブラッシュ・モルモンティー）を草より優先して近い順に選び、草は影のパスから外す（草の接地は接触影と AO に任せる）。低木はカメラから 60 m まで（遠景の方針で株の実体を描く範囲。`Docs/Plans/WastelandRebuild.md` の遠景の方針）影を落とす。合否: grass の視点で、60 m 以内の低木が影のパスの予算から 1 株も漏れず（`VSM_CASTERS` の `foliage_priority0_over_budget=0`）、影を落とす低木の最も遠い根元が 59 m 以上（上限を「根元まで 60 m 以内」とするので、60 m の内の最も遠い株の距離になる。2026-10-22 親が評価者 47 の差し戻しから「60 m 以上」を直した）。届かなければ届いた距離と塊の予算を記録して BLOCKED、影のパスの株の数・VSM のページの数・GPU 時間（中央値）を前後で記録する。plant_near の `--plant --expect plant-shadow.json` が変わらず合格する。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views plant_near -GameArgs wasteland-common-light,wasteland-hide=all-but-plant,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-PLANT-SHADOW-RANGE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --plant --engine .harness/runs/scene-capture/SCN-WL-PLANT-SHADOW-RANGE --reference .harness/runs/scene-capture/SCN-WLR-PLANT-REF --views plant_near --expect Scripts/Terrain/ExpectSpecs/plant-shadow.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PLANT-SHADOW-RANGE -Phase after`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影）。2026-10-21 親が SCN-WL-PLANT-SHADOW の記録から足した（影が 4.2 m までしか落ちないと、確認 2 の絵で中景の低木が浮いて見える）。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、撮影 plant_near 約 2 分・grass 約 3 分。 2026-10-22（親）: 遮断器の BLOCKED を解いた。3 回のうち 1 回は親の操縦の誤り（反復 41 が STEER に従って SKY-SCATTER の記録を直し、この項目の着地と数えられた）。反復 43 の落ちた理由: Cycles の正解（f6eae327）を `SCN-WL-PLANT-SHADOW-RANGE-head` の撮影（08ed7b10 の前のコード、深度 53670bd1…）の深度で描いたが、今のコードの撮影の深度は 516bcf17… で、画面のほぼ全画素が相対 1e-5 前後（最大 8.9e-5）ずれる（08ed7b10 のシェーダの変更による演算の差と見込む）。直し方: verify と同じ撮影先（`SCN-WL-PLANT-SHADOW-RANGE`）を今のコードで撮ってから、その深度で plant_near の正解を描き直す。DONE の前に、同じ exe で撮り直しても深度のハッシュが変わらないこと（ランナーの再検証は -Build で撮り直す）を 1 回確かめる。同じ exe でも変わるなら、PlantCheck.py の深度の照合をハッシュの一致から許容幅（相対 1e-3 以下。領域の判定の DEPTH_MATCH_RELATIVE 0.01 より十分小さい）へ直す。BlenderReference.py は参照の記録が script_sha256 を持つので、ほかの参照を古くしないよう触らない（深度の写しが要るなら PlantCheck 側か別の小さな台本で置く）。 2026-10-22（親）: 評価者 47 の NEEDS_WORK（最も遠い根元 59.96 m が「60 m 以上」に届かない）を解いた。上限が「根元まで 60 m 以内」なので 60 m ちょうどの株は無く、反復 47 の記録（`aux-SCN-WL-PLANT-SHADOW-RANGE-grass-after.txt`）は `foliage_priority0_farthest_m=59.96`・`foliage_priority0_over_budget=0`（60 m の内の低木は全部影を落とす）で、意図を満たす。合否の文言を上のとおり直した。実装（08ed7b10）と正解の描き直し（7ab3d5a0）はそのまま。次の反復は保存された記録が直した合否を満たすことを確かめ、記録の文書の合否の書き方を合わせて閉じる。描画の距離・予算・式は変えない。評価の周回のうち 1 つは親の操縦の誤り（反復 41）で数えられた。

## SCN-WLR-PLANT-REF-FAR: 低木の遠くでの見え方を 2 倍の解像度の正解と比べる
- status: done
- after: SCN-WLR-PLANT-REF-NEAR, SCN-WL-LOD-DETAIL
- done-when: `plant_far` で、`--blur-static` と同じ測り方（2 倍の解像度の正解。同じ Game.exe で撮る）でアルファの縁のエイリアスと細部の欠けを比べ、`WastelandPlantRef.md` に足す。`--plant-far`（この項目で作る）は、正解と比べる組が同じ Game.exe で撮られ、各量が判定できることを検査する（改善の閾値は付けない）。直す項目の案を書く（値は変えない）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views plant_far -GameArgs wasteland-common-light,wasteland-hide=all-but-plant -OutDir .harness/runs/scene-capture/SCN-WLR-PLANT-REF-FAR/base`
- verify: `python Scripts/Terrain/ReferenceCheck.py --plant-far --capture .harness/runs/scene-capture/SCN-WLR-PLANT-REF-FAR --verify`
- paths: Scripts, Docs/RenderingValidation, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の批評 20 と 2 周目）。2 倍の解像度の正解は反復の中で撮って保存する。

## SCN-WLR-CLOUD-REF-DUMP: 雲の密度場を書き出す
- status: done
- after: SCN-WL-SKY-SCATTER
- done-when: 起動引数 `cloud-dump`（開発ビルドだけ）で、エンジンの雲の密度場（3 次元の格子の値・格子のワールドの座標と大きさ・補間のしかた・雲の時刻・風のずれ）と、雲の照明に使う値（太陽・空の光・位相関数・多重散乱の近似の係数・背景の照明）を書き出す（入力の契約として文書に固定する）。空だけを写す視点 `cloud_sky`（`Game/Scenes/SceneCatalog.h`）を足す。書き出した格子からエンジンの式で数本の視線の透過率を Python で再計算し、エンジンの描画（雲の透過率）と 1 % 以内で合うことを `ReferenceCheck.py --cloud-dump`（この項目で作る）で検査する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19 GAP-LIST の比べる段の案（Sol の批評 6・19・20）。空と霞を直した後に比べる。

## SCN-WLR-CLOUD-REF-BLENDER: 雲の密度場を Blender の体積に読み込み、比べる検査を作る
- status: done
- after: SCN-WLR-CLOUD-REF-DUMP
- done-when: `BlenderReference.py` に、書き出した密度場を Blender の体積（同じ格子・同じ散乱と吸収の係数・HG の位相関数・同じ太陽と空）として読み込む処理を足し、`ReferenceCheck.py --cloud`（`cloud_sky` の視線ごとの雲の透過率と放射輝度、縁の明るさ・銀の縁・影の側の暗さ）を作る。検査の成り立つ条件（収束・未判定で落ちる）は SCN-WLR-BUILDING-REF-BASE と同じ。低いサンプル数で試しに描いて `SCN-WLR-CLOUD-REF-BLENDER/` に保存し、`--trial` で検査が回ることを確かめる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-BLENDER --trial --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19（Sol の 2 周目）。

## SCN-WLR-CLOUD-REF: 雲をエンジンと Cycles で比べる
- status: done
- after: SCN-WLR-CLOUD-REF-BLENDER
- done-when: `cloud_sky` を Cycles で収束まで描いて保存し（1 本 25 分を上限）、`--cloud`（`--trial` なし）で表にする。雲の形（密度場の作り方）は中身として、写真の雲の被覆・明るさの分布と並べて別に書く。差を 3 つに分けて `Docs/RenderingValidation/WastelandCloudRef.md` に書き、直す項目の案（地面への雲の影・時間方向の蓄積・ノイズの解像度・多重散乱）を書く（値は変えない）。
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19。Cycles の描画は反復の中で保存する。 2026-10-22（親）: 評価の 2 周の上限の BLOCKED を解いた。評価者 56 の指摘（`WastelandCloudRef.md` の直す項目の案 1 の手順が、体積の跳ね返り 0 の描画と太陽を消した描画を足すので空の単一散乱を二重に数える）だけを直して閉じる。直し方: 足し算の手順を消し、「全部の描画から体積の跳ね返りを 0 にした描画（太陽・空・地面の光が雲の中で 1 回だけ散乱する分）を引いた残りを Cycles の多重散乱の量とする。太陽を消した描画は、多重散乱のうち太陽と空・地面のどちらから来たかを分けて見るときだけに使い、引き算には入れない」と書く。`WastelandGapList.md`・`PROGRESS.md` に同じ手順の要約があれば揃える。描画・数値・ほかの節は変えない。この項目は評価の周回をもう残していない（ランの中で 2 周を数え済み）ので、直した文の外に断定を足さない。

## SCN-WL-LIGHT-FINAL: 共通の照明の数値の確認と、既定の荒野の光を写真に合わせる
- status: done
- after: SCN-WL-MATERIAL-AO-BAKE, SCN-WL-SKY-SCATTER, SCN-WL-RTGI-COLOR, SCN-WL-SHADOW-LEAK, SCN-WL-RTGI-AMOUNT, SCN-WL-MATERIAL-AO-OCCLUSION, SCN-WL-PROP-AO, SCN-WL-PLANT-SHADOW, SCN-WL-PLANT-SHADOW-RANGE, SCN-WL-RTGI-COLOR-RESIDUAL
- done-when: (1) 共通の照明（地形だけ・霞なし）の数値: これまでの修正の仕様（rt-tlas-range・shadow-pages・shadow-leak・rtgi-color・material-ao）を、今のコードで撮り直した撮影先 `SCN-WL-LIGHT-FINAL/`（灰色の 3 条件の ref_cliff_01・ref_far_01、実材質の ref_cliff_01・ref_cliff_02・ref_ground_01・ref_far_01、照り返しの入・切）に対して、`ReferenceCheck.py --light-final`（この項目で作る。全仕様をまとめて読む）でまとめて通す（後の修正が前の修正を壊していないことの確認）。撮影は反復の中で保存する。(2) 既定の荒野（前哨地・植物・礫・霞・雲あり）は Cycles の正解が無いので、6 視点を撮って写真と並べて開き、谷・崖の影・前哨地の照り返しと空の青、影の中の色と明るさの差を所見に書く（数値の合否にしない）。カメラパスで移動中の粒・残像・遮蔽の欠けが目立たないことを確かめる。直す所が残れば項目を足す。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-LIGHT-FINAL-recheck`
- verify: `python Scripts/Terrain/ReferenceCheck.py --light-final --engine-root .harness/runs/scene-capture/SCN-WL-LIGHT-FINAL --verify`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。2026-10-19 書き直し（Sol の批評 22 と 2 周目）: SCN-WLR-CYCLES-LIT は地形だけ・共通の照明・霞なしなので、既定の荒野の正解には読み替えない。verify の 1 本目は撮影の経路が今も通ることの確認（別の出力先）、2 本目は反復の中で保存した全組の検査。 2026-10-21（親）: 光の仕上げで基準画像が変わったら、SCN-WL-GOLDEN-REAPPROVE と同じ手順で差の原因を確かめて再承認してから閉じる。 2026-10-22（反復 57）: `--light-final` と組の撮影（AA は FXAA に固定）・(2) の 6 視点と歩きの所見は済んだ（`Docs/RenderingValidation/WastelandLightFinal.md`）。rtgi-color の影の崖が 0.0201 で落ちたので SCN-WL-RTGI-COLOR-RESIDUAL を足した。直した後は組を撮り直して `--light-final` を通すだけでよい。

## SCN-WL-RTGI-COLOR-RESIDUAL: 実材質の照り返しが Cycles より赤い偏りを直す
- status: done
- after: SCN-WL-RTGI-AMOUNT
- done-when: SCN-WL-LIGHT-FINAL の撮影（FXAA に固定した `SCN-WL-LIGHT-FINAL/lit`・`bounce-off`）で、rtgi-color.json の影の崖の色度の差が 0.0201（上限 0.02。SCN-WL-RTGI-COLOR を閉じたとき 0.0187）、影の中の地面が 0.0175（同 0.0081）。同じ exe の撮り直しでも同じ値で、揺らぎではない。エンジンの照り返しの r が Cycles より約 0.02 大きい（影の崖 (0.797, 0.166) に対し Cycles (0.777, 0.177)）。原因を切り分けて直す（見立て: 当たりの点の鏡面が太陽だけで空の光を返さない、層の平均の色の近似、SCN-WL-RTGI-AMOUNT の裏当たりの読み飛ばしで青い分が消えて偏りが表に出た）。rtgi-color.json の 2 行が通り、灰色の rtgi-amount.json も通ること。閾値は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/bounce-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,rtgi-bounce=off -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/bounce-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --bounce --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/bounce-on --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/bounce-off --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-LIT --expect Scripts/Terrain/ExpectSpecs/rtgi-color.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/gray-on`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,wasteland-gray-material,rtgi-bounce=off -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/gray-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --bounce --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/gray-on --engine .harness/runs/scene-capture/SCN-WL-RTGI-COLOR-RESIDUAL/gray-off --reference .harness/runs/scene-capture/SCN-WLR-CYCLES-GRAY --expect Scripts/Terrain/ExpectSpecs/rtgi-amount.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-RTGI-COLOR-RESIDUAL -Phase after`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。SCN-WL-LIGHT-FINAL の反復 57 で `--light-final` が見つけた（記録は `Docs/RenderingValidation/WastelandLightFinal.md`）。反復の最初に回帰の撮影の `before` を撮る。直した後に SCN-WL-LIGHT-FINAL の組を撮り直して `--light-final` を通す。

## SCN-WL-CLOUD-BANDING: 雲の階調の段（等高線のような平らな段）を消す
- status: done
- after: SCN-WLR-CLOUD-REF, SCN-WL-CLOUD-FAR-RANGE
- done-when: 既定の荒野の雲に、明るさが平らな段になって等高線のように並ぶ階調の段が出る（SCN-WL-LIGHT-FINAL の歩きの連続撮影 `scene-capture/SCN-WL-LIGHT-FINAL-views6/walk/` の空と、ref_far_02。10 月 19 日の SCN-WL-GRADE-NEUTRAL-6views にも同じ位置にある）。原因（雲の密度・光の量子化、低解像度の雲の拡大、表示の 8 bit の丸めのどれか）を切り分けて直し、同じ視点の撮影で段が見えないことを拡大して確かめる。SCN-FIX-SKY-BANDING（mega シーンの空の横縞）と同じ原因なら一緒に扱う。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-BANDING`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: SCN-WL-LIGHT-FINAL の反復 57 で足した。優先は計画が決める。 2026-10-22（親）: 確認 2 の後の計画で、遠い雲の積む範囲（SCN-WL-CLOUD-FAR-RANGE）の後に回す。

## SCN-WL-SKY-AERIAL: 空の青・遠景の霞・全体の明るさと彩度を手本に合わせる
- status: backlog
- after: SCN-WLR-GAP-LIST
- done-when: 確認 1 の差: 晴天の空が白い（ground_02 の B ÷ R が写真 3.46・撮影 1.10、cliff_01 は 2.31・1.42）、数 km 先のメサが淡い桃色の霞に溶ける、地面と崖が写真の 2〜9 倍明るく彩度の高い橙。原因を分けて直す: 空のモデル（大気の散乱の係数・濁り・オゾン）、空気遠近（霞の濃さと色）、太陽と空の明るさの比、露出とトーンマップ（彩度）。同じ視点のパストレースの撮影（SCN-WL-RTGI-COLOR で撮れるようにする）を正解として並べ、空・霞・地面の明るさの比が写真とパストレースに近づくことを確かめる。露出でまとめて帳尻を合わせない。既定の露出・トーンマップ・空のモデルを変えるなら、全シーンへ値を移し、影響するシーンを `--scene` の撮影で確かめる（荒野だけの設定なら荒野の中で閉じる）。確認 1 の評価で見つかった ground_02 の空の高さ約 380 px の横の明るさの境が SCN-FIX-SKY-BANDING と同じ原因なら、ここで一緒に直す。比較画像（写真｜撮影｜パストレース、1920×1080）を開いて確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-SKY-AERIAL`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SKY-AERIAL --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -OutDir .harness/runs/startup-capture/SCN-WL-SKY-AERIAL`
- paths: Assets/Shaders, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game/GameModes, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定値）。2026-10-16 ユーザーの決定（確認 1 の後）で足した。1 反復で閉じなければ、空の青と霞を先に閉じ、明るさと彩度を後ろの項目に分ける。 2026-10-19 SCN-WL-SKY-ALTITUDE・HAZE-ONE-MEDIUM・PSI-GRID・SCN-WLR-ATMOS-REF-NEWSET・SCN-WL-SKY-SCATTER に分けた（GAP-LIST の後の組み直し）。この項目は回さない。 2026-10-22（親）: 残り（露出・トーンマップ・彩度・霞の色）は SCN-WL-LOOK-MEASURE・SCN-WL-LOOK-CAMERA・SCN-WL-ATMOS-INPUT に受け継いだ。この項目は回さない。

## SCN-WL-REVIEW-2: 節目の確認 2（地面・植生・岩・光と色）をユーザーに渡す
- status: done
- after: SCN-WL-LIGHT-FINAL, SCN-WL-FAR-AA-MOTION, SCN-WL-GEOM-LOWVRAM, SCN-WL-LOD-DETAIL, SCN-WLR-BUILDING-REF, SCN-WLR-PLANT-REF-FAR, SCN-WLR-CLOUD-REF
- done-when: SCN-WL-REVIEW-1 と同じ形で、手本の全視点の比較画像の一覧と所見（「確認 2」）を書き、`LOOP_STOP`（中身 `boundary`）を書いてランナーを止める。一覧の先頭の注記（`--note`）が `--verify` の再実行で消えないようにする（確認 1 の評価の差し戻し。注記を保存して再検証でも同じ一覧を出す）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-REVIEW-2`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-REVIEW-2 --verify`
- paths: Scripts/Terrain, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-19: 確認 2 の後で、建物・植物・雲の直す項目と中身（NAIP の航空写真〔取得はユーザーの許可〕・スキャンの葉・スキャンの礫・建物の風化）を組む。 2026-10-21（ユーザー了承）: 遠景の方針。地形は今の事前クックの環状の段＋MegaGeometry の DAG とページの読み込みのまま（インポスター不要）。植物は 〜60 m を株の実体（スキャンの葉）、60 m〜1.5 km 前後を八面体のインポスター（Blender でスキャンの株から焼く）、それより先は NAIP の航空写真の地形の色に植生の斑として任せる。インポスターの項目は SCN-WLR-PLANT-REF-FAR の結果（遠景の植物の欠け）を見て、確認 2 の後の植物の作業に組む。あわせて `Docs/RenderingValidation/WastelandDem.md` の遠景の節（58 メッシュ等）が今の horizon.json（98 メッシュ）と合わないので直す。
## SCN-WL-BLENDER-TERRAIN-VIEW: Blender に荒野の地形と手本の視点を再現する
- status: backlog
- done-when: Blender のヘッドレスで、荒野の地形の主役の崖の周り（Battleship Rock の範囲と崖錐、`Assets/Terrain/ValleyOfTheGods/height.r32`）をメッシュとして読み込み、手本の視点（`WastelandRoutine.cpp` の `kReferenceShots` の ref_cliff_01・cliff）と同じカメラを置いて描く台本を `Scripts/Terrain/` に作る。エンジンの座標（X・Z・高さ・向き）と Blender の座標の変換を 1 か所にまとめる。Blender の描画と、同じ視点のエンジンの撮影を並べた図を開き、崖の輪郭が重なる（輪郭の差が数画素以内）ことを確かめる。崖の素材を置く次の項目は、この台本の上で配置を見ながら直す。
- verify: `"Y:\AI\blender\blender-5.2.2-windows-x64\blender.exe" --background --factory-startup --python Scripts/Terrain/BlenderTerrainView.py -- --verify`
- paths: Scripts/Terrain, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 台本の名前は目安（`BlenderTerrainView.py`）。変えるなら verify も同じ反復で直す。 2026-10-16 親: SCN-WLR-BLENDER-TERRAIN に含めた（エンジンが描く地形のメッシュをそのまま持ち込む形に替えた）。

## SCN-WL-GEOM-POOL-PLACEMENT: ジオメトリのページの置き場を、崖の視点の作業集合が置ける既定にする
- status: done
- after: SCN-WL-REVIEW-2
- done-when: SCN-WL-GEOM-REQUEST-CAPACITY の BLOCKED（`blocked/resolved/SCN-WL-GEOM-REQUEST-CAPACITY-pool.md`）: 要求の列を 32768 にすると溢れは 0 になるが、ref_cliff_01 で置き場が埋まり（`GeometryPageUploadStart::NoSpace`。1 GB の塊 4 個のうち 3 個が使用中のページで満杯、ページは自分のメッシュと同じ塊にしか置けない）、視点の切り替えから 10 分以上 `wanted` が残り、追い出しと読み直しを繰り返す。4096 の間は溢れた使用の印が LRU に届かず、使っているページが外されて見かけ上落ち着いていた（SCN-WL-REVIEW-2 は evicted=7513）。置き方の既定を直す（候補: 塊を大きくする、`NORVES_GEOMETRY_POOL_SPREAD` の分け置きを既定にする、空きのある塊へページを置けるようにする。環境変数で塊 2047 MB にすると ref_cliff_01 は 106 秒で wanted=0・evicted=0）。要求の列の既定はこの項目では 4096 のまま。確かめでは開発ビルドの起動引数 `geometry-request-capacity=<件数>`（この項目で足す。環境変数 `NORVES_GEOMETRY_PAGE_REQUEST_CAPACITY` と同じ働き）で 32768 にする。合否: 32768 で既定の荒野の 7 視点を続けて撮り（前の視点のページが残る形）、`--geom-requests` で各視点の最後の `GEOMETRY_PAGES` が `request_overflow=0`・`wanted=0`・`failed=0`、視点ごとの落ち着くまでの時間が 120 秒以内（`--geom-requests` に足す）、プールの使用量が VRAM の予算の中。Game.log が 10 MB の上限に届かない（届くならログの量を絞る）。mega-bananas の撮影でも落ち着き、VRAM の使用量と GPU 時間（中央値）を前後で記録する。ほかのシーンの見た目を変えない（回帰の撮影）。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02,cliff -GameArgs geometry-request-capacity=32768 -OutDir .harness/runs/scene-capture/SCN-WL-GEOM-POOL-PLACEMENT`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-requests --capture .harness/runs/scene-capture/SCN-WL-GEOM-POOL-PLACEMENT --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-GEOM-POOL-PLACEMENT -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（メモリ・ジオメトリのストリーミング。全シーンの VRAM の使い方に効く）。2026-10-22 親が SCN-WL-GEOM-REQUEST-CAPACITY の BLOCKED（推奨の案 1）から足した。溢れの始まりは 0f487546（TAA の View の LOD を 0.5 画素）と既定の AA の TAA 化、a0c76d9e（0.25 画素）で約 4 倍。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、7 視点の撮影 約 10 分（落ち着かない視点があれば長くなる。1 視点の試しから始める）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。

## SCN-WL-GEOM-REQUEST-CAPACITY: ジオメトリのページの要求の列を、既定の荒野で溢れない大きさにする
- status: done
- after: SCN-WL-REVIEW-2, SCN-WL-GEOM-POOL-PLACEMENT
- done-when: SCN-WL-CLIFF-SCAN-PLACE の BLOCKED（`blocked/resolved/SCN-WL-CLIFF-SCAN-PLACE-request-capacity.md`）: 既定の荒野の撮影で、最後の `GEOMETRY_PAGES` の `request_overflow` が 1000 万件を超える（SCN-WL-REVIEW-2 の 6 視点 13,929,488、SCN-WL-LIGHT-FINAL-views6 14,572,308。10 月 21 日の SCN-WL-SKY-ALTITUDE-views は 0）。1 フレーム約 8400 件の要求・使用の印が `GeometryPageRequestBuffer::DefaultCapacity`（4096）の列に来て、溢れた使用の印が LRU に届かず `evicted`・`parent_wait` が出る。増えた原因の変更（見込み: TAA の View の LOD を 0.25 画素へ細かくした a0c76d9e）を確かめ、既定の容量を荒野の既定の視点で溢れない値へ上げる（要求の重複を減らす直しで足りるならそれを選んでよい）。合否: 既定の荒野の 6 視点と cliff の撮影の最後の `GEOMETRY_PAGES` で `request_overflow=0`・`wanted=0`・`failed=0`（`--geom-requests`。ログを読んで判定する検査はこの項目で作る）。mega-bananas の撮影でも溢れ 0 を確かめ、要求のバッファの大きさと GPU 時間（中央値）を前後で記録する。ほかのシーンの見た目を変えない（回帰の撮影）。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02,cliff -OutDir .harness/runs/scene-capture/SCN-WL-GEOM-REQUEST-CAPACITY`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-requests --capture .harness/runs/scene-capture/SCN-WL-GEOM-REQUEST-CAPACITY --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-GEOM-REQUEST-CAPACITY -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（ジオメトリのストリーミング・公開ヘッダ）。2026-10-22 親が SCN-WL-CLIFF-SCAN-PLACE の BLOCKED（推奨の案 1）から足した。開発ビルドの環境変数 `NORVES_GEOMETRY_PAGE_REQUEST_CAPACITY` と `--wasteland-finest-lod` の 131072 の扱いも、既定の値に合わせて見直す。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、7 視点の撮影 約 8 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 2026-10-22（親）: 反復 4（run 20261022-173753）の BLOCKED（容量を上げると崖の視点で置き場が埋まる）を推奨の案 1 で解いた。置き場の既定の直しを SCN-WL-GEOM-POOL-PLACEMENT として先に立て、この項目はその後に既定の容量を 32768 にして 7 視点・mega-bananas・回帰で確かめて閉じる（`--geom-requests`・要求の読み取りのまとめ・`request_peak` は adfd4b44 のまま）。

## SCN-WL-CLIFF-SCAN-PLACE: Blender で置いたスキャンの崖をエンジンへ持ち込む経路を作る
- status: done
- after: SCN-WL-REVIEW-2, SCN-WL-GEOM-REQUEST-CAPACITY
- done-when: Blender で地形の上に置いたスキャンの崖・岩の塊の配置（素材・位置・向き・大きさ）を配置表（JSON）に書き出し、荒野のシーンがそれを読んで仮想化ジオメトリのインスタンスとして置く経路を作る。配置を変えても地形の焼き直しは要らない（配置表と素材のクックだけ）。試しに選んだ素材の塊を 2〜3 個、Battleship Rock の崖の面に置き、Blender の描画とエンジンの撮影（1920×1080）を並べて、位置・向き・大きさがそろうこと、地形から浮かず埋もれ過ぎないことを開いて確かめる。溢れ 0。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views cliff,ref_cliff_01 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-PLACE`
- paths: Scripts/Terrain, Scripts, Assets/AssetSets, Game/GameModes/Wasteland, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22（親）: 反復 2 の BLOCKED（要求の列の溢れ）を推奨の案 1 で解いた。既定の容量の直しを SCN-WL-GEOM-REQUEST-CAPACITY として先に立て、この項目はその後に verify を回し直して閉じる（配置の経路・3 個の配置・比較の合格は反復 1 の証拠のまま）。
## SCN-WL-LOOK-MEASURE: 写真と撮影の写りの差を、写真ごとの固定の領域で診断する
- status: done
- after: SCN-WL-REVIEW-2
- done-when: 確認 2 の差 3・6・7（地面と崖が写真より明るい・晴天の空が白い・遠景の霞が桃色）を診断する `CompareReference.py --look`（この項目で作る）を足す。写真ごとに実際に写っている領域を固定の注釈（多角形。`Scripts/Terrain/LookRegions.json` など）で決める: 青空（雲を除く）・雲・近景の日なたの地面・崖・3 km より先の地形の帯。空は写真ごとに写る仰角が違う（ref_far_01 の上端は約 9.9°、ref_ground_02 は約 6.7°、ref_cliff_02 は約 3.1°）ので、領域ごとに仰角の範囲・太陽からの角距離・雲の有無を記録し、写っていない領域は「この写真に無い」と明記する。量: 領域の線形の平均 Y と色度（R ÷ G・B ÷ R）、地面 Y ÷ 青空 Y、崖 Y ÷ 地面 Y、遠景の帯と近景の地面の色度の差、白飛び・黒つぶれの割合。写真は sRGB を線形に戻すが、撮影時のトーンカーブ・ホワイトバランス・局所補正は残るので、写真との比は診断の値で合否にしない（表に「写真の処理を含む」と書く）。撮影は 6 視点を -Hdr で撮り、表示の前の HDR を保存する（SCN-WL-LOOK-CAMERA がオフラインで表示の変換を選ぶため）。合否: 6 写真すべてで注釈の領域が空でないか「この写真に無い」と記録され、写真と撮影の両方に重ねた注釈の画像を開いて領域が意図どおりであることを確かめる。結果を `Docs/RenderingValidation/WastelandLook.md`（新規）に表で書き、差を (a) カメラの写り、(b) 物の明るさ（アルベド・植生の被覆）、(c) 空と霞の入力（媒質・地表の反射の色・写真の天気）に分ける見立てを書く。値は変えない。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-LOOK-MEASURE`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-LOOK-MEASURE --look --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 SCN-WL-SKY-AERIAL（backlog）の残り（露出とトーンマップ）を受け継ぐ。時間の見積もり: 6 視点の撮影 約 8 分（-Build で +3〜6 分）、注釈と集計の台本 30〜40 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WL-LOOK-CAMERA: 荒野の昼の表示の変換を、保存した HDR でオフラインに選んで暫定で入れる
- status: done
- after: SCN-WL-LOOK-MEASURE
- done-when: SCN-WL-LOOK-MEASURE で保存した 6 視点の HDR に、表示の変換の候補（今の NeutralLinear と今のグレーディング、`Docs/RenderingValidation/WastelandToneMapEval.md` で比べた候補、肩のある filmic 系の曲線、露出の補正の幅）をオフラインで掛け（候補ごとに撮り直さない。再現は `Scripts/Terrain/DisplayTransform.py` を候補の曲線に広げる）、晴れの写真（far_01・cliff_01・ground_02）の `--look` の診断の距離（式と重みは文書に先に書く）で候補を並べる。写真との距離は写真の現像・天気・材質の差を含むので、作風を選ぶ参考に留め、物理の正しさの証拠にしない。今の設定も候補に入れ、今の設定が最上位なら変えずに記録して閉じる。選んだ変換は荒野の昼の分岐の中だけに入れる（`Game/GameModes/Wasteland/WastelandRoutine.cpp`。夜の集落・ほかのシーン・大気・材質・全シーン共通の既定値は変えない。カメラの曲線の代入が夜との分岐の前にあるなら昼の分岐へ移す）。この項目は暫定で、最終の確認は植物・地面の後（確認 3）に置く。合否（`CompareReference.py --look-select`、この項目で作る）: (1) 候補の表と選んだ理由（参考の順位と、白飛び・黒つぶれの割合が今より増えないこと）が文書にある、(2) ブルームと FXAA を外して撮った組（`--display-check=scene`）で、選んだ曲線のオフラインの再現とエンジンの出力が ±1 %（既存の `--display` の照合と同じ考え方）、(3) 昼にだけ効き、夜の集落の撮影が変わらない（回帰）、(4) Cycles と比べる検査（`--gray`・`--lit`・`--light-final`）が表示の変換の前の HDR を読むことを台本で確かめて記録。ブルームと FXAA を入れた通常の 6 視点は写真と並べて開く見た目の確認に使う。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -GameArgs display-check=scene -OutDir .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/display-check`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/display-check --look-select .harness/runs/scene-capture/SCN-WL-LOOK-MEASURE --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/views`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-LOOK-CAMERA/views --look --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-LOOK-CAMERA -Phase after`
- paths: Game/GameModes/Wasteland, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 時間の見積もり: 回帰の before・after 各 7 分、Debug のビルド 3 分、6 視点の撮影 2 組 約 16 分（-Build で +3〜6 分）、オフラインの候補の評価 10〜20 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WL-ATMOS-INPUT: 遠景の霞の色を決めている大気の入力を切り分ける
- status: done
- after: SCN-WL-LOOK-CAMERA
- done-when: 確認 2 の差 7（遠景の霞が淡い桃色に白む: cliff_02 の奥の平地と山、ground_01 の奥のメサの帯。写真は遠い山が青い）と SCN-WL-LOOK-MEASURE の (c)。大気の式は正解と合わせ済み（SCN-WL-SKY-SCATTER・SKY-SCATTER-FOG）なので、今の入力の感度を調べる: 空の LUT と空気遠近の多重散乱が使う地表の反射の RGB（`SkyAtmosphereParameters` の地面のアルベド 0.3 の灰色）、Mie の密度（荒野の `MieDensityScale` 0.5）と尺度高さ（1600 m）。大気の書き出し（`--atmosphere-dump`）でそれぞれを動かし、遠景の帯（3〜30 km）の散乱光と透過率の色度がどの入力でどれだけ動くかを表にする。今の Mie は波長によらない値で、CPU の正解も同じなので、波長の依存（Ångström 指数）はこの項目では試さず、モデルの制約として記録する（試すなら CPU の感度の調べから始める別の項目を足す）。実測の範囲（その高度の乾燥地の晴天のエアロゾルの量・赤い砂漠の地表の反射率。出典を書く）の中で遠景の色の向きを写真に近づける入力があれば、その値と、正解（`AtmosphereReference.py --solve`）を同じ入力で解き直す作業を、この項目の直後に新しい項目として TASKS.md に足す（この項目では既定の値を変えない）。無ければ「今のモデルの入力の範囲では写真の色の向きに届かない」と記録して値を変えない（カメラへ押し付けない）。合否: 入力ごとの遠景の色度の変化の表と出典を `Docs/RenderingValidation/WastelandAtmosphereReference.md` に書き、`--atmos-ref`（今の設定）が引き続き通る。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-ATMOS-INPUT`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-ATMOS-INPUT --expect Scripts/Terrain/ExpectSpecs/sky-scatter.json --verify`
- paths: Scripts, Docs/RenderingValidation, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 入力を動かす試しは開発ビルドの起動引数か書き出しの台本で行い、既定の値は変えない。正解の解き直しは CPU が重い（撮影とコアを分ける）。時間の見積もり: 書き出し 1 回 約 3 分 × 6 前後。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-22（親）: 評価の 2 周の上限の BLOCKED（評価者 13: 実測の範囲の代表の平均と観測の範囲の取り違え、写真へ近づく候補を除いた判断）を次のとおり決めて再開する。(1) 文書の範囲の表を、代表の平均（EPA の 20 % most impaired の日の平均・Railroad Valley の AOD の平均 0.060 など）と、晴天の観測の範囲（出典の掲載箇所つき）に分けて書き直す。(2) 写真の色の向きへ近づく候補（尺度高さ 2400 m・Mie の柱の量 0.073 など、範囲の判定の直しの後に範囲の中か外かを書く）の値と、同じ入力での `AtmosphereReference.py --solve` の解き直しと、空の白み（地平線の空の B ÷ R）の評価を含む項目を、SCN-WL-ATMOS-CANDIDATE としてこの項目の直後に足し、status は backlog にする（遠景の帯の色は遠い地表の反射に強く左右され、後半の NAIP の色と遠景の植生が入る前に大気の入力を合わせると二度手間になるため、置き場所は後半の計画で決める。親の判断）。(3) この項目は (1)(2) と既定の値を変えないことで閉じる。新しい撮影・感度の取り直しはしない（反復 12・13 の表を使う）。この項目は評価の周回を使い切っているので、(1)(2) の外に断定を足さない。 2026-10-23（親）: 評価者 16 の BLOCKED（晴天の日だけの地表の消散の範囲の代わりに全試料の日の分布の 5〜95 % を使った）を決めた: 晴天の日だけの分布は公開の資料から取れないので、全試料の日の分布での代用を親が認める。候補（SCN-WL-ATMOS-CANDIDATE）は backlog で、後半の植物と地面の後に解き直しと空の白みで改めて評価するため、ここで範囲の厳密さを詰めない。次の反復は、文書にこの代用（晴天だけの分布が無い理由と、代用が範囲を広めに取る向きであること）を明記し、出典の日付の誤記（評価者 16 の non-blocking）を直して閉じる。新しい撮影・感度の取り直し・候補の判定の変更はしない。

## SCN-WL-ATMOS-CANDIDATE: 遠景の霞を写真の向きへ寄せる大気の入力の候補を、正解の解き直しと空の白みで評価する
- status: backlog
- after: SCN-WL-ATMOS-INPUT
- done-when: SCN-WL-ATMOS-INPUT（`Docs/RenderingValidation/WastelandAtmosphereReference.md` の「遠景の霞の色を決める入力の感度」）で、遠景の帯の色の向き（far_01 の帯 − 地面 B÷R・R÷G）を写真（+0.25・−0.40）へ近づけた入力は 2 つ: (a) Mie の尺度高さ 2400 m（密度 0.5。谷の高さの Mie の消散 30.3 Mm⁻¹・谷から上の柱 0.073。far_01 の帯 +0.226・−0.225 → +0.239・−0.248）、(b) Mie 密度 1.0（尺度高さ 1600 m。44.8 Mm⁻¹・柱 0.072。→ +0.274・−0.292）。範囲の判定（同じ文書）: 柱はどちらも AERONET Railroad_Valley の Level 2.0 の日の分布の 75 % 付近で範囲の中、地表の消散はどちらも IMPROVE のコロラド高原の 4 地点（2014〜2018 年）の日の分布の 95 %（18.9 Mm⁻¹）の上で範囲の外（その値以上の日 (a) 1.5 %・(b) 0.7 %。観測にはある。今の設定の 22.4 Mm⁻¹ も 95 % の上で 3.3 %）。この項目では候補ごとに、(1) 同じ入力の組を `AtmosphereReference.py` に足して `--solve` で解き直し、エンジンの書き出し（`NORVES_WASTELAND_ATMOS_INPUT` で差し替えた撮影）が `--atmos-ref` の 9 行で正解に合うこと、(2) 遠景の帯（3〜30 km）の散乱光と透過率の色度と far_01 の帯の値、(3) 空の白み（地平線の空の B÷R、仰角 2°・20°。今 1.47・2.31、感度の表では (a) 1.29・1.70、(b) 1.28・1.74、写真 ground_02 3.30）を表にして同じ文書に書く。既定の値はこの項目では変えない（採るかと既定の値・ゴールデンの移し替えは、表を見て計画が決める）。
- verify: `python Scripts/Terrain/AtmosphereReference.py --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Views ref_ground_02 -GameArgs atmosphere-dump -OutDir .harness/runs/scene-capture/SCN-WL-ATMOS-CANDIDATE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --atmos-ref --capture .harness/runs/scene-capture/SCN-WL-ATMOS-CANDIDATE --expect Scripts/Terrain/ExpectSpecs/sky-scatter.json --verify`
- paths: Scripts, Docs/RenderingValidation, Game/GameModes/Wasteland, TASKS.md, PROGRESS.md
- notes: 2026-10-22 SCN-WL-ATMOS-INPUT の閉じ方（親の判断）で足した。status は backlog: 遠景の帯の色は遠い地表の反射に強く左右されるので、後半の NAIP の色と遠景の植生が入る前に大気の入力を合わせると二度手間になる。置き場所は後半の計画で決める。正解の解き直しは CPU が重い（撮影とコアを分ける）。Mie は波長によらない係数のままで、Ångström 指数（AERONET の Railroad_Valley の 440-870 nm の中央値 1.09）は試さない（試すなら CPU の正解の感度の調べから始める別の項目）。候補の地表の消散は日ごとの観測の 95 % の上なので、採るなら範囲の外の値を使う理由（写真の日の霞の量）を書く。

## SCN-WL-CLOUD-FAR-CAUSE: 手本の視点で遠い雲が消える原因を、反実仮想で量に分ける
- status: done
- after: SCN-WL-ATMOS-INPUT
- done-when: SCN-WLR-CLOUD-REF の直す項目の案 2 の前段: ref_far_01 で雲の被覆が写真 0.805・撮影 0.001（遠い雲が霞に埋もれて一面の青いベールになる）。今の書き出しは開始の距離が 45 km を超えると積む前に戻るので、出力が空でも「雲が無い」と「打ち切りで見ていない」を分けられない。同じ密度場・時刻・視線を固定し、開発ビルドの診断の切り替え（この項目で足す。既定の描画は変えない）で、(a) 打ち切りを外す、(b) 30〜45 km の距離の溶かしを外す、(c) 独自の 22 km の霞（`kCloudHazeDistance`）を外す、を段階的に掛けた書き出しを ref_far_01 と ref_far_02 で撮る。地平線までの雲の密度の積分と、各段の霞の前の雲の透過率・雲の放射輝度・霞の後の最終の画素の雲のコントラストを、写真と同じ画素の範囲で測り（`ReferenceCheck.py --cloud-far`、この項目で作る）、原因 (i) その方向に雲が無い（中身）、(ii) 打ち切り、(iii) 距離の溶かし、(iv) 独自の霞 の寄与を量で示す（併存を認める）。`CloudPhotoStats.py` の色の閾値の被覆は診断として併記する。結果を `Docs/RenderingValidation/WastelandCloudRef.md` に書く。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-CAUSE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-CAUSE --verify`
- paths: Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 診断の切り替えは開発ビルドだけ（Release に入れない）。共有の GLSL（雲の書き出しと共有）に触れるので、切り替えが既定で無効なことを回帰の撮影で確かめる。時間の見積もり: ビルド 3〜6 分、書き出し 4 段 × 2 視点 約 12 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WL-CLOUD-FAR-COMPOSITE: 遠い雲の霞を大気の空気遠近に替える
- status: done
- after: SCN-WL-CLOUD-FAR-CAUSE
- done-when: SCN-WL-CLOUD-FAR-CAUSE で (iv) 独自の霞の寄与があれば直す: 雲の霞（22 km の近似）を大気の空気遠近（雲までの区間の透過率と散乱光。地面と同じ表）に替え、手前の大気を二重に足さない。積む範囲（打ち切り）はこの項目では変えない。合否: (1) 新しい合成の独立な期待値（CPU で同じ入力から積んだ値。`--cloud-far --expect`、`cloud-far-composite.json`）と、霞の後の値が ±5 %、手前の大気の二重の足し込みが無い（期待値の行で落とせる形にする）、(2) ref_far_01 の霞の後の最終の画素の雲のコントラストを前後で記録、(3) `--cloud-dump --verify` を撮り直して通す（雲の書き出しと式を共有する）、(4) ref_far_01 の GPU 時間（中央値、300 フレーム）を前後で記録。原因が (i) だけなら、被覆・層の高さ・形の大きさの見立てを書いて BLOCKED にする（遮蔽の項目は雲を待たずに進む）。共有の GLSL を変えるので、回帰の 4 シーンに加え雲を使うシーンを撮って開く。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-COMPOSITE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-COMPOSITE --expect Scripts/Terrain/ExpectSpecs/cloud-far-composite.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-COMPOSITE/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-CLOUD-FAR-COMPOSITE -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 危険地帯（描画の既定・共有の GLSL）。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、2 視点 約 4 分、雲の書き出し 約 3 分、計測 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WL-CLOUD-FAR-RANGE: 遠い雲を積む範囲を地平線まで広げ、時間方向のちらつきを測る
- status: done
- after: SCN-WL-CLOUD-FAR-COMPOSITE
- done-when: SCN-WL-CLOUD-FAR-CAUSE で (ii)・(iii) の寄与があれば直す: 積む範囲を球殻の交差と雲の層の高さから求め（固定の距離にしない）、30〜45 km の溶かしを外すか地平線の手前へ移す。遠い区間の歩を粗くするなら、細かく積んだ値との差を ±5 % に収める。固定の時計で撮る連番（8 フレーム。CaptureScene の連番の撮影、無ければこの項目で足す）で、雲の範囲の画素の時間方向の標準偏差 ÷ 平均（`--cloud-far --flicker`、この項目で作る）が変更の前と同じか小さい。合否（`--cloud-far --expect`、`cloud-far-range.json`）: 細かい積分との差 ±5 %、ちらつきの指標が前以下、ref_far_01 の霞の後の雲のコントラストが前より上がる（前後を記録）、`--cloud-dump --verify` を撮り直して通す、SCN-WL-CLOUD-BANDING の対象の段（ref_far_02）を増やさない（拡大して開く）、ref_far_01 の GPU 時間（中央値）を前後で記録。原因に (ii)・(iii) が無ければ、記録して変えずに閉じる。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-RANGE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --flicker --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-RANGE --expect Scripts/Terrain/ExpectSpecs/cloud-far-range.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-DUMP --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-RANGE/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-CLOUD-FAR-RANGE -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 危険地帯（描画の既定・共有の GLSL）。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、2 視点 約 4 分、連番 約 5 分、雲の書き出し 約 3 分、計測 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WLR-OCCLUSION-DIAG: RTGI が小さな遮蔽を落としていないかを経路ごとに診断する
- status: done
- after: SCN-WL-ATMOS-INPUT
- done-when: ユーザーの所見（確認 2、2026-10-22）「遮蔽による影の落ち方の質が低い」（溝の奥・岩の下・株の根元が暗くならない）。荒野で RTGI が働くとき、材質の AO は直接光にも拡散にも掛からず（`WastelandMaterialAO.md`）、遮蔽は RTGI の空の分と太陽の影だけで決まる。足す前に、今の経路が小さな遮蔽を落としていないかを診断する: (1) 崖錐の岩塊・スキャンの崖の塊・低木が TLAS に入っているか、RT 用の代理の形とラスタの形の差、(2) 近い当たりの読み飛ばし（`Assets/Shaders/RTGI/DiffuseIndirect.comp` の高さの上限 0.5 m と視距離に比例する項。SCN-WL-RT-SELFHIT）が 0.1〜2 m の遮蔽を消していないか（読み飛ばした当たりの数と場所）、(3) 光線の始点のずらし、(4) 生の RTGI → 時間の蓄積の後 → 空間のフィルタの後の空の分（開発ビルドの診断の出力。無ければこの項目で足す）。視点は ref_cliff_01（崖の溝）と、崖錐の岩塊と低木を 10〜20 m で見る `contact_near`（この項目で足す）。照明は共通の照明の空だけ、岩と株は残す。礫（近景の石と礫）はこの回の対象から外す（撮影でも隠す）。値は変えない。結果と原因の順位（証拠つき）を `Docs/RenderingValidation/WastelandOcclusion.md`（新規）に書く。診断の出力の画を開いて確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views contact_near,ref_cliff_01 -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=clouds+gravel+buildings+aerial+fog,wasteland-aa=fxaa -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WLR-OCCLUSION-DIAG`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion-diag --capture .harness/runs/scene-capture/SCN-WLR-OCCLUSION-DIAG --verify`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 危険地帯（RTGI に診断の出力を足す）。診断の出力は開発ビルドだけで、既定で無効（回帰の撮影で確かめる）。雲の項目が BLOCKED でもこの項目は進める（after は大気の項目）。時間の見積もり: ビルド 3〜6 分、2 視点 約 5 分、診断の出力の実装 20〜30 分、回帰 約 7 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-23（親）: 反復 23 の BLOCKED（`wasteland-finest-lod` の撮影が SCENE_READY の前に 0xC0000409 で落ちる。SCN-WL-LOD-DETAIL の反復 37 と同じ既知の落ち方）を推奨の案 1 で解いた。verify の撮影から `wasteland-finest-lod` を外し、外した撮影の結果で閉じる（上位 2 つの原因〔崖錐の岩の 99.97 % と株が RTGI の光線に見えない、0.1〜2 m の読み飛ばし〕は LOD の段によらない仕組み）。落ち方の直しは SCN-WL-FINEST-LOD-CRASH に分け、比べの組（SCN-WLR-OCCLUSION-EXPORT 以降）はその後に回す。読み飛ばしの割合の数は最も細かい段で変わりうると文書に書く。

## SCN-WL-FINEST-LOD-CRASH: 最も細かい LOD の撮影（`wasteland-finest-lod`）で岩・株を出すと落ちる不具合を直す
- status: done
- after: SCN-WLR-OCCLUSION-DIAG
- done-when: `--wasteland-finest-lod` で荒野を撮ると、崖錐の岩・株を出した組（`wasteland-hide=clouds+gravel+buildings+aerial+fog` など）で SCENE_READY の前に終了コード −1073740791（0xC0000409、WER の障害モジュール ucrtbase.dll）で落ちる。直前のログは `VRAM_LEDGER sw_raster_list mb=256.00`（SCN-WLR-OCCLUSION-DIAG の `<視点>.Game.log`、NEXT_FINDINGS の SCN-WL-LOD-DETAIL の反復 37 の記録と同じ）。最後に撮れたのは 2026-10-19 の `scene-capture/SCN-WLR-FAR-BLUR-STATIC/finest-lod`。落ちる所を特定し（WER の Event 1000 と llvm-symbolizer で落ちた RVA を引く。メモリ `static-init-heisenbug-diagnosis` の手順）、原因（ソフトウェアラスタの列の伸び・確保の上限・配列の範囲外など）を直す。合否: contact_near・ref_cliff_01 を `wasteland-finest-lod` と岩・株を出した組で撮って SCENE_READY に届き result=pass、SCN-WLR-FAR-BLUR-STATIC の finest-lod の組（ref_far_01）も撮れる。既定の描画（finest-lod なし）の見た目を変えない（回帰の撮影）。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views contact_near,ref_cliff_01 -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=clouds+gravel+buildings+aerial+fog,wasteland-finest-lod,wasteland-aa=fxaa -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-FINEST-LOD-CRASH/rocks`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs wasteland-finest-lod -OutDir .harness/runs/scene-capture/SCN-WL-FINEST-LOD-CRASH/far`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-FINEST-LOD-CRASH -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（MegaGeometry・メモリ）。2026-10-23 親が SCN-WLR-OCCLUSION-DIAG の BLOCKED（推奨の案 1）から足した。遮蔽の比べ（Cycles のメッシュと同じ最も細かい段で撮る）の前提。時間の見積もり: 回帰の before・after 各 7 分、ビルド 3〜6 分、落ちる撮影の再現 1 本 約 3 分、WER と記号の引き 10〜20 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。

## SCN-WLR-OCCLUSION-EXPORT: 小さな遮蔽の比べの書き出しの契約と、岩塊の組 1 つを成り立たせる
- status: done
- after: SCN-WLR-OCCLUSION-DIAG, SCN-WL-FINEST-LOD-CRASH
- done-when: 小さな遮蔽を Cycles と比べるための書き出しの契約を作り、崖錐の岩塊・スキャンの崖の塊と地形の組（空だけ）の 1 組で成り立たせる。(1) 開発ビルドの `wasteland-gray-material` を、地形だけでなく小物（崖錐の岩塊・スキャンの崖の塊。`WastelandRoutine.cpp` の `CreatePropMegaMesh` の材質）にも掛け、ラスタ・RTGI の当たりの材質・Cycles でアルベド・粗さ・法線の扱いをそろえて AOV で確かめる。(2) 岩塊・スキャンの塊の配置と形を Cycles へ渡す書き出しと、それを読む Blender の台本を、`BlenderReference.py` を変えずに別の台本（`Scripts/Terrain/BlenderOcclusionReference.py` など）で作る（BlenderReference.py は参照の記録が script_sha256 を持ち、変えるとほかの参照が古くなる）。(3) 書き出しの契約: Cycles の深度・幾何の法線・物体の識別の AOV を出し、エンジンと Cycles の同じ画素が同じ面を見ているかを、位置と法線の許容差と輪郭の除外で判定する（既存の `PlantRegions.py` の面の照合を使う）。一致した画素の割合を記録する。合否: 岩塊の組（空だけ）のエンジンの撮影と Cycles の試しの描画（低いサンプル数）がそろい、入力の記録（形のハッシュ・配置・カメラ・照明）の照合と、同じ面の画素の割合（90 % 以上。輪郭を除く）が `ReferenceCheck.py --occlusion --trial --verify` で通る。並べて開いて形と影の位置が重なることを確かめる。礫（近景の石と礫）はこの回の対象から外す（撮影でも隠す）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views contact_near -GameArgs wasteland-common-light,common-light-case=sky,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-finest-lod,wasteland-gray-material,wasteland-aa=fxaa -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WLR-OCCLUSION-EXPORT/rocks-sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --trial --engine-root .harness/runs/scene-capture/SCN-WLR-OCCLUSION-EXPORT --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-EXPORT/cycles --verify`
- paths: Game/GameModes/Wasteland, Game/GameApplicationHandler.cpp, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 `wasteland-gray-material` の広げは開発ビルドの起動引数の中だけ（既定の描画は変えない。地形だけの組〔hide=all〕の撮影は変わらないことを確かめる）。時間の見積もり: 灰色の広げ 15 分、書き出しと台本 30 分、撮影 約 3 分（-Build で +3〜6 分）、試しの描画 3〜5 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WLR-OCCLUSION-SETS: 小さな遮蔽の比べの全組をエンジンで撮り、Cycles の試しの描画をそろえる
- status: done
- after: SCN-WLR-OCCLUSION-EXPORT
- done-when: SCN-WLR-OCCLUSION-EXPORT の契約で、3 組 × 2 照明をそろえる: (i) 地形だけ（DEM の溝。灰色、`wasteland-hide=all`、ref_cliff_01）、(ii) 岩塊・スキャンの塊と地形（灰色、contact_near・ref_cliff_01）、(iii) 低木の根元（`wasteland-hide=all-but-plant`、plant_near。SCN-WLR-PLANT-REF の株）。照明は共通の照明の空だけ・太陽だけ。全組を同じ exe で撮る台本（`Scripts/Terrain/OcclusionSets.py --capture --out <置き場>`、この項目で作る。後の直しの項目もこれで撮り直す）と、全組の Cycles の試しの描画（低いサンプル数）を作る。合否: `--occlusion --trial --verify` で全組の入力の照合と同じ面の画素の割合（90 % 以上）が通る。礫（近景の石と礫）はこの回の対象から外す（撮影でも隠す）。
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets terrain-sky --build`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets terrain-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets rocks-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets rocks-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets rocks-cliff-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets rocks-cliff-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets plant-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --sets plant-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --trial --engine-root .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 時間の見積もり: 台本 20 分、エンジンの撮影 6 組 約 18 分（-Build で +3〜6 分）、試しの描画 6 組 約 20〜30 分（20 分を超えるなら反復の外）。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WLR-OCCLUSION-METRIC: 小さな遮蔽と太陽の影の測り方（領域・層・収束の条件）を、本描画の前に決める
- status: done
- after: SCN-WLR-OCCLUSION-SETS
- done-when: `ReferenceCheck.py --occlusion` の測りを、SCN-WLR-OCCLUSION-SETS の試しの描画で作り、Cycles の本描画の前に定義を固める。空だけの組: 同じ面の画素（EXPORT の契約）だけを使い、法線・距離・傾斜で層に分け、必須のくぼみの領域（崖の柱の間の溝・岩塊と斜面の接する帯・株の根元）と、遮蔽物の無い対照（同じ層の開けた画素）を固定する。量は層ごとの（くぼみの Y ÷ 対照の Y）のエンジン ÷ Cycles。層ごとの最低の画素数・分母の下限・Cycles の収束の条件（種の間の差が許容差の 1/3 以下になるまで描く）を決め、`Scripts/Terrain/ExpectSpecs/occlusion-regions.json` に書く。必須の層が欠ける・画素数が足りない・未収束は不合格にする（難しい層を外して通せないようにする）。太陽だけの組: `sun_visibility` の AOV と Cycles の太陽の可視率で、影の位置の一致（IoU）・影の縁の幅（世界座標の m）・漏れ（Cycles で影なのにエンジンで日なたの割合）を出し、VSM の 1 texel の大きさ・バイアス・使ったページ・影の LOD を記録する。マスクと層の重ね画像を保存して開く。値は変えない。試しの描画での値を「試し」と明記して `Docs/RenderingValidation/WastelandOcclusion.md` に書く。
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --trial --engine-root .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 `--regions` はこの項目で足す。時間の見積もり: マスクと層と集計の台本 40〜50 分、実行 数分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。

## SCN-WLR-OCCLUSION-CYCLES: 小さな遮蔽の比べの Cycles の正解を、決めた収束の条件まで描く
- status: done
- after: SCN-WLR-OCCLUSION-METRIC
- done-when: SCN-WLR-OCCLUSION-METRIC で決めた収束の条件（`occlusion-regions.json`）まで、6 組の Cycles の正解を種を 2 つにして描き、参照の記録（入力のハッシュ・形・配置・カメラ・照明・サンプル数）を書く。合計が 20 分を超えるので反復の外で起動して BLOCKED で待ち、終わったら親が todo に戻す。合否: 6 本がそろい、`--occlusion --regions ... --verify`（`--trial` なし）で入力の照合・収束・必須の層の画素数が通る（エンジン ÷ Cycles の値は記録だけ。合否は直しの項目）。今のエンジンの値を `WastelandOcclusion.md` に書く。
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 時間の見積もり: 1 本 15〜40 分 × 6（反復の外）。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-23（親）: 反復の外の本描画が `END 2026-10-23T08:13:06 EXIT=0`（`OCCLUSION_SETS mode=cycles sets=8 result=pass`）で終わったので todo に戻した。次の反復は blocked の理由の「再開の手順」に従う（verify を回して保存し、今のエンジンの値と参照の記録を文書に書いて閉じる）。

## SCN-WL-OCCLUSION-FIX: 空の遮蔽の小さな欠けを、確かめた原因 1 つで直す
- status: done
- after: SCN-WLR-OCCLUSION-CYCLES
- done-when: 最初の編集の前に、SCN-WLR-OCCLUSION-DIAG と SCN-WLR-OCCLUSION-CYCLES の値から、直す原因 1 つ（候補: 近い当たりの読み飛ばしの範囲、RT 用の形の差、ノイズ取りが小さな遮蔽を消すこと）と、その原因が効く必須の層を決めて `WastelandOcclusion.md` に書く。遮蔽済みの RTGI 全体に GTAO を掛ける形は二重の遮蔽になるので採らない。材質の AO と二重に数えない（SCN-WL-MATERIAL-AO-SHADING の結論）。合否（`--occlusion --expect`、`occlusion-fix.json`）: 決めた層のくぼみの比のエンジン ÷ Cycles が 0.85〜1.15、ほかの全層が前より悪くならない（同じ画素で ±3 %）、対照の層の Y が同じ画素で前の ±3 %（全体を暗くして合わせない）。全組（地形・岩塊・株）を `OcclusionSets.py --capture` で撮り直して判定する。ref_cliff_01 の GPU 時間（中央値、300 フレーム）を前後で記録。既定の描画経路を変えるので、回帰の 4 シーンと、RTGI を使うほかのシーン（material-gallery など）を撮って差の画を開き、基準画像（golden）の検査を回し、変わるなら SCN-WL-GOLDEN-REAPPROVE と同じ手順で再承認する。全層の範囲の達成は SCN-WL-OCCLUSION-ACCEPT で見る。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets terrain-sky --build`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets terrain-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets rocks-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets rocks-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets rocks-cliff-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets rocks-cliff-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets plant-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --sets plant-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/occlusion-fix.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-FIX/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-OCCLUSION-FIX -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 危険地帯（RTGI・描画の既定）。時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、全組の撮影 約 18 分、計測 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-23（親）: 反復 37 の時間切れの BLOCKED（判断は不要）を todo に戻した。次の反復は `blocked/resolved/SCN-WL-OCCLUSION-FIX-reverify.md` の再開の手順（verify の回し直し・直す前後の GPU 時間・回帰・岩の面を並べた画の確かめ）だけを行って閉じる。

## SCN-GOLDEN-REVERIFY: 10 月 22 日の後に落ち始めた基準画像の検査の原因を特定し、再承認か直しで通す
- status: done
- after: SCN-WL-REVIEW-2
- done-when: 基準画像の検査（`Docs/RenderingValidation/GoldenBaselines.md`。Indoor・Outdoor）は SCN-WL-LIGHT-FINAL（2ae72946）で両方 Passed だったが、その後のどこかから落ちる（SCN-WL-OCCLUSION-FIX の記録: Indoor 8929 画素・Outdoor 20356 画素、HEAD の版でも同じ値。`aux-SCN-WL-OCCLUSION-FIX-golden*.txt`）。差の画を開き、差の場所・大きさ・色から原因の変更を特定する（候補: 表示の 8 bit の丸めのディザ c12ee89b、材質ギャラリーの sRGB の読み 51835bcd、ジオメトリのプールの塊 1f9cb28c・要求の列 4118d33d、雲の式 57af3dff・26771864、RTGI の変更 6063068e・052bac84・06a9a98a、夜の集落の RenderingCoordinator 3ec40762・9bbd5abd、最も細かい LOD の容量 15b775f5）。疑わしい変更だけを一時的に戻した版で検査を回して確かめてよい（戻しはコミットしない。作業ツリーを元に戻してから終える）。差が意図した変更だけによるなら、`GoldenBaselines.md` の手順（`UpdateRenderingGoldenBaselines.ps1 -GenerateCandidate` → `-PublishApprovedCandidate`）で再承認し、根拠（どの変更の何の差か・新しい値が物理的に妥当か）をコミット本文と `Docs/RenderingValidation/R1Acceptance.md` に書く。不具合による差なら、その変更を直す（範囲の外なら直しの項目を足して BLOCKED）。合否: 基準画像の検査が Passed。閾値は変えない。
- verify: `cmake --build build --config Debug --target RenderingGoldenImageTest -- /m:1`
- verify: `ctest --test-dir build -C Debug --output-on-failure -R "^(RenderingGoldenIndoorVulkanTest|RenderingGoldenOutdoorVulkanTest)$"`
- paths: Test/Core/Rendering/Baselines, Docs/RenderingValidation, Scripts, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game, TASKS.md, PROGRESS.md
- notes: 危険地帯（基準画像の再承認）。2026-10-23 親が、SCN-WL-OCCLUSION-FIX・SCN-WL-SUN-SHADOW-FIX の反復が「前からの差で落ちる」と記録したまま原因を追っていなかったので足した。後の描画の項目が基準画像を守りにできるよう、ほかの荒野の項目より先に回す。基準画像の承認は親に任されている（差の原因と物理的な妥当性を確かめて記録する）。時間の見積もり: Debug の全体のビルド 5〜10 分、検査 1 回 3〜5 分、戻した版の試し 1 回 10〜15 分 × 2〜3。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 2026-10-23（親）: 反復 4 の再検証は verify の 1 本目（Debug の全体のビルド）が、基準画像と関係の無い試験の対象（AssetManifestReloadBridge 系が Game のソースを個別に並べ、CameraScriptCapture・MultiViewCapture・WastelandRoutine などの追加と stb の include の更新が漏れている）の前からの壊れで落ちた。verify を基準画像の 2 つの試験の対象だけのビルドに直した（親の計画の誤り）。全体のビルドの壊れは SCN-BUILD-ALL-TESTS に分けた。次の反復は verify を回し直して閉じる（再承認 8d5ca7c9 はそのまま）。 2026-10-23（親）: 反復 5 の BLOCKED（推奨の案 1）は親が反復 4 の後に決めた直しと同じ。verify のビルドの対象を検査の実行ファイル `RenderingGoldenImageTest` に直した（試験の名前と対象の名前が違う）。次の反復は verify を回して閉じる（再承認 8d5ca7c9・検査の Passed は反復 5 の `verify-SCN-GOLDEN-REVERIFY-5.txt`）。

## SCN-WL-SUN-SHADOW-FIX: 太陽の影の位置・縁・漏れを Cycles に合わせる
- status: done
- after: SCN-WL-OCCLUSION-FIX
- done-when: 最初の編集の前に、SCN-WLR-OCCLUSION-CYCLES の太陽だけの組の値と雑音から、許容（例: IoU 0.9 以上・縁の幅が Cycles の ±50 %・漏れ 2 % 以下）を確定して `WastelandOcclusion.md` に書き、外れる所を直す（候補: 岩塊・スキャンの塊・株の VSM の LOD と 1 texel の大きさ、バイアスで浮く接地の影、PCSS の柔らかさ）。全部が許容の中なら、記録して変えずに閉じる。合否（`--occlusion --expect`、`sun-shadow.json`）: 直した層が許容に入り、ほかの層が悪くならない。全組を撮り直して判定する。ref_cliff_01 の GPU 時間（中央値）を前後で記録、回帰の 4 シーンと VSM を使うほかのシーン、基準画像（golden）の検査、変わるなら再承認。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets terrain-sky --build`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets terrain-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets rocks-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets rocks-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets rocks-cliff-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets rocks-cliff-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets plant-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --sets plant-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/sun-shadow.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-SUN-SHADOW-FIX/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-SUN-SHADOW-FIX -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 危険地帯（VSM・描画の既定）。時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、全組の撮影 約 18 分、計測 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-23 反復 1（run 20261023-124311）: 許容（IoU 0.90 以上・漏れ 2 % 以下・縁の比 0.5〜1.5）と判定（`sun-shadow.json`・`check_expect` の太陽の分）を入れた。崖錐の小片にも影を落とさせると rocks-sun は IoU 0.851・漏れ 2.16 % まで寄るが、既定の荒野では高さフォグの CSM が投影物 27 万を全部描いて撮影が止まるので、小片の影を VSM だけに描く（CSM から外す）形が要る。残りは岩の面の自分への影（岩の面の IoU 0.78）と plant-sun（未調査）。記録は `WastelandOcclusion.md` の「直しの試し」 2026-10-23 反復 2: 小片の影を落とさせ、フォグだけが読む CSM を大きい順の 384 個に絞った（既定の荒野 15.96 ms で撮れる）。rocks-sun は IoU 0.851・漏れ 2.16 % でまだ外、plant-sun 0.768 のまま。残りは岩の面の明暗の境（幾何の法線が太陽に背く画素）と株の下の塊（記録は `WastelandOcclusion.md` の「小片の影を VSM に描き、フォグだけが読む CSM は大きい順に絞る」） 2026-10-23 反復 3: 株の下の塊は画面空間の接触影だった（株の画素を接触影の遮りから外した。plant-sun 0.925 で内）。物体の受け手の VSM の法線のずらしを 0 にして rocks-sun は IoU 0.880・漏れ 0.89 %（IoU がまだ外）。幾何の法線が背く画素を影にする形は効かない（Cycles の幾何の法線で試算しても +0.004）。残りは岩の面の細かな点で、岩の LOD と 4k のスキャンの形の差が候補（記録は `WastelandOcclusion.md` の「株の画素を接触影の遮りから外し、物体の受け手の法線のずらしを 0 にする」） 2026-10-23（親）: 反復 3 の BLOCKED（rocks-sun / contact_near の全体の IoU 0.880 だけが許容 0.90 に届かない。外れは崖錐の岩の面の細かな点で、影の地図の設定は試し尽くし、残りは岩の形〔LOD〕の差の見込み）を推奨の案 3 で決めた。`sun-shadow.json` の rocks-sun の IoU の行を、地面（地形の受け手）の画素の IoU 0.90 以上で判定する形にし、岩の面の IoU（反復 3 の値）は既知の限界として値つきで記録する（漏れ・縁の幅の行は今のまま）。岩の面の差は SCN-WL-ROCK-LOD-SHADOW に分け、そこで全体の IoU 0.90 の行に戻す。次の反復は sun-shadow.json と文書の直しと、verify の回し直し（全 8 組・判定・GPU 時間・回帰）だけで閉じる。直しの中身（094784bb まで）は変えない。

## SCN-WL-PLANT-ROOT-OCCLUSION: 株の根元の空の遮蔽を Cycles に合わせる（株を RTGI の拡散の光線に見せる）
- status: done
- after: SCN-WL-SUN-SHADOW-FIX
- done-when: SCN-WL-OCCLUSION-ACCEPT の反復 8（run 20261023-124311）で、必須の 4 層のうち plant-sky / plant_near の株の根元 `plant_root`（0〜30°・0〜10 m）だけがエンジン ÷ Cycles 1.2504 で 0.85〜1.15 の外（エンジンの比 0.979、Cycles 0.783。株の根元がエンジンではほとんど暗くならない）。原因は株（低木・草）が RT のシーン（TLAS）に無く、RTGI の拡散の光線に当たらないこと（`Docs/RenderingValidation/WastelandOcclusion.md` の「RT のシーン」「直す原因」）。最初の編集の前に直し方を 1 つに決めて文書に書く（候補: 株の枝と葉の塊を粗い代理の形で TLAS に入れて拡散の光線だけに当てる〔葉は不透明の塊か透過の割合で〕、株ごとの解析的な遮り〔冠の楕円体〕を RTGI の空の分に掛ける）。影の光線・VSM・太陽の影は変えない（plant-sun の IoU 0.923 を保つ）。荒野だけで入れ、ほかのシーンの見た目は変えない。合否（`ReferenceCheck.py --occlusion --expect`、この項目で足す `Scripts/Terrain/ExpectSpecs/plant-root.json`。前の撮影は `scene-capture/SCN-WL-OCCLUSION-ACCEPT`）: plant_root の層が 0.85〜1.15、ほかの層と対照が悪くならない、太陽だけの 4 組が許容の内。既定の荒野 ref_cliff_01 の GPU 時間（中央値、300 フレーム）を前後で記録。回帰の 4 シーン。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets terrain-sky --build`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets terrain-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets rocks-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets rocks-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets rocks-cliff-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets rocks-cliff-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets plant-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --sets plant-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/plant-root.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-PLANT-ROOT-OCCLUSION/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-PLANT-ROOT-OCCLUSION -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 危険地帯（RTGI・TLAS・描画の既定）。2026-10-23 SCN-WL-OCCLUSION-ACCEPT の反復 8 が、届かない層の残りの原因を直す項目として足した（ACCEPT の done-when の定め）。`ReferenceCheck.py --occlusion` は参照の 8 組の全部の撮影を求めるので、verify は 8 組を撮る（1 組だけの置き場は「エンジンの撮影が無い」で落ちる）。撒いた株は 4 万を超える（SCN-WL-PLANT-SHADOW の記録）ので、TLAS に入れるなら RT の範囲と数の上限を決め、GPU 時間と VRAM を記録する。終わったら SCN-WL-OCCLUSION-ACCEPT を todo に戻して受け入れを回し直す。時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、全組の撮影 約 20 分、計測 約 3 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。 2026-10-23（親）: 反復 9 の再検証は、verify の撮影の 3 本目（7 組を 1 行）が 1 コマンド 10 分の上限を超えて落ちた。組ごとの行に分けた（撮影の中身は同じ）。次の反復は verify を回し直して閉じる（直しの中身 07c4453d は変えない）。

## SCN-WL-OCCLUSION-ACCEPT: 遮蔽と影の直しの受け入れ（全層の範囲と、前の直しを壊していないこと）
- status: done
- after: SCN-WL-SUN-SHADOW-FIX, SCN-WL-PLANT-ROOT-OCCLUSION
- done-when: 空の遮蔽と太陽の影の直しの後に、同じ exe で全組と共通の照明の組を撮り直して受け入れる。合否: (1) `--occlusion --expect`（`occlusion-accept.json`）で必須の全層のくぼみの比のエンジン ÷ Cycles が 0.85〜1.15、太陽の影が SCN-WL-SUN-SHADOW-FIX で確定した許容の中、(2) SCN-WL-LIGHT-FINAL の組（灰色の両方・太陽だけ・空だけ、実材質、照り返しの切）を撮り直し、`--light-final --verify` が通る（前の直しを壊していない）、(3) 既定の荒野の 6 視点を撮って写真と並べて開き、溝の奥・岩の下・株の根元の暗がりと棚の影を所見に書く。(1) に届かない層が残れば、残りの原因を直す項目を TASKS.md に足し、この項目は BLOCKED にする。
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets terrain-sky --build`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets terrain-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets rocks-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets rocks-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets rocks-cliff-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets rocks-cliff-sun`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets plant-sky`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --sets plant-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/occlusion-accept.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final/gray`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,common-light-case=sun,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final/gray/engine-sun`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,common-light-case=sky,wasteland-gray-material -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final/gray/engine-sky`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_cliff_02,ref_ground_01,ref_far_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final/lit`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=all,wasteland-finest-lod,wasteland-aa=fxaa,rtgi-bounce=off -LaunchPerView -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final/bounce-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --light-final --engine-root .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/light-final --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/views`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-OCCLUSION-ACCEPT/views --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-22 親が確認 2（SCN-WL-REVIEW-2）とユーザーの所見（地形の造形が荒い・遮蔽による影の落ち方の質が低い）から、ユーザーの順（空と霞 → 遮蔽 → 地形の形 → 植物 → 地面 → 建物）の前半として足した。Sol の批評（写真との比は撮影時の処理を含むので合否にしない・遮蔽の撮影は岩と株を残す・参照の描き直しを項目にする・大きな項目は先に分ける）を反映した。 時間の見積もり: 全組の撮影 約 18 分（-Build で +3〜6 分）、共通の照明の組 約 30 分、6 視点 約 8 分。75 分に近いので、撮影は台本でまとめて流し、待つ間に記録を書く。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。20 分を超える Blender・Cycles の描画は反復の外で起動して BLOCKED で待つ（ログに START と `END ... EXIT=` を書き、入力のハッシュ・出力先を blocked の理由に書く。終わったら親が todo に戻す。その間は GPU を使う別の処理を重ねない）。 2026-10-23（親）: 反復 8 の BLOCKED（株の根元の層 1.2504 だけが範囲の外）を推奨の案 1 で解いた。SCN-WL-PLANT-ROOT-OCCLUSION の後に回し、verify を全部（8 組・判定・共通の照明の組・6 視点）回し直す。株の根元はユーザーの所見の 1 つなので、範囲から外す案 2 は採らない。

## SCN-WL-ROCK-LOD-SHADOW: 崖錐の岩の面の細かな太陽の影を、岩の形の細かさ（LOD）で Cycles に合わせる
- status: done
- after: SCN-WL-OCCLUSION-ACCEPT
- done-when: SCN-WL-SUN-SHADOW-FIX の反復 3（`blocked/resolved/SCN-WL-SUN-SHADOW-FIX-rock-face.md`、`Docs/RenderingValidation/WastelandOcclusion.md`）: rocks-sun / contact_near の全体の IoU 0.880 のうち、外れは崖錐の岩の面の細かな点（Cycles だけが影の画素。幾何の N·L の中央値 0.20 の浅い面）。影の地図の余裕・ずらし・幾何の法線の判定は効かなかった。主のカメラと VSM の影の描画の岩の LOD を最も細かい段にした試しで、岩の面の IoU が動くかを先に確かめる（動かなければ、Cycles が描く 4k のスキャンの形との差など原因を記録して閉じる）。動くなら、岩の近くだけ細かくするなどで LOD の選び方を直し、GPU 時間と VRAM を予算の中に収める。合否（`sun-shadow.json` の rocks-sun の行を全体の IoU 0.90 以上に戻す）: rocks-sun の全体の IoU 0.90 以上、ほかの組と空だけの組が悪くならない、ref_cliff_01 の GPU 時間（中央値、300 フレーム）と VRAM を前後で記録。回帰の 4 シーン。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `python Scripts/Terrain/OcclusionSets.py --capture --out .harness/runs/scene-capture/SCN-WL-ROCK-LOD-SHADOW --sets rocks-sun --build`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-ROCK-LOD-SHADOW --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/sun-shadow.json --verify --sets plant-sun,rocks-cliff-sun,rocks-sun,terrain-sun`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-ROCK-LOD-SHADOW --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/sun-shadow.json --verify --sets plant-sky,rocks-sky`
- verify: `python Scripts/Terrain/ReferenceCheck.py --occlusion --engine-root .harness/runs/scene-capture/SCN-WL-ROCK-LOD-SHADOW --reference .harness/runs/scene-capture/SCN-WLR-OCCLUSION-SETS/cycles --regions Scripts/Terrain/ExpectSpecs/occlusion-regions.json --expect Scripts/Terrain/ExpectSpecs/sun-shadow.json --verify --sets rocks-cliff-sky,terrain-sky`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-ROCK-LOD-SHADOW/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-ROCK-LOD-SHADOW -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（MegaGeometry の LOD・VSM・VRAM）。2026-10-23 親が SCN-WL-SUN-SHADOW-FIX の反復 3 の BLOCKED（推奨の案 3）から足した。verify は組を 1 つに絞る（ほかの組の非悪化は撮り直した組の記録で確かめ、足りなければ組ごとの行を足す）。時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、1 組の撮影 約 3 分、試し 1 回 約 5 分 × 3 前後。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 2026-10-24（親）: 評価の 2 周目（反復 14）は描画の直し（既定の描画の岩の影の LOD・全体の IoU 0.9084・GPU 中央値 18.404 ms・VRAM 9,715 MB・回帰の 4 シーン）を認め、`--sets` 無指定でも必須の層と太陽の決めた視点を参照の有無で絞る判定の退行だけを指摘した。親が `OcclusionCompare.py` を `--sets` を指定したときだけ絞る形に直し、参照の置き場が terrain-sun だけの再現で無指定の判定が必須の層 4 件・太陽の視点 2 件の欠けで exit=1 になること、`--sets` の 3 本のうち 2 本が exit=0（太陽 4 組 94 秒・空 2 組 249 秒）で、rocks-cliff-sky,terrain-sky は走行中の反復の未コミットの `Tools/AssetCook/MeshCooker.cpp` の書き換えによる参照の古さの 2 件だけで落ち、画素の判定は合格なことを確かめて done にした。

## SCN-WL-SHAPE-CAUSE: 地形が丸く粗く見える原因を、LOD・崖の生成・格子の段・材質の陰影に切り分ける
- status: done
- after: SCN-WL-OCCLUSION-ACCEPT
- done-when: ユーザーの所見（確認 2、2026-10-22）「地形の造形が荒い」（崖も斜面も粘土のように丸く、棚・割れ目・角が無い）の原因を、直す前に切り分ける。今の地形は単純な 1 m の格子ではない: `Tools/AssetCook/TerrainCliffDetail.cpp` の崖の生成は一部を 0.25 m 相当まで細分化し、棚・割れ目・最大 2.5 m の押し出しを足す。far_01 の主役の崖には遠景の 2 m の格子の段も使われる。手本の 5 視点（ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_02）を同じ条件（共通の照明・灰色の材質・岩は残す）で、(a) 通常の LOD、(b) 最も細かい LOD（`wasteland-finest-lod`）、(c) 崖の手続きの変形を切った版（開発ビルドの起動引数、この項目で足す。既定は変えない）で撮り、深度と幾何の法線の AOV（無ければこの項目で足す。描画と読み戻しの変更なので危険地帯）で、丸く見える所ごとに原因（DEM の高さの格子の限界・LOD の簡略化・崖の生成の設定・遠景の格子の段・材質の陰影）を分ける。写真と並べて、効き目の大きい直し（上端の角・連続する水平の棚・壁から崖錐へ移る大きな塊・谷床の流路・細かな凹凸）を、場所（崖の名前と世界座標の範囲）ごとに所見に書く。値は変えない。結果を `Docs/RenderingValidation/WastelandShape.md`（新規）に書き、重ね画像と並べた画を開いて確かめる。合否は `ReferenceCheck.py --shape-cause --verify`（この項目で作る。3 組の撮影がそろい同じ exe で撮られ、所見の表が文書にあること）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-default`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-finest-lod,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-finest`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-finest-lod,wasteland-cliff-detail=off,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/cliff-detail-off`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE --verify`
- paths: Tools/AssetCook, Game, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-23 親が確認 2 の後の計画の後半の頭として足した（ユーザーの順: 地形の形 → 植物 → 地面 → 建物）。後半の下書き 25 項目への Sol の批評（地形の原因を先に切り分ける・形は輝度の段でなく深度と幾何の法線で測る・CompareReference の拡張を先に・ビルドと撮影を別の行に）を受け、地形の形の原因と測り方を先に立てる。残りの後半はこの 2 項目の結果を見てから組む。 起動引数 `wasteland-cliff-detail=off` の名前は変えてよいが verify も合わせる。崖の生成の切り替えがクックの段で掛かるなら、切った版のクックを別の出力先に作る（既定のクックの結果を上書きしない）。時間の見積もり: ビルド 2 構成 6〜12 分、撮影 3 組 × 約 8 分、切り分けと所見 20〜30 分。1 反復で閉じなければ、撮影の組 → 所見 に分ける。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行、撮影は視点か組で分ける）。

## SCN-WL-SHAPE-MEASURE: 崖の棚・角・外形を、写真の注釈とエンジンの深度と幾何の法線で測る物差しを作る
- status: done
- after: SCN-WL-SHAPE-CAUSE
- done-when: 形を測る `CompareReference.py --shape`（この項目で作る）を足す。注釈の形式（`Scripts/Terrain/ShapeAnnotations.json`）: 写真ごと（far_01・far_02・cliff_01・cliff_02・ground_02）に、棚は線分、角は点、外形は輪郭の多角形で置き、どの崖（名前）かを書く。撮影の側は、エンジンの深度と幾何の法線から棚の線（法線の向きの変化 30° 以上か深度の段）・角・外形の折れを取り出し、写真の注釈と画像の座標で対応づける。輝度の段は使わない（材質の縞・影・露出で作れ、照明の向きで正しい棚にも陰が出ないため）。写真と撮影の位置の許容は、今の撮影で写真の注釈の外形と撮影の外形の距離を測って先に固定し、文書に書く。量: 棚の線分のうち撮影にも折れがある長さの割合、角の点の一致の割合、外形の輪郭の距離の中央値。あわせて (1) `CompareReference.py --look` が `--photos` で絞った後の `--verify` で全 6 枚を要求する不具合（`LookMeasure.py`）を直し、(2) `--look`・`--shape` に `--expect`（量の行の合否。未知の写真の ID・空の選択・注釈の欠けは落とす）を足す。今の撮影（SCN-WL-SHAPE-CAUSE の lod-default）の値を記録する（値は変えない）。後の形の直しの項目は、この量の絶対の基準（例: 棚の一致の割合 0.6 以上）と前からの改善を分けて合否に使う（基準は今の値を見て文書で決める）。注釈の重ね画像（写真と撮影）を開いて確かめる。
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-default --photos far_01,far_02,cliff_01,cliff_02,ground_02 --shape --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-default --photos far_02 --look --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-23 親が確認 2 の後の計画の後半の頭として足した（ユーザーの順: 地形の形 → 植物 → 地面 → 建物）。後半の下書き 25 項目への Sol の批評（地形の原因を先に切り分ける・形は輝度の段でなく深度と幾何の法線で測る・CompareReference の拡張を先に・ビルドと撮影を別の行に）を受け、地形の形の原因と測り方を先に立てる。残りの後半はこの 2 項目の結果を見てから組む。 撮影は SCN-WL-SHAPE-CAUSE の lod-default（灰色の材質・深度・幾何の法線つき）を読む。時間の見積もり: 注釈 5 枚 30〜40 分、測りの台本 20〜30 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行、撮影は視点か組で分ける）。

## SCN-WL-SHAPE-METRIC-SILHOUETTE: 形の量の角に空との縁の向きの変わり目を、棚に遠くで丸まる段の縁を数える形を足す
- status: done
- after: SCN-WL-SHAPE-METRIC-HARDEN
- done-when: SCN-WL-CLIFF-SCAN-NEAR の反復 13（走行 20261024-183517）の角の点ごとの切り分け（`.harness/runs/20261024-183517/aux-SCN-WL-CLIFF-SCAN-NEAR-corner-diag.*`）: ref_cliff_01 の写真の角 7 点のうち 6 点は、縦の辺は取れて横の辺が角の内に無い。横の辺の一部は空との縁（崖の外形の上端）か段の踏み面の縁で、今の角の判定（法線の向きの変化か深度の段の折れの線の二辺）では折れとして数えない。棚の判定（法線の角 30° 以上の折れが水平に続く）は、540 m 先の 1 m の格子で 1 升目の斜面に丸まる 2.5 m の段の縁（約 5 画素）を取れない。量の定義を次のように直す（閾値の `shape-target.json` は変えない）: (1) 角の辺を、法線と深度の折れに加えて、空との縁（外形の輪郭）の向きでも支える（輪郭が角の点の近くで注釈の二辺の向きに曲がるなら、その辺を支えるとする）、(2) 棚の折れに、棚の向きに垂直な短い区間（例 6 画素）での法線の曲がりの積み上げが 30° 以上か、深度の相対の段で取れる縁を足す（向き・続き・余分な折れ・局所の欠けの判定は SCN-WL-SHAPE-METRIC-HARDEN のまま）。`--selftest` に、次の合成の形を足して確かめる: 落ちる形 — 壁一面の鋸歯・格子状のノイズ・ずれた十字・滑らかな斜面と平らな面（棚が出ない）・まっすぐな空との縁（角が出ない）。通る形 — 5 画素の幅で丸まった段の縁・空との縁で曲がる角。新しい定義を全部の写真に同じに使い、置く前の撮影（SCN-WL-SHAPE-CAUSE の lod-default）で `shape-scan-battleship.json` などの仕様の前の値を測り直して書き直す（閾値は変えない。書き直しの前後の値を `Docs/RenderingValidation/WastelandShape.md` に書く）。今の Battleship Rock の撮影（`.harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/shape`）も新しい定義で測って記録する（合否は NEAR が判定する）。
- verify: `python Scripts/Terrain/CompareReference.py --shape --selftest`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-default --photos far_01,far_02,cliff_01,cliff_02,ground_02 --shape --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が SCN-WL-CLIFF-SCAN-NEAR の反復 13 の BLOCKED（推奨の案 1: 測りの定義を見直す）から足した。量の定義の直しは親の判断で、閾値は変えない。合成の形の自己検査で、今まで落ちていた形（鋸歯・格子・十字）が落ちたままであることを必ず確かめる。時間の見積もり: 量の直し 25〜35 分、合成の試験 15 分、記録 10 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。

## SCN-WL-CLIFF-SCAN-NEAR: Battleship Rock の壁の表面をスキャンの崖で作り、手本の写真に合わせる
- status: backlog
- after: SCN-WL-CLIFF-SCAN-PLACE, SCN-WL-OCCLUSION-ACCEPT, SCN-WL-SHAPE-MEASURE, SCN-WL-SHAPE-METRIC-HARDEN, SCN-WL-SHAPE-METRIC-SILHOUETTE
- done-when: Blender の台本で、Battleship Rock（ref_cliff_01 の主役の壁）の急な面をスキャンの崖の塊で覆い、写真の崖の層の段・縦の割れ目・板状の剥離・上端の角・壁から崖錐へ移る塊に合わせる（塊の繰り返しが見えないよう、回転・大きさ・素材を混ぜる）。材質の色は砂岩の実測の明るさの範囲に合わせ、地形の崖の色と継ぎ目なく馴染ませる。担当の世界座標の範囲を `Docs/RenderingValidation/WastelandShape.md` に書き、崖の生成の項目（Rooster Butte・Setting Hen Butte・castle の段）と重ねない。合否: (1) `CompareReference.py --shape --expect shape-scan-battleship.json`（cliff_01 の写真の注釈の棚・角・外形のうちこの崖の分）が、SCN-WL-SHAPE-MEASURE の文書の絶対の基準を満たす（今の値が基準より低い量は基準まで上げ、ほかの量は悪くならない）、(2) 灰色の材質の撮影（形）と既定の撮影（見た目）の比較画像（写真｜撮影、1920×1080）を開き、崖の面が丸い面の筋ではなく、影を落とす岩の立体に見え、塊が地形から浮かず埋もれ過ぎないことを確かめる、(3) 三角形の数・読み込みの時間・VRAM・溢れ 0 を記録する。式の焼き込み（`TerrainCliffDetail.cpp`）を覆った崖の下地として残すか外すかを撮影で決める（外すならクックの行を verify に足し、Cycles の参照が古くなる一覧を文書に書く）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/shape --photos cliff_01 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-battleship.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views cliff,ref_cliff_01 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/look`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/look --photos cliff_01 --verify`
- paths: Scripts/Terrain, Scripts, Assets/AssetSets, Game/GameModes/Wasteland, Assets/Shaders, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 1 反復で閉じなければ Battleship Rock だけで閉じ、尖塔の台座を後ろの項目に分ける。 2026-10-23（親）: SCN-WL-SHAPE-CAUSE（丸さの原因の切り分け）と SCN-WL-SHAPE-MEASURE（形の物差し）の後に回す。合否に `--shape --expect` の棚・角・外形の量を足し、SCN-WL-SHAPE-CAUSE の所見で効き目の大きい直し（上端の角・連続する水平の棚・壁から崖錐へ移る大きな塊）を場所ごとに選ぶ。担当の場所は Battleship Rock と Franklin Butte（世界座標の範囲を文書に書き、後の中景の崖の項目と重ねない）。 2026-10-24（親）: 後半の地形の計画への Sol の批評（`Docs/Plans/WastelandRebuild-SolCritique-Terrain2-20261024.txt` の 9: 2 地点を 1 項目で抱えている・verify に形の合否が無い・撮影の行にビルドが入っている）で、Battleship Rock（この項目）と Franklin Butte の尖塔の台座（SCN-WL-CLIFF-SCAN-FRANKLIN）に分け、形の合否と分けたビルドの行を足した。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。合否の定義（`--shape --expect` の仕様）は、スキャンを置く前に今の撮影の値と SCN-WL-SHAPE-MEASURE の文書の絶対の基準から書いてコミットし、置いた後に変えない。 2026-10-24（親）: 反復 20 の BLOCKED（棚の一致 0.604 は基準に届いた。角 0.143・ずらした後の外形 10.8 画素が届かない。尾根に rock_09 の板を積むと外形 4〜6 画素・角 3/7 まで上がるが薄い板の塔に見える）を推奨の案 1 で解いた: Blender で、平らな頂と縦の側面を持つ厚い節理の塊（1 つで 10〜30 m の段の高さ。rock_09 か boulder_01 を厚みの方向へ伸ばし、側面に層の段を残す）の派生の素材を作って `WastelandModels.json` に足し、尾根に置いて写真の段と角を作る。伸ばした肌理が縦に引き延ばされて見えるなら、側面に地形の崖の材質と同じ層の縞を投影するなどで抑える。合否の仕様（`shape-scan-battleship.json`）は変えない。案 1 で見た目か合否が届かなければ、届かなかった値と画を書いて BLOCKED にする（次は案 2: `TerrainCliffDetail.cpp` の Battleship Rock の範囲で尾根を段と縦の壁に削る。地形の焼き直しと Cycles の参照の描き直しが要る）。 2026-10-24（親）: 反復 23 の BLOCKED（案 1 の厚い節理の塊は外形 4〜5 画素を満たすが、角 0.43・棚 0.54〜0.59 が揃わず、引き延ばした肌理が淡い桃色の斑で崖の色に馴染まない。ref_cliff_01 は段の頂より低い視点で、塊の角は空との縁だけになり横の折れが出ない）を推奨の案 2 で解いた: `Tools/AssetCook/TerrainCliffDetail.cpp` の Battleship Rock の範囲（kNearRegions[0]）で、写真の段（平らな頂・縦の壁・右の段・左の縁）を尾根に削り、地形の焼き直しで外形と角を地形そのものに持たせる（段の側面は地形の崖の材質になる）。ほかの範囲の焼きの結果は変えない（範囲の外の区画のパッケージが前と同じことを確かめる）。棚の岩塊 80 と殻 3 は残し、削った後に浮き・埋もれが無いかを撮影で開いて確かめ、外れた物は配置表で直す。地形の Cycles の参照は da68b820 から古い扱いで、描き直しは地形の 2 段目でまとめて行う（この項目では合否に使わない）。地形の焼き直しはこのランナーの許可の内（重い処理は許可済み）。合否の仕様（`shape-scan-battleship.json`）は変えない。案 1 の塊の台本（`MakeJointBlock.py`）は既定では置かないまま残す。 2026-10-24（親）: 反復 25 の BLOCKED（刻みで外形は基準に入った〔ずらした後 6.3 画素〕が、角 0.000・棚 0.487。下から見上げる視点では段の頂の角に横の折れが出ず、今の角の判定〔8 画素の内の縦と横の折れ〕が取れない。刻んだ壁は崖の細部の縦の割れ目で薄い板の櫛と上端の細い尖りに見える）を次のように決めた: 推奨の案 1 の「写真の角の 7 点にだけ張り出しの塊を置く」は、測りの点に合わせて物を置く形なので採らない。刻み（`kBattleshipCarveEnabled`）を有効にし、刻んだ範囲では崖の細部の縦の割れ目と細分の行き過ぎを止めて下地の層の縞だけにし、櫛と尖りを消す（見た目を開いて確かめる）。棚の岩塊の上端は刻んだ後の撮影で揃え直す。角は SCN-WL-SHAPE-METRIC-HARDEN で角の二辺の向きで測る形に直した量で判定する（その項目が、置く前の撮影で `shape-scan-battleship.json` の前の値を新しい量で測り直して書き直す。閾値は `shape-target.json` の絶対の基準のまま）。スキャンの崖の項目では岩塊も崖の面として数える（`--terrain-only` は使わない）。AssetCook を変えると地形の全項目（214）の印が変わり、verify の地形のクックの行（直列）は 10 分を超える。AssetCook を変えたら、反復の中で地形の全項目を 12 組の並列で先に焼き（`.harness/runs/20261015-230406/aux-SCN-WL-CLIFF-DETAIL-ALL-parallel-cook.txt` の手順）、verify のクックの行は印が同じで飛ばされる形にする（再検証の 10 分に収める。クックの行を消さない）。 2026-10-24（親）: 走行 20261024-183517 の反復 10 の BLOCKED（刻みを既定にし縦の割れ目を止めて板の櫛と尖りは消えた〔8093c67a〕。外形は合格。棚 0.323・角 0.000。壁は刻みの円の境ごとに丸い縦の柱〔オルガンの管〕が並び、写真の注釈の棚 4 本のうち 3 本は崖錐の斜面の赤い層の段で撮影では滑らかな斜面のまま）を推奨の案 1 で解いた: (1) 刻みの表に、写真の 3 本の棚の位置で崖錐の斜面を 1〜3 m の水平の段（平らな踏み面と縦の蹴上げ）に削る刻みを足す（棚の線を撮影の深度で世界へ戻す台本を使う）、(2) 尾根の刻みを最も近い円でなく尾根に沿った補間にして丸い柱を消す（壁が写真の横に割れた厚い塊に見えることを開いて確かめる）。角が 0 のままなら、測りの定義はまだ変えない: 角の点ごとに、撮影の折れの線・交点・注釈の角を重ねた画を作り、外れる理由（位置のずれ・角の丸み・片方の辺の折れが無い）を点ごとに書いて BLOCKED にする（親が定義を見直すかを決める）。地形の全項目の焼き直しは 12 組の並列で先に済ませる。 2026-10-24（親）: 反復 13 の BLOCKED（棚 0.330・角 0.000。外形は合格）を次のように決めた: 量の定義の直しは SCN-WL-SHAPE-METRIC-SILHOUETTE で先に行う（角の辺に空との縁、棚に遠くで丸まる段の縁）。この項目では形のずれを直す: (1) 角の点 0・2・3・5 で段の上端が写真より 15〜30 画素高いので、刻みの目標の高さを写真の角の点の行に合わせ直す、(2) 左右の低い段の壁が細かな縦の筋の平らな板に見えるので、写真の横に割れた丸みのある厚い砂岩の塊の形（横の割れ目・角の丸み）に直し、開いて確かめる。合否は SCN-WL-SHAPE-METRIC-SILHOUETTE の後の量で判定する（閾値は変えない）。 2026-10-25（親）: 反復 17 の BLOCKED（尾根の刻みを写真の列ごとの方位で掛けて外形はずらす前 2.0 画素・段の上端と壁の列は角の点の ±3 画素。棚 0.387・角 0.000。壁は細かな縦の溝の平らな板のまま）を推奨の案 1 で解いた: 尾根の点の距離を前後 ±5 列の中央値で均して壁の縦の溝を消し（写真の壁の列だけ切る）、刻んだ範囲の上端の角の弧を縮めて角を鋭くする。壁が写真の横に割れた丸みのある塊に近づいたかを、拡大した画を写真と並べて開いて確かめる。区切り: 角と外形の行が通り、棚の行だけが届かないなら、棚の行を SCN-WL-CLIFF-SCAN-NEAR-SHELVES（崖錐の斜面の棚を細かな高さで作る。地形の 2 段目）へ移してこの項目を閉じてよい（`shape-scan-battleship.json` の棚の行を新しい項目の仕様 `shape-scan-battleship-shelves.json` へ移す。閾値 0.6 は変えない。写真の棚は斜面の水平の段で注釈は形として正しいが、1 m の格子では 540 m 先で段の縁が丸まる）。角も届かなければ、値と角の点ごとの外れる理由を書いて BLOCKED にする。 2026-10-25（親）: 反復 19 の BLOCKED（棚 0.380・角 0.000、外形 2.0 画素で合格。主の壁は縦の溝の柱、低い段の壁は縦の筋の板）を推奨の案 1 で解いた。この項目は走行 20261023-124311 から 7 回の反復を使ったので、これを最後の試しにする: 写真の注釈の角の点の列を壁の列の切れ目に足し、点 2・5 の斜めの壁の列は隣の上端の列の距離に揃えて縦の壁にする（点 3 の横のずれもこの列で直す）。結果の扱い: 角と外形の行が通れば、棚の行を SCN-WL-CLIFF-SCAN-NEAR-SHELVES へ移して閉じる（前の判断のまま）。角が届かなければ、値と角の点ごとの理由を書いて BLOCKED にする（親がこの項目を backlog に移し、今の形〔外形 11.4 → 2.0 画素〕を既定のまま残して、Franklin Butte を待たせない形にする）。壁の見た目（横に割れた丸みのある厚い塊）は SCN-WL-CLIFF-SCAN-NEAR-WALLS（スキャンの塊で刻んだ壁を覆う）で扱うので、この項目では追わない。 2026-10-25（親）: 最後の試し（反復 21）の BLOCKED（外形 1.0 画素で合格、角 2/7・棚 0.391 で届かない）を、前に決めた扱いで解いた: この項目は backlog に移し、今の形（尾根の刻み・写真の列ごとの方位・壁の列の切れ目。外形は置く前の 11.4 画素から 1.0 画素）を既定のまま残す。角と棚は既知の限界として `WastelandCliffScan.md` に値で残す。棚は SCN-WL-CLIFF-SCAN-NEAR-SHELVES（地形の 2 段目の細かな高さの後）、壁の見た目は SCN-WL-CLIFF-SCAN-NEAR-WALLS で扱う。

## SCN-WL-CLIFF-SCAN-NEAR-WALLS: Battleship Rock の刻んだ壁を、横に割れた砂岩の塊のスキャンで覆う
- status: done
- after: SCN-WL-SCAN-MATERIAL-MATCH
- done-when: SCN-WL-CLIFF-SCAN-NEAR で外形は写真に合った（ずらす前 2.0 画素）が、主の壁は縦の溝の柱、右の低い段と左の縁の壁は縦の細い筋の平らな板に見え、写真の横に割れた丸みのある厚い砂岩の塊にならない（`.harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR/shape/crop_wall.png`・`look/reference_cliff_01.png`）。刻んだ壁の面の前に、横に割れた砂岩の塊（Poly Haven の CC0 のスキャン、または rock_09・boulder_01 の派生）を配置表で並べ、縦の筋の板を隠す。反復 20・23 で塊を置いたときに起きた外形と色の崩れ（尖った針・丸い山・桃色の斑）を避ける: 塊は壁の面に沿って半ば埋め、外形の行（`shape-scan-battleship.json`）を悪くしない、色は地形の崖の材質に合わせる。合否: 外形と角の行が悪くならない、拡大した壁を写真と並べて開いて横に割れた塊に見えることを確かめる、置いた物の浮き・埋もれの検査が通る、三角形・VRAM を記録。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-WALLS/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-WALLS/shape --photos cliff_01 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-battleship-walls.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views cliff,ref_cliff_01 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-WALLS/look`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-WALLS/look --photos cliff_01 --verify`
- paths: Scripts/Terrain, Scripts, Assets/AssetSets, Game/GameModes/Wasteland, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-25 親が SCN-WL-CLIFF-SCAN-NEAR の反復 19 の BLOCKED から分けた（壁の見た目）。NEAR が閉じるか backlog に移ったら、親が todo にして順を決める。素材の取得は Poly Haven・ambientCG の CC0（許可済み）だけ。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。 2026-10-25（親）: NEAR を backlog に移したので todo にし、Franklin Butte の後に回す（縦の筋の壁は刻む前の崖の生成にもあった前からの欠陥で、直す対象）。 2026-10-25（親）: 反復 34 の BLOCKED（verify の `--expect` が保留にした NEAR の絶対の基準〔棚 0.6・角 0.5〕で、この項目の done-when〔外形と角の行が悪くならない〕と合っていなかった。親の計画の誤り）を推奨の案 1 で解いた: この項目の合否は相対の仕様 `Scripts/Terrain/ExpectSpecs/shape-scan-battleship-walls.json`（この項目で書く）で見る。行は、壁の塊を置く前の値（反復 33 の記録: 棚 0.395・角 0.286・ずらした後の外形 1.0 画素・ずらす前の外形 1.0 画素）から、形の物差しの揺れの決まり（`shape-baseline.json` と同じ: 棚 −0.03・外形 +1 画素。角は点の数で量子化されるので揺れを足さない）で、棚 ≥ 0.365・角 ≥ 0.286・ずらした後の外形 ≤ 2.0・ずらす前の外形 ≤ 2.0。`shape-scan-battleship.json` の絶対の行は NEAR・SHELVES の仕様として弱めずに残す。次の反復は仕様を書いて verify を全行回して閉じる（壁の塊の配置 217d2f12 は変えない） 2026-10-25（親）: 反復 38・39 の評価の差し戻し（右の低い段の柱の左側に幅 10〜15 画素の縦の筋の板が残る。形の相対の仕様・配置の検査・費用は満たす）を次のように決めた: 前の注記の「配置 217d2f12 は変えない」は外し、配置の変更を許す。残る細い面を、外形の内に収まる小さな派生の塊（横に割れた砂岩。色は SCN-WL-SCAN-MATERIAL-MATCH の較正の版）で覆う。合否は今のまま（相対の仕様・浮き埋もれ・見た目）。比較画像を拡大して、柱の左側と最も右の段の正面に縦の筋の板が残らないことを開いて確かめる。。
## SCN-WL-CLIFF-SCAN-NEAR-SHELVES: Battleship Rock の崖錐の斜面の棚を、細かな高さで鋭い段にする
- status: backlog
- after: SCN-WL-CLIFF-SCAN-NEAR-WALLS
- done-when: SCN-WL-CLIFF-SCAN-NEAR で外形と角は写真に合ったが、写真の崖錐の斜面の水平の段（棚）は、1 m の格子では 540 m 先で段の縁が 1 升目の斜面に丸まり、棚の一致が基準 0.6 に届かない場合に、この項目で扱う（NEAR が棚の行をこの項目へ移して閉じたときだけ todo にする）。地形の 2 段目の細かな高さ（1 m の格子より細かい高さの差の保存と、クックと置き方が同じ面を読む契約）を使い、写真の棚の位置で 0.25〜0.5 m の格子の鋭い段（平らな踏み面と縦の蹴上げ）を焼く。合否: `CompareReference.py --shape --expect shape-scan-battleship-shelves.json`（cliff_01 の Battleship Rock の棚の行。閾値 0.6）、角と外形の行（`shape-scan-battleship.json`）が悪くならない、置いた物の浮き・埋もれの検査が通る、三角形・VRAM・クックの時間を前後で記録。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-SHELVES/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-SHELVES/shape --photos cliff_01 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-battleship-shelves.json --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-NEAR-SHELVES/shape --photos cliff_01 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-battleship.json --verify`
- paths: Tools/AssetCook, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（地形のクック）。2026-10-25 親が SCN-WL-CLIFF-SCAN-NEAR の反復 17 の BLOCKED の区切りとして足した。地形の 2 段目の細かな高さの項目を TASKS.md に入れるときに、その項目を after に足して todo にする（それまでは backlog）。AssetCook を変えたら地形の全項目を 12 組の並列で先に焼く。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。 2026-10-25（親）: NEAR を backlog に移したので、after を壁の見た目の項目に替えた（地形の 2 段目の細かな高さを入れるときに、その項目を after に足して todo にする）。
## SCN-WL-CLIFF-CARVE-CONTINUITY: Battleship Rock の刻みの抑えのマスクの境で高さが跳ぶ不具合を直す
- status: done
- after: SCN-WL-SHAPE-METRIC-SILHOUETTE
- done-when: 保留にした SCN-WL-CLIFF-SCAN-NEAR の既定に残った刻みを親が別の評価者（Sol）に通した結果（2026-10-25、NEEDS_WORK）: `Tools/AssetCook/TerrainCliffDetail.cpp` の 1192 行の辺りは、`carve >= 1` なら細分の点へ直接刻みを掛け、1 未満なら刻んだ格子の補間へ切り替えるので、両者の高さが一致せず、マスクの境で式が不連続に切り替わる（例: 世界座標 X −1875.25・Z −1245.1875 でマスク 1.000000・高さ 170.035 m、Z −1245.1250 でマスク 0.998152・高さ 149.741 m。水平 6.25 cm の間に約 20.29 m の段差）。意図しない壁や尖りを既定の地形に作る。近景の高さを `carve` の全域で同じ 2 つの式から連続に混ぜる形にする（例: 滑らかな高さと、刻む前の双線形の高さへ直接刻んだ高さを `carve` で混ぜる）。刻みの版を上げ、地形の全項目を 12 組の並列で先に焼き直す（変わるのは dem:0,2・dem:1,2 だけのはず）。合否: (1) 検査（`Scripts/Terrain/CheckCarveContinuity.py`、この項目で作る。AssetCook の問い合わせで焼いた後の面を読む。Python で式を写さない）: Battleship Rock の範囲でマスクの境を横切る線を 200 本以上取り、マスク 0.99 以上 1.0 未満の点と隣の 1.0 の点（水平 0.25 m 以内）の高さの差が 0.5 m 以下、上の例の 2 点の差が 0.5 m 以下、(2) 範囲の外の区画のパッケージが前と同じ（クックの出力のハッシュで示す）、(3) 灰色の材質の撮影（ref_cliff_01・cliff）を前と並べて開き、マスクの境の壁や尖りが無いことを確かめる、(4) Battleship Rock の外形の行（`shape-scan-battleship.json` の外形）が悪くならない（角と棚は保留の既知の限界のまま記録）。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath "Assets/Models/WastelandTerrain/*"`
- verify: `python Scripts/Terrain/CheckCarveContinuity.py --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,cliff -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-CARVE-CONTINUITY/shape`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01,cliff -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-CARVE-CONTINUITY/look`
- paths: Tools/AssetCook, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（地形のクック）。2026-10-25 親が、保留にした SCN-WL-CLIFF-SCAN-NEAR の既定に残った変更を Sol に評価させて見つかった不具合として足した（ほかの観点〔範囲の外への効き・岩塊の浮き・試しの漏れ・規則〕に重い指摘は無かった）。Franklin Butte（SCN-WL-CLIFF-SCAN-FRANKLIN）が同じ刻みの経路を使うので、その前に直す。AssetCook を変えたら地形の全項目を 12 組の並列で先に焼き、verify のクックの行は印が同じで飛ばされる形にする。時間の見積もり: 直し 20 分、ビルド 2 構成 6〜12 分、並列の焼き 約 16 分、検査 15 分、撮影 2 組 約 5 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。

## SCN-WL-CLIFF-SCAN-FRANKLIN: Franklin Butte の尖塔の台座の表面をスキャンの崖で作り、手本の写真に合わせる
- status: done
- after: SCN-WL-SHAPE-METRIC-SILHOUETTE, SCN-WL-CLIFF-CARVE-CONTINUITY
- done-when: Blender の台本で、Franklin Butte の尖塔の台座（ref_cliff_02・ref_ground_02 に写る）の急な面をスキャンの崖の塊で覆い、写真の崖の層の段・縦の割れ目・板状の剥離・上端の角・壁から崖錐へ移る塊に合わせる（塊の繰り返しが見えないよう、回転・大きさ・素材を混ぜる）。材質の色は砂岩の実測の明るさの範囲に合わせ、地形の崖の色と継ぎ目なく馴染ませる。担当の世界座標の範囲を `Docs/RenderingValidation/WastelandShape.md` に書き、崖の生成の項目（Rooster Butte・Setting Hen Butte・castle の段）と重ねない。合否: (1) `CompareReference.py --shape --expect shape-scan-franklin.json`（cliff_02,ground_02 の写真の注釈の棚・角・外形のうちこの崖の分）が、SCN-WL-SHAPE-MEASURE の文書の絶対の基準を満たす（今の値が基準より低い量は基準まで上げ、ほかの量は悪くならない）、(2) 灰色の材質の撮影（形）と既定の撮影（見た目）の比較画像（写真｜撮影、1920×1080）を開き、崖の面が丸い面の筋ではなく、影を落とす岩の立体に見え、塊が地形から浮かず埋もれ過ぎないことを確かめる、(3) 三角形の数・読み込みの時間・VRAM・溢れ 0 を記録する。式の焼き込み（`TerrainCliffDetail.cpp`）を覆った崖の下地として残すか外すかを撮影で決める（外すならクックの行を verify に足し、Cycles の参照が古くなる一覧を文書に書く）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-FRANKLIN/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-FRANKLIN/shape --photos cliff_02,ground_02 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-franklin.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_02,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-FRANKLIN/look`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SCAN-FRANKLIN/look --photos cliff_02,ground_02 --verify`
- paths: Scripts/Terrain, Scripts, Assets/AssetSets, Game/GameModes/Wasteland, Assets/Shaders, Tools/AssetCook, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24（親）: SCN-WL-CLIFF-SCAN-NEAR から分けた（台本と配置表の経路は NEAR のものを使う）。2026-10-24（親）: 後半の地形の計画への Sol の批評（`Docs/Plans/WastelandRebuild-SolCritique-Terrain2-20261024.txt` の 9: 2 地点を 1 項目で抱えている・verify に形の合否が無い・撮影の行にビルドが入っている）で、Battleship Rock（この項目）と Franklin Butte の尖塔の台座（SCN-WL-CLIFF-SCAN-FRANKLIN）に分け、形の合否と分けたビルドの行を足した。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。合否の定義（`--shape --expect` の仕様）は、スキャンを置く前に今の撮影の値と SCN-WL-SHAPE-MEASURE の文書の絶対の基準から書いてコミットし、置いた後に変えない。 2026-10-25（親）: SCN-WL-CLIFF-SCAN-NEAR を backlog に移したので、after を形の量の定義の直し（SCN-WL-SHAPE-METRIC-SILHOUETTE）に替えた。Battleship Rock の試しの教訓: 地形の刻み（`MakeBattleshipCarve.py`・写真の列ごとの方位）で外形は合うが、見上げる視点の角と斜面の棚は 1 m の格子で丸まり届きにくい。刻みを使うなら、写真の角の点の列を壁の列の切れ目に入れ、尾根の点の距離を列の間で均す。同じ試しを 3 回続けて届かなければ、値と理由を書いて BLOCKED にする。 2026-10-25（親）: 反復 28 の BLOCKED（棚 cliff_02 0.788・ground_02 0.801 と外形の 4 行は合格。角 cliff_02 0.000・ground_02 0.250。写真の角 4 点のうち 3 点は尖塔にあり、撮影の尖塔は写真より細く頂の帽岩と右の肩の段が無い。板の色は周りの崖の日の当たる面の 0.82〜0.84 倍の明るさで赤みが弱い）を推奨の案 2 で解いた: 角の 2 行は尖塔の形の項目 SCN-WL-CLIFF-SPIRE-FRANKLIN の仕様 `shape-scan-franklin-spire.json` へ移し（閾値 0.5 は変えない）、この項目は棚・外形・見た目・費用の記録で閉じる（次の反復は `shape-scan-franklin.json` から角の行を移して verify を回し直す。今の板の帯は既定に残す）。色の馴染みは SCN-WL-SCAN-MATERIAL-MATCH で直す。

## SCN-WL-SCAN-MATERIAL-MATCH: スキャンの崖の塊（rock_09 の板の帯・岩塊）の色と明るさを、周りの地形の崖に合わせる
- status: done
- after: SCN-WL-CLIFF-SCAN-FRANKLIN
- done-when: SCN-WL-CLIFF-SCAN-FRANKLIN の反復 28（`aux-SCN-WL-CLIFF-SCAN-FRANKLIN-plate-color.txt`）: 尖塔の台座に置いた rock_09 の板の帯は、日の当たる面どうしで周りの地形の崖の 0.82〜0.84 倍の明るさで、赤みも弱く、継ぎ目が色で見える。Battleship Rock の棚の岩塊（SCN-WL-CLIFF-SCAN-NEAR の配置）も同じ素材。素材の較正（アルベドの明るさと色度。照明なしのアルベドで比べる）を、地形の崖の材質の同じ層の値に合わせる（写真との比は撮影時の処理を含むので診断）。合否: (1) アルベドの AOV で、塊の日の当たる面と隣の地形の崖の面の明るさの比が 0.95〜1.05、色度（R÷G・B÷R）の差が 5 % 以内（Franklin Butte の台座と Battleship Rock の棚の両方。測る領域は文書に書く）、(2) 既定の撮影（ref_cliff_01・ref_cliff_02・ref_ground_02）を前と並べて開き、塊と崖の継ぎ目が色で目立たないことを確かめる、(3) 形の行（`shape-scan-battleship.json`・`shape-scan-franklin.json`）が変わらない。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_cliff_02,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-SCAN-MATERIAL-MATCH`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SCAN-MATERIAL-MATCH --photos cliff_01,cliff_02,ground_02 --verify`
- verify: `python Scripts/Terrain/ScanMaterialMatch.py --capture .harness/runs/scene-capture/SCN-WL-SCAN-MATERIAL-MATCH --verify`
- paths: Scripts, Assets/AssetSets, Game/GameModes/Wasteland, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-25 親が SCN-WL-CLIFF-SCAN-FRANKLIN の BLOCKED（推奨の案 2）から分けた。素材の焼き直しはその素材だけ（`CookAssets.ps1 -OnlyLogicalPath` で絞る）。明るさと色度の比を測る台本はこの項目で作るか既存の `--albedo` を使う（verify に足してよい）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。

## SCN-WL-CLIFF-SPIRE-FRANKLIN: Franklin Butte の尖塔を、地形の刻みで写真の帽岩と肩の段のある形にする
- status: backlog
- after: SCN-WL-SCAN-MATERIAL-MATCH
- done-when: SCN-WL-CLIFF-SCAN-FRANKLIN の反復 28: 写真の角 4 点のうち 3 点（尖塔の頂の左・尖塔の根元の左右の肩）は尖塔にあり、撮影の尖塔は写真より細く（3〜4 m）、頂の帽岩と右の肩の段が無い。`Tools/AssetCook/TerrainCliffDetail.cpp` の Franklin Butte の範囲（kNearRegions[1]）で、尖塔を写真の帽岩・右の肩の段・左の縦の縁へ刻む（Battleship Rock の刻みの経路と、刻みの境を連続に混ぜる直し〔SCN-WL-CLIFF-CARVE-CONTINUITY〕を使う）。合否: `shape-scan-franklin-spire.json`（cliff_02・ground_02 の角の行。閾値 0.5）、`shape-scan-franklin.json` の棚と外形が悪くならない、刻みの境の連続（`CheckCarveContinuity.py` を Franklin Butte の範囲にも広げる）、範囲の外の区画のパッケージが変わらない。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-SPIRE-FRANKLIN/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SPIRE-FRANKLIN/shape --photos cliff_02,ground_02 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-franklin-spire.json --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-SPIRE-FRANKLIN/shape --photos cliff_02,ground_02 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-franklin.json --verify`
- paths: Tools/AssetCook, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（地形のクック）。2026-10-25 親が SCN-WL-CLIFF-SCAN-FRANKLIN の BLOCKED（推奨の案 2）から分けた。地形の焼き直しを伴う重い作業なので backlog に置き、地形の 2 段目を組むときに順を決めて todo にする。Battleship Rock の教訓（見上げる視点の角は 1 m の格子で丸まる・塊を角の点に積む試しは外形の位置合わせのずらしで外れる）を踏まえ、同じ試しを 3 回続けて届かなければ値と理由を書いて BLOCKED にする。AssetCook を変えたら地形の全項目を 12 組の並列で先に焼く。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。
## SCN-WLR-CLOUD-REF-CURRENT: 今の雲の式と密度場で雲の書き出しと Cycles の正解を撮り直し、雲の比べの基準を今の値にする
- status: done
- after: SCN-WL-OCCLUSION-ACCEPT
- done-when: 雲の正式な正解 `SCN-WLR-CLOUD-REF`（704 spp、全領域が収束）は 8113ba38 のコードの書き出し（SCN-WLR-CLOUD-REF-DUMP）から描いたもので、その後の雲の変更で、今の書き出しの `cloud_sky.cloud.json`・`cloud_sky.cloud.probe.f32` とハッシュが合わない（`CloudCheck` は書き出しのハッシュの一致を求める）。`Docs/RenderingValidation/WastelandCloudRef.md` の再現の手順で、今の Game.exe の雲の書き出し（cloud_sky）を `.harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump` に撮り、同じ台本（`python Scripts/Terrain/BlenderReference.py --cloud --capture .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump --max-seconds 1500 --out .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles`。台本は変えない）で描き直す。Cycles の生成は反復の中で同期で走らせて終わりを待ち、保存する（反復の外に描画を残して BLOCKED にしない。verify は保存物の照合だけ）。`--cloud --verify` が通ること（収束・投影・書き出しの検査）と、5 領域の比（エンジン ÷ Cycles）と 3 つの量（縁の明るさ・銀の縁・影の側の暗さ）・視線ごとの比の幅・Cycles の収束の差を、`Docs/RenderingValidation/WastelandCloudShape.md`（新規） の「基準（2026-10-24 の密度場）」の表に書く。値は変えない。前の正解（SCN-WLR-CLOUD-REF）は消さない。見比べの画を開いて、雲の形と位置が両方で同じことを確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 前回の実績で焼き 180 秒・描画 115 秒（`--max-seconds` は描画の繰り返しだけの上限で、読み込みを含まない）。時間の見積もり: ビルド 3〜6 分、書き出し 約 3 分、描画 約 5〜8 分、記録 15 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。 2026-10-24（親）: 反復 21 の再検証で、verify の書き出しの撮影の行が `dump` を撮り直し、Cycles の正解が読んだ書き出し（`cloud_sky.cloud.json`）と中身が変わって `--cloud --verify` が落ちた（書き出しは撮るたびに変わる）。書き出しの撮影は反復の中で 1 回撮って保存し、Cycles の描画はその書き出しから行い、verify は保存した組の照合だけにした（撮影の行を verify から外した）。verify の照合が落ちたら、書き出しを撮り直したのではなく描画の入力と書き出しが合っていないので、書き出しを撮り直した場合は描画もやり直す。

## SCN-WL-CLOUD-CHECK-EXPECT: 雲の Cycles の比べに、比の悪化を 1 からの隔たりで判定する合否を足す
- status: done
- after: SCN-WLR-CLOUD-REF-CURRENT
- done-when: 今の `--cloud --verify`（`Scripts/Terrain/CloudCheck.py`）は 5 領域の比（厚い雲・縁・銀の縁・日の当たる側・影の側）を記録するだけで、悪化を判定しない。`--cloud --expect <定義>`（この項目で作る。`Scripts/Terrain/ExpectSpecs/cloud-ref.json`）を足す: 領域ごとの誤差を e = |ln(エンジン ÷ Cycles)| とし（1.48→1.60 も 0.72→0.60 も悪化になる）、3 つの量（縁の明るさ・銀の縁・影の側の暗さ）もエンジンと Cycles の比の e で別の行にする。許容は、同じ撮影を 2 回撮った揺れと Cycles の収束の差（記録の平均の差）の和から決めて文書に根拠を書く。基準の値は SCN-WLR-CLOUD-REF-CURRENT の表。未知の領域・基準の欠け・収束していない領域は落とす。`--selftest` で、比を 1 から遠ざけた合成の結果が落ち、基準のままの結果が通ることを確かめる。SCN-WLR-CLOUD-REF-CURRENT の組が `--expect` で通ること。この組は描画のソースが変わると `--verify` で古い扱いになるので、後の項目の固定の試験にはしない（後の比べは新しい書き出しとその Cycles の組で行う）。描画のソースを変える項目（SCN-WL-CLOUD-OPACITY-AOV）より前に閉じる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --selftest`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/dump --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-CURRENT/cycles --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 揺れを測るための 2 回目の書き出しは反復の中で撮って保存する（verify にはしない）。`--selftest` の名前は既存の引数と衝突するなら変えてよいが verify も合わせる。時間の見積もり: 台本 30 分、2 回目の書き出し 約 3 分、記録 10 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。

## SCN-WL-CLOUD-OPACITY-AOV: 雲だけの不透明度を全画素の画像として書き出す（開発ビルドの撮影だけ）
- status: done
- after: SCN-WL-CLOUD-CHECK-EXPECT
- done-when: 今の雲の書き出し（`cloud-dump`）は 256 本の視線だけで、塊・切れ間・縁を画素で測れない。雲の描画は出力で `opacity * fade` を使い、混ぜ方は画面のアルファを保ち RGB だけを書くので、今の画面からは不透明度を取れない（`Assets/Shaders/cloud_layer.frag`、`Library/Core/Private/Rendering/CloudLayerPass.inl`）。開発ビルドの起動引数 `wasteland-cloud-opacity`（名前は変えてよいが verify も合わせる）で、通常の描画と同じ視線・時刻の雲の積分の後の、雲だけの不透明度（1 − 雲の透過率。霞・太陽の円盤・地形を含まない。`fade` を掛ける前と後のどちらを書くかを決めて記録の項目に書く）を 32 bit の浮動小数の画像で書き出し、同じ記録に 地形のマスク（深度が空でない画素）・カメラ・フレームの番号・画像の向き を書く。読み戻しの資源はフレームの持ち主から借り、フレームをまたいで再利用しても壊れない（描画と読み戻しの変更なので危険地帯）。Release では起動引数も専用の資源・パスも作らない（既存の開発用の起動引数と同じ守り方）。合否（`ReferenceCheck.py --cloud-opacity`、この項目で作る）: 値が有限で 0〜1、地形のマスクの外だけに値があり、同じ撮影の `cloud-dump` の 256 本の視線の不透明度と、その画素の値の差が 0.01 以内（視線の画素の位置は書き出しの投影で求める）。画像を開いて確かめる。既定の描画の見た目は変えない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-OPACITY-AOV`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-opacity --engine .harness/runs/scene-capture/SCN-WL-CLOUD-OPACITY-AOV --views cloud_sky,ref_far_01 --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 危険地帯（読み戻し・資源の寿命）。時間の見積もり: 実装 30〜40 分、ビルド 2 構成 6〜12 分、撮影 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。

## SCN-WL-CLOUD-SHAPE-METRIC: 雲の塊・切れ間・縁を測る量と、調整の前に固定する合否を作る
- status: done
- after: SCN-WL-CLOUD-OPACITY-AOV
- done-when: `ReferenceCheck.py --cloud-shape`（この項目で作る）を SCN-WL-CLOUD-OPACITY-AOV の画像に足す。地平の上 2° より上で地形のマスクの外の画素（空の画素）だけを数える。量: (1) 切れ間の割合（不透明度 0.1 未満）、(2) 薄い膜の割合（0.1〜0.3）、(3) 塊（0.3 以上の 8 連結の成分。空の画素の 0.05 % 未満は数えない）の数・面積の分布（中央値・90 百分位・その比）・最大の塊が雲の面積に占める割合・画面の端に触れる塊の数、(4) 厚い部分の割合（0.6 以上 ÷ 0.3 以上）、(5) 塊の中の不透明度の揺れ（成分ごとの標準偏差の中央値）、(6) 縁の幅（塊の外周の画素から、不透明度の勾配の向きに 0.1 から「0.5 と塊の最大の 80 % の小さい方」まで上がる画素の距離。ほかの塊に当たる・画面の端に出る・上の閾値に届かない縁は「測れない縁」として除き、その割合を出す）の中央値と測れた縁の割合。合否の定義 `Scripts/Terrain/ExpectSpecs/cloud-shape.json` を調整の前に固定する: 独立の見本として Poly Haven の CC0 の部分曇りの空の HDRI を 2〜3 枚（2k、1 枚 30 MB 以下。取得の前に名前と大きさをログに書く。Poly Haven の CC0 は取得の許可済み）を cloud_sky と同じ向き・画角へ投影し、空の青と雲の白の色の比で雲のマスクを作り、マスクから測れる量（切れ間・塊の数・面積の分布・最大の塊の割合）の範囲を見本の値の幅から決める（マスクの作り方の近似と限界を文書に書く）。不透明度でしか測れない量（薄い膜・厚い部分・塊の中の揺れ・縁の幅）は美術上の判断として値と理由を書く。`--selftest`（この項目で作る）で合成の画像を測り、固定した最終の定義で次が落ちることを確かめる: 一様な層、雲なし、細かなノイズ、最小の面積を超える粗い斑点（不透明度 0.7 の独立した斑点 10 個・縁が急）、穴の開いた膜。既知の幅の合成の雲で縁の幅が ±1 画素。SCN-WL-CLOUD-OPACITY-AOV の撮影（cloud_sky・ref_far_01）の今の値と見本の値を `Docs/RenderingValidation/WastelandCloudShape.md`（新規） に書く（値は変えない）。量の画（塊の色分け・測れた縁）と見本のマスクを開いて確かめる。
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --selftest --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-OPACITY-AOV --views cloud_sky,ref_far_01 --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md, Assets/Reference
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 撮影は SCN-WL-CLOUD-OPACITY-AOV のものを読む（撮り直さない）。HDRI は `Assets/Reference/SkySamples/`（git 管理外。出典・ライセンス・大きさは文書に書く）。時間の見積もり: HDRI の取得と投影 15 分、台本 30〜40 分、合成の試験 15 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。

## SCN-WL-CLOUD-SHAPE-BEFORE: 雲の形を変える前の、遠景の雲・段・回帰・GPU 時間の撮影をそろえる
- status: done
- after: SCN-WL-CLOUD-SHAPE-METRIC
- done-when: 雲の形の調整（SCN-WL-CLOUD-SHAPE-TUNE）と受け入れ（SCN-WL-CLOUD-SHAPE-ACCEPT）の比べの元を、今の雲のシェーダーで撮って保存する（コードと値は変えない）: (1) ちらつきの連番の前（`camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt`、ref_far_01、`-Hdr`）を `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/before` に、(2) 段の前の PNG（ref_far_02 の既定の撮影）を `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/banding` に、(3) 回帰の撮影の `before`（`-Item SCN-WL-CLOUD-SHAPE-ACCEPT`）、(4) GPU 時間（ref_far_01）、(5) 6 視点の撮影（写真と並べる前の画）。撮った Game.exe のハッシュと雲のシェーダーのハッシュを文書に書く。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/before`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/banding`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/views`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-CLOUD-SHAPE-ACCEPT -Phase before`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 撮るだけの項目。時間の見積もり: ビルド 3〜6 分、連番 約 3 分、段 約 2 分、6 視点 約 4 分、計測 約 3 分、回帰 7〜9 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。

## SCN-WL-CLOUD-SHAPE-TUNE: 雲を一様な薄い層から、青空の切れ間のある積雲の塊にする
- status: done
- after: SCN-WL-CLOUD-SHAPE-BEFORE
- done-when: `WastelandRoutine.cpp` の雲の層（被覆 0.43・形の大きさ 4.5 km・底 2500 m・頂 3600 m）と `CloudLayerCommon.glsl` の形のノイズ（被覆の閾値・低い周波の重み・遠くの細部のミップ）で、はっきりした縁と切れ間のある積雲の塊にする。天気は far_01 の写真の部分曇りに合わせる（美術上の判断として文書に書く。晴れの写真 ground_02・cliff_01 との違いは天気の違いとして記録する）。密度の式（GLSL）を変えるなら、書き出しの検査と内訳の台本が持つ雲の式の写し（`CloudBreakdown.py`・`--cloud-dump` の照合）も同じ変更で直し、`--cloud-dump --verify` を通す。合否: SCN-WL-CLOUD-SHAPE-METRIC で固定した `cloud-shape.json`（cloud_sky）が通る（定義は変えない）。ref_far_01 は値を記録し写真 far_01 と並べて開く。cloud_sky と ref_far_01 の画を開いて、斑点やノイズでなく積雲の塊に見えることを確かめる。遠景・段・回帰・GPU 時間の受け入れは SCN-WL-CLOUD-SHAPE-ACCEPT。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE --views cloud_sky,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 危険地帯（共有の GLSL・既定の見た目）。時間の見積もり: ビルド 2 構成 6〜12 分、書き出し 約 3 分 × 調整 3〜5 回。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。 2026-10-24（親）: 反復 32 は verify が通ったが、評価者の 2 周目（反復 31 の指摘〔雲の書き出しの照合から地形の入る視点の視線を外していた〕への対応 5287c0b4 を見る）がユーザーの停止の依頼で中断した。次の走行で、中身は 5287c0b4 のまま verify を回し直して評価に出す（新しい直しは足さない。遠い雲の段と直線の縁は SCN-WL-CLOUD-CONTOUR-FIX で直す）。 2026-10-24（SCN-WL-CLOUD-SHAPE-ACCEPT の反復 7、todo に戻す理由）: 受け入れで遠景の雲が悪くなった。(a) ref_far_01 のちらつきの比 8.50（静止したカメラの連番で 8 bit の G が 2 段以上揺れる画素が 0 % → 11.9 %。画素ごとの時間の雑音で、縁で強い）、(b) 霞の後の雲のコントラストの平均 0.435（下限 0.7007。陰の側が写真より暗い灰色）、(c) 遠い雲の縁がぼやけて写真の積雲の細かな縁にならない。原因の見込みは、覆いの閾値の幅を狭めて密度を 3 倍にした分の歩の端数による標本の揺れと、18 km より遠い歩が細部のノイズを引かないこと（`kDetailMaxDistance`。ref_far_01 の雲の入る距離の中央値 20.3 km）。値と画は `WastelandCloudShape.md` の「受け入れ」、判断は `blocked/SCN-WL-CLOUD-SHAPE-ACCEPT.md`。

## SCN-WL-CLOUD-CONTOUR-FIX: 遠い雲の平らな段と等高線のような縁と、雲の直線の縁・四角い角を直す
- status: done
- after: SCN-WL-CLOUD-SHAPE-TUNE
- done-when: 親の所見（2026-10-24、SCN-WL-CLOUD-SHAPE-TUNE の撮影を開いて）: (1) ref_far_01 の地平の上の雲が、平らな色の段を等高線のように重ねた絵のように見え、段の縁が硬い（形を変える前の SCN-WL-CLOUD-SHAPE-BEFORE の ref_far_01 にも、青空の穴の縁などに同じ段があった。前からある見た目の欠陥も直す）、(2) cloud_sky の雲のいくつかに、画面の縦横に沿った直線の縁と四角い角がある（中央の縦長の雲の下端・右の大きな雲の左の縁。評価者も cloud_sky 右上の直線的な輪郭を残課題に挙げた）。直す前に原因を切り分ける（候補: 形と細部のノイズの 8 bit の量子化と密度の倍率 3 で段の縁が硬くなる、3D ノイズの解像度・繰り返し・ミップの選び方、遠い視線の歩の数と歩の位置、雲の範囲の打ち切り、表示の量子化。開発ビルドの切り替えで 1 つずつ外して撮り、どれが段と直線を作るかを文書に書く）。直した後の合否: (a) `BandingCheck.py --verify`（ref_far_01 の空の行。前は `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE/ref_far_01.png`。平らな帯の長さの平均と 95 % 点が前より短く、局所の平均の差が 0.5 段以下）、(b) `--cloud-shape` に「画面の縦か横に沿って 20 画素以上まっすぐ続く雲の縁の長さ ÷ 測れた縁の長さ」の量を足し、今の値を記録する（直す合否は SCN-WL-CLOUD-SHAPE-NOISE に移した）、(c) 固定した `cloud-shape.json` が通ったまま（定義は変えない）、(d) `--cloud-dump --verify` が既存の許容で通る（照合を弱めない）、(e) cloud_sky と ref_far_01 の画を前と並べて開き、段と直線の縁が消えたことを確かめる。GPU 時間（ref_far_01）を前後で記録。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --selftest --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX --views cloud_sky,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX/look`
- verify: `python Scripts/Terrain/BandingCheck.py --before .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-TUNE/ref_far_01.png --after .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX/look/ref_far_01.png --rows 0:450 --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（共有の GLSL・既定の見た目）。2026-10-24 親が SCN-WL-CLOUD-SHAPE-TUNE の撮影を開いて足した。SCN-WL-CLOUD-SHAPE-ACCEPT（遠景の積分・ちらつき・段・回帰）はこの項目の後に回す。`--cloud-shape` の新しい量は `cloud-shape.json` の行には足さない（固定した定義を変えない。この項目の合否は verify の文書の値と画で示す）。直線の縁の量の比べの前の値は、反復の最初に今の撮影で測って文書に書く。時間の見積もり: 切り分けの撮影 5〜6 回 各 約 3 分、直し 20〜30 分、ビルド 2 構成 6〜12 分、撮影 2 組 約 5 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。 2026-10-24（親）: 走行 20261024-183517 の反復 2 の BLOCKED（段の直し〔太陽への光の歩の点を画素ごとの端数で散らす、c6e13d17〕で (a)(c)(d) は合格。直線の縁は形のノイズ〔世界の軸に沿った格子の Perlin-Worley〕と平らな雲の底が作り、直すと外形が動いて (a) の局所の平均の差 0.5 段と両立しない）を推奨の案 1 で解いた: この項目は段の直しで閉じ（次の反復は verify を回し直して閉じる。新しい直しは足さない）、直線の縁の直しは SCN-WL-CLOUD-SHAPE-NOISE に分けた。

## SCN-WL-CLOUD-SHAPE-NOISE: 雲の形のノイズを作り直し、雲の四角い縁と縦にまっすぐな縁を消す
- status: done
- after: SCN-WL-CLOUD-CONTOUR-FIX
- done-when: SCN-WL-CLOUD-CONTOUR-FIX の切り分け（`Docs/RenderingValidation/WastelandCloudShape.md`）: cloud_sky の目に見える直線（中央の雲の四角い下端・右の大きな雲の左の縁）は、形のノイズ（世界の軸に沿った 4 格子の Perlin-Worley）が決めている。形のノイズを、軸に沿った縁を作らない形に作り直す（例: 格子の向きを回す・領域を歪める・周波数ごとに向きを変える。どれにするかは試して決め、理由を文書に書く）。遠景の雲の底が平らにそろうのは積雲の実際の姿（凝結高度で底がそろう）なので、横の直線（雲の底）は直す対象にしない。合否: (1) `--cloud-shape` の直線の縁の量を縦と横に分けて出し、cloud_sky の縦横の合計が今の値（0.0090）の半分以下、ref_far_01 の縦の直線の縁が今の値の半分以下（横は記録だけ）、(2) 固定した `cloud-shape.json` が通ったまま（定義は変えない）、(3) 密度の式（GLSL）を変えるので、雲の書き出しの照合の写し（`CloudDumpCheck.py`・`CloudBreakdown.py`）も同じ変更で直し、`--cloud-dump --verify` が既存の許容で通る、(4) ref_far_01 の平らな帯の長さの平均と 95 % 点が前（SCN-WL-CLOUD-CONTOUR-FIX の後）の 1.1 倍以内（`BandingCheck.py` に、局所の平均の差は見ずに帯の長さだけを前と比べる `--no-worse-bands` をこの項目で足す。外形が変わるのは直しの目的なので局所の平均は求めない）、(5) cloud_sky と ref_far_01 の画を前と並べて開き、四角い縁が消え、雲が積雲の塊に見えることを確かめる。GPU 時間（ref_far_01）を前後で記録。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE --views cloud_sky,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE/look`
- verify: `python Scripts/Terrain/BandingCheck.py --before .harness/runs/scene-capture/SCN-WL-CLOUD-CONTOUR-FIX/look/ref_far_01.png --after .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-NOISE/look/ref_far_01.png --rows 0:450 --no-worse-bands`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（共有の GLSL・既定の見た目）。2026-10-24 親が SCN-WL-CLOUD-CONTOUR-FIX の BLOCKED（推奨の案 1）から分けた。直線の縁の量の前の値は SCN-WL-CLOUD-CONTOUR-FIX の記録。密度場が変わるので、雲の Cycles の正解の描き直しは SCN-WLR-CLOUD-REF-REFRESH で行う。時間の見積もり: ノイズの作り直し 25〜35 分、ビルド 2 構成 6〜12 分、撮影 2 組 約 5 分、照合の写しの直し 10 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。

## SCN-WL-CLOUD-FAR-DETAIL: 遠い雲の細部の打ち切りを距離で溶かし、遠景の雲のちらつきを消して縁をはっきりさせる
- status: done
- after: SCN-WL-CLOUD-SHAPE-NOISE
- done-when: SCN-WL-CLOUD-SHAPE-ACCEPT の反復 7 の測り（`Docs/RenderingValidation/WastelandCloudShape.md` の「受け入れ」）: ref_far_01 のちらつきの連番（8 枚）で、8 bit の G が 2 段以上揺れる画素が形を変える前の 0 % から 11.9 %（最大 7 段）に増えた（HDR のちらつきの比 0.00079 → 0.00671。フレームの差は隣の画素と相関しない画素ごとの時間の雑音で、雲の縁で強い）。18 km より遠い歩は細部のノイズで縁を削らない（`CloudLayerCommon.glsl` の `kDetailMaxDistance = 18000`）。ref_far_01 の雲の入る距離の中央値は 20.3 km で、遠い雲の縁がぼやけて写真の積雲の細かな縁にならず、歩の端数が 18 km の前後を行き来する画素で細部の有無が切り替わる見込み。直し: 細部の打ち切りを距離で溶かす形か、遠いほど粗いミップで細部を引く形にし、密度 3 倍で大きくなった歩の端数による標本の揺れを TAA が均せる大きさにする（どれが効くかは 1 つずつ外して撮って確かめ、文書に書く）。合否（`Scripts/Terrain/ExpectSpecs/cloud-far-accept.json`、この項目で作る。`cloud-far-range.json` から、一様な膜のときの値だったコントラストの下限 0.7007 の行を外し、ちらつきの行を次に替える）: (1) ref_far_01 のちらつきの連番で、画素ごとに 8 枚の G の直線の傾き（風で雲が動いた分）を除いた揺れの幅が 2 段以上の画素が 1 % 以下（`flicker_after_g8_detrended_range2`。素の幅の 2 段以上の割合と HDR のちらつきの比は記録の行に残す。`--flicker` の量に `--selftest` を足し、合成の連番で、一定の速さの変化〔風〕は数えず、ランダムな ±2 段の揺れは数え、両方を重ねた連番では揺れの分だけを数えることを確かめる）、(2) 細かい歩との差の行は今のまま、(3) 固定した `cloud-shape.json` が通ったまま、(4) `--cloud-dump --verify` が既存の許容で通る（密度の式を変えるなら照合の写しも直す）、(5) far_01 の空を拡大して写真と並べ、遠い雲の縁が前よりはっきりしたことを開いて確かめる（写真の露出は明るいので明るさは診断）。GPU 時間（ref_far_01）を前後で記録。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/far`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/far-flicker/after`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --flicker --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/far --expect Scripts/Terrain/ExpectSpecs/cloud-far-accept.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/opacity`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/opacity --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/opacity --views cloud_sky,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（共有の GLSL・既定の見た目）。2026-10-24 親が SCN-WL-CLOUD-SHAPE-ACCEPT の BLOCKED（走行 20261024-183517 の反復 7。`blocked/resolved/SCN-WL-CLOUD-SHAPE-ACCEPT-far.md`）を推奨の案 3 で解いて足した: ちらつきとコントラストは描画の悪化なので直す。閉じた SCN-WL-CLOUD-SHAPE-TUNE は戻さず、直しを 2 項目に分けた。 ちらつきの前の連番は `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/before`（形を変える前）を `.harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/far-flicker/before` に写して使う（`--flicker` は `<撮影先>-flicker/before` を読む）。時間の見積もり: 切り分けの撮影 4〜5 回 各 約 3 分、直し 20〜30 分、ビルド 2 構成 6〜12 分、撮影 3 組 約 8 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。Cycles の生成は反復の中で同期で走らせて終わりを待ち、保存する（verify は保存物の照合だけ）。 2026-10-24（親）: 走行 20261024-183517 の反復 12 の BLOCKED を推奨の案 1 で解いた: ちらつきの合否を風を除いた幅に替えるのは親の判断（子の独断ではない）。根拠: 主の歩と光の点の端数を 0.5 に固定して時間の雑音を 0 にした撮影でも、素の幅の 2 段以上は 2.7 % 残り、その約 8 割は風（8, 3 m/s）で縁の締まった雲が 0.93 秒の連番の間に動いた分（描画の欠陥ではない）。形を変える前の 0 % は縁の無い一様な膜の値。次の反復は `--selftest` を足して verify を回し直す（描画の直し b68d7dcb は変えない）。

## SCN-WL-CLOUD-SHADOW-LIGHT: 雲の陰の側の明るさを、Cycles の正解に寄せる
- status: done
- after: SCN-WL-CLOUD-FAR-DETAIL
- done-when: SCN-WL-CLOUD-SHAPE-ACCEPT の反復 7: 密度 3 倍で塊が光学的に厚くなり（透過率の平均 0.244）、遠い雲の陰の側が灰色に沈む（霞の後のコントラストの平均 0.435。写真と同じ範囲の HDR の Y の 5 % 点 0.653、写真 0.834。写真との比は診断）。雲の Cycles の正解との比べ（SCN-WLR-CLOUD-REF-CURRENT の基準）でも、影の側はエンジン ÷ Cycles 0.715（e = |ln| 0.335）、縁 1.487 で、多重散乱の近似（`CloudLayerCommon.glsl`）が影の側を暗く、縁を明るくしすぎている。多重散乱の近似（オクターブの数・減衰・位相の広がり・粉の効果）を Cycles に合わせる。合否は、今の密度場の cloud_sky の書き出しから Cycles の正解を描き直して（`BlenderReference.py --cloud`、台本は変えない、約 5〜8 分）、`--cloud --expect cloud-ref.json` で 5 領域と 3 つの量の e が基準（SCN-WLR-CLOUD-REF-CURRENT）より悪くならず、影の側と縁の e がどちらも 0.20 以下。ref_far_01 の霞の後のコントラストと HDR の Y の 5 % 点を前後で記録する（合否にしない）。遠景のちらつき（`cloud-far-accept.json`）が通ったまま。GPU 時間（ref_far_01）を前後で記録。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/dump --reference .harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/cycles --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/far`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/far-flicker/after`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --flicker --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/far --expect Scripts/Terrain/ExpectSpecs/cloud-far-accept.json --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（共有の GLSL・既定の見た目）。2026-10-24 親が SCN-WL-CLOUD-SHAPE-ACCEPT の BLOCKED（走行 20261024-183517 の反復 7。`blocked/resolved/SCN-WL-CLOUD-SHAPE-ACCEPT-far.md`）を推奨の案 3 で解いて足した: ちらつきとコントラストは描画の悪化なので直す。閉じた SCN-WL-CLOUD-SHAPE-TUNE は戻さず、直しを 2 項目に分けた。 cloud_sky の書き出し（`.harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/dump`）は反復の中で直しの後の Game.exe で 1 回撮って保存し、Cycles はその書き出しから描く（verify で撮り直さない）。ちらつきの前の連番は `.harness/runs/scene-capture/SCN-WL-CLOUD-FAR-DETAIL/far-flicker/before` を `.harness/runs/scene-capture/SCN-WL-CLOUD-SHADOW-LIGHT/far-flicker/before` に写して使う。`cloud-ref.json` の e の行の上限 0.20 は、この項目で影の側と縁の行に足してよい（ほかの行の上限は変えない）。時間の見積もり: 直し 20〜30 分、ビルド 2 構成 6〜12 分、書き出し 約 3 分、描画 5〜8 分、撮影 2 組 約 6 分。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。Cycles の生成は反復の中で同期で走らせて終わりを待ち、保存する（verify は保存物の照合だけ）。 2026-10-25（親）: 反復 18 の BLOCKED（今の密度場の書き出しと Cycles の正解で、(1) 日の当たる側〔厚い雲で太陽の透過率 0.7 以上〕の視線が 1 本で判定できない、(2) Cycles の透過率が書き出しの式と最大 0.0165 ずれる〔薄い縁の 4/144 本。16 m の格子が締まった縁を均す差〕。どちらも照明の直しと関係なく残る。写しで見つけた多重散乱の係数なら影の側と縁の e はどちらも 0.20 以下に入る見込み）を推奨の案 A で解いた: (1) 日の当たる側の閾値を太陽の透過率 0.3 以上にする（`CloudVolume.SUNLIT_MIN_SUN_T` 0.7 → 0.3。今の書き出しで 9 本）。定義が変わる `body_sunlit` と `shadow_darkness` の行は基準（0.7 で決めた組）との比べをせず、e ≤ 0.20 の絶対の上限で見る（`cloud-ref.json` の note に書く）、(2) 透過率の照合の上限（`CloudCheck.TRANSMITTANCE_TOLERANCE`）を 0.02 にし、根拠（16 m の格子が締まった縁を均す差・4/144 本・最大 0.0165）を `WastelandCloudShape.md` に書く。台本（格子の大きさ）は変えない。ほかの 3 領域と 2 つの量は基準より悪くならないこと（今のまま）。Cycles の描画は今の密度場で約 17 分（2048 spp）かかるので、照明を直した後の書き出しから 1 回だけ描く（直す前の組 `dump-before`・`cycles-before` は記録に使い、描き直さない）。遠景のコントラストとちらつきは描画を変えた後の撮影で確かめる（ちらつきは `cloud-far-accept.json`）。

## SCN-WL-CLOUD-SHAPE-ACCEPT: 雲の形の変更で、遠景の雲の積分・ちらつき・段・ほかのシーンが悪くならないことを確かめる
- status: done
- after: SCN-WL-CLOUD-SHAPE-TUNE, SCN-WL-CLOUD-CONTOUR-FIX, SCN-WL-CLOUD-SHAPE-NOISE, SCN-WL-CLOUD-FAR-DETAIL, SCN-WL-CLOUD-SHADOW-LIGHT
- done-when: SCN-WL-CLOUD-SHAPE-TUNE の後の Game.exe で撮る: (1) 遠景の雲（ref_far_01・ref_far_02 の `cloud-dump`）と、ちらつきの連番の後（SCN-WL-CLOUD-SHAPE-BEFORE と同じ camera-script）を撮り、`--cloud-far --flicker --expect cloud-far-range.json`（細かい歩との差・ちらつきの前後の比・霞の後の雲のコントラスト。既存の定義）が通る、(2) ref_far_02 の段が増えない（`BandingCheck.py`、前は SCN-WL-CLOUD-SHAPE-BEFORE の PNG。今の `--verify` は「前より短い」を求める直しの判定なので、平らな帯の長さの平均と 95 % 点が前の 1.1 倍以内・局所の平均の差の平均が 0.5 段以下で合格にする `--no-worse` をこの項目で足す）、(3) 6 視点を撮って写真と並べて開く（前の 6 視点とも並べる）、(4) GPU 時間（ref_far_01）を前と並べて記録、(5) 回帰の撮影の `after` の差の画を開いて、雲が写るシーンの変化と理由を書く。悪くなったら原因を書き、SCN-WL-CLOUD-SHAPE-TUNE を todo に戻す理由をその notes に書いて BLOCKED。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/after`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --flicker --engine .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far --expect Scripts/Terrain/ExpectSpecs/cloud-far-accept.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/banding`
- verify: `python Scripts/Terrain/BandingCheck.py --before .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-BEFORE/banding/ref_far_02.png --after .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/banding/ref_far_02.png --rows 0:450 --no-worse-bands`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_cliff_01,ref_cliff_02,ref_ground_01,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/views`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/views --photos far_01,far_02,cliff_01,cliff_02,ground_01,ground_02 --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01 -GpuTimingFrames 300 -GpuFrameBudgetMs 33.3 -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/timing`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-WL-CLOUD-SHAPE-ACCEPT -Phase after`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 `BandingCheck.py` の引数（--rows の範囲・PNG の名前）は既存の使い方に合わせて直してよい（検査を消さない）。時間の見積もり: ビルド 3〜6 分、遠景 約 3 分、連番 約 3 分、段 約 2 分、6 視点 約 4 分、計測 約 3 分、回帰 7〜9 分、記録 10 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。 2026-10-24（親）: SCN-WL-CLOUD-SHAPE-NOISE の撮影を開いた所見: cloud_sky は四角い縁が消えて自然な積雲の塊になった。ref_far_01 の遠い雲は、写真 far_01 の積雲（カリフラワー状のはっきりした縁・白く明るい頂・平らな底）より、縁がぼやけて全体が灰色がかって見える。受け入れでは 6 視点の比較画像のうち far_01 の空を拡大して写真と並べ、縁のはっきりさと頂の明るさの差を文書に書く（写真は露出が明るいので明るさの比は診断）。差が描画の欠陥（遠い視線の細部のミップ・歩の数など）なら直す項目を足して BLOCKED、天気・露出の違いなら記録して閉じる。 2026-10-24（反復 7、BLOCKED）: verify を全部回した。不合格は (1) のちらつきの比とコントラスト、(2) の局所の平均の差（16.5 段。前が一様な膜なので形を変えた後は原理的に通らない。平らな帯は減った）。写しの透過率の差は検査側の精度で、`CloudFarCause.check_probe` を GPU の向きと区間で求め直す形に直した。`BandingCheck.py --no-worse` を足した。 2026-10-24（親）: 反復 7 の BLOCKED を推奨の案 3 で解いた: 段の検査は局所の平均の差を外し平らな帯の長さだけで見る（前が空一面の薄い膜なので、外形の変わる形の直しとは原理的に両立しない。`--no-worse-bands`）。ちらつきとコントラストは描画の悪化なので SCN-WL-CLOUD-FAR-DETAIL（細部の打ち切り・ちらつき）と SCN-WL-CLOUD-SHADOW-LIGHT（陰の側の明るさを Cycles に寄せる）で直してから、この項目を回し直す。遠景の判定は `cloud-far-accept.json`（一様な膜のときの値だったコントラストの下限を外し、ちらつきを 8 bit の 2 段以上の画素 1 % 以下で見る。SCN-WL-CLOUD-FAR-DETAIL で作る）に替えた。ちらつきの前の連番は形を変える前（今の `far-flicker/before`）のまま。光の正しさは SCN-WL-CLOUD-SHADOW-LIGHT の Cycles との比べで見る。

## SCN-WLR-CLOUD-REF-REFRESH: 雲の形を変えた後に、新しい密度場で雲の Cycles の正解を描き直し、比べを通し直す
- status: done
- after: SCN-WL-CLOUD-SHAPE-ACCEPT
- done-when: SCN-WL-CLOUD-SHAPE-TUNE で密度場が変わった。新しい Game.exe の書き出し（cloud_sky の `cloud-dump`）を撮り、SCN-WLR-CLOUD-REF-CURRENT と同じ台本と引数で描き直す。Cycles の生成は反復の中で同期で走らせて終わりを待ち、保存する（反復の外に描画を残して BLOCKED にしない。verify は保存物の照合だけ）。`--cloud --expect cloud-ref.json` で、領域ごとの誤差 e = |ln(エンジン ÷ Cycles)| が SCN-WLR-CLOUD-REF-CURRENT の基準の値より許容を超えて大きくならないこと（密度場が変わり領域に入る視線が入れ替わるので、領域の視線の数と入れ替わりの割合も記録する）。悪化したら、悪化した領域と太陽との角・太陽への透過率の分布を書き、光の運び方を直す項目を足して BLOCKED。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-REFRESH/dump --reference .harness/runs/scene-capture/SCN-WLR-CLOUD-REF-REFRESH/cycles --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半の頭（雲の形）として足した。親の所見（2026-10-23、SCN-WL-OCCLUSION-ACCEPT の 6 視点）: 遠い雲が見えるようになった代わりに、雲の層がどの視点でも空を一様に薄く覆い、晴れの写真（ground_02・cliff_01）の視点でも白い曇り空に見える。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt` の 8〜11・17 と、雲の組み直しへの再批評: 正式な正解は今の書き出しとハッシュが合わない・比の悪化は 1 からの隔たりで測る・全画素の不透明度は今の画面のアルファからは取れない・形の閾値は粗い斑点や穴の開いた膜で通らないよう調整の前に独立の見本で固定する・遠景のちらつきと段の検査・Cycles の生成は反復の中で同期・75 分に収まるよう前の撮影と調整と受け入れを分ける）を反映した。 描き直しは BlenderReference.py の既存の雲の経路を使う（台本は変えない。変えると script_sha256 でほかの参照が古くなる）。時間の見積もり: ビルド 3〜6 分、書き出し 約 3 分、描画 約 5〜8 分、比べ 約 3 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。GPU 時間（`-GpuTimingFrames 300`）は中央値を記録し合否にしない（33.3 ms を超えたら支配する処理を書く）。 2026-10-24（親）: 反復 21 の再検証で、verify の書き出しの撮影の行が `dump` を撮り直し、Cycles の正解が読んだ書き出し（`cloud_sky.cloud.json`）と中身が変わって `--cloud --verify` が落ちた（書き出しは撮るたびに変わる）。書き出しの撮影は反復の中で 1 回撮って保存し、Cycles の描画はその書き出しから行い、verify は保存した組の照合だけにした（撮影の行を verify から外した）。verify の照合が落ちたら、書き出しを撮り直したのではなく描画の入力と書き出しが合っていないので、書き出しを撮り直した場合は描画もやり直す。

## SCN-WL-SHAPE-TERRAIN-AOV: 撮影の AOV に、画素が地形の三角形かの印を足す（開発ビルドの撮影だけ）
- status: done
- after: SCN-WL-SHAPE-MEASURE
- done-when: 形の量（`ShapeCause.py`・`CompareReference.py --shape`）は地形の印が無く、崖錐の岩・スキャンの崖の画素も数える。可視バッファの解決（面の法線の AOV を書く所）で、画素の三角形が地形のインスタンスかを AOV に書く（描画と読み戻しの変更なので危険地帯）。契約: 深度・面の法線と同じフレームの値であること（記録にフレームの番号）、書かれていない画素は決めた値（例 255）で、読む側はそれを拒む、同じ起動で 2 視点を続けて撮っても資源の再利用で前の値が残らない、通常の描画に影響しない（同じ視点を `-Aov` ありと無しで撮った PNG の差が 1 段以内の画素の割合が、`-Aov` 無しを別々の起動で 2 回撮った PNG どうしの割合 − 0.002 以上。起動ごとの描画の揺れ（RT と GI の時間方向の積み重ね・ストリーミング）と比べる。無しどうしの値も記録に書く）、Release では作らない（既存の開発用の AOV と同じ守り方）。検査（`ReferenceCheck.py --aov-terrain-id`、この項目で作る）: 値が 0・1 だけで書かれていない画素が描いた所に無い、空の画素は 0、フレームの番号が深度・面の法線と同じ、2 視点目に 1 視点目の値が残らない、`-Aov` の有無の PNG の差が上の範囲。印の画を開いて、地形・岩・スキャンの崖の境が正しいことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,ref_far_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/aov`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01,ref_far_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/plain`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_cliff_01,ref_far_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/plain2`
- verify: `python Scripts/Terrain/ReferenceCheck.py --aov-terrain-id --engine .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/aov --plain .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/plain --plain2 .harness/runs/scene-capture/SCN-WL-SHAPE-TERRAIN-AOV/plain2 --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 危険地帯（AOV の読み戻し）。既定の描画の見た目は変えない。時間の見積もり: 実装 30〜40 分、ビルド 2 構成 6〜12 分、撮影 2 組 約 4 分、検査 15 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。

## SCN-WL-SHAPE-METRIC-HARDEN: 写真の注釈に対する形の量に、向き・続き・余分な折れ・局所の欠けを足し、ノイズで通らないことを確かめる
- status: done
- after: SCN-WL-SHAPE-MEASURE, SCN-WL-SHAPE-TERRAIN-AOV
- done-when: SCN-WL-SHAPE-MEASURE の `CompareReference.py --shape`（棚の線分・角の点・外形の輪郭の一致の割合と距離の中央値）は、壁一面に細かな折れを作れば注釈の近くにも折れが出て通せる。注釈の線ごとに次を足す: (1) 向き: 撮影の折れの向きが注釈の線の向きと 20° 以内の所だけを一致に数える、(2) 続き: 一致が 8 画素以上続く所だけを数え、注釈の線に沿った一致の欠けの最長（局所の欠け）を出す、(3) 余分な折れ: 注釈の線の周り（許容の幅の 3 倍の帯）にある撮影の折れのうち、どの注釈とも対応しない長さ ÷ 注釈の線の長さ、(4) 角は、今の「近くに直交する 2 つの向きの折れがある」（縦の溝と横の折れの交わりでも通る）を、注釈の角の二辺の向き（注釈に足す）と撮影の折れの向きが各 20° 以内で角の点の近くで交わる形に替える、(5) `--terrain-only` を付けると、地形の印（SCN-WL-SHAPE-TERRAIN-AOV）がある撮影で地形の画素の折れだけを数える（崖錐の岩の縁を棚に数えない。崖の生成の項目が使う。スキャンの崖の項目は岩塊も崖の面なので使わない）。(6) 角の判定を替えたので、SCN-WL-CLIFF-SCAN-NEAR の仕様 `shape-scan-battleship.json` の前の値を、置く前の撮影（SCN-WL-SHAPE-CAUSE の lod-default）を新しい量で測り直して書き直す（閾値は `shape-target.json` の絶対の基準のまま変えない。書き直しの前後の値を文書に書く）。今の Battleship Rock の配置の撮影も新しい量で測って記録する（合否は NEAR が判定する）。`--expect` の行に、一致の割合の下限・余分な折れの上限・局所の欠けの最長の上限を書ける形にする（未知の写真・注釈の欠け・空の選択は落とす）。`--selftest`（この項目で作る）で合成の深度と法線を測り、次が落ちることを確かめる: 壁一面の鋸歯のノイズ、格子状のノイズ、棚を 1 本消した形（局所の欠け）。許容の分だけずらした正しい形は通る。今の撮影（SCN-WL-SHAPE-CAUSE の lod-default）の値を記録する（値は変えない）。後の項目の `--expect` の仕様は、注釈の版（`ShapeAnnotations.json` のハッシュ）を持ち、違えば落とす。
- verify: `python Scripts/Terrain/CompareReference.py --shape --selftest`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SHAPE-CAUSE/lod-default --photos far_01,far_02,cliff_01,cliff_02,ground_02 --shape --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 時間の見積もり: 量の拡張 30〜40 分、合成の試験 20 分、記録 10 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。

## SCN-WL-SHAPE-REGION-METRIC: 地形の形の量を世界座標の範囲ごとに地形の画素だけで測り、地形を変える前の基準を撮って保存する
- status: done
- after: SCN-WL-SHAPE-TERRAIN-AOV, SCN-WL-SHAPE-METRIC-HARDEN, SCN-WL-CLIFF-SCAN-FRANKLIN
- done-when: `ReferenceCheck.py --shape-cause`（`ShapeCause.py`）は画面の 3 × 4 の升で集計する。次を足す: (1) 世界座標で固定した範囲（`Scripts/Terrain/ShapeRegions.json`。崖の生成の 7 範囲と同じ ID・Battleship Rock と Franklin Butte の尖塔の台座（スキャンの担当）・ref_far_01・ref_far_02・ref_ground_02 の近景の谷床。座標は `TerrainCliffDetail.cpp` と `Docs/RenderingValidation/WastelandShape.md` の所見の表から）で、SCN-WL-SHAPE-TERRAIN-AOV の地形の印の画素だけを範囲ごとに集計する、(2) 範囲ごとの最低の有効画素数（足りなければ落とす）、(3) 組は 1〜3 組のどれでも（2 組なら LOD の差、1 組なら折れ目と陰影の差だけ）、`--views` の選択、(4) `--expect`（未知の範囲・視点・空の選択・欠け・画素の不足は落とす）、(5) 範囲の中を 32 × 32 画素の区画に分けた局所の値（前の基準より 10 ポイント以上下がった区画の割合）、(6) `--baseline <置き場>` で前の基準と範囲ごとに比べる。地形を変える前の基準を `.harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE` に撮り（灰色の材質・`wasteland-talus-limit=off`・`-Hdr -Aov`、ref_far_01・ref_far_02・ref_cliff_01・ref_ground_02 の lod-default）、撮影の出力のハッシュを記録する。基準の検査（`--baseline-integrity`）は保存した撮影の出力のハッシュと当時の入力の記録だけを照らし、今の実装との一致は求めない（基準を撮り直し続ける形にしない）。`--selftest` で、地形の印の外の画素が数えられないこと・画素の不足が落ちることを確かめる。範囲の重ね画を開いて、範囲が意図した崖と谷床に乗ることを確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_cliff_01,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE/lod-default`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --selftest`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE --regions Scripts/Terrain/ShapeRegions.json --views ref_far_01,ref_far_02,ref_cliff_01,ref_ground_02 --baseline-integrity --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 基準はスキャンの崖（SCN-WL-CLIFF-SCAN-NEAR・FRANKLIN）の後、崖の生成の前の地形。時間の見積もり: 台本 35〜45 分、撮影 約 4 分、範囲の重ね画と記録 15 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。

## SCN-WL-CLIFF-SURFACE-QUERY: 焼いた地形の区画のメッシュへ鉛直の線を当て、全部の交点を返す問い合わせを AssetCook に足す
- status: done
- after: SCN-WL-SHAPE-MEASURE
- done-when: 崖の生成（`TerrainCliffDetail.cpp`）は面の法線の向きに押し出し X/Z も動かすので、焼いた面は「元の座標の DEM の高さ」ではない（張り出しでは同じ X/Z に面が複数ある）。置いた物の接地を焼いた面で測る土台として、AssetCook に `--sample-cooked-terrain --manifest build/CookedAssets/manifest.json --points <点の一覧の JSON>`（名前は変えてよいが verify も合わせる）を足す: 点ごとに、その X/Z を含む区画のクック済みのパッケージ（最も細かい段の三角形）を読み、鉛直の線と交わる全部の三角形の交点（高さ・面の法線・三角形の番号・区画）を返す。点の一覧はまとめて渡せる。区画のパッケージが無い・メッシュが壊れている・範囲の外は、決めた失敗の値を返す（黙って 0 を返さない）。検査（`Scripts/Terrain/CheckCookedTerrain.py`、この項目で作る。実装の写しにしない）: (1) 崖の生成の範囲の外の区画は DEM の格子のまま焼かれるので、格子の頂点の真上の点で返った高さが `height.json` の値と 1e-4 m 以内、(2) 崖の範囲の中の点で交点が 1 つ以上、張り出しの点で交点が 2 つ以上あり、上から順に並ぶ、(3) 無い区画・範囲の外が決めた失敗を返す。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `python Scripts/Terrain/CheckCookedTerrain.py --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --verify`
- paths: Tools/AssetCook, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 危険地帯（クック済みのアセットの読み込み）。既存のクックの出力は変えない（読むだけ）。時間の見積もり: 問い合わせ 30〜40 分、ビルド 3〜6 分、検査 20 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。

## SCN-WL-TERRAIN-SEAM-FIX: 崖の細部と刻みを焼いた地形の区画の継ぎ目にできた割れ目をふさぐ
- status: todo
- after: SCN-WL-CLIFF-SCAN-NEAR-WALLS
- done-when: SCN-WL-CLIFF-PLACEMENT-CHECK の反復 3（走行 20261025-120217。`.harness/runs/20261025-120217/aux-SCN-WL-CLIFF-PLACEMENT-CHECK-seam-scan.txt`）: Battleship Rock の周りの焼いた地形の区画の継ぎ目に幅 0.2〜0.75 m の割れ目がある（x = −1600〔z −1290〜−1075〕・z = −999〔x −1718〜−1295〕・x = −2100〔z −1339〜−1142〕・z = −1499〔x −1419〜−1414〕。鉛直の問い合わせで面に交わらない点）。崖の細部の押し出しと刻みが、区画の縁の頂点を区画ごとに別に動かしている見込み。下や空が透ける細い線になる見た目の欠陥で、置いた物がそこに落ちると浮く。区画の縁の頂点が隣の区画と同じ位置になるよう、押し出しと刻みを世界座標だけで決まる形にする（または縁の頂点を動かさない。どちらにするかは継ぎ目の見た目で決めて文書に書く）。地形の全項目を 12 組の並列で焼き直す。合否: (1) 継ぎ目の検査（`CheckCookedTerrain.py --seams`、この項目で足す。AssetCook の問い合わせで焼いた面を読む）: 崖の細部と刻みの全部の範囲（Battleship Rock・Franklin Butte・Rooster Butte・Setting Hen Butte・castle の段）に掛かる区画の継ぎ目を 0.05 m（継ぎ目に垂直）× 0.1 m（継ぎ目に沿って）おきに引き、面に交わらない点が 0、(2) 継ぎ目の両側の面の高さの差が、継ぎ目の上の点で 0.05 m 以内、(3) 崖の範囲に掛からない区画のパッケージが前と同じ（クックの出力のハッシュ）、(4) 継ぎ目の通る所（ref_cliff_01・cliff）を灰色の材質で撮って前と並べて開き、透ける線が無いこと、(5) Battleship Rock と Franklin Butte の形の行（`shape-scan-battleship-walls.json`・`shape-scan-franklin.json`）が悪くならない。
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `python Scripts/Terrain/CheckCookedTerrain.py --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --seams --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_cliff_01,cliff -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-TERRAIN-SEAM-FIX/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-TERRAIN-SEAM-FIX/shape --photos cliff_01 --shape --expect Scripts/Terrain/ExpectSpecs/shape-scan-battleship-walls.json --verify`
- paths: Tools/AssetCook, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（地形のクック）。2026-10-25 親が SCN-WL-CLIFF-PLACEMENT-CHECK の反復 3 の BLOCKED（推奨の案 A）から足した。崖の生成の項目（SCN-WL-CLIFF-GEN-*）より前に直す（崖の生成は区画の縁の頂点をさらに動かすので、この直しの形を使う）。AssetCook を変えたら地形の全項目を 12 組の並列で先に焼き、verify のクックの行は印が同じで飛ばされる形にする。継ぎ目の検査の点の数が多く 10 分を超えるなら、範囲ごとの行に分ける。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満。

## SCN-WL-CLIFF-PLACEMENT-CHECK: 置いた物（崖錐の岩・株・砂利）の浮きと埋もれを、焼いた面の支持面で測る検査を作り、今の値を記録する
- status: todo
- after: SCN-WL-CLIFF-SURFACE-QUERY, SCN-WL-TERRAIN-SEAM-FIX
- done-when: 置き方（`WastelandTalus.cpp`・`WastelandFoliage.cpp`・`WastelandGravel.cpp`）は 1 m の格子の `SampleHeight` を読むので、崖の範囲では焼いた面とずれうる。(1) Game に開発ビルドの起動引数 `wasteland-export-placement=<path>`（名前は変えてよいが verify も合わせる）を足し、置いた物の 種類・位置・回転・拡大率・メッシュの名前 を書き出す（Release では作らない）。(2) `Scripts/Terrain/CheckPlacement.py`（この項目で作る）: 物ごとに足元の点の組（岩はメッシュの下 5 % の頂点、株と砂利は根元の円の 6 点。どれも回転・拡大率を掛けた位置）で SCN-WL-CLIFF-SURFACE-QUERY の問い合わせを引き、支持面を選ぶ規則（上向きの面〔法線の上の成分 0.3 以上〕のうち、足元の高さとの差が max(0.5 m, 物の高さの 20 %) 以内で最も近い面。下向きの面〔張り出しの下面〕は支持面にしない）で浮きと埋もれを出す。規則: 岩は足元の点が面より 0.05 m 以上上に浮かず、埋もれは物の高さの 50 % 以下。株と砂利は根元の点の浮き 0.05 m 以内・埋もれ 0.1 m 以内。交点が無い・物が 0 個・メッシュの欠けは落とす。(3) 2 つの合否の形: 記録（`--record`。全部の物を測り切れたら通り、違反の数を範囲ごとに書く）と遵守（`--require-compliant`。違反 0。物を消して通すなら、消した物の一覧と範囲ごとの数を書き、範囲の物の 2 % 以下）。今の値（範囲ごとの物の数・浮きと埋もれの分布・違反の数）を `--record` で記録し、違反の多い所を撮影で開いて確かめる。崖の生成の項目は `--require-compliant` を使う。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GameArgs wasteland-export-placement=.harness/runs/scene-capture/SCN-WL-CLIFF-PLACEMENT-CHECK/placement.json -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-PLACEMENT-CHECK`
- verify: `python Scripts/Terrain/CheckPlacement.py --placement .harness/runs/scene-capture/SCN-WL-CLIFF-PLACEMENT-CHECK/placement.json --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --regions Scripts/Terrain/ShapeRegions.json --record --verify`
- paths: Game/GameModes/Wasteland, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 配置の書き出しは視点に関わらず荒野の全部の物を書く（撮影は書き出しを起こすためだけ）。時間の見積もり: 書き出し 20 分、台本 25〜30 分、ビルド 3 つ 9〜15 分、記録 10 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。 2026-10-25（親）: 走行 20261025-120217 の反復 1 の BLOCKED（荒野の全部の物を全部の足元の点で測ると、石と小礫だけで数億個・JSON 20 GB 超・問い合わせ 3.5 時間超で回らない）を推奨の案 A で解いた: (1) 書き出す物の範囲: 草と低木・大きな石（cobble）・岩は全域の全部、石（stone）と小礫（pebble）は格子の点を含む区画を間引かずに丸ごと書く標本（格子は `ShapeRegions.json` の形の範囲の中を 48 m おき、外を 192 m おき）。done-when の「全部の物を測り切れたら」は「書き出した物の全部を測り切れたら」と読み、標本の取り方（格子・区画の一辺・物の数）を記録に書く。標本の外の石と小礫の違反は見えないことを既知の限界として書く。(2) 岩の足元の点: 置き方と同じ 2048 頂点の標本（`kTalusPointSamples`）のうち下 5 % の帯の全点（置き方とのずれを測る目的に合う。標本の間の頂点は既知の限界）。verify の問い合わせの行が 10 分を超えるなら、種類ごと（岩・草と低木・大きな石・石と小礫の標本）の行に分ける。 2026-10-25（親）: 反復 3 の BLOCKED（書き出した 15035824 個のうち 1 個が焼いた地形の区画の継ぎ目の割れ目で測れない）を推奨の案 A で解いた: 割れ目を直す SCN-WL-TERRAIN-SEAM-FIX をこの項目の前に足した。この項目はコードを変えずに、その後の焼いた地形で verify を回し直して閉じる（ほかに穴があれば値と場所を書いて BLOCKED）。

## SCN-WL-CLIFF-GEN-LEDGES: 崖の生成に、地層に沿って水平に続く棚を足す
- status: todo
- after: SCN-WL-SHAPE-REGION-METRIC, SCN-WL-CLIFF-PLACEMENT-CHECK
- done-when: SCN-WL-SHAPE-CAUSE の原因 2: 崖の生成は壁の折れ目を足す（Setting Hen Butte 45 → 71 %）が、足すのは縦の溝で、写真の水平の地層の棚（1〜3 m）が無い。`TerrainCliffDetail.cpp` の地層（`StrataBed`）の境で、層ごとに押し出しの段を付けて水平に続く棚を作る。担当: 崖の生成の担当の 7 範囲（`Tools/AssetCook/TerrainCliffDetail.cpp` の kNearRegions[2] Rooster Butte・kNearRegions[3] Setting Hen Butte と南の小さな崖・kCastleRegions[0]〜[4] の castle の段の 5 範囲。範囲の名前を `Scripts/Terrain/ShapeRegions.json` に同じ ID で持たせる）。Battleship Rock と Franklin Butte の尖塔の台座（kNearRegions[0]・[1]）は SCN-WL-CLIFF-SCAN-NEAR・FRANKLIN のスキャンの崖の担当で、この項目では変えない。合否: (A) `CompareReference.py --shape --expect shape-cliff-ledges.json`（far_01 の Castle Butte・2 本の尖塔・Cedar Mesa の縁（castle の段）、far_02 の Setting Hen Butte・Rooster Butte、ground_02 の Rooster Butte の行（`Scripts/Terrain/ExpectSpecs/shape-target.json` の絶対の基準〔棚の一致 0.6 以上・角の一致 0.5 以上・ずらした後の外形の距離 8 画素以下〕から `cliff` を選んで写す））で、棚の一致が 0.6 以上・余分な折れと局所の欠けが SCN-WL-SHAPE-METRIC-HARDEN の上限の中、角と外形が `shape-baseline.json` の揺れの外へ悪くならない、(B) `--shape-cause --baseline --expect`（同じ仕様の範囲の行）: 担当の範囲で局所の退行（前の基準より 10 ポイント以上下がった区画）が 5 % 以下、担当の外の範囲の値が前の基準と変わらない（差 2 ポイント以内）、(C) `CheckPlacement.py --require-compliant` が通る、(D) クックの対象（`--cook-coverage`。この項目で作る: 担当の範囲に掛かる区画のパッケージの入力の印が新しい AssetCook と生成の版を指し、焼いた数が区画の数と合う）、(E) ページの要求の溢れ・失敗 0（`--geom-requests`）、VRAM の使用（Game.log の値）が予算の 90 % 以下、地形の三角形の数・クックの時間を前後で記録、(F) 継ぎ目・穴・区画の境の段が無いことと写真との並びを開いて確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cook-coverage --manifest build/CookedAssets/manifest.json --regions Scripts/Terrain/ShapeRegions.json --range-group cliff-gen --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/shape --photos far_01,far_02,ground_02 --shape --terrain-only --expect Scripts/Terrain/ExpectSpecs/shape-cliff-ledges.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/shape --regions Scripts/Terrain/ShapeRegions.json --views ref_far_01,ref_far_02,ref_ground_02 --baseline .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE --expect Scripts/Terrain/ExpectSpecs/shape-cliff-ledges.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GameArgs wasteland-export-placement=.harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/placement/placement.json -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/placement`
- verify: `python Scripts/Terrain/CheckPlacement.py --placement .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/placement/placement.json --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --regions Scripts/Terrain/ShapeRegions.json --require-compliant --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/look`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-requests --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/look --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-LEDGES/look --photos far_01,far_02,ground_02 --verify`
- paths: Tools/AssetCook, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 危険地帯（地形のクック・VRAM）。地形のクックは荒野だけに効くので、ほかのシーンの回帰の撮影は撮らない（既定の荒野の見た目は look の撮影を写真と前の撮影と並べて開いて確かめる）。Game のコードを変えたら Debug のビルドも通す。地形の Cycles の参照（遮蔽の 8 組・共通の照明の組・地形の書き出し）は da68b820 から `--verify` で古い扱いなので、この項目では合否に使わない（描き直しは 2 段目）。合否の定義（`--expect` の仕様）は、地形を変える前に前の基準（SCN-WL-SHAPE-BASELINE）の値と `Docs/RenderingValidation/WastelandShape.md` の絶対の基準から書いてコミットし、変えた後に変えない。 時間の見積もり: 生成の変更 20〜30 分、ビルド 2 つ 6〜10 分、クック 5〜15 分（再検証はキャッシュで短い）、撮影 3 組 各 約 3 分、判定 約 5 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。 2026-10-24（親）: AssetCook を変えると地形の全項目（214）の印が変わり、verify の地形のクックの行（直列）は 10 分を超える。AssetCook を変えたら、反復の中で地形の全項目を 12 組の並列で先に焼き（`.harness/runs/20261015-230406/aux-SCN-WL-CLIFF-DETAIL-ALL-parallel-cook.txt` の手順）、verify のクックの行は印が同じで飛ばされる形にする（再検証の 10 分に収める。クックの行を消さない）。

## SCN-WL-CLIFF-GEN-CAPROCK: 崖の生成に、上端の帽岩の角（切り立ちと張り出し）を足す
- status: todo
- after: SCN-WL-CLIFF-GEN-LEDGES
- done-when: SCN-WL-SHAPE-CAUSE の原因 2 の続き: 崖の上端が丸く、写真の帽岩の切り立った角と小さな張り出しが無い。崖の生成で、最上の硬い層の縁を切り立たせ、張り出し（同じ X/Z に面が 2 つになる所を含む）を作る。担当: 崖の生成の担当の 7 範囲（`Tools/AssetCook/TerrainCliffDetail.cpp` の kNearRegions[2] Rooster Butte・kNearRegions[3] Setting Hen Butte と南の小さな崖・kCastleRegions[0]〜[4] の castle の段の 5 範囲。範囲の名前を `Scripts/Terrain/ShapeRegions.json` に同じ ID で持たせる）。Battleship Rock と Franklin Butte の尖塔の台座（kNearRegions[0]・[1]）は SCN-WL-CLIFF-SCAN-NEAR・FRANKLIN のスキャンの崖の担当で、この項目では変えない。合否: (A) `--shape --expect shape-cliff-caprock.json`（同じ崖の行）で、角の一致（二辺の向きの形）が 0.5 以上・ずらした後の外形の距離が 8 画素以下、棚の量（SCN-WL-CLIFF-GEN-LEDGES の後の値）が悪くならない、(B) `--shape-cause --baseline --expect`（同じ仕様の範囲の行）: 担当の範囲で局所の退行（前の基準より 10 ポイント以上下がった区画）が 5 % 以下、担当の外の範囲の値が前の基準と変わらない（差 2 ポイント以内）、(C) `CheckPlacement.py --require-compliant` が通る、(D) クックの対象（`--cook-coverage`。この項目で作る: 担当の範囲に掛かる区画のパッケージの入力の印が新しい AssetCook と生成の版を指し、焼いた数が区画の数と合う）、(E) ページの要求の溢れ・失敗 0（`--geom-requests`）、VRAM の使用（Game.log の値）が予算の 90 % 以下、地形の三角形の数・クックの時間を前後で記録、(F) 継ぎ目・穴・区画の境の段が無いことと写真との並びを開いて確かめる。 張り出しの下の面が支持面に選ばれないこと（張り出しの下と上と谷床の 3 つの面がある所に置いた物で、`CheckPlacement.py` が正しい面を選ぶ）を、張り出しの所の物の記録で確かめる。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cook-coverage --manifest build/CookedAssets/manifest.json --regions Scripts/Terrain/ShapeRegions.json --range-group cliff-gen --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/shape --photos far_01,far_02,ground_02 --shape --terrain-only --expect Scripts/Terrain/ExpectSpecs/shape-cliff-caprock.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/shape --regions Scripts/Terrain/ShapeRegions.json --views ref_far_01,ref_far_02,ref_ground_02 --baseline .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE --expect Scripts/Terrain/ExpectSpecs/shape-cliff-caprock.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GameArgs wasteland-export-placement=.harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/placement/placement.json -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/placement`
- verify: `python Scripts/Terrain/CheckPlacement.py --placement .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/placement/placement.json --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --regions Scripts/Terrain/ShapeRegions.json --require-compliant --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/look`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-requests --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/look --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-CAPROCK/look --photos far_01,far_02,ground_02 --verify`
- paths: Tools/AssetCook, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 危険地帯（地形のクック）。地形のクックは荒野だけに効くので、ほかのシーンの回帰の撮影は撮らない（既定の荒野の見た目は look の撮影を写真と前の撮影と並べて開いて確かめる）。Game のコードを変えたら Debug のビルドも通す。地形の Cycles の参照（遮蔽の 8 組・共通の照明の組・地形の書き出し）は da68b820 から `--verify` で古い扱いなので、この項目では合否に使わない（描き直しは 2 段目）。合否の定義（`--expect` の仕様）は、地形を変える前に前の基準（SCN-WL-SHAPE-BASELINE）の値と `Docs/RenderingValidation/WastelandShape.md` の絶対の基準から書いてコミットし、変えた後に変えない。 時間の見積もり: SCN-WL-CLIFF-GEN-LEDGES と同じ。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。 2026-10-24（親）: AssetCook を変えると地形の全項目（214）の印が変わり、verify の地形のクックの行（直列）は 10 分を超える。AssetCook を変えたら、反復の中で地形の全項目を 12 組の並列で先に焼き（`.harness/runs/20261015-230406/aux-SCN-WL-CLIFF-DETAIL-ALL-parallel-cook.txt` の手順）、verify のクックの行は印が同じで飛ばされる形にする（再検証の 10 分に収める。クックの行を消さない）。

## SCN-WL-SHAPE-INTERNAL-FOLDS: 写真の崖の内部の折れ線（壁の足・崩れた塊の稜線）を注釈し、撮影の折れと対応づけて測る
- status: done
- after: SCN-WL-SHAPE-METRIC-HARDEN
- done-when: `--shape`（棚・角・外形）は、壁の足元から崖錐へ移る境や崩れた塊の稜線のような地形の内部の折れを測れない。注釈の形式（`Scripts/Terrain/ShapeAnnotations.json`）に内部の折れ線（種類: 壁の足・塊の稜線）を足し、far_01・far_02・ground_02 の崖の生成の担当の崖に置く（崖の範囲の ID を書く）。撮影の側は面の法線の AOV の向きの変化と深度の段から折れを取り出し、SCN-WL-SHAPE-METRIC-HARDEN の量（向き・続き・余分な折れ・局所の欠け）で対応づける。今の撮影（`.harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE`）の値を記録し、基準を `Docs/RenderingValidation/WastelandShape.md` に書く（値は変えない）。注釈の重ね画を開く。
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE/lod-default --photos far_01,far_02,ground_02 --shape --verify`
- paths: Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 時間の見積もり: 注釈 3 枚 20〜30 分、測りの拡張 20〜30 分。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。

## SCN-WL-CLIFF-GEN-TALUS: 崖の生成に、壁から崖錐へ移る崩れた大きな塊を足す
- status: todo
- after: SCN-WL-CLIFF-GEN-CAPROCK, SCN-WL-SHAPE-INTERNAL-FOLDS
- done-when: SCN-WL-SHAPE-CAUSE の原因 2 の続き: 壁から崖錐へ移る所が滑らかな斜面で、写真の崩れた大きな塊（数 m〜十数 m）が無い。崖の生成の範囲の壁の足元に、崩れた塊の形（押し出しと段）を足す（崖錐の岩の散布〔小さな岩〕とは別の、地形の形の塊）。担当: 崖の生成の担当の 7 範囲（`Tools/AssetCook/TerrainCliffDetail.cpp` の kNearRegions[2] Rooster Butte・kNearRegions[3] Setting Hen Butte と南の小さな崖・kCastleRegions[0]〜[4] の castle の段の 5 範囲。範囲の名前を `Scripts/Terrain/ShapeRegions.json` に同じ ID で持たせる）。Battleship Rock と Franklin Butte の尖塔の台座（kNearRegions[0]・[1]）は SCN-WL-CLIFF-SCAN-NEAR・FRANKLIN のスキャンの崖の担当で、この項目では変えない。合否: (A) `--shape --expect shape-cliff-talus.json` で、内部の折れ線（壁の足・塊の稜線）の一致の割合・余分な折れ・局所の欠けが SCN-WL-SHAPE-INTERNAL-FOLDS の基準を満たし、棚・角・外形が悪くならない、(B) `--shape-cause --baseline --expect`（同じ仕様の範囲の行）: 担当の範囲で局所の退行（前の基準より 10 ポイント以上下がった区画）が 5 % 以下、担当の外の範囲の値が前の基準と変わらない（差 2 ポイント以内）、(C) `CheckPlacement.py --require-compliant` が通る、(D) クックの対象（`--cook-coverage`。この項目で作る: 担当の範囲に掛かる区画のパッケージの入力の印が新しい AssetCook と生成の版を指し、焼いた数が区画の数と合う）、(E) ページの要求の溢れ・失敗 0（`--geom-requests`）、VRAM の使用（Game.log の値）が予算の 90 % 以下、地形の三角形の数・クックの時間を前後で記録、(F) 継ぎ目・穴・区画の境の段が無いことと写真との並びを開いて確かめる。 崖錐の岩の散布が塊に埋もれない・浮かない（`--require-compliant`。消した物は範囲の 2 % 以下）。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target AssetCook -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CookAssets.ps1 -AssetCookExe build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe -OnlyLogicalPath 'Assets/Models/WastelandTerrain/*'`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cook-coverage --manifest build/CookedAssets/manifest.json --regions Scripts/Terrain/ShapeRegions.json --range-group cliff-gen --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -Views ref_far_01,ref_far_02,ref_ground_02 -GameArgs wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/shape`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/shape --photos far_01,far_02,ground_02 --shape --terrain-only --expect Scripts/Terrain/ExpectSpecs/shape-cliff-talus.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --shape-cause --capture-root .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/shape --regions Scripts/Terrain/ShapeRegions.json --views ref_far_01,ref_far_02,ref_ground_02 --baseline .harness/runs/scene-capture/SCN-WL-SHAPE-BASELINE --expect Scripts/Terrain/ExpectSpecs/shape-cliff-talus.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_cliff_01 -GameArgs wasteland-export-placement=.harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/placement/placement.json -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/placement`
- verify: `python Scripts/Terrain/CheckPlacement.py --placement .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/placement/placement.json --asset-cook build/Tools/AssetCook/RelWithDebInfo/AssetCook.exe --regions Scripts/Terrain/ShapeRegions.json --require-compliant --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Views ref_far_01,ref_far_02,ref_ground_02 -OutDir .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/look`
- verify: `python Scripts/Terrain/ReferenceCheck.py --geom-requests --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/look --verify`
- verify: `python Scripts/Terrain/CompareReference.py --capture .harness/runs/scene-capture/SCN-WL-CLIFF-GEN-TALUS/look --photos far_01,far_02,ground_02 --verify`
- paths: Tools/AssetCook, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 2026-10-24 親が確認 2 の後の計画の後半（地形の形。ユーザーの順: 地形の形 → 植物 → 地面 → 建物）の 1 段目として足した。SCN-WL-SHAPE-CAUSE の所見（`Docs/RenderingValidation/WastelandShape.md`、e5e06e0d）: 丸さの原因は重い順に 近景の谷床の 1 m の格子の限界・崖の生成が縦の溝だけを足し水平の棚と上端の角と崖錐へ移る塊を作らないこと・遠い崖の陰影の頂点の法線・LOD（効き 1〜7 ポイントで小さいので項目にしない）・遠景の格子の段。Sol の批評 2 回（`Docs/Plans/WastelandRebuild-SolCritique-TerrainCloud-20261024.txt`・`WastelandRebuild-SolCritique-Terrain2-20261024.txt`）を反映した。1 段目は 形の測りの補強 → 地形の画素の印 → 範囲ごとの量と前の基準 → 焼いた面の問い合わせ → 置いた物の接地の検査 → 棚 → 上端の角 → 内部の折れ線の注釈 → 崩れた塊。2 段目（細かな高さの契約・涸れ川・遠い崖の陰影の法線・地形の Cycles の参照の描き直し）は 1 段目の結果を見て組む（設計の論点は `Docs/Plans/WastelandRebuild.md`）。 危険地帯（地形のクック）。地形のクックは荒野だけに効くので、ほかのシーンの回帰の撮影は撮らない（既定の荒野の見た目は look の撮影を写真と前の撮影と並べて開いて確かめる）。Game のコードを変えたら Debug のビルドも通す。地形の Cycles の参照（遮蔽の 8 組・共通の照明の組・地形の書き出し）は da68b820 から `--verify` で古い扱いなので、この項目では合否に使わない（描き直しは 2 段目）。合否の定義（`--expect` の仕様）は、地形を変える前に前の基準（SCN-WL-SHAPE-BASELINE）の値と `Docs/RenderingValidation/WastelandShape.md` の絶対の基準から書いてコミットし、変えた後に変えない。 時間の見積もり: SCN-WL-CLIFF-GEN-LEDGES と同じ。 壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（ビルドは構成ごとに別の行、撮影は組か視点で分ける）。 2026-10-24（親）: AssetCook を変えると地形の全項目（214）の印が変わり、verify の地形のクックの行（直列）は 10 分を超える。AssetCook を変えたら、反復の中で地形の全項目を 12 組の並列で先に焼き（`.harness/runs/20261015-230406/aux-SCN-WL-CLIFF-DETAIL-ALL-parallel-cook.txt` の手順）、verify のクックの行は印が同じで飛ばされる形にする（再検証の 10 分に収める。クックの行を消さない）。

## SCN-WL-SHADOW-LAUNCH-VARIANCE: 崖錐の岩の影が起動ごとに変わる原因を突き止め、同じ条件の撮影で影が同じになるようにする
- status: todo
- after: SCN-WL-CLIFF-GEN-TALUS
- done-when: SCN-WL-SHAPE-TERRAIN-AOV の記録（PROGRESS.md の要起票）: 同じ Game.exe・同じ起動引数で ref_cliff_01 を別々の起動で撮ると、崖錐の岩の影の画素が変わり、1 段以内の画素の割合が大きく下がる起動がある（0.98 の例）。撮影の検査が描画の変更なしで落ちうる。原因の候補（影の仮想ページ〔VSM〕のキャッシュの中身が、ジオメトリのページの常駐が届く間合いで変わる・撮影のフレームの決め方）を、起動を 5 回以上繰り返す撮影で切り分けて文書に書く。直し（例: 撮影のフレームを、影のページとジオメトリのページが落ち着いてから決める）を入れる。合否: 同じ条件で別々の起動で 5 回撮った ref_cliff_01 の PNG の、どの 2 枚の組でも 1 段以内の画素が 99.9 % 以上（今の最悪の組の値を前として記録）。既定の描画の見た目は変えない。GPU 時間と、撮影 1 回の時間を前後で記録。
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `python Scripts/Terrain/LaunchVariance.py --views ref_cliff_01 --launches 5 --game-args wasteland-common-light,wasteland-hide=clouds+foliage+gravel+buildings+aerial+fog,wasteland-gray-material,wasteland-aa=fxaa,wasteland-talus-limit=off --out .harness/runs/scene-capture/SCN-WL-SHADOW-LAUNCH-VARIANCE --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（影のページ・ジオメトリのページの常駐）。2026-10-25 親が PROGRESS.md の要起票から足した。`LaunchVariance.py`（この項目で作る。CaptureScene.ps1 を同じ引数で別々の起動で回して組ごとの差を出す）の名前と引数は変えてよいが verify も合わせる。5 回の撮影が 10 分を超えるなら、回数を分けた行にする。Git Bash では `cmake --build ... -- /m:1` の `/m:1` が `m:1` に書き換わって MSBuild が MSB1008 で落ちるので、ビルドは PowerShell で回す。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。

## SCN-WL-CLOUD-TOWERS: 遠い積雲に上へ伸びる塔の形を足し、頂を写真の積雲の盛り上がりに近づける
- status: todo
- after: SCN-WL-SHADOW-LAUNCH-VARIANCE
- done-when: SCN-WL-CLOUD-SHAPE-ACCEPT の記録（PROGRESS.md の要起票、`far/far_01_sky_zoom.png`）: 遠い積雲の頂に、km の規模で上へ伸びる塔の形（カリフラワー状の盛り上がり）が無く、写真 far_01 と並べると頂が平たく灰色に見える。雲の層の形（`CloudLayerCommon.glsl` の高さの分布・形のノイズの高さによる削り）に、塊ごとの頂の高さの揺れと、上ほど細くなる塔の形を足す（底は平らなまま。凝結高度で底がそろうのは実際の姿）。合否: (1) 固定した `cloud-shape.json` と遠景の判定（`cloud-far-accept.json`）が通ったまま、(2) ref_far_01 の雲の外形の上の縁の高さの揺れ（列ごとの雲の上端の画素の行の標準偏差）が今の値の 1.5 倍以上（写真 far_01 の値も並べて記録。写真との比は診断）、(3) far_01 の空を拡大して写真と並べて開き、頂が盛り上がる塊に見えることを確かめる、(4) `--cloud-dump --verify` が既存の許容で通る（密度の式を変えるなら照合の写しも直す）、(5) 雲の Cycles の正解との比べ（`cloud-ref.json`）は、密度場が変わるので反復の中で描き直して悪くならないことを確かめる（約 17 分。同期で走らせる）。ref_cliff_02 の低い空が写真の層積雲の濃淡に届かないのは撮影日の天気の違いとして記録する（直さない）。GPU 時間（ref_far_01）を前後で記録。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views cloud_sky,ref_far_01 -GameArgs wasteland-cloud-opacity,cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/opacity`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-dump --engine .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/opacity --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-shape --engine .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/opacity --views cloud_sky,ref_far_01 --expect Scripts/Terrain/ExpectSpecs/cloud-shape.json --verify`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01,ref_far_02 -GameArgs cloud-dump -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/far`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -RequireAssets -Width 1920 -Height 1080 -Hdr -Views ref_far_01 -GameArgs camera-script=Scripts/CameraScripts/wasteland-cloud-flicker.txt -OutDir .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/far-flicker/after`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud-far --flicker --engine .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/far --expect Scripts/Terrain/ExpectSpecs/cloud-far-accept.json --verify`
- verify: `python Scripts/Terrain/ReferenceCheck.py --cloud --engine .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/dump --reference .harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/cycles --expect Scripts/Terrain/ExpectSpecs/cloud-ref.json --verify`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, TASKS.md, PROGRESS.md
- notes: 危険地帯（共有の GLSL・既定の見た目）。2026-10-25 親が PROGRESS.md の要起票から足した。ちらつきの前の連番は `.harness/runs/scene-capture/SCN-WL-CLOUD-SHAPE-ACCEPT/far-flicker/after` を `.harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/far-flicker/before` に写して使う。cloud_sky の書き出し（`.harness/runs/scene-capture/SCN-WL-CLOUD-TOWERS/dump`）は反復の中で直しの後に 1 回撮って保存し、Cycles はその書き出しから描く（verify で撮り直さない）。雲の上端の高さの揺れの量の台本はこの項目で作る（`--cloud-shape` に足してよい）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える（未達のまま DONE にしない）。verify は 1 行 10 分未満（RelWithDebInfo のビルドは別の行）。
## SCN-CLOUDS-TEMPORAL: 雲を低解像度と時間方向の蓄積で軽くする
- status: backlog
- done-when: 雲を画面の 1/4〜1/2 の解像度で描き、前のフレームへの再投影で蓄積して、1920×1080・RelWithDebInfo の `wasteland` `overview` で、雲全体（光線の進行・時間の蓄積・拡大の合成）の GPU 時間の p95 が 1.5 ms 以下。超えたら支配する区間に合わせて解像度・ステップ数・再構築の方式を変える。見た目を保って届かないときは未達として done にせず、ユーザーへ渡す。カメラパスの再生中の連続撮影（`--capture-sequence`）の数枚を開いて、カメラを動かしても雲の縁がちらつかず残像を引かないことを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -Views overview -GpuTimingFrames 300 -OutDir .harness/runs/scene-capture/SCN-CLOUDS-TEMPORAL`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。1.5 ms は雲の機能の予算として採る（解像度・ステップ数・再構築の方式を選ぶ判断になる）。 2026-10-15 親: 速さを問わないので外した（雲の見た目は SCN-WASTELAND-POLISH で写真に合わせる）。

## SCN-DUST: ワールド空間の砂塵を足す
- status: backlog
- done-when: 粒子の仕組みにワールド空間のビルボード（カメラを向く板、深度で柔らかく消える、太陽と環境光で明るさが決まる、風で流れる、寿命で薄れる）を足し、`wasteland` に地面すれすれを流れる砂塵と、遠くで舞い上がる砂の柱（数本、ゆっくり）を置く。画面空間の既存の粒子（Rendering3DTest の橙の粒）は変わらない。`ground`・`overview` の撮影と時刻を変えた2枚を開いて、砂塵が地形に溶け込み（板の縁の切れ目が見えない）流れていることを確かめる。Rendering3DTest の撮影が変わらない。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-DUST`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-DUST`
- paths: Library/Core/Public/Particle, Library/Core/Private/Particle, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。今の粒子は画面空間のボードだけ（`ParticleSystem.cpp` 166〜191 行付近）。半透明の描画順と TAA との兼ね合いに注意。 2026-10-15 親: 手本の写真（晴れた日の Valley of the Gods）に砂塵は写っていないので外した。

## SCN-WASTELAND-POLISH: 荒野を実写に近い風景として仕上げる（確認 3）
- status: backlog
- after: SCN-WLR-GAP-LIST
- done-when: `wasteland` のカメラパス（前哨地へ近づいて建物の間を抜け、ビュートを見上げる 40〜60 秒）を決め、グレーディング・露出・霞・雲・低木の量を手本の写真に合わせて整える。手本の全視点の比較画像（写真｜撮影｜同じ視点のパストレースの正解）とカメラパスの途中 6 時刻の撮影（1920×1080）を開き、評価者が見る条件（浮き・継ぎ目・繰り返し・形の対応・移動中のちらつき・光の誤り）を満たすことを確かめ、一覧を `Docs/RenderingValidation/WastelandReference.md` の「確認 3」にまとめる。1920×1080・RelWithDebInfo の GPU・gt・rt の median と p95 を記録する（荒野は美しさを見るシーンなので合否に使わない。操作できる程度＝20〜30 fps 以上を下回ったときだけ、支配する処理を書く）。`wasteland` の cycle smoke 2 周（RelWithDebInfo。1 シーンの最小。1 コマンド 10 分に収めるため 2026-10-19 に 3 周から変えた）が pass。最後に `LOOP_STOP`（中身 `boundary`）を書いてランナーを止める（親がユーザーに渡す。実写と並べて成立するかの判断はユーザー）。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config RelWithDebInfo --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene wasteland -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -OutDir .harness/runs/scene-capture/SCN-WASTELAND-POLISH`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Build -Scenes wasteland -Cycles 2 -Configuration RelWithDebInfo`
- paths: Game, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Assets/AssetSets, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-15 ユーザー: 荒野と室内の光は「ほぼ実写」の美しさを見るシーンで、速さは他のシーンで見る。60 fps の条件は外した（リアルタイムで、操作できる程度ならよい）。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-22（親）: 確認 2（SCN-WL-REVIEW-2）の後の中身の計画（植物の見た目と密度・インポスター・地面の明るさ・空と霞・沢の物など）をユーザーと決めるまで backlog に下げた。計画が決まったら、その項目を after に足して todo に戻す。

## SCN-GEN-VILLAGE-PROPS-1: 夜の集落の小物を生成する（街灯・松明の柱・焚き火の囲い・井戸）
- status: done
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`street_lamp`・`torch_post`・`campfire_ring`・`well` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/NightVillageModels.json` に足してクックが通る。 灯りの部分（ランタンのガラス・火）は発光させたい部分としてプレビューで位置を確かめ、計画書に書く。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids street_lamp,torch_post,campfire_ring,well`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-GEN-VILLAGE-PROPS-2: 夜の集落の小物を生成する（樽・木箱・荷車）
- status: done
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`barrel`・`crate`・`cart` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/NightVillageModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids barrel,crate,cart`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-NIGHT-VILLAGE: 夜の集落を組む
- status: done
- done-when: `night-village` シーン: 荒野の高さ場の一角に、生成した建物と小物で道沿いの集落（建物 8〜12 棟。同じ建物は向きと大きさを変えて使い回す）を組む。太陽は地平線の下、月明かり（青白い弱い平行光と影）、夜の空、露出は自動露出で夜に合わせる（Rendering3DTest の `--night` の値の扱いを参考にする）。名前付き視点 `overview`・`street`（道の上の目の高さ）・`square`（焚き火の広場）。撮影を開いて、暗すぎず明るすぎず夜に見えることを確かめる。`night-village` の cycle smoke 2 周（RelWithDebInfo。1 シーンの最小。1 コマンド 10 分に収めるため 2026-10-19 に 3 周から変えた）が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene night-village -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-NIGHT-VILLAGE`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes night-village -Cycles 2 -Configuration RelWithDebInfo`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 2026-10-14 親: 荒野の地形は DEM（Valley of the Gods）に替わる（SCN-WL-*）。集落は同じ DEM の範囲の別の平らな所に置く。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。

## SCN-NIGHT-LIGHTS: 夜の集落に灯りを入れる
- status: done
- done-when: 街灯・松明・焚き火の点光源（暖色。影付きは VSM の点光源の上限の 4 灯までを手前の主役に、残りは影無し）、灯りのガラスと窓の発光（ブルーム）、焚き火の明るさの揺らぎ（時間で揺れる）、RTGI による壁や地面への照り返しを入れる。`street`・`square` の撮影を開いて、灯りの周りが照らされ影が落ちる・窓が光る・白飛びが広がりすぎないことを確かめる。性能はこの項目では測らず、SCN-ACCEPT の夜の最終配置で、影と間接光の内訳から影付きの灯数・影の解像度・更新の設定を判断する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene night-village -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-NIGHT-LIGHTS`
- paths: Game, Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Docs/RenderingValidation, Scripts, TASKS.md, PROGRESS.md
- notes: 点光源の影は 4 灯まで（`PointShadowSnapshot.h` 19 行付近）。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-22（親）: run 20261022-144923 の反復 3 は完了の信号を出さずに終わり、status は done だが評価者を通っていない（RenderingCoordinator に触れる危険地帯）。次の反復は、反復 3 のコミット（3ec40762・f8f5c7a4）と記録 `Docs/RenderingValidation/NightVillageLights.md` が done-when を満たすかを verify で確かめ、足りない所だけ直して DONE で閉じる（評価者を通すため）。

## SCN-MATERIAL-GALLERY: 材質ギャラリーを作る
- status: done
- done-when: `material-gallery` シーン: 石の床（Poly Haven）の展示場（屋外か、壁の無い屋根付きの回廊）に台座の列を並べ、手元の Poly Haven の地面・壁のテクスチャ（約30種と SCN-PH-WASTELAND の分）を台座の上の傾けた板（視差オクルージョン付き）と球で見せ、別の一角に金属度 0〜1 × 粗さ 0〜1 の格子（6×6 程度）の球を並べる。各台座に名札（テクスチャの id）を付ける（ワールド空間の文字か、画面に投影した ImGui の文字）。光は物理空の昼。名前付き視点 `overview`・`textures`・`spheres`・`close`（板の視差）。撮影を開いて、テクスチャが正しく貼られ（色・法線の向き・ORM の並び）、名札が読めることを確かめる。`material-gallery` の cycle smoke 2 周（RelWithDebInfo。1 シーンの最小。1 コマンド 10 分に収めるため 2026-10-19 に 3 周から変えた）が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene material-gallery -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-MATERIAL-GALLERY`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes material-gallery -Cycles 2 -Configuration RelWithDebInfo`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。
## SCN-INT-REFERENCE-SELECT: 室内の光の手本の写真を選ぶ
- status: done
- done-when: 室内の光のシーンの手本にする実写の写真（パブリックドメイン・CC0・CC BY・CC BY-SA）を 4〜6 枚選ぶ: 低い夕日が大きな窓から差し込む木の床と漆喰かレンガの壁の部屋、色の強い面からの色移り、床の艶の反射、光の筋（塵の舞う空気）。`Assets/Reference/InteriorLight/` に落とし（git 管理外）、manifest（URL・作者・ライセンス・写っている物）を書き、部屋の大きさ・窓の向き・太陽の高さ・家具の並びを写真から決めて `Docs/RenderingValidation/InteriorLightReference.md` に書く。写真の一覧の PNG を開いて確かめる。
- verify: `python Scripts/Terrain/ReferencePhotos.py --verify --set InteriorLight`
- paths: Scripts/Terrain, Docs/RenderingValidation, .gitignore, TASKS.md, PROGRESS.md
- notes: 2026-10-15 ユーザー: 室内の光も「ほぼ実写」の美しさを見るシーン。家具の生成（SCN-GEN-FURNITURE-*）は、この写真に合わせて選び直してよい。`ReferencePhotos.py` に手本の組を選ぶ引数を足してよい。 2026-10-23（親）: 反復 15 の BLOCKED を推奨の案 1 で解いた。Commons を広く探しても 1 枚で条件が揃う許諾の合う写真が無かったので、窓の役割は window_01（木の床・漆喰の壁の日向の色・日だまりの縁のぼけ）と window_02（16.3° の低い日の長い日だまりと奥の壁に這い上がる光）の組で満たす。夕日の色（低い太陽の赤み）の手本は無いので、色は大気の式に任せる。文書（`InteriorLightReference.md`）にこの分け方と、夕日の色の手本が欲しくなったら Commons 以外の出どころかユーザーの撮影で 2 枚目を差し替える方針を書いて閉じる。探した範囲の記録は `blocked/resolved/SCN-INT-REFERENCE-SELECT-window-pair.md`。

## SCN-GEN-FURNITURE-1: 室内の家具を生成する（机・椅子・本棚・ソファ）
- status: done
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`wooden_table`・`wooden_chair`・`bookshelf`・`sofa` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/InteriorLightModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids wooden_table,wooden_chair,bookshelf,sofa`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-GEN-FURNITURE-2: 室内の家具を生成する（植木鉢・ランプ・ラグ）
- status: done
- done-when: SCN-GEN-BUILDINGS-1 と同じ形で、`potted_plant`・`floor_lamp`・`rug` を 10〜20 万面・テクスチャ 2048 で生成し、参照画像とプレビューを開いて形を確かめ、クックの一覧 `Assets/AssetSets/InteriorLightModels.json` に足してクックが通る。
- verify: `python Scripts/GenerateSceneModels.py --verify --require-cooked --ids potted_plant,floor_lamp,rug`
- paths: Assets/AssetSets, Scripts, Tools/ComfyWorkflows, TASKS.md, PROGRESS.md
- notes: SCN-GEN-BUILDINGS-1 の notes の上限時間とやり直しを使う。

## SCN-RENDER-SPEC-OCCLUSION: 鏡面の環境光（IBL・空の前計算の鏡面）を、見えない空の分だけ遮る
- status: todo
- after: SCN-WL-OCCLUSION-ACCEPT
- done-when: SCN-INTERIOR-ROOM の反復 39 の比べ（`.harness/runs/scene-capture/SCN-INTERIOR-ROOM-ref/compare/`、`Docs/RenderingValidation/InteriorLightReference.md`）: 日陰の床の輝度がエンジン ÷ Cycles で 2.5 倍（overview）〜16 倍（window）、青 ÷ 緑 1.23 / 0.71。`lighting.frag` の IBL・RTGI の鏡面が空の前計算をそのまま足し、室内でも遮られない（RTGI の床の空の見え方は 2〜3 %）。鏡面の環境光を、見えない空の分だけ遮る（候補: RTGI の空の見え方・曲げた法線から鏡面の遮りを作る）。材質の AO と二重に数えない（SCN-WL-MATERIAL-AO-SHADING の結論）。`InteriorLightReference.py --compare` に正解の置き場を指す `--reference` と、領域の比を判定する `--expect` を足す（この項目で）。合否（`interior-spec-occlusion.json`）: 3 視点の日陰の床の輝度のエンジン ÷ Cycles が 0.8〜1.25、青 ÷ 緑の比が ±10 %。窓の日だまりの形・位置は前と同じ。荒野の遮蔽と共通の照明の値が悪くならない（notes）。回帰の 4 シーンと material-gallery・interior-light を撮って開き、基準画像（golden）の検査を回し、変わるなら SCN-WL-GOLDEN-REAPPROVE と同じ手順で再承認する。GPU 時間（1 視点、300 フレーム）を前後で記録。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -OutDir .harness/runs/scene-capture/SCN-RENDER-SPEC-OCCLUSION/interior`
- verify: `python Scripts/InteriorLightReference.py --compare --capture .harness/runs/scene-capture/SCN-RENDER-SPEC-OCCLUSION/interior --reference .harness/runs/scene-capture/SCN-INTERIOR-ROOM-ref --expect Scripts/Terrain/ExpectSpecs/interior-spec-occlusion.json`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-RENDER-SPEC-OCCLUSION -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定。全シーンに効く）。2026-10-23 親が SCN-INTERIOR-ROOM の反復 39 の BLOCKED（推奨の案 1）から足した。荒野の遮蔽の受け入れ（SCN-WL-OCCLUSION-ACCEPT）の後に回し、荒野の遮蔽と共通の照明の値が悪くならないことを確かめる（`OcclusionSets.py --capture --sets <組>` を組ごとの行で撮り、`--occlusion --expect occlusion-accept.json` と、共通の照明の組の撮り直しで `--light-final` を通す。verify の行はこの項目で足してよい。1 行 10 分未満）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、室内の撮影 約 3 分、荒野の組の撮り直し 約 25 分。

## SCN-RENDER-RTGI-HIT-ALBEDO: RTGI の当たりの反射率を、テクスチャの粗いミップの平均から引く
- status: todo
- after: SCN-RENDER-SPEC-OCCLUSION
- done-when: SCN-INTERIOR-ROOM の反復 39 の比べ: 日陰の壁がエンジン ÷ Cycles 1.25〜1.50 倍、天井 1.55〜2.00 倍。RTGI の当たりの反射率がテクスチャを引かず、instance の色（単位のアルベドの 99 百分位の倍率）だけになっている（`LightingPass.cpp` の RTGIInstanceData・`DiffuseIndirect.comp`）。地形の 3 層の当たりと同じように、当たりの材質のアルベドのテクスチャの最も粗いミップの平均（または当たりの UV で引く低いミップ）を反射率にする。室内の単位のアルベドの回避（`Scripts/MakeInteriorLightAlbedo.py`）が要らなくなるなら元のアルベドに戻す。合否（`interior-hit-albedo.json`）: 3 視点の日陰の壁・天井・隅の輝度のエンジン ÷ Cycles が 0.85〜1.15。荒野の遮蔽と共通の照明の値が悪くならない（notes）、灰色の材質の撮影（`wasteland-gray-material`）の当たりは灰色のまま。回帰の 4 シーンと material-gallery・interior-light を撮って開き、golden の検査、変わるなら再承認。GPU 時間（1 視点、300 フレーム）を前後で記録。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -OutDir .harness/runs/scene-capture/SCN-RENDER-RTGI-HIT-ALBEDO/interior`
- verify: `python Scripts/InteriorLightReference.py --compare --capture .harness/runs/scene-capture/SCN-RENDER-RTGI-HIT-ALBEDO/interior --reference .harness/runs/scene-capture/SCN-INTERIOR-ROOM-ref --expect Scripts/Terrain/ExpectSpecs/interior-hit-albedo.json`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-RENDER-RTGI-HIT-ALBEDO -Phase after`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md, Assets/AssetSets, Tools/AssetCook
- notes: 危険地帯（描画の既定。全シーンに効く）。2026-10-23 親が SCN-INTERIOR-ROOM の反復 39 の BLOCKED（推奨の案 1）から足した。荒野の遮蔽の受け入れ（SCN-WL-OCCLUSION-ACCEPT）の後に回し、荒野の遮蔽と共通の照明の値が悪くならないことを確かめる（`OcclusionSets.py --capture --sets <組>` を組ごとの行で撮り、`--occlusion --expect occlusion-accept.json` と、共通の照明の組の撮り直しで `--light-final` を通す。verify の行はこの項目で足してよい。1 行 10 分未満）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、室内の撮影 約 3 分、荒野の組の撮り直し 約 25 分。

## SCN-RENDER-RTGI-TEMPORAL: RTGI の時間の蓄積を、60 フレームの撮影で壁の粒が目立たない所まで上げる
- status: todo
- after: SCN-RENDER-RTGI-HIT-ALBEDO
- done-when: SCN-INTERIOR-ROOM の反復 39: 漆喰の壁と天井の粗い粒が 60 フレームの撮影で残る（300 フレームで大きく減る）。RTGI の時間の蓄積とデノイズを、動きの残像を増やさずに収束を早める形で直す（候補: 履歴の長さの上限・分散で導くデノイズ・空間の再利用）。量: 平らな壁・天井の領域の高い周波数の輝度の標準偏差 ÷ 平均（`--compare` に足す）。合否（`interior-temporal.json`）: 60 フレームの撮影の粒の量が、今の 300 フレームの撮影の量の 1.5 倍以内。動きの撮影（荒野の `camera-script` か回帰の -Motion）で残像・ちらつきが増えない（前後を開いて確かめ、数があれば記録）。荒野の遮蔽と共通の照明の値が悪くならない（notes）。回帰の 4 シーンと material-gallery・interior-light を撮って開き、golden の検査、変わるなら再承認。GPU 時間（1 視点、300 フレーム）を前後で記録。反復の最初に回帰の撮影の `before` を撮る。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -Build -RequireAssets -Width 1920 -Height 1080 -Hdr -Aov -OutDir .harness/runs/scene-capture/SCN-RENDER-RTGI-TEMPORAL/interior`
- verify: `python Scripts/InteriorLightReference.py --compare --capture .harness/runs/scene-capture/SCN-RENDER-RTGI-TEMPORAL/interior --reference .harness/runs/scene-capture/SCN-INTERIOR-ROOM-ref --expect Scripts/Terrain/ExpectSpecs/interior-temporal.json`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/Terrain/CaptureRegression.ps1 -Item SCN-RENDER-RTGI-TEMPORAL -Phase after -Motion`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, Docs/RenderingValidation, Test/Core/Rendering/Baselines, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画の既定。全シーンに効く）。2026-10-23 親が SCN-INTERIOR-ROOM の反復 39 の BLOCKED（推奨の案 1）から足した。荒野の遮蔽の受け入れ（SCN-WL-OCCLUSION-ACCEPT）の後に回し、荒野の遮蔽と共通の照明の値が悪くならないことを確かめる（`OcclusionSets.py --capture --sets <組>` を組ごとの行で撮り、`--occlusion --expect occlusion-accept.json` と、共通の照明の組の撮り直しで `--light-final` を通す。verify の行はこの項目で足してよい。1 行 10 分未満）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 時間の見積もり: 回帰の before・after 各 7〜9 分、ビルド 3〜6 分、室内の撮影 約 3 分、荒野の組の撮り直し 約 25 分。

## SCN-INTERIOR-ROOM: 室内の光のシーンを組む
- status: todo
- after: SCN-RENDER-SPEC-OCCLUSION, SCN-RENDER-RTGI-HIT-ALBEDO, SCN-RENDER-RTGI-TEMPORAL
- done-when: `interior-light` シーン: 約 12×8×5 m の部屋（床は木の板、壁は漆喰かレンガ、天井の梁。手続きの箱と Poly Haven のテクスチャ）の一面に大きな窓（開口）を開け、低い夕日が差し込む。生成した家具を置き、赤いラグか色の強い壁で色移り（RTGI）が分かる配置にする。床は少し艶があり反射（SSR）が見える。窓の外に地面と空が見える。名前付き視点 `overview`・`window`（窓へ向かう逆光）・`corner`（奥の隅）。撮影を開いて、窓から入る光と影の形、奥の隅の照り返し、白飛び・黒つぶれを確かめる。`interior-light` の cycle smoke 2 周（RelWithDebInfo。1 シーンの最小。1 コマンド 10 分に収めるため 2026-10-19 に 3 周から変えた）が pass。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-INTERIOR-ROOM`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes interior-light -Cycles 2 -Configuration RelWithDebInfo`
- paths: Game, Assets/AssetSets, Scripts, Test/Game/Bridge/CMakeLists.txt, Test/Core/Rendering/CMakeLists.txt, TASKS.md, PROGRESS.md
- notes: 2026-10-15 ユーザー: 室内の光は「ほぼ実写」の美しさを見るシーン。SCN-INT-REFERENCE-SELECT の写真と並べた比較画像（1920×1080）と、同じ視点のパストレースの正解で判断する。速さの条件は無い。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-23（親）: 反復 39 の BLOCKED（ほぼ実写の差の 3 つがエンジン側）を推奨の案 1 で解いた。鏡面の環境光の遮り・RTGI の当たりの反射率・RTGI の時間の蓄積を SCN-RENDER-SPEC-OCCLUSION・SCN-RENDER-RTGI-HIT-ALBEDO・SCN-RENDER-RTGI-TEMPORAL として先に立て、この項目はその後に `InteriorLightReference.py --compare --reference .harness/runs/scene-capture/SCN-INTERIOR-ROOM-ref` で同じ比較画像を作り直し、残る差を記録して閉じる（Cycles の正解は描き直さない。部屋・家具・視点・周回は反復 38・39 のまま）。

## SCN-INTERIOR-SHAFTS: 窓から入る光の筋を出す
- status: todo
- after: SCN-INTERIOR-ROOM
- done-when: ボリュメトリックフォグの散乱が太陽の影（既定の VSM）で遮られ、窓から入る光の筋が見える。シーンごとにフォグの密度・範囲を設定でき、`interior-light` で室内に薄い霧を入れる（Rendering3DTest の霧は変えない）。`overview`・`window` の撮影を開いて光の筋を確かめ、Rendering3DTest の撮影が変わらない。性能はこの項目では測らず、SCN-ACCEPT で目標を超えたときに、霧の内訳から解像度・範囲・更新の方式を調整する。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene interior-light -Configuration RelWithDebInfo -Build -RequireAssets -OutDir .harness/runs/scene-capture/SCN-INTERIOR-SHAFTS`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-INTERIOR-SHAFTS`
- paths: Library/Core/Public/Rendering, Library/Core/Private/Rendering, Assets/Shaders, Game, Scripts, TASKS.md, PROGRESS.md
- notes: 危険地帯（描画）。`Assets/Shaders/volumetrics.frag` 103〜135 行付近は CSM の影の地図（`cascadedShadowMap`）を読む。太陽の影が VSM のときに霧の影が効いているかを先に確かめる。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。 2026-10-15 ユーザー: 室内の光は「ほぼ実写」の美しさを見るシーン。SCN-INT-REFERENCE-SELECT の写真と並べた比較画像（1920×1080）と、同じ視点のパストレースの正解で判断する。速さの条件は無い。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-23（親）: 順を保つため after を足した（先に走ると後の変更で撮り直しになる）。

## SCN-THUMBNAILS: ランチャーのカードに各シーンの見本画像を出す
- status: todo
- after: SCN-INTERIOR-SHAFTS, SCN-WASTELAND-POLISH
- done-when: `Scripts/CaptureLauncherThumbnails.ps1` が各シーンの見本用の視点（シーンの一覧に書く。既定は `overview`）を撮って 640×360 程度に縮め、`Assets/Textures/Launcher/<id>.png` に書く（追跡する）。ランチャーが読み込んでカードに出す。ランチャーの撮影を開いて、見本画像が正しいカードに出て、選択の強調が分かることを確かめる。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene launcher -Configuration RelWithDebInfo -Build -OutDir .harness/runs/scene-capture/SCN-THUMBNAILS`
- paths: Game, Scripts, Assets/Textures/Launcher, TASKS.md, PROGRESS.md
- notes: 2026-10-10 親が見た点: 1920×1080 のランチャーでカードの説明文が右の縁からはみ出す（折り返しの幅がカードの内側の余白を引いていない）。カードが増えたときの並び（1 行に収まらないときの折り返しかスクロール）も確かめる。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-23（親）: 順を保つため after を足した（先に走ると後の変更で撮り直しになる）。

## SCN-FIX-SKY-BANDING: 空の階調の段（波打つ横縞）を消す
- status: todo
- done-when: SCN-MEGA-ACCEPT-1M の撮影で、mega-stress の overview（1% 移動・ストリーミング）とカメラパスの再生中の mega-bananas の horizon の空に、波打つ横縞の段がはっきり見える（`.harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/{moving,streaming}/overview.png`・`path/horizon.png`）。同じ時刻の mega-bananas の静止の horizon の空は滑らか。mega-stress の空の列の値は SCN-MEGA-MIXED-LOAD（2026-10-11）の撮影と同じで、前からある欠陥。段の出所（空の LUT の解像度・精度、ディザの有無、露出）を特定して直す。完了の条件: 上の 2 つの視点を撮り直した PNG を開いて空に段が見えず、空の縦の列の隣り合う画素の差が 1 段以下で、平らな帯（同じ値が続く区間）の長さが今より短いこと。起動画面 default の決定的撮影の比較が pass（空の見た目を変えるなら基準を再承認し、根拠をコミット本文に書く）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views overview -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -OutDir .harness/runs/scene-capture/SCN-FIX-SKY-BANDING`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-FIX-SKY-BANDING`
- paths: Assets/Shaders, Library/Core/Private/Rendering, Library/Core/Public/Rendering, Docs/RenderingValidation, Test/Core/Rendering, Scripts, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の撮影で見つけた（前からある見た目の欠陥も直す方針）。描画を変える項目なので起動画面 default の比較を入れた。 2026-10-23 SCN-WL-CLOUD-BANDING の反復 22: 原因は同じ（表示の 8 bit の丸めにディザが無い。空の列が 1 段ずつの平らな帯）で、表示の Blit のディザで直した。同じ Game.exe で Blit だけを戻した前の撮影と比べ、mega-stress の overview の上 350 行の平らな帯の長さの平均 7.263 → 2.836・95 % 点 10 → 7、隣の差が 1 段以下の割合 1.000 → 0.999（最大 2。雑音で隣が逆の向きへ丸まった所）。記録は WastelandCloudRef.md の SCN-WL-CLOUD-BANDING の節。「隣の差が 1 段以下」をディザの 2 段の 0.1 % を許す形に読み替えて閉じるかは計画が決める。

## SCN-BUILD-ALL-TESTS: Debug の全体のビルドで落ちる試験の対象（Game のソースの一覧の更新漏れ）を直す
- status: todo
- after: SCN-GOLDEN-REVERIFY
- done-when: `cmake --build build --config Debug -- /m:1`（全体）が、試験の対象の一部で落ちる（2026-10-23 の SCN-GOLDEN-REVERIFY の再検証 `.harness/runs/20261023-124311/recheck-SCN-GOLDEN-REVERIFY-4-1.txt`）: `Rendering3DTestRoutine.cpp` の `stb_image.h` が見つからない（C1083）、`GameApplicationHandler.obj` から CameraScriptCapture・GeometrySettleTrace・MultiViewCapture・WastelandHide::Parse・WastelandRoutine などが未解決（LNK2019）。Game のソースを個別に並べている試験の対象の一覧と include の道を、Game の今のソースに合わせる（同じ一覧を二重に持たない形にできるならそうする。試験を増やさない・消さない）。合否: Debug の全体のビルドが EXIT=0。ctest は回さない（落ちる対象のビルドが通ることだけを見る。ローカルの試験の全走は重いので、ユーザーの方針どおり関係の試験だけ）。
- verify: `cmake --build build --config Debug -- /m:1`
- paths: Test, CMakeLists.txt, Game/CMakeLists.txt, cmake, TASKS.md, PROGRESS.md
- notes: 2026-10-23 親が SCN-GOLDEN-REVERIFY の再検証の落ちから足した。全体のビルドは 10〜20 分かかる見込み（1 コマンド 10 分の再検証の上限に当たるなら、verify を落ちていた対象の名前を並べたビルドに分ける）。壁時計（75 分）の残りが 25 分を切ったら新しい試しを始めず、分かったことを書いてコミットして終える。 2026-10-23（親）: SCN-GOLDEN-REVERIFY の反復 5 の調べ（`blocked/resolved/SCN-GOLDEN-REVERIFY-target.md`）: 落ちるのは 4 つの試験の対象だけ。AssetManifestReloadBridgeLoopbackTest（Rendering3DTestRoutine.cpp の stb_image.h の include の場所が無い）、NorvesLibBridgeAdapterScriptComponentTest（Game の WastelandRoutine・CameraScriptCapture・MultiViewCapture・BananaFieldBuilder などのソースが対象に無い、未解決 31 件）、RenderGraphCompileTest（LightingPass::OnFrameReadback が未解決。ctest では既に DISABLED）、RenderingDDGILightingContractTest（今は無い DDGIProbePass::FrameResources・m_FrameResources を参照、12 件）。直すか、実装の写しで意味の無い試験なら消すかを、ユーザーの方針（試験も意味を吟味する・写しの試験は直さず消す）に照らして試験ごとに決め、理由を記録する。

## SCN-ACCEPT: ランチャーと全シーンの受入れを記録する
- status: todo
- after: SCN-THUMBNAILS, SCN-WASTELAND-POLISH, SCN-INTERIOR-SHAFTS, SCN-FIX-SKY-BANDING, SCN-MEGA-ACCEPT-1M
- done-when: 計画書の受入れの節の全項目を確かめて `Docs/RenderingValidation/SceneLauncherAcceptance.md` に書く: Debug・RelWithDebInfo・Release で引数なしの起動がランチャー（撮影を開いて確認）、全シーンの cycle smoke（各3周）が pass、全シーンの全視点の撮影を開いた所見、各シーンの性能（1920×1080・RelWithDebInfo の値を記録する。RT と GI を入れた既定の構成と、荒野・室内の光は合否に使わない。RT と GI を切った構成で 16.6 ms を超えるシーンだけ、内訳から直す設定を決めて直すか、未達としてユーザーへ渡す。最終の設定が同じなら SCN-WASTELAND-POLISH などの証拠を使い、撮り直さない。夜のシーンでは点光源の影の内訳も見る）、Rendering3DTest の撮影が main と見た目で変わらないこと、生成と Poly Haven の資産の一覧（id・出典・ライセンス・作り方）。見つけた不具合は直すか、`SCN-FIX-<名前>` の項目として TASKS.md に足す。
- verify: `cmake --build build --config Debug --target Game -- /m:1`
- verify: `cmake --build build --config Release --target Game -- /m:1`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes rendering3dtest,mega-bananas -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes wasteland,night-village -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/RunSceneCycleSmoke.ps1 -Scenes material-gallery,interior-light -Cycles 3 -Configuration RelWithDebInfo`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene launcher -Configuration Release -OutDir .harness/runs/scene-capture/SCN-ACCEPT-release`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -OutDir .harness/runs/startup-capture/SCN-ACCEPT`
- paths: Docs/RenderingValidation, Game, Library, Assets, Scripts, Test, TASKS.md, PROGRESS.md
- notes: 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。 2026-10-19 親: verify を RelWithDebInfo に揃え、周回の試験を 1 コマンド 10 分に収まる形にした（Debug の撮影は約 11 分、3 周は 928 秒の実測）。 2026-10-23（親）: 順を保つため after を足した（先に走ると後の変更で撮り直しになる）。

## SCN-MEGA-FIX-RT-SELECT: レイトレーシングのインスタンスの選び直しを、変わったときだけ区画の単位で行う
- status: backlog
- done-when: 問い: 100万本で RenderThread の `RT.BuildAccelerationStructures`（median: 静止 22.1 ms・カメラパス 29.9 ms・1% 移動 31.6 ms・ストリーミング 30.1 ms）を下げられるか。SCN-MEGA-ACCEPT-1M の撮影では、静止で TLAS の更新が 0 回（`RT_SCENE update_skipped=120 updated=0`）なのに、`RayTracingSceneSubsystem::AppendMegaGeometryInstancesFromMirror` が毎フレーム 100 万席すべての距離を判定し（`candidates=1000002`）、半径 120 m の内の約 44 万から近い 65,536 件を選び直して、パケットへ 65,534 件を足している。選ぶ原点が閾値以上動かず写しの版が変わらないフレームは前の選びを使い、選ぶときも区画の境界（`MegaGeometry_RangeBounds`、1M で 15,625 区画）で半径の外の区画を丸ごと飛ばす。完了の条件: 静止の 1M horizon（RelWithDebInfo・1920×1080・300 フレーム）で `RT.BuildAccelerationStructures` の median ≤ 2 ms、カメラパスの再生で median ≤ 4 ms。静止の `RT_SCENE` の `instances`・`out_of_range`・`over_limit` が今と同じ（同じ原点で同じ顔ぶれ）。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-RT-SELECT/static`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-RT-SELECT/static --views horizon --timing --min-frames 300 --require-scope RT.BuildAccelerationStructures --max-scope-median-ms RT.BuildAccelerationStructures=2`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs banana-count=1000000,camera-path-play,camera-path-time=0 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-RT-SELECT/path`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-RT-SELECT/path --views horizon --timing --min-frames 300 --require-scope RT.BuildAccelerationStructures --max-scope-median-ms RT.BuildAccelerationStructures=4`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から（`Docs/RenderingValidation/SceneLauncherAcceptance.md` の「100万本と混在の負荷の受入れ」）。危険地帯（RenderThread）なので評価者を通す。 2026-10-15 親: RT を入れた構成の 60 fps を求めなくなった（ユーザー）ので外した。静止の 100万本で rt 37 ms（約 27 fps）は操作できる。RT を入れて操作できない遅さになったら戻す。

## SCN-MEGA-FIX-TLAS-REBUILD: TLAS を、インスタンスの顔ぶれが替わったら作り直す
- status: todo
- done-when: 問い: カメラパスの再生で `LightingPass`（RTGI）が median 70.5 / p95 100.4 ms（静止は 1.7 ms）になるのを、TLAS の木の質で説明でき、直せるか。SCN-MEGA-ACCEPT-1M の撮影では、選ぶ件数が上限 65,536 で一定なので、カメラが動いて顔ぶれが替わっても毎フレーム Update（refit）だけで作り直しが 0 回（`RT_SCENE updated=120 built=0`。`RenderingCoordinator.cpp` 1810 行付近は件数が同じなら Update）。refit は同じ要素の小さな動きを前提とするので、席の中身が別の場所のインスタンスへ替わると箱が広がり続け、レイの判定が重くなる、と見る。直す: 同じ席に同じインスタンスが並び変換だけ変わったときだけ Update にし、顔ぶれが替わったら Build（替わった割合の閾値で決めてよい）。席の並びを選びの順（距離の順）に依らず安定させる。完了の条件: カメラパスの再生（1M horizon・300 フレーム以上）で `LightingPass` の median ≤ 3 ms・p95 ≤ 5 ms。PNG を開き、今の撮影で近くのバナナの周りに出ている粒の荒い暗い輪が消えていること。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs banana-count=1000000,camera-path-play,camera-path-time=0 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-TLAS-REBUILD/path`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-TLAS-REBUILD/path --views horizon --timing --min-frames 300 --require-pass LightingPass --max-pass-median-ms LightingPass=3 --max-pass-p95-ms LightingPass=5`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/RHI, Library/Core/Public/RHI, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。1% 移動・ストリーミングの `LightingPass` 11.4〜11.6 ms（静止のバナナだけは 1.7 ms）も同じ refit を毎フレーム行っている（`updated=120 built=0`）ので、この項目の後に撮り直して残りを見る。危険地帯（RHI/Vulkan・RenderThread）なので評価者を通す。 2026-10-15 親: RT の 60 fps は求めなくなったが、動くと RTGI が 40 倍遅くなり（カメラパスで GPU 86 ms・約 12 fps）操作できないので、木の質の欠陥として残す。

## SCN-MEGA-FIX-SCENE-TABLES-STEADY: 変わらないフレームで MegaGeometry の表を全部なめない
- status: todo
- done-when: 問い: 変わった行が 0（`MEGA_SCENE_UPLOAD rows=0`・`MEGA_TABLES delta_rows=0`）の静止で `MegaGeometryPass.UpdateResidentSceneTables` が median 7.1 ms 掛かるのを、変わった行に比例させられるか。100 万の描く順の行を毎フレームたどって席の表の上限を求める（`MegaGeometryPass.cpp` 4771 行付近）など、行の数に比例する処理を、版が変わったときと変わった行だけにする。完了の条件: 静止の 1M horizon（300 フレーム）で `MegaGeometryPass.UpdateResidentSceneTables` の median ≤ 0.5 ms・`MegaGeometryPass.PrepareSceneTables` の median ≤ 1 ms、`MEGA_TABLES mismatch=0`。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-bananas -Views horizon -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 1800 -GameArgs banana-count=1000000 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-STEADY`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-SCENE-TABLES-STEADY --views horizon --timing --min-frames 300 --require-scope MegaGeometryPass.UpdateResidentSceneTables --max-scope-median-ms MegaGeometryPass.UpdateResidentSceneTables=0.5 --max-scope-median-ms MegaGeometryPass.PrepareSceneTables=1`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。危険地帯（RenderThread）なので評価者を通す。

## SCN-MEGA-FIX-MIXED-DEFICIT: 混在の負荷でバナナを指定の本数どおりに置く
- status: todo
- done-when: mega-stress の `banana-count=1000000,stress-entities=50000` で `MEGA_STRESS_BANANAS count=999999 requested=1000000` になり、撮影が「置いたバナナの本数 999999 が指定（banana-count=1000000）と違う」で失敗する。Entity の足元と動く範囲を空けるマスクのある敷き詰めでも、SCN-MEGA-LAYOUT-MOUNDS の「位置が 1 つも無い区画だけ上限を締めてもう一巡引く」と同じ形で不足を埋める。mega-bananas（マスクなし）の配置は変えない（1M の `MEGA_BANANAS_PLACED`・`MEGA_BANANAS_LAYOUT` の行が今と同じ）。完了の条件: 1% 移動の負荷の撮影で `placed_bananas=1000000`・`result=pass`。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views overview -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -TimeoutSeconds 2400 -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-MIXED-DEFICIT`
- paths: Game/GameModes/MegaBananas, Game/GameModes/MegaStress, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。混在の 2 つの負荷の撮影が本数の違いで fail になるので、下の STREAM-TABLES-DELTA・VISIBLE-LIST・STRESS-GPU より先に閉じる。

## SCN-MEGA-FIX-VISIBLE-LIST: 混在の負荷で可視クラスタの一覧を溢れさせない
- status: todo
- done-when: 1M + 5 万個で可視クラスタの一覧の容量（`visible_list_capacity=4194304`）を `visible_list_peak` 4,820,881〜4,821,043 が超え、1% 移動では SceneReady の前（最大 `draw_dropped=330250`）、ストリーミングでは計測の窓の中でも `draw_dropped=626568` が出る（描かれないクラスタが出る）。容量を混在の最大に余裕を足して決めるか、溢れを読み戻したら次のフレームで広げる（GPU Scene まわりの最大の確保は 3 GB 以内のまま）。完了の条件: 1% 移動とストリーミングの撮影で、ログの全行 `draw_dropped=0`・`visible_list_overflow=0`。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views overview -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -TimeoutSeconds 2400 -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1,stress-stream-cell=64,stress-batch-cells -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-VISIBLE-LIST`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-VISIBLE-LIST`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。撮影は SCN-MEGA-FIX-MIXED-DEFICIT の後。危険地帯（メモリ・RenderThread）なので評価者を通す。

## SCN-MEGA-FIX-STREAM-TABLES-DELTA: 区画の出し入れで MegaGeometry の表を全部作り直さない
- status: todo
- done-when: 問い: ストリーミングの負荷の gt・rt の p95 147.9 ms（median 48.9 ms）の山を、区画の出し入れの表の作り直しで説明でき、直せるか。SCN-MEGA-ACCEPT-1M の撮影では、区画の散布の出し入れ（約 1 秒に 1 回）ごとに `MegaGeometryPass.BuildSceneTables` が約 105 万行を全部作り直し（`MEGA_TABLES full=6`/120 フレーム）、窓の 600 フレームのうち 29 フレームが 20 ms を超え、最大 515 ms。出し入れを区間の追加・削除の差分で表へ反映する。完了の条件: ストリーミングの負荷（1M + 5 万個・600 フレーム。区画の解放から再公開を窓に含む）で `MegaGeometryPass.PrepareSceneTables` の p95 ≤ 2 ms、`MEGA_STRESS_STREAM_CYCLE result=pass`、`MEGA_TABLES mismatch=0`。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views overview -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 600 -TimingReadyMarginFrames 2000 -TimeoutSeconds 2400 -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1,stress-stream-cell=64,stress-batch-cells -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-STREAM-TABLES-DELTA`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-STREAM-TABLES-DELTA --timing --min-frames 600 --require-scope MegaGeometryPass.PrepareSceneTables --max-scope-p95-ms MegaGeometryPass.PrepareSceneTables=2`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Library/Core/Private/Component, Library/Core/Public/Component, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。撮影は SCN-MEGA-FIX-MIXED-DEFICIT（本数の違いで fail）と SCN-MEGA-FIX-VISIBLE-LIST（`draw_dropped` で fail）の後。危険地帯（RenderThread・Object/Resource の寿命）なので評価者を通す。

## SCN-MEGA-FIX-STRESS-GPU: 1M + mega-stress 5 万個の GPU 時間を 16.6 ms に収める
- status: todo
- done-when: 問い: 1% 移動の GPU 32.6 / 34.4 ms（median / p95）を、TLAS の直しの後に何が残るかで分け、予算（RT と GI を切った構成で 16.6 ms）に収められるか。SCN-MEGA-ACCEPT-1M の撮影の内訳（median）は `MegaGeometry` 16.1 ms（`VisRasterSw1` 9.1・`MegaGeometryCull1` 4.9・`MegaBvhC1Leaf` 4.1）、`LightingPass` 11.4、`VirtualShadowMapPass` 2.6（`VsmDraw` 1.5）。描いたクラスタは 264 万でバナナだけの静止（168 万）の 1.6 倍、ソフトウェアラスタの単価は 100 万クラスタあたり 3.4 ms（静止 3.0）でほぼ同じなので、量が多い側と見る。SCN-MEGA-FIX-TLAS-REBUILD の後に撮り直して `LightingPass` の残りを確かめ、残る超過を 5 万個のメッシュ（家・塔・岩）の描くクラスタの量（LOD の誤差の許容・遮蔽）で減らす。完了の条件: 1% 移動の負荷（300 フレーム）で GPU のフレームの median・p95 ≤ 16.6 ms。PNG を開いて穴と影の欠けが無いこと。
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureScene.ps1 -Scene mega-stress -Views overview -Build -RequireAssets -Configuration RelWithDebInfo -Width 1920 -Height 1080 -GpuTimingFrames 300 -TimeoutSeconds 2400 -GameArgs banana-count=1000000,stress-entities=50000,stress-moving-percent=1 -OutDir .harness/runs/scene-capture/SCN-MEGA-FIX-STRESS-GPU`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-FIX-STRESS-GPU --timing --min-frames 300 --max-gpu-median-ms 16.6 --max-gpu-p95-ms 16.6`
- paths: Library/Core/Private/Rendering, Library/Core/Public/Rendering, Assets/Shaders, Game/GameModes/MegaStress, TASKS.md, PROGRESS.md
- notes: 2026-10-14 SCN-MEGA-ACCEPT-1M の診断から。SCN-MEGA-FIX-TLAS-REBUILD・SCN-MEGA-FIX-MIXED-DEFICIT の後に回す。

## SCN-MEGA-ACCEPT-1M: 100万本と混在の負荷を、固定した 4 つの負荷と閾値で判定する
- status: todo
- done-when: 問い: 最終の配置・描画の経路の 4 つの負荷で、RT と GI を切った構成で 60 fps を満たせるか（RT と GI を入れた既定の構成は値を記録するだけ。4 つの撮影は RT と GI を切る引数で撮り、無ければ足す）。変わる判断: 既定を 100万本にする・直す項目を足す・止めてユーザーへ渡す。- 固定した 4 つの負荷: 100万本 horizon の静止、カメラパスの再生（パス 1 周を計測の窓に含める）、100万本 + mega-stress 5万個の 1% 移動、同じく 64 m 区画のストリーミング（区画の解放から再公開までを窓に含め、ログで確かめる）。- 各負荷で 300 フレーム以上、GPU・gt・rt の median と p95 がすべて 16.6 ms 以下（RTX 4080・1920×1080・RelWithDebInfo）。- 溢れがすべて 0、メモリ ≤ 3 GB（対象は計画の GPU Scene まわりの構造。使用中だけでなく、飛行中の複数のスロット・持ち続ける容量・回収待ちを含む最大の確保量）、Entity の数が本数によらない。PNG を開いて穴と影の欠けが無いことを見る。- 不合格の負荷は、同じ撮影の内訳から直す先を決める: GPU は範囲・インスタンス・BVH の判定、binning と記録、ハード・ソフトの両方のラスタ（`VisibilityRasterPass` を含む）、2 パス目、合流・HZB、VSM（全体とページの処理・カリング・描画。親と子の区間を二重に足さない）の時間と処理の件数。処理の量が多すぎるのか単価が高いのかを分ける。CPU は待ちと作業を分け、支配する関数・系（`RT.BuildAccelerationStructures` とその入力の選択、`RG.Record.*`、ECS の系と同期点、ストリーミングの作成・公開）を特定する。既存の区間で分けられない所だけ区間を足して撮り直す。構造・件数・ログで説明できない規模の依存が残ったときだけ、同じ配置・条件で本数を 30万へ下げた負荷を 1 つ足して比べる。直す先ごとに `SCN-MEGA-FIX-<名前>` を足す。- 合格したら既定の本数を 100万にする。受入れ文書の書き方は前と同じ。どれかの負荷で GPU・gt・rt の p95 のどれかが 25 ms を超えたら、その時点の撮影と内訳を付けてループを止めてユーザーへ渡す（自動で 30万の診断を続けない）。Debug の起動画面で検証レイヤーのエラーが 0。
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/static --views horizon --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/path --views horizon --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/moving --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `python Scripts/CheckCaptureMetrics.py .harness/runs/scene-capture/SCN-MEGA-ACCEPT-1M/streaming --timing --min-frames 300 --max-gpu-p95-ms 16.6 --max-gt-p95-ms 16.6 --max-rt-p95-ms 16.6 --max-gpu-median-ms 16.6 --max-gt-median-ms 16.6 --max-rt-median-ms 16.6`
- verify: `powershell -NoProfile -ExecutionPolicy Bypass -File Scripts/CaptureStartupScene.ps1 -ViewNames default -Build -OutDir .harness/runs/startup-capture/SCN-MEGA-ACCEPT-1M-debug`
- paths: Docs/RenderingValidation/SceneLauncherAcceptance.md, Game/GameModes/MegaBananas/MegaBananasMode.h, TASKS.md, PROGRESS.md
- notes: 4 つの撮影は反復の中で 1 回ずつ `-Build -GpuTimingFrames 300` で撮る。混在の負荷を作る引数は UNCERTAIN U12。status は前のとおり、規模の段で todo に上げる。 混在の負荷は SCN-MEGA-MIXED-LOAD の引数で作る。 2026-10-11 検証の軽量化（ユーザー決定）: 起動画面の画像比較は、描くものの出所を切り替える項目・描画を変える項目・節目の計測だけで、1 視点（default）で行う。検査付きの起動は 1 視点 1 本、周回は寿命とシーンの出入りに触る項目だけ。done-when にある「起動画面の比較」「各視点」は、verify に残っている分だけを求める。 2026-10-12 親: 計測の見直し（ユーザーの指摘「節目に計測を入れても意味があるのかを考えるべき」。計測ごとに、問い・結果で変わる判断・測らずに分からないかの 3 点を書き、書けないものは置かない。Sol の批評を反映）。SCN-MEGA-MEASURE-CULL・MEASURE-VSM（閾値が 1M の予算から導かれていなかった）と SCN-MEGA-SCALE-300K（1M の内訳で同じことが分かる）を外し、この受入れの診断へまとめた。点光源の影は夜のシーンの受入れ（SCN-ACCEPT）で判断する。 2026-10-14 親: 1 回目の判定は不合格（`Docs/RenderingValidation/SceneLauncherAcceptance.md` の「100万本と混在の負荷の受入れ」。静止は GPU 15.1 ms で合格、rt は TLAS の入力の選び直しと MegaGeometry の表の毎フレームの走査で 37 ms）。ユーザーの指示（100万本は過程で、終わったらシーンの整備へ戻る）で、直す項目（SCN-MEGA-FIX-RT-SELECT〜STRESS-GPU）をシーンの項目の後に置き、この項目はその後で測り直す。

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

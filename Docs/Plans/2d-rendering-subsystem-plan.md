# NorvesLib 2D 描画サブシステム実装計画（BoardComponent + CanvasView 確定版）

保存先: `Docs/Plans/2d-rendering-subsystem-plan.md`
対象リポジトリ: `SioKo-Shox3/NorvesLib`
対象ブランチ: `develop`
重点領域: `Library/Core/Public/Component/BoardComponent.h` / `Private/Component/BoardComponent.cpp`（新規）, `Library/Core/Public/Rendering/SceneProxy.h`（`BoardProxy` 追加）, `Library/Core/Public/Object/World.h` / `Private/Object/World.cpp`（`SyncEntityRecursive` 分岐 + ScreenSpace Board sink）, `Library/Core/Public/Rendering/SceneView.h` / `Private/Rendering/SceneView.cpp`, `Library/Core/Public/Rendering/CanvasView.h` / `Private/Rendering/CanvasView.cpp`, `Library/Core/Public/Rendering/RenderingCoordinator.h` / `Private/Rendering/RenderingCoordinator.cpp`（カメラ表 + GenerateDrawCommands）, `Library/Core/Public/Rendering/RenderFrameExecutor.h` / `Private/Rendering/RenderFrameExecutor.cpp`, `Library/Core/Public/Rendering/PresentationPass.h` / `Private/Rendering/PresentationPass.cpp`, `Library/Core/Public/Rendering/CompositePass.h` / `Private/Rendering/CompositePass.cpp`, `Library/Core/Public/Rendering/RenderGraph/RenderGraphResourceNames.h`, `Library/Core/Public/Rendering/Viewport.h` / `Private/Rendering/Viewport.cpp`, `Library/Core/Public/Rendering/ViewRenderPlan.h`, `Library/Core/Public/Rendering/FramePacket.h`, `Library/Core/Public/Rendering/DrawCommand.h`, `Library/Core/Public/Rendering/MeshResources.h`, `Library/Core/Public/Rendering/VertexLayout.h`, `Assets/Shaders/`（新規 2D シェーダー）, `Library/Core/CMakeLists.txt`, `Docs/Architecture/RenderingFlow.md`

---

## 0. タイトル・対象・目的・前提

### 0.0 本計画の性格（旧版の全面置換）

> **本ファイルの旧版（および前々版）は破棄する。** 旧版は 2D を「Component 非依存・カメラ基準でバイパスし、`LayerView` が `Board` を直接所有して `PrepareBoardDrawCommands` で独立オーサリングする」案だった。本確定設計はこれを**全面的に置換**し、2D を **World → Entity → BoardComponent → BoardProxy** という既存 3D と同一のオーサリング経路に載せる。旧版の名称・所有方針はこの文書中に**一切残さない**。
>
> ただし、旧版の**合成側の節（段1/段2 のテクスチャフロー、CompositePass、PresentationPass 優先順、RenderFrameExecutor の 2 段化、方式 A/B、順序 2 階層 + 明示安定ソート）は概ね正しい**ため踏襲する。本計画では旧 `LayerView` を **`CanvasView`** へ改名し、合成側はその設計を移植する。差し替えるのは**オーサリング側**（World/BoardComponent/BoardProxy/RenderLayer ルーティング/共有カメラ）のみである。
>
> **旧版ドキュメントの後始末:** 本ファイル（`Docs/Plans/2d-rendering-subsystem-plan.md`）が 2D 描画計画の唯一の正準であり、過去の全 2D 描画計画を上書き置換する。もし旧版が別ファイル（例 `2d-rendering-plan-v0.md` 等）として残存していれば `Docs/Archive/` へ退避するか削除し、`LayerView`/「Component 非依存バイパス」/「カメラ基準バイパス」の記述が二次資料として残らないようにする（開発者の混乱防止）。F0 で旧版ファイルの有無を確認し成果物に記録する。

> **【重要・時制の規約】** 本計画は **「現状（実コード）」** と **「実装後の目標状態」** を明確に分ける。F2 landed 後の現状では `CanvasView`/`CompositePass`/`Canvas.Color`/`Composite.Color`/`RenderFrameExecutor` 2 段化に加え、`RenderingCoordinator` のカメラ表、`Viewport` の `CameraId` 参照、CanvasView の full-rect 正射影 Viewport、`OrthoWidth`/`OrthoHeight` literal 化、deferred Canvas camera sync が**既に存在する基盤**である。`BoardComponent`/`BoardProxy`/`BoardSpace`/ScreenSpace Board sink/Canvas Board store/Board draw command 生成/CompositePass の実 alpha-over は F3 以降、`UVRect` は F6 の実装対象である。
>
> - **§2（現状整理）** は **F0/F1/F2 landed 後に確認される実コードのベースライン**を記述する。ここに現れる型・関数・file:line は**すべて既存**（`View`/`SceneView`/`CanvasView`/`Viewport`/`CameraProxy`/カメラ表/`MeshProxy`/`MeshComponent`/`World::SyncEntityRecursive`/`RenderingCoordinator::GenerateDrawCommands`/`PresentationPass`/`CompositePass`/`RenderFrameExecutor`/`RenderGraphResourceNames`）。**§2 に未実装機能の断定を混ぜない。** 「現状こうなっている（だからここに足す）」という読み取り専用記述に統一する。
> - **§3 以降（目標アーキテクチャ・フェーズ計画）** は **「実装後にこうなる（will be）」** という未来時制で読む。ただし F2 landed 済みのカメラ共有・Canvas 正射影 Viewport・合成アンカーは既存として扱い、F3 以降で新設する型・関数・draw command 生成だけを未来形で扱う。各 §3 項目は、どの §2 既存項目から拡張するかを明示する。
> - 計画中の file:line は **F0 確定前の予測値**を含む。新規追加位置（例「`World.cpp:921-1062` の Component 振り分けに `BoardComponent` 分岐を足す」）は**既存コードの位置を指す**もので、足した後の行番号ではない。F0 で実コードと突き合わせ `2d-rendering-baseline.md` に確定値を記録する。

### 0.1 目的

NorvesLib は F2 landed 後の現状で **3D の Deferred + Forward 透過パイプラインに加え、空 Canvas 合成アンカーと Canvas 用共有カメラ基盤**（`CanvasView`/`CompositePass`/`Canvas.Color`/`Composite.Color`/`RenderFrameExecutor` 2 段化、`RenderingCoordinator` カメラ表、`Viewport.CameraId`、CanvasView full-rect 正射影 Viewport）を持つ。ただし、2D オーサリング機能（スプライト/ビルボード/インポスター/テクスチャアニメーション/スプライトシート分割）はまだ無く、`SceneProxy.h` には `MeshProxy`/`MegaGeometryProxy`/`LightProxy`/`CameraProxy` のみ、`DrawCommandType` は 2D 用ペイロードを持たない。

本計画の最終目標は、既存の所有権モデル（Object: `World → Entity → Component` の Inner/Outer + ObjectHeap、Resource: 参照カウント）・スレッドモデル（GameThread→RenderThread を FramePacket スナップショット越しに通信）・RHI 境界（Rendering 層は抽象 `RHI::I*` のみ）を **一切壊さずに**、その上に **既存 World→Proxy 機構に載せた 2D 描画サブシステム**を新設することである。

確定アーキテクチャの核心は次の 8 点（ユーザー確定）:

1. **2D は World に載せる。** `Entity` 下に `BoardComponent`（`MeshComponent` と同型、`REFLECTION_CLASS`/`PROPERTY` で prefab シリアライズ対応、transform は Entity から取得）を置く。3D と同じ `World → Entity → Component` オーサリング経路に乗せる。Component 非依存バイパス案は破棄。
2. **World 同期で `BoardProxy` を生成。** `World::SyncEntityRecursive`（`World.cpp:921-1062`）が `MeshProxy`/`LightProxy` を作るのと同様に `BoardProxy` を作る（ComponentId primary key、ObjectId payload、live-set/RemoveStale を踏襲）。2D 専用バイパスでなく既存の World→Proxy 機構に載せる。
3. **ルーティング = RenderLayer マスク × カメラ CullingMask（方式 A 確定）。** `BoardProxy` は `RenderLayer` マスクを持ち、各カメラの `CullingMask`（`CameraProxy::CullingMask` `SceneProxy.h:280`、既存）との交差で可視/投入先を決める。ルーティング決定は **GameThread（オーサリング時）** で行い、各 Proxy を対象 View（SceneView/CanvasView）の per-viewport スナップショットへ振り分ける。BoardComponent に単一ターゲット Viewport を直接持たせる方式 B は不採用。
4. **空間種別。** Board は `WorldSpace`/`ScreenSpace` 属性を持つ。`ScreenSpace` board は CanvasView の正射影カメラ Viewport へルーティングされ、transform をそのカメラの画面 px 空間として解釈。`WorldSpace` board（= Billboard/Impostor）は SceneView へルーティングされ 3D カメラ・深度を共有する。
5. **カメラ = Viewport 非依存の共有データ。** `CameraProxy`（`CameraId` 付き、`SceneProxy.h:259`）を GameThread 側で ID 管理（小さなカメラ表）し、Viewport は `CameraId` を参照する。複数 Viewport が同一カメラを共有できる。毎フレームのスナップショット `ViewportRenderPlan.Camera` は従来どおり値コピー（`ViewRenderPlan.h:32`）。新たな live Camera クラスは作らず `CameraProxy` をカメラ実体とする。
6. **View 命名。** 2D 合成先 View は **`CanvasView`**（合成を受ける面）。「layer」は CanvasView 上で合成される順序付き要素群に限定し、View 名を `LayerView` にしない。SceneView は 3D（+world-space Board）。
7. **合成側（旧版確定を維持）。** RenderGraph は View ごとに `Reset()`（`View.cpp:195`）され named resource を View 間で直接共有できない → 各 View が出力テクスチャを publish → 段2 の CompositePass が 2D を 3D の上へ 1 段合成 → PresentationPass が優先順 `Composite.Color → PresentationColor → ToneMappedColor` で 1 回 blit。CanvasView は基底 `View::Render` を完全オーバーライドし `PresentationGraphPass` を抑止。`RenderFrameExecutor` を 2 段化し、単一 SceneView 時は従来経路へフォールバック。方式 A（単一 Canvas カラーターゲットへ painter 順描画）既定 / 方式 B（レイヤー単位 RT + 不透明度）opt-in。順序 2 階層（レイヤー優先度 + レイヤー内安定 SortKey）。`DrawCommandSorter` は `std::sort`（非安定）なので CanvasView 側で明示安定ソート。
8. **World→多 View 化は段階導入。** `MeshProxy → SceneView` は現状維持し、`BoardProxy → CanvasView` ルーティングを並走で足してから一般化する。シングルトン禁止、RHI 境界厳守、独自型厳守。

本計画は **加算的（additive）** を最優先する。既存 3D 経路（SceneView の Deferred パスチェーン、`MeshProxy → SceneView`）の挙動を変えず、2D は `BoardComponent → BoardProxy → CanvasView` として積み上げる。各フェーズは独立してレビュー・ビルド・コミット可能な最小単位に分割し、依存順に積む。

### 0.2 前提（上位計画・文書への参照）

- `Docs/Architecture/RenderingFlow.md`: GameThread→RenderThread のスナップショット原則、RenderGraph named resource 契約、RHI 境界。本計画はこの原則に従う（2D 用 BoardProxy のスナップショットを FramePacket に載せ、RenderThread はスナップショットのみ読む）。整備途中で最新でない可能性があるため、実コードを正とし本書のアンカー（§2）を一次根拠とする。
- `Docs/Plans/debug-render-modes-plan.md`（**完了済み**・develop に landed）: per-viewport スナップショット配線の様式（`ViewportRenderPlan` への値コピー、`ViewRenderContext::GetActive*()` イディオム、`DebugViewMode DebugMode` フィールドが `ViewRenderPlan.h:34` に既存）。2D の per-viewport スナップショット拡張はこの様式を踏襲し、**landed 済みのデバッグ描画契約（`DebugMode` フィールド・per-viewport 配線）を退行させない**。
- `Docs/Plans/object-entity-scenegraph-refactoring-plan.md`（**完了済み**・develop に landed）: Entity ツリー所有・再帰走査・ObjectId 採番・Component→Proxy 同期・`ComponentDataRegistry` の確立済み設計。本計画の `BoardComponent` はこの確立済み経路（`Entity` Inner として Component を持ち、`SyncEntityRecursive` で proxy 化）に**そのまま相乗りする**。`MeshComponent`/`BuildMeshProxy`（`MeshComponent.cpp:186-247`）が正準テンプレート。

> **参照計画の landed 状態の検証（レビュー minor）:** 上記参照計画の「完了済み・landed」は計画策定時の認識であり、**F0（read-only）で実コードの landed 状態と `CLAUDE.md` との整合を再確認・記録**する（`DebugMode` フィールドの存在、`ComponentDataRegistry` の有効化経路、`SyncEntityRecursive` の現行形など）。矛盾があれば baseline.md に記録し本計画の前提を補正する。
- `CLAUDE.md`（ルート、一次情報）: 独自型ルール、所有権モデル、レンダリング/スレッド規律、シングルトン禁止、マルチエージェント・オーケストレーション、モデル選定方針。本計画全体に適用。

---

## 1. 基本方針

- **フェーズ順序は入れ替えない。** 後続フェーズは、先行する依存フェーズが実装レビューと検証ゲートを通過してから着手する。各フェーズは独立してレビュー・ビルド・コミット可能な最小単位に割る。**F0 は前提確定フェーズ**であり、World→Proxy 同期の BoardProxy 追加点 / RenderLayer-CullingMask の現状使用と交差ルーティングの配線規模 / カメラ共有化の影響範囲 / per-viewport オーサリングへの Board 振り分け点 / 合成側アンカー（`Reset`・`Presentation` 条件・2 段化）/ SortKey ビット割付 / ScreenSpace transform 解釈 / 正射影正準を実コードで確定し記録してから F1 実装へ進む（F0 未確定のまま F1-F11 を始めない）。
- 各フェーズのゲートは **focused ターゲットビルド + CTest（focused + 全体）+ 目視確認（描画可視の変更がある場合、MT/ST 両モードでスクショ）+ コミット** とする。**機能可視フェーズ（F3/F4/F5/F6/F7/F9/F11）はゲートに Game テストシーン + 参照スクショを含める**（テストシーンで当該機能をショーケースし、`Docs/Plans/assets/2d-rendering/F{N}_expected.png` に参照スクショを置き目視 or pixel 比較で検証）。
- **CMake 登録・UTF-8+BOM+CRLF はフェーズ内で完結させる。** 新規 `.cpp` / `.frag` / `.vert` / テスト `*Test.cpp` を追加するフェーズでは、その同じフェーズのゲートを通す前に `Library/Core/CMakeLists.txt` の `PRIVATE_SOURCES`（`Private/Component/MeshComponent.cpp` が `:116` 付近）/ `PUBLIC_HEADERS`（`Public/Component/MeshComponent.h` が `:324` 付近）への追加と、`Test/<Area>/CMakeLists.txt`（`add_executable` + `add_test` + `PRIVATE Core`、シェーダー読みテストは `target_compile_definitions(... NORVES_SHADER_DIR="${CMAKE_SOURCE_DIR}/Assets/Shaders")`）への登録を完了させる。登録を後段フェーズへ先送りしない（遅延登録は build failure → 手戻りの直接原因）。
- **独自型ルールを厳守する**（`VariableArray`/`FixedArray`/`Map`/`UnorderedMap<Identity,V,Identity::Hasher>`/`String`/`Span`/`TUniquePtr`/`TSharedPtr`/`MakeUnique`/`IsValid`/`DynamicPointerCast` 等）。`std::` コンテナ/スマートポインタ/文字列はエンジン一般コードで禁止。`String` をハッシュキーにしない。GLSL シェーダーは独自型ルール対象外だが、GLSL 構造体は対応する C++ `alignas(16)`（std140/std430）レイアウトと一致させ、レイアウトテストで固定する。
- **所有権モデルを厳守する。** `BoardComponent` は `Entity` の Inner（`Component` 派生、`World::CreateComponent<BoardComponent>(owner)` で生成、Outer 破棄で連鎖破棄）。**Board は Object/Component であり、World ツリーに参加する**（旧版の「Component でない」方針は破棄）。テクスチャは参照カウント Resource（`TextureResources`/`GpuResourceStore`）。`CanvasView` は `RenderingCoordinator` のメンバ（`m_Views` + `m_Screen.m_Views`）として所有される。カメラ表は `RenderingCoordinator` のメンバ。
- **RHI 境界を厳守する。** Rendering 層から `RHI/Vulkan/*` を include しない。2D の PSO・ブレンドステート・サンプラーは抽象 `RHI::I*`（`IDevice`/`ICommandList`/`IDescriptorSet`）と `GraphicsPipelineDesc`（`BlendAttachmentDesc`/`DepthStencilState`）経由でのみ生成する。深度なし PSO は `DepthStencilState` の depthTest/Write を false にして実現する。バックエンド固有処理は `Library/Core/Private/RHI/Vulkan/` に閉じる。
- **RenderThread は live な `World`/`Entity`/`BoardComponent`/`View`/`Viewport` を読まない。** 2D データは GameThread が `SyncEntityRecursive` で BoardProxy 化 → ScreenSpace sink publish → `GenerateDrawCommands` で対象 View の `ViewportRenderPlan` へ instance/draw としてオーサリングし、RenderThread は `ViewRenderContext` のスナップショットスライス（`CurrentViewport` + 共有 `FramePacket.DrawCommands`/`InstanceData`）のみ読む。
- **シングルトン禁止。** 2D 用の新サブシステム・カメラ表は `NorvesEngine GEngine` の傘下（`RenderWorld`/`RenderingCoordinator`）のメンバとして持ち、`GEngine.Get...()` 経由で参照する。新 `<Name>Manager` クラスを作らない。Quad メッシュ・サンプラー・テクスチャは既存 `GpuResourceStore`・`TextureResources`・`SharedResourceRegistry` を再利用する。
- **ログは `Logging/LogMacros.h`（`NORVES_LOG_*`）、計測は `Debug/Stats.h`（`NORVES_STAT_*`）。** `printf`/`std::cout`/手書き `std::chrono` はテスト実行ファイル以外で禁止。
- スタイル: 中括弧は必ず改行配置・単一文でも必須、4 スペース、メンバ `m_`、bool 用途 `b` 接頭辞、interface `I`、template `T`、`&`/`*` は型側、`#pragma once`、ヘッダ内 `using namespace` 禁止、include 順 self→project→std→thirdparty。ヘッダは `Public/<Area>/`、実装は `Private/<Area>/`。
- 新規ソースは **UTF-8 + BOM + CRLF**（BOM が無いと MSVC が日本語コメントを CP932 誤認し C2838 等で失敗）。**シェーダー `.vert`/`.frag` は UTF-8 + CRLF（BOM なし）**（shaderc が BOM を拒否しうる）。既存ファイル編集後は `git diff --numstat` と `git diff --ignore-cr-at-eol --numstat` を比較し EOL drift を修復してからコミットする。本 Docs はソースでないため **BOM 不要・プレーン UTF-8**。
- **マルチエージェント・オーケストレーション前提。** 各フェーズは実装担当とレビュー担当を必ず別エージェントにする。計画・計画レビュー・実装レビューは最上位ティアを使う。エンジンクリティカルな変更（World 同期 / Proxy レイアウト / ルーティング / カメラ共有 / 新 View 種別 / FramePacket レイアウト / present 合成段 / RenderGraph パス / PSO / RHI / Resource 寿命）は変更が小さく見えても実装レビュー必須とする（§7）。

---

## 2. 現状整理（実コード由来・読み取り専用ベースライン。file:line で裏付け）

調査結果（実コードで検証済み）に基づく**現状（既存コード）**。これらが唯一の現状根拠であり、調査に無い断定はしない。**本節に列挙する型・関数・file:line はすべて現在のコードベースに存在するもの**で、F2 landed 済みの `CanvasView`/`CompositePass`/`Canvas.Color`/`Composite.Color`/`RenderFrameExecutor` 2 段化、カメラ表、`Viewport.CameraId`、CanvasView full-rect 正射影 Viewport、`OrthoWidth`/`OrthoHeight` literal 化、deferred Canvas camera sync を含む。一方、`BoardComponent`/`BoardProxy`/`BoardSpace`/Board sink/Canvas Board store 等の**未実装機能は含まない**（それらは §3 の実装後目標）。各小節末の「→ ここに足す」注記は、実装で拡張する**既存の足場**を指す。

> **未実装の確認（F2 landed 後）:** 次は現在のコードに存在しない（本計画の新設対象）— `BoardComponent`（`Component` 派生）, `BoardProxy`（`SceneProxy.h`）, `BoardSpace` enum（`RenderTypes.h`）, narrow `Rendering::IBoardProxySink`（または同等）, CanvasView の Board store / `PrepareBoardDrawCommands`, Board 用 draw/instance payload, 2D Board 用の共有 quad mesh または shader-generated quad。`UVRect` は F6 で追加するため F3 の未実装対象に含めない。`CompositePass` は F2 landed 時点で Canvas を import するが Scene を素通ししており、F3 で alpha-over Canvas over Scene の実合成へ進める。`BlendMode` は既存だが `MaterialTypes.h:198`（`RenderTypes.h` ではない）。

### 2.1 View / Viewport / カメラ

- `View` が基底（`View.h:55`）、`SceneView : public View`（`SceneView.h:48`）。`ViewType` enum に `Scene/UI/Debug/Custom`（`View.h:27-33`）があり、2D は `ViewType::UI` を流用する。`View::AddViewport`/`GetViewport`/`GetViewportCount`/`GetMainViewport`（`View.h:102-125`）。`GetMainViewport()` は `m_Viewports` 空時に `nullptr`（要 null チェック）。
- 本番描画経路は `View::Render(ViewRenderContext&)`（`View.cpp:173`）。`m_Passes` 空時は legacy `Render()` フォールバック（`View.cpp:182-187`）。`context.Graph->Reset()` を **View ごとに呼ぶ**（`View.cpp:195`）ため named resource は View 間で共有不可。`PresentationGraphPass` 追加は条件付き（`View.cpp:281-284`、`if (context.PresentationGraphPass)`）。
- `Viewport` は `CameraProxy m_Camera` を**値所有**（`Viewport.h:87` の `GetCamera()`、`SetCamera(const CameraProxy&)` は `Viewport.h:82` 宣言・値コピー）し、F2 landed 後は `CameraId` 参照（`SetCameraId`/`GetCameraId`）も併設する。自前の view/projection 行列をキャッシュ（`Viewport.h:92-102`）。コピー禁止（`Viewport.h:55-56`）。
- `CameraProxy` は pure data（`SceneProxy.h:257-290`）: `CameraId`（`:259`、F2 で共有カメラ参照として有効化済み）、Position/Forward/Up/Right、`ProjectionType Projection`（`:268`、Perspective/Orthographic）、FieldOfView/OrthoWidth/OrthoHeight（`:269-274`、F2 で literal 正射影サイズとして尊重）、`ViewportRect Viewport`（`:277`）、`RenderLayer CullingMask = RenderLayer::All`（`:280`）、`RenderOrder`（`:281`）。
- `m_MainCamera` は `CameraProxy`（`RenderingCoordinator.h:354`）、`GetMainCamera()`（`:224`）、`SetMainCamera()`（`:219`、実装 `RenderingCoordinator.cpp` 内で `m_MainCamera` へ値コピー + 主 Viewport へ `SetCamera` コピー）。F2 landed 後は `RenderingCoordinator` がカメラ表（`RegisterCamera`/`UpdateCamera`/`FindCamera`、`m_MainCameraId`/`m_CanvasCameraId`）を持ち、CanvasView の full-rect Viewport は `CullingMask=RenderLayer::UI` の正射影カメラを CameraId 参照で共有する。

### 2.2 スナップショット（per-view / per-viewport）

- `ViewportRenderPlan`（**per-viewport**、`ViewRenderPlan.h:17-63`）= `{ uint32 ViewId; uint32 ViewportId; bool bEnabled; bool bHasCamera; ViewportRect Normalized/Pixel; ScissorRect; uint32 RenderWidth/Height; CameraProxy Camera(:32); DebugViewMode DebugMode(:34); CommandRange Draw/Opaque/Transparent(:36-38); }`。`Clear()`（`:46-62`）が全フィールドを既定へ戻す（`DebugMode` リセットも `:58` で landed 済み）。doc コメント「RenderThread code reads this immutable per-frame value copy」（`:14-15`）。
- `ViewRenderPlan`（**per-view**、`ViewRenderPlan.h:68-85`）= `{ uint32 ViewId; uint8 ViewType; int32 Priority; bool bEnabled; VariableArray<ViewportRenderPlan> Viewports; }`。`Clear()`（`:77-84`）は `Viewports.clear()`。
- `FramePacket.Views` は `VariableArray<ViewRenderPlan>`（`FramePacket.h:79`）。`DrawCommands`/`InstanceData` は全 View 共有の flat array で、各 `ViewportRenderPlan` は絶対 index range で参照。

### 2.3 World → SceneView 同期（既存・landed）

- `World::SyncToSceneView`（`World.cpp:834-873`）が同期トリガー。`UpdateWorldTransforms()`（`:850`、`:875-899`）で top-down トランスフォーム更新後、`GetRootEntities()` から `SyncEntityRecursive` を回し（`:858-868`）、最後に `RemoveStaleMeshProxies`/`RemoveStaleMegaGeometryProxies`/`RemoveStaleLightProxies`（`:870-872`）で live-set 差分回収。
- `World::SyncEntityRecursive`（`World.cpp:921-1062`）は Entity ツリーを再帰。各 Component を `CastTo<MeshComponent>`/`<MegaGeometryComponent>`/`<LightComponent>`（`:943-1026`）で振り分け、dirty 判定（`IsRenderStateDirty()` または `GetLastSyncedTransformVersion() != ownerVersion`、`:948-949`）→ `RefreshRenderTransformCache`（`:966`）→ `BuildXxxProxy`（`:969` 等）→ `ObjectId/ComponentId` 設定 → `m_SceneView->UpdateXxxProxy`（`:973` 等）→ live セット登録（`:974`）。子 Entity へ再帰（`:1051-1061`）。`ComponentDataRegistry` は optional 副 publish 先（`:934-937, 953-961` 等、`IsEnabled()` 時のみ）。
- `MeshComponent::BuildMeshProxy`（`MeshComponent.cpp:186-247`）= proxy 組み立てテンプレート。`outProxy.ObjectId = GetOwnerId()`（`:204`）、`ComponentId`（`:205`）、`WorldTransform = m_WorldTransform`（`:210`）、`WorldBounds`（`:214`）、Materials/BlendModes（`:217-232`）、`LayerMask = RenderLayerProp`（`:238`）、`CustomData`（`:241-244`）。`MeshComponent : Component`（`MeshComponent.h:37`）、`REFLECTION_CLASS(MeshComponent, Component)`（`:39`）、`FieldInitializer`/`IUnknown` コピーコンストラクタ（`:50-55`、prefab デシリアライズ用）。
- `World` は単一 `m_SceneView` に proxy を流す。`World::SpawnEntity<T>(parent)`（`World.h:56`）/ `SpawnObject<T>()`（`:102`）/ `CreateComponent<T>(owner)`（`:119`）が確立済み生成 API。`AttachRootEntity`（`World.cpp:1103`）が ObjectId 採番（`AssignFreshObjectIdsRecursive` `:1123`）と `ComponentDataRegistry` 登録（`:1124`）を行う。

### 2.4 SceneProxy のキーイングと SceneView 登録 API

- `MeshProxy`（`SceneProxy.h:32-105`）: `ObjectId`(`:38`)/`ComponentId`(`:39`)/`SortKey`(`:40`)、`WorldTransform`(`:53`)/`PreviousWorldTransform`(`:54`)、`WorldBounds`(`:60`)、`Materials`/`MaterialBlendModes`(`:66-69`)、`RenderLayer LayerMask = RenderLayer::Default`(`:87`)、`CustomData[4]`(`:93`)。`MegaGeometryProxy.LayerMask`(`:194`)、`LightProxy.AffectedLayers`(`:242`) も同様にマスクを保持。
- `SceneView` 登録 API: `AddMeshProxy`/`RemoveMeshProxy`/`RemoveStaleMeshProxies`（`SceneView.h:87-99`）、`UpdateMeshProxy`（`:141`）、Light/MegaGeometry 系も対（`:101-160`）。内部は ObjectId/ComponentId キーの `UnorderedMap` index + stable-swap-erase。

### 2.5 RenderLayer / CullingMask の現状使用

- `RenderLayer`（`RenderTypes.h:193-202`）: `Default=1<<0, Transparent=1<<1, UI=1<<2, PostProcess=1<<3, Shadow=1<<4, Debug=1<<5, All=0xFF`。`operator|`/`operator&`/`HasFlag`（`:204-217`）。
- `MeshProxy.LayerMask`（`:87`）は `MeshComponent.RenderLayerProp` から設定（`MeshComponent.cpp:238`）。`CameraProxy.CullingMask`（`:280`）は `RenderLayer::All` で初期化されるが、**カリングに一切使われていない**（`SceneView::CullProxies` は錐台/距離テストのみで LayerMask を見ない）。すなわち **マスク × CullingMask 交差ルーティングは現状ゼロから配線する**。
- **マスク交差フィルタは新規ロジック（実装スコープの明示）:** 現状 `SceneView::CullProxies` は錐台/距離カリングのみで、`RenderLayer × CullingMask` 判定はどこにも存在しない。本計画の交差フィルタ（§3.5）は **F3 で `CanvasView::PrepareBoardDrawCommands(viewportPlan)` の GameThread オーサリング時に新設**する（**World/SyncEntityRecursive や RenderThread の描画パスには入れない**）。GameThread が viewport ごとの visible Board 集合を決め、RenderThread は事前に append 済みの不変 `ViewportRenderPlan` スナップショットのみ受け取る。「CullingMask が既にあるからすぐ使える」のではなく、**判定ロジックそのものを足す**点に注意（実装規模を過小評価しない）。

### 2.5.1 per-viewport オーサリングと DrawCommand 構築

- `RenderingCoordinator::GenerateDrawCommands`（`RenderingCoordinator.cpp:802-915`）: `m_Screen.GetViews()` を View ループ（`:804`）、`DynamicPointerCast<SceneView>`（`:818`）、Viewport ループ（`:855`）、`BuildViewportRenderPlan`（`:865`）、`PrepareDrawCommandsForViewport`（`:874`）→ `AppendRebasedDrawCommands`（`:880-887`）で `FramePacket.DrawCommands` へ append、`viewPlan.Viewports.push_back`（`:902`）、`packet->Views.push_back`（`:907`）。F2 landed 後は `BuildViewportRenderPlan` が `Viewport.CameraId` からカメラ表を lookup し、CanvasView の full-rect 正射影 Viewport も value snapshot 化される。**現状 Board の draw/instance はまだ append されない。**
  - **CanvasView Board 分岐の正確な挿入点（実装後）:** View ループ（`:804`）内 Viewport ループ（`:855`）で、`sceneView` cast 成功時の既存 3D 経路（`:872-900`）に対し、**`else if (auto canvasView = DynamicPointerCast<CanvasView>(view))` 分岐を並置**し、その中で GameThread 上の `CanvasView::PrepareBoardDrawCommands(viewportPlan)` を呼ぶ。**3D を先に append → 2D を後に append** することで `FramePacket.DrawCommands` の index が「3D 範囲 → 2D 範囲」の順に安定する（§2.5.1 末・§3.7）。行番号は F2 完了時点から実装時に再確認する。
- `RenderFrameExecutor::Execute`（`RenderFrameExecutor.cpp:11-99`）は `packet->Views` → `Viewports` の二重ループ（`:29-75`）で viewport ごとに `ApplyViewportRenderPlan`（`:55`）→ render → present。**挿入順（ソートなし）**。

### 2.6 合成側スタブ / 本番経路

- `View::Render(ViewRenderContext&)`（`View.cpp:173`）が本番。`context.Graph->Reset()`（`:195`）、`PresentationGraphPass` 条件付き追加（`:281-284`）。legacy `View::Render()`/`CompositeViewports()` は使わない。
- `PresentationPass::Declare`（`PresentationPass.cpp:22-40`）は F1 landed 後に `TryReadTexture` 優先順 **(1) Composite.Color → (2) PresentationColor → (3) ToneMappedColor**へ拡張済み。
- `RenderGraphResourceNames.h`（`:7-20`）に `GBuffer.*`/`Scene.Color`/`Scene.Depth`/`ToneMappedColor`/`PresentationColor`/`Canvas.Color`/`Composite.Color` 等が存在する。
- `CompositePass` は F2 landed 時点で SceneView/CanvasView の persistent RT を import する足場を持つが、出力は Scene を素通ししている。F3 で straight alpha の Canvas over Scene 合成を実装し、`Composite.Color` が visible Canvas を反映することを固定する。
- `DrawCommandSorter` 系の `std::sort` は **非安定**（`DrawCommand.cpp:374/397/420`）。2D の painter 順安定性は CanvasView 側で明示安定ソートする。
- `ProceduralMeshGenerator`（`ProceduralMeshGenerator.h`）は `GenerateUVSphere`（`:34`）/`GeneratePlane`（`:158`）のみ。F3 で共有 quad mesh を使う場合は `ProceduralMeshGenerator` の広い拡張ではなく、allocator-backed の narrow API（例: `MeshResources::CreateProcedural`）で 2 三角 quad を生成・登録する。実装レビューで shader-generated quad を選ぶ余地は残すが、既定は hardcoded handle collision を避ける narrow mesh resource path とする。
- **共有 quad の winding（実装後・F3）:** Vulkan のフロントフェイス規約（カメラから見て CCW＝反時計回りを表面）に合わせる。インデックスは 2 三角形で `0-1-2, 1-3-2`（頂点 0=左下,1=右下,2=左上,3=右上 を CCW で構成）。`GeneratePlane` の既存 winding を F0 で実測し、不整合があれば quad 側を Vulkan 規約に正準化する（深度なし 2D では裏表両面でも可だが、3D world-space Billboard と共用するため規約を統一する）。

---

## 3. 目標アーキテクチャ

### 3.1 配線図（テキスト全体図）

> **【未実装の明示】** 本図は **実装後の確定アーキテクチャ**を示す。F2 landed 後の現状では `CanvasView`/`CompositePass`/`Canvas.Color`/`Composite.Color`/2 段化 `RenderFrameExecutor`、カメラ表、`Viewport.CameraId`、CanvasView full-rect 正射影 Viewport は既存の基盤である。`BoardComponent`/`BuildBoardProxy`/`BoardProxy`/ScreenSpace Board sink/Canvas Board store/Board draw command 生成は F3 以降の実装対象であり、図中で既存（`MeshProxy`/`LightProxy`/`SyncEntityRecursive`/`GenerateDrawCommands`/`PresentationPass`）の隣に並べているのは、**実装後の最終形**を一望するためである。現状（§2）では `BoardComponent` 分岐は存在しない。
>
> **【ルーティング決定点の確定（全レンズ CRITICAL の裁定）】** `World::SyncToSceneView`/`SyncEntityRecursive` は `RenderingCoordinator` を参照せず（`World.cpp:834` のシグネチャは `(const Rendering::MaterialResources* materials)` のみ、camera 表に到達できない）、`viewport.Camera.CullingMask` を読む手段を持たない。よって **ルーティングは 2 ステップに分割**する（確定）:
> - **Step1（`World::SyncEntityRecursive`、GameThread）:** `BoardComponent` を proxy 化するのみ。`BuildBoardProxy` で `LayerMask`/`Space`/`ObjectId`/`ComponentId` を埋め、**カメラ非依存**で `BoardProxy` を生成。F3 では `Space==ScreenSpace` のみを borrowed `Rendering::IBoardProxySink`（または同等）へ `ComponentId` 主キーで publish する。`Space==WorldSpace` は F9 まで Canvas 側 stale removal の対象として削除だけ行い、SceneView Board store は作らない。live-set 登録・stale 回収もここ。
> - **Step2（`RenderingCoordinator::GenerateDrawCommands`、GameThread）:** View ループ（`:804`）で当該 View の Viewport の `Camera.CullingMask` を読み、各 `BoardProxy.LayerMask` と `HasFlag` 交差判定（§3.5）。**交差した Board のみ** `CanvasView::PrepareBoardDrawCommands(viewportPlan)` で当該 viewport の DrawCommand/InstanceData へ append。交差しない Board は store に残るが描画しない（live-set からは外さない＝stale 回収対象外）。
>
> この分割により World は `RenderingCoordinator` へ依存せず（依存方向を逆流させない）、camera を所有する `RenderingCoordinator` が per-viewport 時にマスク交差を実行する。**「`SyncEntityRecursive` 内で `viewport.Camera` を読む」案は採用しない。**

```text
[GameThread]
  World::SyncToSceneView(materials)                         （World.cpp:834）
    UpdateWorldTransforms()                                  （:850, top-down）
    for root in GetRootEntities():
      SyncEntityRecursive(entity, materials, liveSets, cdr)  （:921）
        for comp in entity.GetComponents():
          CastTo<MeshComponent>   → BuildMeshProxy   → MeshProxy   → m_SceneView->UpdateMeshProxy
          CastTo<LightComponent>  → BuildLightProxy  → LightProxy  → m_SceneView->UpdateLightProxy
          CastTo<BoardComponent>  → BuildBoardProxy  → BoardProxy （新規, カメラ非依存）
            BoardProxy.LayerMask（BoardComponent.RenderLayerProp 由来）
            BoardProxy.Space（WorldSpace / ScreenSpace）
            ─ Step1: Space だけで publish/削除（GameThread, カメラ非依存）─
              Space==ScreenSpace → borrowed IBoardProxySink へ UpdateBoardProxy(ComponentId, proxy)
              Space==WorldSpace  → F9 まで Canvas sink から RemoveBoardProxy(ComponentId) のみ
              （CanvasView 未登録時は ScreenSpace Board を live-set に載せるが描画 store へ publish しない）
              キー: ComponentId primary、ObjectId は payload、live-set 登録
      RemoveStaleBoardProxies(liveBoardComponentIds)   ← BoardProxy も ComponentId live-set 差分回収    （:870-872 に追加）

  RenderingCoordinator::GenerateDrawCommands()              （RenderingCoordinator.cpp:802）
    カメラ表 lookup: Viewport.CameraId → CameraProxy（共有カメラ、§3.4）
    for view in m_Screen.GetViews():                        （:804）
      DynamicPointerCast<SceneView>  → 既存 3D 経路（:872-900、不変）
      else if DynamicPointerCast<CanvasView> → CanvasView::PrepareBoardDrawCommands(viewportPlan)   （新規分岐）
        ─ Step2: マスク交差で可視判定（GameThread, ここで初めて Camera を読む）─
          for board in m_BoardProxies:
            if HasFlag(viewportPlan.Camera.CullingMask, board.LayerMask): 可視集合へ
        可視集合を ComputeSortKey で明示安定ソート（std::stable_sort、§3.7）
        → quad draw/instance を FramePacket.DrawCommands/InstanceData へ append（3D の後、EndFrame 前）
        → viewportPlan.DrawCommandRange を Board 範囲に設定
      packet->Views.push_back(viewPlan)                     （:907）
    Views を Priority 昇順に安定化（SceneView 先・CanvasView 後、§3.6.6）

============ スレッド境界（FramePacket State Atomic）============

[RenderThread]
  RenderFrameExecutor::Execute(request)                     （RenderFrameExecutor.cpp:11、2 段化）
   ※ シグネチャは Execute(const RenderFrameExecutionRequest&) const。request.Packet/Views/Context/...
    if (bUseCompositePass):   ← packet ViewId から enabled CanvasView を解決できた時だけ true（§3.6.5）
      段1: 各 View を Priority 昇順で自前の中間 RT（View::m_OutputTexture）へ描く（present しない）
        各 View へ context.PresentationGraphPass=nullptr を渡す（基底/CanvasView とも present 抑止）
        SceneView::Render(context)  → 既存チェーンで描き ToneMappedColor 相当を View::m_OutputTexture へ store
        CanvasView::Render(context) → context.CurrentDrawCommands / packet range のみを消費して CanvasView::m_OutputTexture へ store（基底完全オーバーライド）
        ※ 各 View の RenderGraph は View ごとに Reset（View.cpp:195）。named resource は View 間で共有不可。
          段2 は named resource ではなく **各 View が persist した m_OutputTexture（物理 RT ハンドル）** を import する。
      段2: フレームに 1 回（View ループ外、独立した stage2 RenderGraph）:
        CompositePass: ImportTexture(SceneView.m_OutputTexture) を下地、CanvasView.m_OutputTexture を上、
                       alpha-over 合成 → Composite.Color を常に publish（Canvas 無し時は下地を素通し publish）
        PresentationPass: 優先順 Composite.Color → PresentationColor → ToneMappedColor で backbuffer へ 1 回 blit
    else (フォールバック = enabled CanvasView 未解決):
      従来の per-view 二重ループ（:29-75）でそのまま per-viewport present（段2 を構築しない）
```

### 3.2 BoardComponent（Entity の Inner・transform は Entity 由来）

`BoardComponent : Component`（`MeshComponent` と同型）。`REFLECTION_CLASS(BoardComponent, Component)` + `PROPERTY` で prefab シリアライズ対応。transform は **owner Entity の `GetWorldTransform()`** から取得（`MeshComponent` が `m_WorldTransform` を `RefreshRenderTransformCache` で Entity から取り込むのと同じ）。

```cpp
// Library/Core/Public/Component/BoardComponent.h（新規、UTF-8+BOM+CRLF）
class BoardComponent : public Component
{
    REFLECTION_CLASS(BoardComponent, Component)

public:
    BoardComponent();
    explicit BoardComponent(const FieldInitializer* initializer); // prefab デシリアライズ
    explicit BoardComponent(const IUnknown* sourceObject);        // クローン

    // BuildMeshProxy（MeshComponent.cpp:186）と同契約。dirty/transform-version 経路に乗る。
    bool BuildBoardProxy(Rendering::BoardProxy& outProxy,
                         const Rendering::MaterialResources* materials) const;

    void RefreshRenderTransformCache(); // owner Entity の world transform をキャッシュ

protected:
    // prefab シリアライズ対象（PROPERTY）。
    PROPERTY(Rendering::TextureHandle, Texture)
    PROPERTY(Rendering::BoardSpace, Space)          // WorldSpace / ScreenSpace
    PROPERTY(Rendering::RenderLayer, RenderLayerProp)
    PROPERTY(uint32_t, LayerPriority)               // レイヤー間順序（§3.7）
    PROPERTY(uint32_t, OrderInLayer)                // レイヤー内順序（§3.7）
    PROPERTY(Math::Vector2, SizePx)                 // ScreenSpace の px サイズ（F5 で拡張）
    PROPERTY(Rendering::BlendMode, BlendModeProp)   // Translucent/Additive/Opaque（F5）
    PROPERTY(Math::Vector4, Tint)                   // F5
    // F6 で UVRectProp（スプライトシート UV）を追加。
    // ... フェーズ進行で PROPERTY を加算（フリップ/ピボット/フリップブック等）

private:
    Math::Matrix4x4 m_WorldTransform; // Entity 由来キャッシュ（MeshComponent.h:287 相当）
};
```

**`BuildBoardProxy` シグネチャ（`MeshComponent::BuildMeshProxy` と同契約・確定）:** `MeshComponent::BuildMeshProxy(Rendering::MeshProxy& outProxy, const Rendering::MaterialResources* materials) const`（`MeshComponent.cpp:186`、bool 返却・`outProxy` 参照・`const MaterialResources*`）と**完全に同一の契約**にする。`materials` 引数は Board が単一テクスチャでマテリアル解決を要さないため**未使用でも署名を揃える**（`SyncEntityRecursive` の呼び出し側を統一し、将来 Board がマテリアル化した場合の互換を保つ）。F3 ではテクスチャ無しでも visible solid/default white Board を描く最小契約とし、`Texture` 未設定だけで `false` を返さない。`false` は owner 不正・明示非可視など proxy を作れない場合に限定する。テクスチャ解決・invalid texture の厳密化は F5/F6 以降で扱う。

**`RefreshRenderTransformCache()` の実装（`MeshComponent` 準拠・確定）:** owner Entity の `GetWorldTransform()` を呼び、結果を `m_WorldTransform` に**値コピー**する（`MeshComponent` が `World.cpp:966` 経由で同様にキャッシュ更新するのと同型。entity transform への参照保持はしない＝GameThread フレーム末で安定値を確保）。dirty 判定は `SyncEntityRecursive` 側で `IsRenderStateDirty()` または `GetLastSyncedTransformVersion() != ownerVersion`（`World.cpp:948-949`）に乗せ、dirty 時のみ `RefreshRenderTransformCache` → `BuildBoardProxy` を回す。`ScreenSpace` board では `m_WorldTransform` の並進成分を **px 座標**として解釈する（§3.8 で座標系を確定）。この fetch-and-cache は `ComponentDataRegistry` の async 走査でも安全（GameThread が proxy 化前にキャッシュを populate 済み）。

CMake: `Private/Component/BoardComponent.cpp` を `PRIVATE_SOURCES`（`:116` 付近の MeshComponent 隣）、`Public/Component/BoardComponent.h` を `PUBLIC_HEADERS`（`:324` 付近）へ同フェーズで登録。

**`ComponentDataRegistry` との関係:** `BoardComponent` は既定では `ComponentDataRegistry`（既定 OFF の密配列副 publish 経路）に**登録しない**（`MeshComponent` 同様、proxy 経路が一次・registry は optional 副 publish）。`RegisterComponentType<BoardComponent>()` は本計画スコープ外（将来、密走査が要るほど Board 数が増えた段階で検討）。`SyncEntityRecursive` の `BoardComponent` 分岐は registry 登録を呼ばず、proxy ストアへの publish のみ行う。

### 3.3 BoardProxy（MeshProxy と同型・ComponentId primary キーイング）

```cpp
// Library/Core/Public/Rendering/SceneProxy.h に追加
struct BoardProxy
{
    uint64_t ObjectId = 0;     // 所属 Entity の ObjectId（MeshProxy:38 と同様）
    uint64_t ComponentId = 0;  // BoardComponent の ComponentId（:39 と同様）

    Math::Matrix4x4 WorldTransform; // ScreenSpace は px 空間、WorldSpace は 3D world

    TextureHandle Texture;          // 参照カウント Resource。F3 は未設定なら default white/solid として扱う。
    BoardSpace Space = BoardSpace::ScreenSpace;
    RenderLayer LayerMask = RenderLayer::UI; // ルーティング元（CameraProxy.CullingMask:280 と交差）

    uint32_t LayerPriority = 0;     // レイヤー間（§3.7、uint32）
    uint32_t OrderInLayer = 0;      // レイヤー内（§3.7、uint32）

    BlendMode BlendModeProp = BlendMode::Translucent;
    float Tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    BoundingSphere WorldBounds; // WorldSpace board（F9 Billboard）の錐台カリング専用。
                                // ScreenSpace（F3-F8）では常に未使用＝BoundingSphere{} 既定のまま触らない。

    bool bVisible = true;

    bool IsValid() const { return bVisible; }
    uint64_t ComputeSortKey() const // (LayerPriority<<32) | OrderInLayer（§3.7）
    {
        // uint32 上限の前提を固定（符号拡張・桁あふれ防止）。
        // LayerPriority/OrderInLayer は uint32_t なので shift 前に必ず uint64_t へ拡張。
        return (static_cast<uint64_t>(LayerPriority) << 32) | static_cast<uint64_t>(OrderInLayer);
    }
};
```

`enum class BoardSpace : uint8_t { ScreenSpace = 0, WorldSpace }` を `RenderTypes.h` に追加。`BlendMode` は既存（**`MaterialTypes.h:198`**、`RenderTypes.h` ではない点に注意）を再利用。**`UVRect` は F3 では定義しない。** F3 の quad は full [0,1] UV を固定で使うか、solid-color/default white shader path で UV を要求しない。スプライトシート用 `UVRect` と `BoardProxy.UV` は F6 で追加する。

**F3 スコープと WorldBounds（レビュー major 反映）:** F3 は **ScreenSpace 最小スコープ**で、`WorldBounds` は **F9 Billboard まで一切使わない**。F3 実装時は `WorldBounds` を `BoundingSphere{}` の既定値のまま放置し、`BuildBoardProxy`（ScreenSpace 経路）で**設定しない**。錐台カリング（`WorldBounds` 使用）は F9 で `BillboardComponent::BuildBoardProxy` が WorldSpace 時のみ設定する。フィールド自体は基底 `BoardProxy` に持たせるが、ScreenSpace では「無効・未使用」と明記して読み手の混乱を避ける（構造体分割はしない＝Billboard が基底ストアを共用するため）。

**GPU instance data の扱い（レビュー major 反映・F3 で確定）:** `BoardProxy` は **host-side proxy**（GPU へ送らない、`MeshProxy`/`LightProxy` と同じホスト proxy パターン）。GPU へ渡る per-instance データは `FramePacket.InstanceData`（既存 flat array）へ append する。F3 は **既存 `GPUSceneInstanceData` レイアウトを流用**するか、専用 `Board2DInstanceData{ float WorldTransform[16]; float Tint[4]; }` を新設するかを F0 で既存 `InstanceData` レイアウトを実コード確認のうえ F3 で確定する（GLSL std140/std430 と C++ `alignas(16)` を一致させレイアウトテストで固定）。UV 矩形フィールドは F6 で追加する。`SortKey`/`LayerPriority` は **ホスト側のソート専用**で GPU には送らない（append 順＝描画順がそのまま painter 順）。

### 3.4 共有カメラ（CameraId 参照・スナップショット値コピー維持）

`CameraProxy::CameraId`（`SceneProxy.h:259`、F2 landed で有効化済み）と `RenderingCoordinator` の小さなカメラ表を使う。`Viewport` は `CameraProxy` を値所有し続ける**が**、authoring の正準は CameraId 経由の lookup とする（複数 Viewport が同一カメラを共有可能にする最小機構）。

```cpp
// RenderingCoordinator.h（新サブシステムを作らずメンバ追加。シングルトン禁止）
class RenderingCoordinator
{
    // 既存: CameraProxy m_MainCamera;（:354）/ GetMainCamera()（:224）/ SetMainCamera()（:219）
public:
    uint64_t RegisterCamera(const CameraProxy& camera);          // CameraId 採番（1 始まり、0=invalid）
    bool UpdateCamera(uint64_t cameraId, const CameraProxy& camera);
    const CameraProxy* FindCamera(uint64_t cameraId) const;      // CanvasView/2D から参照
    // SetMainCamera は m_MainCameraId を再利用して overwrite（Game 側は ID を意識しない後方互換）

private:
    Container::UnorderedMap<uint64_t, CameraProxy> m_Cameras;    // GameThread 専有
    uint64_t m_NextCameraId = 1;
    uint64_t m_MainCameraId = 0;
    uint64_t m_CanvasCameraId = 0;
};
```

**スナップショット契約は不変。** `BuildViewportRenderPlan`（`RenderingCoordinator.cpp:40-115`）は Viewport の CameraId からカメラ表を lookup（無ければ従来どおり `m_MainCamera` フォールバック `:863-864`）し、解決した `CameraProxy` を **値で** `ViewportRenderPlan.Camera`（`ViewRenderPlan.h:32`）へコピーする。RenderThread はポインタ追跡せず値コピーのみ読む（不変スナップショット原則を維持）。`FramePacket.Scene.MainCamera` も従来どおり値。カメラ表は GameThread 専有で RenderThread から触らない。

`BuildViewportRenderPlan` は引き続き `RenderingCoordinator.cpp` 内の private/file-local helper とし、公開 API に昇格しない。CameraId 解決は file-local helper へ resolver（lambda/関数オブジェクト）を渡す、または `RenderingCoordinator` private helper で解決済み `CameraProxy` を渡す形に限定する。テスト容易性のためにこの snapshot helper を public 化せず、テストは CameraId lookup の狭い seam（resolver/private helper の単体）または `GenerateDrawCommands`/FramePacket の公開挙動で検証する。

新たな live Camera クラスは作らない（`CameraProxy` がカメラ実体）。CanvasView の正射影 Viewport は `RegisterCamera({Projection=Orthographic, CullingMask=RenderLayer::UI, OrthoWidth=renderWidth, OrthoHeight=renderHeight, ...})` の CameraId を参照する。

#### 3.4.1 カメラ表の詳細設計（採番・寿命・所有・lookup・後方互換）

全レンズが「採番方針・寿命・SetMainCamera との関係・lookup 失敗・Viewport 連携が未確定」と指摘したため確定する:

- **CameraId 採番:** `m_NextCameraId` の**単調増加（auto-increment）**。`0 = invalid`、`1` 始まり。`RegisterCamera` は常に新 ID を返し、同一 `CameraProxy` を再 Register しても **ID は重複しない**（hash/generation は使わない＝単純で衝突なし）。**ID の再利用は F2 では非サポート**（削除しないため枯渇しない。`uint64_t` 単調増加で実用上無限）。将来明示ハンドルが要れば generation 付きへ拡張。
- **source-of-truth:** `RenderingCoordinator::m_Cameras` が **GameThread 専有の一次情報**。RenderThread はカメラ表を lookup しない（不変スナップショット原則）。`FramePacket.Scene.MainCamera` と `ViewportRenderPlan.Camera` は **値スナップショット**（RenderThread はこの値コピーのみ読む）。両者が併存しても矛盾しない（表＝authoring 一次、スナップショット＝frame 値）。
- **`SetMainCamera(proxy)` の後方互換フロー:** `m_MainCameraId == 0`（未設定）なら `m_MainCameraId = RegisterCamera(proxy)`、設定済みなら `UpdateCamera(m_MainCameraId, proxy)` で**上書き**（既存 ID を再利用、Game 側は ID を意識しない）。あわせて従来どおり `m_MainCamera` へ値コピー + 主 Viewport へ `SetCamera` コピーも維持（既存呼び出し側を壊さない）。
- **lookup 失敗フォールバック:** `FindCamera(id)` はミス時 `nullptr`。`BuildViewportRenderPlan` は CameraId lookup 成功時その値を、失敗時は従来の `m_MainCamera`（`:863-864` の fallback）を `ViewportRenderPlan.Camera` へ値コピーする。これにより CameraId 未設定の既存 Viewport は完全に従来挙動。
- **削除セマンティクス:** F2 では `UnregisterCamera` を**実装しない**（カメラ数は少数で寿命はアプリ寿命と同等、リーク懸念なし）。将来必要なら、削除時に当該 ID を参照する Viewport は lookup 失敗 → `m_MainCamera` フォールバックへ落ちる設計とする（dangling にしない）。
- **`Viewport` の CameraId 連携（値所有は不変）:** `Viewport::m_Camera`（`Viewport.h:197`）の**型・値所有・コピー操作は一切変えない**（`SetCamera(const CameraProxy&)` `Viewport.h:82`・`GetCamera()` `:87` の値契約を維持）。新規に **`Viewport::SetCameraId(uint64_t)`/`GetCameraId()`**（`m_CameraId` フィールド追加）を**併設**する。`m_Camera`（値）が **primary**、CameraId 参照は **auxiliary**（`BuildViewportRenderPlan` の lookup 時のみ使用）。`SetCamera(proxy)` は `proxy.CameraId` を `m_CameraId` へ mirror し、値 camera を `m_Camera` へコピーするだけに限定する。`Viewport` は `RenderingCoordinator`/カメラ表へアクセスできないため、`SetCamera` 内で `RegisterCamera` してはならない。
- **CanvasView 生成と RegisterCamera の呼び出し主体（F2）:** `CanvasView::Initialize` は Canvas の不変条件として full-rect Viewport を**ちょうど 1 つ**作る（rect=(0,0,1,1)、enabled）。ただし `CanvasView` 自身はカメラ表を所有/参照しない。`RenderingCoordinator::CreateCanvasView()` が CanvasView 生成後に `RegisterCamera({Projection=Orthographic, CullingMask=RenderLayer::UI, OrthoWidth=m_RenderWidth, OrthoHeight=m_RenderHeight, Near=0, Far=1})` を呼び、返り CameraId を `m_CanvasCameraId` として保持し、その Canvas viewport に `SetCameraId(cameraId)` と値 `SetCamera(camera)` を適用する。`renderWidth/renderHeight` は `RenderingCoordinator` が既知（`m_RenderWidth/m_RenderHeight`）。`IsCanvasViewEnabled()`/`GetCanvasView()` も併設し、ルーティング Step1 で CanvasView 登録有無を確認できるようにする。生成は **pre-frame 専用**で、GameThread 上の coordinator / RenderWorld 初期化後・最初の `BeginFrame` 前に限る。`m_bFrameSubmissionStarted` と、利用可能なら FramePacket state（Writing/Queued/Reading 等でないこと）で enforce し、フレーム提出開始後の追加登録は assert/false で拒否する。
- **Canvas camera resize ownership:** `RenderingCoordinator` owned helper（例: `UpdateCanvasCameraForRenderResolution()`）を作り、`m_CanvasCameraId` の表エントリと Canvas viewport の値 camera を `m_RenderWidth/m_RenderHeight` から更新する。呼び出し元は `UpdateRenderResolution` を通る全経路（`Resize`、`SetRenderScale`、swapchain acquire 時の resize、presentation-dirty resize handling）であり、どの resize path でも Canvas camera の `OrthoWidth/OrthoHeight` が古いまま残らないようにする。
- **カメラ cleanup:** `DestroyView` が `CanvasView` を破棄する場合は `m_CanvasCameraId = 0` に戻す。`ReleaseInitializedResources` は `m_Cameras.clear()`、`m_MainCameraId = 0`、`m_CanvasCameraId = 0`、`m_NextCameraId = 1` を実行し、次回初期化で stale CameraId を再利用しない。

### 3.5 RenderLayer マスク × CullingMask ルーティング（方式 A 確定・2 ステップ分割）

**ルーティング決定点の確定（全レンズ CRITICAL の裁定・§3.1 と一致）:** `World::SyncToSceneView`/`SyncEntityRecursive` は `RenderingCoordinator`（カメラ表）を参照できない（`World.cpp:834` のシグネチャは materials のみ）。よって**マスク × CullingMask 交差は `SyncEntityRecursive` 内では行わない**。判定を以下 2 ステップに分割する（確定。「`SyncEntityRecursive` で `viewport.Camera` を読む」案は不採用）:

#### Step1: proxy 生成と Space 振り分け（`World::SyncEntityRecursive`、GameThread・カメラ非依存）

`SyncEntityRecursive`（`World.cpp:921-1062`）の `CastTo<BoardComponent>` 分岐で `BuildBoardProxy` を呼び `BoardProxy` を生成し、**`Space` だけ**で publish/削除する（カメラ・マスクは見ない）:
- `Space == ScreenSpace` → borrowed `Rendering::IBoardProxySink`（または同等）へ `UpdateBoardProxy(ComponentId, proxy)`。
- `Space == WorldSpace`（F9） → F3 では Canvas sink から `RemoveBoardProxy(ComponentId)` して ScreenSpace から WorldSpace へ変わった stale proxy を消す。SceneView 側 Board store は F9 で追加する。
- live-set 登録（主キーは `liveBoardComponentIds`。`ObjectId` は payload と補助診断用）。
- **CanvasView 未登録時のフォールバック:** ScreenSpace Board は **stale 回収から漏らさず live-set には載せるが、CanvasView store が無いので proxy を捨てる（silent drop、assert しない）**。F1/F2 のフォールバックで present は不変。

**`SyncEntityRecursive` が CanvasView store へ到達する手段（依存方向の裁定）:** F3 では narrow `Rendering::IBoardProxySink`（または同等）を導入し、`World::SetScreenSpaceBoardSink(Rendering::IBoardProxySink* sink)` で **borrowed pointer** として World に渡す。World は `CanvasView`/`RenderingCoordinator` を include/use せず、sink interface だけを知る。sink methods は **ComponentId keyed** に限定する:

```cpp
class IBoardProxySink
{
public:
    virtual bool UpdateBoardProxy(uint64_t componentId, const BoardProxy& proxy) = 0;
    virtual void RemoveBoardProxy(uint64_t componentId) = 0;
    virtual void RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t>& liveComponentIds) = 0;
};
```

`CanvasView` がこの sink を実装し、`RenderingCoordinator`/`RenderWorld` が Canvas create 後・最初の sync 前に `World::SetScreenSpaceBoardSink(canvasView)` を呼ぶ。sink が変わった場合、World は `MarkRenderStateDirty` 相当で BoardComponent の render state を dirty にし、次回 sync で新 sink へ再 publish する。CanvasView destroy/recreate 時は dangling pointer を避けるため、破棄前に必ず `SetScreenSpaceBoardSink(nullptr)` で clear し、旧 Canvas store は `RemoveStaleBoardProxies` ではなく所有者破棄で捨てる。新 Canvas 作成後は sink 再設定 → dirty → 次 sync で再 publish する。

#### Step2: マスク交差による可視判定（`RenderingCoordinator::GenerateDrawCommands`、GameThread）

`GenerateDrawCommands`（`:804` View ループ）の **CanvasView 分岐**（`:818` の SceneView cast に `else if (DynamicPointerCast<CanvasView>)` を並置）で、GameThread 上で `CanvasView::PrepareBoardDrawCommands(viewportPlan)` を呼ぶ。CanvasView は当該 Viewport の `Camera.CullingMask`（`SceneProxy.h:280`）と各 `BoardProxy.LayerMask` の `HasFlag` 交差（`RenderTypes.h:214`）を取り、**交差した Board のみ**を可視集合として `FramePacket.DrawCommands`/`FramePacket.InstanceData` へ append する。交差しない Board は store に残るが当該 viewport では描画されない（live-set からは外さない）。**ここで初めてカメラを読む**（camera snapshot を持つ `ViewportRenderPlan` のスコープ内）。この処理は `RenderingCoordinator::GenerateDrawCommands` 中、`EndFrame` 前にだけ走り、RenderThread では走らない。

#### 二重描画防止（調査の CRITICAL）

SceneView は従来どおり `MeshProxy` を無条件収集する。`BoardProxy` は **SceneView の `MeshProxy` 経路には絶対に載せない**（F3 ScreenSpace は CanvasView の `m_BoardProxies`、F9 WorldSpace は SceneView の**別ストア** `m_BoardProxies`）。同一 ComponentId の Board が ScreenSpace/WorldSpace 両方へ入ることは無い（Space は単一属性）。`RemoveStaleBoardProxies` は ScreenSpace=CanvasView 側・WorldSpace=SceneView 側それぞれが **ComponentId primary** で独立に回収する。`ObjectId` は proxy payload として残すが store index / stale live-set / subtree removal / tests は ComponentId を主キーにする。

#### 段階導入（確定）

F3 では CanvasView 1 つ + 主 SceneView 1 つの **2 View 固定**でルーティングを配線し、`MeshProxy → SceneView` は不変のまま `BoardProxy → CanvasView` を並走させる。`World::SyncToSceneView` は引き続き `m_SceneView` を主対象とし（World.h:253 の単一 SceneView 所有は不変）、CanvasView への ScreenSpace publish は上記注入 sink 経由のみ。一般化（任意 View 数・任意マスク・`World::GetViews()` 列挙）は **Future Goal（§4）** であり本計画スコープ外。

### 3.6 CanvasView と合成（旧版設計の移植 + LayerView→CanvasView 改名）

合成側は旧版が概ね正しいため踏襲し、名称のみ `LayerView → CanvasView` に改める。

#### 3.6.1 CanvasView::Render の契約（PresentationGraphPass 抑止）

**実装方式の確定（レビュー major の裁定）— 方式 (a) 完全オーバーライドを採用、(b) は不採用:**

- **採用 (a)：完全オーバーライド。** `CanvasView::Render(ViewRenderContext&)` は `View::Render`（`View.cpp:173`）を**オーバーライドし基底を一切呼ばない**。内部で `context.Graph->Reset()`（`View.cpp:195` 相当を自前で実行）→ `context.CurrentDrawCommands` / packet range だけを読む 2D Board 描画パス（深度なし quad painter、`BlendMode` ごとに blend state、`PolygonMode::Fill`）を `AddPass` → `Compile`/`ExecuteWithResult` し、出力を `CanvasView::m_OutputTexture`（物理 RT）へ store し **`Canvas.Color`** 名でも publish する。Board 0 個でも RenderGraph パスを実行し、透明 clear/store 済みの物理 `Canvas.Color` を確定する（宣言だけでは不可。null-pipeline の fullscreen/render-pass command などで `BeginRenderPass`/`EndRenderPass` の clear/store を実際に発行）。`context.PresentationGraphPass` は**末尾に追加しない**（= 2D View は直接 present しない。段2 が present を一括で行う）。RenderThread 側の `CanvasView::Render` は mutable な Canvas board store / `BoardProxy` を列挙しない。
- **不採用 (b)：基底を呼んで `PresentationGraphPass=nullptr` を渡す案。** 基底 `View::Render` は `m_Passes` ベースの汎用パスチェーン構築（`:197-279`）を前提とし、2D の安定ソート済み Board painter 経路と噛み合わない。完全オーバーライドの方が制御が明快なため (b) は採らない。

**基底 `View` メンバの side-effect 回避:** 完全オーバーライドでも CanvasView は基底 `View` のメンバ（`m_Viewports`/`m_OutputTexture`/`m_bEnabled`/`m_bInitialized`）を**そのまま利用**する（`AddViewport`/`GetViewport` で正射影 Viewport を持つ）。基底を呼ばないことで触らないのは `m_Passes`/`m_PostProcessStack` ベースの汎用チェーン（`:197-279`）と `PresentationGraphPass` 条件追加（`:281-284`）のみ。`m_Passes` は CanvasView では空（Board 描画は CanvasView 自前パスで構築）とし、F0 で `View.h` の全メンバを列挙して override 時の未初期化/二重実行が無いことを確認・記録する。

#### 3.6.2 段1 / 段2 のテクスチャフロー

**重要（全レンズ CRITICAL の裁定）— RenderGraph は View ごとに `Reset()`（`View.cpp:195`）されるため、段1 各 View の named resource（`ToneMappedColor`/`Canvas.Color`）は段2 からは読めない。** 段2 CompositePass は **named resource ではなく、各 View が persist している物理 RT（`View::m_OutputTexture`）を `ImportTexture` で取り込む**。`Canvas.Color`/`Composite.Color` の named resource は**段2 の独立 RenderGraph 内でのみ**有効で、段1 の View graph とは共有しない。

```text
段1（各 View を自前 RenderGraph で中間 RT へ・present しない。各 View で graph.Reset()）
  SceneView::Render(context, PresentationGraphPass=nullptr)
    → 既存 Deferred/Forward/ToneMap チェーンで描画（自 View graph 内の ToneMappedColor）
    → 完了後 SceneView::m_OutputTexture（物理 RT）に結果が確定（段2 が import するハンドル）
  CanvasView::Render(context)  ※ Board 0 個でも空（透明クリア）の m_OutputTexture を確定
    → CanvasView::m_OutputTexture（物理 RT）に Board painter 結果が確定

============ 段1 完了後（全 View の m_OutputTexture が確定）============

段2（フレームに 1 回・View ループ外・段2 専用 RenderGraph を 1 つ構築）
  CompositePass:
    ImportTexture(SceneView::m_OutputTexture)   → 下地（scene、物理 RT ハンドル）
    TryImport(CanvasView::m_OutputTexture)       → 上（canvas、物理 RT ハンドル）
      CanvasView 有り → alpha-over 合成（下地=scene, 上=canvas）→ Composite.Color を publish
      CanvasView 無し → scene をそのまま Composite.Color へコピー publish（後方互換）
  PresentationPass:
    TryRead 優先順 Composite.Color → PresentationColor → ToneMappedColor → backbuffer へ 1 回 blit
```

> 「SceneColor」という呼称は段1 SceneView の最終カラー（既存 3D 経路では `ToneMappedColor` 相当）を指す**段2 内の入力名**であり、段1 の named resource `Scene.Color`（`RenderGraphResourceNames.h:15`、G-buffer ライティング後の中間）とは別物。混同を避けるため段2 では物理 RT ハンドルで受け、段2 graph 内で `Composite.Color` のみを named resource として publish する。段1 のどの中間（`ToneMappedColor` か別か）を `m_OutputTexture` へ store するかは F1 で実コード確認し `2d-rendering-baseline.md` に確定値を記録する。

#### 3.6.3 CompositePass（新規 IRenderGraphPass）

- 新規 `Library/Core/Public/Rendering/CompositePass.h` / `Private/Rendering/CompositePass.cpp`。合成シェーダー `Assets/Shaders/composite2d.vert`/`composite2d.frag`（UTF-8+CRLF, **BOM なし**）。
- **`Composite.Color` を常に publish**（条件付き publish にしない）。`CanvasView::m_OutputTexture` が無いフレームでも下地（scene RT）をそのまま `Composite.Color` へコピーして publish し、PresentationPass のフォールバックを破綻させない。
- **F3 で実 alpha-over を完成させる。** F2 landed 時点の `CompositePass` は Canvas RT を import するが Scene を素通しするため、visible Canvas は最終 `Composite.Color` に影響しない。F3 では `out.rgb = canvas.rgb * canvas.a + scene.rgb * (1 - canvas.a)` / `out.a = canvas.a + scene.a * (1 - canvas.a)` の straight alpha-over（color space は F1/F2 の既定に合わせる）へ切り替え、Board 1 枚の可視出力が `Composite.Color` と backbuffer に反映されることをテストで固定する。
- **「Canvas 出力なし」と「Canvas が黒」の区別（レビュー missing 反映）:** CompositePass は CanvasView の有無を**ハンドルの有効性**で判定する。`CanvasView::GetOutputTexture()` が有効ハンドルを返す（= CanvasView 登録済み・段1 で描いた）ときのみ canvas を import して合成する。CanvasView 未登録時は import 自体を行わず scene 素通し。**透明な Canvas（Board 0 個）は有効ハンドル + 全 α=0** なので合成しても結果は scene と一致（alpha-over で上が透明 → 下地そのまま）。アルゴリズム擬似コード:
  ```text
  CompositePass::Declare(builder):
    sceneRT = builder.ImportTexture(SceneOutputRT)            // 常に有効（必須）
    bool bHasCanvas = CanvasOutputRT.IsValid()
    if bHasCanvas: canvasRT = builder.ImportTexture(CanvasOutputRT)
    builder.DeclareTexture(Composite.Color, sceneFormat)       // 常に宣言
    // Execute: bHasCanvas ? alpha-over(scene, canvas) : copy(scene) → Composite.Color
  ```
- 入力 binding: scene texture（下地）+ （任意）canvas texture（オーバーレイ）。出力: `Composite.Color`（format/color space は 3D 下地＝tone-mapped sRGB と整合）。
- **premultiply / color space 確定（F1・レビュー minor 反映）:** 既定は **テクスチャ sRGB・straight alpha（非 premultiplied）**を仮定し `composite2d.frag` の blend 式は `out.rgb = ui.rgb*ui.a + scene.rgb*(1-ui.a)`。テクスチャが premultiplied なら `out.rgb = ui.rgb + scene.rgb*(1-ui.a)`。F1 で `TextureHandle` のメタデータ（sRGB/linear・premul/straight）を実確認し、`composite2d.frag` に固定する。tone-mapped 済み 3D（sRGB）と 2D sRGB テクスチャの color space を一致させる（不一致なら変換を入れる）。検証は `CompositeBlendEquationTest`（参照レンダリングと一致）で固定する。

#### 3.6.4 PresentationPass の入力拡張

`PresentationPass::Declare`（`PresentationPass.cpp:22-40`、現状 `PresentationColor → ToneMappedColor` の 2 段 `TryReadTexture` `:27-40`）の**先頭へ `Composite.Color` の `TryReadTexture` を追加**し、優先順を **(1) Composite.Color → (2) PresentationColor → (3) ToneMappedColor** にする。`Composite.Color` は CompositePass が常に publish するので段2 が走れば最優先で読まれる。両 edge case では従来の入力に落ちる。

#### 3.6.5 RenderFrameExecutor の 2 段化

**実シグネチャ（F0 確認済み）:** `RenderFrameExecutionResult RenderFrameExecutor::Execute(const RenderFrameExecutionRequest& request) const`（`RenderFrameExecutor.cpp:11`）。`request.Packet`/`Views`/`Context`/`Renderer`/`CommandList`/`PresentationGraphPass`/`Presentation` を持つ。現状の二重ループは `:29-75`（`request.Packet->Views` → `viewPlan.Viewports`）、per-viewport で `ApplyViewportRenderPlan`（`:55`）→ `ConfigurePresentationGraphPass`（`:56`）→ render → present、`bClearPresentation = result.PresentationBlitCount == 0`（`:54`）、`PresentationBlitCount`（`:70`）。

これを `bUseCompositePass` で分岐する 2 段へ**再構成**する（既存 helper `ApplyViewportRenderPlan`/`ConfigurePresentationGraphPass`/`RenderViewForCurrentViewport`/`FlushPendingFrameCommands` を再利用。CanvasView は full-rect Viewport を 1 つ持つ不変条件で扱う）:

```text
Execute(request):
  ResetFrameOutputs(request)                              // View output と段2 request/result を初期化
  canvasView = ResolveEnabledCanvasView(request.Packet)   // packet ViewId → 実 View object
  bUseCompositePass = (canvasView != nullptr)             // 後述の判定
  if !bUseCompositePass:
    // ── フォールバック = 従来経路（:29-75 をそのまま）──
    per-View → per-Viewport ループで ConfigurePresentationGraphPass→render→present（per-viewport present）
    return

  // ── 段1: 全 View を Priority 昇順で中間 RT へ。present しない ──
  sortedViews = stable_sort(request.Packet->Views, key=Priority)   // SceneView 先・CanvasView 後（§3.6.6）
  for viewPlan in sortedViews:
    for viewportPlan in viewPlan.Viewports (drawable):
      ApplyViewportRenderPlan(*context, &viewportPlan)
      ConfigurePresentationGraphPass(request, /*present=*/nullptr)  // 段1 は present 抑止
      RenderViewForCurrentViewport(request, view)                   // 自 View graph→m_OutputTexture
      FlushPendingFrameCommands(request)
    // ここで view->m_OutputTexture が確定

  // ── 段2: フレームに 1 回（View ループ外、段2 専用 graph）──
  stage2Graph.Reset()
  ApplyViewportRenderPlan(*context, primarySceneViewport ? primarySceneViewport : nullptr)
  CompositePass.SetInputs(SceneView->m_OutputTexture, CanvasView ? CanvasView->m_OutputTexture : invalid)
  stage2Graph.AddPass(CompositePass)        // → Composite.Color を publish
  stage2Graph.AddPass(PresentationPass)     // 優先順 Composite.Color→...→backbuffer、bClearPresentation=true
  stage2Graph.Compile(context); stage2Graph.Execute(context)
  ++result.PresentationBlitCount
  ClearStage2RequestsAndResults(request)
  return
```

**`ShouldCompose`（`bUseCompositePass`）判定の確定（レビュー CRITICAL の裁定）:** 合成は **enabled CanvasView を packet の ViewId から実 View object へ解決できた場合のみ**有効にする。`packet->Views.size() > 1` や `ViewType != Scene` だけでは合成しない。SceneView が 2 つあるだけ、または CanvasView 以外の非 Scene View があるだけの packet は従来/フォールバック経路へ残す。CanvasView が packet にあっても disabled なら false。

```text
ShouldCompose(packet):
  canvasView = ResolveEnabledCanvasViewFromViewId(packet)
  return canvasView != nullptr

正準ケース:
  1 SceneView                         → false
  2 SceneViews                        → false
  disabled CanvasView                 → false
  enabled CanvasView（full-rect viewport 1 個） → true
```

`bUseCompositePass=false` のとき現行 `:29-75` 経路を**完全に保つ**（2D 機能を一切使わないシーンは挙動ビット不変）。`bClearPresentation`/`PresentationBlitCount`（`:54, :70`）のセマンティクスは段2 で「1 回だけ present・clear」へ寄せる（段1 では present もカウントもしない）。enabled CanvasView があるが Board 0 なら、CanvasView::Render が透明 `Canvas.Color` を物理 clear/store し、CompositePass は alpha-over の結果 scene と一致する（完全不変は「CanvasView 未登録」の F1 で担保、§F1）。

**フレーム出力 lifetime:** Render 開始時と失敗時に各 View の frame output（`m_OutputTexture`/公開出力ハンドル相当）を reset し、前フレームの RT を import しない。段2の `CompositePass`/`PresentationPass` request/result は段2完了直後に clear し、`ReleaseInitializedResources` でも必ず破棄する。段2 viewport/scissor は primary SceneView viewport を使い、無ければ `nullptr` で full-output fallback とする。

#### 3.6.6 複数 View の優先度順処理

`packet->Views` は現状 `Priority=viewIndex`（`RenderingCoordinator.cpp:815`）で挿入順走査（`RenderFrameExecutor.cpp:29`、ソートなし）。2D を 3D の上に重ねるため **SceneView を先・CanvasView を後**に登録し、段1 走査は `ViewRenderPlan.Priority` 昇順を保証する（必要なら `GenerateDrawCommands` の `packet->Views` 構築時に `Priority` で安定ソート）。

### 3.7 順序担保（2 階層・明示安定ソート）

- **レイヤー間:** 各 Board の `LayerPriority` 昇順。方式 A では `packet.DrawCommands` への append を `LayerPriority` 順に行い painter 法で 1 枚の RT へ順に描く。
- **レイヤー内:** `OrderInLayer` 昇順で安定。`OrderInLayer` は `BoardComponent` の明示的な serialized `PROPERTY`（既定 0）として扱い、runtime で append 順を自動採番しない。
- **SortKey（ビット割付確定）:** `(static_cast<uint64_t>(LayerPriority) << 32) | static_cast<uint64_t>(OrderInLayer)`（`BoardProxy::ComputeSortKey`）。高位 32bit=`LayerPriority`、低位 32bit=`OrderInLayer`。`LayerPriority`/`OrderInLayer` は**ともに `uint32_t`**（符号拡張バグ防止、shift 前に必ず `uint64_t` へ拡張）。
- **上限 assert の位置（確定）:** `BoardComponent::SetLayerPriority`/`SetOrderInLayer` の setter と、`CanvasView::PrepareBoardDrawCommands` の `stable_sort` 直前で `assert(LayerPriority < (1ull<<32))` / `assert(OrderInLayer < (1ull<<32))` を実行（`uint32_t` なので常に成立するが、将来の型変更に対するガードとして明示）。
- **`OrderInLayer` 採番の所有者と寿命（確定）:** `BoardComponent.LayerPriority` / `BoardComponent.OrderInLayer` は prefab・authoring・game code から与える **明示的な serialized `PROPERTY`** とし、既定値はどちらも `0`。runtime / `CanvasView` は `BoardComponent` を mutate せず、`OrderInLayer` を auto-assign しない。非 default の順序が必要な場合だけ authoring 側が setter で明示設定する。`CanvasView` が所有するのは `ComponentId` keyed の**private / non-serialized な insertion sequence** のみで、Board 初回登録時に採番し、update では保持、explicit remove / stale / shutdown で erase する。serialized `OrderInLayer` は remove でも詰めず、global/static/constructor ベースの単調カウンタも導入しない。
- **SortKey 衝突と stable_sort 規約（確定）:** SortKey 一致は **同 `LayerPriority` かつ同 `OrderInLayer`** を意味する。`CanvasView::PrepareBoardDrawCommands` は `LayerMask x CullingMask` filter 後の visible Board を `Container::VariableArray` の一時リストへ集め、`std::stable_sort` で **`SortKey` 昇順 → Canvas insertion sequence 昇順** に並べる。`ComponentId` は insertion sequence の一意性が壊れた場合に限る defensive final fallback であり、通常の ordering contract ではない。`std::vector` 中継は導入しない。
- **明示安定ソート必須・実装責務:** 既存 `DrawCommandSorter` は `std::sort`（非安定、`DrawCommand.cpp:374/397/420`）で 2D 要件を満たさない。よって **BoardProxy は `CanvasView::PrepareBoardDrawCommands` 内で明示的に安定ソート**してから append し、3D 用の非安定 `DrawCommandSorter` 経路に**乗せない**（Board の DrawCommand 範囲は専用 range で管理し、`DrawCommandSorter` の対象から除外する＝range 単位の exclusion）。ソートは GameThread オーサリング時に確定、RenderThread は range を append 順に発行するだけ。

### 3.8 正射影カメラとスクリーン座標（ScreenSpace transform 解釈）

CanvasView の full-rect Viewport は `CameraProxy{ Projection = Orthographic, CullingMask = RenderLayer::UI }` を CameraId 共有で持つ。F2 では `Viewport::UpdateMatrices` と `CameraViewConstants::BuildProjectionMatrix` の Orthographic パスを揃え、`OrthoWidth` と `OrthoHeight` を**literal に尊重**して既存の centered `MatrixUtils::CreateOrthographic(width, height, near, far)` を呼ぶ。`OrthoHeight = OrthoWidth / aspectRatio` へ暗黙変換する旧挙動は F2 で解消する。

**座標系の正準仕様（レビュー CRITICAL の裁定。F0 で実コード照合し `board2d.vert` に固定）:**

- **(a) ScreenSpace `Board.WorldTransform` は px 絶対座標を encode する。** 並進成分（`m30, m31, m32`）= `(pixelX, pixelY, depthZ)`、スケール成分で px サイズ、回転成分で回転。Board 位置 `(100, 50, 0)` は左上原点なら画面 px `(100, 50)` に Board 基準点を置く。**Game 層が px で授ける**（`board.SetPosition(screenPx)` 等の API を Game 側責務とする）ため、`BoardComponent::RefreshRenderTransformCache` は Entity の `GetWorldTransform()` をそのまま px として扱い、**world→px の追加変換は行わない**（§3.2 と統一。ScreenSpace の Entity transform は px セマンティクスを持つ二重解釈ではなく、Game が px で設定した値がそのまま流れる）。
- **(b) F2 の正射影決定:** `MatrixUtils::CreateOrthographic(width, height, near, far)` は centered orthographic として使い、F2 は `CameraProxy::OrthoWidth`/`OrthoHeight` の値をそのまま `width`/`height` に渡すところまでを確定範囲にする。F2 では off-center overload を増やさず、左上原点・y 下・ピクセル原点合わせを投影行列だけで解決しない。
- **(c) MVP 計算（`board2d.vert`）:** `gl_Position = ProjectionMatrix * (WorldTransform * vec4(VertexPosition, 0, 1))`。ScreenSpace では View 行列は単位（カメラは px 空間そのもの）扱い。`WorldTransform` が px 配置・サイズ、`ProjectionMatrix` が px→clip 変換を担う。F0 で MVP の乗算順（行/列ベクトル規約）を既存 3D シェーダーと一致させる。
- **(d) F3-F8 はスクリーン全面 Canvas（Viewport rect=(0,0,1,1)）を前提。** 全面では `OrthoWidth = renderWidth`、`OrthoHeight = renderHeight` を camera に保持し、リサイズ時は coordinator helper が `UpdateCamera` と Canvas viewport の値 camera を追従（§3.4.1）。top-left pixel semantics、centered projection からの平行移動、または shader 側での座標変換は **F3 Board 配置作業で確定**する。
- **(e) 部分矩形レイヤー**（HUD パネル等）は aspect 一致のため `OrthoWidth/OrthoHeight` 明示指定が要る。**本計画スコープ外**（将来）とし F0 で記録。
- **テクスチャ filtering:** 既定は linear sampler。pixel-art 用途の point sampler 指定は F5 以降で Board/material レベルの override を検討（F3 は linear 固定で記録）。
- **検証:** F2 の `OrthographicViewportMatrixTest` は `Viewport::UpdateMatrices` が `OrthoHeight` を aspect 比から再計算せず literal に使うことを固定する。`CameraViewConstantsTest` にも Orthographic の `OrthoHeight` coverage を追加し、Viewport 経路と shader constant 経路が同じ centered orthographic を返すことを確認する。Board を `(0,0)` に置いた左上 pixel 配置や `(renderWidth/2, renderHeight/2)` の画面配置確認は F3 に送る。

### 3.9 方式 A（既定）/ 方式 B（opt-in）

**用語の確定（レビュー minor）— Viewport ≠ Layer:** CanvasView は **1 つの正射影 Viewport**を持つ（§3.4.1）。「layer」は**その Viewport 上で合成される順序付き要素群**（`LayerPriority` でグルーピングされた Board 群）であり、Viewport でも別 View でもない。

- **方式 A（既定）:** レイヤー単位 RT を持たず、CanvasView の**単一 `m_OutputTexture`（=`Canvas.Color`）へ全 Board を painter 順**に直接描く。Viewport は 1 つ、Layer は描画順のグルーピングに過ぎない。`Viewport::m_OutputTexture` は使わない。`PrepareBoardDrawCommands` は安定ソート → `LayerPriority` で partition → 各 layer 内 append。
- **方式 B（opt-in、F10）:** 指定 `LayerPriority` のみ **per-layer RT** を確保し（`CanvasView::SetLayerCompositeMode(layerPriority, bOwnRT)`）、レイヤー単位の不透明度/単純ブレンド/エフェクトを掛けてから `Canvas.Color` へ painter 順ブレンド合成する。方式 A と方式 B は**レイヤー単位で共存**（opt-in したレイヤーのみ B、他は A）。方式 A レイヤーは方式 B 導入後も不変。

### 3.10 Board 派生階層（基底 → Billboard → Impostor）

`BoardComponent` を基底とし、world-space 派生を後段で足す。基底の `virtual BuildBoardProxy` を派生がオーバーライドする（F3 で基底インターフェースを安定させ、F9/F11 で派生追加）:

- **`BillboardComponent : BoardComponent`（F9）:** `Space=WorldSpace`、`REFLECTION_CLASS(BillboardComponent, BoardComponent)`。`BuildBoardProxy` オーバーライドで:
  - **view-alignment 軸選択:** 完全 view-aligned（カメラに正対、全軸カメラ向き）か axis-aligned（Y 軸固定の水平ビルボード＝木/草向き、カメラ方向へ Y 回転のみ）かを PROPERTY で選択。回転を world transform へ反映。カメラ方向は GameThread の `BuildBoardProxy` 時に主 SceneView カメラの forward から算出（proxy へ確定値を焼く＝RenderThread でカメラ追跡しない）。
  - `WorldBounds`（`BoundingSphere`）を設定（錐台カリング用、§3.3）。
- **`ImpostorComponent : BillboardComponent`（F11）:** ベイク + octahedral アトラス + 距離 LOD。`REFLECTION_CLASS(ImpostorComponent, BillboardComponent)`。距離に応じた `LODLevel`（`MeshProxy.LODLevel` `SceneProxy.h:47` の LOD 機構と整合）でメッシュ/インポスター切替。octahedral セル選択・ベイク解像度・球面セルマッピングは F11 で詳細化（octahedral マッピングで view 方向 → アトラスセル → blend サンプリング）。

派生も `REFLECTION_CLASS`/`PROPERTY` で prefab 化。F3 時点で基底 `BuildBoardProxy` の virtual 契約を確定させ、F9/F11 は派生クラス追加のみ（基底インターフェース改変なし）。

### 3.11 所有権・スレッド・RHI・シングルトンの要約

- **所有権:** `BoardComponent` は Entity の Inner（Outer 破棄で連鎖破棄）。`CanvasView` は `RenderingCoordinator` のメンバ。カメラ表は `RenderingCoordinator` のメンバ。テクスチャは参照カウント Resource。
- **live-set キーイング（確定）:** `BoardProxy` は `ObjectId`（所属 Entity）+ `ComponentId`（BoardComponent）の両方を持つが、Canvas BoardProxy store index / stale live-set / subtree removal / tests は **ComponentId を primary key**にする。`ObjectId` は payload（所属 Entity 参照、ログ、将来の subtree 診断）として残すだけで、Canvas store の主キーにはしない。1 Entity に複数 BoardComponent が付きうるため、回収判定は Component 粒度で固定する。
- **スレッド・FramePacket 不変性（レビュー major 反映）:** Space publish・ソート・カメラ表 lookup・`BoardProxy.LayerMask × viewportPlan.Camera.CullingMask` 判定・`CanvasView::PrepareBoardDrawCommands(viewportPlan)` は GameThread の `RenderingCoordinator::GenerateDrawCommands` 中だけで実行する。`PrepareBoardDrawCommands` は `FramePacket.DrawCommands`/`FramePacket.InstanceData` へ EndFrame 前に append し、`ViewportRenderPlan` の range を確定する。RenderThread は FramePacket スナップショット（共有 flat array + `ViewportRenderPlan`）のみを読み、mutable な Canvas board store / `BoardProxy` / World / Entity / Component へアクセスしない。
  - **値のみ・ポインタ禁止:** `BoardProxy` および FramePacket へ載る全 Board データは **値（transform 行列・`TextureHandle`・tint、F6 以降は UV）のみ**。`BoardComponent`/`Entity`/`BoardProxy` の**ポインタを snapshot に載せない**（既存 `MeshProxy`/`LightProxy` パターン踏襲）。よって in-flight 中に Board entity が delete されても RenderThread は stale ポインタを踏まない。
  - **テクスチャ寿命:** `BoardProxy.Texture` は参照カウント Resource なので、snapshot が値で `TextureHandle` を保持する限り in-flight 中も Resource は生存（GameThread の delete で即解放されない）。テクスチャの非同期アンロード/ストリーミングがある場合も RefCount が in-flight packet 分を保持する設計を F0/実装レビューで確認。
  - **delete タイミング:** Board entity delete → 次回 `SyncToSceneView` で live-set から外れ `RemoveStaleBoardProxies` がストアから除去 → 次フレーム snapshot に含まれない。triple-buffer（`FramePacket.h:205-222` 付近の state machine）で RenderThread が Reading 中の packet は GameThread の別バッファ書き込みと分離されるため、in-flight packet は stale 参照を含まない。FramePacketManager の triple-buffer 分離は実装レビューで検証必須。
- **RHI:** 2D PSO/blend/sampler は抽象 `RHI::I*` + `GraphicsPipelineDesc`。深度なしは `DepthStencilState{depthTest=false, depthWrite=false}`。Vulkan は Private に閉じる。
- **シングルトン禁止:** 新 Manager を作らず `GEngine`/`RenderWorld`/`RenderingCoordinator` のメンバ。

---

## 4. Non-Goals（採否と理由）

- **SDF テキスト / フォントアトラス / テキストレイアウト**: 描画方式が Board と根本的に異なり独立計画。Board でのビットマップ文字表示は F6 のアトラス機構で部分的に可能。
- **9 スライス（nine-patch）**: UV/頂点生成が特殊化するため別計画。
- **タイルマップ**: 大量 Board のインスタンス化（F8）が前提。専用チャンク/カリングは F8 完了後の別計画。
- **2D パーティクルシステム**: GPU シミュレーション/エミッタ寿命管理が必要で別サブシステム。
- **オフラインアトラスパッキング（ツール）**: `Tools/AssetCook` 側機能でランタイム描画のスコープ外。F6 は**ランタイム UV 矩形参照と grid 分割ヘルパ**までを提供し、手動パック済み/既存アトラスを前提とする。
- **方式 B の高度エフェクト（ブラー/歪み/カラーグレード等）**: F10 は「レイヤー単位 RT + 不透明度/単純ブレンド」まで。エフェクトシェーダー群は別計画。
- **複数 SceneView の 2D オーバーレイ個別割り当て UI / Picture-in-Picture の 2D 配置**: per-viewport 基盤は将来対応可能だが本計画は 1 段合成に限定。
- **D3D12 等の追加バックエンド・RenderGraph コア自体の改修**: 抽象 `RHI::I*` と RenderGraph パス追加のみで完結。
- **CameraComponent（Entity 上のカメラ）**: 本設計で実装しない。`CameraProxy` は **Value Proxy（値スナップショット）**であり Entity Component の所有権モデルには載せない（World sync に CameraProxy を流さない、既存方針維持）。複数 Viewport の CameraId 共有は `RenderingCoordinator` のカメラ表（値ストレージ）経由のみ（§3.4.1）。

### 4.1 Future Goal（本フェーズ非対象だが将来の段階で実装）

Non-Goal（採否が「やらない」で確定）と区別し、**段階導入の将来工程**として位置づけるもの:

- **World→多 View 化の完全一般化**: 本計画は「主 SceneView + 1 つの CanvasView」の **2 View 固定ルーティング**に限定（§3.5 段階導入）。任意 View 数・任意マスクの一般ルーティング（`World::GetViews()` 列挙等）は将来段階で実装する Future Goal であり、設計はそれを妨げない方向（依存方向・Proxy ストア分離）で組む。
- **複数 CanvasView（主 UI 層 + HUD 層等）**: 本計画は 1 CanvasView 固定。将来複数 CanvasView を足す際も、ルーティング Step1/Step2 の Space + マスク判定がそのまま一般化できる形（forward-compatibility）にしておく。

---

# フェーズ計画

各フェーズに「目的 / 依存 / エンジンクリティカル印 / 作業（具体ファイルと file:line）/ 成果物 / ゲート / コミット例 / リスクとロールバック」を記す。**エンジンクリティカル印付き**（World 同期・Proxy レイアウト・ルーティング・カメラ共有・新 View 種別・FramePacket レイアウト・present 合成段・RenderGraph パス・PSO・RHI・Resource 寿命）は **計画レビュー + 実装レビューを別の最上位ティアエージェントが必須**で実施する。

推奨フェーズ順序の根拠: まず合成段の骨格（F1）を 2D 空で通して既存挙動不変を担保 → カメラ共有 + 正射影 Viewport（F2）→ BoardComponent/BoardProxy/ScreenSpace sink/最小描画（F3）→ 順序担保（F4）→ 見た目機能（F5-F7）→ 性能（F8）→ world-space Billboard（F9）→ 方式 B（F10）→ Impostor（F11）と、各段が独立してビルド・コミットできる順に積む。

---

## フェーズ F0: ベースライン + 前提確定（read-only）

### 目的

2D 実装前のビルド/CTest/描画ベースラインを固定し、後続フェーズの実装経路を左右する前提を実コードで確定する。**本フェーズのみ読み取り専用 + ベースライン記録（挙動変更なし）。**

> **§2 と F0 の関係（レビュー CRITICAL の裁定）:** 本計画書の §2（現状整理）は **F0 の出力予測値**であり、引用した file:line は計画策定時の調査に基づく。F0 はこれを実コードで再照合し、ズレがあれば `2d-rendering-baseline.md` の確定値で上書きする（計画書 §2 は予測、baseline.md が F0 後の確定一次情報）。F0 完了までは §2 の行番号を「確認待ちの予測」として扱い、F1 以降の実装は baseline.md の確定値を参照する。**F0 は読み取り専用だが「決定の記録」は行う**（実装は変えないが、`board2d.vert` にハードコードする原点/y 軸/MVP 順、正射影行列の引数規約、SortKey 安定ソート可否などの**決定値を baseline.md に確定記録**する。F3 以降はこの記録を decision-by-reference で参照する）。

### 作業（確認のみ）

- `develop` を最新化し、変更前 build（Game）と CTest を実行して結果を記録する。
- MT/ST 両モードで起動し DrawCalls/PSO 数/Vulkan validation を記録、Normal 代表スクショを保存。
- **World→Proxy 同期の BoardProxy 追加点**を確認: `SyncEntityRecursive`（`World.cpp:921-1062`）の Component 振り分け（`:943-1026`）と live-set/RemoveStale（`:870-872`）、`BuildMeshProxy`（`MeshComponent.cpp:186-247`）のテンプレート。BoardComponent 分岐と Space-only sink publish を足す正確な行を特定する。
- **RenderLayer-CullingMask の現状使用と交差判定の配線規模**を確認: `CameraProxy.CullingMask`（`SceneProxy.h:280`）が SceneView カリングに未使用であること、`HasFlag`（`RenderTypes.h:214`）が使えること、`MeshProxy.LayerMask`（`:87`）の設定経路（`MeshComponent.cpp:238`）。Board の交差判定は `GenerateDrawCommands`/`PrepareBoardDrawCommands` 側に置き、`SyncEntityRecursive` には入れない。
- **カメラ共有化の landed 状態**を確認: `CameraProxy.CameraId`（`SceneProxy.h:259`）、`Viewport` の値所有 + CameraId 参照（`Viewport.h:87`）、`SetMainCamera`/`m_MainCamera`（`RenderingCoordinator.h:219,354`）、`BuildViewportRenderPlan` の CameraId lookup とフォールバック。値コピー契約（`ViewRenderPlan.h:32`）を壊さない範囲を確定。
- **per-viewport オーサリングへの Board 振り分け点**を確認: `GenerateDrawCommands` の View ループ（`RenderingCoordinator.cpp:804`）と SceneView cast（`:818`）。CanvasView 分岐を足す位置を特定。
- **合成側アンカー**を確認: `View::Render` の `Reset`（`View.cpp:195`）・`PresentationGraphPass` 条件（`:281-284`）、`PresentationPass::Declare` 優先順（`PresentationPass.cpp:27-40`）、`RenderFrameExecutor::Execute` 二重ループ（`RenderFrameExecutor.cpp:29-75`）、`RenderGraphResourceNames.h` の `Composite.Color`/`Canvas.Color`、および F2 landed の `CompositePass` が Canvas を import しつつ Scene passthrough であること。
- **SortKey ビット割付**を確認: `(LayerPriority<<32)|OrderInLayer`、`uint32` 上限。`Container::VariableArray` に `std::stable_sort` 適用可否（イテレータ互換）を確認。
- **ScreenSpace transform 解釈 / 正射影正準**を確認: `Viewport::UpdateMatrices` の Orthographic パス、`MatrixUtils::CreateOrthographic` の行列レイアウト（`Matrix4x4` Row/Column major）と利き手。px 絶対・原点・y 軸向きを `board2d.vert` 用に確定。
- shared quad mesh に使える narrow allocator-backed API（`MeshResources::CreateProcedural` 相当）の有無、`VertexLayout` の 2D プリセット有無、`TextureHandle`/`BlendMode`（`MaterialTypes.h:198`）/`UVRect`（F6 まで未定義でよい）の既存定義有無を確認。
- **CanvasView 完全オーバーライドの side-effect 確認**: `View.h` の全メンバ（`m_Viewports`/`m_OutputTexture`/`m_Passes`/`m_PostProcessStack`/`m_bEnabled`/`m_bInitialized` 等）を列挙し、`CanvasView::Render` が基底を呼ばない場合に未初期化/二重実行になるメンバが無いことを確認（§3.6.1）。
- **FramePacket triple-buffer の Board 寿命安全性**を確認: `FramePacketManager` の triple-buffer + state machine（`FramePacket.h:205-222` 付近）が RenderThread Reading 中の GameThread 書き込みを分離すること、`TextureHandle` の RefCount が in-flight packet 分を保持すること（§3.11）。
- **旧版 2D 計画ファイルの有無**を確認: `Docs/Plans/` 配下に本ファイル以外の 2D 描画計画（`LayerView`/「Component 非依存バイパス」記述を含むもの）が残存していないか確認し、あれば `Docs/Archive/` 退避 or 削除（§0.0）。

### 成果物

- `Docs/Plans/2d-rendering-baseline.md`（新規、プレーン UTF-8）に: ベースライン描画統計・PSO 数・validation、上記確定項目（BoardProxy 追加点 / Space-only sink publish と Step2 交差判定の配線位置 / カメラ共有 landed 状態 / Board 振り分け点（`:818` の else-if 位置）/ 合成側アンカーと F2 passthrough 状態 / SortKey 割付 + 安定ソート可否 / ScreenSpace transform（原点/y 軸/MVP 順）/ 正射影正準（`CreateOrthographic` 引数規約）/ shared quad mesh または shader-generated quad 方針 / UVRect F6 defer / CanvasView side-effect / triple-buffer 寿命）、`board2d.vert` 用の決定値、Normal 代表スクショ（`Docs/Plans/assets/2d-rendering/`）。

### ゲート

- Debug build 成功（Game）、CTest 全通過、MT/ST 起動確認、`2d-rendering-baseline.md` に F1（合成アンカー）・F2（カメラ共有・正射影）・F3（BoardProxy 追加点・ルーティング・Board 振り分け点）・F4（SortKey・安定ソート）の前提が揃っている。

### コミット例

```text
2D描画サブシステム実装前のベースラインとBoardProxy/ルーティング/合成前提を記録する
```

リスクレベル: なし。ロールバック: 不要（read-only）。

---

## フェーズ F1: CanvasView 骨格 + 最終 1 段合成（2D 空でも present 不変）【エンジンクリティカル】

### 目的

`CanvasView`（`ViewType::UI`）の骨格、`CompositePass`、`PresentationPass` 入力拡張、`RenderFrameExecutor` 2 段化を導入する。合成段の骨格を 2D 空で通して既存挙動不変を担保する。旧 `LayerView` 合成設計を `CanvasView` へ改名移植する。

> **F1 の「不変」契約の明確化（レビュー CRITICAL の裁定）:** F1 では CanvasView を**既定で登録しない**（生成 API は足すが、2D を使わないシーンでは生成されない）。よって `ShouldCompose`（§3.6.5）は false を返し、`RenderFrameExecutor` は**従来 `:29-75` 経路をそのまま走る → ビット完全不変**（既存シーンに影響ゼロ）。CanvasView を**明示登録して enabled にした上で Board 0 個**のケースは、段2 が走るが透明 Canvas の alpha-over 結果が scene と一致するため**視覚的に不変**（validation・blit 回数の差は許容。スクショ pixel 一致で検証）。「2D 空でも present 不変」とは前者（CanvasView 未登録/disabled）の**ビット不変**と、後者（enabled CanvasView 登録・Board 0）の**視覚不変**の二段で担保する。両ケースをゲートで個別に検証する。

### 依存

F0。

### 作業

- `RenderGraphResourceNames.h`（`:7-20`）に `Canvas.Color`・`Composite.Color` を追加（`Identity::Literal`）。
- 新規 `Library/Core/Public/Rendering/CanvasView.h` / `Private/Rendering/CanvasView.cpp`: `CanvasView : public View`、`Initialize` で `ViewType::UI`、`Render(ViewRenderContext&)` を**完全オーバーライド**（基底を呼ばず `Reset` → 空（透明クリア）の `m_OutputTexture` を確定し `Canvas.Color` publish → `PresentationGraphPass` 追加しない、§3.6.1）。透明 Canvas は RenderGraph pass を実行し、null-pipeline fullscreen/render-pass command 等で `BeginRenderPass`/`EndRenderPass` の clear/store を物理発行する（宣言のみ不可）。F2 landed 後の CanvasView は full-rect Viewport を 1 つ持ち、CameraId と正射影 value camera を coordinator から割り当て済み。
- 新規 `CompositePass.h`/`CompositePass.cpp` + `composite2d.vert`/`composite2d.frag`（BOM なし）: `ImportTexture(SceneView::m_OutputTexture)`（物理 RT、named resource ではない）+ 任意 `ImportTexture(CanvasView::m_OutputTexture)` → `Composite.Color` を**常に publish**（§3.6.2/§3.6.3）。F2 landed 実装は Canvas import + scene passthrough であり、F3 で alpha-over を完成させる。
- `PresentationPass::Declare`（`PresentationPass.cpp:22-40`）先頭に `Composite.Color` の `TryReadTexture` を追加（優先順 Composite→Presentation→ToneMapped、§3.6.4）。
- `RenderFrameExecutor::Execute`（`RenderFrameExecutor.cpp:11-99`）を 2 段化（§3.6.5）。`ShouldCompose` は packet ViewId から enabled CanvasView の実 View object を解決できた時だけ true。View 数や非 Scene View の有無だけでは合成しない。フレーム開始/失敗時に View 出力を reset し、段2完了後と `ReleaseInitializedResources` で CompositePass/PresentationPass request/result を clear。段2 viewport/scissor は primary SceneView viewport、無ければ `nullptr` full-output fallback。
- `RenderingCoordinator` に `CanvasView` を作成・登録するメソッド（SceneView の後に `m_Views`/`m_Screen.m_Views` へ登録、§3.6.6）。既定では 2D 機能を使うシーンでのみ生成。`CreateCanvasView` は pre-frame 専用で、GameThread 上の coordinator / RenderWorld 初期化後・最初の `BeginFrame` 前に呼ぶ。`m_bFrameSubmissionStarted` と可能なら packet state で、フレーム提出開始後の呼び出しを拒否する。
- CMake: 新規 `.cpp`/`.h`/シェーダーを `Library/Core/CMakeLists.txt` へ登録。

### 成果物

- `Canvas.Color`/`Composite.Color` named resource、`CanvasView` 骨格、`CompositePass`+シェーダー、`PresentationPass` 優先順拡張、`RenderFrameExecutor` 2 段化 + フォールバック、CanvasView 登録 API。
- 新規テスト: `RenderGraphResourceNamesTest`（Canvas/Composite 追加）、`CompositePassPassthroughTest`（Canvas 無し時 scene 素通し）、`CompositePassInputSourceTest`（段2 が persistent RT を import・named resource を段1 と共有しない、§3.6.2）、`RenderFrameExecutorTwoStageTest`（段1/段2 シーケンス、one-viewport CanvasView render）、`RenderFrameExecutorFallbackTest`（`ShouldCompose` false → 従来 `:29-75` 経路でビット不変、§3.6.5）、`ShouldComposePredicateTest`（1 Scene=false、2 SceneViews=false、disabled Canvas=false、enabled one-viewport Canvas=true）、`CreateCanvasViewLifecycleTest`（pre-frame のみ許可）、`FrameOutputLifetimeTest`（render start/failure reset・段2 request/result clear）。

### ゲート

- focused build（Game）、CTest 全通過（新規含む）、**(1) CanvasView 未登録/disabled 時は描画ビット完全不変**（従来経路、MT/ST スクショ）、**(2) enabled CanvasView 登録・Board 0・one-viewport 時は視覚不変**（段2 経路、透明 Canvas 合成、MT/ST スクショ pixel 一致）、ShouldCompose 4 ケース通過、Canvas clear/store が物理実行されること、Vulkan validation なし、`git diff` 行末破壊なし、エンジンクリティカル実装レビュー完了。

### コミット例

```text
CanvasView骨格と最終1段合成（CompositePass/PresentationPass優先順/RenderFrameExecutor2段化）を追加する
```

リスクレベル: 高（present 合成段・RenderGraph パス・RenderThread 制御フロー）。ロールバック/封じ込め: `bUseCompositePass=false` で従来経路へ即時退避。`bUseCompositePass` は enabled CanvasView 解決時だけ true なので、SceneView が複数あっても CanvasView 無しなら従来/フォールバックへ残る。RenderGraph per-View reset（`View.cpp:195`）で named resource を共有できないため、段2 は各 View の `m_OutputTexture` を import する設計に限定する。

---

## フェーズ F2: カメラ共有化 + CanvasView 正射影 Viewport【エンジンクリティカル】

### 目的

F2 landed 済みの基盤として、`CameraProxy.CameraId`（`SceneProxy.h:259`）を有効化し、`RenderingCoordinator` にカメラ表（`RegisterCamera`/`UpdateCamera`/`FindCamera`）を導入済み。Viewport は CameraId 参照で共有カメラを引く（値所有は維持、スナップショット値コピーも維持）。CanvasView は full-rect Viewport を 1 つ持つ Canvas invariant とし、coordinator がその Viewport に正射影カメラ（`CullingMask=RenderLayer::UI`、`OrthoWidth=m_RenderWidth`、`OrthoHeight=m_RenderHeight`）を登録・割当・リサイズ追従させる。

### 依存

F1。

### 作業

- `RenderingCoordinator` にカメラ表メンバ（`UnorderedMap<uint64_t, CameraProxy> m_Cameras`、`m_NextCameraId`、`m_MainCameraId`、`m_CanvasCameraId`）と `RegisterCamera`/`UpdateCamera`/`FindCamera` を追加（§3.4、新 Manager を作らない）。`SetMainCamera`（`RenderingCoordinator.cpp` の実装）は `m_MainCameraId` を再利用して overwrite（Game 側は ID を意識しない後方互換）。
- `Viewport` に CameraId 参照を追加（`SetCameraId`/`GetCameraId`）。`SetCamera`（`Viewport.h:82`）API は維持するが、内部処理は `camera.CameraId` を `m_CameraId` へ mirror し、値 camera をコピーするだけに限定する。`Viewport` は coordinator/table access を持たず `RegisterCamera` しない。
- `BuildViewportRenderPlan`（`RenderingCoordinator.cpp:40-115`）で CameraId からカメラ表 lookup → 解決した `CameraProxy` を `ViewportRenderPlan.Camera`（`ViewRenderPlan.h:32`）へ**値コピー**（フォールバックは従来の `m_MainCamera` `:863-864` を維持）。この helper は private/file-local のままにし、resolver または private coordinator helper で camera 解決を注入する。テストのために public API 化しない。
- `CanvasView::Initialize` は full-rect Viewport をちょうど 1 つ作る。`RenderingCoordinator::CreateCanvasView()` が `RegisterCamera({Projection=Orthographic, CullingMask=RenderLayer::UI, OrthoWidth=m_RenderWidth, OrthoHeight=m_RenderHeight, NearPlane=0, FarPlane=1})` の CameraId を `m_CanvasCameraId` として保持し、その Viewport に `SetCameraId(cameraId)` と値 `SetCamera(camera)` を設定する。CanvasView はカメラ表を知らない。
- `Viewport::UpdateMatrices` と `CameraViewConstants::BuildProjectionMatrix` の Orthographic パスを、既存 centered `MatrixUtils::CreateOrthographic(width, height, near, far)` に `OrthoWidth`/`OrthoHeight` を literal に渡す実装へ揃える。top-left pixel semantics / off-center projection / shader translation は F3 Board 配置で扱う。
- coordinator-owned helper で Canvas camera resize を一元化し、`UpdateRenderResolution` を通る全経路（`Resize`、`SetRenderScale`、swapchain acquire 時の resize、presentation-dirty resize handling）後に `m_CanvasCameraId` の表エントリと Canvas viewport の値 camera を `m_RenderWidth/m_RenderHeight` へ追従させる。
- cleanup を追加する: `DestroyView` が CanvasView を破棄するとき `m_CanvasCameraId = 0`、`ReleaseInitializedResources` で `m_Cameras.clear()`、`m_MainCameraId = 0`、`m_CanvasCameraId = 0`、`m_NextCameraId = 1`。

### 成果物

- カメラ表 + CameraId 参照、`Viewport` CameraId 連携、`BuildViewportRenderPlan` の lookup（private/file-local 維持）、CanvasView one-viewport invariant、Canvas 正射影 camera 登録/更新/cleanup、`Viewport` と `CameraViewConstants` の OrthoHeight literal 化。
- 新規/更新テスト: `CameraTableTest`（採番 1 始まり・0=invalid・lookup 失敗時 `m_MainCamera` フォールバック・`SetMainCamera` の `m_MainCameraId` 再利用・複数 Viewport の共有スナップショット、§3.4.1）、`ViewportCameraIdRenderPlanTest`（CameraId lookup と fallback を public behavior または狭い resolver seam で検証し、`BuildViewportRenderPlan` を public 化しない）、`OrthographicViewportMatrixTest`（`OrthoWidth`/`OrthoHeight` literal centered matrix）、`CameraViewConstantsTest` OrthoHeight coverage、`CanvasViewRenderTest`（CanvasView は one viewport）、`RenderFrameExecutorPlanTest`（one-viewport Canvas の段1/段2 coverage）、render-scale/resize Canvas camera coverage、Canvas destroy/recreate または cleanup coverage。

### ゲート

- `cmake --build build --config Debug --target Game`、focused テスト（`CameraTableTest` / `ViewportCameraIdRenderPlanTest` / `OrthographicViewportMatrixTest` / `CameraViewConstantsTest` / `CanvasViewRenderTest` / `RenderFrameExecutorPlanTest` / render-scale・resize Canvas camera coverage / Canvas destroy・recreate または cleanup coverage）、`ctest --test-dir build -C Debug` 全通過、focused runtime smoke with `--enable-canvas-view`、**3D 描画結果が不変**（主 SceneView は従来カメラ経路、MT/ST スクショ比較）、`git diff --numstat` と `git diff --ignore-cr-at-eol --numstat` の EOL 比較一致、エンジンクリティカル実装レビュー完了。

### コミット例

```text
CameraId参照の共有カメラ表とCanvasViewの正射影Viewportを追加する
```

リスクレベル: 高（カメラ共有・スナップショット契約・Public 隣接 API）。ロールバック/封じ込め: カメラ表 lookup 失敗時は従来 `m_MainCamera` フォールバックに落ちるため既存経路を壊さない。スナップショットは値コピーのまま（RenderThread はポインタ追跡しない）。SetMainCamera の後方互換を実装レビューで重点確認。

---

## フェーズ F3: BoardComponent + BoardProxy + ルーティング + 単一 ScreenSpace Board 最小描画【エンジンクリティカル】

### 目的

`BoardComponent`（Entity Inner、`REFLECTION_CLASS`/`PROPERTY`）、`BoardProxy`、`SyncEntityRecursive` の BoardComponent 分岐、ScreenSpace Board sink、CanvasView Board store、`GenerateDrawCommands` 中の `BoardProxy.LayerMask × viewportPlan.Camera.CullingMask` 可視判定、単一 ScreenSpace Board の最小描画（正射影・方式 A）、および CompositePass の実 alpha-over 合成を実装する。

### 依存

F2。

### 作業

- `RenderTypes.h` に `enum class BoardSpace` を追加。`BlendMode` は既存（`MaterialTypes.h:198`）を include して再利用。`SceneProxy.h` に `BoardProxy` 追加（§3.3）。**`UVRect` は F3 で追加しない**（F6 へ defer）。F3 は full [0,1] quad UV 固定、または solid-color/default white shader path を最小契約にする。
- 新規 `BoardComponent.h`/`BoardComponent.cpp`（§3.2、`REFLECTION_CLASS(BoardComponent, Component)`、`FieldInitializer`/`IUnknown` コンストラクタ、`virtual BuildBoardProxy`（`MeshComponent::BuildMeshProxy` と同契約、§3.2）、`RefreshRenderTransformCache`（Entity world transform を値コピー、§3.2））。CMake 登録（`PRIVATE_SOURCES :116` / `PUBLIC_HEADERS :324` 付近）。
- **CanvasView に `BoardProxy` store** + `Rendering::IBoardProxySink` 実装（`UpdateBoardProxy(ComponentId, proxy)`/`RemoveBoardProxy(ComponentId)`/`RemoveStaleBoardProxies(liveComponentIds)`、ComponentId primary key、`SceneView.h:87-160` の stable-swap-erase パターン踏襲）。BoardProxy は **CanvasView 側にのみ保持**（SceneView の MeshProxy 経路に載せない、§3.5 二重描画防止）。F3 は ScreenSpace のみなので SceneView 側 Board store は F9 まで作らない。`ObjectId` は payload のみ。
- **Dependency route:** narrow `Rendering::IBoardProxySink`（または同等）を追加し、`World::SetScreenSpaceBoardSink(Rendering::IBoardProxySink* sink)` で borrowed pointer を設定する。World は `CanvasView`/`RenderingCoordinator` を include/use しない。sink 変更時は World が Board render state を dirty にし、Canvas destroy/recreate 時は破棄前に sink を `nullptr` へ clear して dangling pointer を避ける。新 Canvas 作成後に sink を再設定し、次 sync で再 publish する。
- **Step1（`World::SyncEntityRecursive`、`World.cpp:921-1062`、カメラ非依存・Space-only）:** `CastTo<BoardComponent>` 分岐を追加: dirty 判定（`IsRenderStateDirty()`/transform version、`:948-949`）→ `RefreshRenderTransformCache` → `BuildBoardProxy` → ObjectId/ComponentId 設定 → `Space` だけを見る。`Space==ScreenSpace` なら注入された sink へ `UpdateBoardProxy(ComponentId, proxy)`、`Space==WorldSpace` なら F3 では sink へ `RemoveBoardProxy(ComponentId)`（ScreenSpace から切り替わった stale proxy を除去）→ `liveBoardComponentIds` 登録。ここでは camera / CullingMask / LayerMask 交差を読まない。`SyncToSceneView`（`:834`、RemoveStale は `:870-872`）後に sink の `RemoveStaleBoardProxies(liveBoardComponentIds)` を呼ぶ。
- **Step2（`RenderingCoordinator::GenerateDrawCommands`、`:804` View ループ、カメラを読む）:** `:818` の `DynamicPointerCast<SceneView>` に `else if (DynamicPointerCast<CanvasView>)` 分岐を並置し、GameThread 上で `CanvasView::PrepareBoardDrawCommands(viewportPlan)` を呼ぶ。その中で `HasFlag(viewportPlan.Camera.CullingMask, board.LayerMask)` 交差で可視集合を作り（§3.5 Step2）、`ComputeSortKey` で安定ソート（F4 で完全化、F3 は単一 Board なので順不問）→ `FramePacket.DrawCommands`/`InstanceData` へ **3D の後に、EndFrame 前に** append → `viewportPlan.DrawCommandRange` を設定。Render-time `CanvasView::Render` は `context.CurrentDrawCommands` / packet range のみを読む。
- 共有 quad mesh は narrow allocator-backed resource API（例: `MeshResources::CreateProcedural`）で 2 三角・full [0,1] UV・CCW の quad を作ることを既定とする。既存 resource API を広げすぎず、hardcoded handle collision を避ける。実装レビューで shader-generated quad がより小さく安全と判断した場合はその代替を許容するが、`ProceduralMeshGenerator` の広い API 追加は避ける。
- `VertexLayout` の 2D プリセット（Position2D+TexCoord+Color あるいは shader-generated quad 用の最小入力）、`board2d.vert`/`board2d.frag`（BOM なし、深度なし PSO、§3.8 の px 空間・原点/y 軸/MVP 順は baseline.md の確定値に従う）。F3 はテクスチャ未設定でも visible solid/default white Board を描き、`BuildBoardProxy` が無効テクスチャだけで失敗しない。CanvasView の Board 描画パスで `DepthStencilState{depthTest=false, depthWrite=false}`。
- `CompositePass` を F2 landed の Scene passthrough から straight alpha-over Canvas over Scene へ更新し、visible Canvas が `Composite.Color` と backbuffer に反映されるようにする（§3.6.3）。

### 成果物

- `BoardComponent`/`BoardProxy`/`BoardSpace`、SyncEntityRecursive 分岐 + ScreenSpace sink publish、CanvasView BoardProxy store + `PrepareBoardDrawCommands`、quad mesh/2D VertexLayout/board2d シェーダー、CompositePass alpha-over、単一 ScreenSpace Board の描画。
- 新規テスト: `BoardComponentReflectionTest`（PROPERTY round-trip = prefab シリアライズ確認）、`BoardProxyRoutingTest`（Step1 Space-only publish + Step2 `BoardProxy.LayerMask × viewportPlan.Camera.CullingMask` 交差で CanvasView 可視・SceneView の MeshProxy へ漏れない、§3.5）、`BoardProxyKeyingTest`（ComponentId primary key・stale 回収・ObjectId payload）、`BoardComponentTransformTest`（`RefreshRenderTransformCache` が Entity world transform を px としてキャッシュ、§3.2/§3.8）、sink installation after first sync coverage、Canvas destroy/recreate clears sink coverage、multiple BoardComponents on one Entity coverage、ScreenSpace-to-WorldSpace stale removal coverage、no RenderThread reads from mutable Canvas board stores coverage、`CompositePassAlphaOverTest`（visible Canvas affects `Composite.Color`）、Game テストシーンに 1 枚の ScreenSpace Board を表示し `F3_expected.png` で目視/pixel 比較。

### ゲート

- focused build（Game）、CTest 全通過、**3D 描画不変 + 画面に 1 枚の Board が正しい px 位置/サイズで表示**（MT/ST スクショ比較）、CompositePass alpha-over output/visible Canvas affects `Composite.Color`、visible ST/MT Board smoke、二重描画なし、RenderThread が mutable Canvas board store を読まないことのレビュー/テスト確認、エンジンクリティカル実装レビュー完了。

### コミット例

```text
BoardComponentとBoardProxyを追加しCanvasViewへ単一ScreenSpace Boardを描画する
```

リスクレベル: 高（World 同期・Proxy レイアウト・ルーティング・新描画パス・PSO・Resource 寿命）。ロールバック/封じ込め: BoardComponent を持たないシーンは `SyncEntityRecursive` の分岐に入らず完全不変。CanvasView 未登録時は F1 のフォールバックで present 不変。二重描画防止（BoardProxy を SceneView MeshProxy 経路に載せない）を実装レビューで重点確認。

---

## フェーズ F4: 複数 Board + レイヤー内安定 SortKey + 複数レイヤー（優先度）【エンジンクリティカル】

### 目的

複数 Board の painter 順描画を、(a) レイヤー間 = `LayerPriority` 昇順、(b) レイヤー内 = `OrderInLayer` 昇順の 2 階層で担保する。`CanvasView::PrepareBoardDrawCommands` で明示安定ソートする（§3.7）。

### 依存

F3。

### 作業

- `CanvasView::PrepareBoardDrawCommands` で、`LayerMask x CullingMask` filter 後の visible Board を `Container::VariableArray` の一時リストへ集め、`ComputeSortKey`（`(LayerPriority<<32)|OrderInLayer`）で `std::stable_sort` する。比較は `SortKey` 昇順を主、private な Canvas insertion sequence 昇順を副とし、`ComponentId` は defensive final fallback のみとする。`uint32` 上限 `assert`。
- `BoardComponent` の `LayerPriority`/`OrderInLayer` PROPERTY を有効化する。両者は explicit serialized 値で既定 `0`、auto-increment は行わない。CanvasView は `BoardComponent` を mutate せず、`ComponentId` keyed の private / non-serialized insertion sequence bookkeeping だけを持つ。
- 3D 用 `DrawCommandSorter`（`DrawCommand.cpp:374/397/420` の `std::sort`）に Board を載せない（painter range は別管理）。

### 成果物

- 2 階層安定ソート、複数 Board/複数レイヤーの正しい前後関係。
- 新規テスト: `BoardSortKeyStableTest`（同 SortKey の append 順保存・フレーム間バイト一致）、`MultiLayerOrderTest`（LayerPriority 昇順合成）。Game テストシーンで重なり合う複数 Board を表示し `F4_expected.png` 比較（チラつきなし）。

### ゲート

- focused build、CTest 全通過、**複数 Board が安定した前後関係で表示・フレーム間チラつきなし**（MT/ST）、エンジンクリティカル実装レビュー完了。

### コミット例

```text
複数Boardのレイヤー優先度とレイヤー内安定SortKeyで2階層painter順を担保する
```

リスクレベル: 中〜高（順序担保・スナップショット安定性）。ロールバック/封じ込め: 安定ソートは CanvasView オーサリング内に閉じ 3D 経路に影響しない。単一 Board 時は F3 と同一。

---

## フェーズ F5: ブレンド / Tint / フリップ / ピボット / サイズ（px/world）

### 目的

Board の表現力を拡張: `BlendMode`（Translucent/Additive/Opaque）、`Tint`（Vector4）、水平/垂直フリップ、ピボット（回転/スケール中心）、サイズ単位（px 絶対 / world 単位）。

### 依存

F4。

### 作業

- `BoardComponent` に `BlendModeProp`/`Tint`/`bFlipX`/`bFlipY`/`Pivot`/`SizePx` PROPERTY 追加（prefab シリアライズ）。`BuildBoardProxy` で BoardProxy へ反映。
- CanvasView Board 描画パスで `BlendMode` ごとの `BlendAttachmentDesc` PSO バリアント（Translucent=alpha-over、Additive=add、Opaque=opaque）。`board2d.frag` で Tint 乗算。`board2d.vert` でフリップ/ピボット/サイズを transform に反映。
- ブレンドモードごとに draw をグルーピング（PSO 切替最小化）。安定ソートはブレンドグループ内で維持。

### 成果物

- ブレンド/Tint/フリップ/ピボット/サイズ対応、PSO バリアント。新規テスト: `BoardBlendModeTest`（PSO 選択）、`BoardTransformTest`（フリップ/ピボット行列）。Game テストシーンで各機能を `F5_expected.png` で確認。

### ゲート

- focused build、CTest 全通過、各表現が目視一致（MT/ST スクショ）、Vulkan validation なし。

### コミット例

```text
Boardにブレンドモード・Tint・フリップ・ピボット・サイズ単位を追加する
```

リスクレベル: 中（PSO バリアント・シェーダー）。ロールバック: PROPERTY 既定値で従来描画と一致。

---

## フェーズ F6: スプライトシート / アトラス / UV 矩形

### 目的

1 テクスチャ内の部分矩形（UV 矩形）を Board に貼り、スプライトシート/アトラスを参照する。grid 分割ヘルパを提供。

### 依存

F5。

### 作業

- `BoardComponent` に `UVRectProp` PROPERTY、`Board::ComputeSpriteSheetUVRects(texWidth, texHeight, cellWidth, cellHeight)` 静的ヘルパ。`BuildBoardProxy` で `BoardProxy.UV` 反映。`board2d.vert`/`board2d.frag` で UV 矩形を quad UV にマップ。
- 既存 `TextureResources`/`GpuResourceStore` の手動パック済みアトラスを前提（オフラインパッキングは Non-Goal）。

### 成果物

- UV 矩形参照 + grid ヘルパ。新規テスト: `SpriteSheetUVRectTest`（grid 分割の UV 計算）。Game テストシーンでアトラスから複数スプライトを表示し `F6_expected.png` 比較。

### ゲート

- focused build、CTest 全通過、アトラス分割表示が目視一致（MT/ST）。

### コミット例

```text
BoardにスプライトシートUV矩形参照とgrid分割ヘルパを追加する
```

リスクレベル: 低〜中。ロールバック: `UVRect::FullTexture()` 既定で F5 と一致。

---

## フェーズ F7: テクスチャアニメーション / フリップブック

### 目的

時間経過でスプライトシートのコマを切り替えるフリップブックアニメーション。

### 依存

F6。

### 作業

- `BoardComponent::Tick(deltaTime)`（`Component` 既存 Tick 経路）でフレームインデックスを進め、現フレームの UV 矩形を `UVRectProp` に設定（F6 の UV 機構を再利用）。`FrameCount`/`FramesPerSecond`/`bLoop` PROPERTY 追加。Tick は GameThread、proxy は次回 sync で更新。
- 再生制御（Play/Pause/Stop/SetFrame）。

### 成果物

- フリップブックアニメ。新規テスト: `FlipbookAdvanceTest`（時間→フレーム→UV）。Game テストシーンでアニメ表示し `F7_expected.png`（代表フレーム）比較。

### ゲート

- focused build、CTest 全通過、アニメ再生が目視確認（MT/ST）。

### コミット例

```text
Boardにフリップブックによるテクスチャアニメーションを追加する
```

リスクレベル: 低。ロールバック: 既定で非アニメ（単一フレーム）= F6 と一致。

---

## フェーズ F8: インスタンス化 Board バッチング（性能）

### 目的

同一テクスチャ/同一 PSO の多数 Board を 1 ドローにインスタンス化し、大量スプライトの性能を改善する。

### 依存

F7。

### 作業

- `CanvasView::PrepareBoardDrawCommands` で安定ソート後、同一 (Texture, BlendMode, PSO) 連続 Board をインスタンスバッチに集約。per-instance データ（transform/UV/Tint）を `FramePacket.InstanceData` の flat array へ append、`DrawIndexedInstanced` で 1 ドロー（`AppendRebasedDrawCommands` の `FirstInstance`/`InstanceDataOffset` rebasing に整合、`RenderingCoordinator.cpp:880-887`）。
- 安定順を破らない範囲でバッチ境界を切る（ソート → グルーピング → インスタンス化）。

### 成果物

- インスタンスバッチング。新規テスト: `BoardInstanceBatchingTest`（同一キー連続がバッチ化・順序保存）、性能ベンチ。Game テストシーンで N=1000 スプライトを表示し DrawCalls 削減を計測。

### ゲート

- focused build、CTest 全通過、大量 Board で DrawCalls がベースライン比減少・見た目不変（MT/ST スクショ）。

### コミット例

```text
同一テクスチャBoardをインスタンス化バッチングして大量スプライトの性能を改善する
```

リスクレベル: 中（instance data rebasing・順序保存）。ロールバック: バッチ無効化フラグで F4 の per-board draw へ退避。

---

## フェーズ F9: WorldSpace Board = Billboard を SceneView へルーティング【エンジンクリティカル】

### 目的

`Space=WorldSpace` の Board（= Billboard、view/axis-aligned）を SceneView へルーティングし、3D カメラ・深度を共有してオクルージョン/LOD を整合させる。

### 依存

F8（描画基盤）+ F2（カメラ共有）。

### 作業

- `BillboardComponent : BoardComponent`（`Space=WorldSpace`、`REFLECTION_CLASS(BillboardComponent, BoardComponent)`）。`BuildBoardProxy` をオーバーライドし `WorldBounds`（錐台カリング用、§3.3）を設定、view/axis-aligned の回転（GameThread で主 SceneView カメラ forward から算出した確定値）を world transform へ反映（§3.10）。
- **Step1（`SyncEntityRecursive`、カメラ非依存）:** WorldSpace 分岐を追加: `Space==WorldSpace` → **SceneView の別 Board ストア**（`m_BoardProxies`、`MeshProxy` 経路とは分離、§3.5 二重描画防止）へ `UpdateBoardProxy`。ここではマスク交差を見ない。SceneView 側にも `BoardProxy` ストア + 登録 API + `RemoveStaleBoardProxies` を F9 で追加（F3 では CanvasView のみ）。
- **Step2（`GenerateDrawCommands` の SceneView 分岐）:** SceneView の Viewport の `Camera.CullingMask` と `board.LayerMask` の `HasFlag` 交差で可視判定し、交差した WorldSpace Board のみ SceneView の Forward 透過パスへ相乗りさせる（`SceneView::PrepareDrawCommandsForViewport` 側で Billboard を iterate、CanvasView 経路には関与しない）。`Scene.Depth`（`RenderGraphResourceNames.h:16`）を共有し 3D 深度オクルージョン。
- 最終前後は 1 段合成で「3D 下地（Billboard 含む）→ ScreenSpace 2D オーバーレイ」（ScreenSpace 2D は常に 3D + WorldSpace Board の上）。F1 の `ShouldCompose`（§3.6.5）は enabled CanvasView 解決時のみ true であり、CanvasView-less の WorldSpace Board だけでは合成を強制しない。F9 で CanvasView 無しでも present 一本化が必要になった場合は、F9 の明示的な将来 API/flag（例: `bForceCompositeForWorldSpaceBoard`）として別途設計し、F1 の既定 predicate へ混ぜない。

### 成果物

- `BillboardComponent`、WorldSpace ルーティング、SceneView Forward 相乗り。新規テスト: `BillboardRoutingTest`（WorldSpace → SceneView・ScreenSpace → CanvasView 分離）、`BillboardDepthTest`（3D 深度オクルージョン）。Game テストシーンで 3D シーン内に Billboard を表示し `F9_expected.png` 比較。

### ゲート

- focused build、CTest 全通過、Billboard が 3D 深度で正しくオクルードされ ScreenSpace Board と分離（MT/ST スクショ）、エンジンクリティカル実装レビュー完了。

### コミット例

```text
WorldSpace BillboardをSceneViewへルーティングし3D深度を共有して描画する
```

リスクレベル: 高（SceneView 描画経路・深度共有・ルーティング分岐）。ロールバック/封じ込め: WorldSpace Board が無ければ SceneView は完全不変。ScreenSpace 経路（F3-F8）に影響しない。

---

## フェーズ F10: 方式 B（レイヤー単位 RT + 不透明度）opt-in

### 目的

指定レイヤーのみ per-layer RT へ描き、レイヤー単位の不透明度/単純ブレンドを掛けてから `Canvas.Color` へ合成する（方式 B、opt-in）。

### 依存

F4（方式 A 順序担保）。

### 作業

- `CanvasView::SetLayerCompositeMode(layerPriority, OwnRT)` で指定レイヤーのみ per-layer RT（`Viewport::m_OutputTexture` 再利用）を確保。レイヤー単位不透明度 PROPERTY。RT 同士を `Canvas.Color` へ painter 順ブレンド。
- 方式 A（既定）は不変。方式 B は明示 opt-in のレイヤーのみ。

### 成果物

- 方式 B（レイヤー RT + 不透明度）。新規テスト: `LayerCompositeModeBTest`（per-layer RT + 不透明度合成）。Game テストシーンで半透明レイヤーグループを表示し `F10_expected.png` 比較。

### ゲート

- focused build、CTest 全通過、レイヤー単位不透明度が目視一致（MT/ST）、方式 A レイヤーは不変。

### コミット例

```text
レイヤー単位RTと不透明度の方式Bをopt-inで追加する
```

リスクレベル: 中（追加 RT・合成）。ロールバック: opt-in 未使用時は方式 A（F4）と完全一致。

---

## フェーズ F11: インポスター（ベイク + octahedral + LOD、SceneView 側、a/b 分割）

### 目的

`ImpostorComponent`（Billboard 派生）で、ベイクした多視点アトラス（octahedral マッピング）を LOD として SceneView に描く。

### 依存

F9（WorldSpace Billboard）。

### 作業

- **F11a（ベイク + アトラス）:** ベイク経路（複数視点からメッシュをレンダリングし octahedral アトラスへ格納）。`ImpostorComponent : BillboardComponent`、`REFLECTION_CLASS`。アトラスは参照カウント Resource。
- **F11b（octahedral サンプリング + LOD）:** `impostor.frag` で view 方向から octahedral セルを選び blend サンプリング。距離 LOD でメッシュ/インポスター切替（`MeshProxy.LODLevel`/`MeshComponent` LOD 機構と整合）。

### 成果物

- `ImpostorComponent`、ベイク + octahedral + LOD。新規テスト: `ImpostorOctahedralTest`（視点→セル選択）、`ImpostorLODSwitchTest`（距離 LOD）。Game テストシーンで遠景インポスター切替を `F11_expected.png` 比較。

### ゲート

- focused build、CTest 全通過、インポスター LOD 切替が目視一致（MT/ST）、エンジンクリティカル（ベイク経路・SceneView 描画）実装レビュー完了。

### コミット例

```text
ImpostorのベイクとoctahedralサンプリングとLOD切替をSceneViewに追加する
```

リスクレベル: 高（ベイク経路・SceneView 描画・LOD）。ロールバック/封じ込め: a/b 分割で b のみ revert 可。Impostor 未使用シーンは不変。

---

## 5. フェーズ依存と順序根拠（まとめ）

```text
F0(read-only)
 └─ F1(合成骨格・2D空) ── F2(カメラ共有+正射影Viewport)
                              └─ F3(BoardComponent/Proxy/ScreenSpace sink/最小描画)
                                   └─ F4(複数Board・2階層安定ソート)
                                        ├─ F5(ブレンド/Tint/フリップ/ピボット/サイズ)
                                        │    └─ F6(スプライトシート/UV)
                                        │         └─ F7(フリップブック)
                                        │              └─ F8(インスタンスバッチング)
                                        │                   └─ F9(WorldSpace Billboard→SceneView) ── F11(Impostor a/b)
                                        └─ F10(方式B opt-in)
```

順序根拠: 合成段の骨格（F1）を 2D 空で通して既存挙動不変を最初に固める → カメラ共有（F2）で CanvasView の正射影 Viewport の基盤を用意 → BoardComponent/Proxy/ScreenSpace sink（F3）でオーサリング経路を確立 → 順序担保（F4）→ 見た目機能（F5-F7）→ 性能（F8）→ world-space（F9）→ 方式 B（F10）→ Impostor（F11）。各段が独立してビルド・レビュー・コミットできる最小単位。

---

## 6. 機能カバレッジ表

| 機能 | 採否 | フェーズ | 載る基盤（file:line） |
|------|------|----------|------------------------|
| ScreenSpace Board（スプライト表示） | 採用 | F3 | `BoardComponent`(新規) → `BoardProxy`(SceneProxy.h 追加) → CanvasView。MeshComponent 同型（`MeshComponent.cpp:186-247`） |
| RenderLayer マスク × CullingMask 交差判定 | 採用 | F3 | `CameraProxy.CullingMask`(`SceneProxy.h:280`) × `BoardProxy.LayerMask`、`HasFlag`(`RenderTypes.h:214`)、`CanvasView::PrepareBoardDrawCommands`（`GenerateDrawCommands` 中） |
| 共有カメラ（CameraId 参照） | 採用 | F2 | `CameraProxy.CameraId`(`SceneProxy.h:259`)、`RenderingCoordinator` カメラ表、`BuildViewportRenderPlan`(`RenderingCoordinator.cpp:40`) |
| 正射影 Viewport / px スクリーン座標 | 採用 | F2/F3 | `Viewport::UpdateMatrices` Orthographic、`board2d.vert`(新規)、§3.8 |
| 1 段合成（2D over 3D） | 採用 | F1/F3 | `CompositePass`（F1/F2 は passthrough、F3 で alpha-over）、`PresentationPass`(`PresentationPass.cpp:22`)、`RenderFrameExecutor`(`RenderFrameExecutor.cpp:11`) |
| 複数 Board / レイヤー優先度 / 安定ソート | 採用 | F4 | `BoardProxy::ComputeSortKey`、`std::stable_sort`（3D `DrawCommand.cpp:374` の非安定回避） |
| ブレンド / Tint / フリップ / ピボット / サイズ | 採用 | F5 | `BlendAttachmentDesc` PSO バリアント、`board2d.frag` |
| スプライトシート分割 / アトラス / UV 矩形 | 採用 | F6 | `BoardProxy.UV`、`Board::ComputeSpriteSheetUVRects`、`TextureResources` |
| テクスチャアニメ（フリップブック） | 採用 | F7 | `BoardComponent::Tick`、F6 UV 機構 |
| インスタンス化バッチング | 採用 | F8 | `FramePacket.InstanceData`、`AppendRebasedDrawCommands`(`RenderingCoordinator.cpp:880`)、`DrawIndexedInstanced` |
| ビルボード（WorldSpace、3D 深度共有） | 採用 | F9 | `BillboardComponent : BoardComponent`、SceneView Forward + `Scene.Depth`(`RenderGraphResourceNames.h:16`) |
| 方式 B（レイヤー RT + 不透明度） | 採用 | F10 | `Viewport::m_OutputTexture`、CanvasView per-layer RT |
| インポスター（ベイク + octahedral + LOD） | 採用 | F11 | `ImpostorComponent : BillboardComponent`、`MeshProxy.LODLevel`(`SceneProxy.h:47`) |
| prefab シリアライズ（全 Board 種） | 採用 | F3+ | `REFLECTION_CLASS`/`PROPERTY`、`FieldInitializer`(MeshComponent.h:50) |
| SDF テキスト / 9 スライス / タイルマップ / 2D パーティクル / オフラインアトラスパッキング | 不採用 | — | §4 Non-Goals（描画方式が根本的に異なる/別サブシステム） |
| World→多 View の完全一般化 | 不採用（段階導入） | — | §3.5（2 View 固定ルーティングに限定） |

---

## 7. オーケストレーション / 役割分担とモデル選定 + 共有ファイル編集プロトコル

`CLAUDE.md` のマルチエージェント方針とモデルティア方針に従う。

- オーケストレーターは工程分割・設計判断・順序付け・スコープ割り当て・結果統合・検証実行・コミット境界管理・最終受け入れを担う。具体的フェーズ計画作成・ファイル編集・自分が監督した実装のレビューは行わず、サブエージェントへ割り当てる。
- **実装担当とレビュー担当は必ず別エージェント。**
- 計画作成・計画レビュー・実装レビューは**必ず最上位ティア**。実装は既定で下位ティアだが、本計画は **F1/F2/F3/F9/F11**（World 同期・Proxy レイアウト・ルーティング・カメラ共有・新 View 種別・present 合成段・RenderGraph パス・PSO・RHI・Resource 寿命のいずれかに該当）を難易度ベース昇格ルールで**上位〜最上位へ昇格**を既定とする。F0 調査・行末修復・定型修正・テスト雛形は最安〜中位で十分（設計理解を伴う調査は中位以上）。
- **共有ファイルは単一オーナーまたはオーケストレーターが順次編集**する。共有ファイル: `SceneProxy.h`（BoardProxy）/ `World.h`/`World.cpp`（SyncEntityRecursive・ScreenSpace sink）/ `RenderingCoordinator.h`/`RenderingCoordinator.cpp`（カメラ表・GenerateDrawCommands）/ `ViewRenderPlan.h` / `FramePacket.h` / `RenderGraphResourceNames.h` / `PresentationPass.cpp` / `RenderFrameExecutor.cpp` / `Viewport.h`/`Viewport.cpp` / `RenderTypes.h` / `Library/Core/CMakeLists.txt`。サブエージェント間で書き込みスコープを重複させない。
- 各フェーズは先行依存フェーズが実装レビューと検証ゲートを通過するまで後続実装を始めない。エンジンクリティカル印付き（F1/F2/F3/F9/F11）は計画レビュー + 実装レビューを別の最上位ティアエージェントが必須実施。

---

## 8. 守るべき制約（要約）

- **2D は World 経由**（BoardComponent は Entity の Inner、`SyncEntityRecursive` で BoardProxy 化）。旧版の Component 非依存バイパス案・`LayerView` 名を残さない。
- **ルーティングは RenderLayer マスク × CullingMask 方式 A**（GameThread オーサリング時に決定、`HasFlag(camera.CullingMask, proxy.LayerMask)`）。BoardComponent に単一ターゲット Viewport を持たせる方式 B は不採用。
- **カメラは CameraId 共有データ**（`CameraProxy` がカメラ実体、新 live Camera クラスを作らない）。スナップショット `ViewportRenderPlan.Camera` は値コピー維持（RenderThread はポインタ追跡しない）。
- **View 名は `CanvasView`。** 合成は前回確定維持で 1 段、方式 A 既定 / B opt-in、順序 2 階層 + 明示安定ソート（3D の非安定 `std::sort` 経路に Board を載せない）。
- **二重描画防止:** BoardProxy を SceneView の MeshProxy 経路に載せない。SceneView は MeshProxy を無条件収集する従来動作を維持。
- **独自型 / スタイル / RHI 境界 / 所有権 / スレッド規律を厳守。** 新 Manager でなく `GEngine`/`RenderWorld`/`RenderingCoordinator` のメンバ。World→多 View 化は段階導入（2 View 固定ルーティング）。
- 新規ソースは UTF-8+BOM+CRLF、シェーダーは UTF-8+CRLF（BOM なし）。既存ファイル編集後は `git diff --numstat` と `--ignore-cr-at-eol` を比較し EOL drift を修復。CMake 登録はフェーズ内完結。
- エンジンクリティカル変更（World 同期・Proxy レイアウト・ルーティング・カメラ共有・新 View 種別・FramePacket レイアウト・present 合成段・RenderGraph パス・PSO・RHI・Resource 寿命）は実装レビュー必須。

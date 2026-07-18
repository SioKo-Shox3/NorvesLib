# NorvesLib エンジンロードマップ（M1〜M9）

作成日: 2026-07-02 ／ 最終更新: 2026-07-18 ／ ステータス: 進行中（M1〜M4完了、次はM5）

## 0. 現在地（2026-07-18）と実行順の再整理

- **M1「シーンSave/Load」完了**。シーン永続化は完了済みである。
- **M2「viewport.getThumbnail」完了・main に統合済み**。エディタの Game View 向けサムネイル取得を完了した。
- **M3「シャドウ結線・ライト」完了・main に統合済み**（`5d1bce01`、`8ae2f7c6`、`6ed4152f`）。シャドウ行列のライト追従と LightingPass の SSBO 化を完了した。
- **M4「メッシュクック＋リロード最小」完了**（`fddbba0e` でマイルストーンを閉鎖）。メッシュ cook・ランタイム経路・manifest reload は完了済みである。受容済み non-blocking リスクは [閉鎖記録](../Performance/AssetLoadPostCookComparison.md#known-accepted-risks) の2件（`InstanceBufferRing` の `uint32` 理論上限、profile 行番号 allowlist の未規定範囲）に限る。
- **現在の次工程は M5**。M1〜M4 は完了済みであり、M5 のテキスト描画・パーティクル・デバッグUIへ着手する。
  | 順 | M | 依存 | 独立性 | 備考 |
  |---|---|---|---|---|
  | 完了 | M2 ビューポート | なし（RHI/Rendering 単独） | 高 | viewport.getThumbnail を main へ統合済み |
  | 完了 | M3 シャドウ結線・ライト | なし（Rendering 単独） | 高 | shadow追従とライトSSBO化を main へ統合済み |
  | 完了 | M4 メッシュクック | なし（Asset 単独） | 中 | cook・runtime・reload を完了し、`fddbba0e` で閉鎖済み |
  | 次 | M5 テキスト/パーティクル/デバッグUI | M3 の絵作り土台があると映える | 中 | 各項独立フェーズ |
  | 5 | M6 スクリプティング(AngelScript) | **M1**（ScriptComponent がシーン永続化に乗る＝完了済み） | 中 | 第2部の入口 |
  | 6 | M7 固定タイムステップ | なし（M8 の前提工事） | 中 | エンジンクリティカル |
  | 7 | M8 最小物理 | **M7** 必須 | 低 | M7 通過後 |
  | 8 | M9 アニメ＋オーディオ | なし（M4 のメッシュ経路を流用可） | 中 | 2 サブ機能は並行可 |
  - M2/M3/M4 は互いに疎で並行着手できるが、**共有ファイル（中央 CMake・公開ヘッダ）の順次編集はオーケストレーターが単一オーナーで管理**すること。M6 の前提（M1）は達成済み。
- **M1〜M4 で得た運用知見（M5 以降に必ず適用）**:
  1. **計画駆動・完成コード転写が有効**: 計画書に完成コードを全文埋め込み → 実装は「正確な転写＋検証」に還元できた。エンジンクリティカル箇所ほど計画段階でコードを固め切る。
  2. **レビューゲートは実利がある**: M1 の二重レビューが実バグ3件（LoadedRoots未加算・LoadIntoWorldの二重カウント・インデント崩れ）を捕捉。計画レビュー・実装レビューを省略しない。
  3. **実装委譲と停止規則を守る**: 実装は `implementer` サブエージェントへ委譲し、同一手法で2回失敗したら反復せず停止して証拠を添えて報告する。以後の判断は上位モデルへの昇格または別手段の相談で仕切り直す。
  4. **行末・BOM の機械検証を常設**: 新規ソースは UTF-8+BOM+CRLF、既存編集後は `git diff --numstat` と `--ignore-cr-at-eol --numstat` の一致を毎回確認（本リポジトリは CRLF/LF 混在）。
  5. **テスト生成物の掃除**: ファイル書き出しテストは CWD に成果物を残し得る。手動exe実行後は `git status` で生成物を掃除してからコミット。

## 1. 目的と前提

本書は NorvesLib を「ある程度使えるゲームエンジン」に仕上げるための全体ロードマップである。

- **ゴール像**: 第1部で「3D技術デモ＋NorvesEditor連携」の完成度を上げ、第2部でゲームプレイ基盤（スクリプティング・固定タイムステップ・物理・アニメ・オーディオ）を積む。
- **構成方式**: 体験マイルストーン駆動。各マイルストーン（M）は「デモ可能な体験」で完結し、独立してレビュー・検証・コミットできる。
- **NorvesEditor との関係**: NorvesEditor はエンジン非依存の汎用エディター（Tauri + TypeScript、WebSocket/JSON Bridge Protocol 0.2）。エンジン側は Bridge capability を段階的に増やす「エディター機能を外付けできるエンジン」路線を維持する。
- **サードパーティ方針**: 領域ごとに着手時判断（本書では各Mに選択肢を併記）。ただしスクリプティング言語は比較調査の結果 **AngelScript に決定済み**（§5 M6 の決定記録を参照）。
- **運用**: 各Mの実装は CLAUDE.md のマルチエージェント・オーケストレーション（計画→計画レビュー→実装(implementerサブエージェント)→実装レビュー(Claude一次+Codex二次)→統合・検証・コミット）に従う。本書はロードマップであり、各M着手時に個別の実装計画書を `Docs/Plans/` に別途作成する。

## 2. 現状評価サマリ（2026-07 調査時点）

NorvesLib はレンダリングとエンジン基盤に厚く投資された「3Dビューアー／レンダリング研究エンジン」であり、Bridge 連携は Protocol 0.2・20メソッドまで実装済み。不足機能の多くは「ゼロから作る」ではなく「既存部品の最後の結線」で埋まる。

| 領域 | 現状 | 欠けている「最後の一歩」 |
|---|---|---|
| シーン永続化 | M1 で JSON writer・SceneSerializer・World の Save/Load 入口・round-trip を完了 | 完了済み。将来の形式拡張は個別Mで扱う |
| ビューポート連携 | M2 で readback・キャプチャ・PNG・Bridge による `viewport.getThumbnail` を完了 | 完了済み。将来のライブビュー/ギズモは別設計で扱う |
| レンダリング | M3 でシャドウのライト追従と LightingPass の SSBO 化を完了。RenderGraph、2D、ImGui モジュールも実装済み | M5 のゲーム内テキスト描画・パーティクル・デバッグUI |
| アセット | M4 でメッシュ cook・ランタイム解決経路・manifest reload を完了。テクスチャ経路も cook（NVTEXv0）→ manifest → 非同期ロードまで完成 | 受容済み non-blocking リスクを除き、M4 の最小スコープは完了 |
| ゲームプレイ基盤 | GameMode/入力/Tick経路/Module/BVHクエリ/リフレクション動的呼び出しは実用水準 | 固定タイムステップ無し。物理・スケルタルアニメ・オーディオ・スクリプティングは未着手 |

## 3. マイルストーン一覧

| M | 体験（デモできること） | 主な領域 | 状態 |
|---|---|---|---|
| M1 | 編集が消えないエディタ — シーン Save/Load | 永続化 | ✅ 完了 |
| M2 | エンジンが見えるエディタ — viewport.getThumbnail | Editor連携 | ✅ 完了 |
| M3 | 動くライトと正しい影 — シャドウ結線・ライト拡張 | レンダリング | ✅ 完了（main統合済み） |
| M4 | アセットが流れるエンジン — メッシュクック＋リロード最小 | アセット | ✅ 完了（`fddbba0e`で閉鎖、受容済みnon-blockingリスクあり） |
| M5 | デモが映える画面 — テキスト描画・パーティクル・デバッグUI | レンダリング/2D | ▶ 次 |
| M6 | スクリプトで動く世界 — AngelScript 統合 | スクリプティング | 未着手（前提M1達成） |
| M7 | 時間が正しいエンジン — 固定タイムステップ | 基盤 | 未着手 |
| M8 | 触れる世界 — 最小物理/衝突応答 | 物理 | 未着手（M7依存） |
| M9 | 動き、鳴る世界 — スケルタルアニメ＋オーディオ | アニメ/音 | 未着手 |

依存関係: M1→M6（ScriptComponent のプロパティ保存）、M7→M8（物理は固定ステップ前提）以外は疎。M1〜M4 は完了済みであり、次は M5 に着手する。ただし共有ファイル（公開ヘッダ・中央CMake）の編集は単一オーナー原則を守ること。

## 4. 第1部: エディタと絵作り

### M1 「編集が消えないエディタ」— シーン Save/Load

**完了した目的（Phase 1〜4）**: Core が Entity階層・Component・プロパティのシーンを JSON ファイルへ保存し、再ロードできるようにする。リソース参照の永続化と NorvesEditor/ImGui からの保存・読込は、M1完了後の別フェーズへ分離した。

**既存の土台**:
- `Library/Core/Public/Object/SchemaProjection.h` — `EntitySubtreeSnapshot`（FormatVersion=1、StableClassId/StablePropertyId/SerializedValue）
- `Library/Core/Private/Object/World.cpp` — `World::SpawnPrefab`（ClassRegistry 解決→NewInstance→DeserializeStable→AddInner、失敗時 TRawObjectGuard 全ロールバック）
- `Game/Bridge/NorvesLibBridgeAdapter.cpp` — `scene.duplicateObject` が capture→restore の完全な往復パターン
- `Library/Core/Public/FileStream/IFileStream.h` — 書き込み対応済み

**完了フェーズ（Phase 1〜4）**:
1. JSONライター新設（`Core::Text`。既存 `JsonDocument::TryParse` との往復テストを CTest 登録）
2. `SceneSerializer` — `EntitySubtreeSnapshot`⇔JSON 変換層。StableId（FNVハッシュ）に加えクラス名・プロパティ名の文字列を併記して改名耐性を確保。複数ルート Entity を束ねるシーンドキュメント構造を定義
3. enum シリアライズ対応 — `RuntimeSchema.h` の `Detail::SerializeValue/DeserializeValue` に `is_enum_v` 分岐を追加
4. 保存/復元の入口 API — 保存: `World::GetRootEntities()`×スナップショット→JSON→IFileStream、復元: JSON→一時 PrefabAsset＋`SpawnPrefab` 経路。`SceneFileRoundTripTest` を CTest 登録

**後続へ分離した範囲（旧 Phase 5〜6）**:
1. リソース参照の最小永続化 — MeshComponent/BoardComponent のアセット論理パスと再解決
2. Bridge `scene.save`/`scene.load` と NorvesEditor/ImGui の保存・読込入口

**受け入れ基準（Phase 1〜4）**: `SceneFileRoundTripTest` が Core の保存→ロード→再スナップショット比較を通過する。リソース参照の再解決、Bridge `scene.save`/`scene.load`、NorvesEditor/ImGui 経由の受入は本Mの完了条件に含めない。

**設計注意と後続への引継ぎ**:
- 復元は raw new→AddInner の所有権移譲経路。TRawObjectGuard の全ロールバックパターンを厳守（AddInner 後の delete 禁止）
- 保存/ロードは GameThread 限定。後続の Bridge 統合では `DrainInbound` 文脈とライブ World 差し替えのフレーム境界を守る
- 復元後は全 ObjectId が再採番される。後続の Bridge 統合では `scene.treeChanged` とエディタ側再取得を組み込む
- 全か無か復元（1プロパティ不一致で部分木全体が失敗）→ ファイルロードでは未知プロパティ寛容モードを設ける
- 保存時に `bPendingDestroy` の Entity/Component をフィルタする（現状 SchemaProjection は無条件に walk する）

**検証**: `cmake --build build --config Debug --target Core SceneFileRoundTripTest` → `ctest --test-dir build -C Debug -R Scene`。

### M2 「エンジンが見えるエディタ」— viewport.getThumbnail

**目的**: エンジンのレンダリング結果を NorvesEditor の Game View に表示する（低頻度サムネイル。ライブビュー/ギズモは post-alpha の別設計）。

**既存の土台**:
- `Library/Core/Public/RHI/ICommandList.h` — `CopyTextureToBuffer` 定義済み（Vulkan 実装 `VulkanCommandList.cpp` は未実証）
- CPUAccessible バッファ＋Map/Unmap、`ResourceState::CopySource` 遷移は完備
- エディタ側仕様確定済み: JSON result 内 inline base64 PNG、raw 256KiB / 640x360 / ≤1fps pull（`NorvesEditor/bridge/spec/schema/methods/viewport.getThumbnail.*`）

**フェーズ分割案**:
1. RHI readback の単体実証 CTest（ソリッドカラー描画→Barrier(CopySource)→CopyTextureToBuffer→Map→ピクセル検証）
2. RenderingCoordinator にフレームキャプチャ機構 — GameThread `RequestFrameCapture()` → RenderFrame 内（presentation blit 後・overlay seam と同じ録画窓内）でコピー → フェンス完了後 `TryConsumeCapturedFrame()` で `VariableArray<uint8_t>`+width/height/format を返すダブルバッファ。キャプチャ対象テクスチャの usage に TransferSrc を付与。**エンジンクリティカル: 計画レビュー必須**
3. stb_image_write を `Library/ThirdParty/stb` に vendor。PNG エンコード＋CPU ダウンスケール（640x360、256KiB 超過時再縮小）。base64 は SDK 境界（Game/Bridge）で実装
4. `NorvesLibBridgeAdapter::viewportGetThumbnail` 実装＋capability 広告に `viewport.thumbnail` 追加
5. E2E — NorvesEditor の conformance fixture＋GameViewPanel 実機ポーリング

**受け入れ基準**: 実エンジン起動→エディタから約1fpsでサムネイル取得・表示。MT/ST 両レンダリング経路（`bEnableMultiThreadedRendering` 両値）で動作。

**リスクと設計注意**:
- FramePacket は GameThread→RenderThread の一方向規約。逆方向の画像返送は FramePacket を汚さず、RenderingCoordinator 直下の専用ダブルバッファで行う
- Screen::EndFrame はセマフォ同期の非同期 submit。Map 前にフェンス/フレーム遅延で GPU 完了を保証する
- RenderGraph transient リソースはフレーム内寿命。キャプチャは録画窓内でのコピーに限定する
- 画像バイト列は Core 内では NorvesLib 型で運び、base64/std::string 化は SDK 境界のみ
- ライブビューは pull 1fps 前提の本経路を高頻度化して実現してはならない（別トランスポート設計）

**検証**: readback CTest → 全ターゲットビルド → フル ctest（--timeout 180）→ E2E。

### M3 「動くライトと正しい影」— レンダリング拡充・第1弾

**目的**: エディタからライトを動かすと影が追従する。ライト数制限を実質撤廃する。

**既存の土台**: LightComponent 3種＋LightProxy、単一 2048x2048 シャドウマップ、RenderGraph 成熟。

**フェーズ分割案**:
1. シャドウライト結線修正 — LightProxy から最初の Directional ライトを選び、ShadowMapPass と LightingPass（`LightingPass.cpp` にハードコードされた方向/距離/ortho）へ同一のライト行列を共有ヘルパー経由で供給。効果対コスト最大
2. 新パス追加リハーサル — 小さなポストプロセス1本（Vignette/ColorGrading）を IRenderGraphPass ネイティブで追加し、手順を確立（パス実装→シェーダー→SceneView.cpp 登録→CMake→`ForwardPassPipelinePlacementTest` 固定値更新）
3. ライト SSBO 化 — `lighting.frag` の UBO `lights[16]` を SSBO へ移行（クラスタドライティングは将来の別M）
4. シャドウ改善（選択制）— ライト追従シャドウ範囲、余力があればカスケード

**受け入れ基準**: エディタから DirectionalLight の向きを編集すると影が追従するデモ。ライト16超のシーンが正しく描画される。

**リスク**: GPULightingParams/LightData は std140 とシェーダーの手動同期（`LightingParamsLayoutTest` 更新必須）。パス増減で `ForwardPassPipelinePlacementTest` の固定値更新。レンダーパス変更後は全ターゲットビルド→フル ctest。

### M4 「アセットが流れるエンジン」— メッシュクック＋ホットリロード最小

**目的**: メッシュをクック済み形式でロードし起動を高速化。エディタからアセットのリロードを起動できる。

Phase 1 design spec: [NVMESHv0](../Architecture/NVMESHv0.md).

**既存の土台**: NVTEXv0（`CookedTextureFormat.h`）が形式設計の完全テンプレート。TextureAssetResolver/Loader/Runtime/AsyncLoadQueue の4点セットがランタイム経路の複製元。`GLTFAnalyzer` の ModelStagingData→FinalizeModelStaging 分離が CPU/GPU 境界として流用可能。manifest の kind="model" はスキーマ予約済み（消費者ゼロ）。

**フェーズ分割案**:
1. nvmesh v0 形式設計 — 頂点レイアウト/インデックス/SubMesh範囲/bounds、事前クラスタ化データ（MegaGeometry Clusters）を焼き込むかを最初に決める。**設計文書を書き計画レビューに掛ける（エンジンクリティカル）**
2. AssetCook に MeshCooker 追加（glTF→nvmesh、TextureCooker と同型、FourCC 例: Msh0、CTest スモーク込み）
3. ランタイム Model 解決経路 — Texture 系パターン踏襲の Resolver/Loader/Runtime を新設し、resolve→ParseCookedMesh→FinalizeModelStaging へ接続。cooked-ready 時は glTF JSON パースをスキップ
4. manifest 再読込の外部トリガー — 既存 `LoadTextureAssetManifestFromJsonText`＋Generation bump を Bridge コマンドから呼べるように配線（「再ロード後の新規要求は新アセット」をまず成立させる。既存ハンドルの in-place 再バインドは FramePacket 寿命設計を伴うため別フェーズに分離）
5. 計測 — AssetLoadProfile＋`Scripts/SummarizeAssetLoadProfile.ps1` の手法をメッシュに適用し、クック前後比較を取る

**受け入れ基準**: クック済みメッシュでの起動時間短縮を計測値で示す。エディタ（Bridge）から manifest リロードを起動できる。

**受容済み non-blocking リスク**: [閉鎖記録](../Performance/AssetLoadPostCookComparison.md#known-accepted-risks) の2件のみを受容してM4を閉鎖した。`InstanceBufferRing` の instance/capacity 算術は `uint32` のため理論上 `UINT32_MAX` 超で狭窄/オーバーフローし得る。profile 行番号 allowlist は正のASCII十進数を受理し、先頭ゼロ・桁数の制約は未規定である。

### M5 「デモが映える画面」— レンダリング拡充・第2弾＋2D仕上げ

**目的**: 技術デモの画面を完成度高く見せるための表現力と、エンジン内デバッグUIの整備。

**フェーズ分割案**（各項は独立フェーズ、優先順は着手時判断）:
1. ゲーム内テキスト描画 — FreeType を ImGui 専用から Core 利用可能へ昇格（CMake）、フォントアトラス＋グリフ→`BoardProxy.UVRect` 変換。既存 Mesh2D 経路（`DrawCommand::CreateMesh2D`、ImGuiOverlayPass が実証済み）を流用しパス新設不要
2. パーティクル Phase 1 — CPU シミュレーション＋既存 Board インスタンスバッチング描画。GPU シム（RenderGraph WriteBuffer+Dispatch、MegaGeometryPass が参照実装）は将来の Phase 2 に分離。新規 Manager は作らず GEngine サブシステムとして追加
3. ImGui 標準エンジン統計ウィンドウ — RenderingCoordinator の Stats、RenderGraph デバッグダンプを IImGuiView で表示
4. IBL 強化（選択制）— プリフィルタ済みキューブマップ等

**受け入れ基準**: テキスト＋パーティクルを含むデモシーンが動き、ImGui でエンジン統計を確認できる。

## 5. 第2部: ゲームプレイ基盤

### M6 「スクリプトで動く世界」— AngelScript 統合

**決定記録（言語選定）**: Lua 5.4 / LuaJIT / QuickJS-ng / C#(.NET) / AngelScript / Wren / WebAssembly を「独自アロケータフック・リフレクション駆動バインディング・フレーム予算GC・ホットリロード・ツーリング・ライセンス/保守」の6基準で比較調査（2026-07）し、**AngelScript 2.38+**（zlib、GitHub: anjo76/angelscript）を採用。
決め手: (a) `RegisterObjectType/Method` が宣言文字列ベースの完全実行時APIで TypeRegistry からの機械登録に最適、(b) 参照カウント主体＋増分・世代別サイクルGC＋毎フレーム `GarbageCollect(asGC_ONE_STEP)` の公式運用がフレーム予算管理に候補中最良、(c) 静的型付け・C++ライク構文、(d) 採用実績（It Takes Two / The Finals / Urho3D 等）。
既知の代償: バス係数1（コミット固定ベンダリング＋自前フォーク覚悟）、汎用DAPデバッガ無し（自作は独立フェーズ）、SDK同梱 add-on が std 使用。次点は Lua 5.4（最も枯れた構成）、QuickJS-ng+TypeScript（NorvesEditor と言語統一）。

**フェーズ分割案**:
1. ベンダリング＋ビルド統合 — `Library/ThirdParty/angelscript`（MSVC x64 公式対応・CMake 同梱）。`asSetGlobalMemoryFunctions` でエンジンアロケータへ接続（プロセスグローバルな点とシングルトン禁止規約の整合を計画レビューで確認）
2. リフレクションブリッジ — TypeRegistry/`IFunction::Invoke` 走査→ `RegisterObjectType/Method/Property`（asCALL_GENERIC）機械登録。TSharedPtr と AS ハンドル(@)の寿命規則の突き合わせが設計の山場。Vector3 等の数学型は値型として直接登録（asCALL_GENERIC＋IFunction 二重ディスパッチのホットパス回避）。**エンジンクリティカル: 計画レビュー必須**
3. ScriptComponent — スクリプトモジュール/クラス名を String プロパティで保持（M1 のシーン永続化にそのまま乗る）。BeginPlay/Tick/EndPlay をスクリプトクラスへ転送。サブシステムの置き場所は Module 方式（`Library/Modules/AngelScript`、ImGui が先行例）を第一候補に Core 編入と比較
4. GC 運用＋計測 — 毎フレーム `GarbageCollect(asGC_ONE_STEP)` を Tick に組み込み、NORVES_STAT で GC 時間を常時計測
5. ホットリロード v1 — `DiscardModule`→再コンパイル→スクリプトインスタンス再生成（状態は C++ 側が正、スクリプト側は再起動の割り切り。serializer 相当の状態移行は v2）
6. エディタ統合（後続・独立フェーズ）— TypeRegistry から `as.predefined` 自動生成→angel-lsp で型付き補完。DAP デバッガ自作は別フェーズ

**受け入れ基準**: シーンに置いた Entity が AngelScript で毎フレーム動く。エディタから ScriptComponent のプロパティを編集できる。スクリプトファイル編集→リロードが再起動なしで反映される。

**リスク**: SDK add-on（scriptarray/dictionary/ContextManager/serializer）は std 使用 → 独自コンテナで再実装するか、Bridge 同様の SDK 境界例外として隔離するかを最初に判断。スクリプト実行は GameThread 限定・pcall 相当の例外境界でエンジン状態を保護。

### M7 「時間が正しいエンジン」— 固定タイムステップ

**目的**: 決定論的なシミュレーション更新の前提工事（M8 物理の前提）。

**内容**: `ApplicationProcessor::Tick`（現状可変 dt・0.1s クランプのみ）に固定タイムステップアキュムレータ（例: 60Hz）を導入し、`Component::FixedTick` / `IModule::FixedTick` フックを追加。既存のシミュレーションゲート（ShouldAdvanceSimulation）との相互作用整理、物理→Transform 書き戻し→SyncToSceneView の更新順序規約の制定、Module の World Tick 前後順序フック追加を含む。**エンジンクリティカル: 計画レビュー必須。** 描画補間（前回/今回 Transform の二重状態）を入れるかはこのMで判断（入れる場合は SyncToSceneView への波及が大きい）。

**受け入れ基準**: 固定ステップ更新の CTest（フレームレート変動下でステップ数が決定的）。既存デモの挙動非破壊。

### M8 「触れる世界」— 最小物理/衝突応答

**外部ライブラリ判断**: 着手時に (a) 自作最小（学習目的、`GeometryIntersection` 拡張の延長）vs (b) Jolt Physics 統合（MIT、実績）を判断。いずれも `Library/Modules/Physics` として Module 化し、Module 境界の汎用性を実証する。

**内容**: `Math/GeometryIntersection` を接触情報返却版（法線・貫通深度・接触点の out 構造体）へ拡張（既存 bool 版温存）、Capsule-Capsule/Capsule-OBB 追加、ColliderComponent/RigidBodyComponent＋物理専用ブロードフェーズ、OnOverlap/OnHit コールバック（Delegate 使用）。SceneQuery（描画 AABB ベース・借用ポインタ・毎フレーム再構築）は物理の代替にしない。

**受け入れ基準**: 箱/球/カプセルが床に落ちて積み上がるデモ。Raycast/Overlap が物理形状に対して動く。

### M9 「動き、鳴る世界」— スケルタルアニメ＋オーディオ

**スケルタルアニメ**: `GLTFAnalyzer` に skins/animations/JOINTS_0/WEIGHTS_0 パースを追加し、未使用の `VertexLayout::CreateSkinned` に結線。SkeletonResource/AnimationClipResource を参照カウント Resource 系に追加。ボーン行列パレットは FramePacket スナップショット経由で RenderThread へ（ライブ読み禁止）。行列規約（World=行ベクトル、シェーダー転送時の CopyToShaderData/Transpose）に厳密に従う。外部ライブラリ判断: 自作 vs ozz-animation（MIT）。

**オーディオ**: 描画を持たないサービス型 IModule の初実証として `Library/Modules/Audio` を新設。バックエンドは着手時判断（XAudio2 直 / miniaudio）。AssetSystem/AsyncFileStream でのファイルロード、AudioComponent（Entity 位置参照、3D減衰は後続）。

**受け入れ基準**: glTF のスキンメッシュがアニメーション再生され、効果音/BGM が鳴るデモ。

## 6. 横断事項

- **スレッド規律**: GameThread→RenderThread は FramePacket スナップショットのみ（M2 の画像返送・M9 のボーン行列が抵触しやすい設計点）。RenderThread からライブ World/SceneView を読まない。
- **二重エンジン構造**: World を所有し Tick するのは `Engine::Engine`（ApplicationProcessor 駆動）。NorvesEngine（値 `GEngine`）は描画系に加え `ResourceRegistry` / `AssetRegistry` / `ComponentDataRegistry` 等のグローバルサブシステムを所有する。World 紐付き機能は Engine::Engine 側へ。
- **テスト固定値**: レンダーパス増減時は `ForwardPassPipelinePlacementTest` の固定値更新が必須。パイプライン変更後は全ターゲットビルド→フル ctest（--timeout 180、テクスチャ系のフレーキータイムアウトは単体再実行で確認）。
- **独自型ルール**: 新規サードパーティ（AngelScript/stb_image_write/物理・オーディオ選定ライブラリ）は ThirdParty に閉じ、Core へは NorvesLib 型で境界変換。std の保持は SDK 境界（Game/Bridge）と同等の例外判断を明示的に行う。
- **行末/BOM**: 新規ソースは UTF-8+BOM+CRLF。既存編集後は numstat 比較。
- **将来オプション（本ロードマップ外）**: ライブビューポート（共有テクスチャ/ストリーミング）、クラスタドライティング、GPU パーティクル、WebAssembly ベースの mod/プラグインサンドボックス（wasmtime/WAMR。スクリプト第一言語としては不採用だが mod 基盤として有望）、Lua 5.5 系の追従判断。

## 7. 本書の更新ルール

- 各M着手時に個別実装計画書（フェーズ・検証コマンド・コミット境界つき）を `Docs/Plans/` に作成し、本書の該当節へリンクを追記する。
- Mの完了・スコープ変更・順序変更が起きたら本書を更新する（本書が唯一のロードマップ一次情報）。

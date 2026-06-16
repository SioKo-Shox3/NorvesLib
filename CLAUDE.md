# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

> **この文書は `AGENTS.md` と完全に同一内容で運用する。** Claude Code は `CLAUDE.md`、Codex は `AGENTS.md` を読むが、中身は常に揃える。一方を編集したら必ずもう一方へ同じ変更を反映すること。外部ファイル（旧 `.github/copilot-instructions.md` 等）は参照しない。この文書が唯一の一次情報。

---

## プロジェクト概要

NorvesLib は C++23 / Vulkan / CMake のゲームエンジン（Windows）。エンジン本体は単一の静的ライブラリ `Core`（Math・Memory・Thread・RHI・Rendering・Object など全機能を統合）で、`Game`（WIN32 実行ファイル）が利用する。旧来の個別モジュールは `Core` に統合済みなのでリンク先は `Core` だけ。

`Docs/` 以下にアーキテクチャ文書（`ObjectModel.md`, `RenderingFlow.md` 等）があるが**整備途中で最新でない可能性がある**。実コードを正とし、文書は補助として扱う。

---

## ビルドとテスト

**Vulkan SDK** が必須（`shaderc_combined` を含む）。Slang 系ニューラルシェーダーを触るときだけ configure 時に `SLANG_SDK_DIR` を指定する。

```powershell
cmake -B build -S . -G "Visual Studio 17 2022"            # 構成（初回）
cmake --build build --config Debug --target Game            # ゲームをビルド
cmake --build build --config Debug --target FramePacketManagerTest  # 単一テストをビルド
ctest --test-dir build -C Debug                            # 全テスト
ctest --test-dir build -C Debug -R FramePacketManagerTest  # 単一テストを実行
```

- テストは CTest 登録の単体実行ファイル（`*Test.cpp`、`assert` + `std::cout` で進捗表示）。追加時は該当 `Test/<Area>/CMakeLists.txt` に `add_executable` + `add_test` し `PRIVATE Core` をリンクする。
- コンテナ・スレッド・メモリ・パース・レンダリング調整・ファイル/アセットロードの挙動を変えたらテストを追加/更新する。
- `build/` 配下の生成物（ログ、VS プロジェクト等）は編集しない。CMake のソースリスト・ソース・テスト・ドキュメント・アセットを直接更新する。

---

## コーディング規約

### 必ず守る独自型ルール（最重要）

標準ライブラリの多くを禁止し独自実装を使う。`CoreTypes.h` を include すると `Container::` 接頭辞なしで使用可。

- **コンテナ**: `VariableArray`(=`Array`/`Vector`)・`FixedArray`・`List`・`Map`・`Set`・`UnorderedMap`・`UnorderedSet`・`String`・`StringView`・`Span`・`Deque`・`Queue` を使う。`std::vector`/`std::string`/`std::map` 等は禁止。
- **スマートポインタ**: `TUniquePtr`/`TSharedPtr`/`TWeakPtr` と `MakeUnique`/`MakeShared`。`std::unique_ptr`/`std::make_shared` 等は禁止。有効性チェックは `IsValid`/`IsNull`、キャストは `DynamicPointerCast`/`StaticPointerCast`。
- **SDK 境界での std 例外**: NorvesEditor 連携の Generic Bridge SDK（`Game/Bridge` 配下のアダプタ層）が SDK 公開 API から受け取る std 型（`make_websocket_server_transport` が返す `std::unique_ptr<ITransport>`、`recv()` の `std::optional<std::string>`、`emitEvent` やフレームの `std::string` 等）は、その SDK 境界コードに限り保持を許容する。NorvesLib 一般コード（Core/Rendering/Object 等）へは漏らさず、境界で NorvesLib 型へ変換する。Logger 等 Core が既に内部で使う std（`std::chrono` 等）は従来どおり許容。
- **文字列ハッシュキー**: `UnorderedMap<Identity, V, Identity::Hasher>` を使う。`String` をキーにしない（`Identity` は事前計算ハッシュで高速）。
- **ログ**: `Logging/LogMacros.h` のマクロ（`NORVES_LOG_INFO("Cat", "fmt %d", x)` / 簡易版 `LOG_INFO(...)`）。`printf`/`std::cout` はテスト実行ファイル以外で禁止。`_F` 付きサフィックスは使わない（統合マクロが書式対応）。
- **計測**: `Debug/Stats.h` のマクロ（`NORVES_STAT_FUNCTION` / `NORVES_STAT_SCOPE` 等）。手書き `std::chrono` 計測は禁止（Release で消える）。

### スタイル・命名

- **中括弧は必ず改行して配置**し、単一文でも常に使う（`if (x) return;` 不可）。インデントは 4 スペース。
- **ポインタ/参照は型側に付ける**（`void* p`、`Data& d`。`void *p` は不可）。
- メンバは `m_`、bool 用途変数は `b` 接頭辞（型が bool でなくても）、インターフェースは `I`、テンプレートクラスは `T`。
- 公開型・メソッドは PascalCase。名前空間は `NorvesLib::Thread` / `NorvesLib::Core` のようにネストし、終端に `} // namespace ...` コメント。
- ヘッダは `#pragma once`（インクルードガードは使わない）。ヘッダ内で `using namespace` 禁止。
- include 順: 対応ヘッダ → プロジェクト内 → 標準ライブラリ → サードパーティ。
- ヘッダは `Public/<Area>/`、実装は対応する `Private/<Area>/` に置く。
- C++23 のコンセプト/テンプレートを積極活用。`constexpr`/`if constexpr` を活かす。

### 行末コードの扱い（重要）

リポジトリは CRLF/LF 混在で `autocrlf=false`。**既存ファイルを編集したら** `git diff --numstat` と `git diff --ignore-cr-at-eol --numstat` を比較し、数値が食い違ったら編集で行末が書き換わっている＝コミット前に修復する。**新規ソースファイルは UTF-8 + BOM + CRLF** で作る（BOM が無いと MSVC が日本語コメントを CP932 と誤認し C2838 等で失敗する）。

---

## アーキテクチャの肝：所有権モデル

管理対象は全て `IUnknown` を継承し 2 系統に分かれる（編集前に把握必須）。

- **`Object`**（World 内・画面/ゲームロジック）: `World` → `WorldObject` → `Component` の階層。所有は **Inner/Outer**。`Outer` は非所有の親ポインタ、所有は親の Inner 配列が持つ。`AddInner()` が `true` を返したら所有権が移る（以後 `delete` 禁止）。Outer 破棄で Inner も連鎖破棄。生成は `World::SpawnObject<T>()` / `World::CreateComponent<T>(owner)`、破棄は即時 `RemoveObject`/`RemoveComponent` か遅延 `MarkForDestroy()`（次 `Tick()` で回収）。
- **`Resource`**（テクスチャ/メッシュ/マテリアル等のデータ）: World に属さず **参照カウント** で管理し、エンジン側のサブシステムが保持する。

**シングルトン禁止**（`static Instance& Get()` を作らない）。サブシステムはグローバルの `NorvesEngine GEngine`（`Engine/NorvesEngine.h` に `extern`、ポインタでなく実体）のメンバとして持ち、`GEngine` 経由で参照する（例: `GEngine.GetRenderingCoordinator()` / `GetRenderThread()` / `GetResourceRegistry()` / `GetAssetRegistry()`）。新規 `〇〇Manager` クラスは作らず `NorvesEngine` にサブシステムを足す。リフレクションは `REFLECTION_CLASS(Self, Base)` と `PROPERTY` を使い、`ClassId` は `ClassRegistry` のみが発行する。

---

## アーキテクチャの肝：レンダリングとスレッド

GameThread と RenderThread はフレーム単位のスナップショット越しにのみ通信する（詳細 `Docs/Architecture/RenderingFlow.md`）。

- `RenderWorld`（Game 側入口：フレーム進行・RenderThread 起動・Resize 保留）→ `RenderingCoordinator`（Screen/SceneView/DrawCommand/FramePacket/RHI 調整）→ `RenderThread`（スレッド管理と `RenderFrame()` 呼び出しのみ）。
- **`FramePacket`** が GameThread→RenderThread の 1 フレームスナップショット（`Empty→Writing→Ready→Queued→Reading→Recycling→Empty` の状態遷移）。RenderThread はライブの `SceneView`/`World` を読まずスナップショットのみ読む。
- **RHI 境界は厳格**: Rendering 層は抽象 `RHI::I*`（`IDevice`/`ICommandList`/`ISwapChain` 等）だけを見る。バックエンド固有実装は `Library/Core/Private/RHI/<Backend>/`（現状 `Vulkan/`）に閉じ込め、Rendering 層から `RHI/Vulkan/*` を include しない。バックエンドオブジェクトは `RHI::IDevice` のファクトリ経由で生成する。
- 各描画パスは `IViewPass` を実装。シェーダーは `Assets/Shaders/` から実行時コンパイル（パスは `NORVES_SHADER_DIR` 定義で注入、GLSL は shaderc、ニューラル系は任意で Slang）。

---

## ディレクトリ

```
Library/Core/Public/<Area>/    公開ヘッダ
Library/Core/Private/<Area>/   実装（Public と対応させる）
Game/                          WIN32 実行ファイル + 起動（WinMain.cpp, GameBoot, GameApplicationHandler）
Test/<Area>/                   CTest 実行ファイル（*Test.cpp）
Tools/AssetCook/               オフラインアセットクッカー（AssetCook ターゲット）
Assets/                        シェーダー等の非ソース資産は全てここ。Library 配下には置かない
Scripts/                       PowerShell 補助スクリプト
Docs/                          設計/利用文書（整備途中）
```

---

## コミットメッセージ

「正しいコミットメッセージとは何か」を毎回考えて書く。テンプレ的な一文で済ませない。

- **言語**: 主に日本語。命令形で「何をしたか」を書く（例: `FramePacketの状態遷移にReadingガードを追加する`）。
- **粒度**: 1 コミット 1 論理変更。無関係な変更を混ぜない。ステージ前に他者の作業・生成物を明示的に除外する。
- **件名（subject 行）**: その変更の本質を具体的に表す。曖昧語（「修正」「更新」単体）で終わらせず、対象と意図がわかる語を入れる。長さより正確さを優先する。
- **本文**: 自明でない変更には本文を付け、**なぜ**その変更が必要か（背景・問題）、**何を**どう変えたか、**影響範囲**（API/所有権/スレッド/寿命への波及）を書く。エンジンクリティカルな変更（Public API・RHI/Vulkan・RenderThread・メモリ・ジョブ・アセットロード・Object/Resource 寿命）は本文必須。
- **フェーズとの対応**: オーケストレーション作業ではコミットを工程（フェーズ）境界に合わせ、そのフェーズが独立して妥当になった時点でコミット・プッシュする。
- リモート設定が許せばコミット後に現在ブランチをプッシュする。

---

## マルチエージェント・オーケストレーション

非自明な作業はメインエージェント（オーケストレーター）が工程を分割し、サブエージェントに割り当てて進める。

### 役割分担の原則

- **オーケストレーター**が担うもの: 工程分割、設計判断、順序付け、スコープ割り当て、サブエージェント結果の統合、検証実行、コミット境界管理、最終受け入れ。
- オーケストレーター自身は **具体的なフェーズ計画の作成・ファイル編集・自分が監督した実装のレビューを行わない**。これらはサブエージェントに割り当てる。
- **実装担当とレビュー担当は必ず別エージェント**にする。
- 各工程は独立してレビュー・検証・コミットできる最小単位にする。クリーンに分割できない場合は理由を述べ、最小の実用的チェックポイントを定義する。先行する依存フェーズが実装レビューと検証ゲートを通過するまで、後続フェーズの実装を始めない。

### 工程（フェーズ）の流れ

1. **調査 (Investigation)** — 既存コード・依存・命名規約の把握。読み取り専用。
2. **計画 (Planning)** — フェーズごとに具体的な実装計画を書く。計画には次を含める:
   - フェーズの目的と期待される挙動変化。
   - 影響するモジュール・公開 API・具体的なファイル/ディレクトリ。
   - 実装方針（所有権/寿命ルール、依存方向、データ構造、スレッド前提、移行/互換戦略）。
   - そのフェーズで走らせるテスト/検証コマンド（focused ターゲット + CTest）。
   - リスクレベルと、エンジンクリティカルな変更のロールバック/封じ込め方針。
   - コミット境界（コミット可能になる条件）。
3. **計画レビュー (Plan Review)** — API 境界、所有権モデル、依存方向、リソース寿命、スレッド安全性、CMake/テスト登録、後方互換、コミット単位の妥当性を確認。エンジンクリティカルな変更（Public API・RHI/Vulkan・RenderThread 挙動・メモリ管理・ジョブスケジューリング・アセットロード・シェーダーパイプライン・Object/Resource 寿命）は、変更が小さく見えても必須。
4. **実装 (Implementation)** — 承認された計画に沿って編集。サブエージェントには支援するフェーズと目的、許可/禁止の書き込みパス、期待する出力形式（変更ファイル・実行した検証・未解決リスク・前提）、タスク種別（read-only/実装/テスト作成/レビュー）を明示する。書き込みスコープはサブエージェント間で重複させない。公開ヘッダ・中央 CMake・ビルド設定・シリアライズスキーマ・コア寿命管理など共有ファイルは単一オーナーかオーケストレーターが順次編集する。
5. **実装レビュー (Implementation Review)** — 実 diff を承認済み計画と突き合わせ、公開 API 形状、所有権/破棄経路、依存方向、共有ヘッダ影響、生成/ビルドファイルの除外、CMake 登録、テストカバレッジ、サブエージェント出力を確認。指摘は次に進む前に修正する。計画した検証コマンドを実行できなかった場合は理由と残存リスクを記録する。
6. **統合・検証・コミット** — サブエージェント出力は提案物として扱い、オーケストレーターが diff を統合観点で検査し、計画との一致・API 境界・所有権を確認し、関連検証を再実行してから受け入れる。フェーズ完了ゲート: 実装統合済み・実装レビュー完了・関連ターゲットがビルド・CTest/focused テストが通過・レンダリング/ゲーム可視の変更にはログ/スクショ（実用的なら）。タスク完了時は全フェーズをコミットするか、理由を述べて意図的に未コミットとし、作業ツリーの残状態を報告する。

### モデル選定

**判断の質が後工程全体を左右する役割（計画・レビュー＝品質保証）に、その時点で利用可能な最上位モデルを使い、手を動かす分割タスクでコストを下げる**のが基本方針。手戻りがコストになる難所では下位モデルをケチらない。

モデルのラインナップは時期により変わるため、**特定のモデル名に依存せず「最上位／上位／下位／最安」の相対ティアで判断する**。各役割には「その時点で利用可能な中で該当ティアの最良モデル」を割り当てる。下表のモデル名はあくまで現時点の目安で、利用可能なモデルが変わったら読み替える。

| 役割 | モデル方針（ティア） |
|------|-----------|
| オーケストレーター（メイン） | 最上位〜上位。判断の質を優先する。 |
| 計画作成 | **必ず最上位（その時点で最も高性能なモデル）。** 計画の誤りは全工程に波及するため妥協しない。 |
| 計画レビュー | **必ず最上位。** 妥協しない。 |
| 実装レビュー | **必ず最上位。** 妥協しない。 |
| 実装 | 既定は下位でコストを下げる。下記の昇格条件に該当するなら上位〜最上位へ。 |
| 調査・機械的作業（grep 調査、行末修復、定型修正） | 最安で十分。設計理解を伴う調査は中位以上。 |

> 現時点のティア目安（変動するので都度確認）: 最上位 = Opus 系の最新、下位 = Sonnet 系、最安 = Haiku 系。`/model` や各ツールのモデル指定で利用可能なものを確認すること。

**難易度ベースの昇格ルール**: 実装・調査タスクでも、次のいずれかに該当し、間違えたときの手戻りコストが大きいものは上位〜最上位モデルへ昇格を検討する — Public API 変更、RHI/Vulkan、スレッド/同期、所有権・寿命管理、ジョブスケジューリング、アセットロード、シェーダーパイプライン、Object/Resource 寿命。「安いモデルで手戻りを繰り返す」より「最初から上位モデルで一発で通す」方が総コストが低い場面では迷わず昇格する。

---

## MCP / 外部参照

- **GitHub**: PR 要約・レビューコメント・Issue トリアージ・ブランチ/PR メタデータ・CI 調査など GitHub の状態に依存するタスクは、ローカル推測の前に GitHub MCP / 連携アプリの情報を優先する。個人アクセストークンやハードコードした認可ヘッダをコミットしない。
- **ドキュメント検索**: ライブラリ/フレームワーク/API/ツールのドキュメントは Context7 MCP を最初に使う。より広い調査・最新リリースノート・ベンダーページ・標準仕様は組み込みのウェブ検索を使う。技術的主張は一次情報（公式ドキュメント・仕様・ベンダーのリリースノート・ソースリポジトリ・標準文書）を優先し、二次まとめに当たったら原典まで辿る。
- MCP の認証情報はリポジトリに置かない。ユーザーレベルの環境変数かホスト管理のプロンプトを使う。

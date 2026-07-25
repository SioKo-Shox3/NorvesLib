# アーキテクチャ

> `CLAUDE.md` / `AGENTS.md`（working agreement）から移設した詳細規約。本体には索引だけを残し、全文はここが正本。

---

## アーキテクチャの肝：所有権モデル

管理対象は全て `IUnknown` を継承し 2 系統に分かれる（編集前に把握必須）。

- **`Object`**（World 内・画面/ゲームロジック）: `World` → `Entity` → `Component` の階層。所有は **Inner/Outer**。`Outer` は非所有の親ポインタ、所有は親の Inner 配列が持つ。`AddInner()` が `true` を返したら所有権が移る（以後 `delete` 禁止）。Outer 破棄で Inner も連鎖破棄。生成は `World::SpawnObject<T>()` / `World::CreateComponent<T>(owner)`、破棄は即時 `RemoveObject`/`RemoveComponent` か遅延 `MarkForDestroy()`（次 `Tick()` で回収）。
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
Docs/Plans/                    ローカル作業メモ。**リポジトリ管理外**（.gitignore 対象、
                               ユーザー判断 2026-06-15）。ここの計画・ロードマップは
                               各自の作業用で、共有されない前提で読む。**force-add で
                               コミットしない**（PreToolUse の gitignore ガードが遮断する）。
                               共有すべき恒久文書は Docs/ の他の配下（Architecture 等）へ置く。
```

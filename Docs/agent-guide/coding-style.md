# コーディング規約

> `CLAUDE.md` / `AGENTS.md`（working agreement）から移設した詳細規約。本体には索引だけを残し、全文はここが正本。

---

## 必ず守る独自型ルール（最重要）

標準ライブラリの多くを禁止し独自実装を使う。`CoreTypes.h` を include すると `Container::` 接頭辞なしで使用可。

- **コンテナ**: `VariableArray`(=`Array`/`Vector`)・`FixedArray`・`List`・`Map`・`Set`・`UnorderedMap`・`UnorderedSet`・`String`・`StringView`・`Span`・`Deque`・`Queue` を使う。`std::vector`/`std::string`/`std::map` 等は禁止。
- **スマートポインタ**: `TUniquePtr`/`TSharedPtr`/`TWeakPtr` と `MakeUnique`/`MakeShared`。`std::unique_ptr`/`std::make_shared` 等は禁止。有効性チェックは `IsValid`/`IsNull`、キャストは `DynamicPointerCast`/`StaticPointerCast`。
- **SDK 境界での std 例外**: NorvesEditor 連携の Generic Bridge SDK（`Game/Bridge` 配下のアダプタ層）が SDK 公開 API から受け取る std 型（`make_websocket_server_transport` が返す `std::unique_ptr<ITransport>`、`recv()` の `std::optional<std::string>`、`emitEvent` やフレームの `std::string` 等）は、その SDK 境界コードに限り保持を許容する。NorvesLib 一般コード（Core/Rendering/Object 等）へは漏らさず、境界で NorvesLib 型へ変換する。Logger 等 Core が既に内部で使う std（`std::chrono` 等）は従来どおり許容。
- **文字列ハッシュキー**: `UnorderedMap<Identity, V, Identity::Hasher>` を使う。`String` をキーにしない（`Identity` は事前計算ハッシュで高速）。
- **ログ**: `Logging/LogMacros.h` のマクロ（`NORVES_LOG_INFO("Cat", "fmt %d", x)` / 簡易版 `LOG_INFO(...)`）。`printf`/`std::cout` はテスト実行ファイル以外で禁止。`_F` 付きサフィックスは使わない（統合マクロが書式対応）。
- **計測**: `Debug/Stats.h` のマクロ（`NORVES_STAT_FUNCTION` / `NORVES_STAT_SCOPE` 等）。手書き `std::chrono` 計測は禁止（Release で消える）。

## スタイル・命名

- **中括弧は必ず改行して配置**し、単一文でも常に使う（`if (x) return;` 不可）。インデントは 4 スペース。
- **ポインタ/参照は型側に付ける**（`void* p`、`Data& d`。`void *p` は不可）。
- メンバは `m_`、bool 用途変数は `b` 接頭辞（型が bool でなくても）、インターフェースは `I`、テンプレートクラスは `T`。
- 公開型・メソッドは PascalCase。名前空間は `NorvesLib::Thread` / `NorvesLib::Core` のようにネストし、終端に `} // namespace ...` コメント。
- ヘッダは `#pragma once`（インクルードガードは使わない）。ヘッダ内で `using namespace` 禁止。
- include 順: 対応ヘッダ → プロジェクト内 → 標準ライブラリ → サードパーティ。
- ヘッダは `Public/<Area>/`、実装は対応する `Private/<Area>/` に置く。
- C++23 のコンセプト/テンプレートを積極活用。`constexpr`/`if constexpr` を活かす。

## 行末コードの扱い（重要）

リポジトリは CRLF/LF 混在で `autocrlf=false`。**既存ファイルを編集したら** `git diff --numstat` と `git diff --ignore-cr-at-eol --numstat` を比較し、数値が食い違ったら編集で行末が書き換わっている＝コミット前に修復する。**新規ソースファイルは UTF-8 + BOM + CRLF** で作る（BOM が無いと MSVC が日本語コメントを CP932 と誤認し C2838 等で失敗する）。

# コーディング規約準拠: std版スマートポインタをNorvesLib専用エイリアスに移行

## 概要
.github/copilot-instructions.mdで定められたコーディング規約に従い、プロジェクト全体でstd版スマートポインタの直接使用を廃止し、NorvesLib専用のスマートポインタエイリアスに移行する。

## 背景
現在のコードベースでは、コーディング規約で禁止されている以下のstd版スマートポインタが直接使用されている：
- `std::unique_ptr`
- `std::shared_ptr` 
- `std::make_unique`
- `std::make_shared`

これらは以下のNorvesLib専用エイリアスに置き換える必要がある：
- `TUniquePtr<T>`
- `TSharedPtr<T>`
- `MakeUnique<T>()`
- `MakeShared<T>()`

## 修正対象ファイル
以下のファイルで違反が確認されている：

### 1. Game/Application.cpp
- `std::unique_ptr<NorvesLib::IApplication>` → `TUniquePtr<NorvesLib::IApplication>`
- `std::make_unique<GameApplication>()` → `MakeUnique<GameApplication>()`

### 2. Library/GameMode/Public/IStateMachine.h
- `std::unique_ptr<T>` → `TUniquePtr<T>`

### 3. Library/Core/Public/Object/TValue.h
- `std::unique_ptr<IValue>` → `TUniquePtr<IValue>`
- `std::make_unique<TValue<T>>()` → `MakeUnique<TValue<T>>()`

### 4. Library/RHI/Public/RHITypes.h
- `std::shared_ptr<IDevice>` → `TSharedPtr<IDevice>`
- その他のRHI関連スマートポインタ

### 5. Library/Memory/Public/IAllocator.h
- `std::shared_ptr<IAllocator>` → `TSharedPtr<IAllocator>`

## 作業内容
1. 各ファイルでstd版スマートポインタの使用箇所を特定
2. 適切なインクルード(`Container/PointerTypes.h`など)を追加
3. 型エイリアスとファクトリー関数を置き換え
4. 名前空間の適切な使用(`using namespace NorvesLib::Core::Container;`)
5. ビルドエラーの修正
6. テストの実行と動作確認

## 受け入れ基準
- [ ] 全てのstd版スマートポインタがNorvesLib専用エイリアスに置換されている
- [ ] 適切なインクルードが追加されている
- [ ] プロジェクト全体がエラーなくビルドできる
- [ ] 既存のテストが全て通る
- [ ] コーディング規約チェックツールでエラーが出ない

## 関連リソース
- [コーディング規約](.github/copilot-instructions.md#スマートポインタ)
- [PointerTypes.h](Library/Core/Public/Container/PointerTypes.h)

## 優先度
**High** - コーディング規約への準拠は必須

## ラベル
- refactoring
- coding-standards  
- high-priority

## 担当者
GitHub Copilot
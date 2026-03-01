# NorvesLib コーディング規約とCopilot指示書

## プロジェクト概要
NorvesLibは、C++23標準を使用したモダンなゲームエンジンライブラリです。
クロスプラットフォーム対応を目指し、現在Windows環境での開発を進めています。

## 基本方針

### 言語標準
- **C++23**を使用
- コンセプトとテンプレートを積極的に活用
- モダンC++の機能を優先して使用

### アーキテクチャ
- **モジュラー設計**: 各機能を独立したライブラリモジュールとして実装
- **インターフェース指向**: 抽象クラス(`I`プレフィックス)による抽象化
- **テンプレート活用**: 型安全性とパフォーマンスの両立

## コーディングスタイル

### 命名規則

#### ファイル名
- ヘッダーファイル: `.h`拡張子
- ソースファイル: `.cpp`拡張子
- パスカルケース使用: `VectorUtils.h`, `WindowsApplication.cpp`

#### 名前空間
- ネストした名前空間を使用: `NorvesLib::Math`, `NorvesLib::Core::Container`
- 名前空間の終了時にコメント記載: `} // namespace NorvesLib::Math`

#### クラス・構造体
- パスカルケース: `VectorUtils`, `Matrix4x4T`
- インターフェースクラス: `I`プレフィックス (`IApplication`, `IWindow`)
- テンプレートクラス: `T`プレフィックス (`TValue`, `TClass`)

#### 関数・メソッド
- パスカルケース: `Initialize()`, `GetLength()`, `CreateWindow()`

#### 変数
- キャメルケース: `commandLine`, `isRunning`
- **システムハンガリアン記法**を採用:
  - メンバー変数: `m_`プレフィックス (`m_hInstance`, `m_mainWindow`)
  - ブール値用途の変数: `b`プレフィックス (`bIsRunning`, `bInitialized`, `bEnabled`)
    - 型がboolでない場合でも、ブール値として使用する場合は`b`プレフィックスを使用
- プライベートメンバー: アンダースコア使用可 (`m_Function`)

#### 定数・マクロ
- 大文字とアンダースコア: `NORVES_ALIGN`, `RHI_API_VULKAN`
- 名前空間内定数: パスカルケース (`Constants::EPSILON`)

### 中括弧とコードブロック

#### 中括弧の配置
- **必ず改行してから中括弧を配置**
- 関数、クラス、if文、ループなどすべてのコードブロックで統一

```cpp
// 正しい記述
if (condition)
{
    // 処理
}

void Function()
{
    // 実装
}

class MyClass
{
public:
    void Method()
    {
        // 実装
    }
};

for (int i = 0; i < count; ++i)
{
    // 処理
}

while (condition)
{
    // 処理
}

// 避けるべき記述
if (condition) {
    // 処理
}

void Function() {
    // 実装
}
```

#### 例外的なケース
- 単一行の場合でも中括弧を使用し、改行する

```cpp
// 推奨
if (condition)
{
    return;
}

// 避ける
if (condition) return;
if (condition) { return; }
```

### インクルード規則

#### インクルード順序
1. 対応するヘッダーファイル（.cppファイルの場合）
2. プロジェクト内ヘッダー（相対パス）
3. 標準ライブラリヘッダー
4. サードパーティライブラリヘッダー

```cpp
#include "VectorUtils.h"           // 対応するヘッダー
#include "MathForward.h"           // プロジェクト内
#include "Core/Public/Container/Containers.h" // 他モジュール
#include <cmath>                   // 標準ライブラリ
#include <vulkan/vulkan.h>         // サードパーティ
```

#### プラグマの使用
- すべてのヘッダーファイルで`#pragma once`を使用
- インクルードガードは使用しない

### テンプレート・コンセプト

#### コンセプトの命名
- 型制約: `FloatVector`, `HasXYZ`
- 分かりやすい名前を使用

#### テンプレート引数
- 型パラメーター: `T`, `VectorT`, `ScalarT`
- 値パラメーター: 小文字 (`size_t N`)

```cpp
template <FloatVector VectorT>
static auto Length(const VectorT& v)
```

### メモリ管理

#### スマートポインタ
- 生のポインタは避け、**NorvesLib専用のスマートポインタエイリアス**を使用
- **必須**: `TUniquePtr<T>`, `TSharedPtr<T>`, `TWeakPtr<T>`を使用（`std::`版は使わない）
- ファクトリー関数: `MakeUnique<T>()`, `MakeShared<T>()`を使用（`std::make_`版は使わない）
- インクルード: `#include "CoreTypes.h"`（推奨）または`#include "Container/Containers.h"`

```cpp
// 推奨（CoreTypes.hインクルードで名前空間プレフィックス不要）
#include "CoreTypes.h"
auto uniquePtr = MakeUnique<MyClass>(args...);
auto sharedPtr = MakeShared<MyClass>(args...);
TWeakPtr<MyClass> weakPtr = sharedPtr;

// 避ける（std版の直接使用）
auto stdUnique = std::make_unique<MyClass>(args...);
auto stdShared = std::make_shared<MyClass>(args...);
```

#### スマートポインタユーティリティ
- 型安全性: `SmartPointer`, `UniquePointer`, `SharedPointer`コンセプトを活用
- 有効性チェック: `IsValid(ptr)`, `IsNull(ptr)`
- キャスト: `DynamicPointerCast<T>()`, `StaticPointerCast<T>()`
- 変換: `ToShared(std::move(uniquePtr))`

```cpp
template<SmartPointer PtrT>
void ProcessPointer(const PtrT& ptr)
{
    if (IsValid(ptr))
    {
        // 処理
    }
}
```

#### カスタムアロケータ
- `NorvesLib::Core::Container`名前空間のコンテナを使用
- メモリプールやカスタムアロケータを活用

#### コンテナの使用規則
- **必須**: `NorvesLib::Core::Container`以下のカスタムコンテナを使用
- **STLコンテナの直接使用を避ける**: `std::vector`, `std::list`, `std::map`などは使用しない
- **推奨**: `#include "CoreTypes.h"`をインクルードすることで、`Container::`プレフィックスなしで使用可能
- カスタムアロケータによる統一的なメモリ管理を実現

```cpp
// 推奨（CoreTypes.hインクルードで名前空間プレフィックス不要）
#include "CoreTypes.h"
VariableArray<int> numbers;        // std::vectorの代わり
List<MyClass> objects;             // std::listの代わり
Map<String, int> nameToId;         // std::mapの代わり
Set<String> uniqueNames;           // std::setの代わり
UnorderedMap<int, String> cache;   // std::unordered_mapの代わり
String text;                       // std::stringの代わり

// 避ける（STL直接使用）
std::vector<int> stdNumbers;
std::list<MyClass> stdObjects;
std::map<std::string, int> stdMap;
```

#### 利用可能なカスタムコンテナ
- `VariableArray<T>` (std::vectorの代替) / エイリアス: `Array<T>`, `Vector<T>`
- `FixedArray<T, N>` (std::arrayの代替)
- `List<T>` (std::listの代替)
- `Map<K, V>` (std::mapの代替)
- `Set<T>` (std::setの代替)
- `UnorderedMap<K, V>` (std::unordered_mapの代替)
- `UnorderedSet<T>` (std::unordered_setの代替)
- `String` (std::stringの代替)
- `StringView` (std::string_viewの代替)
- `Span<T>` (std::spanの代替)
- `Deque<T>` (std::dequeの代替)
- `Queue<T>` (std::queueの代替)
```

#### Identityの使用規則
- **ハッシュキーとして文字列を使う場合は`Identity`を使用する**
- `UnorderedMap`/`UnorderedSet`のキーに文字列が必要な場合は、`String`ではなく`Identity`を使用
- `Identity`は文字列からハッシュ値を事前計算し、高速な比較と検索が可能
- `Identity::Hasher`をハッシュ関数として指定

```cpp
// 推奨（Identityをハッシュキーとして使用）
#include "CoreTypes.h"

// ハッシュマップのキーにはIdentityを使用
UnorderedMap<Identity, int, Identity::Hasher> nameToValue;

// Identityの作成
Identity id("ResourceName");
nameToValue[id] = 42;

// 検索（高速なハッシュ比較）
auto it = nameToValue.find(Identity("ResourceName"));

// 避ける（Stringをハッシュキーに使わない）
UnorderedMap<String, int> stringMap;  // コンパイルエラーまたは非効率
```

#### Identityの用途
- リソースパスのキャッシュキー
- クラス名・プロパティ名の識別
- 高速な文字列比較が必要な場面
- **注意**: `Identity`は`IdentityPool`で内部管理され、同じ文字列は同じハッシュを返す

### プラットフォーム固有コード

#### 条件コンパイル
```cpp
#ifdef _WIN32
    // Windows固有の実装
#elif defined(__linux__)
    // Linux固有の実装
#endif
```

#### Windowsマクロ対策
```cpp
#define NOMINMAX  // min/maxマクロの無効化
#include <Windows.h>
```

## アーキテクチャパターン

### ファクトリーパターン
- プラットフォーム固有の実装を抽象化
- `ApplicationFactory`, `WindowsApplicationFactory`

### シングルトンパターンの禁止
- **クラス内でstaticインスタンスを持つシングルトンパターンは禁止**
- `〇〇Manager`などのシステムクラスは`GEngine`の下に実体を持たせる
- グローバルアクセスが必要な場合は`GEngine`経由で取得する

```cpp
// 禁止（クラス内でシングルトン）
class ResourceManager
{
public:
    static ResourceManager& Get()
    {
        static ResourceManager instance;
        return instance;
    }
private:
    ResourceManager() = default;
};

// 推奨（GEngineの下に実体を持つ）
class ResourceManager
{
public:
    ResourceManager() = default;
    // ... 通常のクラスとして実装
};

// Engine.h内
class Engine
{
public:
    ResourceManager& GetResourceManager() { return m_ResourceManager; }
private:
    ResourceManager m_ResourceManager;
};

// 使用時
GEngine->GetResourceManager().DoSomething();
```

### ポインタ演算子の記法
- **ポインタ演算子は型のすぐ後に付ける**（変数名との間にスペース）
- 参照も同様

```cpp
// 正しい記法
void* buffer = nullptr;
int* pValue = &value;
const char* str = "hello";
MyClass* pObject = new MyClass();
void Process(const Data* pData);
void Modify(Data& data);

// 避けるべき記法
void *buffer = nullptr;
int *pValue = &value;
const char *str = "hello";
```

### PIMPL (Pointer to Implementation)
- プライベートメンバーの隠蔽
- コンパイル時間の短縮

### RAII (Resource Acquisition Is Initialization)
- リソース管理をコンストラクタ/デストラクタで自動化
- スマートポインタの活用

## デバッグ・ログ・スタット機能

### ログ出力

#### 基本原則
- ログ出力には必ず`Logging/LogMacros.h`のマクロを使用する
- 直接の`printf`, `std::cout`等は**禁止**

#### ログマクロの使用
- **統合マクロを使用**: `_F`サフィックスは不要（自動でフォーマット対応）
- フォーマット引数があってもなくても同じマクロを使用

```cpp
#include "Logging/LogMacros.h"

// カテゴリ指定ログ
NORVES_LOG_INFO("MyCategory", "単純なメッセージ");
NORVES_LOG_INFO("MyCategory", "フォーマット: 値=%d, 名前=%s", value, name);

// 簡易ログ（カテゴリ="General"）
LOG_INFO("単純なメッセージ");
LOG_INFO("フォーマット: 値=%d", value);
```

#### ログレベル
| マクロ | 用途 |
|--------|------|
| `NORVES_LOG_TRACE` | 詳細なトレース情報（関数の入出力等） |
| `NORVES_LOG_DEBUG` | デバッグ情報（開発時のみ有用な情報） |
| `NORVES_LOG_INFO` | 一般的な情報（初期化完了、状態変更等） |
| `NORVES_LOG_WARNING` | 警告（問題ではないが注意が必要） |
| `NORVES_LOG_ERROR` | エラー（処理が失敗したが継続可能） |
| `NORVES_LOG_FATAL` | 致命的エラー（続行不可能） |

### デバッグ出力ユーティリティ

#### DebugOutputの使用
- 開発時のデバッグ情報出力には`Debug/DebugOutput.h`を使用
- 変数値の出力、コンテナの内容表示、関数トレースに活用

```cpp
#include "Debug/DebugOutput.h"

// 変数の値を出力
DEBUG_PRINT_VAR(myVariable);
DEBUG_PRINT_VAR_CATEGORY(myVariable, "MyCategory");

// コンテナの内容を出力
DEBUG_PRINT_CONTAINER(myVector);

// 関数の入出力をトレース（スコープベース）
void MyFunction()
{
    DEBUG_FUNCTION_TRACE();  // 関数の入出力を自動ログ
    // 処理...
}

// メモリ使用量の確認
DEBUG_LOG_MEMORY();
DEBUG_LOG_MEMORY_AT("処理後");
```

### スタット（パフォーマンス計測）

#### 基本原則
- **リリースビルドでは計測系は無効化される**
- 計測には`Debug/Stats.h`のマクロを使用
- 手動での`std::chrono`計測は避け、スタットマクロを使用

#### スタットマクロの使用

```cpp
#include "Debug/Stats.h"

// スコープ計測（スコープ終了時に自動でログ出力）
void MyFunction()
{
    NORVES_STAT_FUNCTION();  // 関数全体を計測
    // 処理...
}

// 名前付きスコープ計測
void ComplexFunction()
{
    {
        NORVES_STAT_SCOPE("初期化処理");
        // 初期化...
    }
    
    {
        NORVES_STAT_SCOPE("メイン処理");
        // 処理...
    }
}

// 手動タイミング計測（スタット構造体への保存用）
NORVES_STAT_TIME_START(myOperation);
// 処理...
NORVES_STAT_TIME_END(myOperation, stats.MyOperationTimeMs);

// カウンター
NORVES_STAT_INC(stats.DrawCalls);
NORVES_STAT_ADD(stats.TriangleCount, triangleCount);
```

#### レンダリングスタット
- レンダリング関連の計測は`Debug::RenderingStats`を使用
- `RenderingCoordinator`で管理される

```cpp
// スタット情報の取得
const auto& stats = coordinator.GetStats();
LOG_INFO("FPS: %.1f, DrawCalls: %u", stats.FPS, stats.DrawCalls);
```

#### 独自スタットの追加
- `Debug::IStats`を継承してカスタムスタットを作成可能
- 大規模なスタットは`StatsManager`への登録を検討

### 条件付きデバッグ機能

#### デバッグビルド専用マクロ
リリースビルドでは自動的に無効化されるマクロを使用：

```cpp
// デバッグビルドのみ有効
NORVES_DEBUG_LOG("Category", "デバッグ情報");
DEBUG_LOG("簡易デバッグ情報");

// スタット（リリースでは何もしない）
NORVES_STAT_FUNCTION();
NORVES_STAT_SCOPE("処理名");
```

### 避けるべきパターン

- `printf`, `std::cout`による直接出力
- 手動での`std::chrono`計測（スタットマクロを使用）
- リリースビルドで残る計測コード（条件付きマクロを使用）
- `_F`サフィックス付きログマクロ（統合版を使用）

## インスタンス管理アーキテクチャ

### IUnknown継承クラスの分類

NorvesLibで「誰かに管理されてほしいクラス」は、特殊な構造体などを除き全て`IUnknown`を継承します。

```
IUnknown
    ├── Object（World管理 - 画面に映るもの）
    │       ├── Actor（シーン内の配置可能オブジェクト）
    │       ├── Component（Actorにアタッチ）
    │       └── その他ゲームオブジェクト
    │
    └── Resource（GEngine管理 - データ/リソース）
            ├── TextureResource
            ├── MeshResource  
            ├── MaterialResource
            └── その他リソース
```

### Objectクラス

- **定義**: `World`に属し、画面に映る/ゲームロジックを持つオブジェクト
- **責任者**: `World`
- **寿命管理**: WorldのInnerとして親子付けされ、**Worldと寿命が一致**
- **リフレクション**: 特別な理由がない限り`REFLECTION_CLASS`マクロを使用

```cpp
// Objectは必ずWorldのInnerとして登録される
// World破棄時に全Objectが連鎖破棄される
class MyGameObject : public Object
{
    REFLECTION_CLASS(MyGameObject, Object)
public:
    // ...
};
```

### Resourceクラス

- **定義**: `World`に属さない、テクスチャ・メッシュ・マテリアル等のデータリソース
- **責任者**: `GEngine`（ResourceRegistry経由）
- **寿命管理**: **参照カウント方式** - 参照がなくなったら自動破棄
- **リフレクション**: 特別な理由がない限り`REFLECTION_CLASS`マクロを使用
- **特徴**: Worldが破棄されても参照があれば生存

```cpp
// Resourceは参照カウントで管理
// 誰からも参照されなくなったら自動破棄
class MyTextureResource : public Resource
{
    REFLECTION_CLASS(MyTextureResource, Resource)
public:
    // ...
};

// 使用例
TSharedPtr<MyTextureResource> texture = GEngine.GetResourceRegistry().Load<MyTextureResource>("path/to/texture");
// textureへの参照がなくなれば自動破棄
```

### Inner/Outer親子関係

- **Inner**: 子オブジェクト（所有される側）
- **Outer**: 親オブジェクト（所有する側）
- **寿命ルール**: Outerが破棄されたら、そのInner全ても破棄される

```cpp
// World -> Actor -> Component の親子関係
// World破棄 → Actor破棄 → Component破棄
```

### Managerパターンの回避

- **`〇〇Manager`クラスの作成は極力避ける**
- 代わりに**エンジンのサブシステム**として実装
- GEngine配下にサブシステムとして配置

```cpp
// 避ける
class MeshManager
{
public:
    static MeshManager& Get(); // シングルトン禁止
};

// 推奨
class NorvesEngine
{
public:
    ResourceRegistry& GetResourceRegistry();
    // RenderWorldなどもサブシステムとして保持
private:
    ResourceRegistry m_ResourceRegistry;
};
```

### 責任の所在まとめ

| 対象 | 責任者 | 寿命管理 | 説明 |
|------|--------|----------|------|
| Object | World | Inner/Outer親子関係 | 画面に映る/ゲームロジックを持つ |
| Resource | GEngine | 参照カウント | テクスチャ、メッシュ等のデータ |
| サブシステム | GEngine | GEngineと同じ | RenderWorld等のエンジン機能 |

## エラーハンドリング

### 例外の使用
- C++標準例外を基本とする
- カスタム例外は`std::exception`を継承

### アサーション
```cpp
#include <cassert>
assert(condition && "エラーメッセージ");
```

## ドキュメント

### コメント
- Doxygenスタイルのコメントを使用
- パブリックAPIには必ずドキュメントコメントを記載

```cpp
/**
 * @brief ベクトルの長さを計算
 * @param v 入力ベクトル
 * @return ベクトルの長さ
 */
template <FloatVector VectorT>
static auto Length(const VectorT& v)
```

### README・ドキュメント
- 各モジュールにドキュメントファイルを配置
- 使用例を含めた説明を記載

## モジュール構成

### ディレクトリ構造
```
Library/
├── Core/           // 基盤機能
├── Math/           // 数学ライブラリ
├── RHI/            // Rendering Hardware Interface
├── Memory/         // メモリ管理
├── Thread/         // スレッド・並行処理
├── Random/         // 乱数生成
└── GameMode/       // ゲームモード管理
```

### CMake規則
- 各モジュールに`CMakeLists.txt`を配置
- モジュール名は`MODULE_NAME`変数で統一
- `PUBLIC`/`PRIVATE`の適切な使い分け

## パフォーマンス

### 最適化指針
- `constexpr`の積極的な使用
- `if constexpr`による条件分岐の最適化
- インライン関数の適切な使用

### メモリ効率
- アライメントを考慮した構造体設計
- `NORVES_ALIGN(16)`マクロの使用
- 無駄なコピーを避ける（`std::move`, 完全転送）

## テスト

### ユニットテスト
- 各モジュールにテストを配置
- CTestフレームワークを使用

### 命名規則
- テストファイル: `ModuleNameTest.cpp`
- テスト関数: `Test_FunctionName_Condition`

## GitHub Copilot 向け指示

### コード生成時の注意点
1. **名前空間の一貫性**: `NorvesLib::`から始まる適切な名前空間を使用
2. **インクルードパス**: 相対パスでの適切なインクルード
3. **テンプレート使用**: コンセプトを活用した型安全なテンプレート
4. **メモリ管理**: スマートポインタの使用
5. **プラットフォーム対応**: 条件コンパイルの適切な使用

### 避けるべきパターン
- 生のポインタの直接使用
- **`std::`版スマートポインタの直接使用**（`std::unique_ptr`, `std::shared_ptr`, `std::make_unique`, `std::make_shared`など）
- **STLコンテナの直接使用**（`std::vector`, `std::list`, `std::map`, `std::string`など）
- **クラス内シングルトンパターン**（`static Instance& Get()`形式）
- **ポインタ演算子を変数名側に付ける**（`void *hoge`ではなく`void* hoge`）
- **`printf`, `std::cout`による直接出力**（`NORVES_LOG_*`マクロを使用）
- **手動での`std::chrono`計測**（`NORVES_STAT_*`マクロを使用）
- **`_F`サフィックス付きログマクロ**（統合版マクロを使用）
- **Stringをハッシュキーに使用する**（`UnorderedMap<String, ...>`ではなく`UnorderedMap<Identity, ..., Identity::Hasher>`を使用）
- `using namespace`の使用（ヘッダーファイル内）
- プラットフォーム固有コードの直接記述
- 古いC++スタイルの記述
- テンプレート特殊化の旧来記法（代わりにコンセプトを使用）

### 推奨パターン
- モダンC++の機能を積極的に使用
- **NorvesLib専用エイリアス**の使用（`TUniquePtr`, `TSharedPtr`, `MakeUnique`, `MakeShared`）
- **NorvesLib専用コンテナ**の使用（`VariableArray`, `List`, `Map`, `String`など）
- **ハッシュキーには`Identity`を使用**（高速な比較・検索が可能）
- **C++23コンセプト**によるテンプレート制約（SFINAE回避）
- RAII原則の遵守
- const correctnessの徹底
- 型安全性の確保

## バージョン管理

### .gitignore
- ビルド成果物は除外
- IDE固有ファイルは除外
- 一時ファイルは除外

### ブランチ戦略
- `main`: 安定版
- `develop`: 開発版
- `feature/*`: 機能開発

---

このドキュメントはプロジェクトの成長に合わせて更新されます。
新しい規約や変更点があれば、チーム内で議論し、このドキュメントに反映してください。
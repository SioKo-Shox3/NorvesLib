# ThreadLocalStorage 使用ガイド

## 概要

`ThreadLocalStorage`は、各スレッドが独自のデータインスタンスを持つことができるクラスです。これにより、スレッド間で共有変数を使わずに、各スレッドが自身のコピーを持つことができます。スレッド固有のデータは、グローバル変数や静的変数をスレッドごとに固有にしたいケースで役立ちます。

## 特徴

- **クロスプラットフォーム**: Windows (TLS API) と POSIX (pthread) の両方をサポート
- **テンプレートベース**: あらゆる型のスレッドローカルデータを作成可能
- **自動初期化**: 初期化関数をサポートし、最初のアクセス時に値を自動的に生成
- **自動リソース管理**: スレッド終了時やオブジェクト破棄時に自動的にリソースを解放

## 使用方法

### 基本的な使用例

```cpp
#include "Thread/Public/ThreadLocalStorage.h"

// int型のスレッドローカルストレージを作成
NorvesLib::Thread::ThreadLocalStorage<int> threadLocalInt;

// 現在のスレッド用の値を取得（最初のアクセスならデフォルト値で初期化）
int& value = threadLocalInt.Get();

// 現在のスレッド用に値を設定
threadLocalInt.Set(42);

// 設定した値が保存されていることを確認
int& retrievedValue = threadLocalInt.Get();
// retrievedValueは42
```

### 初期化関数を使用した例

```cpp
#include "Thread/Public/ThreadLocalStorage.h"
#include <random>

// 乱数生成器を初期化する関数
std::mt19937 CreateRandomGenerator() 
{
    // 各スレッド固有のシード値を作成
    std::random_device rd;
    return std::mt19937(rd());
}

// スレッドごとに別々の乱数生成器を持つTLS
NorvesLib::Thread::ThreadLocalStorage<std::mt19937> threadLocalRng(CreateRandomGenerator);

// スレッド固有の乱数生成器を使用
int GetRandomNumber(int min, int max)
{
    std::uniform_int_distribution<int> dist(min, max);
    return dist(threadLocalRng.Get());
}
```

### 複雑なオブジェクトの例

```cpp
#include "Thread/Public/ThreadLocalStorage.h"
#include <memory>

// スレッド固有のコンテキスト情報を保持するクラス
class ThreadContext
{
public:
    ThreadContext() : m_id(0), m_name("Default") {}
    
    void SetId(int id) { m_id = id; }
    void SetName(const std::string& name) { m_name = name; }
    
    int GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    
private:
    int m_id;
    std::string m_name;
};

// スレッドコンテキストのTLS
NorvesLib::Thread::ThreadLocalStorage<ThreadContext> threadContext;

void ConfigureCurrentThread(int id, const std::string& name)
{
    // 現在のスレッドのコンテキスト情報を更新
    ThreadContext& context = threadContext.Get();
    context.SetId(id);
    context.SetName(name);
}

void DoWork()
{
    // 現在のスレッドのコンテキスト情報を使用
    const ThreadContext& context = threadContext.Get();
    std::cout << "Thread " << context.GetId() << " (" 
              << context.GetName() << ") is working." << std::endl;
}
```

### マルチスレッド環境での使用例

```cpp
#include "Thread/Public/ThreadLocalStorage.h"
#include <thread>
#include <vector>

NorvesLib::Thread::ThreadLocalStorage<int> threadLocalCounter;

void WorkerFunction(int threadId)
{
    // このスレッド用にカウンターを初期化
    threadLocalCounter.Set(threadId * 100);
    
    // スレッド固有のカウンターを使用した処理
    for (int i = 0; i < 10; i++) {
        int& counter = threadLocalCounter.Get();
        counter++;
        std::cout << "Thread " << threadId << ": counter = " << counter << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main()
{
    constexpr int NUM_THREADS = 4;
    std::vector<std::thread> threads;
    
    // 複数スレッドを起動
    for (int i = 0; i < NUM_THREADS; i++) {
        threads.emplace_back(WorkerFunction, i);
    }
    
    // スレッドの終了を待機
    for (auto& t : threads) {
        t.join();
    }
    
    return 0;
}
```

### JobSystemとの組み合わせ例

```cpp
#include "Thread/Public/ThreadLocalStorage.h"
#include "Thread/Public/JobSystem.h"
#include <memory>

// ワーカースレッドごとのプロファイリングデータ
struct ProfileData
{
    size_t tasksProcessed = 0;
    size_t totalProcessingTimeMs = 0;
    
    void AddTaskProfile(size_t processingTimeMs)
    {
        tasksProcessed++;
        totalProcessingTimeMs += processingTimeMs;
    }
    
    double GetAverageProcessingTime() const
    {
        return tasksProcessed > 0 ? static_cast<double>(totalProcessingTimeMs) / tasksProcessed : 0.0;
    }
};

// スレッドごとのプロファイルデータを格納するTLS
NorvesLib::Thread::ThreadLocalStorage<ProfileData> threadProfiler;

// プロファイリング情報を収集するラッパータスク
template<typename TaskFunc>
auto CreateProfiledTask(TaskFunc func)
{
    return [func]() {
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // 実際のタスクを実行
        func();
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        
        // 現在のスレッドのプロファイルデータを更新
        ProfileData& profile = threadProfiler.Get();
        profile.AddTaskProfile(duration);
    };
}

// ジョブシステムを使って処理を実行し、プロファイリングする
void RunJobsWithProfiling()
{
    auto& jobSystem = NorvesLib::Thread::JobSystem::Get();
    
    // 複数のプロファイリング対象タスクを作成
    std::vector<std::shared_ptr<NorvesLib::Thread::Task>> tasks;
    
    for (int i = 0; i < 100; i++) {
        auto task = std::make_shared<NorvesLib::Thread::Task>(
            CreateProfiledTask([i]() {
                // 何らかの処理
                std::this_thread::sleep_for(std::chrono::milliseconds(i % 10));
            })
        );
        tasks.push_back(task);
    }
    
    // タスクをバッチ実行
    auto batchTask = jobSystem.SubmitTasks(tasks);
    
    // 完了を待機
    batchTask->Wait();
    
    // 結果の集計は別途行う必要がある
    // （各スレッドのTLSデータを収集するメカニズムが必要）
}
```

## 実装の詳細

1. **実装の選択**:
   - Windows環境では`TlsAlloc()`/`TlsGetValue()`/`TlsSetValue()`/`TlsFree()`を使用
   - POSIX環境では`pthread_key_create()`/`pthread_getspecific()`/`pthread_setspecific()`/`pthread_key_delete()`を使用

2. **リソース管理**:
   - Windows: `~ThreadLocalStorage()`でTLSインデックスを解放、各スレッドのデータのクリーンアップは制限あり
   - POSIX: `pthread_key_create()`に破壊関数を登録し、スレッド終了時に自動的にリソースを解放

## 注意点

1. **スレッド終了管理**: 
   - WindowsプラットフォームではスレッドのTLSデータの自動破棄に制限があります
   - 長時間実行するプログラムでは、スレッド終了時に明示的にTLSデータをクリーンアップすることを検討してください

2. **初期化エラー処理**:
   - TLS APIコールが失敗した場合、コンストラクタは例外をスローします
   - アプリケーションの起動時にTLSを初期化し、エラーを適切に処理してください

3. **TLS変数のスコープ**:
   - スレッドの生存期間を超えて存在する必要があるため、通常はグローバル変数または静的メンバーとして使用します
   - ローカル変数として使用する場合は、そのTLSオブジェクトを参照するすべてのスレッドがオブジェクトのライフタイム内で終了することを確認してください
# RingBuffer 使用ガイド

## 概要

`RingBuffer`は、単一プロデューサ・単一コンシューマ（SPSC）パターン向けに最適化された、ロックフリーで高性能なリングバッファ実装です。スレッド間のデータ交換やバッファリングに使用できます。

## 特徴

- **ロックフリー実装**: ミューテックスなどのロック機構を使用せず、アトミック操作のみで実装
- **SPSCに最適化**: 1つのスレッドが書き込み、別の1つのスレッドが読み取る用途に最適化
- **キャッシュ効率**: キャッシュラインの分割を回避する設計
- **固定サイズ**: コンパイル時に決定される固定サイズ（2の累乗サイズのみ）
- **トリビアルコピー可能な型に対応**: POD型や単純なオブジェクトのみをサポート

## 使用方法

### 基本的な使用例

```cpp
#include "Thread/Public/RingBuffer.h"

// 容量が1024（2の10乗）の整数用リングバッファを作成
NorvesLib::Thread::RingBuffer<int, 1024> buffer;

// データの書き込み（プロデューサースレッド）
bool success = buffer.TryWrite(42);
if (success) {
    // 書き込み成功
} else {
    // バッファが満杯
}

// データの読み取り（コンシューマースレッド）
int value;
if (buffer.TryRead(value)) {
    // 読み取り成功、valueには42が入っている
} else {
    // バッファが空
}
```

### マルチスレッド環境での使用例

```cpp
#include "Thread/Public/RingBuffer.h"
#include <thread>

NorvesLib::Thread::RingBuffer<int, 1024> buffer;

// プロデューサースレッド
auto producer = [&buffer]() {
    for (int i = 0; i < 1000; i++) {
        while (!buffer.TryWrite(i)) {
            // バッファが満杯なら少し待つ
            std::this_thread::yield();
        }
    }
};

// コンシューマースレッド
auto consumer = [&buffer]() {
    for (int i = 0; i < 1000; i++) {
        int value;
        while (!buffer.TryRead(value)) {
            // バッファが空なら少し待つ
            std::this_thread::yield();
        }
        // valueを処理...
    }
};

// スレッドの作成と実行
std::thread producerThread(producer);
std::thread consumerThread(consumer);

// スレッドの終了を待つ
producerThread.join();
consumerThread.join();
```

### JobSystemと統合した使用例

```cpp
#include "Thread/Public/RingBuffer.h"
#include "Thread/Public/JobSystem.h"
#include "Thread/Public/Task.h"

// ジョブシステムのタスク間でデータを共有するためのバッファ
NorvesLib::Thread::RingBuffer<std::shared_ptr<DataPacket>, 2048> dataQueue;

// タスク生成関数
void CreateDataProcessingPipeline()
{
    auto& jobSystem = NorvesLib::Thread::JobSystem::Get();
    
    // データ生成タスク
    auto dataProducerTask = std::make_shared<NorvesLib::Thread::Task>(
        [&dataQueue]() {
            for (int i = 0; i < 100; i++) {
                auto packet = std::make_shared<DataPacket>(/* データ */);
                while (!dataQueue.TryWrite(packet)) {
                    std::this_thread::yield();
                }
            }
        }
    );
    
    // データ処理タスク
    auto dataConsumerTask = std::make_shared<NorvesLib::Thread::Task>(
        [&dataQueue]() {
            for (int i = 0; i < 100; i++) {
                std::shared_ptr<DataPacket> packet;
                while (!dataQueue.TryRead(packet)) {
                    std::this_thread::yield();
                }
                // パケットを処理...
            }
        }
    );
    
    // ジョブシステムにタスクを登録
    jobSystem.SubmitTask(dataProducerTask);
    jobSystem.SubmitTask(dataConsumerTask);
}
```

## パフォーマンスのヒント

1. **バッファサイズの最適化**:
   - 使用するデータサイズとパターンに合わせて適切なサイズを選択してください
   - バッファサイズは常に2の累乗である必要があります（64, 128, 256, 512, 1024など）
   - より大きなサイズはプロデューサーとコンシューマーの速度差を吸収できますが、メモリ使用量が増えます

2. **キャッシュ効率**:
   - `RingBuffer`は内部でキャッシュラインサイズ（通常64バイト）を考慮したパディングを使用
   - 非常に小さなデータ型を格納する場合は、配列やバッチ処理を検討してください

3. **使用パターン**:
   - `TryWrite`/`TryRead`が失敗した場合は、即座にリトライするよりも`yield()`や短いスリープを挿入することでCPU負荷を軽減できます
   - 複数のプロデューサーまたは複数のコンシューマーがある場合、各プロデューサー/コンシューマーのペアごとに別々の`RingBuffer`を使用してください

## 制限事項

1. このリングバッファは単一プロデューサ・単一コンシューマ（SPSC）向けに設計されています
2. 複数のプロデューサーや複数のコンシューマーがある場合は、適切なロック機構を追加する必要があります
3. 格納する型は`std::is_trivially_copyable`を満たす必要があります
4. 2の累乗サイズのみサポートされます
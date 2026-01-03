#include "Thread/RingBuffer.h"
#include "Thread/Atomic.h"
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <cassert>
#include <iostream>

using namespace NorvesLib::Thread;

// シンプルな単一スレッドのテスト
void TestBasicOperations()
{
    std::cout << "Running basic operations test..." << std::endl;

    // 容量8のリングバッファを作成
    RingBuffer<int, 8> buffer;

    // 初期状態の確認
    assert(buffer.IsEmpty());
    assert(!buffer.IsFull());
    assert(buffer.GetSize() == 0);
    assert(buffer.GetCapacity() == 7); // 実際に格納できるのは(Capacity-1)個

    // 要素の追加
    for (int i = 0; i < 7; i++)
    {
        bool result = buffer.TryWrite(i);
        assert(result);
        assert(!buffer.IsEmpty());
        assert(buffer.GetSize() == static_cast<size_t>(i + 1));
    }

    // バッファが満杯
    assert(buffer.IsFull());

    // これ以上追加できないことを確認
    bool result = buffer.TryWrite(100);
    assert(!result);

    // 要素の取り出し
    for (int i = 0; i < 7; i++)
    {
        int value;
        bool result = buffer.TryRead(value);
        assert(result);
        assert(value == i);
    }

    // バッファが空になったことを確認
    assert(buffer.IsEmpty());

    // これ以上取り出せないことを確認
    int value;
    result = buffer.TryRead(value);
    assert(!result);

    std::cout << "Basic operations test passed!" << std::endl;
}

// マルチスレッドテスト（単一プロデューサ・単一コンシューマ）
void TestMultithreaded()
{
    std::cout << "Running multithreaded test..." << std::endl;

    constexpr size_t BUFFER_SIZE = 1024;
    constexpr int NUM_ITEMS = 10000;

    RingBuffer<int, BUFFER_SIZE> buffer;
    Atomic<bool> done(false);
    Atomic<int> consumedCount(0);

    // プロデューサースレッド
    auto producer = [&buffer]()
    {
        for (int i = 0; i < NUM_ITEMS; i++)
        {
            while (!buffer.TryWrite(i))
            {
                std::this_thread::yield(); // バッファがいっぱいなら少し待つ
            }
        }
    };

    // コンシューマースレッド
    auto consumer = [&buffer, &done, &consumedCount]()
    {
        int expected = 0;
        while (consumedCount < NUM_ITEMS)
        {
            int value;
            if (buffer.TryRead(value))
            {
                // 正しい順序で値が取り出されることを確認
                assert(value == expected);
                expected++;
                consumedCount++;
            }
            else
            {
                std::this_thread::yield(); // バッファが空なら少し待つ
            }
        }
        done = true;
    };

    // スレッドの開始
    std::thread producerThread(producer);
    std::thread consumerThread(consumer);

    // スレッドの終了を待つ
    producerThread.join();
    consumerThread.join();

    // すべての要素が消費されたことを確認
    assert(done);
    assert(consumedCount == NUM_ITEMS);
    assert(buffer.IsEmpty());

    std::cout << "Multithreaded test passed!" << std::endl;
}

// パフォーマンステスト
void TestPerformance()
{
    std::cout << "Running performance test..." << std::endl;

    constexpr size_t BUFFER_SIZE = 4096;
    constexpr int NUM_ITEMS = 1000000;

    RingBuffer<int, BUFFER_SIZE> buffer;
    Atomic<int> consumedCount(0);

    auto startTime = std::chrono::high_resolution_clock::now();

    // プロデューサースレッド
    auto producer = [&buffer]()
    {
        for (int i = 0; i < NUM_ITEMS; i++)
        {
            while (!buffer.TryWrite(i))
            {
                std::this_thread::yield();
            }
        }
    };

    // コンシューマースレッド
    auto consumer = [&buffer, &consumedCount]()
    {
        while (consumedCount < NUM_ITEMS)
        {
            int value;
            if (buffer.TryRead(value))
            {
                consumedCount++;
            }
            else
            {
                std::this_thread::yield();
            }
        }
    };

    // スレッドの開始
    std::thread producerThread(producer);
    std::thread consumerThread(consumer);

    // スレッドの終了を待つ
    producerThread.join();
    consumerThread.join();

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    std::cout << "Performance test completed: " << NUM_ITEMS << " items processed in "
              << duration << " ms (" << (NUM_ITEMS * 1000.0 / duration) << " items/second)" << std::endl;
}

int main()
{
    std::cout << "Running RingBuffer tests..." << std::endl;

    TestBasicOperations();
    TestMultithreaded();
    TestPerformance();

    std::cout << "All RingBuffer tests passed!" << std::endl;
    return 0;
}
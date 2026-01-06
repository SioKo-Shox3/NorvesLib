#include "Thread/ThreadLocalStorage.h"
#include "Thread/Atomic.h"
#include <thread>
#include <vector>
#include <cassert>
#include <iostream>
#include <functional>

using namespace NorvesLib::Thread;

// 基本的な機能テスト
void TestBasicOperations()
{
    std::cout << "Running basic operations test..." << std::endl;

    // デフォルトコンストラクタでTLSを作成
    ThreadLocalStorage<int> tls;

    // デフォルト値はデフォルトコンストラクタによって初期化される（ここでは0）
    assert(tls.Get() == 0);

    // 値を設定
    tls.Set(42);
    assert(tls.Get() == 42);

    // 別の値に変更
    tls.Set(100);
    assert(tls.Get() == 100);

    std::cout << "Basic operations test passed!" << std::endl;
}

// 初期化関数を使用するテスト
void TestInitializer()
{
    std::cout << "Running initializer test..." << std::endl;

    // 初期化関数を指定してTLSを作成
    ThreadLocalStorage<int> tls([]()
                                { return 999; });

    // 初期化関数で設定された値を確認
    assert(tls.Get() == 999);

    // 値を変更
    tls.Set(123);
    assert(tls.Get() == 123);

    std::cout << "Initializer test passed!" << std::endl;
}

// 複雑な型でのテスト
void TestComplexType()
{
    std::cout << "Running complex type test..." << std::endl;

    // std::string型でテスト
    ThreadLocalStorage<std::string> tlsStr;

    // デフォルト値は空文字列
    assert(tlsStr.Get().empty());

    // 値の設定
    tlsStr.Set("Hello, TLS!");
    assert(tlsStr.Get() == "Hello, TLS!");

    // 別のTLS、初期化関数付き
    ThreadLocalStorage<std::string> tlsStr2([]()
                                            { return "Initialized String"; });
    assert(tlsStr2.Get() == "Initialized String");

    std::cout << "Complex type test passed!" << std::endl;
}

// マルチスレッドテスト
void TestMultithreaded()
{
    std::cout << "Running multithreaded test..." << std::endl;

    constexpr int NUM_THREADS = 8;
    ThreadLocalStorage<int> tls;

    // 各スレッドはTLSに自身のスレッド番号を格納し、
    // スレッド終了後も値が変わらないことを確認する
    auto threadFunction = [&tls](int threadId)
    {
        // 各スレッド固有の値を設定
        tls.Set(threadId);

        // 少し待機（他のスレッドからの干渉がないことを確認するため）
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 値が変わっていないことを確認
        assert(tls.Get() == threadId);
    };

    // スレッドを作成して実行
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(threadFunction, i + 1);
    }

    // スレッドの終了を待つ
    for (auto &t : threads)
    {
        t.join();
    }

    // メインスレッドの値は別途設定しない限り0のまま
    // （別のスレッドの影響を受けない）
    assert(tls.Get() == 0);

    std::cout << "Multithreaded test passed!" << std::endl;
}

// スレッド間のデータ独立性テスト
void TestIndependence()
{
    std::cout << "Running independence test..." << std::endl;

    ThreadLocalStorage<int> tls;
    Atomic<int> threadsReady(0);
    Atomic<bool> canProceed(false);
    constexpr int NUM_THREADS = 4;

    auto threadFunction = [&tls, &threadsReady, &canProceed](int threadId)
    {
        // 自分のスレッドIDを設定
        tls.Set(threadId);

        // すべてのスレッドが準備完了するまで待機
        threadsReady.FetchAdd(1);
        while (!canProceed.Load())
        {
            std::this_thread::yield();
        }

        // すべてのスレッドが準備完了した後、自分の値が変わっていないことを確認
        assert(tls.Get() == threadId);
    };

    // スレッドを作成して実行
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; i++)
    {
        threads.emplace_back(threadFunction, i + 100);
    }

    // すべてのスレッドが準備完了するのを待ってから進行許可
    while (threadsReady.Load() < NUM_THREADS)
    {
        std::this_thread::yield();
    }
    canProceed.Store(true);

    // スレッドの終了を待つ
    for (auto &t : threads)
    {
        t.join();
    }

    std::cout << "Independence test passed!" << std::endl;
}

int main()
{
    std::cout << "Running ThreadLocalStorage tests..." << std::endl;

    TestBasicOperations();
    TestInitializer();
    TestComplexType();
    TestMultithreaded();
    TestIndependence();

    std::cout << "All ThreadLocalStorage tests passed!" << std::endl;
    return 0;
}
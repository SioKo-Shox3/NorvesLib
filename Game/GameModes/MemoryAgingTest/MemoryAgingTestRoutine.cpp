#include "MemoryAgingTestRoutine.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Random/Public/Random.h"

using namespace NorvesLib::Core::Container;
using namespace NorvesLib::Core::GameMode;
using namespace NorvesLib::Random;

namespace Game::GameModes
{

    void MemoryAgingTestRoutine::Enter(IStateMachine* proc, MemoryAgingTestData& data)
    {
        (void)proc; // 未使用警告の抑制

        LOG_INFO("=================================================");
        LOG_INFO("メモリエージングテスト開始");
        LOG_INFO("=================================================");
        LOG_INFO("設定:");
        
        char buffer[256];
        sprintf_s(buffer, "  - 1フレームあたりの確保回数: %zu", data.m_AllocationCount);
        LOG_INFO(buffer);
        sprintf_s(buffer, "  - 確保サイズ範囲: %zu - %zu バイト", data.m_MinAllocationSize, data.m_MaxAllocationSize);
        LOG_INFO(buffer);
        sprintf_s(buffer, "  - テスト時間: %.1f 秒", data.m_TestDuration);
        LOG_INFO(buffer);
        sprintf_s(buffer, "  - レポート間隔: %.1f 秒", data.m_ReportInterval);
        LOG_INFO(buffer);
        
        LOG_INFO("-------------------------------------------------");

        // データをリセット
        data.Reset();

        // メモリブロック用の領域を予約
        data.m_MemoryBlocks.reserve(data.m_AllocationCount * 10);
    }

    void MemoryAgingTestRoutine::Do(IStateMachine* proc, MemoryAgingTestData& data, float deltaTime)
    {
        (void)proc; // 未使用警告の抑制

        // テスト完了済みの場合はスキップ
        if (data.m_bIsCompleted)
        {
            return;
        }

        // 経過時間を更新
        data.m_ElapsedTime += deltaTime;

        // 乱数生成器
        static Generator rng(12345);

        // メモリ確保処理
        for (size_t i = 0; i < data.m_AllocationCount; ++i)
        {
            // ランダムなサイズを決定
            size_t allocSize = static_cast<size_t>(rng.GetInt(
                static_cast<int32_t>(data.m_MinAllocationSize),
                static_cast<int32_t>(data.m_MaxAllocationSize)));

            // メモリブロックを確保
            auto block = MakeShared<VariableArray<uint8_t>>();
            block->resize(allocSize);

            // 確保したメモリに書き込み（実際にメモリが確保されていることを確認）
            for (size_t j = 0; j < allocSize; ++j)
            {
                (*block)[j] = static_cast<uint8_t>(j & 0xFF);
            }

            // リストに追加
            data.m_MemoryBlocks.push_back(std::move(block));
            ++data.m_TotalAllocations;
            ++data.m_CurrentAllocations;
        }

        // ランダムにメモリを解放（確保数の約半分を解放）
        size_t releaseCount = data.m_AllocationCount / 2;
        for (size_t i = 0; i < releaseCount && !data.m_MemoryBlocks.empty(); ++i)
        {
            // ランダムな位置のブロックを解放
            size_t index = static_cast<size_t>(rng.GetInt(0, static_cast<int32_t>(data.m_MemoryBlocks.size() - 1)));
            
            // swap and popで効率的に削除
            if (index != data.m_MemoryBlocks.size() - 1)
            {
                std::swap(data.m_MemoryBlocks[index], data.m_MemoryBlocks.back());
            }
            data.m_MemoryBlocks.pop_back();
            
            ++data.m_TotalDeallocations;
            --data.m_CurrentAllocations;
        }

        // 定期レポート
        if (data.m_ElapsedTime - data.m_LastReportTime >= data.m_ReportInterval)
        {
            data.m_LastReportTime = data.m_ElapsedTime;

            char buffer[512];
            sprintf_s(buffer, "[メモリエージング %.1f秒] 確保: %zu, 解放: %zu, 現在保持: %zu",
                data.m_ElapsedTime,
                data.m_TotalAllocations,
                data.m_TotalDeallocations,
                data.m_CurrentAllocations);
            LOG_INFO(buffer);
        }

        // テスト時間が経過したら完了
        if (data.m_ElapsedTime >= data.m_TestDuration)
        {
            data.m_bIsCompleted = true;

            LOG_INFO("-------------------------------------------------");
            LOG_INFO("メモリエージングテスト完了");
            
            char buffer[512];
            sprintf_s(buffer, "  - 総確保回数: %zu", data.m_TotalAllocations);
            LOG_INFO(buffer);
            sprintf_s(buffer, "  - 総解放回数: %zu", data.m_TotalDeallocations);
            LOG_INFO(buffer);
            sprintf_s(buffer, "  - 残存ブロック数: %zu", data.m_CurrentAllocations);
            LOG_INFO(buffer);
            
            LOG_INFO("=================================================");
        }
    }

    void MemoryAgingTestRoutine::Leave(IStateMachine* proc, MemoryAgingTestData& data)
    {
        (void)proc; // 未使用警告の抑制

        LOG_INFO("メモリエージングテスト: 全メモリブロックを解放中...");

        // 全てのメモリブロックを解放
        data.m_MemoryBlocks.clear();
        data.m_CurrentAllocations = 0;

        LOG_INFO("メモリエージングテスト: クリーンアップ完了");
    }

} // namespace Game::GameModes

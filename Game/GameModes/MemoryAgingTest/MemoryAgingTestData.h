#pragma once

#include "Core/Public/Container/Containers.h"

namespace Game::GameModes
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief メモリエージングテストのデータクラス
     *
     * テスト中に使用するデータを保持します。
     */
    struct MemoryAgingTestData
    {
        // テスト設定
        size_t m_AllocationCount = 100;        // 1フレームあたりの確保回数
        size_t m_MinAllocationSize = 64;       // 最小確保サイズ（バイト）
        size_t m_MaxAllocationSize = 4096;     // 最大確保サイズ（バイト）
        float m_TestDuration = 60.0f;          // テスト時間（秒）
        float m_ReportInterval = 5.0f;         // レポート間隔（秒）

        // テスト状態
        float m_ElapsedTime = 0.0f;            // 経過時間
        float m_LastReportTime = 0.0f;         // 最後のレポート時間
        size_t m_TotalAllocations = 0;         // 合計確保回数
        size_t m_TotalDeallocations = 0;       // 合計解放回数
        size_t m_CurrentAllocations = 0;       // 現在の確保数

        // 確保したメモリブロックを保持
        VariableArray<TSharedPtr<VariableArray<uint8_t>>> m_MemoryBlocks;

        // テスト完了フラグ
        bool m_bIsCompleted = false;

        /**
         * @brief テスト状態をリセット
         */
        void Reset()
        {
            m_ElapsedTime = 0.0f;
            m_LastReportTime = 0.0f;
            m_TotalAllocations = 0;
            m_TotalDeallocations = 0;
            m_CurrentAllocations = 0;
            m_MemoryBlocks.clear();
            m_bIsCompleted = false;
        }
    };

} // namespace Game::GameModes

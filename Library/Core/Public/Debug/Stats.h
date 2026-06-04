#pragma once

#include "Debug/DebugConfig.h"
#include "Container/Containers.h"
#include <chrono>

/**
 * @file Stats.h
 * @brief パフォーマンス統計収集用インターフェース
 * 
 * リリースビルドでは計測系は動作しないように設計されています。
 * デバッグ/開発ビルドでのみ有効になります。
 */

namespace NorvesLib::Debug
{
    using namespace NorvesLib::Core::Container;

    /**
     * @brief スタット用のスコープ計測クラス
     * 
     * RAIIパターンでスコープの実行時間を計測します。
     * リリースビルドでは何もしません。
     */
    class ScopedStat
    {
    public:
#if NORVES_ENABLE_STATS
        /**
         * @brief コンストラクタ
         * @param name スタット名
         * @param category カテゴリ
         */
        ScopedStat(const char* name, const char* category = "Stats")
            : m_Name(name)
            , m_Category(category)
            , m_StartTime(std::chrono::high_resolution_clock::now())
        {
        }

        /**
         * @brief デストラクタ - 計測結果をログ出力
         */
        ~ScopedStat();

        // コピー禁止
        ScopedStat(const ScopedStat&) = delete;
        ScopedStat& operator=(const ScopedStat&) = delete;

    private:
        const char* m_Name;
        const char* m_Category;
        std::chrono::high_resolution_clock::time_point m_StartTime;
#else
        // リリースビルドでは何もしない
        ScopedStat(const char*, const char* = "Stats") {}
        ~ScopedStat() = default;
#endif
    };

    /**
     * @brief 統計情報インターフェース
     * 
     * 各サブシステム固有のスタット情報を提供するための基底クラス
     */
    class IStats
    {
    public:
        virtual ~IStats() = default;

        /**
         * @brief スタットをリセット
         */
        virtual void Reset() = 0;

        /**
         * @brief スタット情報を文字列で取得
         */
        virtual String ToString() const = 0;
    };

    /**
     * @brief レンダリングスタット情報
     * 
     * レンダリングパイプラインの各段階の計測情報を保持します。
     */
    struct RenderingStats : public IStats
    {
        // フレーム情報
        uint64_t FrameNumber = 0;
        float DeltaTime = 0.0f;
        float FPS = 0.0f;

        // 描画統計
        uint32_t DrawCalls = 0;
        uint32_t TrianglesRendered = 0;
        uint32_t VisibleObjects = 0;
        uint32_t BatchCount = 0;
        uint32_t InstancedDrawCalls = 0;

        // タイミング（ミリ秒）
        float CollectionTimeMs = 0.0f;
        float CullingTimeMs = 0.0f;
        float BatchingTimeMs = 0.0f;
        float CommandGenerationTimeMs = 0.0f;
        float GPUTimeMs = 0.0f;
        float TotalFrameTimeMs = 0.0f;

        void Reset() override
        {
            FrameNumber = 0;
            DeltaTime = 0.0f;
            FPS = 0.0f;
            DrawCalls = 0;
            TrianglesRendered = 0;
            VisibleObjects = 0;
            BatchCount = 0;
            InstancedDrawCalls = 0;
            CollectionTimeMs = 0.0f;
            CullingTimeMs = 0.0f;
            BatchingTimeMs = 0.0f;
            CommandGenerationTimeMs = 0.0f;
            GPUTimeMs = 0.0f;
            TotalFrameTimeMs = 0.0f;
        }

        String ToString() const override;
    };

    /**
     * @brief メモリスタット情報
     */
    struct MemoryStats : public IStats
    {
        size_t TotalAllocated = 0;
        size_t TotalFreed = 0;
        size_t CurrentUsage = 0;
        size_t PeakUsage = 0;
        uint32_t AllocationCount = 0;
        uint32_t DeallocationCount = 0;

        void Reset() override
        {
            TotalAllocated = 0;
            TotalFreed = 0;
            CurrentUsage = 0;
            PeakUsage = 0;
            AllocationCount = 0;
            DeallocationCount = 0;
        }

        String ToString() const override;
    };

    /**
     * @brief スタットマネージャー
     * 
     * グローバルなスタット収集・管理を行います。
     * GEngine経由でアクセスされることを想定しています。
     */
    class StatsManager
    {
    public:
        StatsManager() = default;
        ~StatsManager() = default;

        // コピー禁止
        StatsManager(const StatsManager&) = delete;
        StatsManager& operator=(const StatsManager&) = delete;

        /**
         * @brief レンダリングスタットを取得
         */
        RenderingStats& GetRenderingStats() { return m_RenderingStats; }
        const RenderingStats& GetRenderingStats() const { return m_RenderingStats; }

        /**
         * @brief メモリスタットを取得
         */
        MemoryStats& GetMemoryStats() { return m_MemoryStats; }
        const MemoryStats& GetMemoryStats() const { return m_MemoryStats; }

        /**
         * @brief 全スタットをリセット
         */
        void ResetAll()
        {
            m_RenderingStats.Reset();
            m_MemoryStats.Reset();
        }

        /**
         * @brief スタット有効かどうか
         */
        static constexpr bool IsEnabled()
        {
#if NORVES_ENABLE_STATS
            return true;
#else
            return false;
#endif
        }

    private:
        RenderingStats m_RenderingStats;
        MemoryStats m_MemoryStats;
    };

} // namespace NorvesLib::Debug

// ===== スタット用マクロ =====

#if NORVES_ENABLE_STATS

/**
 * @brief スコープ計測マクロ
 * 
 * 使用例:
 * ```cpp
 * void MyFunction()
 * {
 *     NORVES_STAT_SCOPE("MyFunction");
 *     // 処理...
 * }
 * ```
 */
#define NORVES_STAT_SCOPE(name) \
    NorvesLib::Debug::ScopedStat __scopedStat_##__LINE__(name)

#define NORVES_STAT_SCOPE_CATEGORY(name, category) \
    NorvesLib::Debug::ScopedStat __scopedStat_##__LINE__(name, category)

/**
 * @brief 関数スコープ計測マクロ
 * 
 * 使用例:
 * ```cpp
 * void MyFunction()
 * {
 *     NORVES_STAT_FUNCTION();
 *     // 処理...
 * }
 * ```
 */
#define NORVES_STAT_FUNCTION() \
    NorvesLib::Debug::ScopedStat __scopedStat_func(__FUNCTION__)

#define NORVES_STAT_FUNCTION_CATEGORY(category) \
    NorvesLib::Debug::ScopedStat __scopedStat_func(__FUNCTION__, category)

/**
 * @brief カウンター増加マクロ
 */
#define NORVES_STAT_INC(counter) \
    ++(counter)

#define NORVES_STAT_ADD(counter, value) \
    (counter) += (value)

/**
 * @brief 時間計測開始マクロ
 */
#define NORVES_STAT_TIME_START(name) \
    auto __statTimeStart_##name = std::chrono::high_resolution_clock::now()

/**
 * @brief 時間計測終了マクロ（ミリ秒で保存）
 */
#define NORVES_STAT_TIME_END(name, targetMs) \
    do { \
        auto __statTimeEnd_##name = std::chrono::high_resolution_clock::now(); \
        (targetMs) = std::chrono::duration<float, std::milli>( \
            __statTimeEnd_##name - __statTimeStart_##name).count(); \
    } while(0)

#else // NORVES_ENABLE_STATS

// リリースビルドでは何もしない
#define NORVES_STAT_SCOPE(name) do {} while(0)
#define NORVES_STAT_SCOPE_CATEGORY(name, category) do {} while(0)
#define NORVES_STAT_FUNCTION() do {} while(0)
#define NORVES_STAT_FUNCTION_CATEGORY(category) do {} while(0)
#define NORVES_STAT_INC(counter) do {} while(0)
#define NORVES_STAT_ADD(counter, value) do {} while(0)
#define NORVES_STAT_TIME_START(name) do {} while(0)
#define NORVES_STAT_TIME_END(name, targetMs) do {} while(0)

#endif // NORVES_ENABLE_STATS

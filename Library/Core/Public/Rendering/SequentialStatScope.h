#pragma once

#include "Debug/Stats.h"

#include <chrono>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 重ならない区間を順に切って記録する（Debug/Stats.h の区間と同じ記録）。
     *
     * Begin は開いている区間を閉じてから次の区間を開き、End と破棄は開いている区間を閉じる。
     * 途中で戻っても最後の区間は破棄で閉じる。トレースが無効なときと Release では何もしない。
     * 区間の名前の文字列は借りるだけなので、閉じるまで生きている文字列を渡す。
     */
    class SequentialStatScope
    {
    public:
#if NORVES_ENABLE_STATS
        explicit SequentialStatScope(const char* category)
            : m_Category(category),
              m_bTraceActive(NorvesLib::Debug::StatsManager::Get().IsTraceActive())
        {
        }

        ~SequentialStatScope()
        {
            End();
        }

        void Begin(const char* name)
        {
            End();
            if (!m_bTraceActive)
            {
                return;
            }
            m_Name = name;
            m_StartTime = std::chrono::high_resolution_clock::now();
        }

        void End()
        {
            if (!m_Name)
            {
                return;
            }
            const float durationMs = std::chrono::duration<float, std::milli>(
                                         std::chrono::high_resolution_clock::now() - m_StartTime)
                                         .count();
            NorvesLib::Debug::StatsManager::Get().RecordScope(m_Name, m_Category, durationMs);
            m_Name = nullptr;
        }

    private:
        const char* m_Category = nullptr;
        bool m_bTraceActive = false;
        const char* m_Name = nullptr;
        std::chrono::high_resolution_clock::time_point m_StartTime;
#else
        explicit SequentialStatScope(const char*) {}
        void Begin(const char*) {}
        void End() {}
#endif

    public:
        SequentialStatScope(const SequentialStatScope&) = delete;
        SequentialStatScope& operator=(const SequentialStatScope&) = delete;
    };

    /**
     * @brief 何度も通る短い区間の時間を、記録せずに足し込むタイマー。
     *
     * 区間の記録は 1 件ごとにトレースのミューテックスとファイルへの書き込みを伴うので、1 フレームに何十回も通る区間は
     * 足し込んだ合計を呼び出し側が 1 件として記録する。Start で測り始め、Stop と破棄で足し込む。
     * bActive が false のときと Release では何もしない。
     */
    class AccumulatingStatTimer
    {
    public:
#if NORVES_ENABLE_STATS
        AccumulatingStatTimer(float& totalMs, bool bActive, bool bStartNow = true)
            : m_TotalMs(totalMs),
              m_bActive(bActive)
        {
            if (bStartNow)
            {
                Start();
            }
        }

        ~AccumulatingStatTimer()
        {
            Stop();
        }

        void Start()
        {
            if (!m_bActive || m_bRunning)
            {
                return;
            }
            m_bRunning = true;
            m_StartTime = std::chrono::high_resolution_clock::now();
        }

        void Stop()
        {
            if (!m_bRunning)
            {
                return;
            }
            m_TotalMs += std::chrono::duration<float, std::milli>(
                             std::chrono::high_resolution_clock::now() - m_StartTime)
                             .count();
            m_bRunning = false;
        }

    private:
        float& m_TotalMs;
        bool m_bActive = false;
        bool m_bRunning = false;
        std::chrono::high_resolution_clock::time_point m_StartTime;
#else
        AccumulatingStatTimer(float&, bool, bool = true) {}
        void Start() {}
        void Stop() {}
#endif

    public:
        AccumulatingStatTimer(const AccumulatingStatTimer&) = delete;
        AccumulatingStatTimer& operator=(const AccumulatingStatTimer&) = delete;
    };
} // namespace NorvesLib::Core::Rendering

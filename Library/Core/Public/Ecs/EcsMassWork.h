#pragma once

/**
 * @file EcsMassWork.h
 * @brief 大量の軽い行の作成・反映・解放を、1 フレームの行数と時間の予算で分けて進める仕事の列（設計 1.11）
 *
 * - 仕事（IEcsMassWork）は、範囲 1 つ・チャンク 1 つのような「途中で止めても描画が欠けない単位」で進む。
 *   1 回の StepMassWork で 1 単位を進め、処理した行の数を予算の残り（EcsMassBudgetCursor）に積む。
 * - 列（EcsMassWorkQueue）は 1 フレームに 1 回 Run で回す。全部の仕事の PollMassWork（所有者の変換の変化などを見て
 *   仕事を積む）を呼んでから、仕事のある項目を先頭をずらしながら順に 1 単位ずつ進め、行数か時間の予算を使い切ったら止める。
 *   予算が小さくても、1 フレームに少なくとも 1 単位は進める（止まったままにしない）。
 * - 項目は 2 種類。Register した仕事は持たず（持ち主が Unregister するまで毎フレーム Poll する）、Adopt した仕事は列が持ち、
 *   仕事が無くなったら列が消す（持ち主が先に消える解放の後始末など）。
 * - 予算の時間は機能の制御に使う値で、計測ではない（Release でも同じに効く）。0 以下なら時間では止めない。
 *
 * EcsWorld が 1 つ持ち、GameThread だけが触る（スレッドセーフではない）。
 */

#include "Container/Containers.h"
#include "Container/PointerTypes.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace NorvesLib::Core::Ecs
{
    /// 1 フレームに進める行の数の既定値（MASS-STREAM で決めた値。PROGRESS.md の計測を参照）
    inline constexpr uint32_t kDefaultMassRowsPerFrame = 8192;
    /// 1 フレームに使う時間[ms]の既定値（MASS-STREAM で決めた値。PROGRESS.md の計測を参照）
    inline constexpr float kDefaultMassMillisecondsPerFrame = 1.0f;

    /// 1 フレームの予算
    struct EcsMassBudget
    {
        /// 1 フレームに進める行の数（1 以上）
        uint32_t RowsPerFrame = kDefaultMassRowsPerFrame;
        /// 1 フレームに使う時間[ms]。0 以下なら時間では止めない
        float MillisecondsPerFrame = kDefaultMassMillisecondsPerFrame;
    };

    /// 1 フレームの予算の残り
    class EcsMassBudgetCursor
    {
    public:
        explicit EcsMassBudgetCursor(const EcsMassBudget &budget)
            : m_RowLimit(std::max<uint32_t>(1u, budget.RowsPerFrame)),
              m_Milliseconds(budget.MillisecondsPerFrame),
              m_Start(std::chrono::steady_clock::now())
        {
        }

        /// まだ進めてよいか（行数と時間の両方に残りがある）
        [[nodiscard]] bool HasRoom() const
        {
            if (m_UsedRows >= m_RowLimit)
            {
                return false;
            }
            return !(m_Milliseconds > 0.0f) || GetElapsedMilliseconds() < static_cast<double>(m_Milliseconds);
        }

        /// 行数の残り（0 なら使い切った）
        [[nodiscard]] uint32_t GetRemainingRows() const
        {
            return m_UsedRows >= m_RowLimit ? 0u : m_RowLimit - m_UsedRows;
        }

        void Consume(uint32_t rows)
        {
            m_UsedRows += rows;
            ++m_Steps;
        }

        [[nodiscard]] uint32_t GetUsedRows() const
        {
            return m_UsedRows;
        }

        [[nodiscard]] uint32_t GetSteps() const
        {
            return m_Steps;
        }

        [[nodiscard]] double GetElapsedMilliseconds() const
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_Start).count();
        }

    private:
        uint32_t m_RowLimit = 1;
        float m_Milliseconds = 0.0f;
        uint32_t m_UsedRows = 0;
        uint32_t m_Steps = 0;
        std::chrono::steady_clock::time_point m_Start;
    };

    /// 検査（MASS_RANGES など）の突き合わせの結果
    struct EcsMassAudit
    {
        /// 突き合わせた範囲の数
        uint32_t Ranges = 0;
        /// 公開中の範囲の数
        uint32_t PublishedRanges = 0;
        /// 破れた範囲の数
        uint32_t Failures = 0;
        /// 最初に破れた範囲の説明
        char FirstFailure[256] = {};
    };

    /// 予算で分けて進める仕事
    class IEcsMassWork
    {
    public:
        virtual ~IEcsMassWork() = default;

        /// 毎フレーム、予算で進める前に 1 回呼ぶ（所有者の変換の変化などを見て仕事を積む）。安く保つ
        virtual void PollMassWork()
        {
        }

        /// 残りの仕事があるか
        [[nodiscard]] virtual bool HasMassWork() const = 0;

        /// 1 単位（範囲 1 つ・チャンク 1 つなど）を進め、処理した行の数を cursor に積む。仕事が無ければ何もしない
        virtual void StepMassWork(EcsMassBudgetCursor &cursor) = 0;

        /// 検査の突き合わせ（範囲の公開の状態と描画側の proxy）。突き合わせない仕事は何もしない
        virtual void AuditMassWork(EcsMassAudit &audit) const
        {
            (void)audit;
        }
    };

    /// 直近の Run の集計
    struct EcsMassWorkStats
    {
        /// 列の項目の数（Register と Adopt の合計）
        uint32_t Items = 0;
        /// Run の後に仕事が残っている項目の数
        uint32_t PendingItems = 0;
        /// 直近の Run で進めた行と単位の数、かかった時間[ms]
        uint32_t Rows = 0;
        uint32_t Steps = 0;
        double Milliseconds = 0.0;
        /// これまでに進めた行の数の合計
        uint64_t TotalRows = 0;
        /// Run を呼んだ回数（フレームの数え方）
        uint64_t Runs = 0;
        /// ひと続きの仕事（仕事が残っている Run が続いた区間）の Run の数・行の数・1 回の Run の最大の時間[ms]。
        /// 区間が終わった Run（仕事が残らなくなった）では bBurstEnded が立ち、値はその区間のもの
        uint32_t BurstRuns = 0;
        uint64_t BurstRows = 0;
        double BurstMaxMilliseconds = 0.0;
        bool bBurstEnded = false;
    };

    /// 予算で分けて進める仕事の列
    class EcsMassWorkQueue
    {
    public:
        void SetBudget(const EcsMassBudget &budget)
        {
            m_Budget = budget;
            m_Budget.RowsPerFrame = std::max<uint32_t>(1u, budget.RowsPerFrame);
        }

        [[nodiscard]] const EcsMassBudget &GetBudget() const
        {
            return m_Budget;
        }

        /// 持たない仕事を足す（同じ仕事の重複は無視）。Unregister するまで毎フレーム Poll する
        void Register(IEcsMassWork *work)
        {
            if (work == nullptr)
            {
                return;
            }
            for (const Entry &entry : m_Entries)
            {
                if (entry.Work == work)
                {
                    return;
                }
            }
            m_Entries.push_back(Entry{work, nullptr});
        }

        /// 持たない仕事を外す。Run の最中でもよい（その項目はもう呼ばない）
        void Unregister(IEcsMassWork *work)
        {
            for (Entry &entry : m_Entries)
            {
                if (entry.Work == work && !entry.Owned)
                {
                    entry.Work = nullptr;
                    m_bNeedsCompact = true;
                }
            }
            if (!m_bRunning)
            {
                Compact();
            }
        }

        /// 仕事を列に持たせる。仕事が無くなった Run の後に消す
        void Adopt(Container::TUniquePtr<IEcsMassWork> work)
        {
            if (!work)
            {
                return;
            }
            IEcsMassWork *raw = work.get();
            m_Entries.push_back(Entry{raw, std::move(work)});
        }

        /// 1 フレーム分進める
        void Run()
        {
            m_bRunning = true;
            for (const Entry &entry : m_Entries)
            {
                if (entry.Work != nullptr)
                {
                    entry.Work->PollMassWork();
                }
            }
            EcsMassBudgetCursor cursor(m_Budget);
            const size_t count = m_Entries.size();
            if (count > 0)
            {
                // 先頭をずらしながら 1 単位ずつ回す（1 つの大きな仕事が予算を独占しない）。1 周して誰も進まなければ終わる
                bool bProgress = true;
                while (bProgress && (cursor.GetSteps() == 0 || cursor.HasRoom()))
                {
                    bProgress = false;
                    for (size_t offset = 0; offset < count; ++offset)
                    {
                        if (cursor.GetSteps() != 0 && !cursor.HasRoom())
                        {
                            break;
                        }
                        IEcsMassWork *work = m_Entries[(m_NextStart + offset) % count].Work;
                        if (work == nullptr || !work->HasMassWork())
                        {
                            continue;
                        }
                        const uint32_t before = cursor.GetSteps();
                        work->StepMassWork(cursor);
                        bProgress = bProgress || cursor.GetSteps() != before;
                    }
                }
                m_NextStart = (m_NextStart + 1) % count;
            }
            m_bRunning = false;

            // 仕事の無くなった持つ項目を消す
            for (Entry &entry : m_Entries)
            {
                if (entry.Owned && !entry.Owned->HasMassWork())
                {
                    entry.Owned.reset();
                    entry.Work = nullptr;
                    m_bNeedsCompact = true;
                }
            }
            Compact();

            m_Stats.Items = static_cast<uint32_t>(m_Entries.size());
            m_Stats.PendingItems = 0;
            for (const Entry &entry : m_Entries)
            {
                m_Stats.PendingItems += entry.Work != nullptr && entry.Work->HasMassWork() ? 1u : 0u;
            }
            m_Stats.Rows = cursor.GetUsedRows();
            m_Stats.Steps = cursor.GetSteps();
            m_Stats.Milliseconds = cursor.GetElapsedMilliseconds();
            m_Stats.TotalRows += cursor.GetUsedRows();
            ++m_Stats.Runs;

            // ひと続きの仕事の区間を数える（予算の既定値を決める計測のログ用）
            if (m_Stats.bBurstEnded)
            {
                m_Stats.BurstRuns = 0;
                m_Stats.BurstRows = 0;
                m_Stats.BurstMaxMilliseconds = 0.0;
                m_Stats.bBurstEnded = false;
            }
            if (cursor.GetSteps() != 0)
            {
                ++m_Stats.BurstRuns;
                m_Stats.BurstRows += cursor.GetUsedRows();
                m_Stats.BurstMaxMilliseconds = std::max(m_Stats.BurstMaxMilliseconds, m_Stats.Milliseconds);
                m_Stats.bBurstEnded = m_Stats.PendingItems == 0;
            }
        }

        /// 仕事の残っている項目があるか（作成・反映・解放のどれか）
        [[nodiscard]] bool HasPendingWork() const
        {
            for (const Entry &entry : m_Entries)
            {
                if (entry.Work != nullptr && entry.Work->HasMassWork())
                {
                    return true;
                }
            }
            return false;
        }

        /// 全項目を突き合わせる
        void Audit(EcsMassAudit &audit) const
        {
            for (const Entry &entry : m_Entries)
            {
                if (entry.Work != nullptr)
                {
                    entry.Work->AuditMassWork(audit);
                }
            }
        }

        [[nodiscard]] uint32_t GetItemCount() const
        {
            return static_cast<uint32_t>(m_Entries.size());
        }

        [[nodiscard]] const EcsMassWorkStats &GetStats() const
        {
            return m_Stats;
        }

    private:
        struct Entry
        {
            IEcsMassWork *Work = nullptr;
            Container::TUniquePtr<IEcsMassWork> Owned;
        };

        void Compact()
        {
            if (!m_bNeedsCompact)
            {
                return;
            }
            size_t write = 0;
            for (size_t read = 0; read < m_Entries.size(); ++read)
            {
                if (m_Entries[read].Work != nullptr)
                {
                    if (write != read)
                    {
                        m_Entries[write] = std::move(m_Entries[read]);
                    }
                    ++write;
                }
            }
            m_Entries.resize(write);
            m_NextStart = m_Entries.empty() ? 0 : m_NextStart % m_Entries.size();
            m_bNeedsCompact = false;
        }

        EcsMassBudget m_Budget;
        Container::VariableArray<Entry> m_Entries;
        size_t m_NextStart = 0;
        bool m_bRunning = false;
        bool m_bNeedsCompact = false;
        EcsMassWorkStats m_Stats;
    };

} // namespace NorvesLib::Core::Ecs

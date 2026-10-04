#pragma once

// VRAM_BUDGET ログを出すかどうかの判定（取得間隔と、前回ログした値からの変化率）。
// 時刻と値を引数で受けるので、予算値を制御したテストから境界を確かめられる。

#include <chrono>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    class VideoMemoryBudgetLogGate
    {
    public:
        /** @brief 予算の取得間隔（初回のログを出した後に使う） */
        static constexpr std::chrono::seconds PollInterval{1};

        /** @brief 前回ログした値からこの割合（%）以上変わったら再びログを出す */
        static constexpr uint64_t ChangePercentThreshold = 1;

        /**
         * @brief 今回、予算を取得してよいか
         * @return 初回は常に true。以後は前回の取得から PollInterval 以上たったときだけ true
         */
        bool IsPollDue(std::chrono::steady_clock::time_point now) const
        {
            return !m_bLogged || now - m_LastPollTime >= PollInterval;
        }

        /** @brief 取得した時刻を記録する（IsPollDue が true のときに呼ぶ） */
        void MarkPolled(std::chrono::steady_clock::time_point now)
        {
            m_LastPollTime = now;
        }

        /**
         * @brief 取得した値をログに出すべきか判定し、出すなら前回値として記録する
         * @return 初回、または予算か使用量のどちらかが前回ログした値から 1% 以上変わったとき true
         */
        bool CommitIfChanged(uint64_t budgetBytes, uint64_t usageBytes)
        {
            if (m_bLogged &&
                !HasChanged(m_LoggedBudgetBytes, budgetBytes) &&
                !HasChanged(m_LoggedUsageBytes, usageBytes))
            {
                return false;
            }

            m_bLogged = true;
            m_LoggedBudgetBytes = budgetBytes;
            m_LoggedUsageBytes = usageBytes;
            return true;
        }

    private:
        static bool HasChanged(uint64_t previous, uint64_t current)
        {
            const uint64_t difference = previous > current ? previous - current : current - previous;
            return difference * 100 >= (previous > 0 ? previous : 1) * ChangePercentThreshold;
        }

        bool m_bLogged = false;
        std::chrono::steady_clock::time_point m_LastPollTime{};
        uint64_t m_LoggedBudgetBytes = 0;
        uint64_t m_LoggedUsageBytes = 0;
    };

} // namespace NorvesLib::Core::Rendering

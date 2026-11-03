#pragma once

#include "Logging/LogMacros.h"

#include <cstddef>
#include <type_traits>

namespace NorvesLib::Core::GameMode
{
    /**
     * @brief 更新の先頭で遷移要求を適用する固定容量の状態機械。
     *
     * 状態 ID は 0 以上 StateCount 未満の整数値を持つ列挙型とする。
     * 状態表は最初の要求を積む前に登録する。StateCount の既定値は 16。
     * 保留列と状態表は本体に格納し、遷移のための動的確保は行わない。
     * 現在状態がある間に破棄する場合は、呼び出し元が先に Shutdown する。
     */
    template <typename TContext, typename TStateId, std::size_t PendingCapacity = 4, std::size_t StateCount = 16>
    class TStateMachine
    {
        static_assert(std::is_enum_v<TStateId>, "TStateId must be an enum");
        static_assert(PendingCapacity > 0, "PendingCapacity must be positive");
        static_assert(StateCount > 0, "StateCount must be positive");
        static_assert(PendingCapacity <= static_cast<std::size_t>(-1) / 4, "PendingCapacity is too large");

    public:
        struct StateTable
        {
            void (*Enter)(TStateMachine&, TContext&) = nullptr;
            void (*Update)(TStateMachine&, TContext&, float) = nullptr;
            void (*Exit)(TStateMachine&, TContext&) = nullptr;
        };

        TStateMachine() = default;
        TStateMachine(const TStateMachine&) = delete;
        TStateMachine& operator=(const TStateMachine&) = delete;

        /// 最初の要求を積む前に状態を登録する。同じ ID の再登録は上書きする。
        bool RegisterState(TStateId id, StateTable table)
        {
            if (m_bStarted || m_bShutdown || !IsValidStateId(id))
            {
                NORVES_LOG_WARNING("GameMode", "状態を登録できません: 開始後または範囲外の ID です");
                return false;
            }

            const std::size_t index = static_cast<std::size_t>(id);
            m_States[index] = table;
            m_bRegistered[index] = true;
            return true;
        }

        /// 初期状態への要求を積む。実際の開始は次の Update の先頭で行う。
        void Start(TStateId initial)
        {
            RequestChange(initial);
        }

        /// 要求を積むだけで、同じ状態への要求も次の安全点で Exit と Enter をやり直す。
        void RequestChange(TStateId id)
        {
            if (m_bShutdown)
            {
                ++m_RejectedRequestCount;
                NORVES_LOG_WARNING("GameMode", "停止済みの状態機械への遷移要求を無視しました");
                return;
            }
            if (!IsValidStateId(id) || !m_bRegistered[static_cast<std::size_t>(id)])
            {
                ++m_RejectedRequestCount;
                NORVES_LOG_WARNING("GameMode", "未登録の状態への遷移要求を無視しました");
                return;
            }
            if (m_PendingCount == PendingCapacity)
            {
                ++m_RejectedRequestCount;
                NORVES_LOG_WARNING("GameMode", "保留列が満杯のため遷移要求を無視しました");
                return;
            }

            const std::size_t tail = (m_PendingHead + m_PendingCount) % PendingCapacity;
            m_Pending[tail] = id;
            ++m_PendingCount;
            m_bStarted = true;
        }

        /**
         * @brief 保留列を FIFO で適用し、空になった後に現在状態を更新する。
         *
         * Enter / Exit の要求は同じ列の末尾から続けて適用する。循環を防ぐため
         * 1 回の適用は PendingCapacity の 4 倍までとし、残りは次回へ持ち越す。
         * 状態の Update 内の要求は次回まで適用しない。
         */
        void Update(TContext& context, float deltaTime)
        {
            if (m_bShutdown || m_bUpdating)
            {
                return;
            }

            m_bUpdating = true;
            std::size_t appliedCount = 0;
            while (!m_bShutdown && m_PendingCount > 0 && appliedCount < PendingCapacity * 4)
            {
                const TStateId next = m_Pending[m_PendingHead];
                m_PendingHead = (m_PendingHead + 1) % PendingCapacity;
                --m_PendingCount;
                ++appliedCount;

                if (m_bHasCurrentState)
                {
                    const StateTable& current = m_States[static_cast<std::size_t>(m_CurrentState)];
                    m_bExiting = true;
                    if (current.Exit)
                    {
                        current.Exit(*this, context);
                    }
                    m_bExiting = false;
                    if (m_bShutdown)
                    {
                        break;
                    }
                    m_bHasCurrentState = false;
                }

                m_CurrentState = next;
                m_bHasCurrentState = true;
                const StateTable& nextState = m_States[static_cast<std::size_t>(next)];
                if (nextState.Enter)
                {
                    nextState.Enter(*this, context);
                }
            }

            if (!m_bShutdown && m_PendingCount > 0)
            {
                ++m_DrainLimitReachedCount;
                NORVES_LOG_WARNING("GameMode", "遷移の連鎖が上限に達したため、残りを次の更新に持ち越します");
            }
            else if (!m_bShutdown && m_bHasCurrentState)
            {
                const StateTable& current = m_States[static_cast<std::size_t>(m_CurrentState)];
                if (current.Update)
                {
                    current.Update(*this, context, deltaTime);
                }
            }
            m_bUpdating = false;
        }

        /// 保留要求を破棄し、現在状態があれば Exit を一度だけ呼んで停止する。
        void Shutdown(TContext& context)
        {
            if (m_bShutdown)
            {
                return;
            }
            m_bShutdown = true;
            m_PendingCount = 0;

            if (m_bHasCurrentState)
            {
                const StateTable& current = m_States[static_cast<std::size_t>(m_CurrentState)];
                m_bHasCurrentState = false;
                // Exit の中から Shutdown された場合は同じ Exit を再入させない。
                if (!m_bExiting && current.Exit)
                {
                    current.Exit(*this, context);
                }
            }
        }

        /// 現在状態がある場合にだけ GetCurrentState の戻り値を使用する。
        bool HasCurrentState() const { return m_bHasCurrentState; }
        TStateId GetCurrentState() const { return m_CurrentState; }
        std::size_t GetRejectedRequestCount() const { return m_RejectedRequestCount; }
        std::size_t GetDrainLimitReachedCount() const { return m_DrainLimitReachedCount; }

    private:
        static bool IsValidStateId(TStateId id)
        {
            using Underlying = std::underlying_type_t<TStateId>;
            const Underlying value = static_cast<Underlying>(id);
            if constexpr (std::is_signed_v<Underlying>)
            {
                if (value < 0)
                {
                    return false;
                }
            }
            if constexpr (sizeof(Underlying) > sizeof(std::size_t))
            {
                if (value > static_cast<Underlying>(static_cast<std::size_t>(-1)))
                {
                    return false;
                }
            }
            return static_cast<std::size_t>(value) < StateCount;
        }

        StateTable m_States[StateCount]{};
        bool m_bRegistered[StateCount]{};
        TStateId m_Pending[PendingCapacity]{};
        TStateId m_CurrentState{};
        std::size_t m_PendingHead = 0;
        std::size_t m_PendingCount = 0;
        std::size_t m_RejectedRequestCount = 0;
        std::size_t m_DrainLimitReachedCount = 0;
        bool m_bStarted = false;
        bool m_bShutdown = false;
        bool m_bUpdating = false;
        bool m_bExiting = false;
        bool m_bHasCurrentState = false;
    };
} // namespace NorvesLib::Core::GameMode

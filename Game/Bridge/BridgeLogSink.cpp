#include "BridgeLogSink.h"

#if defined(NORVES_BRIDGE_ENABLED)

#include <utility>

namespace Game::Bridge
{

    /**
     * @brief Logger からのログ配送を受ける（宣言は BridgeLogSink.h を参照）
     *
     * @note m_Mutex だけを取り Logger へは触れない（ロック順序 m-2）。例外は投げない。
     */
    void BridgeLogSink::OnLog(const NorvesLib::Core::Logging::LogEntry& entry)
    {
        // m_MinLevel 未満は捨てる（LogLevel は uint8_t で単調増加なので数値比較で足りる）。
        if (static_cast<uint8_t>(entry.level) < m_MinLevel.GetValue())
        {
            return;
        }

        // 必要なフィールドだけを値コピーしたスナップショットを作る（Logger の LogEntry を
        // 借用しない）。category / message は String の値コピー。
        ForwardEntry fe{entry.level, entry.category, entry.message};

        // m_Mutex だけを取る（Logger へは触れない。ロック順序 m-2 を参照）。満杯なら
        // 最古を 1 件捨てて落とした件数を数える（残留メモリの暴走を防ぐ）。
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (m_Queue.size() >= kMaxQueued)
        {
            m_Queue.pop();
            m_Dropped.FetchAdd(1);
        }
        m_Queue.push(std::move(fe));
    }

    /**
     * @brief キュー先頭の 1 件を取り出す（宣言は BridgeLogSink.h を参照）
     *
     * @note ゲームスレッド（DrainInbound 経由）から m_Mutex だけを取って呼ばれる。
     */
    bool BridgeLogSink::TryPopForward(ForwardEntry& out)
    {
        // ゲームスレッド（DrainInbound 経由）から呼ばれる。m_Mutex だけを取る。
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (m_Queue.empty())
        {
            return false;
        }
        out = std::move(m_Queue.front());
        m_Queue.pop();
        return true;
    }

    /**
     * @brief 転送する最小ログレベルを設定する（宣言は BridgeLogSink.h を参照）
     */
    void BridgeLogSink::SetMinLevel(NorvesLib::Core::Logging::LogLevel level)
    {
        m_MinLevel.SetValue(static_cast<uint8_t>(level));
    }

    /**
     * @brief キューを空にする（宣言は BridgeLogSink.h を参照）
     *
     * @note m_Mutex 下で残留分を全て pop する。
     */
    void BridgeLogSink::Clear()
    {
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        while (!m_Queue.empty())
        {
            m_Queue.pop();
        }
    }

} // namespace Game::Bridge

#endif // NORVES_BRIDGE_ENABLED

#pragma once

#include "Logging/Logger.h"
#include "Thread/Atomic.h"

#include <cstdint>
#include <cstring>

namespace NorvesLib::Test::RenderingValidation
{
    /**
     * @brief ビジビリティバッファの解決を通ったか、予備（GBuffer のラスタ）へ戻ったかを、エンジンのログで確かめる受け側
     *
     * 解決のパスは、使えない理由があるとき VISBUFFER_FALLBACK reason=<理由> を警告で 1 回出し、解決を記録したとき
     * VISBUFFER_RESOLVE_TILES used=<0|1> を出す（変わったときだけ）。既定（--visibility-buffer=on）の検査は、装置が
     * 対応しないと黙って予備の経路へ落ちて通ってしまうので、この記録で「解決を通った」ことを確かめる。
     * 予備（--visibility-buffer=off）の検査は、解決を通っていないことを確かめる。
     *
     * ログの受け側は非同期ワーカーから呼ばれるので、数は Atomic で持つ。ログが無効なビルド（NORVES_ENABLE_LOGGING=0）では
     * 何も観測できないので CanObserve() が false を返す。
     */
    class VisibilityBufferPathProbe final : public Core::Logging::ILogSink
    {
    public:
        VisibilityBufferPathProbe() = default;
        VisibilityBufferPathProbe(const VisibilityBufferPathProbe&) = delete;
        VisibilityBufferPathProbe& operator=(const VisibilityBufferPathProbe&) = delete;
        ~VisibilityBufferPathProbe() override
        {
            Detach();
        }

        static constexpr bool CanObserve()
        {
            return NORVES_ENABLE_LOGGING != 0;
        }

        /** @brief ロガーへ登録する（二重に呼んでも 1 回だけ登録する） */
        void Attach()
        {
#if NORVES_ENABLE_LOGGING
            if (!m_bAttached)
            {
                Core::Logging::Logger::GetInstance().AddSink(this);
                m_bAttached = true;
            }
#endif
        }

        /** @brief ロガーから外す（ロガーの終了より前に呼ぶ。登録していなければ何もしない） */
        void Detach()
        {
#if NORVES_ENABLE_LOGGING
            if (m_bAttached)
            {
                Core::Logging::Logger::GetInstance().RemoveSink(this);
                m_bAttached = false;
            }
#endif
        }

        /** @brief VISBUFFER_FALLBACK（解決を使えず GBuffer の描画へ戻った）を見た数 */
        uint32_t GetFallbackCount() const
        {
            return m_FallbackCount.Load();
        }

        /** @brief VISBUFFER_RESOLVE_TILES（解決を記録した）を見た数。材質ごとのタイルでも画面全体の dispatch でも数える */
        uint32_t GetResolveCount() const
        {
            return m_ResolveCount.Load();
        }

        void OnLog(const Core::Logging::LogEntry& entry) override
        {
            if (entry.category != "VisibilityResolvePass")
            {
                return;
            }
            if (std::strstr(entry.message.c_str(), "VISBUFFER_FALLBACK") != nullptr)
            {
                m_FallbackCount.FetchAdd(1u);
            }
            else if (std::strstr(entry.message.c_str(), "VISBUFFER_RESOLVE_TILES") != nullptr)
            {
                m_ResolveCount.FetchAdd(1u);
            }
        }

    private:
        NorvesLib::Thread::Atomic<uint32_t> m_FallbackCount{0u};
        NorvesLib::Thread::Atomic<uint32_t> m_ResolveCount{0u};
        bool m_bAttached = false;
    };
}

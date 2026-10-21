#include "Debug/GeometrySettleTrace.h"

// 検証用の記録なので、開発ビルドだけに入れる（Release には入れない）
#if NORVES_BUILD_DEVELOPMENT

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Engine/SkeletalAssetSession.h"
#include "Core/Public/Logging/LogMacros.h"
#include "Core/Public/Rendering/RenderResources.h"
#include "Core/Public/Rendering/RenderWorld.h"

#include <cstdio>

namespace Game::Debug
{
    namespace
    {
        // 段ごとの数を "a/b/c"（添字が段）にして buffer へ書く。空なら "-"
        void FormatLevels(const NorvesLib::Core::Container::VariableArray<uint32_t>& counts, char* buffer, size_t size)
        {
            if (counts.empty())
            {
                std::snprintf(buffer, size, "-");
                return;
            }
            size_t written = 0;
            for (size_t level = 0; level < counts.size() && written < size; ++level)
            {
                const int length = std::snprintf(buffer + written, size - written, level == 0 ? "%u" : "/%u",
                                                 static_cast<unsigned>(counts[level]));
                if (length < 0)
                {
                    break;
                }
                written += static_cast<size_t>(length);
            }
        }

        // ApplicationProcessor の --capture-png と同じ読み込み中の判定（非同期の資産・骨格の資産・決定的な撮影での SceneReady の前）
        bool IsLoading(NorvesLib::Core::Rendering::RenderWorld& renderWorld)
        {
            if (renderWorld.HasPendingAsyncAssets() ||
                NorvesLib::Core::GEngine.GetSkeletalAssetSession().GetPendingCount() != 0)
            {
                return true;
            }
            const auto& deterministicCapture = NorvesLib::Core::Engine::GEngine->GetDeterministicCapture();
            return deterministicCapture.IsEnabled() && !deterministicCapture.IsSceneReady();
        }
    } // namespace

    void GeometrySettleTrace::OnPostRender()
    {
        if (!IsEnabled() || m_State == State::Done || !NorvesLib::Core::Engine::GEngine)
        {
            return;
        }
        NorvesLib::Core::Rendering::RenderWorld& renderWorld = NorvesLib::Core::Engine::GEngine->GetRenderWorld();

        if (m_State == State::WaitingSettle)
        {
            ++m_FrameCount;
            if (IsLoading(renderWorld))
            {
                m_bSawPending = true;
                m_QuietFrames = 0;
                return;
            }
            ++m_QuietFrames;
            const bool bSettled = m_bSawPending ? m_QuietFrames >= kQuietFrames : m_FrameCount >= kFallbackFrames;
            if (!bSettled)
            {
                return;
            }
            m_State = State::WaitingStreaming;
            m_StreamingWaitStart = std::chrono::steady_clock::now();
            m_StreamingQuietFrames = 0;
            m_LastStreamingActivity = ~0ull;
            return;
        }

        if (m_State == State::WaitingStreaming)
        {
            NorvesLib::Core::Rendering::GeometryPageStreamingSnapshot snapshot;
            if (!NorvesLib::Core::Engine::GEngine->GetRenderResources().MegaGeometry().GetPageStreamingSnapshot(snapshot))
            {
                BeginTrace(false); // ストリーマが無い: 記録の最初の行で失敗を書く
                return;
            }
            const uint64_t activity = snapshot.PagesUploaded + snapshot.EvictedPages;
            if (activity != m_LastStreamingActivity)
            {
                m_LastStreamingActivity = activity;
                m_StreamingQuietFrames = 0;
            }
            else
            {
                ++m_StreamingQuietFrames;
            }
            if (m_StreamingQuietFrames >= kStreamingQuietFrames)
            {
                BeginTrace(false);
                return;
            }
            const double waited =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StreamingWaitStart).count();
            if (waited >= kMaxStreamingWaitSeconds)
            {
                BeginTrace(true);
            }
            return;
        }

        // 1 秒ごとに 1 行。決めた秒数の行を書いたら終える
        const double elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - m_StartTime).count();
        if (elapsed < static_cast<double>(m_SamplesWritten))
        {
            return;
        }
        WriteSample(elapsed);
        if (m_SamplesWritten > m_Seconds)
        {
            m_State = State::Done;
            LOG_INFO_F("GEOM_SETTLE_TRACE_END samples=%u", m_SamplesWritten);
        }
    }

    void GeometrySettleTrace::BeginTrace(bool bTimedOut)
    {
        m_State = State::Tracing;
        m_StartTime = std::chrono::steady_clock::now();
        const double waited = std::chrono::duration<double>(m_StartTime - m_StreamingWaitStart).count();
        LOG_INFO_F("GEOM_SETTLE_TRACE_BEGIN seconds=%u rendered=%llu saw_pending=%d streaming_wait_s=%.2f settle_timeout=%d",
                   m_Seconds,
                   static_cast<unsigned long long>(NorvesLib::Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount()),
                   m_bSawPending ? 1 : 0, waited, bTimedOut ? 1 : 0);
        WriteSample(0.0);
    }

    void GeometrySettleTrace::WriteSample(double elapsedSeconds)
    {
        NorvesLib::Core::Rendering::RenderResources& resources = NorvesLib::Core::Engine::GEngine->GetRenderResources();
        NorvesLib::Core::Rendering::GeometryPageStreamingSnapshot snapshot;
        if (!resources.MegaGeometry().GetPageStreamingSnapshot(snapshot))
        {
            LOG_ERROR_F("GEOM_SETTLE_TRACE failed reason=no_page_streamer");
            m_State = State::Done;
            return;
        }
        const NorvesLib::Core::Rendering::VideoMemoryBudgetResult& budget = resources.GetVideoMemoryBudgetResult();
        constexpr double kBytesPerMb = 1024.0 * 1024.0;
        char residentLevels[256] = {};
        char pendingLevels[256] = {};
        FormatLevels(snapshot.ResidentPagesPerLevel, residentLevels, sizeof(residentLevels));
        FormatLevels(snapshot.PendingPagesPerLevel, pendingLevels, sizeof(pendingLevels));
        LOG_INFO_F("GEOM_SETTLE_TRACE t=%.2f rendered=%llu wanted=%u reading=%u ready=%u uploading=%u resident=%u "
                   "resident_mb=%.2f page_budget_mb=%.2f limited=%d fixed_mb=%.2f evicted=%llu uploaded=%llu "
                   "budget_blocked=%llu pool_used_mb=%.2f pool_capacity_mb=%.2f geometry_target_mb=%.2f "
                   "ceiling_mb=%.2f request_overflow=%llu levels_resident=%s levels_pending=%s",
                   elapsedSeconds,
                   static_cast<unsigned long long>(NorvesLib::Core::Engine::GEngine->GetRenderWorld().GetRenderedFrameCount()),
                   snapshot.WantedPages, snapshot.ReadingPages, snapshot.ReadyPages, snapshot.UploadingPages,
                   snapshot.ResidentPages, static_cast<double>(snapshot.ResidentBytes) / kBytesPerMb,
                   static_cast<double>(snapshot.BudgetBytes) / kBytesPerMb, snapshot.bBudgetLimited ? 1 : 0,
                   static_cast<double>(snapshot.FixedBytes) / kBytesPerMb,
                   static_cast<unsigned long long>(snapshot.EvictedPages),
                   static_cast<unsigned long long>(snapshot.PagesUploaded),
                   static_cast<unsigned long long>(snapshot.BudgetBlockedFrames),
                   static_cast<double>(snapshot.PoolUsedBytes) / kBytesPerMb,
                   static_cast<double>(snapshot.PoolCapacityBytes) / kBytesPerMb,
                   static_cast<double>(budget.GetTargetBytes(NorvesLib::Core::Rendering::VideoMemoryPool::Geometry)) /
                       kBytesPerMb,
                   static_cast<double>(budget.CeilingBytes) / kBytesPerMb,
                   static_cast<unsigned long long>(snapshot.RequestOverflowTotal), residentLevels, pendingLevels);
        ++m_SamplesWritten;
    }
} // namespace Game::Debug

#endif // NORVES_BUILD_DEVELOPMENT

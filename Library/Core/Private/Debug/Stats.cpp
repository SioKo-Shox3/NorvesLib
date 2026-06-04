#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <sstream>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace NorvesLib::Debug
{
    namespace
    {
        uint32_t GetCurrentProfileThreadId()
        {
#ifdef _WIN32
            return static_cast<uint32_t>(::GetCurrentThreadId());
#else
            return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
        }
    } // namespace

#if NORVES_ENABLE_STATS

    ScopedStat::~ScopedStat()
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
        const float durationMs = static_cast<float>(duration.count()) * 0.001f;

        StatsManager::Get().RecordScope(m_Name, m_Category, durationMs);

        NORVES_LOG_TRACE(m_Category, "[STAT] %s: %lld us (%.3f ms)",
                         m_Name, duration.count(), durationMs);
    }

#endif // NORVES_ENABLE_STATS

    String FrameProfile::ToString() const
    {
        std::ostringstream oss;
        oss << "=== Frame Profile ===\n";
        oss << "Frame: " << FrameNumber << "\n";
        oss << "FPS: " << FPS << " (DeltaTime: " << DeltaTime * 1000.0f << " ms)\n";
        oss << "CPU:\n";
        oss << "  GameThread: " << GameThreadTimeMs << " ms\n";
        oss << "  RenderPrepare: " << RenderPrepareTimeMs << " ms\n";
        oss << "  RenderThread: " << RenderThreadTimeMs << " ms\n";
        oss << "  RenderFrame: " << RenderFrameTimeMs << " ms\n";
        oss << "  CPU Frame: " << CPUFrameTimeMs << " ms\n";
        oss << "GPU: " << GPUFrameTimeMs << " ms\n";
        oss << "Total Frame: " << TotalFrameTimeMs << " ms\n";
        oss << "Draw Calls: " << DrawCalls << "\n";
        oss << "Triangles: " << TrianglesRendered << "\n";
        oss << "Visible Objects: " << VisibleObjects << " (Batches: " << BatchCount << ")\n";

        if (!Events.empty())
        {
            oss << "Scopes:\n";
            for (const auto &event : Events)
            {
                oss << "  [" << event.Category.c_str() << "] " << event.Name.c_str()
                    << ": " << event.DurationMs << " ms"
                    << " (T:" << event.ThreadId << ")\n";
            }
        }

        return String(oss.str());
    }

    String RenderingStats::ToString() const
    {
        std::ostringstream oss;
        oss << "=== Rendering Stats ===\n";
        oss << "Frame: " << FrameNumber << "\n";
        oss << "FPS: " << FPS << " (DeltaTime: " << DeltaTime * 1000.0f << " ms)\n";
        oss << "Draw Calls: " << DrawCalls << " (Instanced: " << InstancedDrawCalls << ")\n";
        oss << "Triangles: " << TrianglesRendered << "\n";
        oss << "Visible Objects: " << VisibleObjects << " (Batches: " << BatchCount << ")\n";
        oss << "Timings:\n";
        oss << "  Collection: " << CollectionTimeMs << " ms\n";
        oss << "  Culling: " << CullingTimeMs << " ms\n";
        oss << "  Batching: " << BatchingTimeMs << " ms\n";
        oss << "  Command Gen: " << CommandGenerationTimeMs << " ms\n";
        oss << "  GameThread: " << GameThreadTimeMs << " ms\n";
        oss << "  RenderThread: " << RenderThreadTimeMs << " ms\n";
        oss << "  RenderFrame: " << RenderFrameTimeMs << " ms\n";
        oss << "  GPU: " << GPUTimeMs << " ms\n";
        oss << "  Total Frame: " << TotalFrameTimeMs << " ms\n";
        return String(oss.str());
    }

    String MemoryStats::ToString() const
    {
        std::ostringstream oss;
        oss << "=== Memory Stats ===\n";
        oss << "Current Usage: " << (CurrentUsage / 1024.0 / 1024.0) << " MB\n";
        oss << "Peak Usage: " << (PeakUsage / 1024.0 / 1024.0) << " MB\n";
        oss << "Total Allocated: " << (TotalAllocated / 1024.0 / 1024.0) << " MB\n";
        oss << "Total Freed: " << (TotalFreed / 1024.0 / 1024.0) << " MB\n";
        oss << "Allocation Count: " << AllocationCount << "\n";
        oss << "Deallocation Count: " << DeallocationCount << "\n";
        return String(oss.str());
    }

    StatsManager& StatsManager::Get()
    {
        static StatsManager instance;
        return instance;
    }

    FrameProfile StatsManager::GetFrameProfileSnapshot() const
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        return m_FrameProfile;
#else
        return FrameProfile{};
#endif
    }

    void StatsManager::BeginFrame(uint64_t frameNumber, float deltaTime)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.Reset();
        m_FrameProfile.FrameNumber = frameNumber;
        m_FrameProfile.DeltaTime = deltaTime;
        m_FrameProfile.FPS = deltaTime > 0.0f ? 1.0f / deltaTime : 0.0f;

        m_RenderingStats.FrameNumber = frameNumber;
        m_RenderingStats.DeltaTime = deltaTime;
        m_RenderingStats.FPS = m_FrameProfile.FPS;
#else
        (void)frameNumber;
        (void)deltaTime;
#endif
    }

    void StatsManager::EndFrame()
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.CPUFrameTimeMs = m_FrameProfile.GameThreadTimeMs + m_FrameProfile.RenderThreadTimeMs;
        m_FrameProfile.TotalFrameTimeMs = m_FrameProfile.CPUFrameTimeMs;

        m_RenderingStats.GameThreadTimeMs = m_FrameProfile.GameThreadTimeMs;
        m_RenderingStats.RenderThreadTimeMs = m_FrameProfile.RenderThreadTimeMs;
        m_RenderingStats.RenderFrameTimeMs = m_FrameProfile.RenderFrameTimeMs;
        m_RenderingStats.GPUTimeMs = m_FrameProfile.GPUFrameTimeMs;
        m_RenderingStats.TotalFrameTimeMs = m_FrameProfile.TotalFrameTimeMs;
#endif
    }

    void StatsManager::RecordScope(const char* name, const char* category, float durationMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (m_FrameProfile.Events.size() >= Detail::MaxFrameProfileEvents)
        {
            return;
        }

        ProfileEvent event;
        event.Name = name ? String(name) : String{};
        event.Category = category ? String(category) : String{};
        event.DurationMs = durationMs;
        event.ThreadId = GetCurrentProfileThreadId();
        m_FrameProfile.Events.push_back(event);
#else
        (void)name;
        (void)category;
        (void)durationMs;
#endif
    }

    void StatsManager::SetGameThreadTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.GameThreadTimeMs = timeMs;
        m_RenderingStats.GameThreadTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderPrepareTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.RenderPrepareTimeMs = timeMs;
        m_RenderingStats.CommandGenerationTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderThreadTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.RenderThreadTimeMs = timeMs;
        m_RenderingStats.RenderThreadTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderFrameTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.RenderFrameTimeMs = timeMs;
        m_RenderingStats.RenderFrameTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetGPUFrameTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_FrameProfile.GPUFrameTimeMs = timeMs;
        m_RenderingStats.GPUTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::UpdateRenderingStats(const RenderingStats &stats)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_RenderingStats = stats;
        m_FrameProfile.DrawCalls = stats.DrawCalls;
        m_FrameProfile.TrianglesRendered = stats.TrianglesRendered;
        m_FrameProfile.VisibleObjects = stats.VisibleObjects;
        m_FrameProfile.BatchCount = stats.BatchCount;
        m_FrameProfile.RenderPrepareTimeMs = stats.CommandGenerationTimeMs;
        m_FrameProfile.RenderFrameTimeMs = stats.RenderFrameTimeMs;
        m_FrameProfile.GPUFrameTimeMs = stats.GPUTimeMs;
#else
        (void)stats;
#endif
    }

} // namespace NorvesLib::Debug

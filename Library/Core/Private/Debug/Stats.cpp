#include "Debug/Stats.h"
#include <algorithm>
#include <filesystem>
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

        void UpdateFrameTotals(FrameProfile &profile)
        {
            profile.CPUFrameTimeMs = std::max(profile.GameThreadTimeMs, profile.RenderThreadTimeMs);
            profile.TotalFrameTimeMs = std::max(profile.CPUFrameTimeMs, profile.GPUFrameTimeMs);
        }

        uint64_t GetTraceTimestampUs()
        {
            auto now = std::chrono::system_clock::now();
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
        }

        void WriteCsvString(std::ofstream &stream, const String &value)
        {
            stream << '"';
            for (size_t i = 0; i < value.size(); ++i)
            {
                const char ch = static_cast<char>(value[i]);
                if (ch == '"')
                {
                    stream << "\"\"";
                }
                else
                {
                    stream << ch;
                }
            }
            stream << '"';
        }
    } // namespace

#if NORVES_ENABLE_STATS

    ScopedStat::~ScopedStat()
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
        const float durationMs = static_cast<float>(duration.count()) * 0.001f;

        StatsManager::Get().RecordScope(m_Name, m_Category, durationMs);
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
        oss << "RenderGraph: barriers=" << RenderGraphBarrierCount
            << " transientAcquires=" << RenderGraphTransientAcquireCount << "\n";
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

    bool StatsManager::StartTrace(const String& outputPath)
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);

        if (m_TraceFile.is_open())
        {
            m_TraceFile.flush();
            m_TraceFile.close();
        }

        m_TraceOutputPath = outputPath.empty() ? String("NorvesLib.trace.csv") : outputPath;

        try
        {
            std::filesystem::path tracePath(m_TraceOutputPath.c_str());
            if (tracePath.has_parent_path())
            {
                std::filesystem::create_directories(tracePath.parent_path());
            }

            m_TraceFile.open(tracePath, std::ios::out | std::ios::trunc);
        }
        catch (...)
        {
            m_bTraceActive.Store(false, std::memory_order_release);
            return false;
        }

        if (!m_TraceFile.is_open())
        {
            m_bTraceActive.Store(false, std::memory_order_release);
            return false;
        }

        m_FrameProfile.Reset();
        m_RenderingStats.Reset();
        WriteTraceHeader();
        m_bTraceActive.Store(true, std::memory_order_release);
        return true;
#else
        (void)outputPath;
        return false;
#endif
    }

    void StatsManager::StopTrace()
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        m_bTraceActive.Store(false, std::memory_order_release);

        if (m_TraceFile.is_open())
        {
            m_TraceFile.flush();
            m_TraceFile.close();
        }
#endif
    }

    String StatsManager::GetTraceOutputPath() const
    {
#if NORVES_ENABLE_STATS
        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        return m_TraceOutputPath;
#else
        return String{};
#endif
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
        if (!IsTraceActive())
        {
            (void)frameNumber;
            (void)deltaTime;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

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
        if (!IsTraceActive())
        {
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        UpdateFrameTotals(m_FrameProfile);

        m_RenderingStats.GameThreadTimeMs = m_FrameProfile.GameThreadTimeMs;
        m_RenderingStats.RenderThreadTimeMs = m_FrameProfile.RenderThreadTimeMs;
        m_RenderingStats.RenderFrameTimeMs = m_FrameProfile.RenderFrameTimeMs;
        m_RenderingStats.GPUTimeMs = m_FrameProfile.GPUFrameTimeMs;
        m_RenderingStats.TotalFrameTimeMs = m_FrameProfile.TotalFrameTimeMs;

        WriteFrameTraceLine();
#endif
    }

    void StatsManager::RecordScope(const char* name, const char* category, float durationMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)name;
            (void)category;
            (void)durationMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

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
        WriteScopeTraceLine(event);
#else
        (void)name;
        (void)category;
        (void)durationMs;
#endif
    }

    void StatsManager::SetGameThreadTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)timeMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_FrameProfile.GameThreadTimeMs = timeMs;
        UpdateFrameTotals(m_FrameProfile);
        m_RenderingStats.GameThreadTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderPrepareTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)timeMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_FrameProfile.RenderPrepareTimeMs = timeMs;
        m_RenderingStats.CommandGenerationTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderThreadTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)timeMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_FrameProfile.RenderThreadTimeMs = timeMs;
        UpdateFrameTotals(m_FrameProfile);
        m_RenderingStats.RenderThreadTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetRenderFrameTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)timeMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_FrameProfile.RenderFrameTimeMs = timeMs;
        m_RenderingStats.RenderFrameTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::SetGPUFrameTimeMs(float timeMs)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)timeMs;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_FrameProfile.GPUFrameTimeMs = timeMs;
        UpdateFrameTotals(m_FrameProfile);
        m_RenderingStats.GPUTimeMs = timeMs;
#else
        (void)timeMs;
#endif
    }

    void StatsManager::UpdateRenderingStats(const RenderingStats &stats)
    {
#if NORVES_ENABLE_STATS
        if (!IsTraceActive())
        {
            (void)stats;
            return;
        }

        NorvesLib::Thread::ScopedLock lock(m_Mutex);
        if (!IsTraceActive())
        {
            return;
        }

        m_RenderingStats = stats;
        m_FrameProfile.DrawCalls = stats.DrawCalls;
        m_FrameProfile.TrianglesRendered = stats.TrianglesRendered;
        m_FrameProfile.VisibleObjects = stats.VisibleObjects;
        m_FrameProfile.BatchCount = stats.BatchCount;
        m_FrameProfile.RenderPrepareTimeMs = stats.CommandGenerationTimeMs;
        m_FrameProfile.RenderFrameTimeMs = stats.RenderFrameTimeMs;
        m_FrameProfile.GPUFrameTimeMs = stats.GPUTimeMs;
        UpdateFrameTotals(m_FrameProfile);
#else
        (void)stats;
#endif
    }

    void StatsManager::WriteTraceHeader()
    {
#if NORVES_ENABLE_STATS
        if (m_TraceFile.is_open())
        {
            m_TraceFile << "Type,Frame,TimestampUs,ThreadId,Category,Name,DurationMs,"
                           "GameThreadMs,RenderPrepareMs,RenderThreadMs,RenderFrameMs,"
                           "CPUFrameMs,GPUFrameMs,TotalFrameMs,DrawCalls,Triangles,VisibleObjects,Batches,"
                           "RenderGraphBarriers,RenderGraphTransientAcquires\n";
        }
#endif
    }

    void StatsManager::WriteFrameTraceLine()
    {
#if NORVES_ENABLE_STATS
        if (!m_TraceFile.is_open())
        {
            return;
        }

        m_TraceFile << "Frame,"
                    << m_FrameProfile.FrameNumber << ','
                    << GetTraceTimestampUs() << ','
                    << GetCurrentProfileThreadId() << ',';
        WriteCsvString(m_TraceFile, String("Frame"));
        m_TraceFile << ',';
        WriteCsvString(m_TraceFile, String("Frame"));
        m_TraceFile << ",,"
                    << m_FrameProfile.GameThreadTimeMs << ','
                    << m_FrameProfile.RenderPrepareTimeMs << ','
                    << m_FrameProfile.RenderThreadTimeMs << ','
                    << m_FrameProfile.RenderFrameTimeMs << ','
                    << m_FrameProfile.CPUFrameTimeMs << ','
                    << m_FrameProfile.GPUFrameTimeMs << ','
                    << m_FrameProfile.TotalFrameTimeMs << ','
                    << m_FrameProfile.DrawCalls << ','
                    << m_FrameProfile.TrianglesRendered << ','
                    << m_FrameProfile.VisibleObjects << ','
                    << m_FrameProfile.BatchCount << ','
                    << m_RenderingStats.RenderGraphBarrierCount << ','
                    << m_RenderingStats.RenderGraphTransientAcquireCount << '\n';
        m_TraceFile.flush();
#endif
    }

    void StatsManager::WriteScopeTraceLine(const ProfileEvent& event)
    {
#if NORVES_ENABLE_STATS
        if (!m_TraceFile.is_open())
        {
            return;
        }

        m_TraceFile << "Scope,"
                    << m_FrameProfile.FrameNumber << ','
                    << GetTraceTimestampUs() << ','
                    << event.ThreadId << ',';
        WriteCsvString(m_TraceFile, event.Category);
        m_TraceFile << ',';
        WriteCsvString(m_TraceFile, event.Name);
        m_TraceFile << ','
                    << event.DurationMs
                    << ",,,,,,,,,,,,,\n";
#endif
    }

} // namespace NorvesLib::Debug

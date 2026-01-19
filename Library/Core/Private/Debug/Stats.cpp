#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <sstream>

namespace NorvesLib::Debug
{

#if NORVES_ENABLE_STATS

    ScopedStat::~ScopedStat()
    {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - m_StartTime);
        
        NORVES_LOG_TRACE(m_Category, "[STAT] %s: %lld μs (%.3f ms)", 
            m_Name, duration.count(), duration.count() / 1000.0);
    }

#endif // NORVES_ENABLE_STATS

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

} // namespace NorvesLib::Debug

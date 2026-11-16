// 共通の生成数計算を使い、容量と出力総数の上限を守る生成ジョブを作る。
#include "Rendering/GpuParticle/GpuParticleSpawnPlanner.h"

#include "Particle/VfxSimulationMath.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    uint32_t GpuParticleSpawnPlanner::Plan(Container::Span<const GpuParticleSpawnRequest> requests,
        Container::Span<GpuParticleSpawnState> states, VariableArray<GpuSpawnJob>& outJobs)
    {
        if (requests.size() != states.size())
        {
            return 0u;
        }

        outJobs.clear();
        constexpr uint32_t maxCount = std::numeric_limits<uint32_t>::max();
        uint32_t totalCount = 0u;
        for (size_t index = 0u; index < requests.size(); ++index)
        {
            const GpuParticleSpawnRequest& request = requests[index];
            GpuParticleSpawnState& state = states[index];
            uint32_t requested = 0u;
            if (std::isfinite(request.Rate) && request.Rate >= 0.0f &&
                std::isfinite(request.DeltaTime) && request.DeltaTime >= 0.0f)
            {
                requested = Particle::VfxAdvanceSpawnAccumulator(state.Accumulator, request.Rate, request.DeltaTime);
            }
            requested += request.BurstCount < maxCount - requested ? request.BurstCount : maxCount - requested;

            uint32_t count = requested < request.Capacity ? requested : request.Capacity;
            const uint32_t remainingCount = maxCount - totalCount;
            count = count < remainingCount ? count : remainingCount;
            const uint32_t firstSerial = state.NextSerial;
            // 容量や出力上限で省いた粒も通し番号を消費する。
            state.NextSerial += requested;
            if (count > 0u)
            {
                outJobs.push_back(GpuSpawnJob{request.EmitterIndex, count, firstSerial, totalCount});
                totalCount += count;
            }
        }
        return totalCount;
    }
} // namespace NorvesLib::Core::Rendering

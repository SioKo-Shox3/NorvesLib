// エミッタの生成要求から、決定的な生成ジョブと通し番号を計画する。
#pragma once

#include "Container/Span.h"
#include "CoreTypes.h"
#include "Rendering/GpuParticle/GpuParticleTypes.h"

namespace NorvesLib::Core::Rendering
{
    /// エミッタごとに 1 刻みで持ち越す生成の状態。
    struct GpuParticleSpawnState
    {
        float Accumulator = 0.0f; // 生成数の端数。
        uint32_t NextSerial = 0u; // 次に生成する粒の通し番号。
    };

    /// 1 刻みのエミッタごとの生成の要求。
    struct GpuParticleSpawnRequest
    {
        uint32_t EmitterIndex = 0u; // GpuEmitterParams の配列添字。
        float Rate = 0.0f; // 毎秒の生成数。
        float DeltaTime = 0.0f; // エミッタの時間の流れの dt。
        uint32_t BurstCount = 0u; // この刻みの単発の生成数。
        uint32_t Capacity = 0u; // エミッタの範囲の容量。
    };

    /// CPU で生成数と出力の排他的 prefix sum を計画する。
    class GpuParticleSpawnPlanner
    {
    public:
        /// 同じ長さ・順序の要求と状態から状態を更新し、出力を生成数が正のジョブで置き換える。
        /// 戻り値は生成数の飽和合計。長さが異なる場合は出力も状態も変えずに 0 を返す。
        static uint32_t Plan(Container::Span<const GpuParticleSpawnRequest> requests,
            Container::Span<GpuParticleSpawnState> states, VariableArray<GpuSpawnJob>& outJobs);
    };
} // namespace NorvesLib::Core::Rendering

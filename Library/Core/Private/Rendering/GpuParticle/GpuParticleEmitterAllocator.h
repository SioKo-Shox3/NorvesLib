// 粒プール内のエミッタの範囲を、世代つきハンドルで確保・解放する。
#pragma once

#include "CoreTypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /// 解放後の再利用を世代で区別するエミッタのハンドル。
    struct GpuParticleEmitterHandle
    {
        uint32_t Index = 0xFFFFFFFFu;
        uint32_t Generation = 0u;

        bool IsValid() const;
        bool operator==(const GpuParticleEmitterHandle&) const = default;
    };

    /// 粒プール内の連続した範囲。
    struct GpuParticleEmitterRange
    {
        uint32_t Offset = 0u;
        uint32_t Capacity = 0u;
    };

    /// オフセット順の空き範囲から first-fit で確保する。
    class GpuParticleEmitterAllocator
    {
    public:
        explicit GpuParticleEmitterAllocator(uint32_t poolCapacity);
        GpuParticleEmitterHandle Allocate(uint32_t capacity);
        bool Free(GpuParticleEmitterHandle handle);
        /// 有効なハンドルの範囲を返す。失敗時は outRange を変えない。
        bool TryGetRange(GpuParticleEmitterHandle handle, GpuParticleEmitterRange& outRange) const;
        bool IsLive(GpuParticleEmitterHandle handle) const;
        uint32_t GetPoolCapacity() const;
        uint32_t GetUsedCapacity() const;
        uint32_t GetLargestFreeBlock() const;

    private:
        struct EmitterSlot
        {
            GpuParticleEmitterRange Range;
            uint32_t Generation = 1u;
            bool bLive = false;
        };

        uint32_t m_PoolCapacity = 0u;
        uint32_t m_UsedCapacity = 0u;
        VariableArray<GpuParticleEmitterRange> m_FreeRanges;
        VariableArray<EmitterSlot> m_Slots;
        VariableArray<uint32_t> m_FreeSlots;
    };
} // namespace NorvesLib::Core::Rendering

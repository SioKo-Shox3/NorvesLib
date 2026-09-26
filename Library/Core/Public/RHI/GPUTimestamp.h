#pragma once

#include "Container/String.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    inline constexpr uint32_t MaximumGPUTimestampScopesPerFrame = 128u;

    struct GPUTimestampScopeHandle
    {
        uint32_t FrameSlotIndex = UINT32_MAX;
        uint32_t ScopeIndex = UINT32_MAX;
        uint64_t FrameNumber = 0u;

        bool IsValid() const
        {
            return FrameSlotIndex != UINT32_MAX &&
                   ScopeIndex < MaximumGPUTimestampScopesPerFrame;
        }
    };

    struct GPUTimestampResult
    {
        uint64_t FrameNumber = 0u;
        Core::Container::String ScopeName;
        float DurationMs = 0.0f;
        bool bValid = false;
    };
} // namespace NorvesLib::RHI

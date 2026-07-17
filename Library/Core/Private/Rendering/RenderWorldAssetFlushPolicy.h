#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering::Detail
{
    enum class AssetGpuFlushAction : uint8_t
    {
        FlushAndRender,
        DeferFlushAndRender,
        DeferFlushAndSkipRender
    };

    [[nodiscard]] constexpr AssetGpuFlushAction DecideAssetGpuFlushAction(bool bRenderThreadRunning,
                                                                          bool bRenderThreadQuiesced,
                                                                          bool bAssetGpuFlushWindowAcquired,
                                                                          bool bHasPendingAsyncAssets) noexcept
    {
        if (!bRenderThreadRunning || bRenderThreadQuiesced)
        {
            return AssetGpuFlushAction::FlushAndRender;
        }

        if (!bHasPendingAsyncAssets)
        {
            return AssetGpuFlushAction::DeferFlushAndRender;
        }

        return bAssetGpuFlushWindowAcquired
                   ? AssetGpuFlushAction::FlushAndRender
                   : AssetGpuFlushAction::DeferFlushAndSkipRender;
    }
} // namespace NorvesLib::Core::Rendering::Detail

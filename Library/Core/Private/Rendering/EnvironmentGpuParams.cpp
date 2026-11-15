#include "Rendering/EnvironmentGpuParams.h"

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ViewRenderContext.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    GPUEnvironmentParams PackEnvironmentGpuParams(const EnvironmentParameters& environment)
    {
        GPUEnvironmentParams result = MakeDefaultEnvironmentGpuParams();
        if (!environment.bEnabled)
        {
            return result;
        }

        const EnvironmentParameters sanitized = SanitizeEnvironmentParameters(environment);
        result.Wetness = sanitized.Wetness;
        result.RainIntensity = sanitized.RainIntensity;
        result.CloudCover = sanitized.CloudCover;
        result.GameTimeWrapped = static_cast<float>(std::fmod(sanitized.GameTimeSeconds, EnvironmentGameTimeWrapSeconds));
        result.Seed01 = static_cast<float>(static_cast<double>(sanitized.Seed) / 4294967296.0);
        result.Discontinuity = static_cast<float>(sanitized.DiscontinuityId & 0xFFFFFFu);
        return result;
    }

    FrameUploadAllocation AcquireEnvironmentConstants(const ViewRenderContext& context)
    {
        if (context.EnvironmentConstants.IsValid())
        {
            return context.EnvironmentConstants;
        }
        if (context.FrameResources)
        {
            return context.FrameResources->AllocateConstants(context.EnvironmentGpu);
        }
        return {};
    }
} // namespace NorvesLib::Core::Rendering

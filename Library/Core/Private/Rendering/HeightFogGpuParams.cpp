// レンダースレッドのスナップショットから前向き描画用の高さフォグ定数を構築する。
#include "Rendering/HeightFogGpuParams.h"

#include "Rendering/ViewRenderContext.h"
#include "Rendering/SceneProxy.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        float SafePreExposure(const CameraProxy& camera)
        {
            if (!std::isfinite(camera.PreExposure) || camera.PreExposure <= 0.0f)
            {
                return 1.0f;
            }
            return std::clamp(camera.PreExposure, 1.0e-6f, 1.0e6f);
        }

        float SafeFogColorChannel(float value)
        {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        }
    }

    GPUHeightFogParams BuildHeightFogGpuParams(const ViewRenderContext& context,
                                              const CameraProxy& camera,
                                              bool bSkyRadianceAvailable,
                                              const VolumetricFogParameters* fogOverride)
    {
        const SceneProxy* scene = context.SnapshotScene;
        const VolumetricFogParameters fog = fogOverride
            ? *fogOverride
            : (scene ? SanitizeVolumetricFogParameters(scene->VolumetricFog)
                     : MakeDefaultVolumetricFogParameters());

        return GPUHeightFogParams{
            {fog.bEnabled ? fog.DensityAtBaseHeight : 0.0f,
             fog.BaseHeight,
             fog.HeightFalloffPerUnit,
             bSkyRadianceAvailable ? 1.0f : 0.0f},
            {1.0f, 1.0f, 1.0f, 0.0f},
            {scene ? SafeFogColorChannel(scene->FogColorR) : 0.0f,
             scene ? SafeFogColorChannel(scene->FogColorG) : 0.0f,
             scene ? SafeFogColorChannel(scene->FogColorB) : 0.0f,
             fog.AmbientScale},
            {camera.PositionX, camera.PositionY, camera.PositionZ, SafePreExposure(camera)},
            {fog.MaxDistanceMeters, 0.0f, 0.0f, 0.0f}};
    }

} // namespace NorvesLib::Core::Rendering

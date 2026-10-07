#include "Rendering/VirtualShadowMapSample.h"

#include "Rendering/VirtualShadowMapPass.h"

#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 絶対のページの番号を int32 でシェーダーへ渡せる範囲（印付けと同じ。範囲の端 + 128 ページが溢れない余裕を持つ）
        constexpr int64_t MaxOriginMagnitude = 1ll << 30;

        void CopyVector(float* destination, const Math::Vector3& value)
        {
            destination[0] = value.x;
            destination[1] = value.y;
            destination[2] = value.z;
        }
    } // namespace

    bool BuildVirtualShadowMapSampleParams(const VirtualShadowMapClipmap* clipmap,
                                           const float* cameraPosition,
                                           float fovYDegrees,
                                           float screenHeightPixels,
                                           uint32_t poolPages,
                                           GPUVsmSampleParams& outParams)
    {
        std::memset(&outParams, 0, sizeof(outParams));
        if (clipmap == nullptr || cameraPosition == nullptr || !clipmap->bEnabled || clipmap->LevelCount == 0u ||
            clipmap->LevelCount > VirtualShadowMap::LEVEL_COUNT ||
            clipmap->PagesPerAxis != VirtualShadowMap::TABLE_DIMENSION ||
            clipmap->Settings.PageResolution != VirtualShadowMap::PAGE_RESOLUTION || poolPages == 0u ||
            poolPages > VirtualShadowMap::MAX_POOL_PAGES || !(clipmap->Settings.DepthRangeMeters > 0.0f))
        {
            return false;
        }
        const float pixelMeters = VirtualShadowMapScreenPixelMeters(1.0f, fovYDegrees, screenHeightPixels);
        if (!(pixelMeters > 0.0f) || !std::isfinite(cameraPosition[0]) || !std::isfinite(cameraPosition[1]) ||
            !std::isfinite(cameraPosition[2]))
        {
            return false;
        }

        GPUVsmSampleParams params;
        std::memset(&params, 0, sizeof(params));
        if (!VirtualShadowMapLevelDistanceThresholds(clipmap->Settings, fovYDegrees, screenHeightPixels, params.thresholds))
        {
            return false;
        }
        for (uint32_t level = 0; level < clipmap->LevelCount; ++level)
        {
            const VirtualShadowMapClipmapLevel& data = clipmap->Levels[level];
            if (data.OriginPageX <= -MaxOriginMagnitude || data.OriginPageX >= MaxOriginMagnitude ||
                data.OriginPageY <= -MaxOriginMagnitude || data.OriginPageY >= MaxOriginMagnitude || !(data.PageMeters > 0.0f) ||
                !(data.TexelMeters > 0.0f))
            {
                return false;
            }
            params.levelInfo[level][0] = data.PageMeters;
            params.levelInfo[level][1] = data.TexelMeters;
            params.levelOrigin[level][0] = static_cast<int32_t>(data.OriginPageX);
            params.levelOrigin[level][1] = static_cast<int32_t>(data.OriginPageY);
        }

        CopyVector(params.lightRight, clipmap->LightRight);
        CopyVector(params.lightUp, clipmap->LightUp);
        CopyVector(params.lightDirection, clipmap->Direction);
        params.cameraPosition[0] = cameraPosition[0];
        params.cameraPosition[1] = cameraPosition[1];
        params.cameraPosition[2] = cameraPosition[2];
        params.cameraPosition[3] = clipmap->Settings.MaxShadowDistance;
        const float depthRange = clipmap->Settings.DepthRangeMeters;
        params.depth[0] = static_cast<float>(clipmap->DepthCenter);
        params.depth[1] = 0.5f / depthRange;
        params.depth[2] = 2.0f * depthRange;
        params.depth[3] = clipmap->Settings.FadeRatio;
        params.pixel[0] = pixelMeters;
        params.control[0] = 1u;
        params.control[1] = clipmap->LevelCount;
        params.control[2] = poolPages;
        outParams = params;
        return true;
    }
} // namespace NorvesLib::Core::Rendering

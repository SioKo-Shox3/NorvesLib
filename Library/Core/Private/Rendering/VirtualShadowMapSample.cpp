#include "Rendering/VirtualShadowMapSample.h"

#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapPass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 絶対のページの番号を int32 でシェーダーへ渡せる範囲（印付けと同じ。範囲の端 + 128 ページが溢れない余裕を持つ）
        constexpr int64_t MaxOriginMagnitude = 1ll << 30;
        // CSM のカスケードの数（CascadedShadowLightMatrices.h の CSM_CASCADE_COUNT と同じ。分割の距離は 1 つ多い）
        constexpr uint32_t VirtualShadowMapCsmCascadeCount = 4u;

        void CopyVector(float* destination, const Math::Vector3& value)
        {
            destination[0] = value.x;
            destination[1] = value.y;
            destination[2] = value.z;
        }
    } // namespace

    void ResolveVirtualShadowMapViewRange(const VirtualShadowMapClipmapSettings& settings,
                                          const float* cascadeSplitDistances,
                                          float& outNear,
                                          float& outFar,
                                          float& outFade)
    {
        bool bSplitsUsable = cascadeSplitDistances != nullptr;
        for (uint32_t index = 0; bSplitsUsable && index <= VirtualShadowMapCsmCascadeCount; ++index)
        {
            bSplitsUsable = std::isfinite(cascadeSplitDistances[index]) && cascadeSplitDistances[index] >= 0.0f &&
                            (index == 0u || cascadeSplitDistances[index] > cascadeSplitDistances[index - 1u]);
        }
        if (bSplitsUsable)
        {
            const float farDistance = cascadeSplitDistances[VirtualShadowMapCsmCascadeCount];
            outNear = cascadeSplitDistances[0];
            outFar = farDistance;
            outFade = std::max((farDistance - cascadeSplitDistances[VirtualShadowMapCsmCascadeCount - 1u]) * 0.1f, 0.001f);
        }
        else
        {
            outNear = 0.0f;
            outFar = settings.MaxShadowDistance;
            outFade = std::max(settings.MaxShadowDistance * settings.FadeRatio, 0.001f);
        }
    }

    uint32_t BuildVirtualShadowMapSlices(const VirtualShadowMapClipmap* clipmap, const VirtualShadowMapClipmap* previous, GPUVsmSlice* outSlices)
    {
        for (uint32_t index = 0; index < VirtualShadowMapMaxSlices; ++index)
        {
            GPUVsmSlice& slice = outSlices[index];
            std::memset(&slice, 0, sizeof(slice));
            slice.origin[2] = static_cast<int32_t>(index * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL);
            slice.origin[3] = static_cast<int32_t>(VirtualShadowMap::TABLE_DIMENSION);
            slice.extra[2] = VirtualShadowMapSliceProjectionOrtho;
        }
        if (clipmap == nullptr || !clipmap->bEnabled)
        {
            return 0u;
        }
        uint32_t count = 0u;
        const uint32_t levelCount = std::min(clipmap->LevelCount, VirtualShadowMapMaxSlices);
        for (uint32_t level = 0; level < levelCount; ++level)
        {
            const VirtualShadowMapClipmapLevel& data = clipmap->Levels[level];
            if (data.OriginPageX <= -MaxOriginMagnitude || data.OriginPageX >= MaxOriginMagnitude ||
                data.OriginPageY <= -MaxOriginMagnitude || data.OriginPageY >= MaxOriginMagnitude || !(data.PageMeters > 0.0f))
            {
                continue;
            }
            GPUVsmSlice& slice = outSlices[level];
            // 太陽の段のライト空間の基底（右・上・光の進む向き）。w は原点のずれで、ライト空間の原点はワールドの原点と同じ
            CopyVector(slice.axisX, clipmap->LightRight);
            CopyVector(slice.axisY, clipmap->LightUp);
            CopyVector(slice.axisZ, clipmap->Direction);
            slice.info[0] = data.PageMeters;
            slice.info[1] = data.TexelMeters;
            slice.origin[0] = static_cast<int32_t>(data.OriginPageX);
            slice.origin[1] = static_cast<int32_t>(data.OriginPageY);
            const bool bPrevious = previous != nullptr && level < previous->LevelCount;
            slice.extra[0] = bPrevious ? static_cast<int32_t>(previous->Levels[level].OriginPageX) : slice.origin[0];
            slice.extra[1] = bPrevious ? static_cast<int32_t>(previous->Levels[level].OriginPageY) : slice.origin[1];
            ++count;
        }
        return count;
    }

    bool BuildVirtualShadowMapSampleParams(const VirtualShadowMapClipmap* clipmap,
                                           const float* cameraPosition,
                                           const float* cameraForward,
                                           const float* cascadeSplitDistances,
                                           float fovYDegrees,
                                           float screenHeightPixels,
                                           uint32_t poolPages,
                                           GPUVsmSampleParams& outParams)
    {
        std::memset(&outParams, 0, sizeof(outParams));
        if (clipmap == nullptr || cameraPosition == nullptr || cameraForward == nullptr || !clipmap->bEnabled || clipmap->LevelCount == 0u ||
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

        const float forwardLength = std::sqrt(cameraForward[0] * cameraForward[0] + cameraForward[1] * cameraForward[1] +
                                              cameraForward[2] * cameraForward[2]);
        if (!std::isfinite(forwardLength) || !(forwardLength > 1.0e-5f))
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
        for (uint32_t axis = 0; axis < 3u; ++axis)
        {
            params.view[axis] = cameraForward[axis] / forwardLength;
        }
        // 影の距離の範囲・薄めは CSM と同じ（CalculateShadow）。分割の距離が使えなければ設定の最大の距離と割合から決める
        ResolveVirtualShadowMapViewRange(clipmap->Settings, cascadeSplitDistances, params.range[0], params.range[1], params.range[2]);
        params.pixel[0] = pixelMeters * VirtualShadowMap::PCF_MIN_RADIUS_PIXELS;
        params.pixel[1] = VirtualShadowMap::SUN_TAN_ANGULAR_RADIUS;
        params.pixel[2] = VirtualShadowMap::MAX_FILTER_RADIUS_METERS;
        params.control[0] = 1u;
        params.control[1] = clipmap->LevelCount;
        params.control[2] = poolPages;
        outParams = params;
        return true;
    }
} // namespace NorvesLib::Core::Rendering

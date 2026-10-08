#include "Rendering/VirtualShadowMapPointLights.h"

#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapPass.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t MaxMipCount = 16u;

        bool IsPowerOfTwo(uint32_t value)
        {
            return value != 0u && (value & (value - 1u)) == 0u;
        }

        // 面の座標の軸: sc = 前方 × 上、tc = 上、軸 = 前方。PointShadowFaceMatrices のビューの基底と同じ
        // （ビュー空間の +X が sc、+Y が tc）。キューブの面の選び方（sc/|ma|, tc/|ma|）と一致する
        struct FaceAxes
        {
            Math::Vector3 Sc;
            Math::Vector3 Tc;
            Math::Vector3 Major;
        };

        FaceAxes MakeFaceAxes(uint32_t face)
        {
            const PointShadowDetail::PointShadowFaceBasis& basis = PointShadowDetail::GetFaceBasis(face);
            FaceAxes axes;
            axes.Major = basis.Forward;
            axes.Tc = basis.Up;
            axes.Sc = Math::Vector3(basis.Forward.y * basis.Up.z - basis.Forward.z * basis.Up.y,
                                    basis.Forward.z * basis.Up.x - basis.Forward.x * basis.Up.z,
                                    basis.Forward.x * basis.Up.y - basis.Forward.y * basis.Up.x);
            return axes;
        }

        void WriteAxisRow(float* row, const Math::Vector3& axis, const Math::Vector3& origin)
        {
            row[0] = axis.x;
            row[1] = axis.y;
            row[2] = axis.z;
            row[3] = -(axis.x * origin.x + axis.y * origin.y + axis.z * origin.z);
        }
    } // namespace

    bool IsValidVirtualShadowMapPointSettings(const VirtualShadowMapPointSettings& settings)
    {
        if (!IsPowerOfTwo(settings.FaceResolution) || !IsPowerOfTwo(settings.PageResolution) ||
            settings.MipCount == 0u || settings.MipCount > MaxMipCount || !std::isfinite(settings.BiasLevels))
        {
            return false;
        }
        // 最も粗い段でもページの一辺以上
        return (settings.FaceResolution >> (settings.MipCount - 1u)) >= settings.PageResolution;
    }

    uint32_t VirtualShadowMapPointMipResolution(const VirtualShadowMapPointSettings& settings, uint32_t mip)
    {
        return mip < 32u ? (settings.FaceResolution >> mip) : 0u;
    }

    uint32_t VirtualShadowMapPointPagesPerAxis(const VirtualShadowMapPointSettings& settings, uint32_t mip)
    {
        return settings.PageResolution != 0u ? VirtualShadowMapPointMipResolution(settings, mip) / settings.PageResolution : 0u;
    }

    VirtualShadowMapPointLights BuildVirtualShadowMapPointLights(const PointShadowSnapshot& snapshot,
                                                                 const VirtualShadowMapPointSettings& settings,
                                                                 uint32_t firstSlice)
    {
        VirtualShadowMapPointLights result;
        result.Settings = settings;
        result.FirstSlice = firstSlice;
        if (!IsValidVirtualShadowMapPointSettings(settings))
        {
            return result;
        }
        result.SlicesPerLight = PointShadowFaceCount * settings.MipCount;
        const uint32_t lightCount = std::min(snapshot.LightCount, PointShadowMaxLights);
        if (firstSlice + lightCount * result.SlicesPerLight > VirtualShadowMapMaxSlices)
        {
            return result;
        }
        result.LightCount = lightCount;
        for (uint32_t light = 0; light < lightCount; ++light)
        {
            result.LightId[light] = snapshot.Lights[light].LightId;
            result.Position[light] = snapshot.Lights[light].Position;
            result.Range[light] = snapshot.Lights[light].Range;
        }
        return result;
    }

    bool BuildVirtualShadowMapPointSampleParams(const VirtualShadowMapPointLights& lights,
                                                const float* cameraPosition,
                                                float fovYDegrees,
                                                float screenHeightPixels,
                                                uint32_t poolPages,
                                                GPUVsmPointSampleParams& outParams)
    {
        std::memset(&outParams, 0, sizeof(outParams));
        if (cameraPosition == nullptr || lights.LightCount == 0u || lights.LightCount > PointShadowMaxLights ||
            !IsValidVirtualShadowMapPointSettings(lights.Settings) || poolPages == 0u || poolPages > VirtualShadowMap::MAX_POOL_PAGES ||
            lights.FirstSlice + lights.SliceCount() > VirtualShadowMapMaxSlices)
        {
            return false;
        }
        const float pixelMeters = VirtualShadowMapScreenPixelMeters(1.0f, fovYDegrees, screenHeightPixels);
        if (!(pixelMeters > 0.0f) || !std::isfinite(pixelMeters) || !std::isfinite(cameraPosition[0]) || !std::isfinite(cameraPosition[1]) ||
            !std::isfinite(cameraPosition[2]))
        {
            return false;
        }
        GPUVsmPointSampleParams params;
        std::memset(&params, 0, sizeof(params));
        params.header[0] = lights.LightCount;
        params.header[1] = lights.FirstSlice;
        params.header[2] = lights.Settings.MipCount;
        params.header[3] = poolPages;
        params.tuning[0] = pixelMeters;
        params.tuning[1] = std::exp2(lights.Settings.BiasLevels);
        params.tuning[2] = VirtualShadowMap::PCF_MIN_RADIUS_PIXELS;
        params.cameraPosition[0] = cameraPosition[0];
        params.cameraPosition[1] = cameraPosition[1];
        params.cameraPosition[2] = cameraPosition[2];
        params.plane[0] = PointShadowNearPlane;
        params.plane[1] = static_cast<float>(lights.Settings.FaceResolution);
        for (uint32_t light = 0; light < lights.LightCount; ++light)
        {
            params.lights[light][0] = lights.Position[light].x;
            params.lights[light][1] = lights.Position[light].y;
            params.lights[light][2] = lights.Position[light].z;
            params.lights[light][3] = lights.Range[light];
        }
        outParams = params;
        return true;
    }

    uint32_t VirtualShadowMapPointSliceIndex(const VirtualShadowMapPointLights& lights, uint32_t light, uint32_t face, uint32_t mip)
    {
        return lights.FirstSlice + (light * PointShadowFaceCount + face) * lights.Settings.MipCount + mip;
    }

    bool VirtualShadowMapPointLightsDiffer(const VirtualShadowMapPointLights& lhs, const VirtualShadowMapPointLights& rhs)
    {
        if (lhs.LightCount != rhs.LightCount || lhs.FirstSlice != rhs.FirstSlice || lhs.SlicesPerLight != rhs.SlicesPerLight ||
            lhs.Settings.FaceResolution != rhs.Settings.FaceResolution || lhs.Settings.PageResolution != rhs.Settings.PageResolution ||
            lhs.Settings.MipCount != rhs.Settings.MipCount)
        {
            return true;
        }
        for (uint32_t light = 0; light < lhs.LightCount; ++light)
        {
            if (lhs.LightId[light] != rhs.LightId[light] || lhs.Range[light] != rhs.Range[light] ||
                lhs.Position[light].x != rhs.Position[light].x || lhs.Position[light].y != rhs.Position[light].y ||
                lhs.Position[light].z != rhs.Position[light].z)
            {
                return true;
            }
        }
        return false;
    }

    uint32_t BuildVirtualShadowMapPointSliceInvalidation(const VirtualShadowMapPointLights* previous,
                                                         const VirtualShadowMapPointLights* current,
                                                         bool* outInvalid,
                                                         VirtualShadowMapPointRemap* outRemap)
    {
        for (uint32_t index = 0; index < VirtualShadowMapMaxSlices; ++index)
        {
            outInvalid[index] = false;
        }
        VirtualShadowMapPointRemap remap;
        for (uint32_t light = 0; light < PointShadowMaxLights; ++light)
        {
            remap.Source[light] = light;
        }
        const auto markLight = [&](const VirtualShadowMapPointLights& lights, uint32_t light) {
            for (uint32_t offset = 0; offset < lights.SlicesPerLight; ++offset)
            {
                const uint32_t index = lights.FirstSlice + light * lights.SlicesPerLight + offset;
                if (index < VirtualShadowMapMaxSlices)
                {
                    outInvalid[index] = true;
                }
            }
        };
        const bool bSameLayout = previous != nullptr && current != nullptr && previous->FirstSlice == current->FirstSlice &&
                                 previous->SlicesPerLight == current->SlicesPerLight &&
                                 previous->Settings.FaceResolution == current->Settings.FaceResolution &&
                                 previous->Settings.PageResolution == current->Settings.PageResolution &&
                                 previous->Settings.MipCount == current->Settings.MipCount;
        if (!bSameLayout)
        {
            // 並べ方が違えば、同じ番号のスライスでも内容の意味が違う。前後どちらのスライスも引き継がない
            for (const VirtualShadowMapPointLights* lights : {previous, current})
            {
                for (uint32_t light = 0; lights != nullptr && light < lights->LightCount; ++light)
                {
                    markLight(*lights, light);
                }
            }
        }
        else
        {
            // 今フレームの灯 j に、同じ識別子の前フレームの灯を対応づける（前フレームの灯は 1 回だけ使う）
            const uint32_t previousCount = std::min(previous->LightCount, PointShadowMaxLights);
            const uint32_t currentCount = std::min(current->LightCount, PointShadowMaxLights);
            uint32_t source[PointShadowMaxLights];
            bool bClaimed[PointShadowMaxLights] = {};
            bool bMovedAway[PointShadowMaxLights] = {};
            for (uint32_t light = 0; light < currentCount; ++light)
            {
                source[light] = VirtualShadowMapPointNoSource;
                for (uint32_t candidate = 0; candidate < previousCount; ++candidate)
                {
                    if (!bClaimed[candidate] && previous->LightId[candidate] == current->LightId[light])
                    {
                        bClaimed[candidate] = true;
                        source[light] = candidate;
                        bMovedAway[candidate] = candidate != light;
                        break;
                    }
                }
            }
            const uint32_t blockCount = std::max(previousCount, currentCount);
            for (uint32_t light = 0; light < blockCount; ++light)
            {
                if (light < currentCount && source[light] != VirtualShadowMapPointNoSource)
                {
                    // 対応する灯がある: 内容を（番号が変わっていれば移して）引き継ぎ、位置か Range が変わっていれば全ページを無効にする
                    const uint32_t old = source[light];
                    remap.Source[light] = old;
                    if (previous->Range[old] != current->Range[light] || previous->Position[old].x != current->Position[light].x ||
                        previous->Position[old].y != current->Position[light].y || previous->Position[old].z != current->Position[light].z)
                    {
                        markLight(*current, light);
                    }
                }
                else if (bMovedAway[light])
                {
                    // 前フレームのこの番号の灯は別の番号へ移った。ページの二重所有を避けるため、この番号の領域を空にする
                    remap.Source[light] = VirtualShadowMapPointNoSource;
                }
                else
                {
                    // 対応する灯が無い（新しい灯に替わった・灯が無くなった）: 古い内容をそのまま dirty にして、要求の無いページは空きへ戻す
                    markLight(light < previousCount ? *previous : *current, light);
                }
            }
        }
        remap.bMoves = false;
        for (uint32_t light = 0; light < PointShadowMaxLights; ++light)
        {
            remap.bMoves = remap.bMoves || remap.Source[light] != light;
        }
        uint32_t count = 0u;
        for (uint32_t index = 0; index < VirtualShadowMapMaxSlices; ++index)
        {
            count += outInvalid[index] ? 1u : 0u;
        }
        if (outRemap != nullptr)
        {
            *outRemap = remap;
        }
        return count;
    }

    uint32_t BuildVirtualShadowMapPointSlices(const VirtualShadowMapPointLights& lights, GPUVsmSlice* outSlices)
    {
        uint32_t written = 0u;
        for (uint32_t light = 0; light < lights.LightCount; ++light)
        {
            for (uint32_t face = 0; face < PointShadowFaceCount; ++face)
            {
                const FaceAxes axes = MakeFaceAxes(face);
                for (uint32_t mip = 0; mip < lights.Settings.MipCount; ++mip)
                {
                    const uint32_t index = VirtualShadowMapPointSliceIndex(lights, light, face, mip);
                    GPUVsmSlice& slice = outSlices[index];
                    std::memset(&slice, 0, sizeof(slice));
                    WriteAxisRow(slice.axisX, axes.Sc, lights.Position[light]);
                    WriteAxisRow(slice.axisY, axes.Tc, lights.Position[light]);
                    WriteAxisRow(slice.axisZ, axes.Major, lights.Position[light]);
                    const uint32_t pagesPerAxis = VirtualShadowMapPointPagesPerAxis(lights.Settings, mip);
                    slice.info[0] = 2.0f / static_cast<float>(pagesPerAxis);
                    slice.info[1] = 2.0f / static_cast<float>(VirtualShadowMapPointMipResolution(lights.Settings, mip));
                    slice.info[2] = lights.Range[light];
                    slice.info[3] = PointShadowNearPlane;
                    slice.origin[2] = static_cast<int32_t>(index * VirtualShadowMap::TABLE_ENTRIES_PER_LEVEL);
                    slice.origin[3] = static_cast<int32_t>(pagesPerAxis);
                    slice.extra[2] = VirtualShadowMapSliceProjectionPerspective;
                    ++written;
                }
            }
        }
        return written;
    }

    uint32_t SelectVirtualShadowMapPointFace(float directionX, float directionY, float directionZ)
    {
        const float ax = std::fabs(directionX);
        const float ay = std::fabs(directionY);
        const float az = std::fabs(directionZ);
        if (ax >= ay && ax >= az)
        {
            return directionX >= 0.0f ? 0u : 1u;
        }
        if (ay >= az)
        {
            return directionY >= 0.0f ? 2u : 3u;
        }
        return directionZ >= 0.0f ? 4u : 5u;
    }

    float VirtualShadowMapPointTexelMeters(const VirtualShadowMapPointSettings& settings, uint32_t mip, float axialDistance)
    {
        const uint32_t resolution = VirtualShadowMapPointMipResolution(settings, mip);
        return resolution != 0u ? 2.0f * axialDistance / static_cast<float>(resolution) : 0.0f;
    }

    int32_t SelectVirtualShadowMapPointMip(const VirtualShadowMapPointSettings& settings,
                                           float axialDistance,
                                           float cameraDistance,
                                           float fovYDegrees,
                                           float screenHeightPixels)
    {
        if (!IsValidVirtualShadowMapPointSettings(settings) || !(axialDistance > 0.0f) || !std::isfinite(axialDistance) ||
            !(cameraDistance > 0.0f) || !std::isfinite(cameraDistance))
        {
            return -1;
        }
        const float pixelMeters = VirtualShadowMapScreenPixelMeters(cameraDistance, fovYDegrees, screenHeightPixels);
        if (!(pixelMeters > 0.0f) || !std::isfinite(pixelMeters))
        {
            return -1;
        }
        const float targetTexel = pixelMeters * std::exp2(settings.BiasLevels);
        // 粗い段から順に、texel が目標以下になる最初の段。どの段も超えるときは 0
        for (uint32_t mip = settings.MipCount; mip-- > 0u;)
        {
            if (VirtualShadowMapPointTexelMeters(settings, mip, axialDistance) <= targetTexel)
            {
                return static_cast<int32_t>(mip);
            }
        }
        return 0;
    }

    bool LocateVirtualShadowMapPointReceiver(const VirtualShadowMapPointLights& lights,
                                             uint32_t light,
                                             const Math::Vector3& receiverPosition,
                                             float cameraDistance,
                                             float fovYDegrees,
                                             float screenHeightPixels,
                                             VirtualShadowMapPointReceiver& outReceiver)
    {
        outReceiver = VirtualShadowMapPointReceiver{};
        if (light >= lights.LightCount || !(lights.Range[light] > 0.0f))
        {
            return false;
        }
        const Math::Vector3 offset(receiverPosition.x - lights.Position[light].x,
                                   receiverPosition.y - lights.Position[light].y,
                                   receiverPosition.z - lights.Position[light].z);
        const float distanceSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
        const float range = lights.Range[light];
        if (!std::isfinite(distanceSquared) || distanceSquared > range * range)
        {
            return false;
        }
        const uint32_t face = SelectVirtualShadowMapPointFace(offset.x, offset.y, offset.z);
        const FaceAxes axes = MakeFaceAxes(face);
        const float axial = offset.x * axes.Major.x + offset.y * axes.Major.y + offset.z * axes.Major.z;
        if (!(axial >= PointShadowNearPlane))
        {
            return false;
        }
        const int32_t mip = SelectVirtualShadowMapPointMip(lights.Settings, axial, cameraDistance, fovYDegrees, screenHeightPixels);
        if (mip < 0)
        {
            return false;
        }
        const float sc = offset.x * axes.Sc.x + offset.y * axes.Sc.y + offset.z * axes.Sc.z;
        const float tc = offset.x * axes.Tc.x + offset.y * axes.Tc.y + offset.z * axes.Tc.z;
        // 面の縁・角は浮動小数の丸めで 1 をわずかに超えることがあるので [-1,1] に収める
        const float ndcX = std::clamp(sc / axial, -1.0f, 1.0f);
        const float ndcY = std::clamp(tc / axial, -1.0f, 1.0f);
        const uint32_t pagesPerAxis = VirtualShadowMapPointPagesPerAxis(lights.Settings, static_cast<uint32_t>(mip));
        const auto pageOf = [pagesPerAxis](float ndc) {
            const float scaled = (ndc * 0.5f + 0.5f) * static_cast<float>(pagesPerAxis);
            return std::min(static_cast<uint32_t>(std::max(scaled, 0.0f)), pagesPerAxis - 1u);
        };
        outReceiver.Face = face;
        outReceiver.Mip = static_cast<uint32_t>(mip);
        outReceiver.Slice = VirtualShadowMapPointSliceIndex(lights, light, face, outReceiver.Mip);
        outReceiver.PageX = pageOf(ndcX);
        outReceiver.PageY = pageOf(ndcY);
        outReceiver.NdcX = ndcX;
        outReceiver.NdcY = ndcY;
        outReceiver.AxialDistance = axial;
        outReceiver.Depth = axial / range;
        return true;
    }
} // namespace NorvesLib::Core::Rendering

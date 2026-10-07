#include "Rendering/VirtualShadowMapClipmap.h"

#include "Rendering/DirectionalShadowLightSelection.h"
#include "Math/MathTypes.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 近平面の手前の余白（m）。CSM の MinimumDepth と同じ
        constexpr float NearDepthMeters = 0.01f;
        // 深度の原点を範囲のこの割合の刻みでスナップする
        constexpr double DepthSnapRatio = 0.25;

        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsFiniteMatrix(const Math::Matrix4x4& value)
        {
            for (uint32_t index = 0u; index < 16u; ++index)
            {
                if (!std::isfinite(value.values[index]))
                {
                    return false;
                }
            }
            return true;
        }

        Math::Vector3 SelectStableUpVector(const Math::Vector3& direction)
        {
            if (std::abs(Math::VectorUtils::Dot(direction, Math::Vector3::UnitY)) <= 0.98f)
            {
                return Math::Vector3::UnitY;
            }
            return Math::Vector3::UnitX;
        }

        // CSM（CascadedShadowLightMatrices.cpp の BuildLightBasis）と同じ規則のライト空間の基底
        bool BuildLightBasis(const Math::Vector3& direction, Math::Vector3& outRight, Math::Vector3& outUp)
        {
            const Math::Vector3 viewZ = direction * -1.0f;
            outRight = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(SelectStableUpVector(direction), viewZ));
            if (Math::VectorUtils::Length(outRight) <= Math::Constants::EPSILON)
            {
                return false;
            }
            outUp = Math::VectorUtils::Normalize(Math::VectorUtils::Cross(viewZ, outRight));
            return IsFiniteVector(outRight) && IsFiniteVector(outUp);
        }

        // 最も近い格子点（半分は切り上げ）
        int64_t RoundToGridIndex(double coordinate, double gridSize)
        {
            return static_cast<int64_t>(std::floor(coordinate / gridSize + 0.5));
        }
    } // namespace

    bool IsValidVirtualShadowMapClipmapSettings(const VirtualShadowMapClipmapSettings& settings)
    {
        return settings.LevelCount >= 1u && settings.LevelCount <= VirtualShadowMapMaxLevels &&
               std::isfinite(settings.FirstWidthMeters) && settings.FirstWidthMeters > 0.0f &&
               settings.PageResolution >= 1u && settings.VirtualResolution >= settings.PageResolution &&
               settings.VirtualResolution % settings.PageResolution == 0u &&
               std::isfinite(settings.DepthRangeMeters) && settings.DepthRangeMeters > 0.0f &&
               std::isfinite(settings.BiasLevels) &&
               std::isfinite(settings.MaxShadowDistance) && settings.MaxShadowDistance > 0.0f &&
               std::isfinite(settings.FadeRatio) && settings.FadeRatio >= 0.0f && settings.FadeRatio <= 1.0f;
    }

    float VirtualShadowMapLevelWidthMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level)
    {
        return settings.FirstWidthMeters * std::ldexp(1.0f, static_cast<int>(level));
    }

    float VirtualShadowMapLevelTexelMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level)
    {
        return VirtualShadowMapLevelWidthMeters(settings, level) / static_cast<float>(settings.VirtualResolution);
    }

    float VirtualShadowMapLevelCoverageMeters(const VirtualShadowMapClipmapSettings& settings, uint32_t level)
    {
        const float pagesPerAxis = static_cast<float>(settings.VirtualResolution / settings.PageResolution);
        const float usablePages = std::max(pagesPerAxis * 0.5f - 2.0f, 0.0f);
        return VirtualShadowMapLevelWidthMeters(settings, level) * usablePages / pagesPerAxis;
    }

    float VirtualShadowMapScreenPixelMeters(float distance, float fovYDegrees, float screenHeightPixels)
    {
        if (!std::isfinite(distance) || !std::isfinite(fovYDegrees) || !std::isfinite(screenHeightPixels) ||
            distance < 0.0f || fovYDegrees <= 0.0f || fovYDegrees >= 179.0f || screenHeightPixels <= 0.0f)
        {
            return 0.0f;
        }
        const float tangent = std::tan(fovYDegrees * (Math::Constants::PI / 180.0f) * 0.5f);
        return distance * 2.0f * tangent / screenHeightPixels;
    }

    int32_t SelectVirtualShadowMapLevel(const VirtualShadowMapClipmapSettings& settings,
                                        float distance,
                                        float fovYDegrees,
                                        float screenHeightPixels)
    {
        if (!IsValidVirtualShadowMapClipmapSettings(settings) || !std::isfinite(distance) ||
            distance < 0.0f || distance > settings.MaxShadowDistance)
        {
            return -1;
        }
        const float pixelMeters = VirtualShadowMapScreenPixelMeters(distance, fovYDegrees, screenHeightPixels);
        if (!(pixelMeters > 0.0f) && distance > 0.0f)
        {
            return -1;
        }
        const float targetTexel = pixelMeters * std::exp2(settings.BiasLevels);

        // texel は段ごとに 2 倍になるので、目標以下の最も粗い段。目標が段 0 の texel より小さくても段 0 より細かくは選ばない
        int32_t selected = 0;
        for (uint32_t level = 1u; level < settings.LevelCount; ++level)
        {
            if (VirtualShadowMapLevelTexelMeters(settings, level) <= targetTexel)
            {
                selected = static_cast<int32_t>(level);
            }
        }

        // texel を満たす段の範囲が受け手に届かないときは、届く段まで粗くする（最も粗い段が上限）
        while (static_cast<uint32_t>(selected) + 1u < settings.LevelCount &&
               distance > VirtualShadowMapLevelCoverageMeters(settings, static_cast<uint32_t>(selected)))
        {
            ++selected;
        }
        return selected;
    }

    float VirtualShadowMapShadowFadeWeight(const VirtualShadowMapClipmapSettings& settings, float distance)
    {
        const float fadeWidth = std::max(settings.MaxShadowDistance * settings.FadeRatio, 0.001f);
        const float t = std::clamp((distance - (settings.MaxShadowDistance - fadeWidth)) / fadeWidth, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    uint32_t VirtualShadowMapPageTorusAddress(int64_t absolutePage, uint32_t pagesPerAxis)
    {
        const int64_t count = static_cast<int64_t>(pagesPerAxis);
        const int64_t remainder = absolutePage % count;
        return static_cast<uint32_t>(remainder < 0 ? remainder + count : remainder);
    }

    void VirtualShadowMapWorldToLightSpace(const VirtualShadowMapClipmap& clipmap,
                                           const Math::Vector3& worldPosition,
                                           double& outX,
                                           double& outY,
                                           double& outDepth)
    {
        outX = static_cast<double>(worldPosition.x) * clipmap.LightRight.x +
               static_cast<double>(worldPosition.y) * clipmap.LightRight.y +
               static_cast<double>(worldPosition.z) * clipmap.LightRight.z;
        outY = static_cast<double>(worldPosition.x) * clipmap.LightUp.x +
               static_cast<double>(worldPosition.y) * clipmap.LightUp.y +
               static_cast<double>(worldPosition.z) * clipmap.LightUp.z;
        outDepth = static_cast<double>(worldPosition.x) * clipmap.Direction.x +
                   static_cast<double>(worldPosition.y) * clipmap.Direction.y +
                   static_cast<double>(worldPosition.z) * clipmap.Direction.z;
    }

    bool VirtualShadowMapLevelFindPage(const VirtualShadowMapClipmap& clipmap,
                                       uint32_t level,
                                       double lightX,
                                       double lightY,
                                       int64_t& outPageX,
                                       int64_t& outPageY)
    {
        if (!clipmap.bEnabled || level >= clipmap.LevelCount)
        {
            return false;
        }
        const VirtualShadowMapClipmapLevel& levelData = clipmap.Levels[level];
        const double pageMeters = static_cast<double>(levelData.PageMeters);
        const int64_t pageX = static_cast<int64_t>(std::floor(lightX / pageMeters));
        const int64_t pageY = static_cast<int64_t>(std::floor(lightY / pageMeters));
        const int64_t count = static_cast<int64_t>(clipmap.PagesPerAxis);
        if (pageX < levelData.OriginPageX || pageX >= levelData.OriginPageX + count ||
            pageY < levelData.OriginPageY || pageY >= levelData.OriginPageY + count)
        {
            return false;
        }
        outPageX = pageX;
        outPageY = pageY;
        return true;
    }

    VirtualShadowMapClipmap BuildVirtualShadowMapClipmap(const Math::Vector3& lightDirection,
                                                         uint64_t lightId,
                                                         const Math::Vector3& cameraPosition,
                                                         const VirtualShadowMapClipmapSettings& settings)
    {
        VirtualShadowMapClipmap result;
        if (!IsValidVirtualShadowMapClipmapSettings(settings) || !IsFiniteVector(lightDirection) ||
            !IsFiniteVector(cameraPosition) ||
            Math::VectorUtils::Length(lightDirection) <= Math::Constants::EPSILON)
        {
            return result;
        }

        const Math::Vector3 direction = Math::VectorUtils::Normalize(lightDirection);
        Math::Vector3 right;
        Math::Vector3 up;
        if (!IsFiniteVector(direction) || !BuildLightBasis(direction, right, up))
        {
            return result;
        }

        result.LightId = lightId;
        result.Direction = direction;
        result.LightRight = right;
        result.LightUp = up;
        result.Settings = settings;
        result.PagesPerAxis = settings.VirtualResolution / settings.PageResolution;
        result.LevelCount = settings.LevelCount;

        double cameraX = 0.0;
        double cameraY = 0.0;
        double cameraDepth = 0.0;
        VirtualShadowMapWorldToLightSpace(result, cameraPosition, cameraX, cameraY, cameraDepth);

        // 深度の原点は大きな刻み（範囲の 1/4）でスナップする。深度の原点が動くたびに全段のキャッシュが無効になるので、まれにする
        const double depthStep = static_cast<double>(settings.DepthRangeMeters) * DepthSnapRatio;
        result.DepthCenter = static_cast<double>(RoundToGridIndex(cameraDepth, depthStep)) * depthStep;

        const int64_t halfPages = static_cast<int64_t>(result.PagesPerAxis / 2u);
        for (uint32_t level = 0u; level < settings.LevelCount; ++level)
        {
            VirtualShadowMapClipmapLevel& data = result.Levels[level];
            data.Level = level;
            data.WidthMeters = VirtualShadowMapLevelWidthMeters(settings, level);
            data.TexelMeters = VirtualShadowMapLevelTexelMeters(settings, level);
            data.PageMeters = data.TexelMeters * static_cast<float>(settings.PageResolution);

            // 中心をページの格子へスナップする。texel の格子はワールドに固定され、カメラが 1 ページ動くごとに範囲だけが 1 ページずれる
            const double pageMeters = static_cast<double>(data.PageMeters);
            data.CenterPageX = RoundToGridIndex(cameraX, pageMeters);
            data.CenterPageY = RoundToGridIndex(cameraY, pageMeters);
            data.OriginPageX = data.CenterPageX - halfPages;
            data.OriginPageY = data.CenterPageY - halfPages;
            data.OriginLightX = static_cast<double>(data.OriginPageX) * pageMeters;
            data.OriginLightY = static_cast<double>(data.OriginPageY) * pageMeters;

            const double centerX = static_cast<double>(data.CenterPageX) * pageMeters;
            const double centerY = static_cast<double>(data.CenterPageY) * pageMeters;
            data.Center = Math::Vector3(
                static_cast<float>(right.x * centerX + up.x * centerY + direction.x * result.DepthCenter),
                static_cast<float>(right.y * centerX + up.y * centerY + direction.y * result.DepthCenter),
                static_cast<float>(right.z * centerX + up.z * centerY + direction.z * result.DepthCenter));

            const float range = settings.DepthRangeMeters;
            data.NearDepth = NearDepthMeters;
            data.FarDepth = range * 2.0f + NearDepthMeters;
            data.LightPosition = data.Center - direction * (range + NearDepthMeters);
            data.View = Math::MatrixUtils::CreateLookAt(data.LightPosition, data.Center, SelectStableUpVector(direction));
            data.Projection = Math::MatrixUtils::CreateOrthographic(data.WidthMeters, data.WidthMeters,
                                                                    data.NearDepth, data.FarDepth);
            if (!IsFiniteVector(data.Center) || !IsFiniteMatrix(data.View) || !IsFiniteMatrix(data.Projection))
            {
                return VirtualShadowMapClipmap{};
            }
        }

        result.bEnabled = true;
        return result;
    }

    VirtualShadowMapClipmap BuildVirtualShadowMapClipmap(const Container::VariableArray<LightProxy>* lightProxies,
                                                         const CameraProxy& camera,
                                                         const VirtualShadowMapClipmapSettings& settings)
    {
        const LightProxy* sun = SelectShadowedDirectionalLight(lightProxies);
        if (sun == nullptr)
        {
            return VirtualShadowMapClipmap{};
        }
        return BuildVirtualShadowMapClipmap(Math::Vector3(sun->DirectionX, sun->DirectionY, sun->DirectionZ),
                                            sun->LightId,
                                            Math::Vector3(camera.PositionX, camera.PositionY, camera.PositionZ),
                                            settings);
    }
} // namespace NorvesLib::Core::Rendering

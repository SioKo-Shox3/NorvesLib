// 視野と距離で候補を絞り、目から可視点までの地形と草を照会する。
#include "AI/Perception/VisionModel.h"
#include "Math/GeometryTypes.h"
#include "Rendering/Terrain/TerrainSystem.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace NorvesLib::Modules::AI::Perception
{
    namespace
    {
        bool IsFinite(const Math::Vector3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }

        float FadeWeight(double start, double end, double value)
        {
            if (value >= end)
            {
                return 0.0f;
            }
            if (value <= start)
            {
                return 1.0f;
            }
            // 残り幅から計算すると、端の近くでも 1 からの差し引きで精度を失わない。
            const double t = (end - value) / (end - start);
            return static_cast<float>(t * t * (3.0 - 2.0 * t));
        }

        float ScaleTransmittance(float transmittance, float scale)
        {
            if (!std::isfinite(transmittance))
            {
                return 0.0f;
            }
            const float clamped = std::clamp(transmittance, 0.0f, 1.0f);
            if (clamped == 0.0f || clamped == 1.0f)
            {
                return clamped;
            }
            return std::isfinite(scale) && scale >= 0.0f ? std::pow(clamped, scale) : clamped;
        }
    }

    VisionPointResult EvaluateVisionPoint(const Math::Vector3& eye, const Math::Vector3& forward,
                                          const Math::Vector3& target, const IGrassFieldSource* grass,
                                          const Core::Rendering::Terrain::TerrainSystem* terrain, const VisionTuning& tuning)
    {
        VisionPointResult result;
        if (!IsFinite(eye) || !IsFinite(target) || !IsFinite(forward) ||
            !std::isfinite(tuning.FieldOfViewDegrees) || !std::isfinite(tuning.FieldOfViewEdgeDegrees) ||
            !std::isfinite(tuning.FadeStartMeters) || !std::isfinite(tuning.MaxDistanceMeters) ||
            !std::isfinite(tuning.GrassStepMeters) || !std::isfinite(tuning.TerrainClearanceMeters))
        {
            return result;
        }

        const double dx = static_cast<double>(target.x) - eye.x;
        const double dy = static_cast<double>(target.y) - eye.y;
        const double dz = static_cast<double>(target.z) - eye.z;
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (distance == 0.0)
        {
            result.AngleWeight = 1.0f;
            result.DistanceWeight = 1.0f;
            result.Visibility = 1.0f;
            return result;
        }

        result.DistanceWeight = FadeWeight(tuning.FadeStartMeters, tuning.MaxDistanceMeters, distance);
        const double fx = forward.x;
        const double fy = forward.y;
        const double fz = forward.z;
        const double forwardLength = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (forwardLength == 0.0)
        {
            return result;
        }
        const double cosine = (fx * dx + fy * dy + fz * dz) / (forwardLength * distance);
        const double angle = std::acos(std::clamp(cosine, -1.0, 1.0)) * (180.0 / std::numbers::pi);
        const double halfAngle = std::max(0.0, 0.5 * tuning.FieldOfViewDegrees);
        const double edge = std::clamp(static_cast<double>(tuning.FieldOfViewEdgeDegrees), 0.0, halfAngle);
        result.AngleWeight = FadeWeight(halfAngle - edge, halfAngle, angle);
        if (result.AngleWeight == 0.0f || result.DistanceWeight == 0.0f)
        {
            return result;
        }

        if (terrain)
        {
            const Math::Vector3 direction(static_cast<float>(dx / distance), static_cast<float>(dy / distance),
                                          static_cast<float>(dz / distance));
            const double rayLength = std::clamp(distance - tuning.TerrainClearanceMeters, 0.0,
                                                static_cast<double>(std::numeric_limits<float>::max()));
            Core::Rendering::Terrain::GroundHit hit;
            result.bTerrainBlocked = terrain->RaycastGround(Math::Ray(eye, direction), static_cast<float>(rayLength), hit);
            if (result.bTerrainBlocked)
            {
                return result;
            }
        }

        if (grass)
        {
            const float transmittance = grass->QueryTransmittance(eye, target, tuning.GrassStepMeters);
            result.GrassTransmittance = ScaleTransmittance(transmittance, tuning.GrassOpacityScale);
        }
        result.Visibility = result.GrassTransmittance * result.AngleWeight * result.DistanceWeight;
        return result;
    }

    VisionResult EvaluateVision(const VisionQuery& query, const VisionTuning& tuning)
    {
        VisionResult result;
        for (size_t index = 0; index < query.TargetPoints.size(); ++index)
        {
            const VisionPointResult point = EvaluateVisionPoint(query.Eye, query.Forward, query.TargetPoints[index],
                                                                query.Grass, query.Terrain, tuning);
            if (point.Visibility > result.Visibility)
            {
                result.Visibility = point.Visibility;
                result.BestPointIndex = static_cast<int32_t>(index);
                result.Best = point;
            }
        }
        return result;
    }
}

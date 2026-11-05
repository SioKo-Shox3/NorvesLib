// ストリーミングするセルの優先度とヒステリシス付きの輪を純関数で判定する。
#include "Streaming/StreamingPriority.h"

#include <cmath>
#include <limits>

namespace NorvesLib::Core::Streaming
{
    namespace
    {
        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(Math::Vector3::Dot(value, Math::Vector3::UnitX))
                && std::isfinite(Math::Vector3::Dot(value, Math::Vector3::UnitY))
                && std::isfinite(Math::Vector3::Dot(value, Math::Vector3::UnitZ));
        }

        bool IsFiniteFocus(const StreamingFocus& focus)
        {
            return IsFiniteVector(focus.Position) && IsFiniteVector(focus.Velocity);
        }

        Math::Vector3 Horizontal(const Math::Vector3& value)
        {
            return Math::Vector3(
                Math::Vector3::Dot(value, Math::Vector3::UnitX),
                0.0f,
                Math::Vector3::Dot(value, Math::Vector3::UnitZ));
        }
    } // namespace

    bool ValidateRingSettings(const StreamingRingSettings& settings)
    {
        return std::isfinite(settings.ActiveRadius)
            && std::isfinite(settings.DormantRadius)
            && std::isfinite(settings.UnloadRadius)
            && std::isfinite(settings.DirectionWeight)
            && std::isfinite(settings.LatencySeconds)
            && settings.ActiveRadius >= 0.0f
            && settings.ActiveRadius < settings.DormantRadius
            && settings.DormantRadius < settings.UnloadRadius
            && settings.DirectionWeight >= 0.0f
            && settings.LatencySeconds >= 0.0f;
    }

    float ComputeHorizontalDistance(const StreamingBounds& bounds, const Math::Vector3& position)
    {
        if (!IsFiniteVector(position) || !IsFiniteVector(bounds.Min) || !IsFiniteVector(bounds.Max))
        {
            return std::numeric_limits<float>::infinity();
        }

        const float positionX = Math::Vector3::Dot(position, Math::Vector3::UnitX);
        const float positionZ = Math::Vector3::Dot(position, Math::Vector3::UnitZ);
        const float minX = Math::Vector3::Dot(bounds.Min, Math::Vector3::UnitX);
        const float minZ = Math::Vector3::Dot(bounds.Min, Math::Vector3::UnitZ);
        const float maxX = Math::Vector3::Dot(bounds.Max, Math::Vector3::UnitX);
        const float maxZ = Math::Vector3::Dot(bounds.Max, Math::Vector3::UnitZ);
        const float distanceX = positionX < minX ? minX - positionX : (positionX > maxX ? positionX - maxX : 0.0f);
        const float distanceZ = positionZ < minZ ? minZ - positionZ : (positionZ > maxZ ? positionZ - maxZ : 0.0f);
        return Math::Vector3(distanceX, 0.0f, distanceZ).Length();
    }

    float ComputeLookaheadDistance(float cellSize, const StreamingFocus& focus, const StreamingRingSettings& settings)
    {
        if (!IsFiniteFocus(focus))
        {
            return std::numeric_limits<float>::infinity();
        }

        const float speed = Horizontal(focus.Velocity).Length();
        const float travelDistance = speed * settings.LatencySeconds;
        return cellSize + travelDistance;
    }

    float ComputeLoadRadius(float cellSize, const StreamingFocus& focus, const StreamingRingSettings& settings)
    {
        if (!IsFiniteFocus(focus))
        {
            return std::numeric_limits<float>::infinity();
        }

        const float lookahead = ComputeLookaheadDistance(cellSize, focus, settings);
        const float radius = settings.ActiveRadius + lookahead;
        return radius < settings.DormantRadius ? radius : settings.DormantRadius;
    }

    float ComputePriority(const StreamingBounds& bounds, const StreamingFocus& focus, const StreamingRingSettings& settings)
    {
        const float distance = ComputeHorizontalDistance(bounds, focus.Position);
        if (!IsFiniteFocus(focus) || !std::isfinite(distance))
        {
            return std::numeric_limits<float>::max();
        }

        const Math::Vector3 velocity = Horizontal(focus.Velocity);
        const float speed = velocity.Length();
        if (speed < 1.0e-4f)
        {
            return distance;
        }

        // 水平化してから中心と差を求め、無視する高さの演算を持ち込まない。
        const Math::Vector3 center = (Horizontal(bounds.Min) * 0.5f) + (Horizontal(bounds.Max) * 0.5f);
        const Math::Vector3 offset = center - Horizontal(focus.Position);
        const Math::Vector3 direction = velocity / speed;
        const float projection = Math::Vector3::Dot(offset, direction);
        const float correction = settings.DirectionWeight * projection;
        const float priority = distance - correction;
        return std::isfinite(priority) ? priority : std::numeric_limits<float>::max();
    }

    EStreamingRingDecision DecideRing(EStreamingCellState state, float distance, float loadRadius, const StreamingRingSettings& settings)
    {
        // 速度の異常も読み込み半径を経由して受け取り、読み込みを止めて既存セルを解放する。
        if (!std::isfinite(distance) || !std::isfinite(loadRadius))
        {
            distance = std::numeric_limits<float>::infinity();
        }

        switch (state)
        {
        case EStreamingCellState::Unloaded:
            if (std::isfinite(distance) && distance <= loadRadius)
            {
                return EStreamingRingDecision::Load;
            }
            break;
        case EStreamingCellState::Failed:
        case EStreamingCellState::Preparing:
        case EStreamingCellState::ReadyToCommit:
        case EStreamingCellState::Committing:
            if (distance > settings.UnloadRadius)
            {
                return EStreamingRingDecision::Unload;
            }
            break;
        case EStreamingCellState::Active:
            if (distance > settings.UnloadRadius)
            {
                return EStreamingRingDecision::Unload;
            }
            if (distance > settings.DormantRadius)
            {
                return EStreamingRingDecision::MakeDormant;
            }
            break;
        case EStreamingCellState::Dormant:
            if (distance > settings.UnloadRadius)
            {
                return EStreamingRingDecision::Unload;
            }
            if (distance <= settings.ActiveRadius)
            {
                return EStreamingRingDecision::MakeActive;
            }
            break;
        case EStreamingCellState::Unloading:
            break;
        }
        return EStreamingRingDecision::None;
    }

    bool ShouldEnterDormant(float distance, const StreamingRingSettings& settings)
    {
        return !std::isfinite(distance) || distance > settings.ActiveRadius;
    }
} // namespace NorvesLib::Core::Streaming

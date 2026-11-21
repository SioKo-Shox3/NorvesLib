#include "AI/Navigation/NavObstacleSystem.h"

#include "AI/Navigation/NavObstacleComponent.h"
#include "AI/Navigation/NavObstacleSettings.h"
#include "AI/Navigation/NavigationService.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        double HorizontalDistance(const Math::Vector3& a, const Math::Vector3& b)
        {
            return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.z) - b.z);
        }

        bool MakeFootprint(const NavObstacleComponent& obstacle, const Math::Transform& transform,
                           float padding, NavDynamicFootprint& footprint, double& turningRadius)
        {
            footprint.Center = transform.position;
            footprint.MinY = transform.position.y + obstacle.getMinHeight();
            footprint.MaxY = transform.position.y + obstacle.getMaxHeight();
            if (!std::isfinite(footprint.Center.x) || !std::isfinite(footprint.Center.z) ||
                !std::isfinite(footprint.MinY) || !std::isfinite(footprint.MaxY) || footprint.MinY > footprint.MaxY)
            {
                return false;
            }

            const auto axis = transform.TransformDirection(Math::Vector3::UnitX);
            const double length = std::hypot(static_cast<double>(axis.x), static_cast<double>(axis.z));
            if (!std::isfinite(length))
            {
                return false;
            }
            if (length > 1.0e-6)
            {
                footprint.AxisX = static_cast<float>(axis.x / length);
                footprint.AxisZ = static_cast<float>(axis.z / length);
            }

            footprint.Shape = obstacle.getShape();
            switch (footprint.Shape)
            {
            case ENavObstacleShape::Circle:
            case ENavObstacleShape::Capsule:
            {
                const float radius = obstacle.getRadius();
                footprint.Radius = radius + padding;
                if (radius < 0.0f || !std::isfinite(footprint.Radius))
                {
                    return false;
                }
                if (footprint.Shape == ENavObstacleShape::Capsule)
                {
                    footprint.HalfLength = obstacle.getHalfLength();
                    turningRadius = static_cast<double>(footprint.HalfLength) + footprint.Radius;
                    return std::isfinite(footprint.HalfLength) && footprint.HalfLength >= 0.0f;
                }
                // 円は回転しても足跡が変わらない。
                turningRadius = 0.0;
                return true;
            }
            case ENavObstacleShape::Obb:
            {
                const Math::Vector2 half = obstacle.getHalfExtents();
                footprint.HalfExtentX = half.x + padding;
                footprint.HalfExtentZ = half.y + padding;
                turningRadius = std::hypot(static_cast<double>(footprint.HalfExtentX),
                                           static_cast<double>(footprint.HalfExtentZ));
                return half.x > 0.0f && half.y > 0.0f && std::isfinite(turningRadius);
            }
            default:
                return false;
            }
        }
    }

    void NavObstacleSystem::Register(NavObstacleComponent& obstacle)
    {
        if (!obstacle.m_bRegistered && obstacle.GetOwner())
        {
            m_Obstacles.push_back(&obstacle);
            obstacle.m_bRegistered = true;
            obstacle.m_bNeedsRestamp = true;
        }
    }

    void NavObstacleSystem::Detach(NavObstacleComponent& obstacle, NavDynamicOverlay& overlay)
    {
        overlay.Revoke(obstacle.m_Stamp);
        obstacle.m_Overlay = nullptr;
        obstacle.m_bRegistered = false;
        obstacle.m_bHasObservation = false;
        obstacle.m_bWaitingForInterval = false;
    }

    void NavObstacleSystem::Unregister(NavObstacleComponent& obstacle, NavigationService& nav)
    {
        const auto it = std::find(m_Obstacles.begin(), m_Obstacles.end(), &obstacle);
        if (it != m_Obstacles.end())
        {
            Detach(obstacle, nav.GetDynamicOverlay());
            m_Obstacles.erase(it);
        }
    }

    void NavObstacleSystem::Reset(NavigationService& nav)
    {
        for (auto* obstacle : m_Obstacles)
        {
            Detach(*obstacle, nav.GetDynamicOverlay());
        }
        m_Obstacles.clear();
        m_Time = 0.0;
        m_OverlayGeneration = 0;
    }

    void NavObstacleSystem::MarkAllForRestamp()
    {
        for (auto* obstacle : m_Obstacles)
        {
            obstacle->m_bNeedsRestamp = true;
        }
    }

    uint32_t NavObstacleSystem::GetObstacleCount() const
    {
        return static_cast<uint32_t>(std::count_if(m_Obstacles.begin(), m_Obstacles.end(),
            [](const NavObstacleComponent* obstacle) { return obstacle->IsNavStamped(); }));
    }

    size_t NavObstacleSystem::GetStampedCellCount() const
    {
        size_t total = 0;
        for (const auto* obstacle : m_Obstacles)
        {
            total += obstacle->GetStampedCellCount();
        }
        return total;
    }

    void NavObstacleSystem::Step(float dt, NavigationService& nav, const Core::World& world,
                                 const NavObstacleSettings& settings)
    {
        if (!std::isfinite(dt) || dt <= 0.0f)
        {
            return;
        }
        m_Time += dt;
        if (m_Obstacles.empty())
        {
            return;
        }
        const uint64_t generation = nav.GetDynamicOverlay().GetGeneration();
        if (generation != m_OverlayGeneration)
        {
            m_OverlayGeneration = generation;
            MarkAllForRestamp();
        }
        for (auto* obstacle : m_Obstacles)
        {
            Update(*obstacle, nav, world, settings);
        }
    }

    void NavObstacleSystem::Update(NavObstacleComponent& obstacle, NavigationService& nav, const Core::World& world,
                                   const NavObstacleSettings& settings)
    {
        auto& overlay = nav.GetDynamicOverlay();
        auto& stamp = obstacle.m_Stamp;
        const auto* owner = obstacle.GetOwner();
        if (!owner || owner->GetWorld() != &world)
        {
            overlay.Revoke(stamp);
            obstacle.m_bHasObservation = false;
            obstacle.m_bNeedsRestamp = true;
            obstacle.m_bWaitingForInterval = false;
            return;
        }

        const uint64_t version = owner->GetTransformVersion();
        const bool changed = !obstacle.m_bHasObservation || version != obstacle.m_ObservedVersion;
        const bool intervalElapsed = m_Time - obstacle.m_StampedTime >= settings.MinRestampIntervalSeconds;
        if (!changed && !obstacle.m_bNeedsRestamp && !(obstacle.m_bWaitingForInterval && intervalElapsed))
        {
            // 同じ位置にいた時刻も進め、静止後の最初の移動を正しい間隔で測る。
            obstacle.m_ObservedTime = m_Time;
            return;
        }

        const auto transform = owner->GetWorldTransform();
        const double elapsed = m_Time - obstacle.m_ObservedTime;
        const bool tooFast = changed && obstacle.m_bHasObservation &&
            HorizontalDistance(transform.position, obstacle.m_ObservedPosition) > settings.MaxStampSpeed * elapsed;
        obstacle.m_bHasObservation = true;
        obstacle.m_ObservedVersion = version;
        obstacle.m_ObservedPosition = transform.position;
        obstacle.m_ObservedTime = m_Time;
        obstacle.m_bWaitingForInterval = false;
        if (tooFast)
        {
            overlay.Revoke(stamp);
            // 次の歩で停止して変換の版が進まなくても、再開を判定する。
            obstacle.m_bNeedsRestamp = true;
            return;
        }

        const bool force = obstacle.m_bNeedsRestamp || !stamp.bApplied;
        obstacle.m_bNeedsRestamp = false;
        const ENavObstacleMode mode = obstacle.Mode.Get();
        NavDynamicFootprint footprint;
        double turningRadius = 0.0;
        if ((mode != ENavObstacleMode::Block && mode != ENavObstacleMode::Cost) ||
            !MakeFootprint(obstacle, transform, settings.AgentPadding, footprint, turningRadius))
        {
            overlay.Revoke(stamp);
            return;
        }

        const Math::Vector2 axis(footprint.AxisX, footprint.AxisZ);
        if (!force)
        {
            const auto& previous = obstacle.m_StampedAxis;
            const double angle = std::atan2(std::abs(Math::Vector2::Cross(previous, axis)),
                                            Math::Vector2::Dot(previous, axis));
            const double movement = (std::max)(HorizontalDistance(footprint.Center, obstacle.m_StampedPosition),
                                               angle * turningRadius);
            if (movement < static_cast<double>(settings.MinRestampDistanceCells) * nav.GetGrid().GetDesc().CellSize)
            {
                return;
            }
            if (!intervalElapsed)
            {
                obstacle.m_bWaitingForInterval = true;
                return;
            }
        }

        overlay.Revoke(stamp);
        RasterizeNavDynamicFootprint(nav.GetGrid(), footprint, settings.HeightBand, stamp.Cells);
        if (!stamp.Cells.empty())
        {
            stamp.Mode = mode;
            stamp.CostExtraPer128 = NavCostMultiplierToExtraPer128(obstacle.CostMultiplier.Get());
            overlay.Apply(stamp);
            if (stamp.bApplied)
            {
                obstacle.m_Overlay = &overlay;
                obstacle.m_StampedTime = m_Time;
                obstacle.m_StampedPosition = footprint.Center;
                obstacle.m_StampedAxis = axis;
            }
        }
    }
}

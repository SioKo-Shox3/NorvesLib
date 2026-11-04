#include "Gameplay/World/EncounterZoneMath.h"

#include <algorithm>
#include <cmath>

namespace NorvesLib::Modules::Gameplay::World
{
    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        double InflatedSize(float size, float margin)
        {
            return std::max(0.0, double(size) + double(margin));
        }
    } // namespace

    bool IsInsideInflated(const EncounterZoneShapeDesc& shape, const Math::Vector3& point, float margin)
    {
        if (!IsFinite(shape.Center) || !IsFinite(shape.HalfExtents) || !std::isfinite(shape.Radius) ||
            !std::isfinite(shape.HalfHeight) || !IsFinite(point) || !std::isfinite(margin))
        {
            return false;
        }

        // float の差や二乗があふれないよう、距離と寸法を double で計算する。
        const double x = double(point.x) - double(shape.Center.x);
        const double y = double(point.y) - double(shape.Center.y);
        const double z = double(point.z) - double(shape.Center.z);
        switch (shape.Shape)
        {
        case EEncounterZoneShape::Sphere:
        {
            const double radius = InflatedSize(shape.Radius, margin);
            return x * x + y * y + z * z <= radius * radius;
        }
        case EEncounterZoneShape::Box:
            return std::abs(x) <= InflatedSize(shape.HalfExtents.x, margin) &&
                   std::abs(y) <= InflatedSize(shape.HalfExtents.y, margin) &&
                   std::abs(z) <= InflatedSize(shape.HalfExtents.z, margin);
        case EEncounterZoneShape::CylinderY:
        {
            const double radius = InflatedSize(shape.Radius, margin);
            return x * x + z * z <= radius * radius &&
                   std::abs(y) <= InflatedSize(shape.HalfHeight, margin);
        }
        }
        return false;
    }

    float EncounterZoneTuning::GetEffectiveExitMargin() const
    {
        return ExitMargin < EnterMargin ? EnterMargin : ExitMargin;
    }

    EncounterZoneEvents EncounterZoneStateMachine::Step(bool bAnySensorInsideEnter, bool bAllSensorsOutsideExit,
                                                        bool bTeleported, float worldDeltaSeconds,
                                                        const EncounterZoneTuning& tuning)
    {
        EncounterZoneEvents events;
        if (m_State == EEncounterZoneState::Cleared)
        {
            return events;
        }

        if (bTeleported && bAllSensorsOutsideExit &&
            (m_State == EEncounterZoneState::Engaged || m_State == EEncounterZoneState::Resetting))
        {
            m_State = EEncounterZoneState::Outside;
            m_ResetElapsedSeconds = 0.0;
            events.bExited = true;
            events.bReset = true;
            return events;
        }

        switch (m_State)
        {
        case EEncounterZoneState::Outside:
            if (bAnySensorInsideEnter)
            {
                m_State = EEncounterZoneState::Engaged;
                events.bEntered = true;
            }
            break;
        case EEncounterZoneState::Engaged:
            if (bAllSensorsOutsideExit)
            {
                m_State = EEncounterZoneState::Resetting;
                m_ResetElapsedSeconds = 0.0;
            }
            break;
        case EEncounterZoneState::Resetting:
            if (bAnySensorInsideEnter)
            {
                m_State = EEncounterZoneState::Engaged;
                m_ResetElapsedSeconds = 0.0;
            }
            else if (bAllSensorsOutsideExit)
            {
                if (std::isfinite(worldDeltaSeconds) && worldDeltaSeconds > 0.0f)
                {
                    m_ResetElapsedSeconds += double(worldDeltaSeconds);
                }
                if (m_ResetElapsedSeconds >= double(tuning.ResetDelaySeconds))
                {
                    m_State = EEncounterZoneState::Outside;
                    m_ResetElapsedSeconds = 0.0;
                    events.bExited = true;
                    events.bReset = true;
                }
            }
            else
            {
                m_ResetElapsedSeconds = 0.0;
            }
            break;
        case EEncounterZoneState::Cleared:
            break;
        }
        return events;
    }

    EncounterZoneEvents EncounterZoneStateMachine::MarkCleared()
    {
        EncounterZoneEvents events;
        if (m_State != EEncounterZoneState::Cleared)
        {
            m_State = EEncounterZoneState::Cleared;
            m_ResetElapsedSeconds = 0.0;
            events.bCleared = true;
        }
        return events;
    }
} // namespace NorvesLib::Modules::Gameplay::World

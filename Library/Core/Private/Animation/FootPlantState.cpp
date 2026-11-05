// 脚の固定と解除、および着地時の目標と重みを計算する。
#include "Animation/FootPlantState.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Animation
{
    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool CanStore(double value)
        {
            return std::isfinite(value) && std::abs(value) <= double(std::numeric_limits<float>::max());
        }

        double CalculateSwingHeight(const FootPlantInput& input, const FootPlantSettings& plant, double lift,
                                    double releaseDelta, bool bHasReleaseDelta)
        {
            const double a = std::clamp(lift / double(plant.LiftHeight), 0.0, 1.0);
            const double current = input.bHasGround ? double(input.GroundPoint.y) - double(input.OriginY)
                                                    : releaseDelta;
            const double release = bHasReleaseDelta ? releaseDelta : current;
            double height = double(input.AnimToeWorld.y) + current * (1.0 - a) + release * a;
            if (input.bHasGround)
            {
                height = std::max(height, double(input.GroundPoint.y) + double(input.Sole));
            }
            return height;
        }

        bool IsValid(const FootPlantInput& input, const FootPlantSettings& plant, const FootLandSettings& land)
        {
            return IsFinite(input.AnimToeWorld) && IsFinite(input.GroundPoint) && std::isfinite(input.OriginY) &&
                   std::isfinite(input.Sole) && input.Sole >= 0.0f && std::isfinite(input.DeltaSeconds) &&
                   input.DeltaSeconds >= 0.0f && std::isfinite(plant.HeightThreshold) &&
                   std::isfinite(plant.SpeedThreshold) &&
                   plant.SpeedThreshold >= 0.0f && std::isfinite(plant.LiftHeight) && plant.LiftHeight > 0.0f &&
                   std::isfinite(plant.ReleaseBlendSeconds) && plant.ReleaseBlendSeconds >= 0.0f &&
                   std::isfinite(land.BlendSeconds) && land.BlendSeconds >= 0.0f &&
                   std::isfinite(land.SnapDistance) && land.SnapDistance >= 0.0f &&
                   std::isfinite(land.TeleportDistance) && land.TeleportDistance >= 0.0f;
        }
    } // namespace

    bool FootPlantState::Update(const FootPlantInput& input, const FootPlantSettings& plant,
                                const FootLandSettings& land, FootPlantOutput& out)
    {
        if (!IsValid(input, plant, land))
        {
            return false;
        }

        // 結果を作れない場合も、それまでの履歴を残す。
        FootPlantState next = *this;
        const double lift = double(input.AnimToeWorld.y) - double(input.OriginY) - double(input.Sole);
        if (!next.m_bHasLandState)
        {
            next.m_LandWeight = input.bGrounded ? 1.0f : 0.0f;
            next.m_bHasLandState = true;
        }

        if (!input.bGrounded)
        {
            next.m_LandWeight = 0.0f;
            next.m_bLandingStarted = false;
            next.m_Phase = EFootPlantPhase::Swing;
            next.m_Anchor = Math::Vector3();
            next.m_bHasAnchor = false;
            next.m_ReleaseOffsetX = 0.0;
            next.m_ReleaseOffsetY = 0.0;
            next.m_ReleaseOffsetZ = 0.0;
            next.m_ReleaseDelta = 0.0;
            next.m_ReleaseElapsed = 0.0f;
            next.m_bHasReleaseDelta = false;
            next.m_bReleaseBlending = false;
        }
        else if (next.m_LandWeight < 1.0f)
        {
            const double groundHeight = double(input.GroundPoint.y) + double(input.Sole);
            if (!next.m_bLandingStarted && input.bHasGround &&
                std::abs(double(input.AnimToeWorld.y) - groundHeight) <= double(land.SnapDistance))
            {
                next.m_bLandingStarted = true;
            }
            if (next.m_bLandingStarted)
            {
                next.m_LandWeight = land.BlendSeconds == 0.0f
                                        ? 1.0
                                        : std::min(1.0, next.m_LandWeight +
                                                            double(input.DeltaSeconds) / double(land.BlendSeconds));
            }
        }

        next.m_bEventDriven = next.m_bEventDriven || input.bDownEvent || input.bUpEvent;
        bool bReleased = false;
        if (input.bGrounded)
        {
            EFootPlantPhase targetPhase = next.m_Phase;
            if (next.m_bEventDriven)
            {
                if (input.bUpEvent)
                {
                    targetPhase = EFootPlantPhase::Swing;
                }
                else if (input.bDownEvent && input.bHasGround)
                {
                    targetPhase = EFootPlantPhase::Planted;
                }
            }
            else if (next.m_Phase == EFootPlantPhase::Planted)
            {
                if (lift > double(plant.HeightThreshold))
                {
                    targetPhase = EFootPlantPhase::Swing;
                }
            }
            else if (input.bHasGround && next.m_bHasPreviousToe && input.DeltaSeconds > 0.0f &&
                     lift <= double(plant.HeightThreshold))
            {
                const double dx = double(input.AnimToeWorld.x) - double(next.m_PreviousToe.x);
                const double dz = double(input.AnimToeWorld.z) - double(next.m_PreviousToe.z);
                const double speed = std::hypot(dx, dz) / double(input.DeltaSeconds);
                if (speed <= double(plant.SpeedThreshold))
                {
                    targetPhase = EFootPlantPhase::Planted;
                }
            }

            if (next.m_Phase == EFootPlantPhase::Swing && targetPhase == EFootPlantPhase::Planted)
            {
                next.m_Phase = targetPhase;
                next.m_Anchor = input.GroundPoint;
                next.m_bHasAnchor = true;
                next.m_ReleaseElapsed = 0.0f;
                next.m_bReleaseBlending = false;
            }
            else if (next.m_Phase == EFootPlantPhase::Planted && targetPhase == EFootPlantPhase::Swing)
            {
                if (!next.BeginRelease(input, plant, lift))
                {
                    return false;
                }
                bReleased = true;
            }
        }

        if (next.m_Phase == EFootPlantPhase::Swing && next.m_bReleaseBlending)
        {
            if (plant.ReleaseBlendSeconds == 0.0f)
            {
                next.m_bReleaseBlending = false;
            }
            else if (!bReleased)
            {
                next.m_ReleaseElapsed = std::min(double(plant.ReleaseBlendSeconds),
                                                 next.m_ReleaseElapsed + double(input.DeltaSeconds));
                if (next.m_ReleaseElapsed >= plant.ReleaseBlendSeconds)
                {
                    next.m_bReleaseBlending = false;
                }
            }
        }

        FootPlantOutput result;
        if (!next.MakeOutput(input, plant, lift, result))
        {
            return false;
        }
        result.bReleasedThisFrame = bReleased;
        next.m_PreviousToe = input.AnimToeWorld;
        next.m_bHasPreviousToe = true;
        *this = next;
        out = result;
        return true;
    }

    bool FootPlantState::Release(const FootPlantInput& input, const FootPlantSettings& plant,
                                 const FootLandSettings& land, FootPlantOutput& out)
    {
        if (!IsValid(input, plant, land))
        {
            return false;
        }

        FootPlantState next = *this;
        const double lift = double(input.AnimToeWorld.y) - double(input.OriginY) - double(input.Sole);
        const bool bReleased = next.m_Phase == EFootPlantPhase::Planted;
        if (bReleased && !next.BeginRelease(input, plant, lift))
        {
            return false;
        }

        FootPlantOutput result;
        if (!next.MakeOutput(input, plant, lift, result))
        {
            return false;
        }
        result.bReleasedThisFrame = bReleased;
        *this = next;
        out = result;
        return true;
    }

    bool FootPlantState::BeginRelease(const FootPlantInput& input, const FootPlantSettings& plant, double lift)
    {
        const double height = double(m_Anchor.y) + double(input.Sole) + std::max(lift, 0.0);
        if (!m_bHasAnchor || !CanStore(height))
        {
            return false;
        }
        m_ReleaseDelta = double(m_Anchor.y) - double(input.OriginY);
        m_bHasReleaseDelta = true;
        m_ReleaseOffsetX = double(m_Anchor.x) - double(input.AnimToeWorld.x);
        m_ReleaseOffsetY = height - CalculateSwingHeight(input, plant, lift, m_ReleaseDelta, m_bHasReleaseDelta);
        m_ReleaseOffsetZ = double(m_Anchor.z) - double(input.AnimToeWorld.z);
        m_ReleaseElapsed = 0.0f;
        m_bReleaseBlending = plant.ReleaseBlendSeconds > 0.0f;
        m_Phase = EFootPlantPhase::Swing;
        return true;
    }

    bool FootPlantState::MakeOutput(const FootPlantInput& input, const FootPlantSettings& plant, double lift,
                                    FootPlantOutput& out) const
    {
        FootPlantOutput result;
        result.ToeTargetWorld = input.AnimToeWorld;
        result.Phase = m_Phase;
        result.LandWeight = float(m_LandWeight);
        if (m_Phase == EFootPlantPhase::Planted)
        {
            const double height = double(m_Anchor.y) + double(input.Sole) + std::max(lift, 0.0);
            if (!m_bHasAnchor || !CanStore(height))
            {
                return false;
            }
            result.ToeTargetWorld = Math::Vector3(m_Anchor.x, float(height), m_Anchor.z);
            result.bHasTarget = true;
        }
        else if (input.bHasGround || m_bHasAnchor)
        {
            double x = double(input.AnimToeWorld.x);
            double y = CalculateSwingHeight(input, plant, lift, m_ReleaseDelta, m_bHasReleaseDelta);
            double z = double(input.AnimToeWorld.z);
            if (m_bReleaseBlending && plant.ReleaseBlendSeconds > 0.0f)
            {
                const double t = std::clamp(m_ReleaseElapsed / double(plant.ReleaseBlendSeconds), 0.0, 1.0);
                x += m_ReleaseOffsetX * (1.0 - t);
                y += m_ReleaseOffsetY * (1.0 - t);
                z += m_ReleaseOffsetZ * (1.0 - t);
            }
            if (input.bHasGround)
            {
                y = std::max(y, double(input.GroundPoint.y) + double(input.Sole));
            }
            if (!CanStore(x) || !CanStore(y) || !CanStore(z))
            {
                return false;
            }
            result.ToeTargetWorld = Math::Vector3(float(x), float(y), float(z));
            result.bHasTarget = true;
        }
        out = result;
        return true;
    }

    void FootPlantState::Reset()
    {
        *this = FootPlantState{};
    }

    EFootPlantPhase FootPlantState::GetPhase() const
    {
        return m_Phase;
    }

    float FootPlantState::GetLandWeight() const
    {
        return float(m_LandWeight);
    }

    bool FootPlantState::IsEventDriven() const
    {
        return m_bEventDriven;
    }

    const Math::Vector3& FootPlantState::GetAnchor() const
    {
        return m_Anchor;
    }
} // namespace NorvesLib::Core::Animation

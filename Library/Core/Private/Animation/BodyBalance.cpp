// 脚の接地高から体の上下補正とピッチを求める。
#include "Animation/BodyBalance.h"

#include "Math/CriticalDamping.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Animation
{
    void BodyBalance::Reset()
    {
        m_PelvisOffset = 0.0f;
        m_PelvisVelocity = 0.0f;
        m_ChestOffset = 0.0f;
        m_ChestVelocity = 0.0f;
        m_Output = {};
    }

    bool BodyBalance::Update(Container::Span<const BodyBalanceLegSample> legs, const BodyBalanceSettings& settings,
                             float horizontalDistance, float dt, BodyBalanceOutput& out)
    {
        if (!std::isfinite(settings.MaxDrop) || !std::isfinite(settings.MaxRaise) ||
            !std::isfinite(settings.SmoothTime) || !std::isfinite(horizontalDistance) || !std::isfinite(dt) ||
            settings.MaxDrop < 0.0f || settings.MaxRaise < 0.0f || settings.SmoothTime < 0.0f || dt < 0.0f)
        {
            return false;
        }

        float pelvisTarget = 0.0f;
        float chestTarget = 0.0f;
        bool bHasFront = false;
        bool bHasValidRear = false;
        bool bHasValidFront = false;
        for (size_t i = 0; i < legs.size(); ++i)
        {
            const BodyBalanceLegSample& leg = legs[i];
            if (!std::isfinite(leg.HeightDelta))
            {
                return false;
            }
            bHasFront = bHasFront || leg.bFront;
            if (!leg.bValid)
            {
                continue;
            }
            if (leg.bFront)
            {
                if (!bHasValidFront || leg.HeightDelta < chestTarget)
                {
                    chestTarget = leg.HeightDelta;
                    bHasValidFront = true;
                }
            }
            else if (!bHasValidRear || leg.HeightDelta < pelvisTarget)
            {
                pelvisTarget = leg.HeightDelta;
                bHasValidRear = true;
            }
        }

        if (!bHasFront)
        {
            chestTarget = pelvisTarget;
        }
        pelvisTarget = std::clamp(pelvisTarget, -settings.MaxDrop, settings.MaxRaise);
        chestTarget = std::clamp(chestTarget, -settings.MaxDrop, settings.MaxRaise);

        // どちらかの平滑化が失敗しても元の状態を保つ。
        float pelvisOffset = m_PelvisOffset;
        float pelvisVelocity = m_PelvisVelocity;
        float chestOffset = m_ChestOffset;
        float chestVelocity = m_ChestVelocity;
        if (!Math::TryCriticalDamp(pelvisOffset, pelvisVelocity, pelvisTarget, settings.SmoothTime, dt) ||
            !Math::TryCriticalDamp(chestOffset, chestVelocity, chestTarget, settings.SmoothTime, dt))
        {
            return false;
        }

        BodyBalanceOutput nextOutput;
        nextOutput.PelvisOffset = pelvisOffset;
        nextOutput.ChestOffset = chestOffset;
        if (horizontalDistance >= 1e-4f)
        {
            // float の差があふれる場合も角度を求められるようにする。
            nextOutput.PitchRadians = static_cast<float>(std::atan2(static_cast<double>(chestOffset) - pelvisOffset,
                                                                   static_cast<double>(horizontalDistance)));
        }

        m_PelvisOffset = pelvisOffset;
        m_PelvisVelocity = pelvisVelocity;
        m_ChestOffset = chestOffset;
        m_ChestVelocity = chestVelocity;
        m_Output = nextOutput;
        out = nextOutput;
        return true;
    }

    const BodyBalanceOutput& BodyBalance::GetOutput() const
    {
        return m_Output;
    }
} // namespace NorvesLib::Core::Animation

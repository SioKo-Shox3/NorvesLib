#pragma once

#include "Text/IdentityPool.h"

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    enum class ERehitPolicy : uint8_t
    {
        OncePerVictim,
        OncePerPart,
        Interval
    };

    enum class EPerVictimPolicy : uint8_t
    {
        FirstPart,
        HighestPriority,
        AllParts
    };

    /// 攻撃ごとに共有する設定。Hitbox へ渡した後は変更しない。
    struct AttackProfile
    {
        Core::Identity ProfileId;
        Core::Identity WindowEventName;
        float MaxDurationSeconds = 1.0f;
        ERehitPolicy RehitPolicy = ERehitPolicy::OncePerVictim;
        float RehitIntervalSeconds = 0.0f;
        EPerVictimPolicy PerVictimPolicy = EPerVictimPolicy::FirstPart;
        uint32_t HitMask = ~uint32_t{0};
        float Damage = 0.0f;
        float Stagger = 0.0f;
        float HitStopSecondsHint = 0.0f;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat

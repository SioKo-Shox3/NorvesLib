#pragma once

#include <cstdint>

namespace NorvesLib::Modules::Gameplay::Combat
{
    struct DamageInfo
    {
        /// 部位の倍率を掛け終えたダメージ量。体力側では倍率を掛けない。
        float Amount;
        float StaggerAmount = 0.0f;
        uint64_t SourceUserData = 0;
    };

    enum class EDamageOutcome : uint8_t
    {
        Applied,
        Defeated,
        IgnoredInvulnerable,
        IgnoredAlreadyDefeated,
        RejectedInvalid
    };

    struct DamageResult
    {
        EDamageOutcome Outcome;
        float AppliedAmount;
        float HealthBefore;
        float HealthAfter;
        bool bStaggerBroken;
    };
} // namespace NorvesLib::Modules::Gameplay::Combat

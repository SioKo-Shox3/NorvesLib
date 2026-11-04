#pragma once

#include "Math/Vector3.h"

#include <cstdint>
#include <type_traits>

namespace NorvesLib::Modules::Gameplay::Combat
{
    /// ポインタを保持しない接触通知。登録番号はモジュール内の登録順を表す。
    struct CombatHitEvent
    {
        uint64_t AttackInstanceId = 0;
        // Identity の文字列ビューを持ち込まず、AttackProfile::ProfileId.GetHash() を格納する。
        uint64_t ProfileId = 0;
        uint64_t AttackerActorId = 0;
        uint64_t VictimActorId = 0;
        uint64_t HitboxIndex = 0;
        uint64_t HurtboxIndex = 0;
        uint32_t PartId = 0;
        Math::Vector3 Point;
        Math::Vector3 Normal;
        float Depth = 0.0f;
        Math::Vector3 RelativeVelocity;
        float S = 0.0f;
        uint64_t FrameNumber = 0;
    };

    static_assert(std::is_trivially_copyable_v<CombatHitEvent>);
} // namespace NorvesLib::Modules::Gameplay::Combat

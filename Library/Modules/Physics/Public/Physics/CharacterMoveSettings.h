#pragma once
#include <cstdint>
namespace NorvesLib::Modules::Physics
{
    // Yを上とするworld-spaceカプセル。体格・重力・速度の決定は呼出側が所有する。
    struct CharacterMoveSettings
    {
        float SkinWidth = .002f;
        float StepHeight = .25f;
        float GroundSnapDistance = .08f;
        float MaximumSlopeDegrees = 50;
        float MaximumDepenetrationDistance = 1;
        uint32_t SlideIterations = 6;
        uint32_t DepenetrationIterations = 8;
    };
} // namespace NorvesLib::Modules::Physics

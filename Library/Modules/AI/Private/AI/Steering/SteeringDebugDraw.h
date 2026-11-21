// カメラ付近のエージェントの操舵を描画する。
#pragma once

#include "Debug/DebugConfig.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core
{
    class World;
}

namespace NorvesLib::Core::Rendering
{
    class DebugDrawQueue;
}

namespace NorvesLib::Modules::AI
{
    class SteeringSystem;

    struct SteeringDebugDrawSettings
    {
        float Radius = 60.0f;
        float HeightOffset = 0.5f;
        uint32_t CircleSegments = 32;
    };

    /// カメラの XZ 半径内にいる個体の速度・目標・距離帯・経路を描く。
    void SubmitSteeringDebug(SteeringSystem& system, const Core::World& world, const Math::Vector3& cameraCenter,
                             const SteeringDebugDrawSettings& settings, Core::Rendering::DebugDrawQueue& queue);
}
#endif

#pragma once

#include <cstdint>

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace Game::GameModes
{
    struct Rendering3DTestData;

    bool InitializeM9WorldSkeletal(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                   Rendering3DTestData& data);
    void SetM9WorldSkeletalAnimationTime(Rendering3DTestData& data, float seconds);
    bool BuildM9WorldSkeletalPoseFingerprint(Rendering3DTestData& data, uint64_t& outFingerprint);
} // namespace Game::GameModes

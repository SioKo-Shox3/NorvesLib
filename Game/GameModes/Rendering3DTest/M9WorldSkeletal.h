#pragma once

#include <cstdint>
#include "Core/Public/Container/PointerTypes.h"

namespace NorvesLib::Core::GameMode
{
    struct GameModeContext;
}

namespace NorvesLib::Core
{
    class World;
    class Entity;
    class SkeletalAssetResource;
    class AnimationClipResource;
    namespace Component
    {
        class SkinnedMeshComponent;
    }
    namespace GameMode
    {
        class GameModeScope;
    }
} // namespace NorvesLib::Core

namespace Game::GameModes
{
    struct Rendering3DTestData;

    // 実M9とCPU試験で同じWorld/Scope/明示clipの一回attachを使う。
    bool AttachM9SkeletalAsset(
        NorvesLib::Core::World& world, NorvesLib::Core::GameMode::GameModeScope& scope,
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource>& asset,
        const NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource>& clip,
        NorvesLib::Core::Entity*& outObject, NorvesLib::Core::Component::SkinnedMeshComponent*& outComponent);
    bool InitializeM9WorldSkeletal(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                   Rendering3DTestData& data);
    void SetM9WorldSkeletalAnimationTime(Rendering3DTestData& data, float seconds);
    bool BuildM9WorldSkeletalPoseFingerprint(Rendering3DTestData& data, uint64_t& outFingerprint);
} // namespace Game::GameModes

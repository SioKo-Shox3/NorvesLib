#pragma once

#include "CombatSliceTypes.h"
#include "Container/PointerTypes.h"
#include "GameMode/GameModeContext.h"
#include "Rendering/RenderTypes.h"

namespace NorvesLib::Core
{
    class SkeletonResource;
    class SkinnedMeshResource;
    class AnimationClipResource;
    class SkeletalAssetResource;
}

namespace Game::GameModes
{
    struct CombatSliceActors
    {
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletonResource> DogSkeleton;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkinnedMeshResource> DogMesh;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> DogClip;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> DogAsset;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletonResource> ShishaSkeleton;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkinnedMeshResource> ShishaMesh;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::AnimationClipResource> ShishaClip;
        NorvesLib::Core::Container::TSharedPtr<NorvesLib::Core::SkeletalAssetResource> ShishaAsset;
        NorvesLib::Core::Rendering::MaterialHandle DogMaterial;
        NorvesLib::Core::Rendering::MaterialHandle SwordMaterial;
        NorvesLib::Core::Rendering::MaterialHandle ShishaMaterial;
    };

    // 失敗時も生成済みの Entity と資源を保持する。
    bool BuildCombatSliceActors(NorvesLib::Core::GameMode::GameModeContext& ctx,
                                CombatSliceSceneIds& ids, CombatSliceActors& actors);

    // 成功・失敗のどちらでも ScopeRef.Cleanup() の後に呼ぶ。
    void ReleaseCombatSliceActors(NorvesLib::Core::GameMode::GameModeContext& ctx, CombatSliceActors& actors);
} // namespace Game::GameModes

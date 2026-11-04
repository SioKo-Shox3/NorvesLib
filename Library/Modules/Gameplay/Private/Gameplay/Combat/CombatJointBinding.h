// 戦闘判定の関節名を遅延解決し、スケールを除いた姿勢を読み取る。
#pragma once

#include "Container/PointerTypes.h"
#include "Gameplay/Combat/CombatJoint.h"
#include "Math/Transform.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core
{
    class Entity;
    class SkeletalAssetResource;
    class SkeletonResource;
}

namespace NorvesLib::Modules::Gameplay::Combat
{
    struct CombatRuntimeDiagnostics;

    struct CombatJointBindingState
    {
        Core::Identity JointName;
        int32_t JointIndex = -1;
        Core::Container::TWeakPtr<Core::SkeletalAssetResource> Asset;
        Core::Container::TWeakPtr<Core::SkeletonResource> Skeleton;
    };

    // 姿勢の自動評価はしない。失敗時は出力を変更せず false を返す。
    bool ResolveJointPose(Core::Entity& binding, Core::Identity jointName,
                          Core::Container::TSharedPtr<CombatJointBindingState>& state,
                          const CombatJointInfluenceThreshold& threshold,
                          CombatRuntimeDiagnostics& diagnostics, Math::Transform& out);
} // namespace NorvesLib::Modules::Gameplay::Combat

// 関節の解決結果を資産の弱参照とともに保持し、影響の弱い関節を診断する。
#include "CombatJointBinding.h"

#include "Component/SkinnedMeshComponent.h"
#include "Gameplay/Combat/CombatRuntime.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"

#include <cmath>

namespace NorvesLib::Modules::Gameplay::Combat
{
    namespace
    {
        void CheckJointInfluence(const Core::SkeletalAssetResource& asset,
                                 const CombatJointBindingState& state,
                                 const CombatJointInfluenceThreshold& threshold,
                                 CombatRuntimeDiagnostics& diagnostics)
        {
            if ((threshold.MinVertexCount == 0 && threshold.MinWeightSum == 0.0f) || !asset.GetMesh())
            {
                return;
            }
            const auto& vertices = asset.GetMesh()->GetVertices();
            if (vertices.empty())
            {
                return;
            }
            uint32_t vertexCount = 0;
            float weightSum = 0.0f;
            // 頂点の番号と骨格の番号は、姿勢のボーンパレットと同じ添字空間を使う。
            for (const auto& vertex : vertices)
            {
                bool bInfluenced = false;
                for (size_t influence = 0; influence < vertex.JointIndices.size(); ++influence)
                {
                    const float weight = vertex.JointWeights[influence];
                    if (vertex.JointIndices[influence] == static_cast<uint32_t>(state.JointIndex) &&
                        std::isfinite(weight) && weight > 0.0f)
                    {
                        bInfluenced = true;
                        weightSum += weight;
                    }
                }
                if (bInfluenced)
                {
                    ++vertexCount;
                }
            }
            if (vertexCount <= threshold.MinVertexCount || weightSum <= threshold.MinWeightSum)
            {
                ++diagnostics.WeakJointWarningCount;
                NORVES_LOG_WARNING("Gameplay", "影響の弱い関節へ束縛しています: %s (頂点数=%u, 重みの総和=%g)",
                                   state.JointName.ToString().c_str(), vertexCount, weightSum);
            }
        }
    }

    bool ResolveJointPose(Core::Entity& binding, Core::Identity jointName,
                          Core::Container::TSharedPtr<CombatJointBindingState>& state,
                          const CombatJointInfluenceThreshold& threshold,
                          CombatRuntimeDiagnostics& diagnostics, Math::Transform& out)
    {
        auto* skinned = binding.GetComponent<Core::Component::SkinnedMeshComponent>();
        if (skinned == nullptr)
        {
            return false;
        }
        const auto& asset = skinned->GetSkeletalAsset();
        if (!asset || !asset->GetSkeleton())
        {
            return false;
        }
        if (!state)
        {
            state = Core::Container::MakeShared<CombatJointBindingState>();
        }
        if (state->JointName != jointName || state->Asset.lock() != asset ||
            state->Skeleton.lock() != asset->GetSkeleton() || state->JointIndex < 0)
        {
            state->JointName = jointName;
            state->Asset = asset;
            state->Skeleton = asset->GetSkeleton();
            state->JointIndex = skinned->FindJointIndex(jointName);
            if (state->JointIndex >= 0)
            {
                CheckJointInfluence(*asset, *state, threshold, diagnostics);
            }
        }
        Math::Matrix4x4 matrix;
        if (state->JointIndex < 0 ||
            !skinned->TryGetJointWorldMatrix(static_cast<uint32_t>(state->JointIndex), matrix))
        {
            return false;
        }
        const auto scale = Math::MatrixUtils::ExtractScale(matrix);
        if (!std::isfinite(scale.x) || !std::isfinite(scale.y) || !std::isfinite(scale.z) ||
            scale.x <= Math::Constants::EPSILON || scale.y <= Math::Constants::EPSILON ||
            scale.z <= Math::Constants::EPSILON)
        {
            return false;
        }
        const auto rotation = Math::MatrixUtils::ExtractRotationRowVector(matrix, scale);
        // CombatPose は四元数で点を回すため、行ベクトルの基底を転置してから変換する。
        const auto quaternion = Math::QuaternionUtils::Conjugate(
            Math::QuaternionUtils::FromRotationMatrix(rotation));
        const float length = Math::QuaternionUtils::Length(quaternion);
        if (!std::isfinite(length) || length <= Math::Constants::EPSILON)
        {
            return false;
        }
        out = Math::Transform(matrix.GetTranslationRow(), Math::QuaternionUtils::Normalize(quaternion));
        return true;
    }
} // namespace NorvesLib::Modules::Gameplay::Combat

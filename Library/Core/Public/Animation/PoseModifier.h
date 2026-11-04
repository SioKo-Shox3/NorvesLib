#pragma once
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletalPoseRuntime.h"
namespace NorvesLib::Core
{
    class Entity;
    class SkeletonResource;
} // namespace NorvesLib::Core
namespace NorvesLib::Core::Animation
{
    class PoseModifierContext
    {
      public:
        PoseModifierContext(const SkeletonPoseRuntime& skeleton, float dt, const AnimParamSet& params,
                            const AnimGraphInstance& instance, const LocalPose& pose, PoseScratch& scratch,
                            Container::VariableArray<Math::Matrix4x4>& model)
            : Skeleton(skeleton), DeltaSeconds(dt), Parameters(params), m_Instance(instance), m_Pose(pose),
              m_Scratch(scratch), m_Model(model)
        {
        }
        const SkeletonPoseRuntime& Skeleton;
        float DeltaSeconds;
        const AnimParamSet& Parameters;
        Container::Span<const float> SyncPhases;
        // メッシュのモデル空間（GetJointModelMatrices の空間）からワールドへの変換。
        // 所有者 Entity のワールド変換で、SkinnedMeshComponent::TryGetJointWorldMatrix と同じ。
        // 行ベクトル規約で model * MeshToWorld として使う。
        Math::Matrix4x4 MeshToWorld = Math::Matrix4x4::Identity;
        // メッシュノードのグローバル変換。GetJointModelMatrices はこの逆を掛けた後の空間。
        // AngleDistribution の空間（逆を掛ける前）とモデル空間の行き来に使う。
        Math::Matrix4x4 MeshNodeTransform = Math::Matrix4x4::Identity;
        // この modifier に掛ける重み（0〜1）。既定 1。設定ファイルの重みを包みが詰める。
        float Weight = 1.0f;
        const AnimDriveSignals& GetDriveSignals() const
        {
            return m_DriveSignals;
        }
        bool HasDriveSignals() const
        {
            return m_bHasDriveSignals;
        }
        void SetDriveSignals(const AnimDriveSignals* signals)
        {
            m_DriveSignals = signals ? *signals : AnimDriveSignals{};
            m_bHasDriveSignals = signals != nullptr;
        }
        // 所有者のポインタは Apply の間だけ有効。
        Entity* GetOwner() const
        {
            return m_Owner;
        }
        void SetOwner(Entity* owner)
        {
            m_Owner = owner;
        }
        int32_t FindJointIndex(Identity name) const;
        const SkeletonResource* GetSkeletonAsset() const
        {
            return m_SkeletonAsset;
        }
        void SetSkeletonAsset(const SkeletonResource* skeleton)
        {
            m_SkeletonAsset = skeleton;
        }
        // 同一modifier中は一度だけFK。LocalPoseを書き換えた後に再取得するときはInvalidateを呼ぶ。
        const Container::VariableArray<Math::Matrix4x4>* GetJointModelMatrices()
        {
            if (!m_bBuilt)
            {
                if (!m_Instance.BuildJointModelMatrices(m_Pose, m_Scratch, m_Model))
                {
                    return nullptr;
                }
                m_bBuilt = true;
            }
            return &m_Model;
        }
        void Invalidate()
        {
            m_bBuilt = false;
        }

      private:
        const AnimGraphInstance& m_Instance;
        const LocalPose& m_Pose;
        PoseScratch& m_Scratch;
        Container::VariableArray<Math::Matrix4x4>& m_Model;
        bool m_bBuilt = false;
        AnimDriveSignals m_DriveSignals;
        bool m_bHasDriveSignals = false;
        Entity* m_Owner = nullptr;
        const SkeletonResource* m_SkeletonAsset = nullptr;
    };
    class IPoseModifier
    {
      public:
        virtual ~IPoseModifier() = default;
        virtual int32_t Order() const = 0;
        virtual bool Apply(LocalPose&, PoseModifierContext&) = 0;
    };
} // namespace NorvesLib::Core::Animation

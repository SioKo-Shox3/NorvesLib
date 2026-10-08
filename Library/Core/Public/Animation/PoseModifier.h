#pragma once
#include "Animation/AnimGraphInstance.h"
#include "Animation/SkeletalPoseRuntime.h"
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
    };
    class IPoseModifier
    {
      public:
        virtual ~IPoseModifier() = default;
        virtual int32_t Order() const = 0;
        virtual bool Apply(LocalPose&, PoseModifierContext&) = 0;
    };
} // namespace NorvesLib::Core::Animation

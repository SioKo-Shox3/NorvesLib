// 優先度つきの視線目標へ首から頭のチェーンを向ける。
#pragma once

#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/Procedural/PoseModifierConfig.h"

namespace NorvesLib::Core::Animation::Procedural
{
    struct LookAtSettings
    {
        Container::VariableArray<JointRotationShare> Chain;
        int32_t Head = -1;
        Math::Vector3 ForwardAxis = Math::Vector3::UnitZ;
        Math::Vector3 UpAxis = Math::Vector3::UnitY;
        Math::Vector3 LookAxis = Math::Vector3::UnitZ;
        float MaxYawDeg = 70.0f;
        float MaxPitchDeg = 40.0f;
        float HalfLife = 0.12f;
        float MaxAngularAccelDeg = 2000.0f;
        Identity HoldSlot;
        float HoldWeightScale = 0.5f;
        float Strength = 1.0f;
    };

    // 右 = 上×前。正のヨーは右向き、正のピッチは上向き。
    // 状態の角度は rad、角速度は rad/s。Apply には毎フレームの未加工姿勢を渡す。
    class LookAtModifier final : public IPoseModifier
    {
      public:
        explicit LookAtModifier(LookAtSettings settings);
        int32_t Order() const override;
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;
        float GetYaw() const
        {
            return m_Yaw;
        }
        float GetPitch() const
        {
            return m_Pitch;
        }
        float GetYawVelocity() const
        {
            return m_YawVelocity;
        }
        float GetPitchVelocity() const
        {
            return m_PitchVelocity;
        }

      private:
        LookAtSettings m_Settings;
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        float m_YawVelocity = 0.0f;
        float m_PitchVelocity = 0.0f;
    };

    // 組み込みの型の表の作り手（type "Look"）。
    [[nodiscard]] bool CreateLookAtModifier(const PoseModifierCreateArgs& args,
                                            Container::TSharedPtr<IPoseModifier>& out,
                                            PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural

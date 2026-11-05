// 胴のバンクとピッチを背骨チェーンに分配する。
#pragma once

#include "Animation/Procedural/AngleDistribution.h"
#include "Animation/Procedural/PoseModifierConfig.h"

namespace NorvesLib::Core::Animation::Procedural
{
    struct BodyLeanSettings
    {
        bool bUseSignals = false;
        Identity BankParam = Identity("BankAngle");
        Identity PitchParam = Identity("PitchAngle");
        Container::VariableArray<JointRotationShare> Chain;
        Math::Vector3 ForwardAxis = Math::Vector3::UnitZ;
        Math::Vector3 RightAxis = Math::Vector3::UnitX;
        float MaxBankDeg = 25.0f;
        float MaxPitchDeg = 10.0f;
        float PitchPerAccel = 0.02f;
        float Gravity = 9.81f;
        float SmoothingHalfLife = 0.1f;
        float AirFadeHalfLife = 0.1f;
        float Strength = 1.0f;
    };

    class BodyLeanModifier final : public IPoseModifier
    {
      public:
        explicit BodyLeanModifier(BodyLeanSettings settings);
        int32_t Order() const override;
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;

      private:
        BodyLeanSettings m_Settings;
        float m_Bank = 0.0f;
        float m_BankVelocity = 0.0f;
        float m_Pitch = 0.0f;
        float m_PitchVelocity = 0.0f;
        float m_GroundWeight = 1.0f;
        float m_GroundVelocity = 0.0f;
        bool m_bGroundInitialized = false;
    };

    // 組み込みの型の表の作り手（type "Lean"）。
    [[nodiscard]] bool CreateBodyLeanModifier(const PoseModifierCreateArgs& args,
                                              Container::TSharedPtr<IPoseModifier>& out,
                                              PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural

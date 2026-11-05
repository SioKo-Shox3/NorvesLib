// 歩様の位相と速度に応じて背骨の関節を揺らす。
#pragma once

#include "Animation/Procedural/PoseModifierConfig.h"
#include "Math/Curves.h"

namespace NorvesLib::Core::Animation::Procedural
{
    struct SpineWaveJoint
    {
        int32_t Joint = -1;
        float PhaseOffset = 0.0f;
        float Amplitude = 0.0f;
        Container::VariableArray<Math::CurveKeyframe> AmplitudeCurve;
    };

    struct SpineWaveSettings
    {
        Container::VariableArray<SpineWaveJoint> Joints;
        Math::Vector3 UpAxis = Math::Vector3::UnitY;
        Math::Vector3 RightAxis = Math::Vector3::UnitX;
        Identity SyncGroup;
        float Frequency = 1.0f;
        float FallbackHz = 1.0f;
        float Strength = 1.0f;
        bool bPitch = false;
    };

    class SpineWaveModifier final : public IPoseModifier
    {
      public:
        explicit SpineWaveModifier(SpineWaveSettings settings);
        int32_t Order() const override;
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;

      private:
        SpineWaveSettings m_Settings;
        double m_Phase = 0.0;
    };

    // 組み込みの型の表の作り手（type "Spine"）。
    [[nodiscard]] bool CreateSpineWaveModifier(const PoseModifierCreateArgs& args,
                                               Container::TSharedPtr<IPoseModifier>& out,
                                               PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural

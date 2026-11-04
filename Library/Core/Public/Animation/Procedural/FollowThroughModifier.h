#pragma once

#include "Animation/Procedural/PoseModifierConfig.h"
#include "Animation/Procedural/Spring.h"

namespace NorvesLib::Core::Animation::Procedural
{
    struct FollowThroughSettings
    {
        Container::VariableArray<int32_t> Joints; // 遅らせる関節(根側→先端側。例: 胸・首)。
        int32_t HeadJoint = -1; // 指定時は、遅らせた後にこの関節のモデル空間の回転をアニメの値へ戻す。
        float FrequencyHz = 2.0f; // 角度のばねの周波数(仮)。
        float DampingRatio = 0.7f; // 減衰比 (0, 1](仮)。
        float MassScale = 1.0f; // 質量の倍率。実効の周波数は FrequencyHz / sqrt(質量の倍率)。
        Identity HeldSlot; // 名前で指す保持スロット。空なら見ない。
        bool bHasHeldSlot = false;
        float HeldMassScale = 1.0f; // 保持スロットが埋まっている間、質量の倍率に掛ける。
        float Strength = 0.25f; // 既定の重み(仮)。文脈の Weight と掛ける。
    };

    class FollowThroughModifier final : public IPoseModifier
    {
      public:
        explicit FollowThroughModifier(FollowThroughSettings settings);
        int32_t Order() const override; // PoseModifierDefaultOrder::FollowThrough。
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;
        void RequestReset();
        float GetLastDroppedSeconds() const;
        float GetLastMassScale() const; // 直前の Apply で使った質量の倍率(保持の反映後)。
        const FollowThroughSettings& GetSettings() const;

      private:
        FollowThroughSettings m_Settings;
        Container::VariableArray<AngularSpring> m_Springs;
        Container::VariableArray<Math::Quaternion> m_AnimationRotations;
        Container::VariableArray<Math::Quaternion> m_OriginalRotations;
        bool m_bNeedsReset = true;
        float m_LastDroppedSeconds = 0.0f;
        float m_LastMassScale = 1.0f;
    };

    [[nodiscard]] bool CreateFollowThroughModifier(const PoseModifierCreateArgs& args,
                                                   Container::TSharedPtr<IPoseModifier>& out,
                                                   PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural

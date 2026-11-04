#pragma once

#include "Animation/Procedural/PoseModifierConfig.h"

namespace NorvesLib::Core::Animation::Procedural
{
    struct SpringBoneChainSettings
    {
        Identity Name;
        Container::VariableArray<int32_t> Joints; // 根側→先端側の関節の添字。Joints[0] は固定の根(アニメに従う)。
        float Stiffness = 0; // アニメ姿勢の位置へ引く強さ [1/s^2]。
        float Damping = 0; // 速度に比例する減衰 [1/s]。
        Math::Vector3 Gravity; // ワールド空間の重力加速度 [m/s^2]。
        float Drag = 0; // 空気の抗力(速度に比例) [1/s]。
        float Radius = 0; // 当たりの半径 [m]。v1 では使わない(球コライダーは非ゴール)。
        float RotationLimitDeg = 180; // 関節ごとに重ねる回転の上限 [度]。
    };

    struct SpringBoneSettings
    {
        Container::VariableArray<SpringBoneChainSettings> Chains;
        float TeleportDistance = 1.0f; // 1 フレームの所有者の移動がこれを超えたら状態をアニメ姿勢へ戻す [m]。
    };

    class SpringBoneModifier final : public IPoseModifier
    {
      public:
        explicit SpringBoneModifier(SpringBoneSettings settings);
        int32_t Order() const override; // PoseModifierDefaultOrder::SpringBone。
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;
        void RequestReset(); // 次の Apply で状態をアニメ姿勢へ戻す(評価の再開時に呼ぶ)。
        void SetWindAcceleration(const Math::Vector3& acceleration); // 風の加速度の入力口。v1 は 0 のまま。
        float GetLastDroppedSeconds() const; // 直前の Apply で捨てた秒数。
        const SpringBoneSettings& GetSettings() const;
        // 直前の Apply 後の粒子のワールド位置(chain 番目のチェーンの joint 番目)。範囲外・未初期化は false。
        bool TryGetParticleWorldPosition(size_t chain, size_t joint, Math::Vector3& out) const;

      private:
        SpringBoneSettings m_Settings;
    };

    // 組み込みの型の表の作り手(type "SpringBone")。
    [[nodiscard]] bool CreateSpringBoneModifier(const PoseModifierCreateArgs& args,
                                                Container::TSharedPtr<IPoseModifier>& out,
                                                PoseModifierConfigReport& report);
} // namespace NorvesLib::Core::Animation::Procedural

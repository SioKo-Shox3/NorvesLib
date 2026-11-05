// 足の接地設定と姿勢後処理の公開インターフェースを定義する。
#pragma once

#include "Animation/BodyBalance.h"
#include "Animation/GroundQuery.h"
#include "Animation/PoseModifier.h"

namespace NorvesLib::Core
{
    class JsonValue;
}

namespace NorvesLib::Core::Animation
{
    namespace Procedural
    {
        struct PoseModifierConfigReport;
        struct PoseModifierCreateArgs;
    }

    struct FootIKLegSettings
    {
        Identity Name;
        int32_t Joints[4] = {-1, -1, -1, -1}; // 根、中間、足首、省略可能な足先
        uint32_t JointCount = 0;
        bool bAutoPole = true;
        Math::Vector3 Pole;         // 明示されたモデル空間の単位方向
        Math::Vector3 PoleInParent; // 休止姿勢の根の親の回転に対する単位方向
        float Sole = 0.02f;
        float MaxStretch = 1.02f;
        bool bFront = false;
    };

    struct FootIKBodySettings
    {
        bool bEnabled = false;
        int32_t Pelvis = -1;
        int32_t Chest = -1;
        BodyBalanceSettings Balance;
    };

    struct FootIKGroundSettings
    {
        float RayUp = 0.5f;
        float RayDown = 1.0f;
        float MaxSlopeDeg = 50.0f;
        Scene::PhysicsCollisionMask LayerMask = Scene::AllPhysicsLayers;
    };

    struct FootIKSettings
    {
        Container::VariableArray<FootIKLegSettings> Legs;
        FootIKBodySettings Body;
        FootIKGroundSettings Ground;
    };

    /** @brief 足の接地補正に必要な設定と地面問い合わせを保持する。 */
    class FootIKModifier final : public IPoseModifier
    {
    public:
        explicit FootIKModifier(FootIKSettings settings);
        int32_t Order() const override;
        bool Apply(LocalPose& pose, PoseModifierContext& context) override;
        // 借用する問い合わせを差し替える。所有権は移らない。
        void SetGroundQuery(const IGroundQuery* query);
        const IGroundQuery* GetGroundQuery() const;
        const FootIKSettings& GetSettings() const;
        void Reset();

    private:
        friend bool CreateFootIKModifier(const Procedural::PoseModifierCreateArgs&,
                                         Container::TSharedPtr<IPoseModifier>&,
                                         Procedural::PoseModifierConfigReport&);
        FootIKSettings m_Settings;
        const IGroundQuery* m_GroundQuery = nullptr;
        // 骨盤を含まず、胸を含む順に保持する。
        Container::VariableArray<int32_t> m_PitchJoints;
    };

    /** @brief 設定を検証し、全項目が正しいときだけ出力を更新する。 */
    [[nodiscard]] bool TryParseFootIKSettings(JsonValue settings, const SkeletonResource& skeleton,
                                              FootIKSettings& out, Procedural::PoseModifierConfigReport& report);
    /** @brief 設定から足の接地補正を作る。 */
    [[nodiscard]] bool CreateFootIKModifier(const Procedural::PoseModifierCreateArgs& args,
                                             Container::TSharedPtr<IPoseModifier>& out,
                                             Procedural::PoseModifierConfigReport& report);
    /** @brief 設定による包みの中も含めて足の接地補正を探す。 */
    [[nodiscard]] FootIKModifier* FindFootIKModifier(IPoseModifier* modifier);
    /** @brief 接地設定から物理問い合わせに共通する設定を作る。 */
    [[nodiscard]] PhysicsGroundQuerySettings MakePhysicsGroundQuerySettings(const FootIKGroundSettings& ground);
} // namespace NorvesLib::Core::Animation

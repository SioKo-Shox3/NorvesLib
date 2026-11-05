// 脚ごとの接地状態と着地時の目標・重みを管理する。
#pragma once

#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Animation
{
    enum class EFootPlantPhase : uint8_t
    {
        Swing,
        Planted
    };

    struct FootPlantSettings
    {
        float HeightThreshold = 0.03f;     // しきい値駆動で接地とみなす持ち上がりの上限 [m]
        float SpeedThreshold = 0.5f;       // 接地開始に許す補正前の足先の水平速度 [m/s]
        float LiftHeight = 0.15f;          // Swing の持ち上がりの割合が 1 になる高さ [m]
        float ReleaseBlendSeconds = 0.1f; // 解除位置から Swing の目標へ戻る時間 [s]
    };

    struct FootLandSettings
    {
        float BlendSeconds = 0.1f;      // 着地の重みを 0 から 1 に上げる時間 [s]
        float SnapDistance = 0.05f;     // 着地の重みを上げ始める足先と地面の高さの差 [m]
        float TeleportDistance = 1.0f; // 所有者の移動がこれを超えたときに呼び出し側が初期化する [m]
    };

    struct FootPlantInput
    {
        Math::Vector3 AnimToeWorld; // 補正前の足先のワールド位置
        float OriginY = 0.0f;       // 所有者の原点のワールド Y
        float Sole = 0.0f;         // 足先の関節と地面の間の高さ [m]
        bool bHasGround = false;   // GroundPoint が有効か
        Math::Vector3 GroundPoint; // 足先の真下の地面のワールド位置
        bool bDownEvent = false;   // このフレームの接地イベント
        bool bUpEvent = false;     // このフレームの離地イベント
        bool bGrounded = true;     // 所有者が接地しているか
        float DeltaSeconds = 0.0f;
    };

    struct FootPlantOutput
    {
        Math::Vector3 ToeTargetWorld; // 目標がない場合は補正前の足先の位置
        EFootPlantPhase Phase = EFootPlantPhase::Swing;
        float LandWeight = 1.0f; // IK の重みに掛ける着地の重み [0, 1]
        bool bHasTarget = false;
        bool bReleasedThisFrame = false;
    };

    /** @brief 骨格に依存せず、一脚の固定位置と着地の重みを求める。 */
    class FootPlantState
    {
    public:
        /**
         * @brief 状態を進めて目標を返す。無効な入力や計算結果は状態と出力を変えずに拒否する。
         *
         * 位置・値・設定は有限でなければならず、無効な GroundPoint も有限でなければならない。
         * Sole・時間・速度と距離の設定は 0 以上、LiftHeight は正。HeightThreshold は負も許す。
         * 計算した目標が float に収まらないときも false を返す。
         */
        [[nodiscard]] bool Update(const FootPlantInput& input, const FootPlantSettings& plant,
                                  const FootLandSettings& land, FootPlantOutput& out);
        /** @brief 固定を解除し、時間を進めずに目標を求め直す。 */
        [[nodiscard]] bool Release(const FootPlantInput& input, const FootPlantSettings& plant,
                                   const FootLandSettings& land, FootPlantOutput& out);
        /** @brief 固定位置・速度履歴・着地状態・イベント駆動の印を初期化する。 */
        void Reset();
        EFootPlantPhase GetPhase() const;
        float GetLandWeight() const;
        bool IsEventDriven() const;
        /** @brief 最後に固定した地面上のワールド位置を返す。 */
        const Math::Vector3& GetAnchor() const;

    private:
        bool BeginRelease(const FootPlantInput& input, const FootPlantSettings& plant, double lift);
        bool MakeOutput(const FootPlantInput& input, const FootPlantSettings& plant, double lift,
                        FootPlantOutput& out) const;

        EFootPlantPhase m_Phase = EFootPlantPhase::Swing;
        Math::Vector3 m_Anchor;
        Math::Vector3 m_PreviousToe;
        Math::Vector3 m_ReleaseFrom;
        double m_ReleaseDelta = 0.0;
        double m_ReleaseElapsed = 0.0;
        double m_LandWeight = 1.0;
        bool m_bHasAnchor = false;
        bool m_bHasPreviousToe = false;
        bool m_bHasReleaseDelta = false;
        bool m_bReleaseBlending = false;
        bool m_bHasLandState = false;
        bool m_bLandingStarted = false;
        bool m_bEventDriven = false;
    };
} // namespace NorvesLib::Core::Animation

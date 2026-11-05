// 脚の接地高から体の上下補正とピッチを求める。
#pragma once

#include "Container/Span.h"

namespace NorvesLib::Core::Animation
{
    /** @brief 体の高さの補正範囲と平滑化の半減期を指定する。 */
    struct BodyBalanceSettings
    {
        float MaxDrop = 0.25f;    // 下げる上限（メートル、0 以上）
        float MaxRaise = 0.1f;    // 上げる上限（メートル、0 以上）
        float SmoothTime = 0.12f; // 臨界減衰の半減期（秒、0 以上）
    };

    /** @brief 一脚分の接地面とアニメーション上の足との高さの差を表す。 */
    struct BodyBalanceLegSample
    {
        float HeightDelta = 0.0f; // 接地面の高さ − 足の基準の高さ（モデル空間の上向き、メートル）
        bool bFront = false;      // 前脚なら true、後脚なら false
        bool bValid = false;      // 今のフレームで地面が有効なら true
    };

    /** @brief 平滑化された骨盤・胸の高さと体のピッチを表す。 */
    struct BodyBalanceOutput
    {
        float PelvisOffset = 0.0f; // 骨盤の上下オフセット（メートル）
        float ChestOffset = 0.0f;  // 胸の上下オフセット（メートル）
        float PitchRadians = 0.0f; // 胸が上がる向きを正とするピッチ（ラジアン）
    };

    /**
     * @brief 姿勢を変更せず、脚の接地高から体の補正を計算する。
     *
     * 有効な後脚と前脚の高さの差の最小値を、それぞれ骨盤と胸の目標にする。
     * 有効な脚がない群の目標は 0、前脚が一本もない場合の胸の目標は骨盤の目標にする。
     * 各目標を [-MaxDrop, MaxRaise] に制限し、SmoothTime を半減期とする臨界減衰で別々に平滑化する。
     * ピッチは胸と骨盤の平滑化後の高さの差と水平距離から求め、水平距離が 1e-4 未満なら 0 にする。
     * 非有限入力、負の dt や設定値、または平滑化に失敗した場合は false を返し、状態と出力を変更しない。
     * 無効な脚の HeightDelta も有限でなければならない。
     */
    class BodyBalance
    {
    public:
        /** @brief 保持するオフセット・速度・出力をすべて 0 に戻す。 */
        void Reset();

        /** @brief 脚の情報を反映し、成功した場合に補正を出力する。 */
        [[nodiscard]] bool Update(Container::Span<const BodyBalanceLegSample> legs, const BodyBalanceSettings& settings,
                                  float horizontalDistance, float dt, BodyBalanceOutput& out);

        /** @brief 最後に成功した更新の出力を返す。 */
        const BodyBalanceOutput& GetOutput() const;

    private:
        float m_PelvisOffset = 0.0f;
        float m_PelvisVelocity = 0.0f;
        float m_ChestOffset = 0.0f;
        float m_ChestVelocity = 0.0f;
        BodyBalanceOutput m_Output;
    };
} // namespace NorvesLib::Core::Animation

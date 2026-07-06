#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Component
{

    /**
     * @brief スプリングアーム駆動のための1フレーム分の入力意図（DTO）
     *
     * 入力層（MayaCameraController 等）が「どう動かしたいか」を量で表現し、
     * SpringArmComponent::ApplyIntent がアームのパラメーターへ反映します。
     *
     * この型は Component 層に置き、低依存（Math/Vector3 のみ）に保ちます。
     * 入力層から Component 層への一方向 include（Input → Component）とし、
     * Component → Input の逆依存は作りません。
     *
     * 単位の規約:
     * - YawDelta / PitchDelta: 度。アームの方位角・仰角に加算されます。
     * - DollyDelta: ワールド単位。正でアームを縮める（注視点に近づく）。
     *   ApplyIntent は ArmLength から DollyDelta を減算します。
     * - PanDelta: スクリーン基底（x=right, y=up）でのパン量。
     *   ApplyIntent が現在のカメラ basis へ投影し、ピボットの位置を動かします。
     *   x はコントローラのスクリーン右（旧 MayaCameraController::GetRight と同方向）。
     *   ApplyIntent 側の right（LookRotation 由来・右手系）は旧 GetRight と符号が逆のため、
     *   投影時に x 成分は -right(= 旧 right_old) へ向けて旧 Pan のドラッグ感へ揃えます。
     */
    struct SpringArmIntent
    {
        float YawDelta = 0.0f;                          ///< 方位角の増分（度）
        float PitchDelta = 0.0f;                        ///< 仰角の増分（度）
        float DollyDelta = 0.0f;                        ///< アーム長の縮小量（ワールド単位、+で近づく）
        Math::Vector3 PanDelta = Math::Vector3::Zero;   ///< スクリーン基底でのパン量（x=スクリーン右=旧GetRight方向, y=up）
        bool bHasInput = false;                         ///< この意図が実際の入力を含むか
    };

} // namespace NorvesLib::Core::Component

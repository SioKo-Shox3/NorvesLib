#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Core::Component
{
    /**
     * @brief 入力層から SpringArmComponent へ渡す1フレーム分の値 intent。
     *
     * YawDelta/PitchDelta は度、DollyDelta は正でアームを縮めます。
     * PanDelta はスクリーン基底（x=右、y=上）のワールド移動量です。
     * bHasInput は呼び出し側のフロー制御用で、数値適用の条件にはしません。
     */
    struct SpringArmIntent
    {
        float YawDelta = 0.0f;
        float PitchDelta = 0.0f;
        float DollyDelta = 0.0f;
        Math::Vector3 PanDelta = Math::Vector3::Zero;
        bool bHasInput = false;
    };
} // namespace NorvesLib::Core::Component

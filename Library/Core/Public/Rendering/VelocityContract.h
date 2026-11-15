// GBuffer の Velocity に格納する「履歴なし」の印を共有する。
// Assets/Shaders/Common/VelocityEncoding.glsl と同じ値に保つ。
#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering::VelocityContract
{
    // 両成分に格納する印の値。R16G16_FLOAT で正確に表せる。
    inline constexpr float NoHistoryValue = -32768.0f;
    // 両成分がこの値以下なら履歴なしの印とみなす。
    inline constexpr float NoHistoryDetectBelow = -16384.0f;
    // 前フレームのクリップ座標の w がこの値以下なら印を書く。既定のカメラの NearPlane に合わせた暫定値。
    inline constexpr float PreviousClipWMin = 0.1f;
    // R16G16_FLOAT の読み戻しに使う、印の半精度ビット列。
    inline constexpr uint16_t NoHistoryValueHalfBits = 0xF800u;

    // 両成分がしきい値以下のとき、履歴なしの印とみなす。
    constexpr bool IsNoHistory(float x, float y)
    {
        return x <= NoHistoryDetectBelow && y <= NoHistoryDetectBelow;
    }

    static_assert(NoHistoryValue <= NoHistoryDetectBelow);
    static_assert(IsNoHistory(NoHistoryValue, NoHistoryValue));
    static_assert(!IsNoHistory(0.0f, 0.0f));
} // namespace NorvesLib::Core::Rendering::VelocityContract

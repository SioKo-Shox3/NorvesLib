// GBuffer の Velocity に格納する「履歴なし」の印と速度を符号化する。
// 定数は Rendering/VelocityContract.h と同じ値に保つ。
#ifndef NORVES_VELOCITY_ENCODING_GLSL
#define NORVES_VELOCITY_ENCODING_GLSL

// 両成分に格納する印の値。R16G16_FLOAT で正確に表せる。
const float VELOCITY_NO_HISTORY_VALUE = -32768.0;
// 両成分がこの値以下なら履歴なしの印とみなす。
const float VELOCITY_NO_HISTORY_DETECT_BELOW = -16384.0;
// 前フレームのクリップ座標の w がこの値以下なら印を書く。既定のカメラの NearPlane に合わせた暫定値。
const float VELOCITY_PREVIOUS_CLIP_W_MIN = 0.1;

// 前フレームで面がカメラの背後か near 面の近くにあり、再投影できる履歴がないことを示す。
// TAA は履歴を捨て、動きぼけは動き 0 として扱う。印を知らない読み手も画面外の previousUV として履歴を捨てる。
vec2 VelocityNoHistory()
{
    return vec2(VELOCITY_NO_HISTORY_VALUE);
}

// 両成分がしきい値以下のとき、履歴なしの印とみなす。
bool VelocityIsNoHistory(vec2 velocity)
{
    return velocity.x <= VELOCITY_NO_HISTORY_DETECT_BELOW && velocity.y <= VELOCITY_NO_HISTORY_DETECT_BELOW;
}

// 現在の NDC と前フレームのクリップ座標から、現在の UV と前の UV の差を求める。
// 前フレームの w が小さい場合は履歴なしの印を返し、NaN や極端な速度は 0 にする。
vec2 VelocityFromClip(vec2 currentNdc, vec4 previousClip)
{
    if (previousClip.w <= VELOCITY_PREVIOUS_CLIP_W_MIN)
    {
        return VelocityNoHistory();
    }

    vec2 velocity = (currentNdc - previousClip.xy / previousClip.w) * 0.5;
    if (any(notEqual(velocity, velocity)) || dot(velocity, velocity) >= 1.0e6)
    {
        return vec2(0.0);
    }
    return velocity;
}

#endif

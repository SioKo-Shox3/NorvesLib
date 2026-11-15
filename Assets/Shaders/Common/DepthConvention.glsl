// ========================================
// カメラの深度の規約（Rendering/DepthConvention.h と同じ意味）。
// 片方を変えるときはもう片方も変える。現在は標準の Z（手前 0、奥 1）。
// 有効な深度ではクリア値と等しい深度だけが背景で、距離では切らない。
// 距離の上限が必要な効果はメートルの定数で切る。
// ========================================

#ifndef NORVES_DEPTH_CONVENTION_GLSL
#define NORVES_DEPTH_CONVENTION_GLSL

const float kDepthClear = 1.0;
const float kDepthNearest = 0.0;

bool IsCloserDepth(float a, float b) { return a < b; }
float ClosestDepth(float a, float b) { return min(a, b); }
float FarthestDepth(float a, float b) { return max(a, b); }
bool IsBackgroundDepth(float depth) { return !IsCloserDepth(depth, kDepthClear); }
float DepthTowardNear(float depth, float amount) { return depth - amount; }

// 前方への距離 = 近方面 / LinearDepthDenominator(深度, 近方面 / 遠方面)。
float LinearDepthDenominator(float depth, float nearOverFar)
{
    return (1.0 - depth) + depth * nearOverFar;
}

float LinearDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane / LinearDepthDenominator(depth, nearPlane / farPlane);
}

// 0 以上の深度は手前ほど鍵が小さい。-0.0 は +0.0 にそろえる。
uint DepthCloserKey(float depth)
{
    uint bits = floatBitsToUint(depth);
    return bits == 0x80000000u ? 0u : bits;
}

float DepthFromCloserKey(uint key) { return uintBitsToFloat(key); }

#endif // NORVES_DEPTH_CONVENTION_GLSL

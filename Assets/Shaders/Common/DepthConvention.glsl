// ========================================
// カメラの深度は既定で逆 Z（手前 1、奥 0、クリア 0）。
// NORVES_DEPTH_STANDARD を定義すると標準 Z。ShaderManager が起動時の選択で定義する。
// C++ の Rendering/DepthConvention.h と同じ意味。
// 有効な深度ではクリア値と等しい深度だけが背景で、距離では切らない。
// 距離の上限が必要な効果はメートルの定数で切る。
// ========================================

#ifndef NORVES_DEPTH_CONVENTION_GLSL
#define NORVES_DEPTH_CONVENTION_GLSL

#ifdef NORVES_DEPTH_STANDARD
const float kDepthClear = 1.0;
const float kDepthNearest = 0.0;
const float kDepthBeyondFar = 1.0e30;

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

bool ClipCrossesNearPlane(vec4 clip) { return clip.z < 0.0; }
float DepthAsStandard(float depth) { return depth; }
#else
const float kDepthClear = 0.0;
const float kDepthNearest = 1.0;
const float kDepthBeyondFar = -1.0e30;

bool IsCloserDepth(float a, float b) { return a > b; }
float ClosestDepth(float a, float b) { return max(a, b); }
float FarthestDepth(float a, float b) { return min(a, b); }
bool IsBackgroundDepth(float depth) { return !IsCloserDepth(depth, kDepthClear); }
float DepthTowardNear(float depth, float amount) { return depth + amount; }

// 前方への距離 = 近方面 / LinearDepthDenominator(深度, 近方面 / 遠方面)。
float LinearDepthDenominator(float depth, float nearOverFar)
{
    return depth + (1.0 - depth) * nearOverFar;
}

bool ClipCrossesNearPlane(vec4 clip) { return clip.z > clip.w; }
float DepthAsStandard(float depth) { return 1.0 - depth; }
#endif

float LinearDepth(float depth, float nearPlane, float farPlane)
{
    return nearPlane / LinearDepthDenominator(depth, nearPlane / farPlane);
}

// 0 以上の深度は手前ほど鍵が小さい。-0.0 は +0.0 にそろえる。
#ifdef NORVES_DEPTH_STANDARD
uint DepthCloserKey(float depth)
{
    uint bits = floatBitsToUint(depth);
    return bits == 0x80000000u ? 0u : bits;
}

float DepthFromCloserKey(uint key) { return uintBitsToFloat(key); }
#else
uint DepthCloserKey(float depth)
{
    uint bits = floatBitsToUint(depth);
    bits = bits == 0x80000000u ? 0u : bits;
    return ~bits;
}

float DepthFromCloserKey(uint key) { return uintBitsToFloat(~key); }
#endif

#endif // NORVES_DEPTH_CONVENTION_GLSL

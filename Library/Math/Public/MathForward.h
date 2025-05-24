#pragma once

#include <cstdint>

namespace NorvesLib::Math 
{

// アライメント設定（SIMD最適化用）
#if defined(_MSC_VER)
#define NORVES_ALIGN(x) __declspec(align(x))
#elif defined(__GNUC__) || defined(__clang__)
#define NORVES_ALIGN(x) __attribute__((aligned(x)))
#else
#define NORVES_ALIGN(x)
#endif

// 2次元ベクトル（x, y）
struct NORVES_ALIGN(8) Vector2;

// 3次元ベクトル（x, y, z）
struct NORVES_ALIGN(16) Vector3;

// 4次元ベクトル（x, y, z, w）
struct NORVES_ALIGN(16) Vector4;

// 4x4行列
struct NORVES_ALIGN(16) Matrix4x4;

// 四元数（回転表現用）
struct NORVES_ALIGN(16) Quaternion;

// トランスフォーム（位置、回転、拡縮）
struct NORVES_ALIGN(16) Transform;

} // namespace NorvesLib::Math
// カメラの深度の規約をまとめる唯一の置き場。既定は逆 Z（手前 1、奥 0）。
// 開発ビルドの起動引数で標準 Z を選べる。モードは起動時に 1 回決まり、途中で変わらない。
// GLSL は ShaderManager が標準 Z のときだけ NORVES_DEPTH_STANDARD を定義する。
// 影の地図と上から見た高さの地図は対象外。
// 線形化は CameraViewConstants::BuildForDevice の透視射影（Vulkan の ZeroToOne）から導く。
// 前方への距離 = 逆 Z: near / (depth + (1 - depth) * near / far)、
// 標準 Z: near / ((1 - depth) + depth * near / far)。遠方面は有限のまま扱う。

#pragma once

#include "RHI/RHITypes.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 起動時に選ぶカメラの深度の規約 */
    enum class DepthConventionMode : uint8_t
    {
        Reversed,
        Standard
    };
} // namespace NorvesLib::Core::Rendering

namespace NorvesLib::Core::Rendering::DepthConvention
{
    /** @brief 深度の規約ごとのクリア値・近方面・比較 */
    struct ModeValues
    {
        float ClearDepth;
        float NearestDepth;
        RHI::CompareOp DepthCompareOp;
        RHI::CompareOp DepthCompareOpOrEqual;
    };

    inline constexpr ModeValues ModeTable[] =
    {
        {0.0f, 1.0f, RHI::CompareOp::Greater, RHI::CompareOp::GreaterOrEqual},
        {1.0f, 0.0f, RHI::CompareOp::Less, RHI::CompareOp::LessOrEqual}
    };

    /** @brief GEngine が所有する、起動時に決まった規約を取得する */
    DepthConventionMode GetMode();

    constexpr bool IsReversed(DepthConventionMode mode)
    {
        return mode == DepthConventionMode::Reversed;
    }

    /** @brief 何も描いていない画素の深度（最も奥） */
    constexpr float ClearDepth(DepthConventionMode mode)
    {
        return ModeTable[static_cast<uint8_t>(mode)].ClearDepth;
    }

    /** @brief 近方面の深度（最も手前） */
    constexpr float NearestDepth(DepthConventionMode mode)
    {
        return ModeTable[static_cast<uint8_t>(mode)].NearestDepth;
    }

    /** @brief 手前の深度だけを通す比較 */
    constexpr RHI::CompareOp DepthCompareOp(DepthConventionMode mode)
    {
        return ModeTable[static_cast<uint8_t>(mode)].DepthCompareOp;
    }

    /** @brief 手前と同じ深度を通す比較 */
    constexpr RHI::CompareOp DepthCompareOpOrEqual(DepthConventionMode mode)
    {
        return ModeTable[static_cast<uint8_t>(mode)].DepthCompareOpOrEqual;
    }

    /** @brief a が b より手前なら true */
    constexpr bool IsCloserDepth(DepthConventionMode mode, float a, float b)
    {
        return IsReversed(mode) ? a > b : a < b;
    }

    /** @brief 手前の深度を返す */
    constexpr float ClosestDepth(DepthConventionMode mode, float a, float b)
    {
        return IsCloserDepth(mode, a, b) ? a : b;
    }

    /** @brief 奥の深度を返す */
    constexpr float FarthestDepth(DepthConventionMode mode, float a, float b)
    {
        return IsCloserDepth(mode, a, b) ? b : a;
    }

    /** @brief 有効な深度の範囲ではクリア値だけを背景とする。NaN も背景とする */
    constexpr bool IsBackgroundDepth(DepthConventionMode mode, float depth)
    {
        return !IsCloserDepth(mode, depth, ClearDepth(mode));
    }

    inline bool IsReversed()
    {
        return IsReversed(GetMode());
    }

    inline float ClearDepth()
    {
        return ClearDepth(GetMode());
    }

    inline float NearestDepth()
    {
        return NearestDepth(GetMode());
    }

    inline RHI::CompareOp DepthCompareOp()
    {
        return DepthCompareOp(GetMode());
    }

    inline RHI::CompareOp DepthCompareOpOrEqual()
    {
        return DepthCompareOpOrEqual(GetMode());
    }

    inline bool IsCloserDepth(float a, float b)
    {
        return IsCloserDepth(GetMode(), a, b);
    }

    inline float ClosestDepth(float a, float b)
    {
        return ClosestDepth(GetMode(), a, b);
    }

    inline float FarthestDepth(float a, float b)
    {
        return FarthestDepth(GetMode(), a, b);
    }

    inline bool IsBackgroundDepth(float depth)
    {
        return IsBackgroundDepth(GetMode(), depth);
    }

    /**
     * @brief 透視深度から前方への距離（m）を求める
     *
     * 近方面・遠方面が 0 < near < far を満たさない場合と、結果が非有限の場合は 0 を返す。
     */
    inline float LinearizeDepth(float depth, float nearPlane, float farPlane)
    {
        if (!(nearPlane > 0.0f) || !(farPlane > nearPlane))
        {
            return 0.0f;
        }

        const float nearFarRatio = nearPlane / farPlane;
        const float denominator = IsReversed()
            ? depth + (1.0f - depth) * nearFarRatio
            : (1.0f - depth) + depth * nearFarRatio;
        const float distance = nearPlane / denominator;
        return std::isfinite(distance) ? distance : 0.0f;
    }

    /** @brief より手前ほど小さい整数の鍵。有限な非負深度が対象で、-0.0 は +0.0 にそろえる */
    inline uint32_t CloserKeyBits(float depth)
    {
        const uint32_t rawBits = std::bit_cast<uint32_t>(depth);
        const uint32_t bits = rawBits == 0x80000000u ? 0u : rawBits;
        return IsReversed() ? ~bits : bits;
    }

    /** @brief 深度の鍵から深度へ戻す。逆 Z ではすべてのビットが 1 の空の鍵はクリア値の 0 になる */
    inline float DepthFromCloserKeyBits(uint32_t key)
    {
        return std::bit_cast<float>(IsReversed() ? ~key : key);
    }

    static_assert(IsBackgroundDepth(DepthConventionMode::Reversed, ClearDepth(DepthConventionMode::Reversed)));
    static_assert(!IsBackgroundDepth(DepthConventionMode::Reversed, NearestDepth(DepthConventionMode::Reversed)));
    static_assert(IsCloserDepth(DepthConventionMode::Reversed,
                               NearestDepth(DepthConventionMode::Reversed), ClearDepth(DepthConventionMode::Reversed)));
    static_assert(ClosestDepth(DepthConventionMode::Reversed,
                               NearestDepth(DepthConventionMode::Reversed), ClearDepth(DepthConventionMode::Reversed)) ==
                  NearestDepth(DepthConventionMode::Reversed));
    static_assert(FarthestDepth(DepthConventionMode::Reversed,
                                NearestDepth(DepthConventionMode::Reversed), ClearDepth(DepthConventionMode::Reversed)) ==
                  ClearDepth(DepthConventionMode::Reversed));

    static_assert(IsBackgroundDepth(DepthConventionMode::Standard, ClearDepth(DepthConventionMode::Standard)));
    static_assert(!IsBackgroundDepth(DepthConventionMode::Standard, NearestDepth(DepthConventionMode::Standard)));
    static_assert(IsCloserDepth(DepthConventionMode::Standard,
                               NearestDepth(DepthConventionMode::Standard), ClearDepth(DepthConventionMode::Standard)));
    static_assert(ClosestDepth(DepthConventionMode::Standard,
                               NearestDepth(DepthConventionMode::Standard), ClearDepth(DepthConventionMode::Standard)) ==
                  NearestDepth(DepthConventionMode::Standard));
    static_assert(FarthestDepth(DepthConventionMode::Standard,
                                NearestDepth(DepthConventionMode::Standard), ClearDepth(DepthConventionMode::Standard)) ==
                  ClearDepth(DepthConventionMode::Standard));
} // namespace NorvesLib::Core::Rendering::DepthConvention

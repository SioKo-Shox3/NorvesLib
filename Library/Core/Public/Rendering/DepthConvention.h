// カメラの深度の規約をまとめる唯一の置き場。現在は標準の Z（手前 0、奥 1）。
// Assets/Shaders/Common/DepthConvention.glsl も同じ意味を持つため、片方を変えるときはもう片方も変える。
// 影の地図と上から見た高さの地図は対象外。
// 線形化は CameraViewConstants::BuildForDevice の透視射影（Vulkan の ZeroToOne）から導く。
// 深度 = far / (far - near) × (1 - near / 前方への距離)。lighting.frag の位置復元と同じ式。

#pragma once

#include "RHI/RHITypes.h"

#include <cmath>

namespace NorvesLib::Core::Rendering::DepthConvention
{
    /** @brief 何も描いていない画素の深度（最も奥） */
    inline constexpr float ClearDepth = 1.0f;
    /** @brief 近方面の深度（最も手前） */
    inline constexpr float NearestDepth = 0.0f;
    /** @brief 手前の深度だけを通す比較 */
    inline constexpr RHI::CompareOp DepthCompareOp = RHI::CompareOp::Less;
    /** @brief 手前と同じ深度を通す比較 */
    inline constexpr RHI::CompareOp DepthCompareOpOrEqual = RHI::CompareOp::LessOrEqual;

    /** @brief a が b より手前なら true */
    constexpr bool IsCloserDepth(float a, float b)
    {
        return a < b;
    }

    /** @brief 手前の深度を返す */
    constexpr float ClosestDepth(float a, float b)
    {
        return IsCloserDepth(a, b) ? a : b;
    }

    /** @brief 奥の深度を返す */
    constexpr float FarthestDepth(float a, float b)
    {
        return IsCloserDepth(a, b) ? b : a;
    }

    /** @brief 有効な深度の範囲ではクリア値だけを背景とする。NaN も背景とする */
    constexpr bool IsBackgroundDepth(float depth)
    {
        return !IsCloserDepth(depth, ClearDepth);
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

        const float distance = nearPlane / ((1.0f - depth) + depth * (nearPlane / farPlane));
        return std::isfinite(distance) ? distance : 0.0f;
    }

    static_assert(IsBackgroundDepth(ClearDepth));
    static_assert(!IsBackgroundDepth(NearestDepth));
    static_assert(IsCloserDepth(NearestDepth, ClearDepth));
    static_assert(ClosestDepth(NearestDepth, ClearDepth) == NearestDepth);
    static_assert(FarthestDepth(NearestDepth, ClearDepth) == ClearDepth);
} // namespace NorvesLib::Core::Rendering::DepthConvention

#pragma once

#include "Math/Vector3.h"
#include <limits>

namespace NorvesLib::Modules::AI
{
    /// 目から対象までの線分の草の照会結果。
    struct GrassPathSample
    {
        float Transmittance = 1.0f; ///< 倍率を反映した草の透過率（0..1）。
        float MaxTopHeight = -std::numeric_limits<float>::infinity(); ///< 積分点の草の先端の最大高さ（m）。草がなければ負の無限大。
    };

    /// AI の知覚が視線上の草を照会するインターフェース。
    class IGrassFieldSource
    {
    public:
        /// 派生した草の場のソースを基底型経由で破棄できる。
        virtual ~IGrassFieldSource() = default;

        /// 目から対象までの草を stepMeters 刻み（m）で積分し、透過率と先端の最大高さを返す。既定は草なし。
        /// 実装側は GrassField::QueryOcclusion と同じく描画と共通の枯れ度を使い、積分値 τ に
        /// opacityScale を掛けてから指数関数を 1 回だけ適用し、透過率 exp(-τ × opacityScale) を求める。
        virtual GrassPathSample QueryGrassPath(const Math::Vector3&, const Math::Vector3&,
                                               float stepMeters, float opacityScale) const
        {
            return {};
        }
    };
}

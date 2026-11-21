#pragma once

#include "Math/Vector3.h"
#include <limits>

namespace NorvesLib::Modules::AI
{
    /// 目から対象までの線分の草の照会結果。
    struct GrassPathSample
    {
        float OpticalDepth = 0.0f; ///< 倍率を掛ける前の草の光学的厚さ（0 以上）。
        float MaxTopHeight = -std::numeric_limits<float>::infinity(); ///< 積分点の草の先端の最大高さ（m）。草がなければ負の無限大。
    };

    /// AI の知覚が視線上の草を照会するインターフェース。
    class IGrassFieldSource
    {
    public:
        /// 派生した草の場のソースを基底型経由で破棄できる。
        virtual ~IGrassFieldSource() = default;

        /// 目から対象までの草を第 3 引数の刻み（m）で積分した結果を返す。
        /// 実装側は描画と同じ式で枯れ度を反映する。既定は草なし。
        virtual GrassPathSample QueryGrassPath(const Math::Vector3&, const Math::Vector3&, float) const
        {
            return {};
        }
    };
}

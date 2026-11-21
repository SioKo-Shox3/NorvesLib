#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Modules::AI
{
    /// AI の知覚が草の透過率を取得するインターフェース。
    class IGrassFieldSource
    {
    public:
        /// 派生した草の場のソースを基底型経由で破棄できる。
        virtual ~IGrassFieldSource() = default;

        /// ワールド座標（m）の始点 a から終点 b までの草の透過率 [0, 1] を返す。第 3 引数は積分の刻み（m）。
        /// 実装側は描画と同じ式で枯れ度を反映する。既定は草なしで 1。
        virtual float QueryTransmittance(const Math::Vector3&, const Math::Vector3&, float) const
        {
            return 1.0f;
        }
    };
}

#pragma once

#include "Math/Vector3.h"

namespace NorvesLib::Modules::AI
{
    /// AI の知覚が風速を取得するインターフェース。
    class IWindSource
    {
    public:
        /// 派生した風のソースを基底型経由で破棄できる。
        virtual ~IWindSource() = default;

        /// ワールド位置（m）とゲーム開始からの時刻（s）における風速（m/s）を返す。既定は無風（零ベクトル）。
        virtual Math::Vector3 SampleWind(const Math::Vector3&, double) const
        {
            return Math::Vector3(0.0f, 0.0f, 0.0f);
        }
    };
}

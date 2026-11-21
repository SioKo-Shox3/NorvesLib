#pragma once

#include <cstdint>

namespace NorvesLib::Modules::AI
{
    /// ワールドの XZ 座標から地形の高さ、水深、草の密度を取得するインターフェース。
    class INavTerrainSource
    {
    public:
        /// 派生した地形ソースを基底型経由で破棄できる。
        virtual ~INavTerrainSource() = default;

        /// 指定した位置の地面の高さをメートルで返す。
        virtual float SampleHeight(float x, float z) const = 0;

        /// 指定した位置の水深をメートルで返す。既定は 0。
        virtual float SampleWaterDepth(float, float) const
        {
            return 0.0f;
        }

        /// 指定した位置の草の密度を 0～255 で返す。既定は 0。
        virtual uint8_t SampleGrassDensity(float, float) const
        {
            return 0;
        }
    };

    /// 全域で高さが一定の平らな地形。
    class FlatNavTerrainSource final : public INavTerrainSource
    {
    public:
        /// 地面の高さを指定する。既定の高さは 0 m。
        explicit FlatNavTerrainSource(float height = 0.0f)
            : m_Height(height)
        {
        }

        /// 座標によらず、設定した地面の高さを返す。
        float SampleHeight(float, float) const override
        {
            return m_Height;
        }

    private:
        float m_Height;
    };
} // namespace NorvesLib::Modules::AI

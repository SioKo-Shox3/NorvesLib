#pragma once

namespace NorvesLib::Core::Vegetation
{
    /**
     * @brief 地面の高さの取り出し口。このインターフェースに高さの取得を統一する。
     * 地形を使う実装は TerrainGroundHeightSource。実装の const 呼び出しは再入可能であること。
     */
    class IGroundHeightSource
    {
    public:
        virtual ~IGroundHeightSource() = default;
        virtual float GetGroundHeight(float x, float z) const = 0;
    };

    /** @brief すべての位置で一定の高さを返す地面。 */
    class ConstantGroundHeightSource final : public IGroundHeightSource
    {
    public:
        explicit ConstantGroundHeightSource(float height = 0.0f)
            : m_Height(height)
        {
        }

        float GetGroundHeight(float, float) const override
        {
            return m_Height;
        }

        void SetHeight(float height)
        {
            m_Height = height;
        }

    private:
        float m_Height;
    };
} // namespace NorvesLib::Core::Vegetation

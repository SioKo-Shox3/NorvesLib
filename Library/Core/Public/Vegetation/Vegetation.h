#pragma once

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;
}

namespace NorvesLib::Core::Vegetation
{
    class GrassField;

    /** @brief GameThread 専用の草の場の登録先。場の所有権は登録した側が持つ。 */
    class Vegetation
    {
    public:
        /** @brief 場を借用して登録する。null または別の場が登録済みなら false。同じ場の再登録は成功する。 */
        bool RegisterGrassField(GrassField* pField);
        /** @brief 登録中の場と一致するときだけ解除する。場を破棄する前に呼ぶこと。 */
        bool UnregisterGrassField(GrassField* pField);
        GrassField* GetGrassField();
        const GrassField* GetGrassField() const;
        /** @brief GameThread で変更時だけ描画データを公開する。初回と解除後は未登録なら null を送る。 */
        void PublishRenderData(Rendering::RenderWorld& renderWorld);

    private:
        GrassField* m_pGrassField = nullptr;
        uint64_t m_PublishedRevision = 0; // 公開した場の Revision。
        const GrassField* m_pPublishedField = nullptr;
        bool m_bPublishedOnce = false;
    };
} // namespace NorvesLib::Core::Vegetation

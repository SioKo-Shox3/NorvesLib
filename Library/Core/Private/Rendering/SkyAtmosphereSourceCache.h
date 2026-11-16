// 空と照明で共有する大気モデルと放射輝度の源を保持する。
#pragma once

#include "Container/PointerTypes.h"
#include "Container/VariableArray.h"
#include "Rendering/SkyAtmosphere.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 空の大気モデルと空由来 IBL の放射輝度の源を保持する
     *
     * RenderThread だけが触り、GameThread からは使わない。
     * 返すポインタは、次に異なる鍵で Acquire するか Reset するまで有効。
     * 放射輝度の源は、同じモデルでも大きさが変わると中身が置き換わる。
     */
    class SkyAtmosphereSourceCache final
    {
    public:
        SkyAtmosphereSourceCache() = default;
        SkyAtmosphereSourceCache(const SkyAtmosphereSourceCache&) = delete;
        SkyAtmosphereSourceCache& operator=(const SkyAtmosphereSourceCache&) = delete;

        /** 補正後の鍵に合う sky-view 表付きのモデルを返す。空が無効なら保持物を変えず nullptr を返す。 */
        const SkyAtmosphereModel* AcquireModel(const SkyAtmosphereParameters& parameters);

        /** 同じ鍵のモデルから作った正距円筒 RGBA32F の放射輝度の源を返す。作れなければ nullptr を返す。 */
        const Container::VariableArray<float>* AcquireRadianceSource(const SkyAtmosphereParameters& parameters,
                                                                      uint32_t width,
                                                                      uint32_t height);

        /** モデルを作った通算回数を返す。 */
        uint64_t GetModelBuildCount() const { return m_ModelBuildCount; }

        /** 保持物を捨てる。モデルの通算構築回数は残す。 */
        void Reset();

    private:
        Container::TUniquePtr<SkyAtmosphereModel> m_Model;
        SkyAtmosphereParameters m_ModelParameters;
        Container::VariableArray<float> m_RadianceSource;
        uint32_t m_RadianceSourceWidth = 0u;
        uint32_t m_RadianceSourceHeight = 0u;
        bool m_bRadianceSourceBuilt = false;
        bool m_bRadianceSourceValid = false;
        uint64_t m_ModelBuildCount = 0u;
    };
}

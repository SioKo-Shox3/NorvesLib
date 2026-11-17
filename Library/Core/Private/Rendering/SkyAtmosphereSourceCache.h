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
     * モデルと晴天の放射輝度の源は、晴天の鍵が変わるか Reset するまで有効。
     * 単位曇天の源は、単位曇天の鍵が変わるか Reset するまで有効で、晴天モデルの作り直しでは捨てない。
     * それぞれの放射輝度の源は、同じ鍵でも大きさが変わると中身が置き換わる。
     */
    class SkyAtmosphereSourceCache final
    {
    public:
        SkyAtmosphereSourceCache() = default;
        SkyAtmosphereSourceCache(const SkyAtmosphereSourceCache&) = delete;
        SkyAtmosphereSourceCache& operator=(const SkyAtmosphereSourceCache&) = delete;

        /**
         * 晴天の鍵に合う sky-view 表付きのモデルを返す。雲量を掛けず、GetParameters() は晴天の鍵を返す。
         * 空が無効なら保持物を変えず nullptr を返す。
         */
        const SkyAtmosphereModel* AcquireModel(const SkyAtmosphereParameters& parameters);

        /** 晴天モデルから作った正距円筒 RGBA32F の放射輝度の源を返す。作れなければ nullptr を返す。 */
        const Container::VariableArray<float>* AcquireRadianceSource(const SkyAtmosphereParameters& parameters,
                                                                      uint32_t width,
                                                                      uint32_t height);

        /**
         * 天頂と色味を 1 とした単位曇天の正距円筒 RGBA32F の源を返す。作れなければ nullptr を返す。
         * 鍵は sanitize 後の惑星半径・地表高度・地表アルベド・天頂と地平線の比で、太陽や雲量には依らない。
         */
        const Container::VariableArray<float>* AcquireUnitOvercastRadianceSource(
            const SkyAtmosphereParameters& parameters, uint32_t width, uint32_t height);

        /** モデルを作った通算回数を返す。 */
        uint64_t GetModelBuildCount() const { return m_ModelBuildCount; }

        /** 単位曇天の源を作った通算回数を返す。 */
        uint64_t GetUnitOvercastBuildCount() const { return m_UnitOvercastBuildCount; }

        /** 保持物を捨てる。モデルと単位曇天の源の通算構築回数は残す。 */
        void Reset();

    private:
        Container::TUniquePtr<SkyAtmosphereModel> m_Model;
        SkyAtmosphereParameters m_ModelParameters;
        Container::VariableArray<float> m_RadianceSource;
        uint32_t m_RadianceSourceWidth = 0u;
        uint32_t m_RadianceSourceHeight = 0u;
        bool m_bRadianceSourceBuilt = false;
        bool m_bRadianceSourceValid = false;
        SkyAtmosphereParameters m_UnitOvercastParameters;
        Container::VariableArray<float> m_UnitOvercastRadianceSource;
        uint32_t m_UnitOvercastSourceWidth = 0u;
        uint32_t m_UnitOvercastSourceHeight = 0u;
        bool m_bUnitOvercastSourceBuilt = false;
        bool m_bUnitOvercastSourceValid = false;
        uint64_t m_ModelBuildCount = 0u;
        uint64_t m_UnitOvercastBuildCount = 0u;
    };
}

// 地形の混ぜ合わせの材質のサンプラーを、撮り比べのために環境変数で差し替える（開発ビルドだけ）。
// Game の --terrain-mip-bias・--terrain-anisotropy が環境変数へ渡し、ラスタ（MegaGeometryPass）と可視バッファの解決
// （VisibilityResolvePass）が地形のサンプラーを作るときに同じ差し替えを掛ける。指定が無ければ desc を変えない。
//   NORVES_TERRAIN_MIP_BIAS        … ミップの偏り（-4〜4。負で細かい段を読む）
//   NORVES_TERRAIN_MAX_ANISOTROPY  … 異方性の上限（1〜16。1 は等方の線形）
#pragma once

#include "Logging/LogMacros.h"
#include "RHI/IDevice.h"

#include <cmath>
#include <cstdlib>

namespace NorvesLib::Core::Rendering
{
    inline void ApplyTerrainSamplerOverride(RHI::SamplerDesc &desc, const char *passName)
    {
#if NORVES_BUILD_DEVELOPMENT
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char *biasText = std::getenv("NORVES_TERRAIN_MIP_BIAS");
        const char *anisotropyText = std::getenv("NORVES_TERRAIN_MAX_ANISOTROPY");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        bool bOverridden = false;
        if (biasText != nullptr && biasText[0] != '\0')
        {
            char *end = nullptr;
            const float bias = std::strtof(biasText, &end);
            if (end != biasText && *end == '\0' && std::isfinite(bias) && bias >= -4.0f && bias <= 4.0f)
            {
                desc.mipLODBias = bias;
                bOverridden = true;
            }
        }
        if (anisotropyText != nullptr && anisotropyText[0] != '\0')
        {
            char *end = nullptr;
            const unsigned long anisotropy = std::strtoul(anisotropyText, &end, 10);
            if (end != anisotropyText && *end == '\0' && anisotropy >= 1ul && anisotropy <= 16ul)
            {
                desc.maxAnisotropy = static_cast<uint32_t>(anisotropy);
                if (anisotropy == 1ul)
                {
                    desc.filterMin = RHI::FilterMode::Linear;
                    desc.filterMag = RHI::FilterMode::Linear;
                    desc.filterMip = RHI::FilterMode::Linear;
                }
                bOverridden = true;
            }
        }
        if (bOverridden)
        {
            LOG_INFO_F("TERRAIN_SAMPLER pass=%s mip_bias=%.3f max_anisotropy=%u source=env", passName,
                       static_cast<double>(desc.mipLODBias), desc.maxAnisotropy);
        }
#else
        (void)desc;
        (void)passName;
#endif
    }
} // namespace NorvesLib::Core::Rendering

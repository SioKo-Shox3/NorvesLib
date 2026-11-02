#pragma once

#include "Particle/VfxEffectTypes.h"

namespace NorvesLib::Core::Particle
{
    constexpr uint32_t kVfxCookedMagic = 0x5846564Eu;
    constexpr uint32_t kVfxCookedVersion = 1u;

    bool LoadVfxEffectFromJson(const Container::String& text, VfxEffectAsset& outAsset, Container::String& outError);
    /** @brief エフェクト資産を検証し、JSON 文字列に書き出す。 */
    bool SaveVfxEffectToJson(const VfxEffectAsset& asset, Container::String& outText, Container::String& outError);
    bool CookVfxEffect(const VfxEffectAsset& asset, VariableArray<uint8_t>& outBytes, Container::String& outError);
    bool LoadCookedVfxEffect(Container::Span<const uint8_t> bytes, VfxEffectAsset& outAsset, Container::String& outError);
} // namespace NorvesLib::Core::Particle

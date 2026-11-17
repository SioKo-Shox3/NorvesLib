#pragma once

#include "Particle/VfxEffectTypes.h"

namespace NorvesLib::Core::Particle
{
    constexpr uint32_t kVfxCookedMagic = 0x5846564Eu;
    constexpr uint32_t kVfxCookedVersion = 3u;
    /// GpuSpriteRenderer の DrawInSlotOrder を持たない版 2。
    constexpr uint32_t kVfxCookedVersionLegacyV2 = 2u;
    constexpr uint32_t kVfxCookedVersionLegacyV1 = 1u;

    /// パスの効果を読む。先頭 4 バイトが kVfxCookedMagic なら焼いた形式、そうでなければ UTF-8 の JSON（BOM は除く）として読む。
    /// 相対パスは作業ディレクトリ基準。失敗したら false を返し error に理由を書く。
    bool LoadVfxEffectFile(const Container::String& path, VfxEffectAsset& asset, Container::String& error);

    bool LoadVfxEffectFromJson(const Container::String& text, VfxEffectAsset& outAsset, Container::String& outError);
    /** @brief エフェクト資産を検証し、JSON 文字列に書き出す。 */
    bool SaveVfxEffectToJson(const VfxEffectAsset& asset, Container::String& outText, Container::String& outError);
    bool CookVfxEffect(const VfxEffectAsset& asset, VariableArray<uint8_t>& outBytes, Container::String& outError);
    bool LoadCookedVfxEffect(Container::Span<const uint8_t> bytes, VfxEffectAsset& outAsset, Container::String& outError);
} // namespace NorvesLib::Core::Particle

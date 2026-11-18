#pragma once

#include "Asset/SurfaceSampleTable.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/VariableArray.h"

namespace NorvesLib::Core::Skeletal
{
    struct SkinMeshV1Data;
}

namespace NorvesLib::Tools::AssetCook
{
    struct DecodedTextureRgba8;

    // slot ごとの元画像と、焼き込み前の基本色の係数。画像が無ければ白を使う。
    struct SurfaceSampleAlbedoSource
    {
        const DecodedTextureRgba8* Image = nullptr;
        float Factor[3] = {1, 1, 1};
    };

    // LOD0 の面積に比例する決定的な標本を作る。失敗時は out を変えない。
    [[nodiscard]] bool BuildSurfaceSampleTable(
        const Core::Skeletal::SkinMeshV1Data& mesh,
        Core::Container::Span<const SurfaceSampleAlbedoSource> slotAlbedo, uint32_t count,
        Core::Container::VariableArray<Core::Skeletal::SurfaceSampleRecord>& out,
        Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::AssetCook

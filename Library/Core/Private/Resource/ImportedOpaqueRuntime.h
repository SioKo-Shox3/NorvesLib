#pragma once
#include "Resource/ModelMaterialStaging.h"
#include "Asset/CookedTextureFormat.h"
namespace NorvesLib::Core::ResourceIO::ModelStaging
{
    // GR79本文の既存shaderで正しく表現できるsubset。出力reasonは結果で置換する。
    [[nodiscard]] bool ValidateImportedOpaqueMaterial(const ImportedMaterialStaging& material,
                                                      Container::AnsiString& reason);
    // RGBA8/一層/所定色空間/全mip境界を検査。Opaque albedoは全alpha255へ限定する。
    [[nodiscard]] bool ValidateImportedTexture(const Asset::CookedTextureData& texture,
                                               Asset::CookedTextureColorSpace colorSpace, bool bRequireOpaqueAlpha,
                                               Container::AnsiString& reason);
    struct ImportedArmMip
    {
        uint32_t Width = 0, Height = 0;
        Container::VariableArray<uint8_t> Pixels[3];
    };
    struct ImportedArmChannels
    {
        Container::VariableArray<ImportedArmMip> Mips;
    };
    // 使うchannelの全mipを所有する。mask0ではimageに触れず空結果。失敗時outを保持する。
    [[nodiscard]] bool SplitImportedArmChannels(const Asset::CookedTextureData& texture, uint8_t mask,
                                                ImportedArmChannels& out, Container::AnsiString& reason);
} // namespace NorvesLib::Core::ResourceIO::ModelStaging

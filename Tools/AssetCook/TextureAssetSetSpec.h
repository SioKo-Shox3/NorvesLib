#pragma once
#include "Container/Containers.h"
#include "CookBatchReport.h"
#include "Resource/MaterialImportPolicy.h"
#include <cstddef>

namespace NorvesLib::Core { class JsonValue; }
namespace NorvesLib::Tools::AssetCook
{
    // spec v1の順序・値を所有する。usage等の未知fieldは旧wrapperと同じく解釈しない。
    struct TextureAssetSetEntry
    {
        Core::Container::AnsiString LogicalPath, SourcePath, Format, PackageName, EntryName, Variant;
        Core::Container::AnsiString Kind = "texture", EntryType = "Tex0";
        CookAssetBudget Budget;
        Core::Container::VariableArray<uint32_t> JointNodes;
    };
    struct TextureAssetSetSpec
    {
        uint32_t Version = 1;
        CookAssetBudget TotalBudget;
        Core::AssetImport::EmissiveScale Emission;
        Core::Container::AnsiString Name, PackageRoot, DefaultVariant;
        Core::Container::VariableArray<TextureAssetSetEntry> Textures;
    };
    enum class TextureAssetSetError
    {
        None, InvalidRoot, MissingField, DuplicateField, InvalidType, InvalidValue,
        InvalidVersion, UnsafePath, DuplicateLogicalKey, DuplicatePackage
    };
    struct TextureAssetSetResult
    {
        TextureAssetSetError Code = TextureAssetSetError::None;
        size_t TextureIndex = static_cast<size_t>(-1);
        const char* Field = "";
        [[nodiscard]] bool Succeeded() const { return Code == TextureAssetSetError::None; }
    };
    // 成功時だけoutを置換。sourceの存在や出力先とのaliasは実行層が検査する。
    [[nodiscard]] TextureAssetSetResult ParseTextureAssetSetSpec(const Core::JsonValue& root, TextureAssetSetSpec& out,
                                                                 bool bAllowVersion2 = false);
}

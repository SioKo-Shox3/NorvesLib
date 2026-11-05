// クック済みの草の場から、種別 ID を保った実行時の場を作る。
#pragma once

#include "Container/PointerTypes.h"
#include "Container/String.h"

namespace NorvesLib::Core::Asset
{
    struct CookedGrassFieldData;
}

namespace NorvesLib::Core::Vegetation
{
    class GrassField;
    class GrassSpeciesTable;

    /**
     * @brief 解析済みのデータから場を作る。失敗時は空を返す。
     * 表にない種別は既定値で補い、該当する名前を outWarning に列挙する。
     */
    [[nodiscard]] Container::TUniquePtr<GrassField> BuildGrassFieldFromCooked(
        const Asset::CookedGrassFieldData& data, const GrassSpeciesTable& loadedTable,
        Container::String& outWarning);
} // namespace NorvesLib::Core::Vegetation

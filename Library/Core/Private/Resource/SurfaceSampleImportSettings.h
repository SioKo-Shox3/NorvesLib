#pragma once

#include "Asset/SurfaceSampleTable.h"
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    struct SurfaceSampleImportSettings
    {
        bool bRequested = false; // 節があれば要求する。空 object も既定値で要求する。
        uint32_t Count = Skeletal::DefaultSurfaceSampleCount; // 標本数の既定値は仮。
    };
} // namespace NorvesLib::Core::AssetImport

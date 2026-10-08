#include "Resource/ImportSettingsHash.h"
#include "Asset/AssetPackageFormat.h"

namespace NorvesLib::Core::AssetImport
{
    ImportSettingsHash AppendImportSettingsHash(uint64_t sourceHash, bool bPresent,
        const ImportSettings& settings, uint32_t algorithmVersion) noexcept
    {
        if (!bPresent)
        {
            return {true, sourceHash};
        }
        const auto canonical = EncodeCanonicalSettings(settings);
        if (canonical.Size == 0)
        {
            return {};
        }
        uint64_t hash = sourceHash;
        const auto append = [&](uint8_t byte)
        {
            hash = (hash ^ byte) * Asset::AssetPackageFormatV1::Fnv1a64Prime;
        };
        for (size_t byte = 0; byte < sizeof(uint64_t); ++byte)
        {
            append(static_cast<uint8_t>(static_cast<uint64_t>(canonical.Size) >> (byte * 8)));
        }
        for (size_t byte = 0; byte < canonical.Size; ++byte)
        {
            append(canonical.Bytes[byte]);
        }
        for (size_t byte = 0; byte < sizeof(uint32_t); ++byte)
        {
            append(static_cast<uint8_t>(algorithmVersion >> (byte * 8)));
        }
        return {true, hash};
    }
} // namespace NorvesLib::Core::AssetImport

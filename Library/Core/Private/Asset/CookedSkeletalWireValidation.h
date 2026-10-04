#pragma once

#include "Asset/CookedSkeletalWireFormat.h"
#include "Container/Span.h"

namespace NorvesLib::Core::Asset
{
    enum class CookedSkeletalWireStatus
    {
        Success,
        InvalidInput,
        UnsupportedVersion,
        InvalidHeaderSize,
        InvalidRecordSize,
        InvalidCount,
        InvalidSectionRange,
        UnsupportedExtraVertex
    };
    struct CookedSkeletalWireProfile
    {
        uint16_t VersionMinor = 0;
        uint32_t HeaderSize = 0;
        bool bHasSubmeshTables = false;
        bool bAllowsMultipleClips = false;
    };
    struct CookedSkeletalWireSection
    {
        uint64_t Offset = 0;
        uint64_t Size = 0;
    };
    struct CookedSkeletalWireCounts
    {
        uint64_t Vertices = 0, Indices = 0, Joints = 0, Clips = 0, Channels = 0, Samples = 0;
        uint64_t Submeshes = 0, MaterialSlots = 0;
    };
    struct CookedSkeletalV02Extension
    {
        CookedSkeletalWireSection Submeshes;
        CookedSkeletalWireSection MaterialSlots;
        uint32_t SubmeshCount = 0;
        uint32_t MaterialSlotCount = 0;
    };
    // 版別契約を解決する。出力引数は成功時のみ変更する。
    [[nodiscard]] CookedSkeletalWireStatus ResolveCookedSkeletalWireProfile(uint16_t major, uint16_t minor,
        uint32_t headerSize, CookedSkeletalWireProfile& outProfile) noexcept;
    [[nodiscard]] CookedSkeletalWireStatus ValidateCookedSkeletalWireCounts(uint16_t minor,
        const CookedSkeletalWireCounts& counts) noexcept;
    // 順序はvertex/index/joint/clip/channel/sample/(submesh/slot)/string。
    // packed配置とrecord数量を検査する。paddingの実byte、文字列、record内容は別責務。
    [[nodiscard]] CookedSkeletalWireStatus ValidateCookedSkeletalWireSections(uint16_t minor, uint64_t fileSize,
        Container::Span<const CookedSkeletalWireSection> sections, const CookedSkeletalWireCounts& counts) noexcept;
    // extensionはheader[256,320)の64byte。extra vertexは現profileで0必須。
    [[nodiscard]] CookedSkeletalWireStatus ReadCookedSkeletalV02Extension(Container::Span<const uint8_t> extension,
        uint64_t fileSize, CookedSkeletalV02Extension& outExtension) noexcept;
    // raw hash用の境界検査。実headerのmagic/値とpayload内容の妥当性は検査しない。
    [[nodiscard]] CookedSkeletalWireStatus TryComputeCookedSkeletalWireHash(uint16_t major, uint16_t minor,
        uint32_t headerSize, Container::Span<const uint8_t> blob, uint64_t& outHash) noexcept;
} // namespace NorvesLib::Core::Asset

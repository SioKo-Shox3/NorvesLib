#pragma once

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::Save
{
    inline constexpr size_t SaveContainerHeaderSize = 48;
    inline constexpr uint32_t SaveContainerVersion = 1;
    inline constexpr uint32_t SavePayloadFormatJsonUtf8 = 1;

    enum class SaveContainerStatus : uint8_t
    {
        Ok,
        TooSmall,
        BadMagic,
        UnsupportedContainerVersion,
        UnsupportedPayloadFormat,
        BadFlags,
        HeaderHashMismatch,
        SizeMismatch,
        PayloadHashMismatch
    };

    struct SaveContainerInfo
    {
        uint32_t ContainerVersion = 0;
        uint32_t PayloadFormat = 0;
        uint32_t Flags = 0;
        uint64_t PayloadSize = 0;
        uint64_t PayloadHash = 0;
        uint64_t WriteCounter = 0;
        uint64_t HeaderHash = 0;
    };

    // ヘッダは48バイトで、整数欄はすべてリトルエンディアン。
    // ヘッダのバイト 0〜47 の配置は版によらず固定する。
    // 0: magic（4）、4: containerVersion（4）、8: payloadFormat（4）、12: flags（4）。
    // 16: payloadSize（8）、24: payloadHash（8）、32: writeCounter（8）、40: headerHash（8）。
    // headerHash はバイト 0〜39 を対象とし、writeCounter 等の単一ビット反転も検出する。
    // FNV-1a は破損検出用であり、改ざん対策ではない。
    void EncodeSaveContainer(Container::Span<const uint8_t> payload, uint64_t writeCounter,
                             Container::VariableArray<uint8_t>& outBytes);

    // 成功した場合のペイロードは bytes を借用し、失敗した場合の出力は空にする。
    SaveContainerStatus DecodeSaveContainer(Container::Span<const uint8_t> bytes, SaveContainerInfo& outInfo,
                                            Container::Span<const uint8_t>& outPayload);

    const char* GetSaveContainerStatusName(SaveContainerStatus status);
} // namespace NorvesLib::Core::Save

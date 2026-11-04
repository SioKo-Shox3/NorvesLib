#include "Save/SaveContainer.h"

#include "Asset/AssetPackageFormat.h"
#include <limits>
#include <utility>

namespace NorvesLib::Core::Save
{
    namespace
    {
        constexpr uint8_t Magic[] = {'N', 'V', 'S', 'V'};

        void WriteLittleEndian(uint8_t* pBytes, uint64_t value, size_t count)
        {
            for (size_t index = 0; index < count; ++index)
            {
                pBytes[index] = static_cast<uint8_t>(value >> (index * 8));
            }
        }

        uint64_t ReadLittleEndian(const uint8_t* pBytes, size_t count)
        {
            uint64_t result = 0;
            for (size_t index = 0; index < count; ++index)
            {
                result |= static_cast<uint64_t>(pBytes[index]) << (index * 8);
            }
            return result;
        }
    } // namespace

    void EncodeSaveContainer(Container::Span<const uint8_t> payload, uint64_t writeCounter,
                             Container::VariableArray<uint8_t>& outBytes)
    {
        if (payload.size() > (std::numeric_limits<size_t>::max)() - SaveContainerHeaderSize)
        {
            outBytes.clear();
            return;
        }

        // 入力が outBytes の一部を借用していても、書き換え前にコピーする。
        Container::VariableArray<uint8_t> encoded(SaveContainerHeaderSize + payload.size(), 0);
        for (size_t index = 0; index < sizeof(Magic); ++index)
        {
            encoded[index] = Magic[index];
        }
        WriteLittleEndian(encoded.data() + 4, SaveContainerVersion, 4);
        WriteLittleEndian(encoded.data() + 8, SavePayloadFormatJsonUtf8, 4);
        WriteLittleEndian(encoded.data() + 12, 0, 4);
        WriteLittleEndian(encoded.data() + 16, static_cast<uint64_t>(payload.size()), 8);
        WriteLittleEndian(encoded.data() + 24, Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size()), 8);
        WriteLittleEndian(encoded.data() + 32, writeCounter, 8);
        WriteLittleEndian(encoded.data() + 40, Asset::ComputeAssetPackagePayloadHash(encoded.data(), 40), 8);
        for (size_t index = 0; index < payload.size(); ++index)
        {
            encoded[SaveContainerHeaderSize + index] = payload[index];
        }
        outBytes = std::move(encoded);
    }

    SaveContainerStatus DecodeSaveContainer(Container::Span<const uint8_t> bytes, SaveContainerInfo& outInfo,
                                            Container::Span<const uint8_t>& outPayload)
    {
        outInfo = {};
        outPayload = {};
        if (bytes.size() < SaveContainerHeaderSize)
        {
            return SaveContainerStatus::TooSmall;
        }
        for (size_t index = 0; index < sizeof(Magic); ++index)
        {
            if (bytes[index] != Magic[index])
            {
                return SaveContainerStatus::BadMagic;
            }
        }
        SaveContainerInfo info;
        info.HeaderHash = ReadLittleEndian(bytes.data() + 40, 8);
        if (Asset::ComputeAssetPackagePayloadHash(bytes.data(), 40) != info.HeaderHash)
        {
            return SaveContainerStatus::HeaderHashMismatch;
        }
        info.ContainerVersion = static_cast<uint32_t>(ReadLittleEndian(bytes.data() + 4, 4));
        if (info.ContainerVersion != SaveContainerVersion)
        {
            return SaveContainerStatus::UnsupportedContainerVersion;
        }
        info.PayloadFormat = static_cast<uint32_t>(ReadLittleEndian(bytes.data() + 8, 4));
        if (info.PayloadFormat != SavePayloadFormatJsonUtf8)
        {
            return SaveContainerStatus::UnsupportedPayloadFormat;
        }
        info.Flags = static_cast<uint32_t>(ReadLittleEndian(bytes.data() + 12, 4));
        if (info.Flags != 0)
        {
            return SaveContainerStatus::BadFlags;
        }
        info.PayloadSize = ReadLittleEndian(bytes.data() + 16, 8);
        if (info.PayloadSize != bytes.size() - SaveContainerHeaderSize)
        {
            return SaveContainerStatus::SizeMismatch;
        }
        info.PayloadHash = ReadLittleEndian(bytes.data() + 24, 8);
        info.WriteCounter = ReadLittleEndian(bytes.data() + 32, 8);
        const auto payload = bytes.subspan(SaveContainerHeaderSize);
        if (Asset::ComputeAssetPackagePayloadHash(payload.data(), payload.size()) != info.PayloadHash)
        {
            return SaveContainerStatus::PayloadHashMismatch;
        }
        outInfo = info;
        outPayload = payload;
        return SaveContainerStatus::Ok;
    }

    const char* GetSaveContainerStatusName(SaveContainerStatus status)
    {
        switch (status)
        {
        case SaveContainerStatus::Ok: return "Ok";
        case SaveContainerStatus::TooSmall: return "TooSmall";
        case SaveContainerStatus::BadMagic: return "BadMagic";
        case SaveContainerStatus::UnsupportedContainerVersion: return "UnsupportedContainerVersion";
        case SaveContainerStatus::UnsupportedPayloadFormat: return "UnsupportedPayloadFormat";
        case SaveContainerStatus::BadFlags: return "BadFlags";
        case SaveContainerStatus::HeaderHashMismatch: return "HeaderHashMismatch";
        case SaveContainerStatus::SizeMismatch: return "SizeMismatch";
        case SaveContainerStatus::PayloadHashMismatch: return "PayloadHashMismatch";
        }
        return "Unknown";
    }
} // namespace NorvesLib::Core::Save

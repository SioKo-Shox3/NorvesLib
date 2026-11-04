#include "Save/SaveContainer.h"
#include "Asset/AssetPackageFormat.h"
#include <cstdio>
#include <cstring>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Save;

namespace
{
#define SAVE_CONTAINER_CHECK(condition) \
    do { if (!(condition)) { std::fprintf(stderr, "確認失敗: %s (%s:%d)\n", #condition, __FILE__, __LINE__); return false; } } while (false)

    uint32_t NextRandom(uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    void WriteLittleEndian(Container::VariableArray<uint8_t>& bytes, size_t offset, uint64_t value, size_t size)
    {
        for (size_t index = 0; index < size; ++index)
        {
            bytes[offset + index] = static_cast<uint8_t>(value >> (index * 8));
        }
    }

    void RefreshHeaderHash(Container::VariableArray<uint8_t>& bytes)
    {
        WriteLittleEndian(bytes, 40, Asset::ComputeAssetPackagePayloadHash(bytes.data(), 40), 8);
    }

    bool RoundTrip(Container::Span<const uint8_t> payload, uint64_t counter)
    {
        Container::VariableArray<uint8_t> bytes;
        EncodeSaveContainer(payload, counter, bytes);
        SAVE_CONTAINER_CHECK(bytes.size() == SaveContainerHeaderSize + payload.size());
        SaveContainerInfo info;
        Container::Span<const uint8_t> decoded;
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, decoded) == SaveContainerStatus::Ok);
        SAVE_CONTAINER_CHECK(info.ContainerVersion == 1 && info.PayloadFormat == 1 && info.Flags == 0);
        SAVE_CONTAINER_CHECK(info.WriteCounter == counter && info.PayloadSize == payload.size());
        SAVE_CONTAINER_CHECK(decoded.size() == payload.size());
        for (size_t index = 0; index < payload.size(); ++index)
        {
            SAVE_CONTAINER_CHECK(decoded[index] == payload[index]);
        }
        return true;
    }

    bool TestRoundTrip()
    {
        SAVE_CONTAINER_CHECK(RoundTrip({}, 0));
        uint32_t state = 0x9366c9e5u;
        Container::VariableArray<uint8_t> payload(4096);
        for (uint8_t& byte : payload)
        {
            byte = static_cast<uint8_t>(NextRandom(state));
        }
        SAVE_CONTAINER_CHECK(RoundTrip(payload, 0x0102030405060708ull));

        Container::VariableArray<uint8_t> bytes = payload;
        EncodeSaveContainer(bytes, 0x0102030405060708ull, bytes);
        SAVE_CONTAINER_CHECK(bytes[32] == 8 && bytes[33] == 7 && bytes[34] == 6 && bytes[35] == 5 &&
                             bytes[36] == 4 && bytes[37] == 3 && bytes[38] == 2 && bytes[39] == 1);
        SaveContainerInfo info;
        Container::Span<const uint8_t> decoded;
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, decoded) == SaveContainerStatus::Ok);
        SAVE_CONTAINER_CHECK(decoded.size() == payload.size() &&
                             std::memcmp(decoded.data(), payload.data(), payload.size()) == 0);
        return true;
    }

    bool TestCorruptionAndTruncation()
    {
        uint32_t state = 0x86eae17bu;
        Container::VariableArray<uint8_t> payload(4096);
        for (uint8_t& byte : payload)
        {
            byte = static_cast<uint8_t>(NextRandom(state));
        }
        Container::VariableArray<uint8_t> bytes;
        EncodeSaveContainer(payload, 123456789, bytes);
        SaveContainerInfo info;
        Container::Span<const uint8_t> decoded;
        for (size_t attempt = 0; attempt < 10000; ++attempt)
        {
            const size_t index = NextRandom(state) % bytes.size();
            const uint8_t mask = static_cast<uint8_t>(1u << (NextRandom(state) % 8));
            bytes[index] ^= mask;
            SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, decoded) != SaveContainerStatus::Ok);
            SAVE_CONTAINER_CHECK(decoded.empty() && info.WriteCounter == 0);
            bytes[index] ^= mask;
        }
        for (size_t length = 0; length < bytes.size(); ++length)
        {
            SAVE_CONTAINER_CHECK(DecodeSaveContainer({bytes.data(), length}, info, decoded) != SaveContainerStatus::Ok);
        }
        bytes.push_back(0);
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, decoded) == SaveContainerStatus::SizeMismatch);
        return true;
    }

    bool TestStatuses()
    {
        const uint8_t data[] = {1, 2, 3};
        Container::VariableArray<uint8_t> original;
        EncodeSaveContainer(data, 4, original);
        SaveContainerInfo info;
        Container::Span<const uint8_t> payload;
        Container::VariableArray<uint8_t> bytes = original;
        bytes[0] ^= 1;
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::BadMagic);
        bytes = original;
        WriteLittleEndian(bytes, 4, 2, 4);
        RefreshHeaderHash(bytes);
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::UnsupportedContainerVersion);
        bytes = original;
        bytes[32] ^= 1;
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::HeaderHashMismatch);
        bytes = original;
        WriteLittleEndian(bytes, 8, 2, 4);
        RefreshHeaderHash(bytes);
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::UnsupportedPayloadFormat);
        bytes = original;
        WriteLittleEndian(bytes, 12, 1, 4);
        RefreshHeaderHash(bytes);
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::BadFlags);
        bytes = original;
        WriteLittleEndian(bytes, 16, UINT64_MAX, 8);
        RefreshHeaderHash(bytes);
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::SizeMismatch);
        bytes = original;
        bytes.back() ^= 1;
        SAVE_CONTAINER_CHECK(DecodeSaveContainer(bytes, info, payload) == SaveContainerStatus::PayloadHashMismatch);
        SAVE_CONTAINER_CHECK(std::strcmp(GetSaveContainerStatusName(SaveContainerStatus::HeaderHashMismatch),
                                         "HeaderHashMismatch") == 0);
        return true;
    }
} // namespace

int main()
{
    return TestRoundTrip() && TestCorruptionAndTruncation() && TestStatuses() ? 0 : 1;
}

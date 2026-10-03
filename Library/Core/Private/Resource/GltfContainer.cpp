#include "Resource/GltfContainer.h"

namespace NorvesLib::Core::Gltf
{
    namespace
    {
        constexpr uint32_t GlbMagic = 0x46546c67;
        constexpr uint32_t JsonChunk = 0x4e4f534a;
        constexpr uint32_t BinChunk = 0x004e4942;

        // 呼出側で4バイトの範囲を確認する。非整列ポインタでも安全なlittle-endian読取り。
        uint32_t ReadU32(const uint8_t* bytes) noexcept
        {
            return static_cast<uint32_t>(bytes[0]) |
                (static_cast<uint32_t>(bytes[1]) << 8) |
                (static_cast<uint32_t>(bytes[2]) << 16) |
                (static_cast<uint32_t>(bytes[3]) << 24);
        }
    }

    ContainerParseResult ParseContainer(Container::Span<const uint8_t> file, ContainerView& outView) noexcept
    {
        outView = {};
        if (!file.empty() && file.data() == nullptr)
        {
            return ContainerParseResult::InvalidArgument;
        }
        if (file.size() < 4 || ReadU32(file.data()) != GlbMagic)
        {
            // テキスト経路だけUTF-8 BOMを除く。空入力のnullptrへ加算しない。
            if (file.size() >= 3 && file[0] == 0xef && file[1] == 0xbb && file[2] == 0xbf)
            {
                outView.Json = {file.data()+3, file.size()-3};
            }
            else
            {
                outView.Json = file;
            }
            return ContainerParseResult::NotGlb;
        }
        if (file.size() < 12)
        {
            return ContainerParseResult::TruncatedHeader;
        }
        if (ReadU32(file.data()+4) != 2)
        {
            return ContainerParseResult::UnsupportedVersion;
        }
        if (ReadU32(file.data()+8) != file.size())
        {
            return ContainerParseResult::LengthMismatch;
        }

        ContainerView candidate;
        candidate.IsGlb = true;
        size_t cursor = 12;
        size_t chunkIndex = 0;
        bool hasJson = false;
        while (cursor < file.size())
        {
            if (file.size()-cursor < 8)
            {
                return ContainerParseResult::TruncatedChunk;
            }
            const uint32_t length = ReadU32(file.data()+cursor);
            const uint32_t type = ReadU32(file.data()+cursor+4);
            cursor += 8;
            if ((length & 3u) != 0)
            {
                return ContainerParseResult::InvalidChunkLength;
            }
            // 先に残長と比較し、cursor+lengthのoverflowを避ける。
            if (length > file.size()-cursor)
            {
                return ContainerParseResult::TruncatedChunk;
            }
            if (chunkIndex == 0 && type != JsonChunk)
            {
                return ContainerParseResult::InvalidChunkOrder;
            }
            if (type == JsonChunk)
            {
                if (hasJson)
                {
                    return ContainerParseResult::DuplicateChunk;
                }
                candidate.Json = {file.data()+cursor, length};
                hasJson = true;
            }
            else if (type == BinChunk)
            {
                if (candidate.HasBin)
                {
                    return ContainerParseResult::DuplicateChunk;
                }
                if (chunkIndex != 1)
                {
                    return ContainerParseResult::InvalidChunkOrder;
                }
                candidate.Bin = {file.data()+cursor, length};
                candidate.HasBin = true;
            }
            // 未知chunkも長さと整列は検証するが、内容には触れない。
            cursor += length;
            ++chunkIndex;
        }
        if (!hasJson)
        {
            return ContainerParseResult::MissingJsonChunk;
        }
        outView = candidate;
        return ContainerParseResult::Success;
    }
}

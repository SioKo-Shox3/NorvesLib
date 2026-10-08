#include "Asset/CookedSkeletalNameCodec.h"

namespace NorvesLib::Core::Asset
{
    namespace SkeletalNameDetail
    {
        bool ValidStorage(const void* data, size_t count, size_t elementSize, size_t alignment) noexcept
        {
            if (elementSize == 0 || alignment == 0)
            {
                return false;
            }
            if (count == 0)
            {
                return true;
            }
            const uintptr_t address = reinterpret_cast<uintptr_t>(data);
            return data && address % alignment == 0 && count <= std::numeric_limits<size_t>::max() / elementSize &&
                count * elementSize <= std::numeric_limits<uintptr_t>::max() - address;
        }
        bool Overlaps(const void* first, size_t firstBytes, const void* second, size_t secondBytes) noexcept
        {
            if (firstBytes == 0 || secondBytes == 0)
            {
                return false;
            }
            // 呼出側がValidStorageで両方の終端overflowを検査済み。
            const uintptr_t a = reinterpret_cast<uintptr_t>(first), b = reinterpret_cast<uintptr_t>(second);
            return a < b + secondBytes && b < a + firstBytes;
        }
        SkeletalNameStatus ReadUtf8(Container::Span<const uint8_t> bytes, size_t& cursor, uint32_t& scalar) noexcept
        {
            if (!bytes.data() || cursor >= bytes.size())
            {
                return SkeletalNameStatus::InvalidUtf8;
            }
            const uint8_t lead = bytes[cursor];
            size_t length = 0;
            uint32_t value = 0, minimum = 0;
            if (lead < 0x80)
            {
                length = 1; value = lead;
            }
            else if (lead >= 0xc2 && lead <= 0xdf)
            {
                length = 2; value = lead & 0x1f; minimum = 0x80;
            }
            else if (lead >= 0xe0 && lead <= 0xef)
            {
                length = 3; value = lead & 0x0f; minimum = 0x800;
            }
            else if (lead >= 0xf0 && lead <= 0xf4)
            {
                length = 4; value = lead & 7; minimum = 0x10000;
            }
            else
            {
                return SkeletalNameStatus::InvalidUtf8;
            }
            if (length > bytes.size() - cursor)
            {
                return SkeletalNameStatus::InvalidUtf8;
            }
            for (size_t index = 1; index < length; ++index)
            {
                const uint8_t next = bytes[cursor + index];
                if ((next & 0xc0) != 0x80)
                {
                    return SkeletalNameStatus::InvalidUtf8;
                }
                value = (value << 6) | (next & 0x3f);
            }
            if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            {
                return SkeletalNameStatus::InvalidUtf8;
            }
            scalar = value;
            cursor += length;
            return SkeletalNameStatus::Success;
        }
        ScanResult Scan(uint16_t minor, Container::Span<const uint8_t> bytes) noexcept
        {
            if (minor > 2)
            {
                return {SkeletalNameStatus::UnsupportedVersion};
            }
            if (bytes.size() > UINT32_MAX)
            {
                return {SkeletalNameStatus::NameTooLong};
            }
            if (!ValidStorage(bytes.data(), bytes.size(), 1, 1))
            {
                return {SkeletalNameStatus::InvalidInput};
            }
            ScanResult result{SkeletalNameStatus::Success, 0, 0};
            size_t cursor = 0;
            while (cursor < bytes.size())
            {
                uint32_t scalar = 0;
                if (minor < 2)
                {
                    scalar = bytes[cursor++];
                    if (scalar != 0 && (scalar < 0x20 || scalar > 0x7e))
                    {
                        return {SkeletalNameStatus::InvalidAscii};
                    }
                }
                else if (ReadUtf8(bytes, cursor, scalar) != SkeletalNameStatus::Success)
                {
                    return {SkeletalNameStatus::InvalidUtf8};
                }
                if (scalar == 0)
                {
                    return {SkeletalNameStatus::EmbeddedNul};
                }
                ++result.CodePoints;
                result.Utf16Units += scalar > 0xffff ? 2 : 1;
            }
            return result;
        }
        void WriteUtf8(uint32_t scalar, uint8_t* output, size_t& cursor) noexcept
        {
            if (scalar < 0x80)
            {
                output[cursor++] = static_cast<uint8_t>(scalar);
            }
            else if (scalar < 0x800)
            {
                output[cursor++] = static_cast<uint8_t>(0xc0 | (scalar >> 6));
                output[cursor++] = static_cast<uint8_t>(0x80 | (scalar & 0x3f));
            }
            else if (scalar < 0x10000)
            {
                output[cursor++] = static_cast<uint8_t>(0xe0 | (scalar >> 12));
                output[cursor++] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3f));
                output[cursor++] = static_cast<uint8_t>(0x80 | (scalar & 0x3f));
            }
            else
            {
                output[cursor++] = static_cast<uint8_t>(0xf0 | (scalar >> 18));
                output[cursor++] = static_cast<uint8_t>(0x80 | ((scalar >> 12) & 0x3f));
                output[cursor++] = static_cast<uint8_t>(0x80 | ((scalar >> 6) & 0x3f));
                output[cursor++] = static_cast<uint8_t>(0x80 | (scalar & 0x3f));
            }
        }
    }

    SkeletalWireNameView ResolveSkeletalWireName(uint16_t minor, Container::Span<const uint8_t> stringTable,
        uint64_t offset, uint64_t length) noexcept
    {
        if (minor > 2)
        {
            return {SkeletalNameStatus::UnsupportedVersion, {}};
        }
        if (length > UINT32_MAX)
        {
            return {SkeletalNameStatus::NameTooLong, {}};
        }
        if (!SkeletalNameDetail::ValidStorage(stringTable.data(), stringTable.size(), 1, 1))
        {
            return {SkeletalNameStatus::InvalidInput, {}};
        }
        if (offset > stringTable.size() || length > stringTable.size() - offset)
        {
            return {SkeletalNameStatus::ReferenceOutOfRange, {}};
        }
        const uint8_t* data = stringTable.data() ? stringTable.data() + static_cast<size_t>(offset) : nullptr;
        const Container::Span<const uint8_t> bytes(data, static_cast<size_t>(length));
        const auto scan = SkeletalNameDetail::Scan(minor, bytes);
        if (scan.Status != SkeletalNameStatus::Success)
        {
            return {scan.Status, {}};
        }
        return {SkeletalNameStatus::Success, bytes};
    }
} // namespace NorvesLib::Core::Asset

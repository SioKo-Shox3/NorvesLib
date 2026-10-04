#pragma once

#include "Container/Span.h"
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

namespace NorvesLib::Core::Asset
{
    enum class SkeletalNameStatus
    {
        Success, InvalidInput, UnsupportedVersion, InvalidAscii, InvalidUtf8, InvalidUnicode,
        EmbeddedNul, NameTooLong, ReferenceOutOfRange, InsufficientStorage, OverlappingStorage
    };
    struct SkeletalNameCodecResult
    {
        SkeletalNameStatus Status = SkeletalNameStatus::InvalidInput;
        size_t ByteCount = 0;
        size_t CodeUnitCount = 0;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalNameStatus::Success;
        }
    };
    struct SkeletalWireNameView
    {
        SkeletalNameStatus Status = SkeletalNameStatus::InvalidInput;
        Container::Span<const uint8_t> Bytes;
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SkeletalNameStatus::Success;
        }
    };
    namespace SkeletalNameDetail
    {
        struct ScanResult
        {
            SkeletalNameStatus Status = SkeletalNameStatus::InvalidInput;
            size_t CodePoints = 0;
            size_t Utf16Units = 0;
        };
        [[nodiscard]] bool ValidStorage(const void* data, size_t count, size_t elementSize, size_t alignment) noexcept;
        [[nodiscard]] bool Overlaps(const void* first, size_t firstBytes, const void* second, size_t secondBytes) noexcept;
        [[nodiscard]] ScanResult Scan(uint16_t minor, Container::Span<const uint8_t> bytes) noexcept;
        [[nodiscard]] SkeletalNameStatus ReadUtf8(Container::Span<const uint8_t> bytes, size_t& cursor, uint32_t& scalar) noexcept;
        void WriteUtf8(uint32_t scalar, uint8_t* output, size_t& cursor) noexcept;
        [[nodiscard]] constexpr size_t Utf8Size(uint32_t scalar) noexcept
        {
            return scalar < 0x80 ? 1 : scalar < 0x800 ? 2 : scalar < 0x10000 ? 3 : 4;
        }
        template<typename Char>
        inline constexpr bool SupportedChar = std::is_same_v<Char,char> || std::is_same_v<Char,unsigned char> ||
            std::is_same_v<Char,char8_t> || std::is_same_v<Char,char16_t> || std::is_same_v<Char,char32_t> || std::is_same_v<Char,wchar_t>;
        template<typename Char>
        [[nodiscard]] SkeletalNameStatus ReadWide(Container::Span<const Char> source, size_t& cursor, uint32_t& scalar) noexcept
        {
            static_assert(sizeof(Char) == 2 || sizeof(Char) == 4);
            using Unsigned = std::make_unsigned_t<Char>;
            if (cursor >= source.size())
            {
                return SkeletalNameStatus::InvalidUnicode;
            }
            uint32_t value = static_cast<uint32_t>(static_cast<Unsigned>(source[cursor++]));
            if constexpr (sizeof(Char) == 2)
            {
                if (value >= 0xd800 && value <= 0xdbff)
                {
                    if (cursor >= source.size())
                    {
                        return SkeletalNameStatus::InvalidUnicode;
                    }
                    const uint32_t low = static_cast<uint32_t>(static_cast<Unsigned>(source[cursor++]));
                    if (low < 0xdc00 || low > 0xdfff)
                    {
                        return SkeletalNameStatus::InvalidUnicode;
                    }
                    value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
                }
            }
            if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
            {
                return SkeletalNameStatus::InvalidUnicode;
            }
            scalar = value;
            return SkeletalNameStatus::Success;
        }
    }

    // minor0/1はprintable ASCII、minor2は厳密UTF-8。全版でNULを拒否し、正規化/置換はしない。
    // 参照長はu32 byte数。成功viewはstringTableの寿命に従う借用で、失敗は空view。
    [[nodiscard]] SkeletalWireNameView ResolveSkeletalWireName(uint16_t minor, Container::Span<const uint8_t> stringTable,
        uint64_t offset, uint64_t length) noexcept;

    // Charの1byteはUTF-8（旧版ではASCII）、2byteはUTF-16、4byteはUnicode scalar列。
    // ByteCountは保存byte数、CodeUnitCountはencoding元またはdecoding先のChar単位数。
    template<typename Char>
    [[nodiscard]] SkeletalNameCodecResult MeasureSkeletalNameEncoding(uint16_t minor, Container::Span<const Char> source) noexcept
    {
        static_assert(SkeletalNameDetail::SupportedChar<Char>);
        if (minor > 2)
        {
            return {SkeletalNameStatus::UnsupportedVersion};
        }
        if (source.size() > UINT32_MAX)
        {
            return {SkeletalNameStatus::NameTooLong};
        }
        if (!SkeletalNameDetail::ValidStorage(source.data(), source.size(), sizeof(Char), alignof(Char)))
        {
            return {SkeletalNameStatus::InvalidInput};
        }
        if constexpr (sizeof(Char) == 1)
        {
            const auto scan = SkeletalNameDetail::Scan(minor, {reinterpret_cast<const uint8_t*>(source.data()), source.size()});
            if (scan.Status != SkeletalNameStatus::Success)
            {
                return {scan.Status};
            }
            return {SkeletalNameStatus::Success, source.size(), source.size()};
        }
        else
        {
            size_t cursor = 0, bytes = 0;
            while (cursor < source.size())
            {
                uint32_t scalar = 0;
                const auto status = SkeletalNameDetail::ReadWide(source, cursor, scalar);
                if (status != SkeletalNameStatus::Success)
                {
                    return {status};
                }
                if (scalar == 0)
                {
                    return {SkeletalNameStatus::EmbeddedNul};
                }
                if (minor < 2 && (scalar < 0x20 || scalar > 0x7e))
                {
                    return {SkeletalNameStatus::InvalidAscii};
                }
                const size_t required = SkeletalNameDetail::Utf8Size(scalar);
                if (bytes > UINT32_MAX - required)
                {
                    return {SkeletalNameStatus::NameTooLong};
                }
                bytes += required;
            }
            return {SkeletalNameStatus::Success, bytes, source.size()};
        }
    }

    template<typename Char>
    [[nodiscard]] SkeletalNameCodecResult MeasureSkeletalNameDecoding(uint16_t minor, Container::Span<const uint8_t> bytes) noexcept
    {
        static_assert(SkeletalNameDetail::SupportedChar<Char>);
        const auto scan = SkeletalNameDetail::Scan(minor, bytes);
        if (scan.Status != SkeletalNameStatus::Success)
        {
            return {scan.Status};
        }
        const size_t units = sizeof(Char) == 1 ? bytes.size() : sizeof(Char) == 2 ? scan.Utf16Units : scan.CodePoints;
        return {SkeletalNameStatus::Success, bytes.size(), units};
    }

    // 失敗時は全outを保持する。入力/全out領域の重複（未使用末尾を含む）を拒否する。
    // 終端NULは書かない。呼出中に入力を変更してはならない。
    template<typename Char>
    [[nodiscard]] SkeletalNameCodecResult EncodeSkeletalWireName(uint16_t minor, Container::Span<const Char> source,
        Container::Span<uint8_t> out) noexcept
    {
        const auto measured = MeasureSkeletalNameEncoding(minor, source);
        if (!measured.Succeeded())
        {
            return measured;
        }
        if (!SkeletalNameDetail::ValidStorage(out.data(), out.size(), 1, 1))
        {
            return {SkeletalNameStatus::InvalidInput};
        }
        if (SkeletalNameDetail::Overlaps(source.data(), source.size() * sizeof(Char), out.data(), out.size()))
        {
            return {SkeletalNameStatus::OverlappingStorage};
        }
        if (out.size() < measured.ByteCount)
        {
            return {SkeletalNameStatus::InsufficientStorage};
        }
        if constexpr (sizeof(Char) == 1)
        {
            if (measured.ByteCount != 0)
            {
                std::memcpy(out.data(), source.data(), measured.ByteCount);
            }
        }
        else
        {
            size_t inputCursor = 0, outputCursor = 0;
            while (inputCursor < source.size())
            {
                uint32_t scalar = 0;
                (void)SkeletalNameDetail::ReadWide(source, inputCursor, scalar); // 全入力を先行検証済み。
                SkeletalNameDetail::WriteUtf8(scalar, out.data(), outputCursor);
            }
        }
        return measured;
    }

    template<typename Char>
    [[nodiscard]] SkeletalNameCodecResult DecodeSkeletalWireName(uint16_t minor, Container::Span<const uint8_t> bytes,
        Container::Span<Char> out) noexcept
    {
        const auto measured = MeasureSkeletalNameDecoding<Char>(minor, bytes);
        if (!measured.Succeeded())
        {
            return measured;
        }
        if (!SkeletalNameDetail::ValidStorage(out.data(), out.size(), sizeof(Char), alignof(Char)))
        {
            return {SkeletalNameStatus::InvalidInput};
        }
        if (SkeletalNameDetail::Overlaps(bytes.data(), bytes.size(), out.data(), out.size() * sizeof(Char)))
        {
            return {SkeletalNameStatus::OverlappingStorage};
        }
        if (out.size() < measured.CodeUnitCount)
        {
            return {SkeletalNameStatus::InsufficientStorage};
        }
        if constexpr (sizeof(Char) == 1)
        {
            if (measured.ByteCount != 0)
            {
                std::memcpy(out.data(), bytes.data(), measured.ByteCount);
            }
        }
        else
        {
            size_t inputCursor = 0, outputCursor = 0;
            while (inputCursor < bytes.size())
            {
                uint32_t scalar = 0;
                (void)SkeletalNameDetail::ReadUtf8(bytes, inputCursor, scalar); // 全入力を先行検証済み。
                if constexpr (sizeof(Char) == 2)
                {
                    if (scalar > 0xffff)
                    {
                        scalar -= 0x10000;
                        out[outputCursor++] = static_cast<Char>(0xd800 + (scalar >> 10));
                        out[outputCursor++] = static_cast<Char>(0xdc00 + (scalar & 0x3ff));
                        continue;
                    }
                }
                out[outputCursor++] = static_cast<Char>(scalar);
            }
        }
        return measured;
    }
} // namespace NorvesLib::Core::Asset

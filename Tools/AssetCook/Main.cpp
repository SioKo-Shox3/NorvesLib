#include "MeshCooker.h"
#include "AudioCooker.h"
#include "TextureCooker.h"

#include "Asset/CookedMeshFormat.h"
#include "Asset/CookedAudioFormat.h"
#include "Asset/CookedSkeletalFormat.h"
#include "Asset/CookedTextureFormat.h"
#include "Asset/AssetManifest.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetPath.h"
#include "Asset/AssetSystem.h"
#include "Container/Span.h"
#include "FileStream/Package.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <tchar.h>

namespace
{
    using NorvesLib::Core::Asset::AssetKind;
    using NorvesLib::Core::Asset::AssetBlob;
    using NorvesLib::Core::Asset::AssetPackageCompression;
    using NorvesLib::Core::Asset::AssetPackageFourCC;
    using NorvesLib::Core::Asset::AssetPath;
    using NorvesLib::Core::Asset::AssetResolveStatus;
    using NorvesLib::Core::Asset::AssetSystem;
    using NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash;
    using NorvesLib::Core::Asset::FormatAssetHashHex;
    using NorvesLib::Core::Asset::FormatAssetPackageFourCCText;
    using NorvesLib::Core::Asset::MakeAssetPackageFourCC;
    using NorvesLib::Core::Asset::ParseCookedMesh;
    using NorvesLib::Core::Asset::ParseCookedAudio;
    using NorvesLib::Core::Asset::ParseCookedSkeletal;
    using NorvesLib::Core::Asset::ParseCookedTexture;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::EndianMarker;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::EntryRecordSize;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::HeaderSize;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::Magic;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::MagicSize;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::MinimumAlignment;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::RawEntryType;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::VersionMajor;
    using NorvesLib::Core::Asset::AssetPackageFormatV1::VersionMinor;
    namespace HeaderOffset = NorvesLib::Core::Asset::AssetPackageFormatV1::HeaderOffset;
    namespace EntryOffset = NorvesLib::Core::Asset::AssetPackageFormatV1::EntryOffset;

    struct CookOptions
    {
        std::filesystem::path InputPath;
        std::filesystem::path PackagePath;
        std::filesystem::path ManifestPath;
        std::string LogicalPath;
        std::string Kind;
        std::string EntryName;
        std::string EntryTypeText;
        std::string Format;
        std::string Variant;
    };

    std::string ToStdString(const NorvesLib::Core::Container::AnsiString &value)
    {
        return std::string(value.data(), value.size());
    }

    NorvesLib::Core::Container::String ToCoreString(const std::string &value)
    {
#if defined(UNICODE)
        std::wstring wide;
        wide.reserve(value.size());
        for (const unsigned char character : value)
        {
            wide.push_back(static_cast<wchar_t>(character));
        }
        return NorvesLib::Core::Container::String(wide.c_str());
#else
        return NorvesLib::Core::Container::String(value.c_str());
#endif
    }

    NorvesLib::Core::Container::String ToCoreString(NorvesLib::Core::Container::AnsiStringView value)
    {
        NorvesLib::Core::Container::String result;
        result.reserve(value.size());
        for (const char character : value)
        {
            result += static_cast<TCHAR>(static_cast<unsigned char>(character));
        }
        return result;
    }

    bool CheckedAdd(size_t lhs, size_t rhs, size_t &outValue)
    {
        if (lhs > std::numeric_limits<size_t>::max() - rhs)
        {
            return false;
        }

        outValue = lhs + rhs;
        return true;
    }

    bool AlignUp(size_t value, size_t alignment, size_t &outValue)
    {
        if (alignment == 0)
        {
            return false;
        }

        size_t withPadding = 0;
        if (!CheckedAdd(value, alignment - 1, withPadding))
        {
            return false;
        }

        outValue = withPadding & ~(alignment - 1);
        return true;
    }

    void WriteLe16(std::vector<uint8_t> &bytes, size_t offset, uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteLe32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteLe64(std::vector<uint8_t> &bytes, size_t offset, uint64_t value)
    {
        WriteLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    void WriteSkeletalLe16(NorvesLib::Core::Container::VariableArray<uint8_t>& bytes,
                           size_t offset,
                           uint16_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
    }

    void WriteSkeletalLe32(NorvesLib::Core::Container::VariableArray<uint8_t>& bytes,
                           size_t offset,
                           uint32_t value)
    {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xffu);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xffu);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xffu);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xffu);
    }

    void WriteSkeletalLe64(NorvesLib::Core::Container::VariableArray<uint8_t>& bytes,
                           size_t offset,
                           uint64_t value)
    {
        WriteSkeletalLe32(bytes, offset, static_cast<uint32_t>(value & 0xffffffffull));
        WriteSkeletalLe32(bytes, offset + 4, static_cast<uint32_t>((value >> 32) & 0xffffffffull));
    }

    bool IsAsciiPrintable(char value)
    {
        const unsigned char byte = static_cast<unsigned char>(value);
        return byte >= 0x20 && byte <= 0x7e;
    }

    bool ValidateAsciiJsonField(std::string_view fieldName, std::string_view value, std::string &error)
    {
        if (value.empty())
        {
            error = std::string(fieldName) + " must not be empty";
            return false;
        }

        for (const char character : value)
        {
            if (!IsAsciiPrintable(character))
            {
                error = std::string(fieldName) + " must contain printable ASCII only";
                return false;
            }
        }

        return true;
    }

    bool NormalizeManifestPathField(std::string_view fieldName,
                                    const std::string &value,
                                    std::string &outValue,
                                    std::string &error)
    {
        if (!ValidateAsciiJsonField(fieldName, value, error))
        {
            return false;
        }

        const AssetPath path = AssetPath::Normalize(NorvesLib::Core::Container::AnsiString(value));
        if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
        {
            error = std::string(fieldName) + " must be a valid relative logical path";
            return false;
        }

        outValue = ToStdString(path.GetLogicalPath());
        return ValidateAsciiJsonField(fieldName, outValue, error);
    }

    bool ValidateSkeletalAsciiField(NorvesLib::Core::Container::AnsiStringView value, auto& error)
    {
        if (value.empty())
        {
            error = "skeletal manifest field must not be empty";
            return false;
        }

        for (const char character : value)
        {
            if (!IsAsciiPrintable(character))
            {
                error = "skeletal manifest field must contain printable ASCII only";
                return false;
            }
        }

        return true;
    }

    bool NormalizeSkeletalManifestPath(NorvesLib::Core::Container::AnsiStringView value,
                                       NorvesLib::Core::Container::AnsiString& outValue,
                                       auto& error)
    {
        if (!ValidateSkeletalAsciiField(value, error))
        {
            return false;
        }

        const AssetPath path = AssetPath::Normalize(value);
        if (!path.IsValid() || path.IsAbsolute() || !path.HasLogicalPath())
        {
            error = "skeletal manifest path must be a valid relative logical path";
            return false;
        }

        outValue = path.GetLogicalPath();
        return ValidateSkeletalAsciiField(outValue, error);
    }

    bool ParseEntryType(const std::string &text,
                        AssetPackageFourCC &outType,
                        std::string &outManifestText,
                        std::string &error)
    {
        if (text == "Raw")
        {
            outType = RawEntryType;
            outManifestText = ToStdString(FormatAssetPackageFourCCText(outType));
            return true;
        }

        if (text.size() != 4)
        {
            error = "--entry-type must be Raw or exactly 4 printable ASCII bytes";
            return false;
        }

        for (const char character : text)
        {
            if (!IsAsciiPrintable(character))
            {
                error = "--entry-type must be Raw or exactly 4 printable ASCII bytes";
                return false;
            }
        }

        outType = MakeAssetPackageFourCC(text[0], text[1], text[2], text[3]);
        outManifestText = ToStdString(FormatAssetPackageFourCCText(outType));
        return true;
    }

    std::string EscapeJsonString(const std::string &value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (const char character : value)
        {
            if (character == '\\' || character == '"')
            {
                escaped.push_back('\\');
            }

            escaped.push_back(character);
        }

        return escaped;
    }

    void AppendJsonStringField(std::string &json,
                               const char *name,
                               const std::string &value,
                               bool bTrailingComma)
    {
        json += "\"";
        json += name;
        json += "\":\"";
        json += EscapeJsonString(value);
        json += "\"";
        if (bTrailingComma)
        {
            json += ",";
        }
    }

    bool BuildSingleEntryPackage(const std::string &entryName,
                                 AssetPackageFourCC entryType,
                                 const std::vector<uint8_t> &payload,
                                 std::vector<uint8_t> &outBytes,
                                 uint64_t &outPayloadHash,
                                 std::string &error)
    {
        if (entryName.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        {
            error = "entry name is too large";
            return false;
        }

        size_t entryTableEnd = 0;
        if (!CheckedAdd(HeaderSize, EntryRecordSize, entryTableEnd))
        {
            error = "package entry table size overflow";
            return false;
        }

        size_t nameTableOffset = 0;
        if (!AlignUp(entryTableEnd, MinimumAlignment, nameTableOffset))
        {
            error = "package name table offset overflow";
            return false;
        }

        size_t nameTableEnd = 0;
        if (!CheckedAdd(nameTableOffset, entryName.size(), nameTableEnd))
        {
            error = "package name table size overflow";
            return false;
        }

        size_t blobDataOffset = 0;
        if (!AlignUp(nameTableEnd, MinimumAlignment, blobDataOffset))
        {
            error = "package blob data offset overflow";
            return false;
        }

        size_t packageSize = 0;
        if (!CheckedAdd(blobDataOffset, payload.size(), packageSize))
        {
            error = "package payload size overflow";
            return false;
        }

        outBytes.assign(packageSize, 0);
        std::memcpy(outBytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteLe32(outBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteLe16(outBytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteLe16(outBytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteLe32(outBytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteLe32(outBytes, HeaderOffset::EntryRecordSize, static_cast<uint32_t>(EntryRecordSize));
        WriteLe64(outBytes, HeaderOffset::PackageSize, static_cast<uint64_t>(packageSize));
        WriteLe32(outBytes, HeaderOffset::EntryCount, 1);
        WriteLe32(outBytes, HeaderOffset::Flags, 0);
        WriteLe64(outBytes, HeaderOffset::EntryTableOffset, static_cast<uint64_t>(HeaderSize));
        WriteLe64(outBytes, HeaderOffset::EntryTableSize, static_cast<uint64_t>(EntryRecordSize));
        WriteLe64(outBytes, HeaderOffset::NameTableOffset, static_cast<uint64_t>(nameTableOffset));
        WriteLe64(outBytes, HeaderOffset::NameTableSize, static_cast<uint64_t>(entryName.size()));
        WriteLe64(outBytes, HeaderOffset::BlobDataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteLe32(outBytes, HeaderOffset::Alignment, static_cast<uint32_t>(MinimumAlignment));
        WriteLe32(outBytes, HeaderOffset::Reserved0, 0);
        WriteLe64(outBytes, HeaderOffset::Reserved1, 0);

        if (!entryName.empty())
        {
            std::memcpy(outBytes.data() + nameTableOffset, entryName.data(), entryName.size());
        }

        if (!payload.empty())
        {
            std::memcpy(outBytes.data() + blobDataOffset, payload.data(), payload.size());
        }

        outPayloadHash = ComputeAssetPackagePayloadHash(payload.data(), payload.size());

        const size_t recordOffset = HeaderSize;
        WriteLe64(outBytes, recordOffset + EntryOffset::NameOffset, static_cast<uint64_t>(nameTableOffset));
        WriteLe32(outBytes, recordOffset + EntryOffset::NameSize, static_cast<uint32_t>(entryName.size()));
        WriteLe32(outBytes, recordOffset + EntryOffset::Type, entryType);
        WriteLe32(outBytes, recordOffset + EntryOffset::Compression, static_cast<uint32_t>(AssetPackageCompression::None));
        WriteLe32(outBytes, recordOffset + EntryOffset::Flags, 0);
        WriteLe64(outBytes, recordOffset + EntryOffset::DataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteLe64(outBytes, recordOffset + EntryOffset::StoredSize, static_cast<uint64_t>(payload.size()));
        WriteLe64(outBytes, recordOffset + EntryOffset::UncompressedSize, static_cast<uint64_t>(payload.size()));
        WriteLe64(outBytes, recordOffset + EntryOffset::PayloadHash, outPayloadHash);
        WriteLe64(outBytes, recordOffset + EntryOffset::Reserved0, 0);

        return true;
    }

    bool BuildSingleSkeletalEntryPackage(
        NorvesLib::Core::Container::AnsiStringView entryName,
        AssetPackageFourCC entryType,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& payload,
        NorvesLib::Core::Container::VariableArray<uint8_t>& outBytes,
        uint64_t& outPayloadHash,
        auto& error)
    {
        if (entryName.size() > static_cast<size_t>(std::numeric_limits<uint32_t>::max()))
        {
            error = "entry name is too large";
            return false;
        }

        size_t entryTableEnd = 0;
        if (!CheckedAdd(HeaderSize, EntryRecordSize, entryTableEnd))
        {
            error = "package entry table size overflow";
            return false;
        }

        size_t nameTableOffset = 0;
        if (!AlignUp(entryTableEnd, MinimumAlignment, nameTableOffset))
        {
            error = "package name table offset overflow";
            return false;
        }

        size_t nameTableEnd = 0;
        if (!CheckedAdd(nameTableOffset, entryName.size(), nameTableEnd))
        {
            error = "package name table size overflow";
            return false;
        }

        size_t blobDataOffset = 0;
        if (!AlignUp(nameTableEnd, MinimumAlignment, blobDataOffset))
        {
            error = "package blob data offset overflow";
            return false;
        }

        size_t packageSize = 0;
        if (!CheckedAdd(blobDataOffset, payload.size(), packageSize))
        {
            error = "package payload size overflow";
            return false;
        }

        outBytes.assign(packageSize, 0);
        std::memcpy(outBytes.data() + HeaderOffset::Magic, Magic, MagicSize);
        WriteSkeletalLe32(outBytes, HeaderOffset::HeaderSize, static_cast<uint32_t>(HeaderSize));
        WriteSkeletalLe16(outBytes, HeaderOffset::VersionMajor, VersionMajor);
        WriteSkeletalLe16(outBytes, HeaderOffset::VersionMinor, VersionMinor);
        WriteSkeletalLe32(outBytes, HeaderOffset::EndianMarker, EndianMarker);
        WriteSkeletalLe32(outBytes, HeaderOffset::EntryRecordSize, static_cast<uint32_t>(EntryRecordSize));
        WriteSkeletalLe64(outBytes, HeaderOffset::PackageSize, static_cast<uint64_t>(packageSize));
        WriteSkeletalLe32(outBytes, HeaderOffset::EntryCount, 1);
        WriteSkeletalLe32(outBytes, HeaderOffset::Flags, 0);
        WriteSkeletalLe64(outBytes, HeaderOffset::EntryTableOffset, static_cast<uint64_t>(HeaderSize));
        WriteSkeletalLe64(outBytes, HeaderOffset::EntryTableSize, static_cast<uint64_t>(EntryRecordSize));
        WriteSkeletalLe64(outBytes, HeaderOffset::NameTableOffset, static_cast<uint64_t>(nameTableOffset));
        WriteSkeletalLe64(outBytes, HeaderOffset::NameTableSize, static_cast<uint64_t>(entryName.size()));
        WriteSkeletalLe64(outBytes, HeaderOffset::BlobDataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteSkeletalLe32(outBytes, HeaderOffset::Alignment, static_cast<uint32_t>(MinimumAlignment));
        WriteSkeletalLe32(outBytes, HeaderOffset::Reserved0, 0);
        WriteSkeletalLe64(outBytes, HeaderOffset::Reserved1, 0);

        if (!entryName.empty())
        {
            std::memcpy(outBytes.data() + nameTableOffset, entryName.data(), entryName.size());
        }

        if (!payload.empty())
        {
            std::memcpy(outBytes.data() + blobDataOffset, payload.data(), payload.size());
        }

        outPayloadHash = ComputeAssetPackagePayloadHash(payload.data(), payload.size());

        const size_t recordOffset = HeaderSize;
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::NameOffset, static_cast<uint64_t>(nameTableOffset));
        WriteSkeletalLe32(outBytes, recordOffset + EntryOffset::NameSize, static_cast<uint32_t>(entryName.size()));
        WriteSkeletalLe32(outBytes, recordOffset + EntryOffset::Type, entryType);
        WriteSkeletalLe32(
            outBytes,
            recordOffset + EntryOffset::Compression,
            static_cast<uint32_t>(AssetPackageCompression::None));
        WriteSkeletalLe32(outBytes, recordOffset + EntryOffset::Flags, 0);
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::DataOffset, static_cast<uint64_t>(blobDataOffset));
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::StoredSize, static_cast<uint64_t>(payload.size()));
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::UncompressedSize, static_cast<uint64_t>(payload.size()));
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::PayloadHash, outPayloadHash);
        WriteSkeletalLe64(outBytes, recordOffset + EntryOffset::Reserved0, 0);

        return true;
    }

    bool ReadBinaryFile(const std::filesystem::path &path, std::vector<uint8_t> &outBytes, std::string &error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "failed to open input file: " + path.string();
            return false;
        }

        input.seekg(0, std::ios::end);
        const std::streamoff fileSize = input.tellg();
        if (fileSize < 0)
        {
            error = "failed to query input file size: " + path.string();
            return false;
        }

        if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = "input file is too large: " + path.string();
            return false;
        }

        outBytes.resize(static_cast<size_t>(fileSize));
        input.seekg(0, std::ios::beg);
        if (!outBytes.empty())
        {
            input.read(reinterpret_cast<char *>(outBytes.data()), static_cast<std::streamsize>(outBytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(outBytes.size()))
            {
                error = "failed to read input file: " + path.string();
                return false;
            }
        }

        return true;
    }

    bool ReadSkeletalBinaryFile(const std::filesystem::path& path,
                                NorvesLib::Core::Container::VariableArray<uint8_t>& outBytes,
                                auto& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input.is_open())
        {
            error = "failed to open skeletal input file";
            return false;
        }

        input.seekg(0, std::ios::end);
        const std::streamoff fileSize = input.tellg();
        if (fileSize < 0)
        {
            error = "failed to query skeletal input file size";
            return false;
        }

        if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = "skeletal input file is too large";
            return false;
        }

        outBytes.resize(static_cast<size_t>(fileSize));
        input.seekg(0, std::ios::beg);
        if (!outBytes.empty())
        {
            input.read(reinterpret_cast<char*>(outBytes.data()), static_cast<std::streamsize>(outBytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(outBytes.size()))
            {
                error = "failed to read skeletal input file";
                return false;
            }
        }

        return true;
    }

    bool EnsureParentDirectory(const std::filesystem::path &path, std::string &error)
    {
        const std::filesystem::path parent = path.parent_path();
        if (parent.empty())
        {
            return true;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(parent, errorCode);
        if (errorCode)
        {
            error = "failed to create parent directory: " + parent.string();
            return false;
        }

        return true;
    }

    bool WriteBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes, std::string &error)
    {
        if (!EnsureParentDirectory(path, error))
        {
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            error = "failed to open output package: " + path.string();
            return false;
        }

        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        if (!output.good())
        {
            error = "failed to write output package: " + path.string();
            return false;
        }

        return true;
    }

    bool WriteTextFile(const std::filesystem::path &path, const std::string &text, std::string &error)
    {
        if (!EnsureParentDirectory(path, error))
        {
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            error = "failed to open output manifest: " + path.string();
            return false;
        }

        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output.good())
        {
            error = "failed to write output manifest: " + path.string();
            return false;
        }

        return true;
    }

    bool WriteSkeletalBinaryFile(const std::filesystem::path& path,
                                 const NorvesLib::Core::Container::VariableArray<uint8_t>& bytes,
                                 auto& error)
    {
        if (!EnsureParentDirectory(path, error))
        {
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            error = "failed to open skeletal output package";
            return false;
        }

        if (!bytes.empty())
        {
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        if (!output.good())
        {
            error = "failed to write skeletal output package";
            return false;
        }

        return true;
    }

    bool WriteSkeletalTextFile(const std::filesystem::path& path,
                               NorvesLib::Core::Container::AnsiStringView text,
                               auto& error)
    {
        if (!EnsureParentDirectory(path, error))
        {
            return false;
        }

        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
        {
            error = "failed to open skeletal output manifest";
            return false;
        }

        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output.good())
        {
            error = "failed to write skeletal output manifest";
            return false;
        }

        return true;
    }

    bool HasParentSegment(const std::filesystem::path &path)
    {
        for (const std::filesystem::path &part : path)
        {
            if (part == "..")
            {
                return true;
            }
        }

        return false;
    }

    bool MakeAbsolutePath(const std::filesystem::path &path, std::filesystem::path &outPath, std::string &error)
    {
        if (path.empty())
        {
            error = "path must not be empty";
            return false;
        }

        std::error_code errorCode;
        std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
        {
            error = "failed to make absolute path: " + path.string();
            return false;
        }

        outPath = absolutePath.lexically_normal();
        return true;
    }

    bool MakeCookedPackageManifestPath(const std::filesystem::path &packagePath,
                                       const std::filesystem::path &manifestParent,
                                       std::string &outPath,
                                       std::string &error)
    {
        const std::filesystem::path relativePath = packagePath.lexically_relative(manifestParent).lexically_normal();
        if (relativePath.empty() || relativePath == "." || relativePath.is_absolute() || HasParentSegment(relativePath))
        {
            error = "--out must be inside the manifest parent directory so cooked_package does not require ..";
            return false;
        }

        outPath = relativePath.generic_string();

        std::string normalizedCookedPackage;
        if (!NormalizeManifestPathField("cooked_package", outPath, normalizedCookedPackage, error))
        {
            return false;
        }

        if (normalizedCookedPackage != outPath)
        {
            error = "cooked_package must not rely on path normalization";
            return false;
        }

        return true;
    }

    bool MakeSkeletalCookedPackageManifestPath(const std::filesystem::path& packagePath,
                                               const std::filesystem::path& manifestParent,
                                               NorvesLib::Core::Container::AnsiString& outPath,
                                               auto& error)
    {
        const std::filesystem::path relativePath = packagePath.lexically_relative(manifestParent).lexically_normal();
        if (relativePath.empty() || relativePath == "." || relativePath.is_absolute() || HasParentSegment(relativePath))
        {
            error = "--out must be inside the manifest parent directory so cooked_package does not require ..";
            return false;
        }

        outPath = NorvesLib::Core::Container::AnsiString(relativePath.generic_string().c_str());
        if (!NormalizeSkeletalManifestPath(outPath, outPath, error))
        {
            return false;
        }

        return true;
    }

    struct SkeletalManifestMetadata
    {
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t JointCount = 0;
        uint32_t ClipCount = 0;
    };

    bool BuildManifestJson(const std::string &logicalPath,
                           const std::string &kind,
                           uint64_t sourceHash,
                           const std::string &variant,
                           const std::string &format,
                           const std::string &cookedPackage,
                           const std::string &entryName,
                           const std::string &entryTypeText,
                           uint64_t cookedHash,
                           std::string &outJson,
                           std::string &error)
    {
        const std::pair<const char *, const std::string *> fields[] = {
            {"logical_path", &logicalPath},
            {"kind", &kind},
            {"variant", &variant},
            {"format", &format},
            {"cooked_package", &cookedPackage},
            {"entry_name", &entryName},
            {"entry_type", &entryTypeText},
        };

        for (const auto &[name, value] : fields)
        {
            if (!ValidateAsciiJsonField(name, *value, error))
            {
                return false;
            }
        }

        outJson.clear();
        outJson += "{\n";
        outJson += "  \"version\":1,\n";
        outJson += "  \"assets\":[\n";
        outJson += "    {\n";
        outJson += "      ";
        AppendJsonStringField(outJson, "logical_path", logicalPath, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "kind", kind, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "source_hash", ToStdString(FormatAssetHashHex(sourceHash)), true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "variant", variant, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "format", format, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "cooked_package", cookedPackage, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "entry_name", entryName, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "entry_type", entryTypeText, true);
        outJson += "\n      ";
        AppendJsonStringField(outJson, "cooked_hash", ToStdString(FormatAssetHashHex(cookedHash)), true);
        outJson += "\n      \"cooked_version\":0\n";
        outJson += "    }\n";
        outJson += "  ]\n";
        outJson += "}\n";
        return true;
    }

    void AppendSkeletalJsonStringField(NorvesLib::Core::Container::AnsiString& json,
                                       const char* name,
                                       NorvesLib::Core::Container::AnsiStringView value,
                                       bool bTrailingComma)
    {
        json += "\"";
        json += name;
        json += "\":\"";
        for (const char character : value)
        {
            if (character == '\\' || character == '\"')
            {
                json += '\\';
            }
            json += character;
        }
        json += "\"";
        if (bTrailingComma)
        {
            json += ",";
        }
    }

    void AppendSkeletalJsonUInt32Field(NorvesLib::Core::Container::AnsiString& json,
                                       const char* name,
                                       uint32_t value,
                                       bool bTrailingComma)
    {
        char digits[16] = {};
        const auto conversion = std::to_chars(digits, digits + sizeof(digits), value);
        json += "\"";
        json += name;
        json += "\":";
        json.append(digits, static_cast<size_t>(conversion.ptr - digits));
        if (bTrailingComma)
        {
            json += ",";
        }
    }

    void SetSkeletalStatusError(auto& error, const char* prefix, uint32_t status)
    {
        char digits[16] = {};
        const auto conversion = std::to_chars(digits, digits + sizeof(digits), status);
        error = prefix;
        error.append(digits, static_cast<size_t>(conversion.ptr - digits));
    }

    bool BuildSkeletalManifestJson(NorvesLib::Core::Container::AnsiStringView logicalPath,
                                   uint64_t sourceHash,
                                   NorvesLib::Core::Container::AnsiStringView variant,
                                   NorvesLib::Core::Container::AnsiStringView format,
                                   NorvesLib::Core::Container::AnsiStringView cookedPackage,
                                   NorvesLib::Core::Container::AnsiStringView entryName,
                                   const SkeletalManifestMetadata& metadata,
                                   uint64_t cookedHash,
                                   NorvesLib::Core::Container::AnsiString& outJson,
                                   auto& error)
    {
        if (!ValidateSkeletalAsciiField(logicalPath, error) ||
            !ValidateSkeletalAsciiField(variant, error) ||
            !ValidateSkeletalAsciiField(format, error) ||
            !ValidateSkeletalAsciiField(cookedPackage, error) ||
            !ValidateSkeletalAsciiField(entryName, error))
        {
            return false;
        }

        const NorvesLib::Core::Container::AnsiString sourceHashText = FormatAssetHashHex(sourceHash);
        const NorvesLib::Core::Container::AnsiString cookedHashText = FormatAssetHashHex(cookedHash);
        outJson.clear();
        outJson += "{\n  \"version\":1,\n  \"assets\":[\n    {\n      ";
        AppendSkeletalJsonStringField(outJson, "logical_path", logicalPath, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "kind", "model", true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "source_hash", sourceHashText, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "variant", variant, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "format", format, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "cooked_package", cookedPackage, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "entry_name", entryName, true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "entry_type", "Skl0", true);
        outJson += "\n      ";
        AppendSkeletalJsonStringField(outJson, "cooked_hash", cookedHashText, true);
        outJson += "\n      \"metadata\":{\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "vertex_count", metadata.VertexCount, true);
        outJson += "\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "index_count", metadata.IndexCount, true);
        outJson += "\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "joint_count", metadata.JointCount, true);
        outJson += "\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "clip_count", metadata.ClipCount, false);
        outJson += "\n      },\n      \"cooked_version\":0\n    }\n  ]\n}\n";
        return true;
    }

    bool HasSameManifestKey(const NorvesLib::Core::Asset::AssetCookedReference& left,
                            const NorvesLib::Core::Asset::AssetCookedReference& right)
    {
        return left.LogicalPath == right.LogicalPath &&
               left.Kind == right.Kind &&
               left.Variant == right.Variant;
    }

    bool BuildMergedAudioManifestJson(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Asset::AssetCookedReference& audioReference,
        NorvesLib::Core::Container::AnsiString& outJson,
        auto& error)
    {
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Asset::AssetCookedReference> references;
        std::error_code existsError;
        const bool bManifestExists = std::filesystem::exists(manifestPath, existsError);
        if (existsError)
        {
            error = "failed to inspect existing manifest";
            return false;
        }
        if (bManifestExists)
        {
            NorvesLib::Core::Container::VariableArray<uint8_t> manifestBytes;
            if (!ReadSkeletalBinaryFile(manifestPath, manifestBytes, error))
            {
                return false;
            }
            NorvesLib::Core::Container::AnsiString manifestText;
            manifestText.append(reinterpret_cast<const char*>(manifestBytes.data()), manifestBytes.size());
            NorvesLib::Core::Asset::AssetManifest manifest;
            const NorvesLib::Core::Container::AnsiString sourceName(manifestPath.generic_string().c_str());
            if (!manifest.LoadFromJsonText(ToCoreString(manifestText), sourceName))
            {
                error = "existing manifest is invalid: " + ToStdString(manifest.GetParseError());
                return false;
            }
            references.reserve(manifest.GetReferenceCount() + 1);
            for (size_t index = 0; index < manifest.GetReferenceCount(); ++index)
            {
                const auto& reference = manifest.GetReference(index);
                if (!HasSameManifestKey(reference, audioReference))
                {
                    references.push_back(reference);
                }
            }
        }
        references.push_back(audioReference);
        std::sort(references.begin(), references.end(), [](const auto& left, const auto& right)
        {
            if (left.LogicalPath != right.LogicalPath)
            {
                return left.LogicalPath < right.LogicalPath;
            }
            const auto leftKind = NorvesLib::Core::Asset::GetAssetKindName(left.Kind);
            const auto rightKind = NorvesLib::Core::Asset::GetAssetKindName(right.Kind);
            if (leftKind != rightKind)
            {
                return leftKind < rightKind;
            }
            return left.Variant < right.Variant;
        });

        outJson = "{\n  \"version\":1,\n  \"assets\":[\n";
        for (size_t index = 0; index < references.size(); ++index)
        {
            const auto& reference = references[index];
            outJson += "    {\n      ";
            AppendSkeletalJsonStringField(outJson, "logical_path", reference.LogicalPath, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "kind", NorvesLib::Core::Asset::GetAssetKindName(reference.Kind), true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "source_hash", reference.SourceHashHex, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "variant", reference.Variant, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "format", reference.Format, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "cooked_package", reference.CookedPackage, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "entry_name", reference.EntryName, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "entry_type", reference.EntryTypeText, true);
            outJson += "\n      ";
            AppendSkeletalJsonStringField(outJson, "cooked_hash", reference.CookedHashHex, true);
            outJson += "\n      ";
            if (reference.bHasSkeletalMetadata)
            {
                outJson += "\"metadata\":{\n        ";
                AppendSkeletalJsonUInt32Field(
                    outJson, "vertex_count", reference.SkeletalMetadata.VertexCount, true);
                outJson += "\n        ";
                AppendSkeletalJsonUInt32Field(
                    outJson, "index_count", reference.SkeletalMetadata.IndexCount, true);
                outJson += "\n        ";
                AppendSkeletalJsonUInt32Field(
                    outJson, "joint_count", reference.SkeletalMetadata.JointCount, true);
                outJson += "\n        ";
                AppendSkeletalJsonUInt32Field(
                    outJson, "clip_count", reference.SkeletalMetadata.ClipCount, false);
                outJson += "\n      },\n      ";
            }
            AppendSkeletalJsonUInt32Field(outJson, "cooked_version", reference.CookedVersion, false);
            outJson += "\n    }";
            outJson += index + 1 < references.size() ? ",\n" : "\n";
        }
        outJson += "  ]\n}\n";
        return true;
    }

    bool CompareBytes(const uint8_t *actualData, size_t actualSize, const std::vector<uint8_t> &expected)
    {
        if (actualSize != expected.size())
        {
            return false;
        }

        if (expected.empty())
        {
            return true;
        }

        return actualData != nullptr && std::memcmp(actualData, expected.data(), expected.size()) == 0;
    }

    bool CompareSkeletalBytes(
        const uint8_t* actualData,
        size_t actualSize,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expected)
    {
        if (actualSize != expected.size())
        {
            return false;
        }

        if (expected.empty())
        {
            return true;
        }

        return actualData != nullptr && std::memcmp(actualData, expected.data(), expected.size()) == 0;
    }

    bool ValidatePackageOutput(const std::filesystem::path &packagePath,
                               const std::string &entryName,
                               AssetPackageFourCC entryType,
                               const std::vector<uint8_t> &expectedPayload,
                               std::string &error)
    {
        std::vector<uint8_t> packageBytes;
        if (!ReadBinaryFile(packagePath, packageBytes, error))
        {
            return false;
        }

        NorvesLib::FileStream::Package package;
        const NorvesLib::Core::Container::Span<const uint8_t> packageSpan(packageBytes.data(), packageBytes.size());
        if (!package.LoadFromMemory(packageSpan))
        {
            error = "self-validation failed: package parse failed";
            return false;
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(NorvesLib::Core::Container::AnsiString(entryName), entryType, entry))
        {
            error = "self-validation failed: package entry missing";
            return false;
        }

        const NorvesLib::Core::Asset::AssetBlob blob = package.OpenEntry(entry);
        if (!blob.IsValid() || !CompareBytes(blob.GetData(), blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: package entry bytes mismatch";
            return false;
        }

        return true;
    }

    bool ValidateCookedTexturePayload(const std::vector<uint8_t> &expectedPayload, std::string &error)
    {
        const NorvesLib::Core::Container::Span<const uint8_t> span(expectedPayload.data(), expectedPayload.size());
        const NorvesLib::Core::Asset::CookedTextureParseResult result =
            ParseCookedTexture(AssetBlob::CopyBytes(span, "AssetCook self-validation"));
        if (!result.Succeeded())
        {
            error = "self-validation failed: cooked texture parse failed: status=" +
                    std::to_string(static_cast<int>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateCookedTexturePackageOutput(const std::filesystem::path &packagePath,
                                            const std::string &entryName,
                                            AssetPackageFourCC entryType,
                                            const std::vector<uint8_t> &expectedPayload,
                                            std::string &error)
    {
        std::vector<uint8_t> packageBytes;
        if (!ReadBinaryFile(packagePath, packageBytes, error))
        {
            return false;
        }

        NorvesLib::FileStream::Package package;
        const NorvesLib::Core::Container::Span<const uint8_t> packageSpan(packageBytes.data(), packageBytes.size());
        if (!package.LoadFromMemory(packageSpan))
        {
            error = "self-validation failed: package parse failed";
            return false;
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(NorvesLib::Core::Container::AnsiString(entryName), entryType, entry))
        {
            error = "self-validation failed: package entry missing";
            return false;
        }

        const AssetBlob blob = package.OpenEntry(entry);
        if (!blob.IsValid() || !CompareBytes(blob.GetData(), blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: package entry bytes mismatch";
            return false;
        }

        const NorvesLib::Core::Asset::CookedTextureParseResult result = ParseCookedTexture(blob);
        if (!result.Succeeded())
        {
            error = "self-validation failed: package cooked texture parse failed: status=" +
                    std::to_string(static_cast<int>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateCookedMeshPayload(const std::vector<uint8_t>& expectedPayload, std::string& error)
    {
        const NorvesLib::Core::Container::Span<const uint8_t> span(expectedPayload.data(), expectedPayload.size());
        const NorvesLib::Core::Asset::CookedMeshParseResult result =
            ParseCookedMesh(AssetBlob::CopyBytes(span, "AssetCook mesh self-validation"));
        if (!result.Succeeded())
        {
            error = "self-validation failed: cooked mesh parse failed: status=" +
                    std::to_string(static_cast<int>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateCookedMeshPackageOutput(const std::filesystem::path& packagePath,
                                         const std::string& entryName,
                                         AssetPackageFourCC entryType,
                                         const std::vector<uint8_t>& expectedPayload,
                                         std::string& error)
    {
        std::vector<uint8_t> packageBytes;
        if (!ReadBinaryFile(packagePath, packageBytes, error))
        {
            return false;
        }

        NorvesLib::FileStream::Package package;
        const NorvesLib::Core::Container::Span<const uint8_t> packageSpan(packageBytes.data(), packageBytes.size());
        if (!package.LoadFromMemory(packageSpan))
        {
            error = "self-validation failed: mesh package parse failed";
            return false;
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(NorvesLib::Core::Container::AnsiString(entryName), entryType, entry))
        {
            error = "self-validation failed: mesh package entry name or type mismatch";
            return false;
        }

        const uint64_t expectedHash = ComputeAssetPackagePayloadHash(expectedPayload.data(), expectedPayload.size());
        if (entry.PayloadHash != expectedHash)
        {
            error = "self-validation failed: mesh package entry hash mismatch";
            return false;
        }

        const AssetBlob blob = package.OpenEntry(entry);
        if (!blob.IsValid() || !CompareBytes(blob.GetData(), blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: mesh package entry bytes mismatch";
            return false;
        }

        const NorvesLib::Core::Asset::CookedMeshParseResult result = ParseCookedMesh(blob);
        if (!result.Succeeded())
        {
            error = "self-validation failed: package cooked mesh parse failed: status=" +
                    std::to_string(static_cast<int>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateCookedSkeletalPayload(const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
                                       auto& error)
    {
        const NorvesLib::Core::Container::Span<const uint8_t> span(expectedPayload.data(), expectedPayload.size());
        const NorvesLib::Core::Asset::CookedSkeletalParseResult result =
            ParseCookedSkeletal(AssetBlob::CopyBytes(span, "AssetCook skeletal self-validation"));
        if (!result.Succeeded())
        {
            SetSkeletalStatusError(
                error,
                "self-validation failed: cooked skeletal parse failed: status=",
                static_cast<uint32_t>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateCookedSkeletalPackageOutput(const std::filesystem::path& packagePath,
                                             const auto& entryName,
                                             AssetPackageFourCC entryType,
                                             const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
                                             auto& error)
    {
        NorvesLib::Core::Container::VariableArray<uint8_t> packageBytes;
        if (!ReadSkeletalBinaryFile(packagePath, packageBytes, error))
        {
            return false;
        }

        NorvesLib::FileStream::Package package;
        const NorvesLib::Core::Container::Span<const uint8_t> packageSpan(packageBytes.data(), packageBytes.size());
        if (!package.LoadFromMemory(packageSpan))
        {
            error = "self-validation failed: skeletal package parse failed";
            return false;
        }

        NorvesLib::FileStream::PackageEntry entry;
        if (!package.FindEntry(NorvesLib::Core::Container::AnsiString(entryName), entryType, entry))
        {
            error = "self-validation failed: skeletal package entry name or type mismatch";
            return false;
        }

        const uint64_t expectedHash = ComputeAssetPackagePayloadHash(expectedPayload.data(), expectedPayload.size());
        if (entry.PayloadHash != expectedHash)
        {
            error = "self-validation failed: skeletal package entry hash mismatch";
            return false;
        }

        const AssetBlob blob = package.OpenEntry(entry);
        if (!blob.IsValid() || !CompareSkeletalBytes(blob.GetData(), blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: skeletal package entry bytes mismatch";
            return false;
        }

        const NorvesLib::Core::Asset::CookedSkeletalParseResult result = ParseCookedSkeletal(blob);
        if (!result.Succeeded())
        {
            SetSkeletalStatusError(
                error,
                "self-validation failed: package cooked skeletal parse failed: status=",
                static_cast<uint32_t>(result.Status));
            return false;
        }

        return true;
    }

    bool ValidateManifestOutput(const std::filesystem::path &manifestPath,
                                const std::string &manifestJson,
                                std::string &error)
    {
        NorvesLib::Core::Asset::AssetManifest manifest;
        const std::string sourceName = manifestPath.generic_string();
        if (!manifest.LoadFromJsonText(ToCoreString(manifestJson), sourceName.c_str()))
        {
            error = "self-validation failed: manifest parse failed: " + ToStdString(manifest.GetParseError());
            return false;
        }

        return true;
    }

    bool ValidateAssetSystemOutput(const std::filesystem::path &manifestPath,
                                   const std::string &manifestJson,
                                   const std::string &logicalPath,
                                   AssetKind kind,
                                   const std::string &variant,
                                   const std::vector<uint8_t> &expectedPayload,
                                   std::string &error)
    {
        const std::filesystem::path manifestParent = manifestPath.parent_path();
        AssetSystem system{NorvesLib::Core::Container::AnsiString(manifestParent.generic_string())};
        const std::string sourceName = manifestPath.generic_string();
        if (!system.LoadManifestFromJsonText(ToCoreString(manifestJson), sourceName.c_str()))
        {
            error = "self-validation failed: AssetSystem manifest load failed";
            return false;
        }

        const NorvesLib::Core::Asset::AssetResolveResult result =
            system.ResolveAsset(logicalPath.c_str(), kind, variant.c_str());
        if (!result.Succeeded() || result.Status != AssetResolveStatus::SuccessCooked || !result.UsedCooked())
        {
            error = "self-validation failed: AssetSystem cooked resolve failed";
            if (!result.Reason.empty())
            {
                error += ": ";
                error += ToStdString(result.Reason);
            }
            return false;
        }

        if (!CompareBytes(result.Blob.GetData(), result.Blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: AssetSystem resolved bytes mismatch";
            return false;
        }

        return true;
    }

    bool ValidateSkeletalManifestOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        auto& error)
    {
        NorvesLib::Core::Asset::AssetManifest manifest;
        const NorvesLib::Core::Container::AnsiString sourceName(manifestPath.generic_string().c_str());
        if (!manifest.LoadFromJsonText(ToCoreString(manifestJson), sourceName))
        {
            error = "self-validation failed: skeletal manifest parse failed";
            return false;
        }

        return true;
    }

    bool ValidateSkeletalAssetSystemOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        const NorvesLib::Core::Container::AnsiString& logicalPath,
        const NorvesLib::Core::Container::AnsiString& variant,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
        auto& error)
    {
        const std::filesystem::path manifestParent = manifestPath.parent_path();
        AssetSystem system{NorvesLib::Core::Container::AnsiString(manifestParent.generic_string().c_str())};
        const NorvesLib::Core::Container::AnsiString sourceName(manifestPath.generic_string().c_str());
        if (!system.LoadManifestFromJsonText(ToCoreString(manifestJson), sourceName))
        {
            error = "self-validation failed: skeletal AssetSystem manifest load failed";
            return false;
        }

        const NorvesLib::Core::Asset::AssetResolveResult result =
            system.ResolveAsset(logicalPath, AssetKind::Model, variant);
        if (!result.Succeeded() || result.Status != AssetResolveStatus::SuccessCooked || !result.UsedCooked())
        {
            error = "self-validation failed: skeletal AssetSystem cooked resolve failed";
            return false;
        }

        if (!CompareSkeletalBytes(result.Blob.GetData(), result.Blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: skeletal AssetSystem resolved bytes mismatch";
            return false;
        }

        return true;
    }

    bool ValidateAudioAssetSystemOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        const NorvesLib::Core::Container::AnsiString& logicalPath,
        const NorvesLib::Core::Container::AnsiString& variant,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
        auto& error)
    {
        const std::filesystem::path manifestParent = manifestPath.parent_path();
        AssetSystem system{NorvesLib::Core::Container::AnsiString(manifestParent.generic_string().c_str())};
        const NorvesLib::Core::Container::AnsiString sourceName(manifestPath.generic_string().c_str());
        if (!system.LoadManifestFromJsonText(ToCoreString(manifestJson), sourceName))
        {
            error = "self-validation failed: audio AssetSystem manifest load failed";
            return false;
        }
        const auto result = system.ResolveAsset(logicalPath, AssetKind::Audio, variant);
        if (!result.Succeeded() || result.Status != AssetResolveStatus::SuccessCooked || !result.UsedCooked())
        {
            error = "self-validation failed: audio AssetSystem cooked resolve failed";
            return false;
        }
        if (!CompareSkeletalBytes(result.Blob.GetData(), result.Blob.GetSize(), expectedPayload))
        {
            error = "self-validation failed: audio AssetSystem resolved bytes mismatch";
            return false;
        }
        const auto parsed = ParseCookedAudio(result.Blob);
        if (!parsed.Succeeded())
        {
            error = "self-validation failed: resolved cooked audio parse failed";
            return false;
        }
        return true;
    }

    bool ReadNextValue(int argc, char **argv, int &index, std::string &outValue, std::string &error)
    {
        if (index + 1 >= argc)
        {
            error = std::string("missing value for ") + argv[index];
            return false;
        }

        ++index;
        outValue = argv[index];
        return true;
    }

    bool ParseCommandLine(int argc, char **argv, CookOptions &outOptions, std::string &error)
    {
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            error.clear();
            return false;
        }

        for (int index = 1; index < argc; ++index)
        {
            std::string argument = argv[index];
            std::string value;
            const size_t equals = argument.find('=');
            if (equals != std::string::npos)
            {
                value = argument.substr(equals + 1);
                argument = argument.substr(0, equals);
            }

            auto readValue = [&]() -> bool
            {
                if (equals != std::string::npos)
                {
                    return true;
                }

                return ReadNextValue(argc, argv, index, value, error);
            };

            if (argument == "--input")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.InputPath = value;
            }
            else if (argument == "--out")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.PackagePath = value;
            }
            else if (argument == "--manifest")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.ManifestPath = value;
            }
            else if (argument == "--logical")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.LogicalPath = value;
            }
            else if (argument == "--kind")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Kind = value;
            }
            else if (argument == "--entry")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.EntryName = value;
            }
            else if (argument == "--entry-type")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.EntryTypeText = value;
            }
            else if (argument == "--format")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Format = value;
            }
            else if (argument == "--variant")
            {
                if (!readValue())
                {
                    return false;
                }
                outOptions.Variant = value;
            }
            else
            {
                error = "unknown argument: " + argument;
                return false;
            }
        }

        if (outOptions.InputPath.empty() ||
            outOptions.PackagePath.empty() ||
            outOptions.ManifestPath.empty() ||
            outOptions.LogicalPath.empty() ||
            outOptions.Kind.empty() ||
            outOptions.EntryName.empty() ||
            outOptions.EntryTypeText.empty() ||
            outOptions.Format.empty() ||
            outOptions.Variant.empty())
        {
            error = "missing required arguments";
            return false;
        }

        if (outOptions.Kind == "raw")
        {
            if (outOptions.Format != "raw.v0")
            {
                error = "--kind raw requires --format raw.v0";
                return false;
            }
        }
        else if (outOptions.Kind == "texture")
        {
            if (!NorvesLib::Tools::AssetCook::IsSupportedTextureCookFormat(outOptions.Format))
            {
                error = "unsupported texture --format";
                return false;
            }

            if (outOptions.EntryTypeText != "Tex0")
            {
                error = "--kind texture requires --entry-type Tex0";
                return false;
            }
        }
        else if (outOptions.Kind == "model")
        {
            const bool bStaticMeshFormat = NorvesLib::Tools::AssetCook::IsSupportedMeshCookFormat(outOptions.Format);
            const bool bSkeletalFormat = NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(outOptions.Format);
            if (!bStaticMeshFormat && !bSkeletalFormat)
            {
                error = "--kind model requires a supported nvmesh or nvskel --format";
                return false;
            }

            if (bStaticMeshFormat && outOptions.EntryTypeText != "Msh0")
            {
                error = "--kind model requires --entry-type Msh0";
                return false;
            }

            if (bSkeletalFormat && outOptions.EntryTypeText != "Skl0")
            {
                error = "--kind model with skeletal --format requires --entry-type Skl0";
                return false;
            }
        }
        else if (outOptions.Kind == "audio")
        {
            if (!NorvesLib::Tools::AssetCook::IsSupportedAudioCookFormat(outOptions.Format))
            {
                error = "unsupported audio --format";
                return false;
            }
            if (outOptions.EntryTypeText != "Aud0")
            {
                error = "--kind audio requires --entry-type Aud0";
                return false;
            }
        }
        else
        {
            error = "--kind must be raw, texture, model, or audio";
            return false;
        }

        return true;
    }

    void PrintUsage()
    {
        std::cerr
            << "Usage: AssetCook --input <file> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind raw --entry <entry> --entry-type Raw "
            << "--format raw.v0 --variant default\n"
            << "       AssetCook --input <image> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind texture --entry <entry.nvtex> --entry-type Tex0 "
            << "--format nvtex.v0.rgba8.srgb|nvtex.v0.rgba8.linear|nvtex.v0.rg8.linear|nvtex.v0.r8.linear "
            << "--variant default\n"
            << "       AssetCook --input <model.gltf> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind model --entry <entry.nvmesh> --entry-type Msh0 "
            << "--format nvmesh.v0.mesh3d.pnt.u32.clustered "
            << "--variant default\n"
            << "       AssetCook --input <model.gltf> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind model --entry <entry.nvskel> --entry-type Skl0 "
            << "--format nvskel.v0.skinned.pnujiw.u32 "
            << "--variant default\n"
            << "       AssetCook --input <audio.wav> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind audio --entry <entry.nvaud> --entry-type Aud0 "
            << "--format nvaud.v0.pcm16 --variant default\n";
    }

    bool CookRawAsset(const CookOptions &options, std::string &error)
    {
        std::filesystem::path inputPath;
        std::filesystem::path packagePath;
        std::filesystem::path manifestPath;
        if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
            !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
            !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
        {
            return false;
        }

        std::vector<uint8_t> inputBytes;
        if (!ReadBinaryFile(inputPath, inputBytes, error))
        {
            return false;
        }

        std::string logicalPath;
        std::string entryName;
        if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
            !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
            !ValidateAsciiJsonField("variant", options.Variant, error) ||
            !ValidateAsciiJsonField("format", options.Format, error))
        {
            return false;
        }

        AssetPackageFourCC entryType = 0;
        std::string entryTypeText;
        if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
        {
            return false;
        }

        const std::filesystem::path manifestParent = manifestPath.parent_path();
        std::string cookedPackagePath;
        if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
        {
            return false;
        }

        uint64_t payloadHash = 0;
        std::vector<uint8_t> packageBytes;
        if (!BuildSingleEntryPackage(entryName, entryType, inputBytes, packageBytes, payloadHash, error))
        {
            return false;
        }

        std::string manifestJson;
        if (!BuildManifestJson(logicalPath,
                               options.Kind,
                               payloadHash,
                               options.Variant,
                               options.Format,
                               cookedPackagePath,
                               entryName,
                               entryTypeText,
                               payloadHash,
                               manifestJson,
                               error))
        {
            return false;
        }

        if (!WriteBinaryFile(packagePath, packageBytes, error) ||
            !WriteTextFile(manifestPath, manifestJson, error))
        {
            return false;
        }

        if (!ValidatePackageOutput(packagePath, entryName, entryType, inputBytes, error) ||
            !ValidateManifestOutput(manifestPath, manifestJson, error) ||
            !ValidateAssetSystemOutput(manifestPath, manifestJson, logicalPath, AssetKind::Raw, options.Variant, inputBytes, error))
        {
            return false;
        }

        std::cerr << "AssetCook wrote package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" bytes=" << inputBytes.size() << "\n";
        return true;
    }

    bool CookTextureAsset(const CookOptions &options, std::string &error)
    {
        std::filesystem::path inputPath;
        std::filesystem::path packagePath;
        std::filesystem::path manifestPath;
        if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
            !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
            !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
        {
            return false;
        }

        std::vector<uint8_t> inputBytes;
        if (!ReadBinaryFile(inputPath, inputBytes, error))
        {
            return false;
        }

        std::string logicalPath;
        std::string entryName;
        if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
            !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
            !ValidateAsciiJsonField("variant", options.Variant, error) ||
            !ValidateAsciiJsonField("format", options.Format, error))
        {
            return false;
        }

        AssetPackageFourCC entryType = 0;
        std::string entryTypeText;
        if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
        {
            return false;
        }

        if (entryTypeText != "Tex0")
        {
            error = "--kind texture requires --entry-type Tex0";
            return false;
        }

        NorvesLib::Tools::AssetCook::TextureCookResult textureResult;
        if (!NorvesLib::Tools::AssetCook::CookTextureToNvtex(inputBytes.data(),
                                                             inputBytes.size(),
                                                             options.Format,
                                                             inputPath.generic_string(),
                                                             textureResult,
                                                             error))
        {
            return false;
        }

        if (!ValidateCookedTexturePayload(textureResult.NvtexBytes, error))
        {
            return false;
        }

        const std::filesystem::path manifestParent = manifestPath.parent_path();
        std::string cookedPackagePath;
        if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
        {
            return false;
        }

        uint64_t cookedHash = 0;
        std::vector<uint8_t> packageBytes;
        if (!BuildSingleEntryPackage(entryName, entryType, textureResult.NvtexBytes, packageBytes, cookedHash, error))
        {
            return false;
        }

        const uint64_t sourceHash = ComputeAssetPackagePayloadHash(inputBytes.data(), inputBytes.size());
        std::string manifestJson;
        if (!BuildManifestJson(logicalPath,
                               options.Kind,
                               sourceHash,
                               options.Variant,
                               options.Format,
                               cookedPackagePath,
                               entryName,
                               entryTypeText,
                               cookedHash,
                               manifestJson,
                               error))
        {
            return false;
        }

        if (!WriteBinaryFile(packagePath, packageBytes, error) ||
            !WriteTextFile(manifestPath, manifestJson, error))
        {
            return false;
        }

        if (!ValidateCookedTexturePackageOutput(packagePath, entryName, entryType, textureResult.NvtexBytes, error) ||
            !ValidateManifestOutput(manifestPath, manifestJson, error) ||
            !ValidateAssetSystemOutput(manifestPath,
                                       manifestJson,
                                       logicalPath,
                                       AssetKind::Texture,
                                       options.Variant,
                                       textureResult.NvtexBytes,
                                       error))
        {
            return false;
        }

        std::cerr << "AssetCook wrote texture package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" source_bytes=" << inputBytes.size()
                  << " nvtex_bytes=" << textureResult.NvtexBytes.size()
                  << " width=" << textureResult.Width
                  << " height=" << textureResult.Height
                  << " mips=" << textureResult.MipCount
                  << " bytes_per_pixel=" << textureResult.BytesPerPixel
                  << "\n";
        return true;
    }

    bool CookModelAsset(const CookOptions& options, std::string& error)
    {
        std::filesystem::path inputPath;
        std::filesystem::path packagePath;
        std::filesystem::path manifestPath;
        if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
            !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
            !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
        {
            return false;
        }

        std::vector<uint8_t> inputBytes;
        if (!ReadBinaryFile(inputPath, inputBytes, error))
        {
            return false;
        }

        std::string logicalPath;
        std::string entryName;
        if (!NormalizeManifestPathField("logical_path", options.LogicalPath, logicalPath, error) ||
            !NormalizeManifestPathField("entry_name", options.EntryName, entryName, error) ||
            !ValidateAsciiJsonField("variant", options.Variant, error) ||
            !ValidateAsciiJsonField("format", options.Format, error))
        {
            return false;
        }

        AssetPackageFourCC entryType = 0;
        std::string entryTypeText;
        if (!ParseEntryType(options.EntryTypeText, entryType, entryTypeText, error))
        {
            return false;
        }
        if (entryTypeText != "Msh0")
        {
            error = "--kind model requires --entry-type Msh0";
            return false;
        }

        NorvesLib::Tools::AssetCook::MeshCookResult meshResult;
        NorvesLib::Core::Container::AnsiString meshError;
        if (!NorvesLib::Tools::AssetCook::CookGltfToNvmesh(inputBytes.data(),
                                                           inputBytes.size(),
                                                           options.Format,
                                                           inputPath.generic_string(),
                                                           logicalPath,
                                                           meshResult,
                                                           meshError))
        {
            error = ToStdString(meshError);
            return false;
        }

        // Single conversion at the package boundary: MeshCooker exposes NorvesLib containers,
        // the package/manifest writers below still take std::vector payloads.
        const std::vector<uint8_t> nvmeshBytes(meshResult.NvmeshBytes.begin(), meshResult.NvmeshBytes.end());
        if (!ValidateCookedMeshPayload(nvmeshBytes, error))
        {
            return false;
        }

        const std::filesystem::path manifestParent = manifestPath.parent_path();
        std::string cookedPackagePath;
        if (!MakeCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
        {
            return false;
        }

        uint64_t cookedHash = 0;
        std::vector<uint8_t> packageBytes;
        if (!BuildSingleEntryPackage(entryName,
                                     entryType,
                                     nvmeshBytes,
                                     packageBytes,
                                     cookedHash,
                                     error))
        {
            return false;
        }

        // The mesh source hash covers the glTF JSON and every external buffer it loaded,
        // not just the JSON bytes handed to this process.
        const uint64_t sourceHash = meshResult.SourceHash;
        std::string manifestJson;
        if (!BuildManifestJson(logicalPath,
                               options.Kind,
                               sourceHash,
                               options.Variant,
                               options.Format,
                               cookedPackagePath,
                               entryName,
                               entryTypeText,
                               cookedHash,
                               manifestJson,
                               error))
        {
            return false;
        }

        if (!WriteBinaryFile(packagePath, packageBytes, error) ||
            !WriteTextFile(manifestPath, manifestJson, error))
        {
            return false;
        }

        if (!ValidateCookedMeshPackageOutput(packagePath,
                                             entryName,
                                             entryType,
                                             nvmeshBytes,
                                             error) ||
            !ValidateManifestOutput(manifestPath, manifestJson, error) ||
            !ValidateAssetSystemOutput(manifestPath,
                                       manifestJson,
                                       logicalPath,
                                       AssetKind::Model,
                                       options.Variant,
                                       nvmeshBytes,
                                       error))
        {
            return false;
        }

        std::cerr << "AssetCook wrote model package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" source_bytes=" << inputBytes.size()
                  << " nvmesh_bytes=" << meshResult.NvmeshBytes.size()
                  << " vertices=" << meshResult.VertexCount
                  << " indices=" << meshResult.IndexCount
                  << " clusters=" << meshResult.ClusterCount
                  << "\n";
        return true;
    }

    bool CookSkeletalModelAsset(const CookOptions& options, auto& error)
    {
        std::filesystem::path inputPath;
        std::filesystem::path packagePath;
        std::filesystem::path manifestPath;
        if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
            !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
            !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
        {
            return false;
        }

        NorvesLib::Core::Container::VariableArray<uint8_t> inputBytes;
        if (!ReadSkeletalBinaryFile(inputPath, inputBytes, error))
        {
            return false;
        }

        const NorvesLib::Core::Container::AnsiString sourceLogicalPath(options.LogicalPath.c_str());
        const NorvesLib::Core::Container::AnsiString sourceEntryName(options.EntryName.c_str());
        const NorvesLib::Core::Container::AnsiString variant(options.Variant.c_str());
        const NorvesLib::Core::Container::AnsiString format(options.Format.c_str());
        NorvesLib::Core::Container::AnsiString logicalPath;
        NorvesLib::Core::Container::AnsiString entryName;
        if (!NormalizeSkeletalManifestPath(sourceLogicalPath, logicalPath, error) ||
            !NormalizeSkeletalManifestPath(sourceEntryName, entryName, error) ||
            !ValidateSkeletalAsciiField(variant, error) ||
            !ValidateSkeletalAsciiField(format, error))
        {
            return false;
        }

        if (options.EntryTypeText != "Skl0")
        {
            error = "--kind model with skeletal --format requires --entry-type Skl0";
            return false;
        }
        const AssetPackageFourCC entryType = NorvesLib::Core::Asset::CookedSkeletalFormatV0::EntryType;

        NorvesLib::Tools::AssetCook::SkeletalCookResult skeletalResult;
        NorvesLib::Core::Container::AnsiString skeletalError;
        const NorvesLib::Core::Container::AnsiString sourcePath(inputPath.generic_string().c_str());
        if (!NorvesLib::Tools::AssetCook::CookGltfToNvskel(inputBytes.data(),
                                                           inputBytes.size(),
                                                           format,
                                                           sourcePath,
                                                           skeletalResult,
                                                           skeletalError))
        {
            error.assign(skeletalError.data(), skeletalError.size());
            return false;
        }

        if (!ValidateCookedSkeletalPayload(skeletalResult.NvskelBytes, error))
        {
            return false;
        }

        const std::filesystem::path manifestParent = manifestPath.parent_path();
        NorvesLib::Core::Container::AnsiString cookedPackagePath;
        if (!MakeSkeletalCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
        {
            return false;
        }

        uint64_t cookedHash = 0;
        NorvesLib::Core::Container::VariableArray<uint8_t> packageBytes;
        if (!BuildSingleSkeletalEntryPackage(entryName,
                                             entryType,
                                             skeletalResult.NvskelBytes,
                                             packageBytes,
                                             cookedHash,
                                             error))
        {
            return false;
        }

        const SkeletalManifestMetadata metadata{
            .VertexCount = skeletalResult.VertexCount,
            .IndexCount = skeletalResult.IndexCount,
            .JointCount = skeletalResult.JointCount,
            .ClipCount = skeletalResult.ClipCount,
        };
        NorvesLib::Core::Container::AnsiString manifestJson;
        if (!BuildSkeletalManifestJson(logicalPath,
                               skeletalResult.SourceHash,
                               variant,
                               format,
                               cookedPackagePath,
                               entryName,
                               metadata,
                               cookedHash,
                               manifestJson,
                               error))
        {
            return false;
        }

        if (!WriteSkeletalBinaryFile(packagePath, packageBytes, error) ||
            !WriteSkeletalTextFile(manifestPath, manifestJson, error))
        {
            return false;
        }

        if (!ValidateCookedSkeletalPackageOutput(packagePath,
                                                 entryName,
                                                 entryType,
                                                 skeletalResult.NvskelBytes,
                                                 error) ||
            !ValidateSkeletalManifestOutput(manifestPath, manifestJson, error) ||
            !ValidateSkeletalAssetSystemOutput(manifestPath,
                                               manifestJson,
                                               logicalPath,
                                               variant,
                                               skeletalResult.NvskelBytes,
                                               error))
        {
            return false;
        }

        std::cerr << "AssetCook wrote skeletal model package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" source_bytes=" << inputBytes.size()
                  << " nvskel_bytes=" << skeletalResult.NvskelBytes.size()
                  << " vertices=" << skeletalResult.VertexCount
                  << " indices=" << skeletalResult.IndexCount
                  << " joints=" << skeletalResult.JointCount
                  << " clips=" << skeletalResult.ClipCount
                  << "\n";
        return true;
    }

    bool CookAudioAsset(const CookOptions& options, auto& error)
    {
        std::filesystem::path inputPath;
        std::filesystem::path packagePath;
        std::filesystem::path manifestPath;
        if (!MakeAbsolutePath(options.InputPath, inputPath, error) ||
            !MakeAbsolutePath(options.PackagePath, packagePath, error) ||
            !MakeAbsolutePath(options.ManifestPath, manifestPath, error))
        {
            return false;
        }

        NorvesLib::Core::Container::VariableArray<uint8_t> inputBytes;
        if (!ReadSkeletalBinaryFile(inputPath, inputBytes, error))
        {
            return false;
        }

        const NorvesLib::Core::Container::AnsiString sourceLogicalPath(options.LogicalPath.c_str());
        const NorvesLib::Core::Container::AnsiString sourceEntryName(options.EntryName.c_str());
        const NorvesLib::Core::Container::AnsiString variant(options.Variant.c_str());
        const NorvesLib::Core::Container::AnsiString format(options.Format.c_str());
        NorvesLib::Core::Container::AnsiString logicalPath;
        NorvesLib::Core::Container::AnsiString entryName;
        if (!NormalizeSkeletalManifestPath(sourceLogicalPath, logicalPath, error) ||
            !NormalizeSkeletalManifestPath(sourceEntryName, entryName, error) ||
            !ValidateSkeletalAsciiField(variant, error) ||
            !ValidateSkeletalAsciiField(format, error))
        {
            return false;
        }

        NorvesLib::Tools::AssetCook::AudioCookResult audioResult;
        NorvesLib::Core::Container::AnsiString audioError;
        if (!NorvesLib::Tools::AssetCook::CookWaveToNvaud(
                inputBytes.data(), inputBytes.size(), format, audioResult, audioError))
        {
            error.assign(audioError.data(), audioError.size());
            return false;
        }

        const std::filesystem::path manifestParent = manifestPath.parent_path();
        NorvesLib::Core::Container::AnsiString cookedPackagePath;
        if (!MakeSkeletalCookedPackageManifestPath(packagePath, manifestParent, cookedPackagePath, error))
        {
            return false;
        }

        uint64_t cookedHash = 0;
        NorvesLib::Core::Container::VariableArray<uint8_t> packageBytes;
        if (!BuildSingleSkeletalEntryPackage(
                entryName,
                NorvesLib::Core::Asset::CookedAudioFormatV0::EntryType,
                audioResult.NvaudBytes,
                packageBytes,
                cookedHash,
                error))
        {
            return false;
        }

        NorvesLib::Core::Asset::AssetCookedReference reference;
        reference.LogicalPath = logicalPath;
        reference.Kind = AssetKind::Audio;
        reference.SourceHash = audioResult.SourceHash;
        reference.SourceHashHex = FormatAssetHashHex(reference.SourceHash);
        reference.Variant = variant;
        reference.Format = format;
        reference.CookedPackage = cookedPackagePath;
        reference.EntryName = entryName;
        reference.EntryType = NorvesLib::Core::Asset::CookedAudioFormatV0::EntryType;
        reference.EntryTypeText = "Aud0";
        reference.CookedHash = cookedHash;
        reference.CookedHashHex = FormatAssetHashHex(cookedHash);
        reference.CookedVersion = 0;

        NorvesLib::Core::Container::AnsiString manifestJson;
        if (!BuildMergedAudioManifestJson(manifestPath, reference, manifestJson, error))
        {
            return false;
        }
        if (!WriteSkeletalBinaryFile(packagePath, packageBytes, error) ||
            !WriteSkeletalTextFile(manifestPath, manifestJson, error))
        {
            return false;
        }
        if (!ValidateSkeletalManifestOutput(manifestPath, manifestJson, error) ||
            !ValidateAudioAssetSystemOutput(
                manifestPath, manifestJson, logicalPath, variant, audioResult.NvaudBytes, error))
        {
            return false;
        }

        std::cerr << "AssetCook wrote audio package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" source_bytes=" << inputBytes.size()
                  << " nvaud_bytes=" << audioResult.NvaudBytes.size()
                  << " sample_rate=" << audioResult.SampleRate
                  << " channels=" << audioResult.ChannelCount
                  << " frames=" << audioResult.FrameCount
                  << "\n";
        return true;
    }
}

int main(int argc, char **argv)
{
    CookOptions options;
    std::string error;
    if (!ParseCommandLine(argc, argv, options, error))
    {
        PrintUsage();
        if (!error.empty())
        {
            std::cerr << "AssetCook error: " << error << "\n";
        }
        return error.empty() ? 0 : 1;
    }

    bool bSucceeded = false;
    if (options.Kind == "raw")
    {
        bSucceeded = CookRawAsset(options, error);
    }
    else if (options.Kind == "texture")
    {
        bSucceeded = CookTextureAsset(options, error);
    }
    else if (options.Kind == "model")
    {
        if (NorvesLib::Tools::AssetCook::IsSupportedSkeletalCookFormat(options.Format))
        {
            bSucceeded = CookSkeletalModelAsset(options, error);
        }
        else
        {
            bSucceeded = CookModelAsset(options, error);
        }
    }
    else if (options.Kind == "audio")
    {
        bSucceeded = CookAudioAsset(options, error);
    }
    if (!bSucceeded)
    {
        std::cerr << "AssetCook error: " << error << "\n";
        return 1;
    }

    return 0;
}

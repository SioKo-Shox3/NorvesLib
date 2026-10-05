// 単体cookのpackage/manifest/IO/自己検証。bodyのbyte生成規則は従来通り。
#include "AssetCookOutput.h"
#include "NativeCookPath.h"

namespace NorvesLib::Tools::AssetCook::Detail
{
    std::string DescribeCookPath(const std::filesystem::path& path, bool bGenericSeparators)
    {
        NorvesLib::Core::Container::AnsiString text;
        if (!EncodeCookPathUtf8(path, text, bGenericSeparators))
        {
            return "<invalid-unicode-path>";
        }
        return ToStdString(text);
    }

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

    bool ValidateSkeletalAsciiField(NorvesLib::Core::Container::AnsiStringView value, std::string& error)
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
                                       std::string& error)
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
        std::string& error)
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
            error = "failed to open input file: " + DescribeCookPath(path, false);
            return false;
        }

        input.seekg(0, std::ios::end);
        const std::streamoff fileSize = input.tellg();
        if (fileSize < 0)
        {
            error = "failed to query input file size: " + DescribeCookPath(path, false);
            return false;
        }

        if (static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            static_cast<uint64_t>(fileSize) > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        {
            error = "input file is too large: " + DescribeCookPath(path, false);
            return false;
        }

        outBytes.resize(static_cast<size_t>(fileSize));
        input.seekg(0, std::ios::beg);
        if (!outBytes.empty())
        {
            input.read(reinterpret_cast<char *>(outBytes.data()), static_cast<std::streamsize>(outBytes.size()));
            if (input.gcount() != static_cast<std::streamsize>(outBytes.size()))
            {
                error = "failed to read input file: " + DescribeCookPath(path, false);
                return false;
            }
        }

        return true;
    }

    bool ReadSkeletalBinaryFile(const std::filesystem::path& path,
                                NorvesLib::Core::Container::VariableArray<uint8_t>& outBytes,
                                std::string& error)
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
                                 std::string& error)
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

        output.flush();
        if (!output.good())
        {
            error = "failed to write skeletal output package";
            return false;
        }

        return true;
    }

    bool WriteSkeletalTextFile(const std::filesystem::path& path,
                               NorvesLib::Core::Container::AnsiStringView text,
                               std::string& error)
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
        output.flush();
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

        if (!NorvesLib::Core::Gltf::IsValidNativeSourcePath(path))
        {
            error = "file path contains invalid Unicode or NUL";
            return false;
        }

        std::error_code errorCode;
        std::filesystem::path absolutePath = std::filesystem::absolute(path, errorCode);
        if (errorCode)
        {
            error = "failed to make absolute path: " + DescribeCookPath(path, false);
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
                                               std::string& error)
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
        outJson += format == "nvmesh.v1.mesh3d.pnt.u32.clustered" ? "\n      \"cooked_version\":1\n"
                                                                  : "\n      \"cooked_version\":0\n";
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

    void SetSkeletalStatusError(std::string& error, const char* prefix, uint32_t status)
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
                                   std::string& error)
    {
        if (!NorvesLib::Core::Skeletal::IsValidSkeletalTableMetadata(
                metadata.SubmeshCount, metadata.MaterialSlotCount, metadata.IndexCount))
        {
            error = "\xe9\xaa\xa8\xe6\xa0\xbc\xe3\x81\xae\xe8\xa1\xa8\xe6\x95\xb0\xe9\x87\x8f\x6d\x65\x74\x61\x64\x61\x74\x61\xe3\x81\x8c\xe4\xb8\x8d\xe6\xad\xa3\xe3\x81\xa7\xe3\x81\x99";
            return false;
        }
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
        AppendSkeletalJsonUInt32Field(outJson, "clip_count", metadata.ClipCount, true);
        outJson += "\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "submesh_count", metadata.SubmeshCount, true);
        outJson += "\n        ";
        AppendSkeletalJsonUInt32Field(outJson, "material_slot_count", metadata.MaterialSlotCount, false);
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

    bool BuildMergedManifestJson(
        const std::filesystem::path& manifestPath,
        NorvesLib::Core::Container::Span<const NorvesLib::Core::Asset::AssetCookedReference> incoming,
        NorvesLib::Core::Container::AnsiString& outJson,
        std::string& error)
    {
        if (incoming.empty() || incoming.data() == nullptr)
        {
            error = "manifest update requires asset references";
            return false;
        }
        for (size_t index = 0; index < incoming.size(); ++index)
        {
            for (size_t previous = 0; previous < index; ++previous)
            {
                if (HasSameManifestKey(incoming[index], incoming[previous]))
                {
                    error = "manifest update contains duplicate asset keys";
                    return false;
                }
            }
        }
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
            if (incoming.size() > std::numeric_limits<size_t>::max() - manifest.GetReferenceCount())
            {
                error = "manifest reference count overflow";
                return false;
            }
            references.reserve(manifest.GetReferenceCount() + incoming.size());
            for (size_t index = 0; index < manifest.GetReferenceCount(); ++index)
            {
                const auto& reference = manifest.GetReference(index);
                const bool bReplaced = std::any_of(incoming.begin(), incoming.end(), [&](const auto& replacement)
                {
                    return HasSameManifestKey(reference, replacement);
                });
                if (!bReplaced)
                {
                    references.push_back(reference);
                }
            }
        }
        for (const auto& reference : incoming)
        {
            references.push_back(reference);
        }
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
                    outJson, "clip_count", reference.SkeletalMetadata.ClipCount, reference.SkeletalMetadata.bHasSubmeshCounts);
                if (reference.SkeletalMetadata.bHasSubmeshCounts)
                {
                    outJson += "\n        ";
                    AppendSkeletalJsonUInt32Field(outJson, "submesh_count", reference.SkeletalMetadata.SubmeshCount, true);
                    outJson += "\n        ";
                    AppendSkeletalJsonUInt32Field(outJson, "material_slot_count", reference.SkeletalMetadata.MaterialSlotCount, false);
                }
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
                                       std::string& error)
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
                                             const NorvesLib::Core::Container::AnsiString& entryName,
                                             AssetPackageFourCC entryType,
                                             const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
                                             std::string& error)
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
        std::string& error)
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
        std::string& error)
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
        std::string& error)
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

}

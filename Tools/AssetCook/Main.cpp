#include "Asset/AssetManifest.h"
#include "Asset/AssetPackageFormat.h"
#include "Asset/AssetPath.h"
#include "Asset/AssetSystem.h"
#include "Container/Span.h"
#include "FileStream/Package.h"

#include <algorithm>
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
    using NorvesLib::Core::Asset::AssetPackageCompression;
    using NorvesLib::Core::Asset::AssetPackageFourCC;
    using NorvesLib::Core::Asset::AssetPath;
    using NorvesLib::Core::Asset::AssetResolveStatus;
    using NorvesLib::Core::Asset::AssetSystem;
    using NorvesLib::Core::Asset::ComputeAssetPackagePayloadHash;
    using NorvesLib::Core::Asset::FormatAssetHashHex;
    using NorvesLib::Core::Asset::FormatAssetPackageFourCCText;
    using NorvesLib::Core::Asset::MakeAssetPackageFourCC;
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
            system.ResolveAsset(logicalPath.c_str(), AssetKind::Raw, variant.c_str());
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

        if (outOptions.Kind != "raw")
        {
            error = "only --kind raw is supported in Phase 8";
            return false;
        }

        if (outOptions.Format != "raw.v0")
        {
            error = "only --format raw.v0 is supported in Phase 8";
            return false;
        }

        return true;
    }

    void PrintUsage()
    {
        std::cerr
            << "Usage: AssetCook --input <file> --out <package> --manifest <manifest.json> "
            << "--logical <path> --kind raw --entry <entry> --entry-type Raw "
            << "--format raw.v0 --variant default\n";
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
            !ValidateAssetSystemOutput(manifestPath, manifestJson, logicalPath, options.Variant, inputBytes, error))
        {
            return false;
        }

        std::cout << "AssetCook wrote package=\"" << packagePath.generic_string()
                  << "\" manifest=\"" << manifestPath.generic_string()
                  << "\" bytes=" << inputBytes.size() << "\n";
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

    if (!CookRawAsset(options, error))
    {
        std::cerr << "AssetCook error: " << error << "\n";
        return 1;
    }

    return 0;
}

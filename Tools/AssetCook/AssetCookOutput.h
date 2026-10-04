#pragma once
// 既存出力処理のTU間宣言だけを持つprivate互換header。std型は旧実装のまま移動する。
#include "MeshCooker.h"
#include "ImportCliOptions.h"
#include "SkeletalCliOptions.h"
#include "ModelCookCache.h"
#include "ModelInspection.h"
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
#include "Resource/GltfImageSource.h"
#include "Resource/SkeletalLimits.h"
#include "FileStream/Package.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <tchar.h>
#if defined(_WIN32)
#include <Windows.h>
#endif

namespace NorvesLib::Tools::AssetCook::Detail
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

    struct SkeletalManifestMetadata
    {
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t JointCount = 0;
        uint32_t ClipCount = 0;
        uint32_t SubmeshCount = 0;
        uint32_t MaterialSlotCount = 0;
    };

    std::string ToStdString(const NorvesLib::Core::Container::AnsiString &value);

    NorvesLib::Core::Container::String ToCoreString(const std::string &value);

    NorvesLib::Core::Container::String ToCoreString(NorvesLib::Core::Container::AnsiStringView value);

    bool ValidateAsciiJsonField(std::string_view fieldName, std::string_view value, std::string &error);

    bool NormalizeManifestPathField(std::string_view fieldName,
                                    const std::string &value,
                                    std::string &outValue,
                                    std::string &error);

    bool ValidateSkeletalAsciiField(NorvesLib::Core::Container::AnsiStringView value, std::string& error);

    bool NormalizeSkeletalManifestPath(NorvesLib::Core::Container::AnsiStringView value,
                                       NorvesLib::Core::Container::AnsiString& outValue,
                                       std::string& error);

    bool ParseEntryType(const std::string &text,
                        AssetPackageFourCC &outType,
                        std::string &outManifestText,
                        std::string &error);

    bool BuildSingleEntryPackage(const std::string &entryName,
                                 AssetPackageFourCC entryType,
                                 const std::vector<uint8_t> &payload,
                                 std::vector<uint8_t> &outBytes,
                                 uint64_t &outPayloadHash,
                                 std::string &error);

    bool BuildSingleSkeletalEntryPackage(
        NorvesLib::Core::Container::AnsiStringView entryName,
        AssetPackageFourCC entryType,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& payload,
        NorvesLib::Core::Container::VariableArray<uint8_t>& outBytes,
        uint64_t& outPayloadHash,
        std::string& error);

    bool ReadBinaryFile(const std::filesystem::path &path, std::vector<uint8_t> &outBytes, std::string &error);

    bool ReadSkeletalBinaryFile(const std::filesystem::path& path,
                                NorvesLib::Core::Container::VariableArray<uint8_t>& outBytes,
                                std::string& error);

    bool WriteBinaryFile(const std::filesystem::path &path, const std::vector<uint8_t> &bytes, std::string &error);

    bool WriteTextFile(const std::filesystem::path &path, const std::string &text, std::string &error);

    bool WriteSkeletalBinaryFile(const std::filesystem::path& path,
                                 const NorvesLib::Core::Container::VariableArray<uint8_t>& bytes,
                                 std::string& error);

    bool WriteSkeletalTextFile(const std::filesystem::path& path,
                               NorvesLib::Core::Container::AnsiStringView text,
                               std::string& error);

    bool MakeAbsolutePath(const std::filesystem::path &path, std::filesystem::path &outPath, std::string &error);

    bool MakeCookedPackageManifestPath(const std::filesystem::path &packagePath,
                                       const std::filesystem::path &manifestParent,
                                       std::string &outPath,
                                       std::string &error);

    bool MakeSkeletalCookedPackageManifestPath(const std::filesystem::path& packagePath,
                                               const std::filesystem::path& manifestParent,
                                               NorvesLib::Core::Container::AnsiString& outPath,
                                               std::string& error);

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
                           std::string &error);

    bool BuildSkeletalManifestJson(NorvesLib::Core::Container::AnsiStringView logicalPath,
                                   uint64_t sourceHash,
                                   NorvesLib::Core::Container::AnsiStringView variant,
                                   NorvesLib::Core::Container::AnsiStringView format,
                                   NorvesLib::Core::Container::AnsiStringView cookedPackage,
                                   NorvesLib::Core::Container::AnsiStringView entryName,
                                   const SkeletalManifestMetadata& metadata,
                                   uint64_t cookedHash,
                                   NorvesLib::Core::Container::AnsiString& outJson,
                                   std::string& error);

    bool HasSameManifestKey(const NorvesLib::Core::Asset::AssetCookedReference& left,
                            const NorvesLib::Core::Asset::AssetCookedReference& right);

    bool BuildMergedManifestJson(
        const std::filesystem::path& manifestPath,
        NorvesLib::Core::Container::Span<const NorvesLib::Core::Asset::AssetCookedReference> incoming,
        NorvesLib::Core::Container::AnsiString& outJson,
        std::string& error);

    bool CompareSkeletalBytes(
        const uint8_t* actualData,
        size_t actualSize,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expected);

    bool ValidatePackageOutput(const std::filesystem::path &packagePath,
                               const std::string &entryName,
                               AssetPackageFourCC entryType,
                               const std::vector<uint8_t> &expectedPayload,
                               std::string &error);

    bool ValidateCookedTexturePayload(const std::vector<uint8_t> &expectedPayload, std::string &error);

    bool ValidateCookedTexturePackageOutput(const std::filesystem::path &packagePath,
                                            const std::string &entryName,
                                            AssetPackageFourCC entryType,
                                            const std::vector<uint8_t> &expectedPayload,
                                            std::string &error);

    bool ValidateCookedMeshPayload(const std::vector<uint8_t>& expectedPayload, std::string& error);

    bool ValidateCookedMeshPackageOutput(const std::filesystem::path& packagePath,
                                         const std::string& entryName,
                                         AssetPackageFourCC entryType,
                                         const std::vector<uint8_t>& expectedPayload,
                                         std::string& error);

    bool ValidateCookedSkeletalPayload(const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
                                       std::string& error);

    bool ValidateCookedSkeletalPackageOutput(const std::filesystem::path& packagePath,
                                             const NorvesLib::Core::Container::AnsiString& entryName,
                                             AssetPackageFourCC entryType,
                                             const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
                                             std::string& error);

    bool ValidateManifestOutput(const std::filesystem::path &manifestPath,
                                const std::string &manifestJson,
                                std::string &error);

    bool ValidateAssetSystemOutput(const std::filesystem::path &manifestPath,
                                   const std::string &manifestJson,
                                   const std::string &logicalPath,
                                   AssetKind kind,
                                   const std::string &variant,
                                   const std::vector<uint8_t> &expectedPayload,
                                   std::string &error);

    bool ValidateSkeletalManifestOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        std::string& error);

    bool ValidateSkeletalAssetSystemOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        const NorvesLib::Core::Container::AnsiString& logicalPath,
        const NorvesLib::Core::Container::AnsiString& variant,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
        std::string& error);

    bool ValidateAudioAssetSystemOutput(
        const std::filesystem::path& manifestPath,
        const NorvesLib::Core::Container::AnsiString& manifestJson,
        const NorvesLib::Core::Container::AnsiString& logicalPath,
        const NorvesLib::Core::Container::AnsiString& variant,
        const NorvesLib::Core::Container::VariableArray<uint8_t>& expectedPayload,
        std::string& error);
}

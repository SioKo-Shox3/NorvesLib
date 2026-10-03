#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Resource/SkeletalImportOptions.h"
#include "SkeletalImportReport.h"

#include <cstddef>
#include <cstdint>

namespace NorvesLib::Core::AssetImport
{
    struct ImportSettingsFileOptions;
}

namespace NorvesLib::Tools::AssetCook
{
    enum class MeshImageRole : uint8_t { Albedo = 1, Normal = 2, Arm = 4 };

    class MeshEmbeddedImage
    {
    public:
        MeshEmbeddedImage() = default;
        MeshEmbeddedImage(const MeshEmbeddedImage&) = default;
        MeshEmbeddedImage(MeshEmbeddedImage&&) noexcept = default;
        MeshEmbeddedImage& operator=(const MeshEmbeddedImage& other);
        MeshEmbeddedImage& operator=(MeshEmbeddedImage&&) noexcept = default;
        void Swap(MeshEmbeddedImage& other) noexcept;
        // 借用は元GLBだけに用いる。借用元はこの結果とGetBytesのviewより長く保持すること。
        // 所有へ切り替える場合は確保完了後に置換する。失敗入力では既存bytesを保持する。
        [[nodiscard]] bool SetBytes(Core::Container::Span<const uint8_t> bytes, bool bBorrow);
        Core::Container::Span<const uint8_t> GetBytes() const noexcept;
        bool IsBorrowed() const noexcept;

        uint32_t ImageIndex = 0;
        uint8_t Roles = 0;
        Core::Container::AnsiString LogicalPath;
        Core::Container::AnsiString Format;
        uint64_t SourceHash = 0;
    private:
        Core::Container::VariableArray<uint8_t> m_OwnedBytes;
        Core::Container::Span<const uint8_t> m_BorrowedBytes;
    };

    struct MeshCookResult
    {
        Core::Container::VariableArray<uint8_t> NvmeshBytes;
        Core::Container::VariableArray<MeshEmbeddedImage> EmbeddedImages;
        Core::Container::AnsiString ImportSettingsPath;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;
    };

    struct SkeletalCookResult
    {
        Core::Container::VariableArray<uint8_t> NvskelBytes;
        Core::Container::AnsiString ImportSettingsPath;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t JointCount = 0;
        uint32_t ClipCount = 0;
        Core::Skeletal::SkeletalGltfDecodeReport DecodeReport;
    };

    struct ModelImageFingerprint
    {
        uint32_t ImageIndex = 0;
        Core::Container::AnsiString LogicalPath;
        Core::Container::AnsiString Format;
        uint64_t SourceHash = 0;
    };
    struct ModelCookFingerprint
    {
        uint64_t SourceHash = 0;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        Core::Container::AnsiString ImportSettingsPath;
        Core::Container::VariableArray<ModelImageFingerprint> EmbeddedImages;
    };
    // 入力/設定のcache照合用。geometry検証・変換・画像decode・cookは行わない。
    // 全metadataを所有し、失敗時outを保持する。成功はmodel自体のcook可能性を保証しない。
    [[nodiscard]] bool FingerprintModelCookSource(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, Core::Container::AnsiStringView sourcePath,
        Core::Container::AnsiStringView logicalPath, ModelCookFingerprint& outResult,
        Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr);

    [[nodiscard]] bool IsSupportedMeshCookFormat(Core::Container::AnsiStringView format) noexcept;

    // 成功時のEmbeddedImagesはGLB画像だけsourceBytesを借用し、それ以外は結果が所有する。
    // GLB入力はoutResult自身が所有するstorageを参照しないこと。成功時にoutResultを置換する。
    [[nodiscard]] bool CookGltfToNvmesh(const uint8_t* sourceBytes,
                                        size_t sourceSize,
                                        Core::Container::AnsiStringView format,
                                        Core::Container::AnsiStringView sourcePath,
                                        Core::Container::AnsiStringView logicalPath,
                                        MeshCookResult& outResult,
                                        Core::Container::AnsiString& error,
                                        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr);

    [[nodiscard]] bool IsSupportedSkeletalCookFormat(Core::Container::AnsiStringView format) noexcept;

    [[nodiscard]] bool CookGltfToNvskel(const uint8_t* sourceBytes,
                                        size_t sourceSize,
                                        Core::Container::AnsiStringView format,
                                        Core::Container::AnsiStringView sourcePath,
                                        SkeletalCookResult& outResult,
                                        Core::Container::AnsiString& error,
                                        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
                                        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr,
                                        SkeletalCookDiagnostics* outDiagnostics = nullptr);
} // namespace NorvesLib::Tools::AssetCook

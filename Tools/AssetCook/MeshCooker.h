#pragma once

#include "Container/String.h"
#include "Container/StringView.h"
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Resource/SkeletalImportOptions.h"
#include "SkeletalImportReport.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace NorvesLib::Core::AssetImport
{
    struct ImportSettingsFileOptions;
}

namespace NorvesLib::Tools::AssetCook
{
    enum class MeshImageRole : uint8_t
    {
        Albedo = 1,
        Normal = 2,
        Arm = 4,
        Emissive = 8
    };
    enum class MeshImagePayload : uint8_t
    {
        Encoded,
        RawRgba8
    };

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
        [[nodiscard]] bool SetRawRgba8(Core::Container::Span<const uint8_t> bytes, uint32_t width, uint32_t height);
        Core::Container::Span<const uint8_t> GetBytes() const noexcept;
        bool IsBorrowed() const noexcept;

        uint64_t ImageIndex = 0;
        uint8_t Roles = 0;
        MeshImagePayload Payload = MeshImagePayload::Encoded;
        uint32_t Width = 0, Height = 0;
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
        uint32_t VersionMajor = 0;
        uint32_t DuplicateMaterialNameGroups = 0, FirstDuplicateMaterialIndex = UINT32_MAX,
                 SecondDuplicateMaterialIndex = UINT32_MAX;
        Core::Container::VariableArray<MeshEmbeddedImage> EmbeddedImages;
        std::filesystem::path ImportSettingsPath;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t ClusterCount = 0;
        // 書き出した NVMESH の主版(0 か 1)。1 のときだけ下の LOD の階層の項目に値が入る。
        uint32_t FormatMajor = 0;
        uint32_t LODLevelCount = 1;
        // LOD の階層の焼き込み(溶接・クラスタ化・簡略化・書き出し・自己検証)にかかった時間
        uint32_t DagMilliseconds = 0;
        // 安全な簡略化が見つからず、簡略化せずに残したグループの数(階層が粗くなりにくくなる。0 が望ましい)
        uint32_t DagRejectedGroups = 0;
        // NVMESH v1.1 のページの詰め方(FormatMajor が 1 のときだけ)。根のページ(常駐。複数)の数・大きさの合計と、
        // 最も大きいグループがページの中で占めるバイト数(ページの上限は根のページも含めて 128 KiB)
        uint32_t PageCount = 0;
        uint32_t RootPageCount = 0;
        uint32_t RootPageBytes = 0;
        uint32_t RootPageClusterCount = 0;
        uint32_t RootPageMinLODLevel = 0;
        uint32_t MaxPageBytes = 0;
        uint32_t LargestGroupBytes = 0;
    };

    struct SkeletalCookResult
    {
        Core::Container::VariableArray<uint8_t> NvskelBytes;
        std::filesystem::path ImportSettingsPath;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        uint64_t SourceHash = 0;
        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;
        uint32_t JointCount = 0;
        uint32_t ClipCount = 0;
        Core::Skeletal::SkeletalGltfDecodeReport DecodeReport;
        uint32_t SubmeshCount = 0;
        uint32_t MaterialSlotCount = 0;
    };

    struct ModelImageFingerprint
    {
        uint64_t ImageIndex = 0;
        Core::Container::AnsiString LogicalPath;
        Core::Container::AnsiString Format;
        uint64_t SourceHash = 0;
    };
    struct ModelCookFingerprint
    {
        uint32_t DuplicateMaterialNameGroups = 0, FirstDuplicateMaterialIndex = UINT32_MAX,
                 SecondDuplicateMaterialIndex = UINT32_MAX;
        uint64_t SourceHash = 0;
        uint64_t ImportSettingsHash = 0;
        bool bHasImportSettings = false;
        std::filesystem::path ImportSettingsPath;
        Core::Container::VariableArray<ModelImageFingerprint> EmbeddedImages;
    };
    // 入力/設定のcache照合用。v0/骨格は画像decodeをしない。明示v1はARM inventoryのため画像解析をする。
    // geometry変換・cluster化・NVMESH/NVTEX出力は行わない。
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
                                        Core::Container::AnsiStringView logicalPath, MeshCookResult &outResult,
                                        Core::Container::AnsiString &error,
                                        const Core::AssetImport::ImportSettingsFileOptions *importOptions = nullptr,
                                        uint32_t fallbackMinTriangles = 0);

    // 起動画面の大きな球（石畳の高さマップ cobblestone_floor_09_disp_4k.png で変位した緯度経度の球）を作り、
    // NVMESH v1 に焼く（--generate displaced-sphere）。heightMapBytes は 16 ビットのグレーの PNG の中身。
    // 球の仕様は Rendering/MegaGeometry/StartupBigSphereSpec.h（実行時の生成と共有）。
    [[nodiscard]] bool CookDisplacedSphereToNvmesh(const uint8_t* heightMapBytes,
                                                   size_t heightMapSize,
                                                   Core::Container::AnsiStringView format,
                                                   Core::Container::AnsiStringView logicalPath,
                                                   MeshCookResult& outResult,
                                                   Core::Container::AnsiString& error,
                                                   uint32_t fallbackMinTriangles = 0);

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

    // I/Oのsource/sidecar locatorをnativeで渡す。既存narrow入口はASCII互換のwrapperとして残す。
    [[nodiscard]] bool FingerprintModelCookSourceNativePath(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, const std::filesystem::path& sourcePath,
        Core::Container::AnsiStringView logicalPath, ModelCookFingerprint& outResult,
        Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr);
    [[nodiscard]] bool CookGltfToNvmeshNativePath(
        const uint8_t *sourceBytes, size_t sourceSize, Core::Container::AnsiStringView format,
        const std::filesystem::path &sourcePath, Core::Container::AnsiStringView logicalPath, MeshCookResult &outResult,
        Core::Container::AnsiString &error, const Core::AssetImport::ImportSettingsFileOptions *importOptions = nullptr,
        uint32_t fallbackMinTriangles = 0);
    [[nodiscard]] bool CookGltfToNvskelNativePath(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, const std::filesystem::path& sourcePath,
        SkeletalCookResult& outResult, Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr,
        SkeletalCookDiagnostics* outDiagnostics = nullptr);
} // namespace NorvesLib::Tools::AssetCook

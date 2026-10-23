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
                                        uint32_t fallbackMinTriangles = 0,
                                        uint32_t dagRootMaxClusters = 0);

    // 起動画面の大きな球（石畳の高さマップ cobblestone_floor_09_disp_4k.png で変位した緯度経度の球）を作り、
    // NVMESH v1 に焼く（--generate displaced-sphere）。heightMapBytes は 16 ビットのグレーの PNG の中身。
    // 球の仕様は Rendering/MegaGeometry/StartupBigSphereSpec.h（実行時の生成と共有）。
    [[nodiscard]] bool CookDisplacedSphereToNvmesh(const uint8_t* heightMapBytes,
                                                   size_t heightMapSize,
                                                   Core::Container::AnsiStringView format,
                                                   Core::Container::AnsiStringView logicalPath,
                                                   MeshCookResult& outResult,
                                                   Core::Container::AnsiString& error,
                                                   uint32_t fallbackMinTriangles = 0,
                                                   uint32_t dagRootMaxClusters = 0);

    // --generate の値 "terrain-tile:<x>,<z>"（荒野の手続きの地形の区画の番号）と "terrain-tile:dem:<x>,<z>"（DEM の格子の
    // 区画の番号。outDem が真）を読む。形が違う・番号が範囲の外なら false（DEM の区画の数は格子の記述で決まるので、
    // DEM の番号の範囲はクックのときに確かめる）。
    [[nodiscard]] bool TryParseTerrainTileGenerate(Core::Container::AnsiStringView generate, uint32_t& outTileX,
                                                   uint32_t& outTileZ, bool& outDem);

    // --generate の値が "terrain-tile:dem-plain:<x>,<z>"（崖の細部を焼かない DEM の区画。形の解釈は "dem:" と同じで、
    // 撮り比べで崖の手続きの変形を切った版を別の論理パスへ焼くためのもの）か
    [[nodiscard]] bool IsTerrainTileCliffDetailOffGenerate(Core::Container::AnsiStringView generate);

    // 荒野の地形の区画 (tileX, tileZ) を高さ場（Rendering/Terrain/TerrainHeightField.h。Game と共有）から作り、
    // NVMESH v1 に焼く（--generate terrain-tile:<x>,<z>）。外周の頂点は階層のどの段でも動かさない。
    [[nodiscard]] bool CookTerrainTileToNvmesh(uint32_t tileX,
                                               uint32_t tileZ,
                                               Core::Container::AnsiStringView format,
                                               Core::Container::AnsiStringView logicalPath,
                                               MeshCookResult& outResult,
                                               Core::Container::AnsiString& error,
                                               uint32_t fallbackMinTriangles = 0,
                                               uint32_t dagRootMaxClusters = 0);

    // 荒野の DEM の格子（Rendering/Terrain/TerrainHeightField.h の WastelandTerrain::kDemGridDescriptorPath。Game と同じ
    // ファイル）の区画 (tileX, tileZ) を NVMESH v1 に焼く（--generate terrain-tile:dem:<x>,<z>）。区画の数と格子の間隔は
    // 格子の記述から決まり、格子のファイルの SHA-256 が記述の sha256 と違えば失敗する。inputBytes は一覧の source_path
    // （格子の記述）の中身で、高さ場の読む記述と同じでなければならない。外周の頂点は階層のどの段でも動かさない。
    // bAllowCliffDetail が偽なら、崖の細部（TerrainCliffDetail.h の細分と押し出し）を近景にも遠景にも焼かない。
    [[nodiscard]] bool CookTerrainDemTileToNvmesh(const uint8_t* inputBytes,
                                                  size_t inputSize,
                                                  uint32_t tileX,
                                                  uint32_t tileZ,
                                                  Core::Container::AnsiStringView format,
                                                  Core::Container::AnsiStringView logicalPath,
                                                  MeshCookResult& outResult,
                                                  Core::Container::AnsiString& error,
                                                  uint32_t fallbackMinTriangles = 0,
                                                  uint32_t dagRootMaxClusters = 0,
                                                  bool bAllowCliffDetail = true);

    // DEM の隣り合う 2 区画（B は A の東か南の隣）をクックと同じ引数で LOD の階層まで焼き、境の頂点の位置・法線・UV が
    // 両区画でビット単位で同じで、どの段・根のクラスタの集まり・フォールバックの段でも境の開いた辺が元の 1 升目の辺のまま
    // （境の頂点が簡略化で動かない・消えない。境の 3 頂点で作る縦の折れの長い辺は 2 つの三角形が共有する）かを確かめる（AssetCook --check-terrain-dem-seam）。結果の行を outReport に
    // 書き、合否を outPass に入れる。確かめられない（格子を読めない・区画が隣でない）ときは false
    [[nodiscard]] bool CheckTerrainDemTileSeam(uint32_t tileAX,
                                               uint32_t tileAZ,
                                               uint32_t tileBX,
                                               uint32_t tileBZ,
                                               bool& outPass,
                                               Core::Container::AnsiString& outReport,
                                               Core::Container::AnsiString& error);

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
        uint32_t fallbackMinTriangles = 0, uint32_t dagRootMaxClusters = 0);
    [[nodiscard]] bool CookGltfToNvskelNativePath(const uint8_t* sourceBytes, size_t sourceSize,
        Core::Container::AnsiStringView format, const std::filesystem::path& sourcePath,
        SkeletalCookResult& outResult, Core::Container::AnsiString& error,
        const Core::AssetImport::ImportSettingsFileOptions* importOptions = nullptr,
        const Core::Skeletal::SkeletalGltfDecodeOptions* decodeOptions = nullptr,
        SkeletalCookDiagnostics* outDiagnostics = nullptr);
} // namespace NorvesLib::Tools::AssetCook

#pragma once
// split専用の入力/展開/出力予算。既存texture cookの既定を変更しない。
#include "MeshCooker.h"
#include "TextureCooker.h"
#include "Resource/RigGltfImportCapture.h"
namespace NorvesLib::Tools::AssetCook
{
    struct RigSplitImageLimits
    {
        uint64_t MaxEncodedBytes = 32ull * 1024 * 1024;
        uint64_t MaxTotalEncodedBytes = 128ull * 1024 * 1024;
        uint64_t MaxDecodedBytes = 64ull * 1024 * 1024;
        uint64_t MaxTotalDecodedBytes = 256ull * 1024 * 1024;
        uint64_t MaxDecoderWorkspaceBytes = 256ull * 1024 * 1024;
        uint64_t MaxOutputBytes = 128ull * 1024 * 1024;
        uint64_t MaxTotalOutputBytes = 256ull * 1024 * 1024;
        uint32_t MaxDimension = 4096;
        uint32_t MaxSourceImages = 256;
    };
    [[nodiscard]] bool IsValidRigSplitImageLimits(const RigSplitImageLimits&) noexcept;
    [[nodiscard]] bool DecodeRigSplitImage(Core::Container::Span<const uint8_t>, const RigSplitImageLimits&,
                                           DecodedTextureRgba8& out, Core::Container::AnsiString& error);
    [[nodiscard]] bool MeasureRigSplitTextureBytes(uint32_t width, uint32_t height, const RigSplitImageLimits&,
                                                   uint64_t& out) noexcept;
    struct RigSplitImageEntry
    {
        uint32_t Index = 0;
        std::filesystem::path CanonicalFile;
        MeshEmbeddedImage Encoded;
        DecodedTextureRgba8 Decoded;
        Core::Gltf::DataUriMime Mime = Core::Gltf::DataUriMime::Unknown;
    };
    struct RigSplitImageInputs
    {
        const Core::Skeletal::RigGltfImportCapture* Capture = nullptr;
        std::filesystem::path SourcePath;
        RigSplitImageLimits Limits;
        Core::Container::VariableArray<RigSplitImageEntry> Entries;
        uint64_t EncodedBytes = 0, DecodedBytes = 0, OutputBytes = 0, ReturnedCopyBytes = 0;
        [[nodiscard]] bool Read(uint32_t index, MeshEmbeddedImage&, DecodedTextureRgba8&, Core::Gltf::DataUriMime&,
                                Core::Container::AnsiString&);
        [[nodiscard]] bool ReserveCopies(uint64_t bytes, Core::Container::AnsiString&);
        [[nodiscard]] bool ReserveDerived(uint64_t bytes, uint32_t width, uint32_t height,
                                          Core::Container::AnsiString&);
        [[nodiscard]] bool ReserveOutput(uint64_t bytes, Core::Container::AnsiString&);
    };
} // namespace NorvesLib::Tools::AssetCook

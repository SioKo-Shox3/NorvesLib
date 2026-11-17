// インポスターの読み戻し画素と、PNG・NVTEX・復元用メタデータの書き出し。
#pragma once

#include "Rendering/ImpostorBake.h"
#include <filesystem>

namespace NorvesLib::Tools::ImpostorBaker
{
    struct ImpostorBakerPixels
    {
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Channels = 0;
        Core::Container::VariableArray<uint8_t> Bytes;
    };

    Core::Container::AnsiString ImpostorBakerPathUtf8(const std::filesystem::path& path);

    // 詰めた行の画素から PNG を作る。法線・ORM の呼び手は RGB に詰め替えて渡す。
    bool EncodeImpostorBakerPng(const ImpostorBakerPixels& pixels,
                               Core::Container::VariableArray<uint8_t>& png,
                               Core::Container::AnsiString& error);
    bool WriteImpostorBakerFile(const std::filesystem::path& path,
                               Core::Container::Span<const uint8_t> bytes,
                               Core::Container::AnsiString& error);
    bool CookImpostorBakerTexture(Core::Container::Span<const uint8_t> png, uint32_t atlasIndex,
                                 const std::filesystem::path& path, Core::Container::AnsiString& error);
    bool WriteImpostorBakerMetadata(const std::filesystem::path& path,
                                   const std::filesystem::path& source, const std::filesystem::path& name,
                                   const Core::Rendering::ImpostorBakeMetadata& metadata, bool bCooked,
                                   Core::Container::AnsiString& error);

    // 配列の順はアルベド・法線・ORM・深度。成功時は書き出した全ファイルのパスを返す。
    bool WriteImpostorBakerOutput(const std::filesystem::path& directory, const std::filesystem::path& name,
                                 const std::filesystem::path& source,
                                 const Core::Rendering::ImpostorBakeMetadata& metadata,
                                 const ImpostorBakerPixels (&pixels)[4], bool bNoCook,
                                 Core::Container::VariableArray<std::filesystem::path>& files,
                                 Core::Container::AnsiString& error);
} // namespace NorvesLib::Tools::ImpostorBaker

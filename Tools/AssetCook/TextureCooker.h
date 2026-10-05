#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include "Container/VariableArray.h"
#include "Container/Span.h"
#include "Container/String.h"
#include "Container/StringView.h"

namespace NorvesLib::Tools::AssetCook
{
    struct TextureCookResult
    {
        Core::Container::VariableArray<uint8_t> NvtexBytes;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipCount = 0;
        uint32_t BytesPerPixel = 0;
    };

    struct DecodedTextureRgba8
    {
        Core::Container::VariableArray<uint8_t> Pixels;
        uint32_t Width = 0, Height = 0;
    };
    inline constexpr size_t MaximumMaterialImageDecodedBytes = 512 * 1024 * 1024;
    // 色空間変換をせずRGBA8へ展開する。上限は最終pixelだけでstb内部workspaceを含まない。
    // input/formatとerrorは独立。成功時だけoutを更新する。
    [[nodiscard]] bool DecodeTextureRgba8(Core::Container::Span<const uint8_t> encoded, DecodedTextureRgba8& out,
                                          Core::Container::AnsiString& error);
    // 既存encoded入口と同じpixel選択・mip/wire生成を使う。raw bytesをPNGと偽らない。
    [[nodiscard]] bool CookRgba8ToNvtex(Core::Container::Span<const uint8_t> pixels, uint32_t width, uint32_t height,
                                        Core::Container::AnsiStringView format, TextureCookResult& out,
                                        Core::Container::AnsiString& error);

    [[nodiscard]] bool IsSupportedTextureCookFormat(std::string_view format) noexcept;

    [[nodiscard]] bool CookTextureToNvtex(const uint8_t *sourceBytes,
                                          size_t sourceSize,
                                          std::string_view format,
                                          std::string_view sourceName,
                                          TextureCookResult &outResult,
                                          std::string &error);
}

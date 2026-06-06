#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace NorvesLib::Tools::AssetCook
{
    struct TextureCookResult
    {
        std::vector<uint8_t> NvtexBytes;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipCount = 0;
        uint32_t BytesPerPixel = 0;
    };

    [[nodiscard]] bool IsSupportedTextureCookFormat(std::string_view format) noexcept;

    [[nodiscard]] bool CookTextureToNvtex(const uint8_t *sourceBytes,
                                          size_t sourceSize,
                                          std::string_view format,
                                          std::string_view sourceName,
                                          TextureCookResult &outResult,
                                          std::string &error);
}

#pragma once

#include "BlockCompressor.h"

#include "Container/String.h"
#include "Container/StringView.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include "Container/Span.h"

namespace NorvesLib::Tools::AssetCook
{
    struct TextureCookResult
    {
        ByteArray NvtexBytes;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t MipCount = 0;
        // 非圧縮の形式の 1 画素のバイト数。ブロック圧縮の形式は 0。
        uint32_t BytesPerPixel = 0;
        // 書き出した形式の名前(BC7・BC5・BC4・R16 など)。ログと検証用。
        ErrorString PixelFormatName;
    };

    // クッカーが用途ごとに焼く先。用途が形式・色空間・ミップの作り方を決める。
    enum class TextureUsage : uint8_t
    {
        Albedo,   // BC7 sRGB。ミップは線形空間の平均
        Normal,   // BC5 linear。入力は DirectX の向き。ミップは非正規化ベクトルの平均を再正規化する
        Orm,      // BC7 linear。AO・粗さ・メタリックを R・G・B に詰める（詰め済みの画像を source に渡してもよい）
        Single,   // BC4 linear。R の 1 チャンネル
        Height16, // R16 linear(非圧縮)。16bit の入力の精度を保つ
    };

    [[nodiscard]] bool ParseTextureUsage(Core::Container::AnsiStringView text, TextureUsage &outUsage) noexcept;
    [[nodiscard]] const char *GetTextureUsageName(TextureUsage usage) noexcept;
    // マニフェストの format 欄に書く名前(例: nvtex.v0.2.bc7.srgb)。
    [[nodiscard]] const char *GetTextureUsageManifestFormat(TextureUsage usage) noexcept;

    struct TextureSourceImage
    {
        const uint8_t *Bytes = nullptr;
        size_t Size = 0;
        // 入力の名前(エラー文用)。ビューにすると、渡した一時文字列が消えたあとに読んでしまうので所有する。
        ErrorString Name;

        [[nodiscard]] bool IsPresent() const noexcept { return Bytes != nullptr && Size != 0; }
    };

    // ORM の元画像。それぞれ R(グレースケールなら同じ値)を読む。無い枠は AO=1・粗さ=1・メタリック=0。
    struct OrmSourceImages
    {
        TextureSourceImage Ao;
        TextureSourceImage Roughness;
        TextureSourceImage Metallic;
    };

    struct TextureUsageCookParams
    {
        TextureUsage Usage = TextureUsage::Albedo;
        BlockQuality Quality = BlockQuality::Normal;
        // ブロック圧縮のスレッド数。0 はハードウェアの並列数。
        uint32_t ThreadCount = 0;
        // Normal のとき、入力の法線の Y（緑）を反転する（OpenGL の向きの入力を DirectX の向きへ直す）。
        bool bFlipNormalY = false;
    };

    // 用途に応じて元画像を焼く。Orm は source があればそれを詰め済みの ORM として、無ければ orm の 3 枠を読む。それ以外は source を読む。NVTEX v0.2(タイル配置)を書く。
    [[nodiscard]] bool CookTextureForUsage(const TextureSourceImage &source,
                                           const OrmSourceImages &orm,
                                           const TextureUsageCookParams &params,
                                           TextureCookResult &outResult,
                                           ErrorString &error);

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
                                          const ErrorString &sourceName,
                                          TextureCookResult &outResult,
                                          ErrorString &error);
    [[nodiscard]] bool CookTextureToNvtex(const uint8_t *sourceBytes, size_t sourceSize, std::string_view format,
                                          std::string_view sourceName, TextureCookResult &outResult,
                                          std::string &error);
}

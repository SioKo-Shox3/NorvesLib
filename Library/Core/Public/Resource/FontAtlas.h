#pragma once

#include "Asset/AssetFileReader.h"
#include "Container/Containers.h"
#include "Math/Vector2.h"
#include "Math/Vector4.h"
#include "Object/Resource.h"
#include "Rendering/RenderResourcesFwd.h"
#include "Rendering/RenderTypes.h"

namespace NorvesLib::Core
{
    class FontFaceCache;

    struct FontAtlasDesc
    {
        /** @brief Faces が空のときに使うフォントの論理パス。 */
        Container::AnsiString FontLogicalPath = "Fonts/Inter-Regular.ttf";
        /** @brief すべてのフォントに共通する、ラスタ化の高さの画素数。 */
        uint32_t PixelHeight = 32;
        /**
         * @brief アトラスの幅の画素数。
         * 0 の場合は 64 から MaxAtlasSize 以下まで2倍ずつ試し、詰めた高さが幅以下になる最初の幅を使う。
         */
        uint32_t AtlasWidth = 512;
        /**
         * @brief 焼くコードポイントの集合。
         * 空なら 0x20〜0x7E を使う。空でなければ整列・重複除去し、サロゲートと 0x10FFFF 超を除く。
         * さらに代替候補の '?'・U+25A1・U+FFFD を加える。
         */
        Container::VariableArray<uint32_t> Codepoints;
        /**
         * @brief 代替順に並べたフォントの論理パス。空なら FontLogicalPath を使う。
         * 各文字は最初に字形を持つフォントを選ぶ。1本でも読込または初期化に失敗すると焼きは失敗する。
         */
        Container::VariableArray<Container::AnsiString> Faces;
        /** @brief 字形の間隔と開始位置の画素数。 */
        uint32_t Padding = 1;
        /** @brief アトラスの幅と高さの上限。収まらなければ焼きは失敗する。 */
        uint32_t MaxAtlasSize = 4096;
    };

    struct FontAtlasGlyph
    {
        Math::Vector2 SizePx = Math::Vector2(0.0f, 0.0f);
        Math::Vector2 BearingPx = Math::Vector2(0.0f, 0.0f);
        float AdvancePx = 0.0f;
        Math::Vector4 UVRect = Math::Vector4(0.0f, 0.0f, 0.0f, 0.0f);
        bool bValid = false;
    };

    class FontAtlas final : public Resource
    {
        REFLECTION_CLASS(FontAtlas, Resource)

    public:
        FontAtlas();
        ~FontAtlas() override;

        /**
         * @brief FreeType で CPU の画素と字形表を作る。
         * GPU に触れずワーカーから呼べる。同じ FontAtlas への同時アクセスは呼び出し側で同期する。
         * faceCache が nullptr なら一時キャッシュを使う。成功・失敗を問わず試行は一度だけで、
         * 2回目以降は false を返す。
         */
        bool BuildCpu(const FontAtlasDesc& desc,
                      const Asset::AssetFileReader& reader,
                      FontFaceCache* faceCache = nullptr);
        /**
         * @brief CPU 生成済みで未登録の場合にテクスチャを登録する。
         * 前提条件を満たさない場合は false を返す。テクスチャの登録に失敗すると CPU の状態も消す。
         */
        bool Upload(Rendering::TextureResources& textures);
        /** @brief 一時キャッシュを使い、BuildCpu の後に Upload を同期的に行う。 */
        bool Build(const FontAtlasDesc& desc,
                   const Asset::AssetFileReader& reader,
                   Rendering::TextureResources& textures);
        /** @brief 指定したキャッシュを使い、BuildCpu の後に Upload を同期的に行う。 */
        bool Build(const FontAtlasDesc& desc,
                   const Asset::AssetFileReader& reader,
                   Rendering::TextureResources& textures,
                   FontFaceCache& faceCache);
        void Shutdown(Rendering::TextureResources& textures);

        bool Load() override;
        void Unload() override;
        /** @brief テクスチャの登録が済んでいれば true を返す。 */
        bool IsValid() const override;
        /** @brief CPU の画素と字形表が生成済みなら true を返す。 */
        bool IsCpuBuilt() const { return m_bCpuBuilt; }

        /**
         * @brief 指定したコードポイントの有効な字形を返す。
         * 無いか無効なら '?'・U+25A1・U+FFFD の順に有効な字形を選び、
         * どれも無ければ bValid が false の空の字形を返す。
         */
        const FontAtlasGlyph& GetGlyph(uint32_t codepoint) const;
        Rendering::TextureHandle GetTextureHandle() const { return m_Texture; }
        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        float GetLineAdvancePx() const { return m_LineAdvancePx; }
        const Container::VariableArray<uint8_t>& GetPixels() const { return m_Pixels; }
        bool HasBuildAttempted() const { return m_bBuildAttempted; }

    private:
        static constexpr uint32_t FirstCodepoint = 0x20;
        static constexpr uint32_t LastCodepoint = 0x7E;
        static constexpr uint32_t GlyphCount = LastCodepoint - FirstCodepoint + 1;

        const FontAtlasGlyph* FindGlyph(uint32_t codepoint) const;
        const FontAtlasGlyph& GetFallbackGlyph() const;
        void ClearCpuState();

        Container::VariableArray<uint32_t> m_Codepoints;
        Container::VariableArray<FontAtlasGlyph> m_Glyphs;
        Container::VariableArray<uint8_t> m_Pixels;
        Rendering::TextureHandle m_Texture = Rendering::TextureHandle::Invalid();
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        float m_LineAdvancePx = 0.0f;
        bool m_bBuildAttempted = false;
        bool m_bCpuBuilt = false;
        bool m_bBuilt = false;
    };
} // namespace NorvesLib::Core

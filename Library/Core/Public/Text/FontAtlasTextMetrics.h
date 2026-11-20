// フォントアトラスの字形と全フェイスの基準線を組版に提供する。
#pragma once

#include "Container/PointerTypes.h"
#include "Text/TextMetrics.h"

namespace NorvesLib::Core
{
    namespace Asset
    {
        class AssetFileReader;
    }
    class FontAtlas;
    struct FontAtlasDesc;
    class FontFaceCache;

    class FontAtlasTextMetrics final : public ITextMetrics
    {
    public:
        ~FontAtlasTextMetrics() override = default;

        /// CPU 生成済みのアトラスと、その生成に使った設定を結び付ける。失敗時は前の状態を保つ。
        bool Bind(Container::TSharedPtr<FontAtlas> atlas, const FontAtlasDesc& desc,
                  const Asset::AssetFileReader& reader, FontFaceCache* faceCache = nullptr);
        /// アトラスを手放し、寸法と世代番号を 0 に戻す。
        void Unbind();

        TextGlyphMetrics GetGlyphMetrics(uint32_t codepoint) const override;
        float GetAscenderPx() const override { return m_AscenderPx; }
        float GetDescenderPx() const override { return m_DescenderPx; }
        float GetLineAdvancePx() const override;
        uint64_t GetGeneration() const override { return m_Generation; }

    private:
        Container::TSharedPtr<FontAtlas> m_Atlas;
        float m_AscenderPx = 0.0f;
        float m_DescenderPx = 0.0f;
        uint64_t m_Generation = 0;
    };
}

// アトラスの生成と同じ画素サイズで各フェイスの行寸法を取得する。
#include "Text/FontAtlasTextMetrics.h"

#include "Logging/LogMacros.h"
#include "Resource/FontAtlas.h"

#include <algorithm>

#if NORVES_ENABLE_CORE_TEXT
#include "Resource/FontFaceCache.h"
#include "Thread/Atomic.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <limits>
#include <utility>
#endif

namespace NorvesLib::Core
{
#if NORVES_ENABLE_CORE_TEXT
    namespace
    {
        Thread::Atomic<uint64_t> g_NextGeneration{1};
    }
#endif

    bool FontAtlasTextMetrics::Bind(Container::TSharedPtr<FontAtlas> atlas, const FontAtlasDesc& desc,
                                    const Asset::AssetFileReader& reader, FontFaceCache* faceCache)
    {
#if NORVES_ENABLE_CORE_TEXT
        if (!atlas || !atlas->IsCpuBuilt() || desc.PixelHeight == 0)
        {
            return false;
        }

        FT_Library rawLibrary = nullptr;
        if (FT_Init_FreeType(&rawLibrary) != 0)
        {
            NORVES_LOG_ERROR("FontAtlasTextMetrics", "FreeType を初期化できません");
            return false;
        }
        Container::TUniquePtr<FT_LibraryRec_, decltype(&FT_Done_FreeType)> library(rawLibrary, FT_Done_FreeType);

        FontFaceCache localCache;
        FontFaceCache& cache = faceCache ? *faceCache : localCache;
        float ascenderPx = 0.0f;
        float descenderPx = 0.0f;
        const size_t faceCount = desc.Faces.empty() ? 1 : desc.Faces.size();
        for (size_t i = 0; i < faceCount; ++i)
        {
            const Container::AnsiString& path = desc.Faces.empty() ? desc.FontLogicalPath : desc.Faces[i];
            const Asset::AssetBlob blob = cache.Acquire(Container::AnsiStringView(path), reader);
            FT_Face rawFace = nullptr;
            if (!blob.IsValid() || blob.GetSize() > static_cast<size_t>((std::numeric_limits<FT_Long>::max)()) ||
                FT_New_Memory_Face(library.get(), blob.GetData(), static_cast<FT_Long>(blob.GetSize()), 0, &rawFace) != 0)
            {
                NORVES_LOG_ERROR("FontAtlasTextMetrics", "フォントを読み込めません: %s", path.c_str());
                return false;
            }
            Container::TUniquePtr<FT_FaceRec_, decltype(&FT_Done_Face)> face(rawFace, FT_Done_Face);
            if (FT_Set_Pixel_Sizes(face.get(), 0, desc.PixelHeight) != 0 || !face->size)
            {
                NORVES_LOG_ERROR("FontAtlasTextMetrics", "指定の画素サイズで行寸法を取得できません: %s", path.c_str());
                return false;
            }
            const FT_Size_Metrics& metrics = face->size->metrics;
            ascenderPx = (std::max)(ascenderPx, static_cast<float>(metrics.ascender) / 64.0f);
            descenderPx = (std::max)(descenderPx, -static_cast<float>(metrics.descender) / 64.0f);
        }

        m_Atlas = std::move(atlas);
        m_AscenderPx = ascenderPx;
        m_DescenderPx = descenderPx;
        m_Generation = g_NextGeneration.FetchAdd(1);
        return true;
#else
        (void)atlas;
        (void)desc;
        (void)reader;
        (void)faceCache;
        NORVES_LOG_ERROR("FontAtlasTextMetrics", "テキスト機能が無効です");
        return false;
#endif
    }

    void FontAtlasTextMetrics::Unbind()
    {
        m_Atlas.reset();
        m_AscenderPx = 0.0f;
        m_DescenderPx = 0.0f;
        m_Generation = 0;
    }

    TextGlyphMetrics FontAtlasTextMetrics::GetGlyphMetrics(uint32_t codepoint) const
    {
        if (!m_Atlas)
        {
            return {};
        }
        const FontAtlasGlyph& glyph = m_Atlas->GetGlyph(codepoint);
        return {glyph.AdvancePx, glyph.SizePx, glyph.BearingPx, glyph.bValid};
    }

    float FontAtlasTextMetrics::GetLineAdvancePx() const
    {
        return m_Atlas ? (std::max)(m_Atlas->GetLineAdvancePx(), m_AscenderPx + m_DescenderPx) : 0.0f;
    }
}

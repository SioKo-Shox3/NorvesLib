#include "Resource/FontAtlas.h"

#include "Logging/LogMacros.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/RenderResources.h"

#if NORVES_ENABLE_CORE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(FontAtlas, Resource)

    FontAtlas::FontAtlas() = default;
    FontAtlas::~FontAtlas() = default;

    bool FontAtlas::Build(const FontAtlasDesc& desc,
                          const Asset::AssetFileReader& reader,
                          Rendering::TextureResources& textures)
    {
        if (m_bBuildAttempted)
        {
            return false;
        }

        m_bBuildAttempted = true;
        m_Desc = desc;
#if !NORVES_ENABLE_CORE_TEXT
        (void)reader;
        (void)textures;
        NORVES_LOG_ERROR("FontAtlas", "Core text is disabled");
        return false;
#else
        const Asset::AssetReadResult readResult = reader.Read(desc.FontLogicalPath);
        if (!readResult.Succeeded() || desc.PixelHeight == 0 || desc.AtlasWidth == 0)
        {
            NORVES_LOG_ERROR("FontAtlas", "Failed to read default font atlas source");
            return false;
        }

        FT_Library library = nullptr;
        FT_Face face = nullptr;
        if (FT_Init_FreeType(&library) != 0 ||
            FT_New_Memory_Face(library,
                               reinterpret_cast<const FT_Byte*>(readResult.Blob.GetData()),
                               static_cast<FT_Long>(readResult.Blob.GetSize()),
                               0,
                               &face) != 0 ||
            FT_Set_Pixel_Sizes(face, 0, desc.PixelHeight) != 0)
        {
            if (face)
            {
                FT_Done_Face(face);
            }
            if (library)
            {
                FT_Done_FreeType(library);
            }
            NORVES_LOG_ERROR("FontAtlas", "Failed to initialize FreeType font face");
            return false;
        }

        uint32_t penX = 1;
        uint32_t penY = 1;
        uint32_t rowHeight = 0;
        uint32_t atlasHeight = 1;
        for (uint32_t codepoint = FirstCodepoint; codepoint <= LastCodepoint; ++codepoint)
        {
            if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0)
            {
                continue;
            }

            const FT_Bitmap& bitmap = face->glyph->bitmap;
            const uint32_t glyphWidth = bitmap.width;
            const uint32_t glyphHeight = bitmap.rows;
            if (penX + glyphWidth + 1 > desc.AtlasWidth)
            {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }
            penX += glyphWidth + 1;
            rowHeight = glyphHeight > rowHeight ? glyphHeight : rowHeight;
            atlasHeight = penY + rowHeight + 1;
        }

        if (atlasHeight == 0)
        {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return false;
        }

        m_Width = desc.AtlasWidth;
        m_Height = atlasHeight;
        m_LineAdvancePx = face->size && face->size->metrics.height > 0
                              ? static_cast<float>(face->size->metrics.height) / 64.0f
                              : static_cast<float>(desc.PixelHeight);
        m_Pixels.resize(static_cast<size_t>(m_Width) * m_Height * 4u, 0);
        penX = 1;
        penY = 1;
        rowHeight = 0;
        for (uint32_t codepoint = FirstCodepoint; codepoint <= LastCodepoint; ++codepoint)
        {
            if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER) != 0)
            {
                continue;
            }

            const FT_GlyphSlot slot = face->glyph;
            const FT_Bitmap& bitmap = slot->bitmap;
            const uint32_t glyphWidth = bitmap.width;
            const uint32_t glyphHeight = bitmap.rows;
            if (penX + glyphWidth + 1 > m_Width)
            {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }

            FontAtlasGlyph& glyph = m_Glyphs[codepoint - FirstCodepoint];
            glyph.SizePx = Math::Vector2(static_cast<float>(glyphWidth), static_cast<float>(glyphHeight));
            glyph.BearingPx = Math::Vector2(static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top));
            glyph.AdvancePx = static_cast<float>(slot->advance.x) / 64.0f;
            glyph.UVRect = Math::Vector4(static_cast<float>(penX) / m_Width,
                                         static_cast<float>(penY) / m_Height,
                                         static_cast<float>(glyphWidth) / m_Width,
                                         static_cast<float>(glyphHeight) / m_Height);
            glyph.bValid = true;
            for (uint32_t row = 0; row < glyphHeight; ++row)
            {
                for (uint32_t column = 0; column < glyphWidth; ++column)
                {
                    const uint8_t coverage = bitmap.buffer[row * bitmap.pitch + column];
                    const size_t pixelIndex = (static_cast<size_t>(penY + row) * m_Width + penX + column) * 4u;
                    m_Pixels[pixelIndex] = 255;
                    m_Pixels[pixelIndex + 1] = 255;
                    m_Pixels[pixelIndex + 2] = 255;
                    m_Pixels[pixelIndex + 3] = coverage;
                }
            }
            penX += glyphWidth + 1;
            rowHeight = glyphHeight > rowHeight ? glyphHeight : rowHeight;
        }

        FT_Done_Face(face);
        FT_Done_FreeType(library);

        Rendering::TextureCreateInfo createInfo;
        createInfo.Width = m_Width;
        createInfo.Height = m_Height;
        createInfo.PixelFormat = Rendering::TextureCreateInfo::Format::RGBA8_UNORM;
        createInfo.DebugName = "CoreText.FontAtlas";
        m_Texture = textures.CreateTexture(createInfo, m_Pixels.data(), m_Pixels.size());
        m_bBuilt = m_Texture.IsValid();
        if (!m_bBuilt)
        {
            ClearCpuState();
            NORVES_LOG_ERROR("FontAtlas", "Failed to register font atlas texture");
        }
        return m_bBuilt;
#endif
    }

    void FontAtlas::Shutdown(Rendering::TextureResources& textures)
    {
        const Rendering::TextureHandle texture = m_Texture;
        m_Texture = Rendering::TextureHandle::Invalid();
        ClearCpuState();
        m_bBuilt = false;
        if (texture.IsValid())
        {
            textures.ReleaseTexture(texture);
        }
    }

    bool FontAtlas::Load()
    {
        return false;
    }

    void FontAtlas::Unload()
    {
    }

    bool FontAtlas::IsValid() const
    {
        return m_bBuilt && m_Texture.IsValid();
    }

    const FontAtlasGlyph& FontAtlas::GetGlyph(uint32_t codepoint) const
    {
        if (codepoint >= FirstCodepoint && codepoint <= LastCodepoint)
        {
            const FontAtlasGlyph& glyph = m_Glyphs[codepoint - FirstCodepoint];
            if (glyph.bValid)
            {
                return glyph;
            }
        }
        return GetFallbackGlyph();
    }

    const FontAtlasGlyph& FontAtlas::GetFallbackGlyph() const
    {
        return m_Glyphs[static_cast<uint32_t>('?') - FirstCodepoint];
    }

    void FontAtlas::ClearCpuState()
    {
        m_Glyphs = {};
        m_Pixels.clear();
        m_Width = 0;
        m_Height = 0;
        m_LineAdvancePx = 0.0f;
    }
} // namespace NorvesLib::Core

#include "Resource/FontAtlas.h"

#include "Logging/LogMacros.h"
#include "Rendering/GpuResourceTypes.h"
#include "Rendering/RenderResources.h"
#include "Resource/FontFaceCache.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

#if NORVES_ENABLE_CORE_TEXT
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace NorvesLib::Core
{
#if NORVES_ENABLE_CORE_TEXT
    namespace
    {
        struct LoadedFace
        {
            Asset::AssetBlob Blob;
            FT_Face Face = nullptr;
        };

        struct LoadedFaces
        {
            FT_Library Library = nullptr;
            Container::VariableArray<LoadedFace> Faces;

            ~LoadedFaces()
            {
                for (const LoadedFace& entry : Faces)
                {
                    if (entry.Face)
                    {
                        FT_Done_Face(entry.Face);
                    }
                }
                if (Library)
                {
                    FT_Done_FreeType(Library);
                }
            }
        };

        struct GlyphBitmap
        {
            uint32_t Width = 0;
            uint32_t Height = 0;
            size_t CoverageOffset = 0;
            bool bValid = false;
        };

        struct GlyphPosition
        {
            uint32_t X = 0;
            uint32_t Y = 0;
        };

        bool AppendCoverage(const FT_Bitmap& bitmap, uint32_t maxAtlasSize,
                            Container::VariableArray<uint8_t>& coverage, size_t& outOffset)
        {
            outOffset = coverage.size();
            const uint64_t pixelCount = static_cast<uint64_t>(bitmap.width) * bitmap.rows;
            const uint64_t maxPixels = static_cast<uint64_t>(maxAtlasSize) * maxAtlasSize;
            if (pixelCount > maxPixels || coverage.size() > maxPixels - pixelCount ||
                pixelCount > (std::numeric_limits<size_t>::max)() - coverage.size())
            {
                return false;
            }
            if (pixelCount == 0)
            {
                return true;
            }
            if (!bitmap.buffer)
            {
                return false;
            }

            uint64_t rowBytes = 0;
            switch (bitmap.pixel_mode)
            {
            case FT_PIXEL_MODE_MONO:
                rowBytes = (static_cast<uint64_t>(bitmap.width) + 7) / 8;
                break;
            case FT_PIXEL_MODE_GRAY2:
                rowBytes = (static_cast<uint64_t>(bitmap.width) + 3) / 4;
                break;
            case FT_PIXEL_MODE_GRAY4:
                rowBytes = (static_cast<uint64_t>(bitmap.width) + 1) / 2;
                break;
            case FT_PIXEL_MODE_GRAY:
                rowBytes = bitmap.width;
                break;
            case FT_PIXEL_MODE_BGRA:
                rowBytes = static_cast<uint64_t>(bitmap.width) * 4;
                break;
            default:
                return false;
            }

            const int64_t pitch = bitmap.pitch;
            const uint64_t absolutePitch = pitch < 0 ? static_cast<uint64_t>(-pitch) : static_cast<uint64_t>(pitch);
            if (absolutePitch < rowBytes)
            {
                return false;
            }

            coverage.resize(coverage.size() + static_cast<size_t>(pixelCount));
            const uint8_t* topRow = bitmap.buffer;
            if (pitch < 0)
            {
                topRow += static_cast<size_t>(absolutePitch) * (bitmap.rows - 1);
            }
            for (uint32_t row = 0; row < bitmap.rows; ++row)
            {
                const uint8_t* source = topRow + static_cast<ptrdiff_t>(pitch) * row;
                for (uint32_t column = 0; column < bitmap.width; ++column)
                {
                    uint8_t value = 0;
                    switch (bitmap.pixel_mode)
                    {
                    case FT_PIXEL_MODE_MONO:
                        value = (source[column / 8] & (0x80u >> (column % 8))) ? 255 : 0;
                        break;
                    case FT_PIXEL_MODE_GRAY2:
                        value = static_cast<uint8_t>(((source[column / 4] >> (6 - 2 * (column % 4))) & 3u) * 85u);
                        break;
                    case FT_PIXEL_MODE_GRAY4:
                        value = static_cast<uint8_t>(((source[column / 2] >> (4 - 4 * (column % 2))) & 15u) * 17u);
                        break;
                    case FT_PIXEL_MODE_GRAY:
                        value = source[column];
                        if (bitmap.num_grays > 1 && bitmap.num_grays < 256)
                        {
                            value = static_cast<uint8_t>(static_cast<uint32_t>(value) * 255u / (bitmap.num_grays - 1u));
                        }
                        break;
                    case FT_PIXEL_MODE_BGRA:
                        value = source[static_cast<size_t>(column) * 4 + 3];
                        break;
                    default:
                        break;
                    }
                    coverage[outOffset + static_cast<size_t>(row) * bitmap.width + column] = value;
                }
            }
            return true;
        }

        bool PackGlyphs(const Container::VariableArray<GlyphBitmap>& bitmaps,
                        uint32_t width, uint32_t padding, uint32_t maxHeight,
                        uint32_t& outHeight, Container::VariableArray<GlyphPosition>* positions)
        {
            if (width == 0 || padding > maxHeight)
            {
                return false;
            }

            if (positions)
            {
                positions->resize(bitmaps.size());
            }
            uint64_t penX = padding;
            uint64_t penY = padding;
            uint64_t rowHeight = 0;
            uint64_t atlasHeight = padding;
            for (size_t i = 0; i < bitmaps.size(); ++i)
            {
                const GlyphBitmap& bitmap = bitmaps[i];
                if (!bitmap.bValid)
                {
                    continue;
                }
                if (static_cast<uint64_t>(padding) * 2 + bitmap.Width > width)
                {
                    return false;
                }
                if (penX + bitmap.Width + padding > width)
                {
                    penX = padding;
                    penY += rowHeight + padding;
                    rowHeight = 0;
                }
                rowHeight = (std::max)(rowHeight, static_cast<uint64_t>(bitmap.Height));
                atlasHeight = penY + rowHeight + padding;
                if (atlasHeight > maxHeight)
                {
                    return false;
                }
                if (positions)
                {
                    (*positions)[i] = {static_cast<uint32_t>(penX), static_cast<uint32_t>(penY)};
                }
                penX += bitmap.Width + static_cast<uint64_t>(padding);
            }

            atlasHeight = (std::max)(atlasHeight, uint64_t{1});
            if (atlasHeight > maxHeight)
            {
                return false;
            }
            outHeight = static_cast<uint32_t>(atlasHeight);
            return true;
        }
    }
#endif

    IMPLEMENT_CLASS(FontAtlas, Resource)

    FontAtlas::FontAtlas() = default;
    FontAtlas::~FontAtlas() = default;

    bool FontAtlas::BuildCpu(const FontAtlasDesc& desc,
                             const Asset::AssetFileReader& reader,
                             FontFaceCache* faceCache)
    {
        if (m_bBuildAttempted)
        {
            return false;
        }
        m_bBuildAttempted = true;

#if !NORVES_ENABLE_CORE_TEXT
        (void)desc;
        (void)reader;
        (void)faceCache;
        NORVES_LOG_ERROR("FontAtlas", "テキスト機能が無効です");
        return false;
#else
        if (desc.PixelHeight == 0 || desc.MaxAtlasSize == 0 ||
            (desc.AtlasWidth != 0 && desc.AtlasWidth > desc.MaxAtlasSize))
        {
            NORVES_LOG_ERROR("FontAtlas", "フォントアトラスの設定が不正です");
            return false;
        }

        Container::VariableArray<uint32_t> codepoints;
        if (desc.Codepoints.empty())
        {
            codepoints.reserve(GlyphCount);
            for (uint32_t codepoint = FirstCodepoint; codepoint <= LastCodepoint; ++codepoint)
            {
                codepoints.push_back(codepoint);
            }
        }
        else
        {
            for (uint32_t codepoint : desc.Codepoints)
            {
                if (codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF))
                {
                    codepoints.push_back(codepoint);
                }
            }
            codepoints.push_back(0x3F);
            codepoints.push_back(0x25A1);
            codepoints.push_back(0xFFFD);
            std::sort(codepoints.begin(), codepoints.end());
            codepoints.erase(std::unique(codepoints.begin(), codepoints.end()), codepoints.end());
        }

        FontFaceCache localCache;
        FontFaceCache& cache = faceCache ? *faceCache : localCache;
        LoadedFaces loaded;
        if (FT_Init_FreeType(&loaded.Library) != 0)
        {
            NORVES_LOG_ERROR("FontAtlas", "FreeTypeを初期化できません");
            return false;
        }

        const size_t faceCount = desc.Faces.empty() ? 1 : desc.Faces.size();
        loaded.Faces.reserve(faceCount);
        for (size_t i = 0; i < faceCount; ++i)
        {
            const Container::AnsiString& path = desc.Faces.empty() ? desc.FontLogicalPath : desc.Faces[i];
            loaded.Faces.emplace_back();
            LoadedFace& entry = loaded.Faces.back();
            entry.Blob = cache.Acquire(Container::AnsiStringView(path), reader);
            if (!entry.Blob.IsValid() ||
                entry.Blob.GetSize() > static_cast<size_t>((std::numeric_limits<FT_Long>::max)()) ||
                FT_New_Memory_Face(loaded.Library,
                                   reinterpret_cast<const FT_Byte*>(entry.Blob.GetData()),
                                   static_cast<FT_Long>(entry.Blob.GetSize()),
                                   0, &entry.Face) != 0 ||
                FT_Set_Pixel_Sizes(entry.Face, 0, desc.PixelHeight) != 0)
            {
                NORVES_LOG_ERROR("FontAtlas", "フォントを読み込めません: %s", path.c_str());
                return false;
            }
        }

        const FT_Face firstFace = loaded.Faces[0].Face;
        const float lineAdvance = firstFace->size && firstFace->size->metrics.height > 0
                                      ? static_cast<float>(firstFace->size->metrics.height) / 64.0f
                                      : static_cast<float>(desc.PixelHeight);

        Container::VariableArray<FontAtlasGlyph> glyphs(codepoints.size());
        Container::VariableArray<GlyphBitmap> bitmaps(codepoints.size());
        Container::VariableArray<uint8_t> coverage;
        for (size_t i = 0; i < codepoints.size(); ++i)
        {
            FT_Face selectedFace = nullptr;
            FT_UInt glyphIndex = 0;
            for (const LoadedFace& entry : loaded.Faces)
            {
                glyphIndex = FT_Get_Char_Index(entry.Face, codepoints[i]);
                if (glyphIndex != 0)
                {
                    selectedFace = entry.Face;
                    break;
                }
            }
            if (!selectedFace || FT_Load_Glyph(selectedFace, glyphIndex, FT_LOAD_RENDER) != 0)
            {
                continue;
            }

            const FT_GlyphSlot slot = selectedFace->glyph;
            const FT_Bitmap& bitmap = slot->bitmap;
            GlyphBitmap& glyphBitmap = bitmaps[i];
            glyphBitmap.Width = bitmap.width;
            glyphBitmap.Height = bitmap.rows;
            if (!AppendCoverage(bitmap, desc.MaxAtlasSize, coverage, glyphBitmap.CoverageOffset))
            {
                NORVES_LOG_ERROR("FontAtlas", "文字の画素をアトラスに格納できません");
                return false;
            }
            glyphBitmap.bValid = true;

            FontAtlasGlyph& glyph = glyphs[i];
            glyph.SizePx = Math::Vector2(static_cast<float>(bitmap.width), static_cast<float>(bitmap.rows));
            glyph.BearingPx = Math::Vector2(static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top));
            glyph.AdvancePx = static_cast<float>(slot->advance.x) / 64.0f;
            glyph.bValid = true;
        }

        uint32_t width = desc.AtlasWidth;
        uint32_t height = 0;
        if (width == 0)
        {
            for (uint32_t candidate = 64; candidate <= desc.MaxAtlasSize;)
            {
                uint32_t candidateHeight = 0;
                if (PackGlyphs(bitmaps, candidate, desc.Padding, desc.MaxAtlasSize, candidateHeight, nullptr) &&
                    candidateHeight <= candidate)
                {
                    width = candidate;
                    height = candidateHeight;
                    break;
                }
                if (candidate > desc.MaxAtlasSize / 2)
                {
                    break;
                }
                candidate *= 2;
            }
        }
        else if (!PackGlyphs(bitmaps, width, desc.Padding, desc.MaxAtlasSize, height, nullptr))
        {
            width = 0;
        }

        if (width == 0)
        {
            NORVES_LOG_ERROR("FontAtlas", "指定された上限に文字を詰められません");
            return false;
        }

        Container::VariableArray<GlyphPosition> positions;
        if (!PackGlyphs(bitmaps, width, desc.Padding, desc.MaxAtlasSize, height, &positions))
        {
            NORVES_LOG_ERROR("FontAtlas", "文字の配置を決められません");
            return false;
        }
        const uint64_t pixelCount = static_cast<uint64_t>(width) * height;
        if (pixelCount > (std::numeric_limits<size_t>::max)() / 4)
        {
            NORVES_LOG_ERROR("FontAtlas", "フォントアトラスの画素数が大きすぎます");
            return false;
        }

        Container::VariableArray<uint8_t> pixels(static_cast<size_t>(pixelCount) * 4u, 0);
        for (size_t i = 0; i < bitmaps.size(); ++i)
        {
            const GlyphBitmap& bitmap = bitmaps[i];
            if (!bitmap.bValid)
            {
                continue;
            }
            const GlyphPosition& position = positions[i];
            glyphs[i].UVRect = Math::Vector4(static_cast<float>(position.X) / width,
                                             static_cast<float>(position.Y) / height,
                                             static_cast<float>(bitmap.Width) / width,
                                             static_cast<float>(bitmap.Height) / height);
            for (uint32_t row = 0; row < bitmap.Height; ++row)
            {
                for (uint32_t column = 0; column < bitmap.Width; ++column)
                {
                    const size_t pixelIndex = (static_cast<size_t>(position.Y + row) * width + position.X + column) * 4u;
                    pixels[pixelIndex] = 255;
                    pixels[pixelIndex + 1] = 255;
                    pixels[pixelIndex + 2] = 255;
                    pixels[pixelIndex + 3] = coverage[bitmap.CoverageOffset + static_cast<size_t>(row) * bitmap.Width + column];
                }
            }
        }

        m_Codepoints = std::move(codepoints);
        m_Glyphs = std::move(glyphs);
        m_Pixels = std::move(pixels);
        m_Width = width;
        m_Height = height;
        m_LineAdvancePx = lineAdvance;
        m_bCpuBuilt = true;
        return true;
#endif
    }

    bool FontAtlas::Upload(Rendering::TextureResources& textures)
    {
        if (!m_bCpuBuilt || m_bBuilt || m_Texture.IsValid())
        {
            return false;
        }

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
            NORVES_LOG_ERROR("FontAtlas", "フォントアトラスのテクスチャを登録できません");
        }
        return m_bBuilt;
    }

    bool FontAtlas::Build(const FontAtlasDesc& desc,
                          const Asset::AssetFileReader& reader,
                          Rendering::TextureResources& textures)
    {
        return BuildCpu(desc, reader) && Upload(textures);
    }

    bool FontAtlas::Build(const FontAtlasDesc& desc,
                          const Asset::AssetFileReader& reader,
                          Rendering::TextureResources& textures,
                          FontFaceCache& faceCache)
    {
        return BuildCpu(desc, reader, &faceCache) && Upload(textures);
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

    const FontAtlasGlyph* FontAtlas::FindGlyph(uint32_t codepoint) const
    {
        const auto it = std::lower_bound(m_Codepoints.begin(), m_Codepoints.end(), codepoint);
        if (it == m_Codepoints.end() || *it != codepoint)
        {
            return nullptr;
        }
        const FontAtlasGlyph& glyph = m_Glyphs[static_cast<size_t>(it - m_Codepoints.begin())];
        return glyph.bValid ? &glyph : nullptr;
    }

    const FontAtlasGlyph& FontAtlas::GetGlyph(uint32_t codepoint) const
    {
        const FontAtlasGlyph* glyph = FindGlyph(codepoint);
        return glyph ? *glyph : GetFallbackGlyph();
    }

    const FontAtlasGlyph& FontAtlas::GetFallbackGlyph() const
    {
        constexpr uint32_t fallbacks[] = {0x3F, 0x25A1, 0xFFFD};
        for (uint32_t codepoint : fallbacks)
        {
            if (const FontAtlasGlyph* glyph = FindGlyph(codepoint))
            {
                return *glyph;
            }
        }
        static const FontAtlasGlyph emptyGlyph;
        return emptyGlyph;
    }

    void FontAtlas::ClearCpuState()
    {
        m_Codepoints.clear();
        m_Glyphs.clear();
        m_Pixels.clear();
        m_Width = 0;
        m_Height = 0;
        m_LineAdvancePx = 0.0f;
        m_bCpuBuilt = false;
    }
} // namespace NorvesLib::Core

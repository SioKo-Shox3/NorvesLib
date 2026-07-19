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
    struct FontAtlasDesc
    {
        Container::AnsiString FontLogicalPath = "Fonts/Inter-Regular.ttf";
        uint32_t PixelHeight = 32;
        uint32_t AtlasWidth = 512;
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

        bool Build(const FontAtlasDesc& desc,
                   const Asset::AssetFileReader& reader,
                   Rendering::TextureResources& textures);
        void Shutdown(Rendering::TextureResources& textures);

        bool Load() override;
        void Unload() override;
        bool IsValid() const override;

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

        const FontAtlasGlyph& GetFallbackGlyph() const;
        void ClearCpuState();

        FontAtlasDesc m_Desc;
        Container::FixedArray<FontAtlasGlyph, GlyphCount> m_Glyphs;
        Container::VariableArray<uint8_t> m_Pixels;
        Rendering::TextureHandle m_Texture = Rendering::TextureHandle::Invalid();
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        float m_LineAdvancePx = 0.0f;
        bool m_bBuildAttempted = false;
        bool m_bBuilt = false;
    };
} // namespace NorvesLib::Core


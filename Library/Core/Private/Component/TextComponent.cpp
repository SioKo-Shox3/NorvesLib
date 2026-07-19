#include "Component/TextComponent.h"

#include "Math/Vector3.h"
#include "Resource/FontAtlas.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(TextComponent, BoardComponent)

    TextComponent::TextComponent() = default;
    TextComponent::~TextComponent() = default;

    void TextComponent::SetText(const Container::String& text)
    {
        m_Text = text;
        MarkRenderStateDirty();
    }

    void TextComponent::SetFontAtlas(const FontAtlas* atlas)
    {
        m_FontAtlas = atlas;
        MarkRenderStateDirty();
    }

    void TextComponent::SetLetterSpacing(float spacing)
    {
        m_LetterSpacing = spacing;
        MarkRenderStateDirty();
    }

    void TextComponent::BuildGlyphBoardProxies(Container::VariableArray<Rendering::BoardProxy>& outProxies) const
    {
        if (!IsVisible() || m_Text.empty() || !m_FontAtlas || !m_FontAtlas->IsValid())
        {
            return;
        }

        const float lineHeight = m_FontAtlas->GetLineAdvancePx() > 0.0f
                                     ? m_FontAtlas->GetLineAdvancePx()
                                     : 1.0f;
        float penX = 0.0f;
        float penY = lineHeight;
        for (const TCHAR character : m_Text)
        {
            if (character == TEXT('\n'))
            {
                penX = 0.0f;
                penY += lineHeight;
                continue;
            }

            const FontAtlasGlyph& glyph = m_FontAtlas->GetGlyph(static_cast<uint32_t>(character));
            if (!glyph.bValid)
            {
                continue;
            }

            if (character != TEXT(' '))
            {
                Rendering::BoardProxy proxy;
                if (BuildBoardProxy(proxy))
                {
                    Math::Vector3 translation = proxy.WorldTransform.GetTranslationRow();
                    translation.x += penX + glyph.BearingPx.x;
                    translation.y += penY - glyph.BearingPx.y;
                    proxy.WorldTransform.SetTranslationRow(translation);
                    proxy.Texture = m_FontAtlas->GetTextureHandle();
                    proxy.SizePx = glyph.SizePx;
                    proxy.UVRect = glyph.UVRect;
                    proxy.Space = Rendering::BoardSpace::ScreenSpace;
                    proxy.ComponentId = GetComponentId();
                    outProxies.push_back(proxy);
                }
            }
            penX += glyph.AdvancePx + m_LetterSpacing;
        }
    }
} // namespace NorvesLib::Core::Component

#pragma once

#include "Component/BoardComponent.h"

namespace NorvesLib::Core
{
    class FontAtlas;
}

namespace NorvesLib::Core::Component
{
    class TextComponent final : public BoardComponent
    {
        REFLECTION_CLASS(TextComponent, BoardComponent)

    public:
        TextComponent();
        ~TextComponent() override;

        void SetText(const Container::String& text);
        const Container::String& GetText() const { return m_Text; }
        void SetFontAtlas(const FontAtlas* atlas);
        const FontAtlas* GetFontAtlas() const { return m_FontAtlas; }
        void SetLetterSpacing(float spacing);
        float GetLetterSpacing() const { return m_LetterSpacing; }
        void BuildGlyphBoardProxies(Container::VariableArray<Rendering::BoardProxy>& outProxies) const;

    private:
        Container::String m_Text;
        const FontAtlas* m_FontAtlas = nullptr;
        float m_LetterSpacing = 0.0f;
    };
} // namespace NorvesLib::Core::Component


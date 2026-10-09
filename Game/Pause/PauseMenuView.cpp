#include "Pause/PauseMenuView.h"

#if defined(NORVES_ENABLE_IMGUI)

#include "Pause/PauseMenu.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Game::Pause
{

    namespace
    {
        using NorvesLib::Modules::Gui::ImGuiFontSlot;

        // 文言（UTF-8 の固定の文字列）。ここに書いた文字は RegisterPauseMenuDisplayText で字のアトラスへ載る。
        constexpr const char *kTitleText = "一時停止";
        constexpr const char *kItemTexts[PauseMenu::kItemCount] = {"続ける", "ランチャーへ戻る", "ゲームを終了"};
        constexpr const char *kHintText = "矢印キー・WASD・マウス・ゲームパッドで選び、Enter・クリック・Aボタンで決めます。Esc で続けます";

        // 配色（ランチャーと同じ夕景に合わせた暖色のアクセント）
        constexpr ImU32 kColorAccent = IM_COL32(255, 176, 92, 255);
        constexpr ImU32 kColorText = IM_COL32(244, 238, 230, 255);
        constexpr ImU32 kColorShadow = IM_COL32(0, 0, 0, 150);
        constexpr ImU32 kColorCurtain = IM_COL32(4, 6, 14, 168);
        constexpr ImU32 kColorPanel = IM_COL32(14, 16, 28, 236);
        constexpr ImU32 kColorButton = IM_COL32(14, 16, 28, 214);
        constexpr ImU32 kColorButtonSelected = IM_COL32(28, 26, 38, 232);
        constexpr ImU32 kColorEdge = IM_COL32(255, 255, 255, 40);

        ImFont *GetFont(ImGuiIO &io, ImGuiFontSlot slot)
        {
            const int index = static_cast<int>(slot);
            if (index >= 0 && index < io.Fonts->Fonts.Size && io.Fonts->Fonts[index] != nullptr)
            {
                return io.Fonts->Fonts[index];
            }
            return io.Fonts->Fonts.Size > 0 ? io.Fonts->Fonts[0] : nullptr;
        }

        float TextWidth(ImFont *font, const char *text)
        {
            return font->CalcTextSizeA(font->LegacySize, FLT_MAX, 0.0f, text).x;
        }

        struct Rect
        {
            ImVec2 Min;
            ImVec2 Max;
        };
    } // namespace

    void RegisterPauseMenuDisplayText()
    {
        using NorvesLib::Modules::Gui::AddImGuiDisplayText;
        AddImGuiDisplayText(kTitleText);
        for (const char *itemText : kItemTexts)
        {
            AddImGuiDisplayText(itemText);
        }
        AddImGuiDisplayText(kHintText);
    }

    void PauseMenuView::OnImGui()
    {
        if (m_pMenu == nullptr || !m_pMenu->IsOpen())
        {
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        const ImVec2 display = io.DisplaySize;
        if (display.x <= 0.0f || display.y <= 0.0f)
        {
            return;
        }

        ImFont *titleFont = GetFont(io, ImGuiFontSlot::DisplayHeading);
        ImFont *itemFont = GetFont(io, ImGuiFontSlot::DisplayHeading);
        ImFont *hintFont = GetFont(io, ImGuiFontSlot::DisplayCaption);
        if (titleFont == nullptr || itemFont == nullptr || hintFont == nullptr)
        {
            return;
        }

        // 1080 を基準にした画面の高さの比で、基準の長さ（px）を実際の長さにする
        const float unit = display.y / 1080.0f;
        const auto px = [unit](float basis) { return basis * unit; };

        // ---- 配置 ----
        const float panelPad = px(44.0f);
        const float titleHeight = titleFont->LegacySize * 1.3f;
        const float buttonWidth = px(480.0f);
        const float buttonHeight = px(64.0f);
        const float buttonGap = px(16.0f);
        const float panelWidth = buttonWidth + panelPad * 2.0f;
        const float panelHeight = panelPad + titleHeight + px(28.0f) +
                                  buttonHeight * static_cast<float>(PauseMenu::kItemCount) +
                                  buttonGap * static_cast<float>(PauseMenu::kItemCount - 1) + panelPad;
        const Rect panel = {
            ImVec2((display.x - panelWidth) * 0.5f, (display.y - panelHeight) * 0.5f),
            ImVec2((display.x + panelWidth) * 0.5f, (display.y + panelHeight) * 0.5f),
        };
        const float buttonsTop = panel.Min.y + panelPad + titleHeight + px(28.0f);

        Rect buttons[PauseMenu::kItemCount];
        for (int index = 0; index < PauseMenu::kItemCount; ++index)
        {
            const float top = buttonsTop + (buttonHeight + buttonGap) * static_cast<float>(index);
            buttons[index].Min = ImVec2(panel.Min.x + panelPad, top);
            buttons[index].Max = ImVec2(panel.Min.x + panelPad + buttonWidth, top + buttonHeight);
        }

        const bool bMouseValid = io.MousePos.x > -FLT_MAX * 0.5f && io.MousePos.y > -FLT_MAX * 0.5f;
        const bool bMouseMoved = bMouseValid && (std::fabs(io.MousePos.x - m_LastMouseX) > 0.5f ||
                                                 std::fabs(io.MousePos.y - m_LastMouseY) > 0.5f);

        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        constexpr ImGuiWindowFlags kWindowFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("##NorvesPauseMenu", nullptr, kWindowFlags))
        {
            // ---- 入力（マウスのホバーとクリック）----
            for (int index = 0; index < PauseMenu::kItemCount; ++index)
            {
                ImGui::PushID(index);
                ImGui::SetCursorScreenPos(buttons[index].Min);
                const bool bClicked = ImGui::InvisibleButton(
                    "item", ImVec2(buttons[index].Max.x - buttons[index].Min.x,
                                   buttons[index].Max.y - buttons[index].Min.y));
                if (bClicked)
                {
                    m_pMenu->SetSelected(index);
                    m_pMenu->RequestActivate(index);
                }
                else if (bMouseMoved && ImGui::IsItemHovered())
                {
                    m_pMenu->SetSelected(index);
                }
                ImGui::PopID();
            }

            // ---- 描画 ----
            ImDrawList *drawList = ImGui::GetWindowDrawList();
            const int selected = m_pMenu->GetSelected();
            const float panelRadius = px(12.0f);

            // 暗幕とパネル
            drawList->AddRectFilled(ImVec2(0.0f, 0.0f), display, kColorCurtain);
            drawList->AddRectFilled(panel.Min, panel.Max, kColorPanel, panelRadius);
            drawList->AddRect(panel.Min, panel.Max, kColorEdge, panelRadius, 0, px(1.5f));

            // タイトルと下線のアクセント
            const float titleWidth = TextWidth(titleFont, kTitleText);
            const float titleY = panel.Min.y + panelPad;
            const float shadowOffset = px(2.0f);
            drawList->AddText(titleFont, titleFont->LegacySize,
                              ImVec2((display.x - titleWidth) * 0.5f + shadowOffset, titleY + shadowOffset),
                              kColorShadow, kTitleText);
            drawList->AddText(titleFont, titleFont->LegacySize, ImVec2((display.x - titleWidth) * 0.5f, titleY),
                              kColorText, kTitleText);
            drawList->AddRectFilled(ImVec2(display.x * 0.5f - px(48.0f), titleY + titleHeight + px(6.0f)),
                                    ImVec2(display.x * 0.5f + px(48.0f), titleY + titleHeight + px(10.0f)),
                                    kColorAccent, px(2.0f));

            // 項目
            for (int index = 0; index < PauseMenu::kItemCount; ++index)
            {
                const bool bSelected = index == selected;
                const float lift = bSelected ? -px(3.0f) : 0.0f;
                const ImVec2 min(buttons[index].Min.x, buttons[index].Min.y + lift);
                const ImVec2 max(buttons[index].Max.x, buttons[index].Max.y + lift);
                if (bSelected)
                {
                    for (int ring = 3; ring >= 1; --ring)
                    {
                        const float spread = px(4.0f) * static_cast<float>(ring);
                        drawList->AddRect(ImVec2(min.x - spread, min.y - spread),
                                          ImVec2(max.x + spread, max.y + spread), IM_COL32(255, 176, 92, 34 / ring),
                                          buttonHeight * 0.5f + spread, 0, px(4.0f));
                    }
                }
                drawList->AddRectFilled(min, max, bSelected ? kColorButtonSelected : kColorButton,
                                        buttonHeight * 0.5f);
                drawList->AddRect(min, max, bSelected ? kColorAccent : kColorEdge, buttonHeight * 0.5f, 0,
                                  bSelected ? px(3.5f) : px(1.5f));
                const float labelWidth = TextWidth(itemFont, kItemTexts[index]);
                drawList->AddText(itemFont, itemFont->LegacySize,
                                  ImVec2(min.x + (buttonWidth - labelWidth) * 0.5f,
                                         min.y + (buttonHeight - itemFont->LegacySize) * 0.5f),
                                  bSelected ? kColorAccent : kColorText, kItemTexts[index]);
            }

            // 操作の案内
            {
                const float hintWidth = TextWidth(hintFont, kHintText);
                const ImVec2 position((display.x - hintWidth) * 0.5f, display.y - px(56.0f));
                drawList->AddText(hintFont, hintFont->LegacySize, ImVec2(position.x + px(1.5f), position.y + px(1.5f)),
                                  kColorShadow, kHintText);
                drawList->AddText(hintFont, hintFont->LegacySize, position, IM_COL32(214, 218, 232, 210), kHintText);
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(3);

        if (bMouseValid)
        {
            m_LastMouseX = io.MousePos.x;
            m_LastMouseY = io.MousePos.y;
        }
    }

} // namespace Game::Pause

#endif // NORVES_ENABLE_IMGUI

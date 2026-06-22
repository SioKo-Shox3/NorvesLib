#include "ImGuiModule/NorvesImGuiStyle.h"

#include "imgui.h"

// NorvesImGuiStyle.cpp — ダーク基調＋シアン系アクセント 1 色のテーマ実装。
//
// StyleColorsDark を起点とし、角丸・余白・配色を NorvesLib の見た目へ寄せる。
// 色はすべて ImVec4(r,g,b,a)（0..1 線形ではなく sRGB 近似の素直な値）で指定し、
// アクセント色は 1 系統（シアン/ブルー）に統一してホバー/アクティブ/選択の
// インタラクション色を揃える（色のみ。フォントラスタライザには非依存）。
namespace NorvesLib::Modules::Gui
{
    namespace
    {
        // アクセント（シアン系）。通常/ホバー/アクティブの 3 段で明度を変える。
        constexpr ImVec4 kAccent = ImVec4(0.16f, 0.65f, 0.86f, 1.00f);
        constexpr ImVec4 kAccentHovered = ImVec4(0.26f, 0.74f, 0.94f, 1.00f);
        constexpr ImVec4 kAccentActive = ImVec4(0.10f, 0.52f, 0.74f, 1.00f);

        // 背景階調（暗→やや明）。ウィンドウ/フレーム/ヘッダの土台色。
        constexpr ImVec4 kBgWindow = ImVec4(0.09f, 0.10f, 0.12f, 1.00f);
        constexpr ImVec4 kBgChild = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
        constexpr ImVec4 kBgPopup = ImVec4(0.08f, 0.09f, 0.11f, 0.98f);
        constexpr ImVec4 kBgFrame = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
        constexpr ImVec4 kBgFrameHovered = ImVec4(0.21f, 0.24f, 0.28f, 1.00f);
        constexpr ImVec4 kBgFrameActive = ImVec4(0.24f, 0.28f, 0.33f, 1.00f);
        constexpr ImVec4 kBgHeader = ImVec4(0.18f, 0.21f, 0.25f, 1.00f);

        constexpr ImVec4 kText = ImVec4(0.90f, 0.91f, 0.93f, 1.00f);
        constexpr ImVec4 kTextDisabled = ImVec4(0.46f, 0.48f, 0.52f, 1.00f);
        constexpr ImVec4 kBorder = ImVec4(0.24f, 0.26f, 0.30f, 0.60f);
    } // namespace

    void NorvesImGuiStyle::ApplyNorvesStyle(ImGuiStyle &style)
    {
        // 起点はダーク。以降で角丸・余白・配色を NorvesLib 仕様へ上書きする。
        ::ImGui::StyleColorsDark(&style);

        // ---- 角丸（やわらかい印象に） ----
        style.WindowRounding = 6.0f;
        style.ChildRounding = 6.0f;
        style.FrameRounding = 4.0f;
        style.PopupRounding = 4.0f;
        style.GrabRounding = 4.0f;
        style.ScrollbarRounding = 6.0f;
        style.TabRounding = 4.0f;

        // ---- 余白・間隔（情報密度と可読性のバランス） ----
        style.WindowPadding = ImVec2(10.0f, 10.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 8.0f);
        style.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
        style.IndentSpacing = 20.0f;
        style.ScrollbarSize = 14.0f;
        style.GrabMinSize = 12.0f;

        // ---- 枠線（うっすら） ----
        style.WindowBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;

        // ---- 配色（統一ダーク＋アクセント 1 色） ----
        ImVec4 *colors = style.Colors;
        colors[ImGuiCol_Text] = kText;
        colors[ImGuiCol_TextDisabled] = kTextDisabled;
        colors[ImGuiCol_WindowBg] = kBgWindow;
        colors[ImGuiCol_ChildBg] = kBgChild;
        colors[ImGuiCol_PopupBg] = kBgPopup;
        colors[ImGuiCol_Border] = kBorder;
        colors[ImGuiCol_FrameBg] = kBgFrame;
        colors[ImGuiCol_FrameBgHovered] = kBgFrameHovered;
        colors[ImGuiCol_FrameBgActive] = kBgFrameActive;
        colors[ImGuiCol_TitleBg] = kBgChild;
        colors[ImGuiCol_TitleBgActive] = kBgHeader;
        colors[ImGuiCol_TitleBgCollapsed] = kBgWindow;
        colors[ImGuiCol_MenuBarBg] = kBgChild;
        colors[ImGuiCol_ScrollbarBg] = kBgWindow;
        colors[ImGuiCol_ScrollbarGrab] = kBgFrameHovered;
        colors[ImGuiCol_ScrollbarGrabHovered] = kBgFrameActive;
        colors[ImGuiCol_ScrollbarGrabActive] = kAccentActive;

        // インタラクション系はアクセントで統一。
        colors[ImGuiCol_CheckMark] = kAccent;
        colors[ImGuiCol_SliderGrab] = kAccent;
        colors[ImGuiCol_SliderGrabActive] = kAccentHovered;
        colors[ImGuiCol_Button] = kBgFrame;
        colors[ImGuiCol_ButtonHovered] = kAccent;
        colors[ImGuiCol_ButtonActive] = kAccentActive;
        colors[ImGuiCol_Header] = kBgHeader;
        colors[ImGuiCol_HeaderHovered] = kAccent;
        colors[ImGuiCol_HeaderActive] = kAccentActive;
        colors[ImGuiCol_Separator] = kBorder;
        colors[ImGuiCol_SeparatorHovered] = kAccentHovered;
        colors[ImGuiCol_SeparatorActive] = kAccentActive;
        colors[ImGuiCol_ResizeGrip] = kBgFrameHovered;
        colors[ImGuiCol_ResizeGripHovered] = kAccentHovered;
        colors[ImGuiCol_ResizeGripActive] = kAccentActive;
        colors[ImGuiCol_Tab] = kBgChild;
        colors[ImGuiCol_TabHovered] = kAccentHovered;
        colors[ImGuiCol_TabSelected] = kBgHeader;
        colors[ImGuiCol_TabSelectedOverline] = kAccent;
        colors[ImGuiCol_TabDimmed] = kBgWindow;
        colors[ImGuiCol_TabDimmedSelected] = kBgChild;
        colors[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
        colors[ImGuiCol_NavCursor] = kAccent;
        colors[ImGuiCol_PlotHistogram] = kAccent;
        colors[ImGuiCol_PlotHistogramHovered] = kAccentHovered;
    }
} // namespace NorvesLib::Modules::Gui

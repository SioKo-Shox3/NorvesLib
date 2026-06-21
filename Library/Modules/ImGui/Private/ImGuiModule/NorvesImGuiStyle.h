#pragma once

struct ImGuiStyle;

// NorvesImGuiStyle.h — NorvesLib 固有の ImGui テーマ（ダーク＋アクセント 1 色）を
// ImGuiStyle へ適用するヘルパ。
//
// imgui 型を本ヘッダの公開面に漏らさないよう ImGuiStyle の前方宣言のみを参照し、
// 実装（.cpp）で imgui.h を include して具体的な色/角丸/余白を設定する。適用は
// ImGuiModule::Initialize の 1 箇所のみ（StyleColorsDark 起点 → 本適用で上書き）。
namespace NorvesLib::Modules::Gui
{
    /**
     * @brief NorvesLib テーマを ImGuiStyle へ適用する
     *
     * StyleColorsDark を起点に、WindowRounding/FrameRounding/GrabRounding と
     * WindowPadding/FramePadding/ItemSpacing を調整し、統一ダーク配色＋シアン系
     * アクセント 1 色を上書きする。DPI スケール（ScaleAllSizes）は呼び出し側で別途
     * 適用する（本関数は等倍前提の基準値を設定する）。
     *
     * @param style 適用先 ImGuiStyle（通常は ::ImGui::GetStyle()）
     */
    class NorvesImGuiStyle
    {
    public:
        static void ApplyNorvesStyle(ImGuiStyle &style);
    };
} // namespace NorvesLib::Modules::Gui

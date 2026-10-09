#pragma once

// ---------------------------------------------------------------------------
// mega-bananas の情報表示（ImGui の小さな半透明パネル）
//
// H キーで出し入れする。本数・1 本の三角形数・元の三角形の総数を出し、開発ビルドでは MegaGeometry の GPU 時間と、
// 描いたクラスタ数の扱いも出す。窓は入力を受けず（マウス・キーを奪わない）、画面の左上のパネルだけを描く。
// NORVES_ENABLE_IMGUI が無いビルドでは、このクラスはコンパイルされない。
// ---------------------------------------------------------------------------

#if defined(NORVES_ENABLE_IMGUI)

#include "GameModes/MegaBananas/BananasInfo.h"
#include "ImGuiModule/IImGuiView.h"

namespace Game::GameModes
{

    /// 情報表示が画面に出す文字を、ImGui の表示用フォントへ載せる文字として渡す。
    /// ImGui モジュールの Initialize より前に呼ぶ
    void RegisterBananasInfoDisplayText();

    class MegaBananasInfoView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        /// info と bVisible は借用（Mode のデータが所有し、ビューより長く生きる）
        MegaBananasInfoView(const BananasInfo *info, const bool *bVisible) : m_pInfo(info), m_pVisible(bVisible)
        {
        }

        void OnImGui() override;

        const char *GetViewName() const override
        {
            return "MegaBananasInfo";
        }

    private:
        const BananasInfo *m_pInfo = nullptr;
        const bool *m_pVisible = nullptr;
    };

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI

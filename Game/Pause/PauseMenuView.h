#pragma once

// ---------------------------------------------------------------------------
// ポーズメニューの画面（ImGui の全画面の暗幕と中央のパネル）
//
// 見た目はランチャーと同じ作り（半透明の暗幕・角丸のパネル・橙のアクセント・NotoSansJP の表示用フォント）。
// ImGui の既定の窓（灰色の背景・タイトルバー）は使わず、背景なしの全画面の窓に ImDrawList で直接描く。
// メニューが閉じている間は ImGui の窓を作らない（シーンのマウス操作を ImGui が奪わないように）。
// NORVES_ENABLE_IMGUI が無いビルドでは、このクラスはコンパイルされない。
// ---------------------------------------------------------------------------

#if defined(NORVES_ENABLE_IMGUI)

#include "ImGuiModule/IImGuiView.h"

namespace Game::Pause
{

    class PauseMenu;

    /// ポーズメニューが画面に出す文字を、ImGui の表示用フォントへ載せる文字として渡す。
    /// ImGui モジュールの Initialize より前に呼ぶ
    void RegisterPauseMenuDisplayText();

    class PauseMenuView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        explicit PauseMenuView(PauseMenu *menu) : m_pMenu(menu)
        {
        }

        void OnImGui() override;

        const char *GetViewName() const override
        {
            return "PauseMenu";
        }

    private:
        PauseMenu *m_pMenu = nullptr;
        /// マウスが動いたときだけホバーで選ぶ（止まったマウスが矢印キーの選択を奪わないように）
        float m_LastMouseX = -1.0f;
        float m_LastMouseY = -1.0f;
    };

} // namespace Game::Pause

#endif // NORVES_ENABLE_IMGUI

#pragma once

// ---------------------------------------------------------------------------
// ランチャーの画面（ImGui の全画面のメニュー）
//
// 背景の物理空（LauncherRoutine が組む）の上に、タイトル・シーンのカード・「ゲームを終了」を描く。
// ImGui の既定の窓（灰色の背景・タイトルバー・スクロールバー）は使わず、背景なしの全画面の窓に
// ImDrawList で直接描く。字は ImGuiFontSlot::Display*（NotoSansJP。窓の高さに比例した大きさ）で描く。
// NORVES_ENABLE_IMGUI が無いビルドでは、このクラスはコンパイルされない。
// ---------------------------------------------------------------------------

#if defined(NORVES_ENABLE_IMGUI)

#include "ImGuiModule/IImGuiView.h"
#include "Scenes/SceneCatalog.h"

namespace Game::GameModes
{

    class LauncherMenu;

    /// ランチャーが画面に出す文字（固定の文言とシーンの一覧の文字列）を、ImGui の表示用フォントへ載せる文字として渡す。
    /// ImGui モジュールの Initialize より前に呼ぶ
    void RegisterLauncherDisplayText();

    class LauncherView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        explicit LauncherView(LauncherMenu *menu) : m_pMenu(menu)
        {
        }

        /// シーンの必要な資産が欠けているか（カードに取得のコマンドを出す）。添字は kSceneCatalog の並び
        void SetAssetsMissing(std::size_t sceneIndex, bool bMissing);

        void OnImGui() override;

        const char *GetViewName() const override
        {
            return "Launcher";
        }

    private:
        static constexpr std::size_t kMaxScenes = 16;
        static_assert(Game::Scenes::kSceneCatalogCount <= kMaxScenes, "ランチャーのカードの上限を超えている");

        LauncherMenu *m_pMenu = nullptr;
        bool m_AssetsMissing[kMaxScenes] = {};
        /// マウスが動いたときだけホバーで選ぶ（止まったマウスが矢印キーの選択を奪わないように）
        float m_LastMouseX = -1.0f;
        float m_LastMouseY = -1.0f;
    };

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI

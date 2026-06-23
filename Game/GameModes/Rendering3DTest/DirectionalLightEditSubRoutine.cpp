// DirectionalLightEditSubRoutine 実装(Piece 3)。
//
// 本ファイル全体を NORVES_ENABLE_IMGUI で囲む。Game は GLOB_RECURSE で本ファイルを無条件
// 収集するため、OFF 時は空 TU 化して素ビルドを byte-for-byte 不変に保つ。imgui.h は OFF 時
// include パスに無いのでガード内 include 必須。
//
// 文字コード: 日本語ラベルは BOM+UTF-8 ソースに narrow リテラル直書きとし、Game の
// NORVES_ENABLE_IMGUI 有効ブロックで付与する /utf-8(MSVC)で実行文字コードも UTF-8 に
// 揃える。これにより ImGuiModule(同様に /utf-8)と同じ方式で ImGui の const char* へ
// UTF-8 をそのまま渡す(u8"" は C++23 で const char8_t* となり const char* に不適合なので使わない)。

#if defined(NORVES_ENABLE_IMGUI)

#include "DirectionalLightEditSubRoutine.h"

#include "imgui.h"

namespace Game::GameModes
{

    void DirectionalLightEditView::OnImGui()
    {
        // 初期サイズ/位置(初回のみ)。フォントサイズ(解像度連動の DPI スケールが乗って
        // いる)基準で決め、低解像度でも極端に小さくならないようにする。以後はユーザーの
        // リサイズ/移動を尊重する(ImGuiCond_FirstUseEver)。
        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(fontSize * 22.0f, fontSize * 13.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(fontSize * 2.0f, fontSize * 2.0f), ImGuiCond_FirstUseEver);

        // ウィンドウは常に開く。Begin が false(折りたたみ等)なら End して即座に抜ける。
        if (!ImGui::Begin("方向ライト"))
        {
            ImGui::End();
            return;
        }

        // ライトが借用解除済み(null)なら操作 UI は出さない(ウィンドウ枠のみ)。
        if (m_pLight != nullptr)
        {
            // 色(RGB)
            float col[3] = {1.0f, 1.0f, 1.0f};
            m_pLight->GetLightColor(col[0], col[1], col[2]);
            if (ImGui::ColorEdit3("色", col))
            {
                m_pLight->SetLightColor(col[0], col[1], col[2]);
            }

            // 強度
            float intensity = m_pLight->GetIntensity();
            if (ImGui::SliderFloat("強度", &intensity, 0.0f, 10.0f))
            {
                m_pLight->SetIntensity(intensity);
            }

            // 方向(XYZ)
            float dir[3] = {0.0f, -1.0f, 0.0f};
            m_pLight->GetLightDirection(dir[0], dir[1], dir[2]);
            if (ImGui::SliderFloat3("方向", dir, -1.0f, 1.0f))
            {
                m_pLight->SetLightDirection(dir[0], dir[1], dir[2]);
            }
        }

        ImGui::End();
    }

    void DirectionalLightEditSubRoutine::Enter(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        (void)ctx;
        NorvesLib::Modules::Gui::RegisterImGuiView(&m_View);
    }

    void DirectionalLightEditSubRoutine::Leave(NorvesLib::Core::GameMode::GameModeContext& ctx)
    {
        (void)ctx;
        NorvesLib::Modules::Gui::UnregisterImGuiView(&m_View);
    }

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI

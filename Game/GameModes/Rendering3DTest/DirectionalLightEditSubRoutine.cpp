// DirectionalLightEditSubRoutine 実装。
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
#include "SkySunControl.h"

#include "imgui.h"

namespace Game::GameModes
{

    void DirectionalLightEditView::OnImGui()
    {
        // 初期サイズ/位置(初回のみ)。フォントサイズ(解像度連動の DPI スケールが乗って
        // いる)基準で決め、低解像度でも極端に小さくならないようにする。以後はユーザーの
        // リサイズ/移動を尊重する(ImGuiCond_FirstUseEver)。
        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(fontSize * 22.0f, fontSize * 15.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(fontSize * 2.0f, fontSize * 2.0f), ImGuiCond_FirstUseEver);

        // ウィンドウは常に開く。Begin が false(折りたたみ等)なら End して即座に抜ける。
        if (!ImGui::Begin("空の太陽"))
        {
            ImGui::End();
            return;
        }

        // 操作が借用解除済み(null)なら操作 UI は出さない(ウィンドウ枠のみ)。
        if (m_pController != nullptr)
        {
            // 仰角・方位(度)。矢印キーと同じ操作の角度へ書く。
            float altitude = 0.0f;
            float azimuth = 0.0f;
            ConvertLightControllerAnglesToSkySun(m_pController->GetYaw(), m_pController->GetPitch(),
                                                 altitude, azimuth);
            const bool bAltitudeChanged = ImGui::SliderFloat("仰角 (度)", &altitude, 0.0f, 90.0f);
            const bool bAzimuthChanged = ImGui::SliderFloat("方位 (度)", &azimuth, -180.0f, 180.0f);
            if (bAltitudeChanged || bAzimuthChanged)
            {
                float yaw = 0.0f;
                float pitch = 0.0f;
                ConvertSkySunToLightControllerAngles(altitude, azimuth, yaw, pitch);
                m_pController->SetDirection(yaw, pitch);
            }
        }

        // 自動露出。切ると下の手動露出(EV100)で明るさを合わせる。手動露出の値は、自動のときも
        // 最初の測定が出るまでの露出に使われる。
        if (m_pAutoExposure != nullptr)
        {
            ImGui::Checkbox("自動露出", m_pAutoExposure);
        }
        // アンチエイリアシング。切ると FXAA になる。
        if (m_pTemporalAA != nullptr)
        {
            ImGui::Checkbox("TAA（切ると FXAA）", m_pTemporalAA);
        }
        // レンズの効果（画面の端ほど強い色収差と、明るいブルームに浮くレンズダート）。
        if (m_pLensEffects != nullptr)
        {
            ImGui::Checkbox("レンズの効果（色収差・ダート）", m_pLensEffects);
        }
        // 見た目の3D LUT（暖かみのある映画調）。
        if (m_pLookLut != nullptr)
        {
            ImGui::Checkbox("見た目のLUT（暖かみのある映画調）", m_pLookLut);
        }
        // 自動露出の測定(RenderThread が読み戻した値)。手動のときも測定は続くので表示する。
        if (m_pAutoExposureMeasurement != nullptr)
        {
            if (m_pAutoExposureMeasurement->bValid)
            {
                ImGui::Text("目標 EV100: %.2f", m_pAutoExposureMeasurement->TargetEV100);
                ImGui::Text("順応後 EV100: %.2f", m_pAutoExposureMeasurement->AdaptedEV100);
            }
            else
            {
                ImGui::TextUnformatted("自動露出の測定はまだありません");
            }
        }
        if (m_pExposureEV100 != nullptr)
        {
            ImGui::SliderFloat("手動露出 EV100", m_pExposureEV100, 6.0f, 17.0f, "%.1f");
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

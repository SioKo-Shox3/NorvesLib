#pragma once

// DirectionalLightEditSubRoutine — Rendering3DTest に併走する空の太陽の編集 ImGui ビュー。
//
// Rendering3DTestRoutine::Enter が空の太陽の操作(LightController)を設定した直後にこの SubRoutine を
// ControllerRef.RequestPushSubRoutine() で現在の段(=Rendering3DTest 自身)へ積む。push は
// 同一ドレイン内で Enter() され、その中で view を RegisterImGuiView() する。以後 ImGuiModule::Tick
// が毎フレーム view->OnImGui() を呼び、ImGui ウィンドウから空の太陽の仰角・方位を編集する。
// 編集は矢印キーと同じ LightController の角度へ書き、Rendering3DTest の Tick が空の設定へ写す。
//
// 寿命: view(DirectionalLightEditView)は本 SubRoutine が値で保持し、SubRoutine 破棄まで生存する。
// view は ImGui モジュールへ借用ポインタで登録するため、Leave() で必ず UnregisterImGuiView() し、
// view 破棄前に登録を外す。LightController は Rendering3DTest のデータが値で持ち、Routine 生存中
// ずっと有効なので、SubRoutine は LightController を借用ポインタで保持する。
//
// 依存ガード: 本ファイル全体を NORVES_ENABLE_IMGUI で囲む。Game は GLOB_RECURSE で本ファイルを
// 無条件収集するため、OFF 時は空 TU 化して素ビルドを byte-for-byte 不変に保つ(IImGuiView.h は
// OFF 時 include パスに無いのでガード内 include 必須)。

#if defined(NORVES_ENABLE_IMGUI)

#include "ImGuiModule/IImGuiView.h"
#include "Core/Public/GameMode/ISubRoutine.h"
#include "Core/Public/Input/LightController.h"
#include "Core/Public/Rendering/AutoExposure.h"

namespace Game::GameModes
{

    /**
     * @brief 空の太陽(仰角/方位)を編集する ImGui ビュー
     *
     * OnImGui() で ImGui ウィンドウを開き、借用した LightController の角度を空の太陽の
     * 仰角・方位として取得/設定する。空の設定への反映は Rendering3DTest の Tick が行う。
     */
    class DirectionalLightEditView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        /**
         * @brief コンストラクタ(LightController を借用)
         * @param controller 空の太陽の操作(非所有・呼び出し側が寿命を持つ)
         * @param exposureEV100 手動露出 EV100 の置き場(非所有・呼び出し側が寿命を持つ)
         * @param autoExposure 自動露出の有無の置き場(非所有・呼び出し側が寿命を持つ)
         * @param autoExposureMeasurement 表示する自動露出の測定(非所有・呼び出し側が寿命を持つ)
         * @param temporalAA TAA の有無の置き場(非所有・呼び出し側が寿命を持つ。切ると FXAA)
         */
        explicit DirectionalLightEditView(NorvesLib::Core::Input::LightController* controller,
                                          float* exposureEV100,
                                          bool* autoExposure,
                                          const NorvesLib::Core::Rendering::AutoExposureMeasurement* autoExposureMeasurement,
                                          bool* temporalAA)
            : m_pController(controller), m_pExposureEV100(exposureEV100), m_pAutoExposure(autoExposure),
              m_pAutoExposureMeasurement(autoExposureMeasurement), m_pTemporalAA(temporalAA)
        {
        }

        /**
         * @brief このフレームの ImGui UI を構築する(GameThread)
         */
        void OnImGui() override;

        /**
         * @brief ビュー名(デバッグ/ログ用)
         */
        const char* GetViewName() const override
        {
            return "DirectionalLightEdit";
        }

    private:
        // 空の太陽の操作(借用・非所有)
        NorvesLib::Core::Input::LightController* m_pController = nullptr;
        // 手動露出 EV100 の置き場(借用・非所有)。Rendering3DTest の Tick がカメラへ写す。
        float* m_pExposureEV100 = nullptr;
        // 自動露出の有無の置き場(借用・非所有)。Rendering3DTest の Tick がカメラの露出の方式へ写す。
        bool* m_pAutoExposure = nullptr;
        // 自動露出の測定(借用・非所有)。Rendering3DTest の Tick が統計のスナップショットから写す。
        const NorvesLib::Core::Rendering::AutoExposureMeasurement* m_pAutoExposureMeasurement = nullptr;
        // TAA の有無の置き場(借用・非所有)。Rendering3DTest の Tick がカメラのアンチエイリアシングへ写す。
        bool* m_pTemporalAA = nullptr;
    };

    /**
     * @brief 空の太陽の編集ビューを併走させる SubRoutine
     *
     * Enter() で view を RegisterImGuiView()、Leave() で UnregisterImGuiView() する。
     * view は値メンバで保持し、SubRoutine と寿命を束ねる。
     */
    class DirectionalLightEditSubRoutine final : public NorvesLib::Core::GameMode::ISubRoutine
    {
    public:
        /**
         * @brief コンストラクタ(空の太陽の操作を借用)
         * @param controller 空の太陽の操作(非所有・Rendering3DTest が所有)
         * @param exposureEV100 手動露出 EV100 の置き場(非所有・Rendering3DTest が所有)
         * @param autoExposure 自動露出の有無の置き場(非所有・Rendering3DTest が所有)
         * @param autoExposureMeasurement 表示する自動露出の測定(非所有・Rendering3DTest が所有)
         * @param temporalAA TAA の有無の置き場(非所有・Rendering3DTest が所有)
         */
        explicit DirectionalLightEditSubRoutine(NorvesLib::Core::Input::LightController* controller,
                                                float* exposureEV100,
                                                bool* autoExposure,
                                                const NorvesLib::Core::Rendering::AutoExposureMeasurement* autoExposureMeasurement,
                                                bool* temporalAA)
            : m_View(controller, exposureEV100, autoExposure, autoExposureMeasurement, temporalAA)
        {
        }

        /**
         * @brief 段に積まれたとき: view を ImGui モジュールへ登録する
         */
        void Enter(NorvesLib::Core::GameMode::GameModeContext& ctx) override;

        /**
         * @brief 段から取り除かれるとき: view の登録を解除する
         */
        void Leave(NorvesLib::Core::GameMode::GameModeContext& ctx) override;

        /**
         * @brief デバッグ用の名前
         */
        const char* DebugName() const override
        {
            return "DirectionalLightEditSubRoutine";
        }

    private:
        // 併走 view。SubRoutine が値で保持し寿命を束ねる(登録は借用ポインタ)。
        DirectionalLightEditView m_View;
    };

} // namespace Game::GameModes

#endif // NORVES_ENABLE_IMGUI

#pragma once

// DirectionalLightEditSubRoutine — Rendering3DTest に併走する方向ライト編集 ImGui ビュー
// (Piece 3)。
//
// Rendering3DTestRoutine::Enter が方向ライト(LightComponent)生成直後にこの SubRoutine を
// ControllerRef.RequestPushSubRoutine() で現在の段(=Rendering3DTest 自身)へ積む。push は
// 同一ドレイン内で Enter() され、その中で view を RegisterImGuiView() する。以後 ImGuiModule::Tick
// が毎フレーム view->OnImGui() を呼び、ImGui ウィンドウから方向ライトの色/強度/方向を編集する。
//
// 寿命: view(DirectionalLightEditView)は本 SubRoutine が値で保持し、SubRoutine 破棄まで生存する。
// view は ImGui モジュールへ借用ポインタで登録するため、Leave() で必ず UnregisterImGuiView() し、
// view 破棄前に登録を外す。LightComponent は Rendering3DTest が所有し、Routine 生存中ずっと有効
// (Leave で null 化)なので、SubRoutine は LightComponent を借用ポインタで保持する。
//
// 依存ガード: 本ファイル全体を NORVES_ENABLE_IMGUI で囲む。Game は GLOB_RECURSE で本ファイルを
// 無条件収集するため、OFF 時は空 TU 化して素ビルドを byte-for-byte 不変に保つ(IImGuiView.h は
// OFF 時 include パスに無いのでガード内 include 必須)。

#if defined(NORVES_ENABLE_IMGUI)

#include "ImGuiModule/IImGuiView.h"
#include "Core/Public/GameMode/ISubRoutine.h"
#include "Core/Public/Component/LightComponent.h"

namespace Game::GameModes
{

    /**
     * @brief 方向ライト(色/強度/方向)を編集する ImGui ビュー
     *
     * OnImGui() で ImGui ウィンドウを開き、借用した LightComponent のプロパティを
     * 取得/設定する。LightComponent の各 Setter は MarkRenderStateDirty を呼ぶため、
     * 編集は次フレームのスナップショット同期でレンダリングへ反映される。
     */
    class DirectionalLightEditView final : public NorvesLib::Modules::Gui::IImGuiView
    {
    public:
        /**
         * @brief コンストラクタ(LightComponent を借用)
         * @param light 編集対象の方向ライト(非所有・呼び出し側が寿命を持つ)
         */
        explicit DirectionalLightEditView(NorvesLib::Core::Component::LightComponent* light)
            : m_pLight(light)
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
        // 編集対象の方向ライト(借用・非所有)
        NorvesLib::Core::Component::LightComponent* m_pLight = nullptr;
    };

    /**
     * @brief 方向ライト編集ビューを併走させる SubRoutine
     *
     * Enter() で view を RegisterImGuiView()、Leave() で UnregisterImGuiView() する。
     * view は値メンバで保持し、SubRoutine と寿命を束ねる。
     */
    class DirectionalLightEditSubRoutine final : public NorvesLib::Core::GameMode::ISubRoutine
    {
    public:
        /**
         * @brief コンストラクタ(編集対象の方向ライトを借用)
         * @param light 方向ライト(非所有・Rendering3DTest が所有)
         */
        explicit DirectionalLightEditSubRoutine(NorvesLib::Core::Component::LightComponent* light)
            : m_View(light)
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

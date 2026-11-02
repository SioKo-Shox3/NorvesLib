#pragma once

// ImGuiViewRegistry.h — モジュール内部 view レジストリのアクセサ宣言(Piece 2)。
//
// 公開 API(RegisterImGuiView/UnregisterImGuiView)は IImGuiView.h に宣言する。本ヘッダは
// ImGuiModule::Tick が登録済み view を反復するための内部アクセサだけを足す(モジュール内
// Private 専用。Game へは公開しない)。本ヘッダは imgui 非依存(IImGuiView/CoreTypes のみ)。

#include "ImGuiModule/IImGuiView.h"

#include "CoreTypes.h"

namespace NorvesLib::Modules::Gui
{
    /**
     * @brief 登録済み view の借用ポインタ配列を返す(GameThread・モジュール内部用)
     *
     * ImGuiModule::Tick が NewFrame と Render の間で反復し各 view->OnImGui() を呼ぶ。返す
     * 配列は非所有(借用)で、寿命は登録側が持つ。GameThread 単一スレッド前提でロック不要。
     */
    Core::Container::VariableArray<IImGuiView *> &GetRegisteredImGuiViews();

    /**
     * @brief AddImGuiDisplayText で足された文字（UTF-8 の連結）を返す（モジュール内部用）
     */
    const Core::Container::AnsiString &GetImGuiDisplayText();

    // GameThread 専用。初期化後は画素の写しを解放し、描画可否だけを問い合わせに残す。
    struct ImGuiImageRegistration
    {
        uint64_t Id = 0;
        uint32_t Width = 0;
        uint32_t Height = 0;
        Core::Container::VariableArray<uint8_t> Pixels;
        bool bReady = false;
    };

    /** @brief 登録画像の表を返す（GameThread・モジュール内部用） */
    Core::Container::VariableArray<ImGuiImageRegistration> &GetRegisteredImGuiImages();

    /** @brief Initialize 開始を記録し、以後の画像登録を閉じる（GameThread 専用） */
    void BeginImGuiImageInitialization();
} // namespace NorvesLib::Modules::Gui

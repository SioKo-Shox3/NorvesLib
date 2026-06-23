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
} // namespace NorvesLib::Modules::Gui

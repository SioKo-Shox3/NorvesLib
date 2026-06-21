#pragma once

#include "Module/ModuleRegistry.h"

// DummyRenderModule.h — 描画参加するダミーモジュールの最小公開面(第1段1C-ii)。
//
// DummyModule(描画なし)とは別に、IModule + IRenderModule を実装した描画ダミーを
// 別 static lib(NorvesModule_Dummy)側で生成し、Core 在駐の ModuleRegistry へ明示
// 登録する自由関数だけを公開する。具象 DummyRenderModule / DummyOverlayPass は
// Private 側に隠す(公開面は IModule* の借用ポインタのみ)。
//
// CLI ゲート(--dummy-overlay)が指定されたときだけ Game がこの自由関数を呼ぶ。
// フラグ無しの通常起動では誰も呼ばず、overlay seam は完全 no-op を保つ。
namespace NorvesLib::Core::Module
{
    /**
     * @brief 描画ダミーモジュールを生成し registry へ登録する(所有は registry が持つ)
     * @return 登録された IModule の借用ポインタ(重複時は既存登録)
     *
     * 戻り IModule* は registry 生存かつ当該モジュール Uninstall まで有効な借用。
     * このモジュールは IRenderModule も実装し、GetOverlayPass() で DummyOverlayPass を
     * 返す(所有はモジュール)。overlay seam がそのパスを Initialize/Setup/Execute する。
     */
    IModule *RegisterDummyRenderModule(ModuleRegistry &registry);
} // namespace NorvesLib::Core::Module

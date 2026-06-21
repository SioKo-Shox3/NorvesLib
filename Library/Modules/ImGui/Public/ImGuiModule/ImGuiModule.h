#pragma once

#include "Module/ModuleRegistry.h"

// ImGuiModule.h — Game/Test が見る最小公開面(第2段 B-i)。
//
// ImGui デバッグオーバーレイを別 static lib(NorvesModule_ImGui)側で生成し、
// Core 在駐の ModuleRegistry へ明示登録する自由関数だけを公開する。具象
// ImGuiModule / ImGuiOverlayPass / ImDrawData スナップショットは Private 側に
// 隠す(公開面は IModule* の借用ポインタのみ・imgui 型は一切露出しない)。
//
// この自由関数を Game が明示参照することで、別 lib の TU がリンクへ引き込まれ
// dead-strip(/OPT:REF)で落ちない(decision② の本実証)。CLI ゲート(--imgui)が
// 指定されたときだけ Game がこの自由関数を呼ぶ。フラグ無しの通常起動では誰も
// 呼ばず、overlay seam は完全 no-op(F1 描画 baseline 不変)を保つ。
//
// 本ヘッダは NORVES_ENABLE_IMGUI ビルドでのみ意味を持つが、宣言自体は常に存在
// させ、Game 側のゲート分岐をシンプルに保つ(定義側 TU が OFF 時は空になる)。
namespace NorvesLib::Core::Module
{
    /**
     * @brief ImGui モジュールを生成し registry へ登録する(所有は registry が持つ)
     * @return 登録された IModule の借用ポインタ(重複時は既存登録)
     *
     * 戻り IModule* は registry 生存かつ当該モジュール Uninstall まで有効な借用。
     * 長期保持せず必要時 FindModule(Identity("NorvesImGuiModule")) で再取得すること。
     *
     * このモジュールは IRenderModule も実装し、GetOverlayPass() で ImGuiOverlayPass を
     * 返す(所有はモジュール)。overlay seam がそのパスを Initialize/Setup/Execute する。
     * ImGui コンテキストの生成・NewFrame/Render・ImDrawData スナップショットは
     * モジュールが GameThread で駆動し、RenderThread はスナップショットのみ読む。
     */
    IModule *RegisterImGuiModule(ModuleRegistry &registry);
} // namespace NorvesLib::Core::Module

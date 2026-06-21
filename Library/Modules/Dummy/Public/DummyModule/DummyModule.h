#pragma once

#include "Module/ModuleRegistry.h"

// DummyModule.h — Game/Test が見る最小公開面。
//
// ダミーモジュールを別 static lib(NorvesModule_Dummy)側で生成し、Core 在駐の
// ModuleRegistry へ明示登録する自由関数だけを公開する。具象 DummyModule 型は
// Private 側に隠す(公開面は IModule* の借用ポインタのみ)。
//
// この自由関数を Test/Game が明示参照することで、別 lib の TU がリンクへ引き込まれ
// dead-strip(/OPT:REF)で落ちないことを実証する(decision② の早期検証)。
namespace NorvesLib::Core::Module
{
    /**
     * @brief ダミーモジュールを生成し registry へ登録する(所有は registry が持つ)
     * @return 登録された IModule の借用ポインタ(重複時は既存登録・null 不可の自明生成)
     *
     * 戻り IModule* は registry 生存かつ当該モジュール Uninstall まで有効な借用。
     * 長期保持せず必要時 FindModule(Identity("NorvesDummyModule")) で再取得すること。
     */
    IModule *RegisterDummyModule(ModuleRegistry &registry);
} // namespace NorvesLib::Core::Module

// DummyModuleLinkTest — 別 static lib(NorvesModule_Dummy)が Core 境界を越えて
// (a)リンクでき (b)/OPT:REF で落ちず (c)実行時に Core 在駐 ModuleRegistry へ
// 登録・全寿命駆動できることを実証する自動テスト(decision② の早期検証)。
//
// RegisterDummyModule を明示参照することで、別 lib の TU がリンクへ引き込まれ
// dead-strip されない(= FindModule で同一借用が引ける)ことを確かめる。
//
// Engine& 問題: 第1段A の ModuleRegistryTest と同方式。default 構築した Engine を
// 意図的にリークし(~Engine() の重いテアダウンを走らせない)その参照だけを渡す。
// ダミーモジュールはこの参照をデリファレンスしないため安全。

#include "DummyModule/DummyModule.h"
#include "Module/ModuleRegistry.h"
#include "Module/IModule.h"
#include "Engine/Engine.h"
#include "CoreTypes.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;

namespace
{
    // ~Engine() を走らせないために意図的にリークした Engine への参照を返す。
    Engine::Engine &LeakedEngineRef()
    {
        static Engine::Engine *s_Engine = new Engine::Engine();
        return *s_Engine;
    }
} // namespace

int main()
{
    std::cout << "=== DummyModuleLinkTest ===" << std::endl;

    const Identity dummyId("NorvesDummyModule");

    // (a)(b): 別 lib の RegisterDummyModule を明示参照 → リンクへ引き込まれる
    //         (dead-strip されていれば未解決シンボルでリンク失敗するはず)。
    std::cout << "[Test] cross-lib register: NorvesModule_Dummy -> Core registry" << std::endl;
    ModuleRegistry &reg = GetModuleRegistry();
    IModule *m = RegisterDummyModule(reg);
    assert(m != nullptr);

    // 登録された借用が Core 在駐レジストリから同一ポインタで引ける(別 lib TU が
    // 確かにレジストリへ登録された = dead-strip されていない証明)。
    assert(reg.FindModule(dummyId) == m);
    std::cout << "  OK" << std::endl;

    // 描画モジュールではない(IRenderModule 非実装 = overlay 集合に現れない)。
    std::cout << "[Test] dummy is a non-render service module" << std::endl;
    assert(reg.GetRenderModules().size() == 0);
    std::cout << "  OK" << std::endl;

    // (c): 別 lib 越しに全寿命を駆動する(Install→Initialize→Tick→Shutdown)。
    std::cout << "[Test] drive full lifecycle across lib boundary" << std::endl;
    bool installed = reg.InstallAll(LeakedEngineRef());
    assert(installed);
    // モジュール単体の到達フェーズは Initialized(registry が Running になる)。
    // 第1段A の ModuleRegistryTest と同じ意味論に揃える。
    assert(m->GetPhase() == EModulePhase::Initialized);

    // registry が Running の時のみ Tick が走る。Running でなければ何も起きず、
    // 後続の Shutdown 経路の駆動だけが残る(別 lib 越し駆動の実証)。
    reg.TickAll(0.016f);

    reg.ShutdownAll(LeakedEngineRef());
    assert(m->GetPhase() == EModulePhase::Uninstalled);
    std::cout << "  OK" << std::endl;

    std::cout << "=== All DummyModuleLinkTest passed ===" << std::endl;
    return 0;
}

// ModuleRegistryTest — モジュール枠組み(ModuleRegistry/IModule/IRenderModule)の
// 純ロジック単体テスト。描画/RHI/完全初期化済み Engine を要さない。
//
// Engine& 問題: IModule::Install/Uninstall は Engine& を取るが、本テストのモジュールは
// その参照を一切デリファレンスしない設計にする。テストは default 構築した Engine を
// 「意図的にリーク」して(~Engine() の重いテアダウンを走らせない)その参照だけを渡す。
// production の API 形状(Engine&)は計画通り維持する。

#include "Module/ModuleRegistry.h"
#include "Module/IModule.h"
#include "Module/IRenderModule.h"
#include "Engine/Engine.h"
#include "CoreTypes.h"

#include <cassert>
#include <iostream>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Module;

namespace
{
    // 各モジュールが寿命イベントを刻む共有ログ(呼び出し順序の検証に使う)。
    VariableArray<Container::String> g_CallLog;

    void Log(const char *name, const char *event)
    {
        Container::String entry(name);
        entry += ":";
        entry += event;
        g_CallLog.push_back(entry);
    }

    // 描画なしのサービス型テストモジュール(Audio 型の最小経路を先行実証)。
    // Engine& は保持/参照しない。
    class TestModule : public IModule
    {
    public:
        TestModule(const char *name, bool bInstallOk = true, bool bInitOk = true)
            : m_Name(name), m_Id(name), m_bInstallOk(bInstallOk), m_bInitOk(bInitOk)
        {
        }

        Identity GetModuleId() const override { return m_Id; }
        const char *GetName() const override { return m_Name; }

        bool Install(Engine::Engine & /*engine*/) override
        {
            Log(m_Name, "Install");
            return m_bInstallOk;
        }

        bool Initialize() override
        {
            Log(m_Name, "Initialize");
            return m_bInitOk;
        }

        void Tick(float /*deltaTime*/) override { Log(m_Name, "Tick"); }

        void Shutdown() override { Log(m_Name, "Shutdown"); }

        void Uninstall(Engine::Engine & /*engine*/) override { Log(m_Name, "Uninstall"); }

    private:
        const char *m_Name;
        Identity m_Id;
        bool m_bInstallOk;
        bool m_bInitOk;
    };

    // 描画参加するテストモジュール(IModule + IRenderModule 両基底を継承)。
    class TestRenderModule : public IModule, public IRenderModule
    {
    public:
        explicit TestRenderModule(const char *name) : m_Name(name), m_Id(name) {}

        Identity GetModuleId() const override { return m_Id; }
        const char *GetName() const override { return m_Name; }
        bool Install(Engine::Engine & /*engine*/) override { return true; }
        bool Initialize() override { return true; }
        void Shutdown() override {}

        // 描画なしのダミー(GetOverlayPass は null を返してよい契約)。
        Rendering::IViewPass *GetOverlayPass() override { return nullptr; }

    private:
        const char *m_Name;
        Identity m_Id;
    };

    // ~Engine() を走らせないために意図的にリークした Engine への参照を返す。
    // テストモジュールはこの参照をデリファレンスしないため安全。
    Engine::Engine &LeakedEngineRef()
    {
        static Engine::Engine *s_Engine = new Engine::Engine();
        return *s_Engine;
    }

    void ResetLog() { g_CallLog.clear(); }

    bool LogEquals(size_t index, const char *expected)
    {
        return index < g_CallLog.size() && g_CallLog[index] == Container::String(expected);
    }
} // namespace

static void TestRegisterOwnershipAndDuplicate()
{
    std::cout << "[Test] Register: ownership move + duplicate detection" << std::endl;
    ModuleRegistry registry;

    IModule *a = registry.Register(MakeUnique<TestModule>("Alpha"));
    IModule *b = registry.Register(MakeUnique<TestModule>("Beta"));
    assert(a != nullptr);
    assert(b != nullptr);
    assert(a != b);

    // FindModule で借用が引ける(所有は registry が持つ)。
    assert(registry.FindModule(Identity("Alpha")) == a);
    assert(registry.FindModule(Identity("Beta")) == b);
    assert(registry.FindModule(Identity("Missing")) == nullptr);

    // 同名 Identity 重複は既存を返し、新規は破棄される(先勝ち)。
    IModule *dup = registry.Register(MakeUnique<TestModule>("Alpha"));
    assert(dup == a);

    // null 登録は nullptr を返す。
    IModule *nullReg = registry.Register(Container::TUniquePtr<IModule>());
    assert(nullReg == nullptr);

    std::cout << "  OK" << std::endl;
}

static void TestInstallAllNormalProgression()
{
    std::cout << "[Test] InstallAll: normal phase progression in order" << std::endl;
    ResetLog();
    ModuleRegistry registry;
    IModule *a = registry.Register(MakeUnique<TestModule>("A"));
    IModule *b = registry.Register(MakeUnique<TestModule>("B"));

    bool ok = registry.InstallAll(LeakedEngineRef());
    assert(ok);
    assert(a->GetPhase() == EModulePhase::Initialized);
    assert(b->GetPhase() == EModulePhase::Initialized);

    // 順序: A.Install, B.Install(フェーズ1) → A.Initialize, B.Initialize(フェーズ2)。
    assert(LogEquals(0, "A:Install"));
    assert(LogEquals(1, "B:Install"));
    assert(LogEquals(2, "A:Initialize"));
    assert(LogEquals(3, "B:Initialize"));
    assert(g_CallLog.size() == 4);

    // 二重 InstallAll は冪等(再進行しない・Running なら true)。
    ResetLog();
    bool again = registry.InstallAll(LeakedEngineRef());
    assert(again);
    assert(g_CallLog.size() == 0);

    std::cout << "  OK" << std::endl;
}

static void TestInstallAllRollbackOnInstallFailure()
{
    std::cout << "[Test] InstallAll: reverse rollback on Install failure" << std::endl;
    ResetLog();
    ModuleRegistry registry;
    registry.Register(MakeUnique<TestModule>("A"));
    registry.Register(MakeUnique<TestModule>("B", /*bInstallOk=*/false));
    registry.Register(MakeUnique<TestModule>("C"));

    bool ok = registry.InstallAll(LeakedEngineRef());
    assert(!ok);

    // A.Install 成功 → B.Install 失敗 → A を逆順ロールバック(Uninstall)。
    // C は到達しない。B 自身は未 Install のためロールバック対象外。
    assert(LogEquals(0, "A:Install"));
    assert(LogEquals(1, "B:Install"));
    assert(LogEquals(2, "A:Uninstall"));
    assert(g_CallLog.size() == 3);

    std::cout << "  OK" << std::endl;
}

static void TestInstallAllRollbackOnInitializeFailure()
{
    std::cout << "[Test] InstallAll: reverse rollback on Initialize failure" << std::endl;
    ResetLog();
    ModuleRegistry registry;
    registry.Register(MakeUnique<TestModule>("A"));
    registry.Register(MakeUnique<TestModule>("B", /*bInstallOk=*/true, /*bInitOk=*/false));

    bool ok = registry.InstallAll(LeakedEngineRef());
    assert(!ok);

    // フェーズ1: A.Install, B.Install。フェーズ2: A.Initialize OK, B.Initialize 失敗。
    // 全 Install 済みを逆順後退: B(Initialize 未到達=Installed→Uninstall),
    // A(Initialized→Shutdown→Uninstall)。
    assert(LogEquals(0, "A:Install"));
    assert(LogEquals(1, "B:Install"));
    assert(LogEquals(2, "A:Initialize"));
    assert(LogEquals(3, "B:Initialize"));
    // 逆順ロールバック: B は Installed のまま → Uninstall のみ。
    assert(LogEquals(4, "B:Uninstall"));
    // A は Initialized → Shutdown → Uninstall。
    assert(LogEquals(5, "A:Shutdown"));
    assert(LogEquals(6, "A:Uninstall"));
    assert(g_CallLog.size() == 7);

    std::cout << "  OK" << std::endl;
}

static void TestTickAllOnlyWhenRunning()
{
    std::cout << "[Test] TickAll: only when Running + in order" << std::endl;
    ResetLog();
    ModuleRegistry registry;
    registry.Register(MakeUnique<TestModule>("A"));
    registry.Register(MakeUnique<TestModule>("B"));

    // Install 前(Created)は Tick されない。
    registry.TickAll(0.016f);
    assert(g_CallLog.size() == 0);

    registry.InstallAll(LeakedEngineRef());
    ResetLog();

    registry.TickAll(0.016f);
    // 登録順に Tick。
    assert(LogEquals(0, "A:Tick"));
    assert(LogEquals(1, "B:Tick"));
    assert(g_CallLog.size() == 2);

    std::cout << "  OK" << std::endl;
}

static void TestShutdownAllReverseOrder()
{
    std::cout << "[Test] ShutdownAll: reverse order + idempotent" << std::endl;
    ResetLog();
    ModuleRegistry registry;
    registry.Register(MakeUnique<TestModule>("A"));
    registry.Register(MakeUnique<TestModule>("B"));
    registry.Register(MakeUnique<TestModule>("C"));
    registry.InstallAll(LeakedEngineRef());
    ResetLog();

    registry.ShutdownAll(LeakedEngineRef());
    // 逆順: C, B, A。各 Shutdown → Uninstall。
    assert(LogEquals(0, "C:Shutdown"));
    assert(LogEquals(1, "C:Uninstall"));
    assert(LogEquals(2, "B:Shutdown"));
    assert(LogEquals(3, "B:Uninstall"));
    assert(LogEquals(4, "A:Shutdown"));
    assert(LogEquals(5, "A:Uninstall"));
    assert(g_CallLog.size() == 6);

    // 二重 ShutdownAll は冪等(何もしない)。
    ResetLog();
    registry.ShutdownAll(LeakedEngineRef());
    assert(g_CallLog.size() == 0);

    std::cout << "  OK" << std::endl;
}

static void TestGetRenderModulesView()
{
    std::cout << "[Test] GetRenderModules: non-owning view of render modules" << std::endl;
    ModuleRegistry registry;
    registry.Register(MakeUnique<TestModule>("Service1"));      // 非描画
    IModule *r1 = registry.Register(MakeUnique<TestRenderModule>("Render1")); // 描画
    registry.Register(MakeUnique<TestModule>("Service2"));      // 非描画
    IModule *r2 = registry.Register(MakeUnique<TestRenderModule>("Render2")); // 描画

    Span<IRenderModule *> renderModules = registry.GetRenderModules();
    // 描画モジュールのみが収集される(登録順)。
    assert(renderModules.size() == 2);

    // ビューの要素が登録した描画モジュールの IRenderModule サブオブジェクトを指す。
    assert(renderModules[0] == dynamic_cast<IRenderModule *>(r1));
    assert(renderModules[1] == dynamic_cast<IRenderModule *>(r2));

    // 契約: GetOverlayPass は null を返してよい。
    assert(renderModules[0]->GetOverlayPass() == nullptr);

    std::cout << "  OK" << std::endl;
}

static void TestGetModuleRegistrySingleton()
{
    std::cout << "[Test] GetModuleRegistry: stable Meyers accessor" << std::endl;
    ModuleRegistry &r1 = GetModuleRegistry();
    ModuleRegistry &r2 = GetModuleRegistry();
    assert(&r1 == &r2);
    std::cout << "  OK" << std::endl;
}

int main()
{
    std::cout << "=== ModuleRegistryTest ===" << std::endl;

    TestRegisterOwnershipAndDuplicate();
    TestInstallAllNormalProgression();
    TestInstallAllRollbackOnInstallFailure();
    TestInstallAllRollbackOnInitializeFailure();
    TestTickAllOnlyWhenRunning();
    TestShutdownAllReverseOrder();
    TestGetRenderModulesView();
    TestGetModuleRegistrySingleton();

    std::cout << "=== All ModuleRegistryTest passed ===" << std::endl;
    return 0;
}

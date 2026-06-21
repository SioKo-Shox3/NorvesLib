#include "Module/ModuleRegistry.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Module
{
    namespace
    {
        constexpr const char *kLogCategory = "Module";
    } // namespace

    IModule *ModuleRegistry::Register(Container::TUniquePtr<IModule> module)
    {
        if (!module)
        {
            NORVES_LOG_ERROR(kLogCategory, "Register called with null module");
            return nullptr;
        }

        const Identity moduleId = module->GetModuleId();

        // 同名 Identity 重複検出: 既存があれば新規は破棄して既存の借用を返す。
        // (登録順=実行順を壊さないため、後勝ち差し替えはせず先勝ちを維持する)
        auto existing = m_ById.find(moduleId);
        if (existing != m_ById.end())
        {
            NORVES_LOG_WARNING(kLogCategory,
                               "Duplicate module id '%s' ignored; keeping existing registration",
                               moduleId.ToString().c_str());
            return existing->second;
        }

        IModule *borrowed = module.get();

        // 描画モジュールなら非所有ビューへ追加(cross-cast: 具象型が両基底を継承)。
        if (IRenderModule *renderModule = dynamic_cast<IRenderModule *>(borrowed))
        {
            m_RenderModules.push_back(renderModule);
        }

        m_ById[moduleId] = borrowed;
        m_Modules.push_back(std::move(module)); // 所有移譲(登録順を保持)

        NORVES_LOG_INFO(kLogCategory, "Registered module '%s'", borrowed->GetName());
        return borrowed;
    }

    void ModuleRegistry::RollbackInstalled(Engine::Engine &engine, size_t count)
    {
        // m_Modules[0, count) のうち到達フェーズに応じて逆順に後退させる。
        for (size_t i = count; i > 0; --i)
        {
            IModule *module = m_Modules[i - 1].get();
            if (!module)
            {
                continue;
            }

            // Initialize 済みなら Shutdown。Install 済みなら UnregisterReflectedTypes→Uninstall。
            if (module->m_Phase == EModulePhase::Initialized ||
                module->m_Phase == EModulePhase::Running)
            {
                module->Shutdown();
                module->m_Phase = EModulePhase::Installed;
            }

            if (module->m_Phase == EModulePhase::Installed)
            {
                module->UnregisterReflectedTypes();
                module->Uninstall(engine);
                module->m_Phase = EModulePhase::Uninstalled;
            }
        }
    }

    bool ModuleRegistry::InstallAll(Engine::Engine &engine)
    {
        // 冪等ガード: 既に Install 系を完了している場合は再進行しない。
        if (m_Phase != EModulePhase::Created)
        {
            NORVES_LOG_WARNING(kLogCategory, "InstallAll ignored; registry phase is not Created");
            return m_Phase == EModulePhase::Running;
        }

        // フェーズ1: 全モジュールを Install→RegisterReflectedTypes(登録順)。
        for (size_t i = 0; i < m_Modules.size(); ++i)
        {
            IModule *module = m_Modules[i].get();
            if (!module)
            {
                continue;
            }

            if (!module->Install(engine))
            {
                NORVES_LOG_ERROR(kLogCategory, "Module '%s' Install failed; rolling back",
                                 module->GetName());
                // 当該モジュールは未 Install のまま。直前まで(i 個)をロールバック。
                RollbackInstalled(engine, i);
                return false;
            }
            module->m_Phase = EModulePhase::Installed;
            module->RegisterReflectedTypes();
        }

        // フェーズ2: 全モジュールを Initialize(登録順)。
        for (size_t i = 0; i < m_Modules.size(); ++i)
        {
            IModule *module = m_Modules[i].get();
            if (!module)
            {
                continue;
            }

            if (!module->Initialize())
            {
                NORVES_LOG_ERROR(kLogCategory, "Module '%s' Initialize failed; rolling back",
                                 module->GetName());
                // [0, i) は Initialized、当該 i は Installed のまま。全 Install 済みを後退。
                RollbackInstalled(engine, m_Modules.size());
                return false;
            }
            module->m_Phase = EModulePhase::Initialized;
        }

        m_Phase = EModulePhase::Running;
        NORVES_LOG_INFO(kLogCategory, "InstallAll complete; %zu module(s) running", m_Modules.size());
        return true;
    }

    void ModuleRegistry::TickAll(float deltaTime)
    {
        // Running 時のみ・登録順。
        if (m_Phase != EModulePhase::Running)
        {
            return;
        }

        for (size_t i = 0; i < m_Modules.size(); ++i)
        {
            IModule *module = m_Modules[i].get();
            if (module)
            {
                module->Tick(deltaTime);
            }
        }
    }

    void ModuleRegistry::ShutdownAll(Engine::Engine &engine)
    {
        // 冪等ガード: 既に Shutdown 済みなら何もしない(二重呼び出し安全)。
        if (m_Phase == EModulePhase::ShuttingDown || m_Phase == EModulePhase::Uninstalled)
        {
            return;
        }
        // Running になっていなくても(Install 途中で停止した等)安全に後退させる。
        m_Phase = EModulePhase::ShuttingDown;

        // 逆順に UnregisterReflectedTypes→Shutdown→Uninstall。
        for (size_t i = m_Modules.size(); i > 0; --i)
        {
            IModule *module = m_Modules[i - 1].get();
            if (!module)
            {
                continue;
            }

            if (module->m_Phase == EModulePhase::Initialized ||
                module->m_Phase == EModulePhase::Running)
            {
                module->Shutdown();
                module->m_Phase = EModulePhase::Installed;
            }

            if (module->m_Phase == EModulePhase::Installed)
            {
                module->UnregisterReflectedTypes();
                module->Uninstall(engine);
                module->m_Phase = EModulePhase::Uninstalled;
            }
        }

        m_Phase = EModulePhase::Uninstalled;
        NORVES_LOG_INFO(kLogCategory, "ShutdownAll complete");
    }

    Container::Span<IRenderModule *> ModuleRegistry::GetRenderModules()
    {
        return Container::Span<IRenderModule *>(m_RenderModules.data(), m_RenderModules.size());
    }

    IModule *ModuleRegistry::FindModule(const Identity &id) const
    {
        auto it = m_ById.find(id);
        if (it != m_ById.end())
        {
            return it->second;
        }
        return nullptr;
    }

    ModuleRegistry &GetModuleRegistry()
    {
        static ModuleRegistry instance;
        return instance;
    }
} // namespace NorvesLib::Core::Module

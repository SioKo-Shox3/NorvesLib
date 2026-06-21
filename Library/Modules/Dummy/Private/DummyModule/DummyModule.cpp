#include "DummyModule/DummyModule.h"

#include "Module/IModule.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

namespace NorvesLib::Core::Module
{
    namespace
    {
        constexpr const char *kLogCategory = "Module";
        constexpr const char *kModuleName = "NorvesDummyModule";

        /**
         * @brief 描画なしのサービス型最小ダミーモジュール(Audio 型の最小経路)
         *
         * IRenderModule は実装しない(GetRenderModules には現れない)。Engine& は
         * 各寿命で一切デリファレンスしない(完全初期化済み Engine を要さない)。
         * dead-strip 早期検証が目的なので、各寿命イベントでログを 1 行だけ残す。
         */
        class DummyModule : public IModule
        {
        public:
            Identity GetModuleId() const override
            {
                return Identity(kModuleName);
            }

            const char *GetName() const override
            {
                return kModuleName;
            }

            bool Install(Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyModule Install");
                return true;
            }

            bool Initialize() override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyModule Initialize");
                return true;
            }

            void Tick(float /*deltaTime*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyModule Tick");
            }

            void Shutdown() override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyModule Shutdown");
            }

            void Uninstall(Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyModule Uninstall");
            }
        };
    } // namespace

    IModule *RegisterDummyModule(ModuleRegistry &registry)
    {
        return registry.Register(Container::MakeUnique<DummyModule>());
    }
} // namespace NorvesLib::Core::Module

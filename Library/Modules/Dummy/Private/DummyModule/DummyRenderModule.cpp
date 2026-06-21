#include "DummyModule/DummyRenderModule.h"
#include "DummyModule/DummyOverlayPass.h"

#include "Module/IModule.h"
#include "Module/IRenderModule.h"
#include "Logging/LogMacros.h"
#include "CoreTypes.h"

namespace NorvesLib::Core::Module
{
    namespace
    {
        constexpr const char *kLogCategory = "Module";
        constexpr const char *kModuleName = "NorvesDummyRenderModule";

        /**
         * @brief 描画参加する最小ダミーモジュール(IModule + IRenderModule)
         *
         * GetOverlayPass() で所有する DummyOverlayPass を借用返しする。overlay seam が
         * そのパスの Initialize(遅延)/Setup/Execute を駆動する。Shutdown は seam では
         * 駆動されない(IRenderModule 契約=モジュール責務)ため本モジュールの Shutdown で
         * パスの Shutdown を呼ぶ。Engine& は各寿命で一切デリファレンスしない。
         */
        class DummyRenderModule final : public IModule, public IRenderModule
        {
        public:
            // --- IModule 識別 ---
            Identity GetModuleId() const override
            {
                return Identity(kModuleName);
            }

            const char *GetName() const override
            {
                return kModuleName;
            }

            // --- IModule 寿命(GameThread 単一スレッド) ---
            bool Install(Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyRenderModule Install");
                return true;
            }

            bool Initialize() override
            {
                // overlay pass の RHI 初期化は seam(録画窓内)で遅延実行されるため
                // ここでは何もしない。Device 等を要する初期化を Game スレッドで行わない。
                NORVES_LOG_INFO(kLogCategory, "DummyRenderModule Initialize");
                return true;
            }

            void Tick(float /*deltaTime*/) override
            {
                // overlay は毎フレーム seam で Execute されるため Tick は何もしない。
            }

            void Shutdown() override
            {
                // seam は overlay の Initialize のみ駆動し Shutdown は駆動しない
                // (IRenderModule 契約=モジュール責務)。RenderThread 静止後・device
                // 生存中に呼ばれる前提でパスのリソースを解放する。
                m_OverlayPass.Shutdown();
                NORVES_LOG_INFO(kLogCategory, "DummyRenderModule Shutdown");
            }

            void Uninstall(Engine::Engine & /*engine*/) override
            {
                NORVES_LOG_INFO(kLogCategory, "DummyRenderModule Uninstall");
            }

            // --- IRenderModule 描画参加 ---
            Rendering::IViewPass *GetOverlayPass() override
            {
                // 借用ポインタ。寿命はモジュール所有(Shutdown まで有効)。
                return &m_OverlayPass;
            }

        private:
            // overlay パスをモジュールが所有する(寿命はモジュールと一致)。
            Modules::Dummy::DummyOverlayPass m_OverlayPass;
        };
    } // namespace

    IModule *RegisterDummyRenderModule(ModuleRegistry &registry)
    {
        return registry.Register(Container::MakeUnique<DummyRenderModule>());
    }
} // namespace NorvesLib::Core::Module

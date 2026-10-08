#pragma once
#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#include <cstdint>
namespace NorvesLib::Core
{
    class World;
}
namespace Game::Debug
{
    // 明示検証sceneが所有し、World破棄前にDetachする。
    class SocketDebugView final : public NorvesLib::Modules::Gui::IImGuiView
    {
      public:
        ~SocketDebugView() override;
        void Attach(NorvesLib::Core::World&, uint64_t socketOwner, uint64_t heldItem);
        void Detach();
        void OnImGui() override;
        const char* GetViewName() const override
        {
            return "Sockets";
        }

      private:
        void SubmitDebugDraw();
        NorvesLib::Core::World* m_World = nullptr;
        uint64_t m_Owner = 0, m_Item = 0;
        bool m_bAxes = true, m_bInvalid = false;
    };
} // namespace Game::Debug
#endif

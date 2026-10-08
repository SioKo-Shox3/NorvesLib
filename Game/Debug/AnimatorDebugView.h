#pragma once
#if defined(NORVES_ENABLE_IMGUI)
#include "ImGuiModule/IImGuiView.h"
#include "Core/Public/Component/AnimatorComponent.h"
namespace NorvesLib::Core
{
    class World;
}
namespace Game::Debug
{
    // 明示的な検証シーンだけがAttachする。Worldの終了前にDetachする。
    class AnimatorDebugView final : public NorvesLib::Modules::Gui::IImGuiView
    {
      public:
        ~AnimatorDebugView() override;
        void Attach(NorvesLib::Core::World&, uint64_t entityObjectId);
        void Detach();
        void OnImGui() override;
        const char* GetViewName() const override
        {
            return "Animator";
        }

      private:
        NorvesLib::Core::World* m_World = nullptr;
        uint64_t m_EntityId = 0;
        bool m_bCompareBounds = false;
        NorvesLib::Core::Component::AnimatorDebugSnapshot m_Snapshot;
    };
} // namespace Game::Debug
#endif

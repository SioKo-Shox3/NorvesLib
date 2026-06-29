#pragma once

#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Object/EntityHandle.h"

namespace Game::Input
{

    class PickingController : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent& event) override;
        void DrawSelection();
        void ClearSelection();

        const char* DebugName() const override
        {
            return "PickingController";
        }

    private:
        void PerformPick(float screenX, float screenY);

        float m_PressX = 0.0f;
        float m_PressY = 0.0f;
        bool m_bLeftPressed = false;
        NorvesLib::Core::EntityHandle m_SelectionHandle = NorvesLib::Core::EntityHandle::Invalid();
    };

} // namespace Game::Input

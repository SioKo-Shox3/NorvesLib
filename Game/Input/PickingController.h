#pragma once

#include "Core/Public/Container/Containers.h"
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
        void PerformBoxSelect(float x0, float y0, float x1, float y1);

        float m_PressX = 0.0f;
        float m_PressY = 0.0f;
        bool m_bLeftPressed = false;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::EntityHandle> m_SelectionHandles;
        bool m_bBoxSelecting = false;
        float m_BoxStartX = 0.0f;
        float m_BoxStartY = 0.0f;
    };

} // namespace Game::Input

#pragma once

#include "Core/Public/Container/Containers.h"
#include "Core/Public/Input/IInputController.h"
#include "Core/Public/Math/GeometryTypes.h"
#include "Core/Public/Object/EntityHandle.h"

namespace Game::Input
{

    class PickingController : public NorvesLib::Core::Input::IInputController
    {
    public:
        bool OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent& event) override;
        bool OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent& event) override;
        void SetFallbackSelectionDepth(float depth);
        void DrawSelection();
        void ClearSelection();

        const char* DebugName() const override
        {
            return "PickingController";
        }

    private:
        void PerformPick(float screenX, float screenY);
        void PerformBoxSelect(float x0, float y0, float x1, float y1);
        void PerformSphereSelect(float centerX, float centerY, float edgeX, float edgeY);
        bool BuildSelectionSphereFromScreen(
            float centerX,
            float centerY,
            float edgeX,
            float edgeY,
            NorvesLib::Math::Sphere& outSphere) const;

        float m_PressX = 0.0f;
        float m_PressY = 0.0f;
        bool m_bLeftPressed = false;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::EntityHandle> m_SelectionHandles;
        bool m_bBoxSelecting = false;
        float m_BoxStartX = 0.0f;
        float m_BoxStartY = 0.0f;
        bool m_bSphereSelecting = false;
        float m_SphereCenterX = 0.0f;
        float m_SphereCenterY = 0.0f;
        NorvesLib::Math::Sphere m_SelectionSphere;
        bool m_bHasSelectionSphere = false;
        float m_FallbackSelectionDepth = 5.0f;
    };

} // namespace Game::Input

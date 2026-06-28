#include "Input/PickingController.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Scene/SceneQuery.h"
#include "Core/Public/Rendering/CameraPicking.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Math/Vector4.h"

namespace
{
    constexpr float ClickThresholdPixels = 4.0f;
    constexpr float RayLength = 1000.0f;
    const NorvesLib::Math::Vector4 SelectionColor(0.1f, 0.85f, 1.0f, 1.0f);
    const NorvesLib::Math::Vector4 RayColor(1.0f, 0.25f, 0.1f, 0.75f);
} // namespace

namespace Game::Input
{

    bool PickingController::OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent& event)
    {
        if (event.Button != NorvesLib::Core::Input::MouseButton::Left)
        {
            return false;
        }

        if (event.Action == NorvesLib::Core::Input::InputAction::Pressed)
        {
            m_PressX = event.PositionX;
            m_PressY = event.PositionY;
            m_bLeftPressed = true;
            return false;
        }

        if (event.Action == NorvesLib::Core::Input::InputAction::Released && m_bLeftPressed)
        {
            m_bLeftPressed = false;

            const float dx = event.PositionX - m_PressX;
            const float dy = event.PositionY - m_PressY;
            if (dx * dx + dy * dy <= ClickThresholdPixels * ClickThresholdPixels)
            {
                PerformPick(event.PositionX, event.PositionY);
            }
        }

        return false;
    }

    void PickingController::DrawSelection() const
    {
        if (m_bHasSelection)
        {
            NorvesLib::Core::GEngine.GetDebugDraw().AddAABB(m_SelectionBounds, SelectionColor);
        }
    }

    void PickingController::ClearSelection()
    {
        m_bHasSelection = false;
    }

    void PickingController::PerformPick(float screenX, float screenY)
    {
        NorvesLib::Core::Engine::Engine* engine = NorvesLib::Core::Engine::GEngine;
        if (engine == nullptr)
        {
            return;
        }

        const NorvesLib::Core::Rendering::CameraProxy& camera =
            engine->GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        if (!camera.IsValid())
        {
            return;
        }

        NorvesLib::Math::Ray ray;
        if (!NorvesLib::Core::Rendering::BuildPickingRay(camera, screenX, screenY, ray))
        {
            return;
        }

        NorvesLib::Core::GEngine.GetDebugDraw().AddLine(
            ray.Origin,
            ray.Origin + ray.Direction * RayLength,
            RayColor);

        NorvesLib::Core::Scene::RaycastHit hit;
        const bool bHit = engine->GetSceneQuery().Raycast(ray, hit);
        if (bHit && hit.HitEntity != nullptr)
        {
            NorvesLib::Math::AABB bounds;
            if (hit.HitEntity->GetWorldAABB(bounds))
            {
                m_SelectionBounds = bounds;
                m_bHasSelection = true;
                return;
            }
        }

        m_bHasSelection = false;
    }

} // namespace Game::Input

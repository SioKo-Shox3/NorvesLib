#include "Input/PickingController.h"

#include "Core/Public/Engine/Engine.h"
#include "Core/Public/Engine/ComponentDataRegistry.h"
#include "Core/Public/Engine/NorvesEngine.h"
#include "Core/Public/Object/Entity.h"
#include "Core/Public/Scene/SceneQuery.h"
#include "Core/Public/Rendering/CameraPicking.h"
#include "Core/Public/Rendering/RenderingCoordinator.h"
#include "Core/Public/Rendering/SceneProxy.h"
#include "Core/Public/Math/GeometryTypes.h"
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

    void PickingController::DrawSelection()
    {
        if (!m_SelectionHandle.IsValid())
        {
            return;
        }

        NorvesLib::Core::Entity* entity =
            NorvesLib::Core::GEngine.GetComponentDataRegistry().ResolveEntity(m_SelectionHandle);
        if (entity == nullptr)
        {
            m_SelectionHandle = NorvesLib::Core::EntityHandle::Invalid();
            return;
        }

        NorvesLib::Math::AABB bounds;
        if (entity->GetWorldAABB(bounds))
        {
            NorvesLib::Core::GEngine.GetDebugDraw().AddAABB(bounds, SelectionColor);
        }
    }

    void PickingController::ClearSelection()
    {
        m_SelectionHandle = NorvesLib::Core::EntityHandle::Invalid();
    }

    void PickingController::PerformPick(float screenX, float screenY)
    {
        m_SelectionHandle = NorvesLib::Core::EntityHandle::Invalid();

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
            m_SelectionHandle = hit.HitEntity->GetEntityHandle();
        }
    }

} // namespace Game::Input

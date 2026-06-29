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
            NorvesLib::Core::Engine::Engine* engine = NorvesLib::Core::Engine::GEngine;
            const bool bShift =
                (engine != nullptr) && engine->GetInputSystem().GetState().IsShiftDown();

            if (bShift)
            {
                m_bBoxSelecting = true;
                m_BoxStartX = event.PositionX;
                m_BoxStartY = event.PositionY;
                m_bLeftPressed = false;
                return true;
            }

            m_bBoxSelecting = false;
            m_bLeftPressed = true;
            m_PressX = event.PositionX;
            m_PressY = event.PositionY;
            return false;
        }

        if (event.Action == NorvesLib::Core::Input::InputAction::Released)
        {
            if (m_bBoxSelecting)
            {
                m_bBoxSelecting = false;
                PerformBoxSelect(m_BoxStartX, m_BoxStartY, event.PositionX, event.PositionY);
                return true;
            }

            if (m_bLeftPressed)
            {
                m_bLeftPressed = false;

                const float dx = event.PositionX - m_PressX;
                const float dy = event.PositionY - m_PressY;
                if (dx * dx + dy * dy <= ClickThresholdPixels * ClickThresholdPixels)
                {
                    PerformPick(event.PositionX, event.PositionY);
                }
            }
        }

        return false;
    }

    void PickingController::DrawSelection()
    {
        if (m_SelectionHandles.empty())
        {
            return;
        }

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::EntityHandle> liveHandles;
        liveHandles.reserve(m_SelectionHandles.size());

        for (const NorvesLib::Core::EntityHandle& handle : m_SelectionHandles)
        {
            if (!handle.IsValid())
            {
                continue;
            }

            NorvesLib::Core::Entity* entity =
                NorvesLib::Core::GEngine.GetComponentDataRegistry().ResolveEntity(handle);
            if (entity == nullptr)
            {
                continue;
            }

            liveHandles.push_back(handle);

            NorvesLib::Math::AABB bounds;
            if (entity->GetWorldAABB(bounds))
            {
                NorvesLib::Core::GEngine.GetDebugDraw().AddAABB(bounds, SelectionColor);
            }
        }

        m_SelectionHandles = liveHandles;
    }

    void PickingController::ClearSelection()
    {
        m_SelectionHandles.clear();
    }

    void PickingController::PerformPick(float screenX, float screenY)
    {
        m_SelectionHandles.clear();

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
            NorvesLib::Core::EntityHandle handle = hit.HitEntity->GetEntityHandle();
            if (handle.IsValid())
            {
                m_SelectionHandles.push_back(handle);
            }
        }
    }

    void PickingController::PerformBoxSelect(float x0, float y0, float x1, float y1)
    {
        NorvesLib::Core::Engine::Engine* engine = NorvesLib::Core::Engine::GEngine;
        if (engine == nullptr)
        {
            m_SelectionHandles.clear();
            return;
        }

        const NorvesLib::Core::Rendering::CameraProxy& camera =
            engine->GetRenderWorld().GetRenderingCoordinator().GetMainCamera();
        if (!camera.IsValid())
        {
            m_SelectionHandles.clear();
            return;
        }

        NorvesLib::Math::Frustum frustum;
        if (!NorvesLib::Core::Rendering::BuildScreenRectFrustum(camera, x0, y0, x1, y1, frustum))
        {
            m_SelectionHandles.clear();
            return;
        }

        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Entity*> hits;
        engine->GetSceneQuery().QueryFrustum(frustum, hits);

        m_SelectionHandles.clear();
        for (NorvesLib::Core::Entity* entity : hits)
        {
            if (entity == nullptr)
            {
                continue;
            }

            NorvesLib::Core::EntityHandle handle = entity->GetEntityHandle();
            if (handle.IsValid())
            {
                m_SelectionHandles.push_back(handle);
            }
        }
    }

} // namespace Game::Input

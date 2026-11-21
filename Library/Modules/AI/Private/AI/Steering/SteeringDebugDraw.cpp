// 固定ステップで求めた操舵の結果を描画キューに積む。
#include "SteeringDebugDraw.h"

#if NORVES_ENABLE_DEBUG_OUTPUT
#include "AI/Navigation/NavDebugDraw.h"
#include "AI/Steering/SteeringAgentComponent.h"
#include "AI/Steering/SteeringSystem.h"
#include "Object/Entity.h"
#include "Rendering/DebugDrawQueue.h"

#include <cmath>

namespace NorvesLib::Modules::AI
{
    namespace
    {
        using Core::Rendering::DebugDrawQueue;
        using Math::Vector3;
        using Math::Vector4;

        void DrawDistanceCircle(const Vector3& center, float radius, uint32_t segments, const Vector4& color,
                                DebugDrawQueue& queue)
        {
            if (segments < 3 || segments > (DebugDrawQueue::kMaxDebugLineVertices - queue.GetVertexCount()) / 2)
            {
                return;
            }
            const Vector3 first(center.x + radius, center.y, center.z);
            Vector3 previous = first;
            for (uint32_t i = 1; i < segments; ++i)
            {
                const float angle = Math::Constants::TWO_PI * static_cast<float>(i) / static_cast<float>(segments);
                const Vector3 next(center.x + radius * std::cos(angle), center.y, center.z + radius * std::sin(angle));
                queue.AddLine(previous, next, color);
                previous = next;
            }
            queue.AddLine(previous, first, color);
        }
    }

    void SubmitSteeringDebug(SteeringSystem& system, const Core::World& world, const Vector3& cameraCenter,
                             const SteeringDebugDrawSettings& settings, DebugDrawQueue& queue)
    {
        if (!Steering::IsFiniteVector(cameraCenter) || !std::isfinite(settings.Radius) || settings.Radius < 0.0f ||
            !std::isfinite(settings.HeightOffset))
        {
            return;
        }
        const double radiusSquared = static_cast<double>(settings.Radius) * settings.Radius;
        const Vector3 offset(0.0f, settings.HeightOffset, 0.0f);
        const NavDebugDrawSettings pathSettings{.Radius = settings.Radius};
        for (uint32_t i = 0; i < system.GetAgentCount(); ++i)
        {
            if (queue.GetVertexCount() == DebugDrawQueue::kMaxDebugLineVertices)
            {
                return;
            }
            const auto* agent = system.ResolveAgent(i, world);
            if (!agent || !SteeringSystem::CanSteer(*agent))
            {
                continue;
            }
            const auto& record = system.GetDebugRecord(i);
            const Vector3 position = agent->GetOwner()->GetWorldTransform().position;
            const double dx = static_cast<double>(position.x) - cameraCenter.x;
            const double dz = static_cast<double>(position.z) - cameraCenter.z;
            if (!Steering::IsFiniteVector(position) || dx * dx + dz * dz > radiusSquared)
            {
                continue;
            }

            const Vector3 origin = position + offset;
            queue.AddLine(origin, origin + agent->GetSteeringVelocity(), {0.0f, 1.0f, 0.0f, 1.0f});
            queue.AddLine(origin, origin + record.DesiredVelocity, {1.0f, 1.0f, 0.0f, 1.0f});

            // 固定ステップ後に要求が変わった場合、古い目標や経路に新しい要求の設定を重ねない。
            const auto& request = agent->GetRequest();
            if (record.Revision != request.Revision)
            {
                continue;
            }
            if (record.bHasLastTarget && Steering::IsFiniteVector(record.LastTarget))
            {
                queue.AddSphere(record.LastTarget, 0.2f, {0.0f, 0.85f, 1.0f, 1.0f});
                queue.AddLine(position, record.LastTarget, {0.25f, 0.5f, 0.55f, 0.5f});
                if (request.Kind == ESteeringRequest::KeepDistance)
                {
                    DrawDistanceCircle(record.LastTarget, request.MinDistance, settings.CircleSegments,
                                       {1.0f, 0.5f, 0.0f, 1.0f}, queue);
                    DrawDistanceCircle(record.LastTarget, request.MaxDistance, settings.CircleSegments,
                                       {1.0f, 0.0f, 0.0f, 1.0f}, queue);
                }
            }
            if (request.Kind == ESteeringRequest::MoveTo && record.bPolylinePrepared)
            {
                SubmitNavPathDebug(record.Polyline.GetPoints(), cameraCenter, pathSettings,
                                   {0.1f, 0.35f, 1.0f, 1.0f}, queue);
            }
        }
    }
}
#endif

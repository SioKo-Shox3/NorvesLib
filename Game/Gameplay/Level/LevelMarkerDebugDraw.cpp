// レベルのマーカーを近い順に、形状単位で頂点予算へ収める。
#include "Gameplay/Level/LevelMarkerDebugDraw.h"

#include "Container/Span.h"
#include "Container/VariableArray.h"
#include "Gameplay/Level/AnimalSpawnPointComponent.h"
#include "Gameplay/Level/PoiMarkerComponent.h"
#include "Gameplay/Level/RestPointComponent.h"
#include "Gameplay/Level/ScentTrailComponent.h"
#include "Gameplay/Level/TerritoryMarkerComponent.h"
#include "Gameplay/World/EncounterZoneComponent.h"
#include "Math/GeometryTypes.h"
#include "Math/Vector4.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Rendering/DebugDrawQueue.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Game::LevelEdit
{
    namespace
    {
        namespace Core = NorvesLib::Core;
        namespace Math = NorvesLib::Math;
        namespace Level = NorvesLib::Modules::Gameplay::Level;
        namespace GameplayWorld = NorvesLib::Modules::Gameplay::World;
        using Core::Container::VariableArray;
        using Core::Rendering::DebugDrawQueue;
        using Math::Vector3;
        using Math::Vector4;

        constexpr uint32_t kCircleSegments = 16;
        constexpr uint32_t kCircleVertices = kCircleSegments * 2;
        constexpr uint32_t kCrossVertices = 6;
        constexpr uint32_t kSphereVertices = kCircleVertices * 3 + kCrossVertices;
        constexpr uint32_t kBoxVertices = 24 + kCrossVertices;
        constexpr uint32_t kCylinderVertices = kCircleVertices * 2 + 8 + kCrossVertices;
        constexpr uint32_t kRestVertices = 16;
        constexpr uint32_t kPoiVertices = 2 + kCrossVertices + kCircleVertices;
        constexpr uint32_t kSpawnVertices = kCircleVertices + 2;
        constexpr uint32_t kTrailSegmentsPerSpan = 8;
        constexpr uint32_t kTrailSpanVertices = kTrailSegmentsPerSpan * 2;

        enum class EMarkerKind : uint8_t
        {
            Territory,
            Rest,
            Poi,
            AnimalSpawn,
            Trail
        };

        struct DrawCandidate
        {
            EMarkerKind Kind = EMarkerKind::Territory;
            GameplayWorld::EEncounterZoneShape Shape = GameplayWorld::EEncounterZoneShape::Sphere;
            Vector3 Position;
            Vector3 HalfExtents;
            float Radius = 0.0f;
            float HalfHeight = 0.0f;
            float ArrivalYaw = 0.0f;
            bool bFastTravel = false;
            uint64_t ObjectId = 0;
            double DistanceSquared = 0.0;
            uint32_t VertexCount = 0;
            VariableArray<Vector3> TrailPoints;
        };

        float NonNegativeFinite(float value)
        {
            return std::isfinite(value) ? std::max(value, 0.0f) : 0.0f;
        }

        double DistanceSquared(const Vector3& a, const Vector3& b)
        {
            const double x = double(a.x) - double(b.x);
            const double y = double(a.y) - double(b.y);
            const double z = double(a.z) - double(b.z);
            return x * x + y * y + z * z;
        }

        DrawCandidate MakeCandidate(EMarkerKind kind, const Core::Entity& entity,
                                    const Vector3& position, const Vector3& cameraPosition,
                                    uint32_t vertexCount)
        {
            DrawCandidate candidate;
            candidate.Kind = kind;
            candidate.Position = position;
            candidate.ObjectId = entity.GetObjectId();
            candidate.DistanceSquared = DistanceSquared(position, cameraPosition);
            candidate.VertexCount = vertexCount;
            return candidate;
        }

        void AddCandidate(DrawCandidate&& candidate, const LevelMarkerDebugDrawSettings& settings,
                          VariableArray<DrawCandidate>& candidates, LevelMarkerDebugDrawStats& stats)
        {
            const double maxDistance = double(settings.MaxDistance);
            if (std::isnan(maxDistance) || maxDistance < 0.0 || !std::isfinite(candidate.DistanceSquared) ||
                candidate.DistanceSquared > maxDistance * maxDistance)
            {
                ++stats.CulledByDistance;
                return;
            }
            candidates.push_back(std::move(candidate));
        }

        uint32_t TrailVertexCount(size_t pointCount)
        {
            // キュー全体にも入らない点数なら、乗算のあふれを避けて上限超過を表す。
            constexpr size_t maxFittingPoints =
                (DebugDrawQueue::kMaxDebugLineVertices + kTrailSpanVertices) /
                (kCrossVertices + kTrailSpanVertices);
            if (pointCount > maxFittingPoints)
            {
                return DebugDrawQueue::kMaxDebugLineVertices + 1;
            }
            return static_cast<uint32_t>(pointCount) * kCrossVertices +
                static_cast<uint32_t>(pointCount - 1) * kTrailSpanVertices;
        }

        void GatherLiveTrailPoints(const Level::ScentTrailComponent& trail,
                                   const VariableArray<Core::Entity*>& children,
                                   VariableArray<Vector3>& points)
        {
            VariableArray<float> intensities;
            trail.GatherPoints(points, intensities);

            // GatherPoints は破棄予約を見ないため、同じ子順で対応する点だけを詰め直す。
            size_t readIndex = 0;
            size_t writeIndex = 0;
            for (const Core::Entity* child : children)
            {
                if (child->GetComponent<Level::ScentTrailPointComponent>() == nullptr)
                {
                    continue;
                }
                if (!child->IsPendingDestroy())
                {
                    points[writeIndex++] = points[readIndex];
                }
                ++readIndex;
            }
            points.resize(writeIndex);
        }

        void CollectEntity(const Core::Entity& entity, const Vector3& cameraPosition,
                           const LevelMarkerDebugDrawSettings& settings,
                           VariableArray<DrawCandidate>& candidates, LevelMarkerDebugDrawStats& stats)
        {
            if (entity.IsPendingDestroy())
            {
                return;
            }

            const Vector3 position = entity.GetWorldTransform().position;
            const auto children = entity.GetChildEntities();
            if (settings.bDrawTerritories)
            {
                if (const auto* territory = entity.GetComponent<Level::TerritoryMarkerComponent>())
                {
                    DrawCandidate candidate = MakeCandidate(
                        EMarkerKind::Territory, entity, position, cameraPosition, kSphereVertices);
                    candidate.Radius = NonNegativeFinite(territory->getEngageRadius());
                    if (const auto* zone = entity.GetComponent<GameplayWorld::EncounterZoneComponent>())
                    {
                        candidate.Shape = zone->getShape();
                        switch (candidate.Shape)
                        {
                        case GameplayWorld::EEncounterZoneShape::Sphere:
                            candidate.Radius = NonNegativeFinite(zone->getRadius());
                            break;
                        case GameplayWorld::EEncounterZoneShape::Box:
                        {
                            const Vector3 extents = zone->getHalfExtents();
                            candidate.HalfExtents = Vector3(NonNegativeFinite(extents.x),
                                                            NonNegativeFinite(extents.y),
                                                            NonNegativeFinite(extents.z));
                            candidate.VertexCount = kBoxVertices;
                            break;
                        }
                        case GameplayWorld::EEncounterZoneShape::CylinderY:
                            candidate.Radius = NonNegativeFinite(zone->getRadius());
                            candidate.HalfHeight = NonNegativeFinite(zone->getHalfHeight());
                            candidate.VertexCount = kCylinderVertices;
                            break;
                        default:
                            candidate.Shape = GameplayWorld::EEncounterZoneShape::Sphere;
                            break;
                        }
                    }
                    AddCandidate(std::move(candidate), settings, candidates, stats);
                }
            }

            if (settings.bDrawPoints)
            {
                if (const auto* rest = entity.GetComponent<Level::RestPointComponent>())
                {
                    DrawCandidate candidate = MakeCandidate(
                        EMarkerKind::Rest, entity, position, cameraPosition, kRestVertices);
                    candidate.bFastTravel = rest->getbFastTravel();
                    const float yaw = rest->getArrivalYaw();
                    candidate.ArrivalYaw = std::isfinite(yaw) ? yaw : 0.0f;
                    AddCandidate(std::move(candidate), settings, candidates, stats);
                }
                if (const auto* poi = entity.GetComponent<Level::PoiMarkerComponent>())
                {
                    DrawCandidate candidate = MakeCandidate(
                        EMarkerKind::Poi, entity, position, cameraPosition, kPoiVertices);
                    candidate.Radius = NonNegativeFinite(poi->getDiscoverRadius());
                    AddCandidate(std::move(candidate), settings, candidates, stats);
                }
                if (const auto* spawn = entity.GetComponent<Level::AnimalSpawnPointComponent>())
                {
                    DrawCandidate candidate = MakeCandidate(
                        EMarkerKind::AnimalSpawn, entity, position, cameraPosition, kSpawnVertices);
                    candidate.Radius = NonNegativeFinite(spawn->getRadius());
                    AddCandidate(std::move(candidate), settings, candidates, stats);
                }
            }

            if (settings.bDrawTrails)
            {
                if (const auto* trail = entity.GetComponent<Level::ScentTrailComponent>())
                {
                    DrawCandidate candidate = MakeCandidate(EMarkerKind::Trail, entity, position, cameraPosition, 0);
                    GatherLiveTrailPoints(*trail, children, candidate.TrailPoints);
                    if (!candidate.TrailPoints.empty())
                    {
                        candidate.VertexCount = TrailVertexCount(candidate.TrailPoints.size());
                        candidate.DistanceSquared = std::numeric_limits<double>::infinity();
                        for (const Vector3& point : candidate.TrailPoints)
                        {
                            const double distanceSquared = DistanceSquared(point, cameraPosition);
                            if (!std::isfinite(distanceSquared))
                            {
                                candidate.DistanceSquared = std::numeric_limits<double>::infinity();
                                break;
                            }
                            candidate.DistanceSquared = std::min(candidate.DistanceSquared, distanceSquared);
                        }
                        AddCandidate(std::move(candidate), settings, candidates, stats);
                    }
                }
            }

            for (const Core::Entity* child : children)
            {
                CollectEntity(*child, cameraPosition, settings, candidates, stats);
            }
        }

        void DrawCircle(DebugDrawQueue& queue, const Vector3& center, float radius,
                        const Vector3& axisU, const Vector3& axisV, const Vector4& color)
        {
            for (uint32_t i = 0; i < kCircleSegments; ++i)
            {
                const float angle0 = Math::Constants::TWO_PI * float(i) / float(kCircleSegments);
                const float angle1 = Math::Constants::TWO_PI * float(i + 1) / float(kCircleSegments);
                const Vector3 p0 = center + (axisU * std::cos(angle0) + axisV * std::sin(angle0)) * radius;
                const Vector3 p1 = center + (axisU * std::cos(angle1) + axisV * std::sin(angle1)) * radius;
                queue.AddLine(p0, p1, color);
            }
        }

        void DrawCross(DebugDrawQueue& queue, const Vector3& center, float size, const Vector4& color)
        {
            const Vector3 x = Vector3::UnitX * size;
            const Vector3 y = Vector3::UnitY * size;
            const Vector3 z = Vector3::UnitZ * size;
            queue.AddLine(center - x, center + x, color);
            queue.AddLine(center - y, center + y, color);
            queue.AddLine(center - z, center + z, color);
        }

        void DrawTerritory(const DrawCandidate& candidate, DebugDrawQueue& queue, const Vector4& color)
        {
            const Vector3& center = candidate.Position;
            switch (candidate.Shape)
            {
            case GameplayWorld::EEncounterZoneShape::Sphere:
                DrawCircle(queue, center, candidate.Radius, Vector3::UnitX, Vector3::UnitY, color);
                DrawCircle(queue, center, candidate.Radius, Vector3::UnitY, Vector3::UnitZ, color);
                DrawCircle(queue, center, candidate.Radius, Vector3::UnitZ, Vector3::UnitX, color);
                break;
            case GameplayWorld::EEncounterZoneShape::Box:
                queue.AddAABB(Math::AABB::FromCenterExtents(center, candidate.HalfExtents), color);
                break;
            case GameplayWorld::EEncounterZoneShape::CylinderY:
            {
                const Vector3 vertical = Vector3::UnitY * candidate.HalfHeight;
                const Vector3 bottom = center - vertical;
                const Vector3 top = center + vertical;
                DrawCircle(queue, bottom, candidate.Radius, Vector3::UnitX, Vector3::UnitZ, color);
                DrawCircle(queue, top, candidate.Radius, Vector3::UnitX, Vector3::UnitZ, color);
                const Vector3 x = Vector3::UnitX * candidate.Radius;
                const Vector3 z = Vector3::UnitZ * candidate.Radius;
                queue.AddLine(bottom + x, top + x, color);
                queue.AddLine(bottom - x, top - x, color);
                queue.AddLine(bottom + z, top + z, color);
                queue.AddLine(bottom - z, top - z, color);
                break;
            }
            }
            DrawCross(queue, center, 0.2f, color);
        }

        void DrawRest(const DrawCandidate& candidate, DebugDrawQueue& queue, const Vector4& color)
        {
            const Vector3& position = candidate.Position;
            const Vector3 top = position + Vector3(0.0f, 1.6f, 0.0f);
            const Vector3 right = position + Vector3(0.3f, 1.3f, 0.0f);
            const Vector3 bottom = position + Vector3(0.0f, 1.0f, 0.0f);
            const Vector3 left = position + Vector3(-0.3f, 1.3f, 0.0f);
            queue.AddLine(position, top, color);
            queue.AddLine(top, right, color);
            queue.AddLine(right, bottom, color);
            queue.AddLine(bottom, left, color);
            queue.AddLine(left, top, color);

            const float yaw = candidate.ArrivalYaw * (Math::Constants::PI / 180.0f);
            const Vector3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
            const Vector3 side(forward.z, 0.0f, -forward.x);
            const Vector3 start = position + Vector3(0.0f, 0.1f, 0.0f);
            const Vector3 end = start + forward;
            queue.AddLine(start, end, color);
            queue.AddLine(end, end - forward * 0.25f + side * 0.15f, color);
            queue.AddLine(end, end - forward * 0.25f - side * 0.15f, color);
        }

        void DrawTrail(const DrawCandidate& candidate, DebugDrawQueue& queue, const Vector4& color)
        {
            const Core::Container::Span<const Vector3> points(candidate.TrailPoints);
            for (const Vector3& point : points)
            {
                DrawCross(queue, point, 0.15f, color);
            }
            for (size_t span = 0; span + 1 < points.size(); ++span)
            {
                Vector3 previous = Level::EvaluateCatmullRom(points, float(span));
                for (uint32_t segment = 1; segment <= kTrailSegmentsPerSpan; ++segment)
                {
                    const float t = float(span) + float(segment) / float(kTrailSegmentsPerSpan);
                    const Vector3 next = Level::EvaluateCatmullRom(points, t);
                    queue.AddLine(previous, next, color);
                    previous = next;
                }
            }
        }

        void DrawMarker(const DrawCandidate& candidate, const LevelMarkerDebugDrawSettings& settings,
                        DebugDrawQueue& queue)
        {
            Vector4 color;
            switch (candidate.Kind)
            {
            case EMarkerKind::Territory:
                color = Vector4(1.0f, 0.35f, 0.08f, 1.0f);
                break;
            case EMarkerKind::Rest:
                color = candidate.bFastTravel ? Vector4(0.2f, 0.9f, 1.0f, 1.0f)
                                              : Vector4(0.3f, 0.45f, 1.0f, 1.0f);
                break;
            case EMarkerKind::Poi:
                color = Vector4(1.0f, 0.85f, 0.1f, 1.0f);
                break;
            case EMarkerKind::AnimalSpawn:
                color = Vector4(0.2f, 0.9f, 0.25f, 1.0f);
                break;
            case EMarkerKind::Trail:
                color = Vector4(0.85f, 0.25f, 0.9f, 1.0f);
                break;
            }
            if (settings.SelectedObjectId != 0 && settings.SelectedObjectId == candidate.ObjectId)
            {
                color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            switch (candidate.Kind)
            {
            case EMarkerKind::Territory:
                DrawTerritory(candidate, queue, color);
                break;
            case EMarkerKind::Rest:
                DrawRest(candidate, queue, color);
                break;
            case EMarkerKind::Poi:
                queue.AddLine(candidate.Position, candidate.Position + Vector3::UnitY * 1.5f, color);
                DrawCross(queue, candidate.Position + Vector3::UnitY * 1.5f, 0.25f, color);
                DrawCircle(queue, candidate.Position, candidate.Radius, Vector3::UnitX, Vector3::UnitZ, color);
                break;
            case EMarkerKind::AnimalSpawn:
                DrawCircle(queue, candidate.Position, candidate.Radius, Vector3::UnitX, Vector3::UnitZ, color);
                queue.AddLine(candidate.Position, candidate.Position + Vector3::UnitY * 0.6f, color);
                break;
            case EMarkerKind::Trail:
                DrawTrail(candidate, queue, color);
                break;
            }
        }
    } // namespace

    LevelMarkerDebugDrawStats DrawLevelMarkers(const Core::World& world, const Vector3& cameraPosition,
                                               const LevelMarkerDebugDrawSettings& settings,
                                               DebugDrawQueue& queue)
    {
        LevelMarkerDebugDrawStats stats;
        VariableArray<DrawCandidate> candidates;
        for (const Core::Entity* root : world.GetRootEntities())
        {
            CollectEntity(*root, cameraPosition, settings, candidates, stats);
        }

        std::sort(candidates.begin(), candidates.end(), [](const DrawCandidate& a, const DrawCandidate& b)
        {
            if (a.DistanceSquared != b.DistanceSquared)
            {
                return a.DistanceSquared < b.DistanceSquared;
            }
            if (a.ObjectId != b.ObjectId)
            {
                return a.ObjectId < b.ObjectId;
            }
            return a.Kind < b.Kind;
        });

        const size_t initialCount = queue.GetVertexCount();
        const size_t available = initialCount < DebugDrawQueue::kMaxDebugLineVertices
            ? DebugDrawQueue::kMaxDebugLineVertices - initialCount : 0;
        const size_t budget = std::min(size_t(settings.MaxVertices), available);
        size_t used = 0;
        for (const DrawCandidate& candidate : candidates)
        {
            if (candidate.VertexCount > budget - used)
            {
                ++stats.CulledByBudget;
                continue;
            }
            DrawMarker(candidate, settings, queue);
            used += candidate.VertexCount;
            ++stats.Drawn;
        }
        stats.VerticesAdded = static_cast<uint32_t>(queue.GetVertexCount() - initialCount);
        return stats;
    }
} // namespace Game::LevelEdit

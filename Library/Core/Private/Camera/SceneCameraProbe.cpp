#include "Camera/SceneCameraProbe.h"
#include "Scene/SceneQuery.h"
#include <cmath>
namespace NorvesLib::Core::Camera
{
    SceneCameraProbe::SceneCameraProbe(const Scene::SceneQuery& query, const Scene::PhysicsQueryFilter& filter)
        : m_Query(query), m_Filter(filter)
    {
        m_Filter.TriggerPolicy = Scene::EPhysicsQueryTriggerPolicy::Exclude;
    }
    CameraProbeResult SceneCameraProbe::SweepSphere(const CameraProbeRequest& request, CameraProbeHit& out) const
    {
        Scene::PhysicsQueryDesc query;
        query.Kind = Scene::EPhysicsQueryKind::SweepSphere;
        query.Sphere = Math::Sphere(request.Origin, request.Radius);
        query.Direction = request.Direction;
        query.MaxDistance = request.Distance;
        query.MaxHits = 1;
        query.bReportStartOverlap = true;
        query.Filter = m_Filter;
        m_LastResult = Scene::EPhysicsSceneQueryResult::NotReady;
        const auto result = m_Query.ExecuteQuery(query, m_Hits);
        m_LastResult = result;
        if (result == Scene::EPhysicsSceneQueryResult::NoHit)
            return CameraProbeResult::Clear;
        if (result != Scene::EPhysicsSceneQueryResult::Success || m_Hits.size() != 1)
            return CameraProbeResult::Failed;
        const auto& hit = m_Hits[0];
        if (!std::isfinite(hit.Distance) || hit.Distance < 0 || hit.Distance > request.Distance)
        {
            m_LastResult = Scene::EPhysicsSceneQueryResult::InvalidArgument;
            return CameraProbeResult::Failed;
        }
        CameraProbeHit next;
        next.Distance = hit.Distance;
        next.bStartPenetrating = hit.bStartPenetrating;
        out = next;
        return CameraProbeResult::Hit;
    }
} // namespace NorvesLib::Core::Camera

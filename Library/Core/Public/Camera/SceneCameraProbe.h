#pragma once
#include "Camera/CameraCollisionSolver.h"
#include "Container/VariableArray.h"
#include "Scene/PhysicsQueryTypes.h"
namespace NorvesLib::Core::Scene
{
    class SceneQuery;
}
namespace NorvesLib::Core::Camera
{
    // SceneQueryを借用する主スレッド用アダプター。queryの寿命内でのみ使う。
    // 自身のBody/Collider handleは呼出側がfilterへ設定する。Triggerは常に除外する。
    class SceneCameraProbe final : public ICameraProbe
    {
      public:
        SceneCameraProbe(const Scene::SceneQuery& query, const Scene::PhysicsQueryFilter& filter);
        CameraProbeResult SweepSphere(const CameraProbeRequest& request, CameraProbeHit& out) const override;
        Scene::EPhysicsSceneQueryResult GetLastQueryResult() const
        {
            return m_LastResult;
        }

      private:
        const Scene::SceneQuery& m_Query;
        Scene::PhysicsQueryFilter m_Filter;
        mutable Container::VariableArray<Scene::PhysicsQueryHit> m_Hits;
        mutable Scene::EPhysicsSceneQueryResult m_LastResult = Scene::EPhysicsSceneQueryResult::NotReady;
    };
} // namespace NorvesLib::Core::Camera

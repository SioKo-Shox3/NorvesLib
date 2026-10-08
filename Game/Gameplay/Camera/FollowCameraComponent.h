#pragma once
#include "Camera/CameraCollisionSolver.h"
#include "Component/SpringArmComponent.h"
#include "Input/InputMapper.h"
#include "Math/Transform.h"
namespace NorvesLib::Core::Scene
{
    class SceneQuery;
}
namespace Game::Gameplay
{
    // LookはInput群、補間済対象への追従はCamera群で行う。固定更新では駆動しない。
    class FollowCameraComponent : public NorvesLib::Core::Component::SpringArmComponent
    {
        REFLECTION_CLASS(FollowCameraComponent, NorvesLib::Core::Component::SpringArmComponent)
      public:
        void Initialize() override;
        void Finalize() override;
        void OnTickGroup(NorvesLib::Core::Component::ETickGroup group, float dt) override;
        // Mapperとqueryはcomponentより長生きするか、破棄前にnullptrへ解除すること。
        void BindInput(NorvesLib::Core::Input::InputMapper* mapper)
        {
            m_Input = mapper;
        }
        void BindSceneQuery(const NorvesLib::Core::Scene::SceneQuery* query)
        {
            m_Query = query;
        }
        // カメラownerは独立root Entityに置く。
        bool SetSubject(NorvesLib::Core::Entity* visualRoot, NorvesLib::Core::Entity* physicsRoot);
        // 同じInput群の移動処理に最新視線を渡す専用root Entity。実カメラとは別にする。
        bool SetViewSource(NorvesLib::Core::Entity* view);
        bool SetCollisionSettings(const NorvesLib::Core::Camera::CameraCollisionSettings& settings);
        const NorvesLib::Core::Camera::CameraCollisionOutput& GetCollisionOutput() const
        {
            return m_Output;
        }
        NorvesLib::Core::Camera::CameraCollisionResult GetLastCollisionResult() const
        {
            return m_LastResult;
        }

      protected:
        // late snapshotのRefreshは確定済み姿勢の再適用だけ。solverの時間を二重に進めない。
        void DriveOwnerTransform(const NorvesLib::Core::Entity& pivot) override;

      private:
        NorvesLib::Core::Entity* Resolve(uint64_t id) const;
        void ReadLook();
        void UpdateCamera(float dt);
        NorvesLib::Core::Input::InputMapper* m_Input = nullptr;
        const NorvesLib::Core::Scene::SceneQuery* m_Query = nullptr;
        uint64_t m_PhysicsRootId = 0, m_ViewSourceId = 0;
        NorvesLib::Core::Camera::CameraCollisionSettings m_Settings;
        NorvesLib::Core::Camera::CameraCollisionState m_State;
        NorvesLib::Core::Camera::CameraCollisionOutput m_Output;
        NorvesLib::Core::Camera::CameraCollisionResult m_LastResult =
            NorvesLib::Core::Camera::CameraCollisionResult::ProbeFailed;
        NorvesLib::Math::Transform m_CachedPose;
        bool m_bHasPose = false;
    };
} // namespace Game::Gameplay

#pragma once
#include "Asset/TextAssetReloadTracker.h"
#include "Camera/CameraCollisionSolver.h"
#include "Component/SpringArmComponent.h"
#include "Gameplay/Camera/FollowCameraProfile.h"
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
        bool SetFollowSmoothing(float horizontalHalfLife, float verticalHalfLife);
        bool SetProfilePath(NorvesLib::Core::Container::AnsiStringView path);
        bool SetLockOnTarget(NorvesLib::Core::Entity* target, float radius = 0);
        bool SetLockOnJoint(NorvesLib::Core::Entity* target, uint32_t jointIndex, float radius = 0);
        void ClearLockOn()
        {
            m_LockOnId = 0;
            m_LockJoint = UINT32_MAX;
        }
        void AddTrauma(float amount);
        float GetSubjectFade() const
        {
            return m_SubjectFade;
        }
        bool SetCollisionSettings(const NorvesLib::Core::Camera::CameraCollisionSettings& settings);
        // Camera群での更新試行数。late snapshotの姿勢再適用では増やさない。
        uint64_t GetCameraTickCount() const
        {
            return m_CameraTickCount;
        }
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
        void ReadLook(float dt);
        void ReloadProfile(float dt);
        float SubjectSpeed() const;
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
        uint64_t m_CameraTickCount = 0;
        FollowCameraProfile m_Profile;
        NorvesLib::Core::Asset::TextAssetReloadTracker m_ProfileReload;
        uint64_t m_LockOnId = 0;
        uint32_t m_LockJoint = UINT32_MAX;
        float m_LockRadius = 0;
        NorvesLib::Math::Vector3 m_FollowPosition, m_FollowVelocity;
        float m_TimeSinceLook = 0, m_YawVelocity = 0, m_FovVelocity = 0, m_FocusVelocity = 0;
        float m_Trauma = 0, m_ShakeTime = 0, m_SubjectFade = 1;
        bool m_bFollowInitialized = false, m_bProfileReadFailed = false;
    };
} // namespace Game::Gameplay

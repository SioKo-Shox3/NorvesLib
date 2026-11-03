#include "Gameplay/Camera/FollowCameraComponent.h"
#include "Camera/SceneCameraProbe.h"
#include "Component/CameraComponent.h"
#include "Component/SkinnedMeshComponent.h"
#include "Ecs/EcsTypes.h"
#include "Game/Input/GameInputActions.h"
#include "Logging/LogMacros.h"
#include "Math/CriticalDamping.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/CharacterBodyComponent.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
#include <algorithm>
#include <cmath>
namespace Game::Gameplay
{
    namespace Core = NorvesLib::Core;
    namespace Math = NorvesLib::Math;
    namespace Physics = NorvesLib::Modules::Physics;
    IMPLEMENT_CLASS(FollowCameraComponent, Core::Component::SpringArmComponent)
    void FollowCameraComponent::Initialize()
    {
        Core::Component::SpringArmComponent::Initialize();
        using namespace Core::Component;
        SetTickGroup(ETickGroup::Input);
        SetTickGroupMask(TickGroupBit(ETickGroup::Input) | TickGroupBit(ETickGroup::Camera));
        SetTickPriority(-100);
        (void)m_ProfileReload.SetPath("Gameplay/FollowCameraProfile.json");
    }
    void FollowCameraComponent::Finalize()
    {
        m_Input = nullptr;
        m_Query = nullptr;
        m_PhysicsRootId = m_ViewSourceId = 0;
        ClearLockOn();
        m_bFollowInitialized = false;
        m_bPreviousSubjectValid = false;
        m_bHasPose = false;
        m_bClinging = m_bClingContinuing = m_bPreviousTickYawValid = false;
        m_YawVelocity = m_MeasuredYawRate = 0;
        Core::Component::SpringArmComponent::Finalize();
    }
    Core::Entity* FollowCameraComponent::Resolve(uint64_t id) const
    {
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        auto* value = world && id ? world->FindEntityByObjectId(id) : nullptr;
        return value && !value->IsPendingDestroy() && value->IsActive() ? value : nullptr;
    }
    bool FollowCameraComponent::SetSubject(Core::Entity* visual, Core::Entity* physical)
    {
        auto* owner = GetOwner();
        if (!owner || owner->GetParentEntity() || !visual || !physical || physical->GetParentEntity() ||
            visual == physical || physical == owner || visual == owner ||
            (m_ViewSourceId &&
             (m_ViewSourceId == physical->GetObjectId() || m_ViewSourceId == visual->GetObjectId())) ||
            physical->GetWorld() != owner->GetWorld() || visual->GetWorld() != owner->GetWorld() ||
            physical->IsPendingDestroy() || visual->IsPendingDestroy())
            return false;
        bool descendant = false;
        for (auto* entity = visual; entity; entity = entity->GetParentEntity())
            if (entity == physical)
            {
                descendant = true;
                break;
            }
        if (!descendant || !SetPivot(visual))
            return false;
        m_PhysicsRootId = physical->GetObjectId();
        m_bFollowInitialized = false;
        m_bPreviousSubjectValid = false;
        m_FollowVelocity = {};
        m_State = {};
        m_bHasPose = false;
        if (m_bClinging || m_bClingContinuing)
            m_YawVelocity = 0;
        m_bClinging = m_bClingContinuing = m_bPreviousTickYawValid = false;
        m_MeasuredYawRate = 0;
        return true;
    }
    bool FollowCameraComponent::SetViewSource(Core::Entity* view)
    {
        auto* owner = GetOwner();
        if (!owner || (view && (view == owner || view->GetParentEntity() || view->GetWorld() != owner->GetWorld() ||
                                view->IsPendingDestroy() || view->GetObjectId() == m_PhysicsRootId ||
                                view->GetObjectId() == GetPivotObjectId())))
            return false;
        m_ViewSourceId = view ? view->GetObjectId() : 0;
        return true;
    }
    bool FollowCameraComponent::SetCollisionSettings(const Core::Camera::CameraCollisionSettings& value)
    {
        if (!std::isfinite(value.ProbeRadius) || value.ProbeRadius <= 0 || !std::isfinite(value.Margin) ||
            value.Margin < 0 || !std::isfinite(value.ExtendHalfLife) || value.ExtendHalfLife < 0 ||
            !std::isfinite(value.MaximumExtendSpeed) || value.MaximumExtendSpeed < 0)
            return false;
        m_Settings = value;
        return true;
    }
    void FollowCameraComponent::RegisterTick(Core::World& world)
    {
        // Input群とCamera群で呼ぶ（主群によらない）。群・mask・優先度・channelはComponentの設定のまま。
        // SpringArmの登録は型の完全一致なので、この型にはSpringArmの本体は呼ばれない（旧と同じ）
        using namespace Core::Component;
        ComponentTickRegistration camera;
        camera.Name = "FollowCamera.InputCamera";
        camera.bPrimaryGroupOnly = false;
        camera.Groups = TickGroupBit(ETickGroup::Input) | TickGroupBit(ETickGroup::Camera);
        camera.GroupRuns[static_cast<size_t>(ETickGroup::Input)] = [](Component& component, float dt)
        { static_cast<FollowCameraComponent&>(component).TickInputGroup(dt); };
        camera.GroupRuns[static_cast<size_t>(ETickGroup::Camera)] = [](Component& component, float dt)
        { static_cast<FollowCameraComponent&>(component).TickCameraGroup(dt); };
        camera.TimeChannel = Core::Engine::TimeChannel::Unscaled;
        camera.Reads.push_back(Core::Ecs::EcsColumnInfoOf<Core::Ecs::EcsWorldTransform>().Type);
        camera.Writes.push_back(Core::Ecs::EcsColumnInfoOf<Core::Ecs::EcsLocalTransform>().Type);
        world.RegisterComponentTick(StaticClass(), camera);
    }
    void FollowCameraComponent::TickInputGroup(float dt)
    {
        ReadLook(dt);
    }
    void FollowCameraComponent::TickCameraGroup(float dt)
    {
        if (m_CameraTickCount != UINT64_MAX)
            ++m_CameraTickCount;
        UpdateCamera(dt);
    }
    void FollowCameraComponent::ReadLook(float dt)
    {
        if (!std::isfinite(dt) || dt < 0)
            return;
        ReloadProfile(dt);
        m_TimeSinceLook += dt;
        if (m_Input && m_Input->GetActiveContext() == InputActions::GameplayContext)
        {
            const auto look = m_Input->GetAction(InputActions::Look);
            if (look.Active && look.Type == Core::Input::EInputMappingValueType::Axis2D && std::isfinite(look.Axis.x) &&
                std::isfinite(look.Axis.y))
            {
                Core::Component::SpringArmIntent intent;
                // Lookは度単位のframe delta。ここでdtを再度掛けない。
                intent.YawDelta = look.Axis.x;
                intent.PitchDelta = -look.Axis.y;
                const float input = std::hypot(look.Axis.x, look.Axis.y);
                if (input > .001f)
                {
                    m_TimeSinceLook = 0;
                    m_YawVelocity = 0;
                    m_bClingContinuing = false;
                }
                if (input > m_Profile.LockBreakDegrees)
                    ClearLockOn();
                ApplyIntent(intent);
            }
        }
        auto* physical = Resolve(m_PhysicsRootId);
        auto* owner = GetOwner();
        if ((m_PhysicsRootId && (!physical || physical->GetParentEntity())) || !owner || owner->GetParentEntity())
            return;
        auto* visual = ResolvePivot();
        bool descendant = false;
        for (auto* entity = visual; entity; entity = entity->GetParentEntity())
            if (entity == physical)
            {
                descendant = true;
                break;
            }
        if (m_PhysicsRootId && (!visual || visual == physical || !descendant))
            return;
        if (physical && !m_bClinging && !m_bClingContinuing && !IsLockedOn() &&
            m_TimeSinceLook >= m_Profile.YawReturnDelay &&
            SubjectSpeed() >= m_Profile.MinimumYawSpeed)
        {
            const auto forward = physical->GetWorldTransform().rotation * Math::Vector3::UnitZ;
            const float target = float(std::atan2(forward.x, forward.z) * 57.29577951308232 + 180);
            float yaw = GetYaw();
            if (Math::TryCriticalDamp(yaw, m_YawVelocity, yaw + std::remainder(target - yaw, 360.f),
                                      m_Profile.YawHalfLife, dt) &&
                yaw != GetYaw())
                SetYaw(yaw);
        }
        auto* view = Resolve(m_ViewSourceId);
        if (!view || view->GetParentEntity() || view == GetOwner() || view->GetObjectId() == m_PhysicsRootId ||
            view->GetObjectId() == GetPivotObjectId())
            return;
        auto direction = ComputeArmOffset() * -1.f;
        direction.y = 0;
        const float length = direction.Length();
        if (std::isfinite(length) && length > 1e-5f)
            view->SetRotation(Math::QuaternionUtils::LookRotation(direction / length, Math::Vector3::UnitY));
    }
    void FollowCameraComponent::UpdateCamera(float dt)
    {
        const float tickYaw = GetYaw();
        const bool bValidYawDeltaTime = std::isfinite(dt) && dt > 0;
        m_MeasuredYawRate = bValidYawDeltaTime && m_bPreviousTickYawValid
            ? std::remainder(tickYaw - m_PreviousTickYaw, 360.f) / dt : 0;
        m_PreviousTickYaw = tickYaw;
        m_bPreviousTickYawValid = true;
        // 途中で更新に失敗した場合、古い目標位置を次の区間に使わない。
        const bool bPreviousSubjectValid = m_bPreviousSubjectValid;
        m_bPreviousSubjectValid = false;
        auto* owner = GetOwner();
        auto* visual = ResolvePivot();
        auto* physical = Resolve(m_PhysicsRootId);
        bool descendant = false;
        for (auto* entity = visual; entity; entity = entity->GetParentEntity())
            if (entity == physical)
            {
                descendant = true;
                break;
            }
        if (!owner || owner->GetParentEntity() || owner->IsPendingDestroy() || !visual || !visual->IsActive() ||
            !physical || physical->GetParentEntity() || visual == physical || !descendant || !m_Query)
        {
            m_LastResult = Core::Camera::CameraCollisionResult::ProbeFailed;
            if (owner)
                DriveOwnerTransform(*owner);
            return;
        }
        Core::Scene::PhysicsQueryFilter filter;
        if (auto* body = physical->GetComponent<Physics::RigidBodyComponent>())
            filter.IgnoreBodies[0] = body->GetBodyHandle();
        if (auto* collider = physical->GetComponent<Physics::ColliderComponent>())
            filter.IgnoreColliders[0] = collider->GetColliderHandle();
        if (m_SubjectBodyOverride.IsValid())
            filter.IgnoreBodies[0] = m_SubjectBodyOverride;
        if (m_SubjectColliderOverride.IsValid())
            filter.IgnoreColliders[0] = m_SubjectColliderOverride;
        Core::Camera::SceneCameraProbe probe(*m_Query, filter);
        Core::Camera::CameraCollisionRequest request;
        const auto subject = visual->GetRenderWorldTransform().position + GetTargetOffset();
        auto followPosition = m_FollowPosition, followVelocity = m_FollowVelocity;
        if (!m_bFollowInitialized || (subject - followPosition).LengthSquared() > 64)
        {
            followPosition = subject;
            followVelocity = {};
        }
        else
        {
            const bool bDamped = bPreviousSubjectValid
                ? Math::TryCriticalDampMovingTarget(followPosition, followVelocity, m_PreviousSubject, subject,
                                                    m_Profile.PositionHalfLives, dt)
                : Math::TryCriticalDamp(followPosition, followVelocity, subject, m_Profile.PositionHalfLives, dt);
            if (!bDamped)
            {
                return;
            }
        }
        request.Pivot = followPosition;
        const float speed = std::clamp(SubjectSpeed() / m_Profile.SpeedReference, 0.f, 1.f);
        request.DesiredLength = GetArmLength() + m_Profile.ArmGain * speed * m_Profile.SpeedEffectsStrength;
        auto* camera = owner->GetComponent<Core::Component::CameraComponent>();
        auto lockTarget = m_LockOn.lock();
        auto* world = owner->GetWorld();
        Core::Camera::LockOnPoint point;
        bool locked = false;
        Math::Vector3 focusPoint = subject;
        if (lockTarget && world && lockTarget->TryResolve(*world, point))
        {
            locked = true;
            focusPoint = point.Position;
            const auto offset = focusPoint - subject;
            const float distance = offset.Length();
            const float fov = camera ? camera->GetFieldOfView() : m_Profile.BaseFov;
            const float screenLimit = GetArmLength() * std::tan(fov * .4f * .01745329252f);
            request.Pivot += offset * std::min(m_Profile.LockBias, screenLimit / std::max(distance, .01f));
            request.DesiredLength += distance * m_Profile.LockArmGain + point.Radius;
            if (!m_bClinging && !m_bClingContinuing && offset.x * offset.x + offset.z * offset.z > 1e-6f)
            {
                const float target = float(std::atan2(offset.x, offset.z) * 57.29577951308232 + 180);
                float yaw = GetYaw();
                if (Math::TryCriticalDamp(yaw, m_YawVelocity, yaw + std::remainder(target - yaw, 360.f),
                                          m_Profile.YawHalfLife, dt))
                    SetYaw(yaw);
            }
        }
        else
            ClearLockOn();
        if (m_bClinging && bValidYawDeltaTime)
        {
            auto forward = visual->GetRenderWorldTransform().rotation * Math::Vector3::UnitZ;
            if (locked)
            {
                const auto offset = focusPoint - subject;
                if (std::hypot(offset.x, offset.z) >= 1e-4f)
                    forward = offset;
            }
            if (std::hypot(forward.x, forward.z) >= 1e-4f)
                m_PreviousClingTargetYaw = float(std::atan2(forward.x, forward.z) * 57.29577951308232 + 180);
            const float yaw = GetYaw();
            float dampedYaw = yaw, dampedVelocity = m_YawVelocity;
            if (Math::TryCriticalDamp(dampedYaw, dampedVelocity,
                                      yaw + std::remainder(m_PreviousClingTargetYaw - yaw, 360.f),
                                      m_Profile.ClingYawHalfLife, dt))
            {
                const float change = m_Profile.ClingMaximumYawSpeed * dt;
                const float limit = std::max(m_Profile.ClingMaximumYawSpeed, std::fabs(m_YawVelocity) - change);
                float want = (dampedYaw - yaw) / dt;
                want = std::clamp(want, m_YawVelocity - change, m_YawVelocity + change);
                m_YawVelocity = std::clamp(want, -limit, limit);
                SetYaw(yaw + m_YawVelocity * dt);
            }
        }
        else if (m_bClingContinuing && bValidYawDeltaTime)
        {
            const float change = m_Profile.ClingMaximumYawSpeed * dt;
            m_YawVelocity = std::clamp(0.f, m_YawVelocity - change, m_YawVelocity + change);
            SetYaw(GetYaw() + m_YawVelocity * dt);
            if (m_YawVelocity == 0)
                m_bClingContinuing = false;
        }
        request.Direction = ComputeArmOffset();
        request.DesiredLength = std::min(request.DesiredLength, m_Profile.MaximumArmLength);
        auto candidate = m_State;
        Core::Camera::CameraCollisionOutput output;
        try
        {
            m_LastResult =
                Core::Camera::CameraCollisionSolver::Solve(candidate, m_Settings, request, probe, dt, output);
        }
        catch (...)
        {
            m_LastResult = Core::Camera::CameraCollisionResult::ProbeFailed;
            DriveOwnerTransform(*visual);
            throw;
        }
        if (m_LastResult != Core::Camera::CameraCollisionResult::Success)
        {
            DriveOwnerTransform(*visual);
            return;
        }
        auto pose = owner->GetWorldTransform();
        pose.position = output.Position;
        // 実効armが0でも方向を定義できる。衝突解決後の位置補間は行わない。
        // 基本姿勢の上方向は世界の上向きとし、対象のロールやピッチを写さない。
        pose.rotation = Math::QuaternionUtils::LookRotation(request.Direction * -1.f, Math::Vector3::UnitY);
        m_FollowPosition = followPosition;
        m_FollowVelocity = followVelocity;
        m_bFollowInitialized = true;
        m_SubjectFade = std::clamp(
            (output.EffectiveLength - m_Profile.FadeStart) / (m_Profile.FadeEnd - m_Profile.FadeStart), 0.f, 1.f);
        if (camera)
        {
            float fov = camera->GetFieldOfView();
            const float wanted = m_Profile.BaseFov + m_Profile.FovGain * speed * speed * m_Profile.SpeedEffectsStrength;
            if (Math::TryCriticalDamp(fov, m_FovVelocity, wanted, m_Profile.FovHalfLife, dt) &&
                fov != camera->GetFieldOfView())
                camera->SetFieldOfView(fov);
            if (m_Profile.bAutoFocus)
            {
                const auto forward = pose.rotation * Math::Vector3::UnitZ;
                float focus = Math::Vector3::Dot(focusPoint - pose.position, forward);
                if (!locked)
                {
                    Core::Scene::PhysicsQueryDesc ray;
                    ray.Ray = Math::Ray(pose.position, forward);
                    ray.MaxDistance = m_Profile.MaximumFocusDistance;
                    ray.MaxHits = 1;
                    ray.Filter = filter;
                    ray.Filter.TriggerPolicy = Core::Scene::EPhysicsQueryTriggerPolicy::Exclude;
                    Core::Container::VariableArray<Core::Scene::PhysicsQueryHit> hits;
                    if (m_Query->ExecuteQuery(ray, hits) == Core::Scene::EPhysicsSceneQueryResult::Success &&
                        !hits.empty())
                        focus = hits[0].Distance;
                }
                focus = std::clamp(focus, .01f, m_Profile.MaximumFocusDistance);
                float current = camera->GetFocusDistance();
                if (current <= 0)
                    current = focus;
                if (Math::TryCriticalDamp(current, m_FocusVelocity, focus, m_Profile.FocusHalfLife, dt) &&
                    current != camera->GetFocusDistance())
                    (void)camera->SetFocusDistance(current);
            }
        }
        m_Trauma = std::max(0.f, m_Trauma - m_Profile.TraumaDecay * dt);
        m_ShakeTime = std::fmod(m_ShakeTime + dt, 1000.f);
        const float shake = m_Trauma * m_Trauma * m_Profile.ShakeStrength * .01745329252f;
        if (shake > 0)
            pose.rotation = pose.rotation *
                            Math::Quaternion(Math::Vector3::UnitY,
                                             std::sin(m_ShakeTime * 23.1f) * m_Profile.ShakeDegrees.x * shake) *
                            Math::Quaternion(Math::Vector3::UnitX,
                                             std::sin(m_ShakeTime * 31.7f) * m_Profile.ShakeDegrees.y * shake) *
                            Math::Quaternion(Math::Vector3::UnitZ,
                                             std::sin(m_ShakeTime * 17.3f) * m_Profile.ShakeDegrees.z * shake);
        m_State = candidate;
        m_Output = output;
        m_CachedPose = pose;
        m_bHasPose = true;
        DriveOwnerTransform(*visual);
        m_PreviousSubject = subject;
        m_bPreviousSubjectValid = true;
    }
    bool FollowCameraComponent::SetFollowSmoothing(float horizontalHalfLife, float verticalHalfLife)
    {
        if (!std::isfinite(horizontalHalfLife) || !std::isfinite(verticalHalfLife) || horizontalHalfLife < 0 ||
            verticalHalfLife < 0)
            return false;
        m_Profile.PositionHalfLives = Math::Vector3(horizontalHalfLife, verticalHalfLife, horizontalHalfLife);
        return true;
    }
    bool FollowCameraComponent::SetProfilePath(Core::Container::AnsiStringView path)
    {
        return m_ProfileReload.SetPath(path);
    }
    void FollowCameraComponent::ReloadProfile(float dt)
    {
        Core::Asset::AssetBlob blob;
        const auto result = m_ProfileReload.Poll(dt, blob);
        if (result == Core::Asset::TextAssetPollResult::Changed)
        {
            FollowCameraProfile next;
            if (ParseFollowCameraProfile(blob.GetSpan(), next) && SetCollisionSettings(next.Collision))
            {
                m_Profile = next;
                m_bProfileReadFailed = false;
                if (!next.bAutoFocus)
                    if (auto* owner = GetOwner())
                        if (auto* camera = owner->GetComponent<Core::Component::CameraComponent>())
                            if (camera->GetFocusDistance() != 0)
                                (void)camera->SetFocusDistance(0);
            }
            else
                NORVES_LOG_WARNING("FollowCamera", "カメラ設定が不正なため以前の値を保持します");
        }
        else if (result == Core::Asset::TextAssetPollResult::ReadFailed && !m_bProfileReadFailed)
        {
            m_bProfileReadFailed = true;
            NORVES_LOG_WARNING("FollowCamera", "カメラ設定を読めないため既定または以前の値を使います");
        }
    }
    float FollowCameraComponent::SubjectSpeed() const
    {
        if (m_SubjectSpeedOverride >= 0)
            return m_SubjectSpeedOverride;
        auto* physical = Resolve(m_PhysicsRootId);
        auto* body = physical ? physical->GetComponent<Physics::CharacterBodyComponent>() : nullptr;
        if (!body || !body->GetState().bReady)
            return 0;
        const auto v = body->GetState().Velocity - body->GetState().PlatformVelocity;
        return std::hypot(v.x, v.z);
    }
    void FollowCameraComponent::SetClinging(bool clinging)
    {
        if (m_bClinging == clinging)
            return;
        m_bClinging = clinging;
        if (clinging)
        {
            m_bClingContinuing = false;
            m_YawVelocity = m_MeasuredYawRate;
            m_PreviousClingTargetYaw = GetYaw();
        }
        else
            m_bClingContinuing = m_YawVelocity != 0;
    }
    bool FollowCameraComponent::IsClinging() const
    {
        return m_bClinging;
    }
    bool FollowCameraComponent::SetLockOn(Core::Container::TWeakPtr<const Core::Camera::ILockOnTarget> target)
    {
        auto resolved = target.lock();
        if (!resolved)
            return false;
        m_LockOn = target;
        m_OwnedLockOn.reset();
        if (!m_bClinging && !m_bClingContinuing)
            m_YawVelocity = 0;
        return true;
    }
    bool FollowCameraComponent::IsLockedOn() const
    {
        return !m_LockOn.expired();
    }
    bool FollowCameraComponent::SetLockOnTarget(Core::Entity* target, float radius)
    {
        if (!std::isfinite(radius) || radius < 0)
            return false;
        auto* owner = GetOwner();
        if (target && (!owner || target->GetWorld() != owner->GetWorld() || target == owner ||
                       target->IsPendingDestroy() || target->GetObjectId() == m_PhysicsRootId))
            return false;
        if (!target)
        {
            ClearLockOn();
            return true;
        }
        m_OwnedLockOn = Core::Container::MakeShared<Core::Camera::EntityLockOnTarget>(target->GetObjectId(), radius);
        m_LockOn = m_OwnedLockOn;
        if (!m_bClinging && !m_bClingContinuing)
            m_YawVelocity = 0;
        return true;
    }
    bool FollowCameraComponent::SetLockOnJoint(Core::Entity* target, uint32_t jointIndex, float radius)
    {
        auto* mesh = target ? target->GetComponent<Core::Component::SkinnedMeshComponent>() : nullptr;
        Math::Transform joint;
        if (!mesh || !mesh->TryGetJointWorldTransform(jointIndex, joint))
        {
            ClearLockOn();
            return false;
        }
        if (!SetLockOnTarget(target, radius))
            return false;
        m_OwnedLockOn =
            Core::Container::MakeShared<Core::Camera::JointLockOnTarget>(target->GetObjectId(), jointIndex, radius);
        m_LockOn = m_OwnedLockOn;
        if (!m_bClinging && !m_bClingContinuing)
            m_YawVelocity = 0;
        return true;
    }
    void FollowCameraComponent::AddTrauma(float amount)
    {
        if (std::isfinite(amount) && amount > 0)
            m_Trauma = std::min(1.f, m_Trauma + amount);
    }
    void FollowCameraComponent::DriveOwnerTransform(const Core::Entity&)
    {
        auto* owner = GetOwner();
        if (m_bHasPose && owner && !owner->IsPendingDestroy())
            owner->SetWorldTransform(m_CachedPose);
    }
} // namespace Game::Gameplay

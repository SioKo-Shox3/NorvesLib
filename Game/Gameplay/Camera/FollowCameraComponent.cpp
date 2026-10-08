#include "Gameplay/Camera/FollowCameraComponent.h"
#include "Camera/SceneCameraProbe.h"
#include "Game/Input/GameInputActions.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include "Physics/ColliderComponent.h"
#include "Physics/RigidBodyComponent.h"
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
    }
    void FollowCameraComponent::Finalize()
    {
        m_Input = nullptr;
        m_Query = nullptr;
        m_PhysicsRootId = m_ViewSourceId = 0;
        m_bHasPose = false;
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
        m_State = {};
        m_bHasPose = false;
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
    void FollowCameraComponent::OnTickGroup(Core::Component::ETickGroup group, float dt)
    {
        if (group == Core::Component::ETickGroup::Input)
            ReadLook();
        else if (group == Core::Component::ETickGroup::Camera)
        {
            if (m_CameraTickCount != UINT64_MAX)
                ++m_CameraTickCount;
            UpdateCamera(dt);
        }
    }
    void FollowCameraComponent::ReadLook()
    {
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
        Core::Camera::SceneCameraProbe probe(*m_Query, filter);
        Core::Camera::CameraCollisionRequest request;
        request.Pivot = visual->GetRenderWorldTransform().position + GetTargetOffset();
        request.Direction = ComputeArmOffset();
        request.DesiredLength = GetArmLength();
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
        pose.rotation = Math::QuaternionUtils::LookRotation(request.Direction * -1.f, Math::Vector3::UnitY);
        m_State = candidate;
        m_Output = output;
        m_CachedPose = pose;
        m_bHasPose = true;
        DriveOwnerTransform(*visual);
    }
    void FollowCameraComponent::DriveOwnerTransform(const Core::Entity&)
    {
        auto* owner = GetOwner();
        if (m_bHasPose && owner && !owner->IsPendingDestroy())
            owner->SetWorldTransform(m_CachedPose);
    }
} // namespace Game::Gameplay

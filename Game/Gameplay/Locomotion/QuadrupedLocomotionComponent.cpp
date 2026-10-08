#include "Gameplay/Locomotion/QuadrupedLocomotionComponent.h"
#include "Component/AnimatorComponent.h"
#include "Engine/Engine.h"
#include "Game/Input/GameInputActions.h"
#include "Locomotion/QuadrupedLocomotionJson.h"
#include "Logging/LogMacros.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <cmath>
#include <limits>
namespace Game::Gameplay
{
    namespace Core = NorvesLib::Core;
    namespace Math = NorvesLib::Math;
    namespace Physics = NorvesLib::Modules::Physics;
    namespace Locomotion = Core::Locomotion;
    using namespace Core::literals;
    IMPLEMENT_CLASS(QuadrupedLocomotionComponent, Core::Component::Component)
    QuadrupedLocomotionComponent::~QuadrupedLocomotionComponent()
    {
        Detach();
    }
    void QuadrupedLocomotionComponent::Initialize()
    {
        Core::Component::Component::Initialize();
        using namespace Core::Component;
        SetTickGroup(ETickGroup::Input);
        SetTickGroupMask(TickGroupBit(ETickGroup::Input) | TickGroupBit(ETickGroup::PoseFinalize) |
                         TickGroupBit(ETickGroup::PostPhysics));
        (void)m_Profile.SetPath("Gameplay/DogLocomotion.json");
    }
    Physics::CharacterBodyComponent* QuadrupedLocomotionComponent::Body() const
    {
        auto* owner = GetOwner();
        return owner ? owner->GetComponent<Physics::CharacterBodyComponent>() : nullptr;
    }
    Core::Entity* QuadrupedLocomotionComponent::Resolve(uint64_t id) const
    {
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        auto* entity = world && id ? world->FindEntityByObjectId(id) : nullptr;
        return entity && !entity->IsPendingDestroy() ? entity : nullptr;
    }
    void QuadrupedLocomotionComponent::ClearIntent()
    {
        m_Intent = {};
        m_JumpEvents.clear();
        m_State.JumpBufferRemaining = 0;
    }
    void QuadrupedLocomotionComponent::BindInput(Core::Input::InputMapper* mapper)
    {
        if (m_Input == mapper)
            return;
        if (m_Input)
            (void)m_Input->SetFixedButtonEventCapture(InputActions::Jump, false);
        m_Input = mapper;
        m_InputGeneration = mapper ? mapper->GetCancellationGeneration() : 0;
        m_JumpGeneration =
            mapper ? mapper->GetAction(InputActions::GameplayContext, InputActions::Jump).CancellationGeneration : 0;
        ClearIntent();
        if (m_Input && IsEnabled())
            (void)m_Input->SetFixedButtonEventCapture(InputActions::Jump, true);
    }
    bool QuadrupedLocomotionComponent::SetProfilePath(Core::Container::AnsiStringView path)
    {
        if (!m_Profile.SetPath(path))
            return false;
        m_ProfileError.clear();
        m_bProfileReadFailed = false;
        return true;
    }
    bool QuadrupedLocomotionComponent::SetDriveMode(Physics::CharacterDriveMode mode)
    {
        auto* body = Body();
        if (!body || body->SetDriveMode(mode) != Physics::EPhysicsResult::Success)
            return false;
        m_LastDrive = mode;
        ResetRootMotion();
        m_State = {};
        ClearIntent();
        if (auto* visual = Resolve(m_VisualRootId))
            visual->SetRenderInterpolationEnabled(mode == Physics::CharacterDriveMode::Fixed);
        return true;
    }
    bool QuadrupedLocomotionComponent::SetVisualRoots(Core::Entity* root, Core::Entity* pose)
    {
        auto* owner = GetOwner();
        if (!owner || !root || !pose || root == pose || root->GetParentEntity() != owner || root->IsPendingDestroy() ||
            pose->IsPendingDestroy() || pose->GetWorld() != owner->GetWorld())
            return false;
        bool below = false;
        for (auto* p = pose->GetParentEntity(); p; p = p->GetParentEntity())
            if (p == root)
            {
                below = true;
                break;
            }
        if (!below)
            return false;
        if (auto* previous = Resolve(m_VisualRootId))
            previous->SetRenderInterpolationEnabled(false);
        ResetRootMotion();
        m_VisualRootId = root->GetObjectId();
        if (m_VisualPoseId != pose->GetObjectId())
            m_VisualBaseRotation = pose->GetLocalTransform().rotation;
        m_VisualPoseId = pose->GetObjectId();
        root->SetRenderInterpolationEnabled(!Body() || Body()->GetDriveMode() == Physics::CharacterDriveMode::Fixed);
        return true;
    }
    bool QuadrupedLocomotionComponent::SetViewSource(Core::Entity* view)
    {
        auto* owner = GetOwner();
        if (!owner || (view && (view->GetWorld() != owner->GetWorld() || view->IsPendingDestroy())))
            return false;
        m_ViewId = view ? view->GetObjectId() : 0;
        return true;
    }
    bool QuadrupedLocomotionComponent::EnsureBinding()
    {
        auto* owner = GetOwner();
        auto* body = Body();
        if (!owner || !body || owner->GetComponent<QuadrupedLocomotionComponent>() != this)
            return false;
        if (m_BoundOwnerId == owner->GetObjectId() && m_BoundBodyId == body->GetComponentId())
            return true;
        ++m_BindingGeneration;
        m_BoundOwnerId = owner->GetObjectId();
        m_BoundBodyId = body->GetComponentId();
        m_LastDrive = body->GetDriveMode();
        m_State = {};
        ClearIntent();
        auto* world = owner->GetWorld();
        const auto generation = m_BindingGeneration;
        const auto ownerId = m_BoundOwnerId, componentId = GetComponentId(), bodyId = m_BoundBodyId;
        m_Before = Core::Delegate<void, float>([world, ownerId, componentId, bodyId, generation](float dt) {
            auto* entity = world->FindEntityByObjectId(ownerId);
            if (!entity || entity->IsPendingDestroy())
                return;
            auto* driver = entity->GetComponent<QuadrupedLocomotionComponent>();
            auto* current = entity->GetComponent<Physics::CharacterBodyComponent>();
            if (driver && current && !driver->IsPendingDestroy() && !current->IsPendingDestroy() &&
                driver->GetComponentId() == componentId && driver->m_BindingGeneration == generation &&
                current->GetComponentId() == bodyId && driver->IsActive() && driver->IsTickEnabled())
                driver->Simulate(dt);
        });
        body->BeforeSimulation.Add(static_cast<const Core::Delegate<void, float>&>(m_Before));
        (void)body->SetDesiredVelocity(Math::Vector3::Zero);
        return true;
    }
    void QuadrupedLocomotionComponent::Detach()
    {
        ++m_BindingGeneration;
        // Outerが先に解除されるObjectHeap経路でも、破棄済Worldを参照しない。
        if (auto* owner = GetOwner())
            if (owner->GetObjectId() == m_BoundOwnerId)
                if (auto* body = owner->GetComponent<Physics::CharacterBodyComponent>())
                    if (body->GetComponentId() == m_BoundBodyId)
                    {
                        body->BeforeSimulation.Remove(m_Before);
                        body->SetVariableTickAfterAnimation(false);
                    }
        ResetRootMotion();
        m_BoundOwnerId = m_BoundBodyId = 0;
        m_Before = {};
        BindInput(nullptr);
        ClearIntent();
    }
    void QuadrupedLocomotionComponent::EndPlay()
    {
        Detach();
        Core::Component::Component::EndPlay();
    }
    void QuadrupedLocomotionComponent::Finalize()
    {
        Detach();
        Core::Component::Component::Finalize();
    }
    void QuadrupedLocomotionComponent::Enable()
    {
        Core::Component::Component::Enable();
        if (m_Input && IsEnabled())
            (void)m_Input->SetFixedButtonEventCapture(InputActions::Jump, true);
    }
    void QuadrupedLocomotionComponent::Disable()
    {
        if (m_Input)
            (void)m_Input->SetFixedButtonEventCapture(InputActions::Jump, false);
        ClearIntent();
        m_State = {};
        ResetRootMotion();
        Core::Component::Component::Disable();
    }
    void QuadrupedLocomotionComponent::OnTickGroup(Core::Component::ETickGroup group, float dt)
    {
        if (group == Core::Component::ETickGroup::Input)
            CollectInput(dt);
        else if (group == Core::Component::ETickGroup::PoseFinalize)
            CaptureRootMotion();
        else if (group == Core::Component::ETickGroup::PostPhysics)
        {
            FinishRootMotionFrame();
            PublishVisual(dt);
        }
    }
    bool QuadrupedLocomotionComponent::ValidateInput()
    {
        const auto jumpGeneration =
            m_Input ? m_Input->GetAction(InputActions::GameplayContext, InputActions::Jump).CancellationGeneration : 0;
        if (jumpGeneration != m_JumpGeneration)
        {
            m_JumpGeneration = jumpGeneration;
            m_JumpEvents.clear();
            m_State.JumpBufferRemaining = 0;
        }
        if (m_Input && m_InputGeneration != m_Input->GetCancellationGeneration())
        {
            m_InputGeneration = m_Input->GetCancellationGeneration();
            ClearIntent();
        }
        if (!m_Input || m_Input->GetActiveContext() != InputActions::GameplayContext ||
            !m_Input->GetAction(InputActions::Move).Active)
        {
            ClearIntent();
            return false;
        }
        return true;
    }
    void QuadrupedLocomotionComponent::CollectInput(float dt)
    {
        if (!std::isfinite(dt) || dt < 0 || !EnsureBinding())
            return;
        auto* body = Body();
        body->SetVariableTickAfterAnimation(m_MotionSource == LocomotionMotionSource::Animation);
        auto* pose = Resolve(m_VisualPoseId);
        auto* animator = pose ? pose->GetComponent<Core::Component::AnimatorComponent>() : nullptr;
        m_AnimationSerialAtInput = animator ? animator->GetRootMotionFrame().UpdateSerial : 0;
        if (body->GetDriveMode() != m_LastDrive)
        {
            m_LastDrive = body->GetDriveMode();
            ResetRootMotion();
            m_State = {};
            ClearIntent();
        }
        if (auto* root = Resolve(m_VisualRootId))
            root->SetRenderInterpolationEnabled(m_LastDrive == Physics::CharacterDriveMode::Fixed);
        Core::Asset::AssetBlob blob;
        const auto poll = m_Profile.Poll(dt, blob);
        if (poll == Core::Asset::TextAssetPollResult::Changed)
        {
            m_bProfileReadFailed = false;
            if (!Locomotion::ParseQuadrupedLocomotionJson(blob.GetSpan(), m_Params, m_ProfileError))
                NORVES_LOG_WARNING("Locomotion", "移動設定が不正なため以前の値を保持します");
        }
        else if (poll == Core::Asset::TextAssetPollResult::ReadFailed && !m_bProfileReadFailed)
        {
            m_bProfileReadFailed = true;
            NORVES_LOG_WARNING("Locomotion", "移動設定を読めないため以前の値を保持します");
        }
        if (!ValidateInput())
            return;
        (void)m_Input->SetFixedButtonEventCapture(InputActions::Jump, true);
        const auto move = m_Input->GetAction(InputActions::Move).Axis;
        Math::Vector3 forward = Math::Vector3::UnitZ;
        if (auto* view = Resolve(m_ViewId))
        {
            forward = view->GetWorldTransform().rotation * Math::Vector3::UnitZ;
            forward.y = 0;
            const float length = forward.Length();
            forward = length > 1e-5f ? forward / length : Math::Vector3::UnitZ;
        }
        const Math::Vector3 right(forward.z, 0, -forward.x);
        m_Intent.Move = right * move.x + forward * move.y;
        m_Intent.bSprint = m_Input->GetAction(InputActions::Sprint).Button.Held;
        Core::Input::InputButtonEvent event;
        while (m_Input->ConsumeFixedButtonEvent(InputActions::Jump, event))
            m_JumpEvents.push_back(event);
    }
    bool QuadrupedLocomotionComponent::SetMotionSource(LocomotionMotionSource source)
    {
        if (source != LocomotionMotionSource::Procedural && source != LocomotionMotionSource::Animation)
            return false;
        if (m_MotionSource != source)
        {
            m_MotionSource = source;
            ResetRootMotion();
        }
        if (auto* body = Body())
            body->SetVariableTickAfterAnimation(source == LocomotionMotionSource::Animation);
        return true;
    }
    void QuadrupedLocomotionComponent::ResetRootMotion()
    {
        m_RootMotion.Clear();
        m_RootAnimatorId = m_RootSourceGeneration = m_RootBodyGeneration = 0;
    }
    void QuadrupedLocomotionComponent::CaptureRootMotion()
    {
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        auto* body = Body();
        if (!world || !body || m_RootCaptureTick == world->GetTickSerial())
            return;
        m_RootCaptureTick = world->GetTickSerial();
        auto* pose = Resolve(m_VisualPoseId);
        auto* animator = pose ? pose->GetComponent<Core::Component::AnimatorComponent>() : nullptr;
        if (!animator || !animator->IsActive())
        {
            ResetRootMotion();
            return;
        }
        if (m_MotionSource == LocomotionMotionSource::Procedural)
        {
            (void)animator->ConsumeRootMotion();
            ResetRootMotion();
            return;
        }
        const auto frame = animator->GetRootMotionFrame();
        if (!frame.bValid || frame.UpdateSerial == m_AnimationSerialAtInput)
        {
            (void)animator->ConsumeRootMotion();
            ResetRootMotion();
            return;
        }
        Core::Animation::RootMotionDelta delta;
        if (!animator->TryConsumeRootMotionFrame(frame.UpdateSerial, delta))
        {
            ResetRootMotion();
            return;
        }
        const auto* engine = Core::Engine::GEngine;
        const bool fixed = body->GetDriveMode() == Physics::CharacterDriveMode::Fixed;
        if (fixed && (!engine || !engine->GetFixedStepFrameTiming().Serial))
        {
            ResetRootMotion();
            return;
        }
        if (m_RootAnimatorId != animator->GetComponentId() || m_RootSourceGeneration != frame.SourceGeneration ||
            m_RootBodyGeneration != body->GetMotionGeneration())
        {
            ResetRootMotion();
            m_RootAnimatorId = animator->GetComponentId();
            m_RootSourceGeneration = frame.SourceGeneration;
            m_RootBodyGeneration = body->GetMotionGeneration();
            if (fixed && engine->GetFixedStepFrameTiming().StartRemainderScaledUnits)
                (void)m_RootMotion.Push({}, engine->GetFixedStepFrameTiming().StartRemainderScaledUnits);
        }
        uint64_t duration = 1000000000;
        if (fixed)
        {
            const auto& timing = engine->GetFixedStepFrameTiming();
            if (timing.InputNanoseconds <= 0)
                return;
            if (uint64_t(timing.InputNanoseconds) > std::numeric_limits<uint64_t>::max() / timing.Rate)
            {
                ResetRootMotion();
                return;
            }
            duration = uint64_t(timing.InputNanoseconds) * timing.Rate;
        }
        else
        {
            m_RootMotion.Clear();
            float dt = 0;
            if (!world->TryGetComponentTickDelta(*body, dt) || dt <= 0)
                return;
        }
        if (!m_RootMotion.Push(delta, duration))
        {
            NORVES_LOG_WARNING("Locomotion", "ルート移動の時間区間を保持できないため、この区間を破棄します");
            ResetRootMotion();
        }
    }
    void QuadrupedLocomotionComponent::FinishRootMotionFrame()
    {
        auto* body = Body();
        auto* owner = GetOwner();
        auto* world = owner ? owner->GetWorld() : nullptr;
        auto* engine = Core::Engine::GEngine;
        if (!world || !engine || !body || m_MotionSource != LocomotionMotionSource::Animation ||
            body->GetDriveMode() != Physics::CharacterDriveMode::Fixed || m_RootFinishedTick == world->GetTickSerial())
            return;
        m_RootFinishedTick = world->GetTickSerial();
        const auto& timing = engine->GetFixedStepFrameTiming();
        if (!timing.bScheduled)
            return;
        const auto dropped = timing.Advance.DroppedSteps;
        m_RootMotion.Discard(dropped > std::numeric_limits<uint64_t>::max() / 1000000000
                                 ? std::numeric_limits<uint64_t>::max()
                                 : dropped * 1000000000);
        const auto remainder = timing.Advance.RemainderScaledUnits;
        // 未準備/無効化で実行されなかった時間も繰り越さず、schedulerの端数だけを残す。
        if (m_RootMotion.GetRemainingTime() != remainder)
        {
            m_RootMotion.Clear();
            if (remainder)
                (void)m_RootMotion.Push({}, remainder);
        }
    }
    void QuadrupedLocomotionComponent::Simulate(float dt)
    {
        (void)ValidateInput();
        auto* body = Body();
        auto* owner = GetOwner();
        if (!body || !owner || !std::isfinite(dt) || dt <= 0)
            return;
        Core::Animation::RootMotionDelta rootDelta;
        if (m_MotionSource == LocomotionMotionSource::Animation)
        {
            if (m_RootBodyGeneration != body->GetMotionGeneration())
                ResetRootMotion();
            const uint64_t duration = body->GetDriveMode() == Physics::CharacterDriveMode::Fixed
                                          ? 1000000000
                                          : m_RootMotion.GetRemainingTime();
            if (!m_RootMotion.Consume(duration, rootDelta))
            {
                ResetRootMotion();
                return;
            }
        }
        if (!body->GetState().bReady)
            return;
        const auto observed = body->GetState();
        Locomotion::LocomotionGroundInfo ground;
        ground.bReady = observed.bReady;
        ground.bGrounded = observed.bGrounded;
        ground.Normal = observed.GroundNormal;
        ground.Velocity = observed.Velocity;
        ground.PlatformVelocity = observed.PlatformVelocity;
        ground.Gravity = 9.81f * body->GetSettings().GravityScale;
        auto candidate = m_State;
        const auto forward = owner->GetWorldTransform().rotation * Math::Vector3::UnitZ;
        candidate.Yaw = std::atan2(forward.x, forward.z);
        auto intent = m_Intent;
        for (const auto& event : m_JumpEvents)
            intent.bJumpPressed = intent.bJumpPressed || event.Type == Core::Input::EInputButtonEventType::Pressed;
        Locomotion::LocomotionOutput output;
        if (Locomotion::QuadrupedLocomotionModel::Step(candidate, m_Params, intent, ground, dt, output) !=
            Locomotion::LocomotionStepStatus::Success)
            return;
        auto delta = output.PlanarDisplacement;
        float yaw = output.DeltaYaw;
        if (m_MotionSource == LocomotionMotionSource::Animation)
        {
            const double startYaw = std::atan2(forward.x, forward.z);
            const double c = std::cos(startYaw), s = std::sin(startYaw);
            delta =
                Math::Vector3(float(c * rootDelta.X + s * rootDelta.Z), 0, float(-s * rootDelta.X + c * rootDelta.Z));
            yaw = float(rootDelta.Yaw);
            candidate.Yaw = std::atan2(forward.x, forward.z) + yaw;
            output.Yaw = candidate.Yaw;
            output.DeltaYaw = yaw;
            output.TurnRate = yaw / dt;
        }
        if (body->SetDesiredVelocity(Math::Vector3::Zero) != Physics::EPhysicsResult::Success ||
            body->MoveDelta(delta, yaw) != Physics::EPhysicsResult::Success)
            return;
        if (output.bJumpRequested && body->LaunchVertical(output.JumpSpeed) == Physics::EPhysicsResult::Success)
            Locomotion::QuadrupedLocomotionModel::AcknowledgeJump(candidate);
        for (const auto& event : m_JumpEvents)
        {
            (void)event;
            if (m_ConsumedJumpEvents != UINT64_MAX)
                ++m_ConsumedJumpEvents;
        }
        m_JumpEvents.clear();
        m_State = candidate;
        m_Output = output;
    }
    void QuadrupedLocomotionComponent::PublishVisual(float)
    {
        auto* pose = Resolve(m_VisualPoseId);
        auto* body = Body();
        if (!pose || !body)
            return;
        // 傾きだけを見た目の子へ適用。VisualRootの固定履歴と直立capsuleは変更しない。
        pose->SetLocalRotation(Math::Quaternion(Math::Vector3::UnitZ, m_Output.Bank) *
                               Math::Quaternion(Math::Vector3::UnitX, -m_Output.Pitch) * m_VisualBaseRotation);
        if (auto* animator = pose->GetComponent<Core::Component::AnimatorComponent>())
        {
            const auto& state = body->GetState();
            Core::Animation::AnimDriveSignals signals;
            const auto relative = state.Velocity - state.PlatformVelocity;
            signals.Speed = std::hypot(relative.x, relative.z);
            signals.Velocity = state.Velocity;
            signals.TurnRate = m_Output.TurnRate;
            signals.bGrounded = state.bGrounded;
            signals.GroundNormal = state.GroundNormal;
            (void)animator->SetDriveSignals(signals);
            (void)animator->SetInt(animator->FindParam("GaitId"_id), m_Output.Gait);
            constexpr Core::Identity weights[] = {"GaitWeight0"_id, "GaitWeight1"_id, "GaitWeight2"_id,
                                                  "GaitWeight3"_id};
            for (unsigned i = 0; i < 4; ++i)
                (void)animator->SetFloat(animator->FindParam(weights[i]), m_Output.GaitWeights[i]);
            (void)animator->SetFloat(animator->FindParam("PitchAngle"_id), m_Output.Pitch);
            (void)animator->SetFloat(animator->FindParam("BankAngle"_id), m_Output.Bank);
        }
    }
} // namespace Game::Gameplay

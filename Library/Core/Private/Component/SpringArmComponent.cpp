#include "Component/SpringArmComponent.h"
#include "Logging/LogMacros.h"
#include "Math/MathTypes.h"
#include "Math/QuaternionUtils.h"
#include "Math/Vector3.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <algorithm>
#include <cmath>

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(SpringArmComponent, Component)

    namespace
    {
        constexpr float DefaultArmLength = 5.0f;
        constexpr float DefaultYaw = 0.0f;
        constexpr float DefaultPitch = 30.0f;
        constexpr float DefaultMinPitch = -89.0f;
        constexpr float DefaultMaxPitch = 89.0f;
        constexpr float DefaultMinArmLength = 0.1f;
        constexpr float DefaultMaxArmLength = 10000.0f;
        constexpr float MaxSafePitch = 89.9f;
        constexpr float MinSafeArmLength = 0.01f;

        float NormalizeYaw(float yaw)
        {
            yaw = std::fmod(yaw, 360.0f);
            return yaw < 0.0f ? yaw + 360.0f : yaw;
        }
    }

    SpringArmComponent::SpringArmComponent()
        : Component()
    {
        PivotObjectId = 0;
        ArmLength = DefaultArmLength;
        Yaw = DefaultYaw;
        Pitch = DefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = DefaultMinPitch;
        MaxPitch = DefaultMaxPitch;
        MinArmLength = DefaultMinArmLength;
        MaxArmLength = DefaultMaxArmLength;
    }

    SpringArmComponent::SpringArmComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        PivotObjectId = 0;
        ArmLength = DefaultArmLength;
        Yaw = DefaultYaw;
        Pitch = DefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = DefaultMinPitch;
        MaxPitch = DefaultMaxPitch;
        MinArmLength = DefaultMinArmLength;
        MaxArmLength = DefaultMaxArmLength;
    }

    SpringArmComponent::SpringArmComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        PivotObjectId = 0;
        ArmLength = DefaultArmLength;
        Yaw = DefaultYaw;
        Pitch = DefaultPitch;
        TargetOffset = Math::Vector3::Zero;
        MinPitch = DefaultMinPitch;
        MaxPitch = DefaultMaxPitch;
        MinArmLength = DefaultMinArmLength;
        MaxArmLength = DefaultMaxArmLength;
    }

    SpringArmComponent::~SpringArmComponent() = default;

    void SpringArmComponent::Initialize()
    {
        Component::Initialize();
    }

    void SpringArmComponent::Finalize()
    {
        Component::Finalize();
    }

    void SpringArmComponent::BeginPlay()
    {
        Component::BeginPlay();
    }

    void SpringArmComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void SpringArmComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
        RefreshOwnerTransform();
    }

    bool SpringArmComponent::SetPivot(const Entity* pivot)
    {
        if (!pivot)
        {
            ClearPivot();
            return true;
        }

        const Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!owner || !world || owner->IsPendingDestroy() || pivot == owner ||
            pivot->IsPendingDestroy() || pivot->GetWorld() != world ||
            world->FindEntityByObjectId(pivot->GetObjectId()) != pivot)
        {
            NORVES_LOG_WARNING("SpringArmComponent", "Rejected invalid pivot assignment");
            return false;
        }

        PivotObjectId = pivot->GetObjectId();
        MarkRenderStateDirty();
        return true;
    }

    void SpringArmComponent::SetPivotObjectId(uint64_t objectId)
    {
        PivotObjectId = objectId;
        MarkRenderStateDirty();
    }

    void SpringArmComponent::ClearPivot()
    {
        PivotObjectId = 0;
        MarkRenderStateDirty();
    }

    bool SpringArmComponent::HasValidPivot() const
    {
        return ResolvePivot() != nullptr;
    }

    Entity* SpringArmComponent::ResolvePivot() const
    {
        const Entity* owner = GetOwner();
        if (!owner || owner->IsPendingDestroy() || PivotObjectId.Get() == 0)
        {
            return nullptr;
        }

        World* world = owner->GetWorld();
        if (!world)
        {
            return nullptr;
        }

        Entity* pivot = world->FindEntityByObjectId(PivotObjectId);
        return pivot != owner ? pivot : nullptr;
    }

    void SpringArmComponent::SetArmLength(float armLength)
    {
        ArmLength = std::clamp(armLength, MinArmLength.Get(), MaxArmLength.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetYaw(float yaw)
    {
        Yaw = NormalizeYaw(yaw);
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetPitch(float pitch)
    {
        Pitch = std::clamp(pitch, MinPitch.Get(), MaxPitch.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetTargetOffset(const Math::Vector3& offset)
    {
        TargetOffset = offset;
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetPitchLimits(float minPitch, float maxPitch)
    {
        if (minPitch > maxPitch)
        {
            std::swap(minPitch, maxPitch);
        }

        MinPitch = std::clamp(minPitch, -MaxSafePitch, MaxSafePitch);
        MaxPitch = std::clamp(maxPitch, -MaxSafePitch, MaxSafePitch);
        Pitch = std::clamp(Pitch.Get(), MinPitch.Get(), MaxPitch.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::SetArmLengthLimits(float minArmLength, float maxArmLength)
    {
        if (minArmLength > maxArmLength)
        {
            std::swap(minArmLength, maxArmLength);
        }

        minArmLength = std::max(minArmLength, MinSafeArmLength);
        maxArmLength = std::max(maxArmLength, minArmLength);
        MinArmLength = minArmLength;
        MaxArmLength = maxArmLength;
        ArmLength = std::clamp(ArmLength.Get(), MinArmLength.Get(), MaxArmLength.Get());
        MarkRenderStateDirty();
    }

    void SpringArmComponent::ApplyIntent(const SpringArmIntent& intent)
    {
        Yaw = NormalizeYaw(Yaw.Get() + intent.YawDelta);
        Pitch = std::clamp(Pitch.Get() + intent.PitchDelta, MinPitch.Get(), MaxPitch.Get());
        ArmLength = std::clamp(
            ArmLength.Get() - intent.DollyDelta,
            MinArmLength.Get(),
            MaxArmLength.Get());

        if (intent.PanDelta.x != 0.0f || intent.PanDelta.y != 0.0f)
        {
            Entity* pivot = ResolvePivot();
            if (pivot)
            {
                const Math::Vector3 forward = (ComputeArmOffset() * -1.0f).Normalized();
                const Math::Quaternion rotation =
                    Math::QuaternionUtils::LookRotation(forward, Math::Vector3::Up);
                const Math::Vector3 right = rotation * Math::Vector3::Right;
                const Math::Vector3 up = rotation * Math::Vector3::Up;
                const Math::Vector3 worldDelta =
                    (right * -1.0f) * intent.PanDelta.x + up * intent.PanDelta.y;
                pivot->SetPosition(pivot->GetPosition() + worldDelta);
            }
        }

        MarkRenderStateDirty();
    }

    void SpringArmComponent::RefreshOwnerTransform()
    {
        Entity* pivot = ResolvePivot();
        if (pivot)
        {
            DriveOwnerTransform(*pivot);
        }
    }

    Math::Vector3 SpringArmComponent::ComputeArmOffset() const
    {
        const float yawRadians = Yaw.Get() * Math::Constants::PI / 180.0f;
        const float pitchRadians = Pitch.Get() * Math::Constants::PI / 180.0f;
        const float cosPitch = std::cos(pitchRadians);
        const float distance = ArmLength.Get();
        return Math::Vector3(
            distance * cosPitch * std::sin(yawRadians),
            distance * std::sin(pitchRadians),
            distance * cosPitch * std::cos(yawRadians));
    }

    void SpringArmComponent::DriveOwnerTransform(const Entity& pivot)
    {
        Entity* owner = GetOwner();
        if (!owner || owner->IsPendingDestroy())
        {
            return;
        }

        const Math::Vector3 target = pivot.GetPosition() + TargetOffset.Get();
        const Math::Vector3 cameraPosition = target + ComputeArmOffset();
        const Math::Vector3 forward = (target - cameraPosition).Normalized();
        const Math::Quaternion rotation =
            Math::QuaternionUtils::LookRotation(forward, Math::Vector3::Up);
        owner->SetPosition(cameraPosition);
        owner->SetRotation(rotation);
    }
} // namespace NorvesLib::Core::Component

#include "Component/CameraComponent.h"
#include "Math/Quaternion.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include "Object/Entity.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(CameraComponent, Component)

    CameraComponent::CameraComponent()
        : Component()
    {
        SetDefaults();
    }

    CameraComponent::CameraComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    CameraComponent::CameraComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    CameraComponent::~CameraComponent() = default;

    void CameraComponent::Initialize()
    {
        Component::Initialize();
    }

    void CameraComponent::Finalize()
    {
        Component::Finalize();
    }

    void CameraComponent::BeginPlay()
    {
        Component::BeginPlay();
    }

    void CameraComponent::EndPlay()
    {
        Component::EndPlay();
    }

    void CameraComponent::Tick(float deltaTime)
    {
        (void)deltaTime;
    }

    bool CameraComponent::BuildCameraProxy(Rendering::CameraProxy& outProxy) const
    {
        const Entity* owner = GetOwner();
        if (owner == nullptr)
        {
            return false;
        }

        const Math::Transform& worldTransform = owner->GetWorldTransform();
        const Math::Vector3 forward = worldTransform.rotation * Math::Vector3::Forward;
        const Math::Vector3 up = worldTransform.rotation * Math::Vector3::Up;
        const Math::Vector3 right = worldTransform.rotation * Math::Vector3::Right;

        Rendering::CameraProxy snapshot = outProxy;
        snapshot.CameraId = GetComponentId();
        snapshot.PositionX = worldTransform.position.x;
        snapshot.PositionY = worldTransform.position.y;
        snapshot.PositionZ = worldTransform.position.z;
        snapshot.ForwardX = forward.x;
        snapshot.ForwardY = forward.y;
        snapshot.ForwardZ = forward.z;
        snapshot.UpX = up.x;
        snapshot.UpY = up.y;
        snapshot.UpZ = up.z;
        snapshot.RightX = right.x;
        snapshot.RightY = right.y;
        snapshot.RightZ = right.z;
        snapshot.Projection = ProjectionTypeProp;
        snapshot.FieldOfView = FieldOfView;
        snapshot.NearPlane = NearPlane;
        snapshot.FarPlane = FarPlane;
        snapshot.OrthoWidth = OrthoWidth;
        snapshot.OrthoHeight = OrthoHeight;
        snapshot.Viewport = m_Viewport;
        snapshot.CullingMask = CullingMaskProp;
        snapshot.RenderOrder = RenderOrder;

        outProxy = snapshot;
        return true;
    }

    void CameraComponent::SetDefaults()
    {
        ProjectionTypeProp = Rendering::ProjectionType::Perspective;
        FieldOfView = 60.0f;
        NearPlane = 0.1f;
        FarPlane = 1000.0f;
        OrthoWidth = 10.0f;
        OrthoHeight = 10.0f;
        RenderOrder = 0;
        CullingMaskProp = Rendering::RenderLayer::All;
        bIsActiveCamera = false;
    }
} // namespace NorvesLib::Core::Component

#include "Component/CameraComponent.h"
#include "Object/WorldObject.h"
#include "Math/Vector3.h"
#include "Math/Quaternion.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Component
{
    IMPLEMENT_CLASS(CameraComponent, Component)

    CameraComponent::CameraComponent()
        : Component()
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

    CameraComponent::CameraComponent(const FieldInitializer *initializer)
        : Component(initializer)
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

    CameraComponent::CameraComponent(const IUnknown *sourceObject)
        : Component(sourceObject)
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

    CameraComponent::~CameraComponent()
    {
    }

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
        // カメラの位置・姿勢はオーナーのWorldObjectからBuildCameraProxy時に取得するため
        // Tick内での特別な処理は不要
    }

    // ========================================
    // CameraProxy構築
    // ========================================

    bool CameraComponent::BuildCameraProxy(Rendering::CameraProxy &outProxy) const
    {
        const WorldObject *owner = GetOwner();
        if (owner == nullptr)
        {
            return false;
        }

        // CameraIdはComponentIdを使用（一意性を保証）
        outProxy.CameraId = GetComponentId();

        // 位置: オーナーのWorldObjectから取得
        const Math::Vector3 &position = owner->GetPosition();
        outProxy.PositionX = position.x;
        outProxy.PositionY = position.y;
        outProxy.PositionZ = position.z;

        // 向き: オーナーの回転（クォータニオン）から基底ベクトルを導出。
        // ローカル +Z を forward とする規約（QuaternionUtils::LookRotation と一致）。
        const Math::Quaternion &rotation = owner->GetRotation();
        const Math::Vector3 forward = rotation * Math::Vector3::Forward;
        const Math::Vector3 up = rotation * Math::Vector3::Up;
        const Math::Vector3 right = rotation * Math::Vector3::Right;

        outProxy.ForwardX = forward.x;
        outProxy.ForwardY = forward.y;
        outProxy.ForwardZ = forward.z;

        outProxy.UpX = up.x;
        outProxy.UpY = up.y;
        outProxy.UpZ = up.z;

        outProxy.RightX = right.x;
        outProxy.RightY = right.y;
        outProxy.RightZ = right.z;

        // レンズ設定
        outProxy.Projection = ProjectionTypeProp;
        outProxy.FieldOfView = FieldOfView;
        outProxy.NearPlane = NearPlane;
        outProxy.FarPlane = FarPlane;
        outProxy.OrthoWidth = OrthoWidth;
        outProxy.OrthoHeight = OrthoHeight;
        outProxy.Viewport = m_Viewport;
        outProxy.CullingMask = CullingMaskProp;
        outProxy.RenderOrder = RenderOrder;

        // AspectRatioは描画解像度に追従させるため触らない（CameraProxyの既定値のまま）。

        return true;
    }

} // namespace NorvesLib::Core::Component

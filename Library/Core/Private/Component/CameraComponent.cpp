#include "Component/CameraComponent.h"
#include "Math/Quaternion.h"
#include "Math/Transform.h"
#include "Math/Vector3.h"
#include "Object/Entity.h"
#include <cmath>
#include <limits>

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool TryConvertFiniteFloat(double value, float& outValue)
        {
            const double maxFloat = static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(value) || value < -maxFloat || value > maxFloat)
            {
                return false;
            }
            outValue = static_cast<float>(value);
            return std::isfinite(outValue);
        }

        bool TryConvertPositiveFloat(double value, float& outValue)
        {
            if (!TryConvertFiniteFloat(value, outValue) || outValue <= 0.0f)
            {
                return false;
            }
            return true;
        }
    }

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

    bool CameraComponent::TryBuildExposureSnapshot(
        float aperture,
        float shutterSpeed,
        float iso,
        float exposureCompensation,
        Rendering::CameraProxy& outSnapshot)
    {
        if (!std::isfinite(aperture) || aperture <= 0.0f ||
            !std::isfinite(shutterSpeed) || shutterSpeed <= 0.0f ||
            !std::isfinite(iso) || iso <= 0.0f ||
            !std::isfinite(exposureCompensation))
        {
            return false;
        }

        const double apertureValue = static_cast<double>(aperture);
        const double shutterValue = static_cast<double>(shutterSpeed);
        const double isoValue = static_cast<double>(iso);
        const double exposureArgument =
            (apertureValue * apertureValue / shutterValue) * (100.0 / isoValue);
        if (!std::isfinite(exposureArgument) || exposureArgument <= 0.0)
        {
            return false;
        }

        const double ev100Value = std::log2(exposureArgument);
        const double exposureValue =
            std::exp2(static_cast<double>(exposureCompensation) - ev100Value) / 1.2;
        const double invPreExposureValue = 1.0 / exposureValue;

        float ev100 = 0.0f;
        float exposure = 0.0f;
        float invPreExposure = 0.0f;
        if (!TryConvertFiniteFloat(ev100Value, ev100) ||
            !TryConvertPositiveFloat(exposureValue, exposure) ||
            !TryConvertPositiveFloat(invPreExposureValue, invPreExposure))
        {
            return false;
        }

        Rendering::CameraProxy snapshot = outSnapshot;
        snapshot.Aperture = aperture;
        snapshot.ShutterSpeed = shutterSpeed;
        snapshot.ISO = iso;
        snapshot.ExposureCompensation = exposureCompensation;
        snapshot.EV100 = ev100;
        snapshot.Exposure = exposure;
        snapshot.PreExposure = exposure;
        snapshot.InvPreExposure = invPreExposure;
        outSnapshot = snapshot;
        return true;
    }

    bool CameraComponent::SetAperture(float aperture)
    {
        Rendering::CameraProxy validation;
        if (!TryBuildExposureSnapshot(
                aperture,
                ShutterSpeed.Get(),
                ISO.Get(),
                ExposureCompensation.Get(),
                validation))
        {
            return false;
        }
        if (Aperture.Get() != aperture)
        {
            Aperture = aperture;
            MarkRenderStateDirty();
        }
        return true;
    }

    bool CameraComponent::SetShutterSpeed(float shutterSpeed)
    {
        Rendering::CameraProxy validation;
        if (!TryBuildExposureSnapshot(
                Aperture.Get(),
                shutterSpeed,
                ISO.Get(),
                ExposureCompensation.Get(),
                validation))
        {
            return false;
        }
        if (ShutterSpeed.Get() != shutterSpeed)
        {
            ShutterSpeed = shutterSpeed;
            MarkRenderStateDirty();
        }
        return true;
    }

    bool CameraComponent::SetISO(float iso)
    {
        Rendering::CameraProxy validation;
        if (!TryBuildExposureSnapshot(
                Aperture.Get(),
                ShutterSpeed.Get(),
                iso,
                ExposureCompensation.Get(),
                validation))
        {
            return false;
        }
        if (ISO.Get() != iso)
        {
            ISO = iso;
            MarkRenderStateDirty();
        }
        return true;
    }

    bool CameraComponent::SetExposureCompensation(float exposureCompensation)
    {
        Rendering::CameraProxy validation;
        if (!TryBuildExposureSnapshot(
                Aperture.Get(),
                ShutterSpeed.Get(),
                ISO.Get(),
                exposureCompensation,
                validation))
        {
            return false;
        }
        if (ExposureCompensation.Get() != exposureCompensation)
        {
            ExposureCompensation = exposureCompensation;
            MarkRenderStateDirty();
        }
        return true;
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
        if (!TryBuildExposureSnapshot(
                Aperture.Get(),
                ShutterSpeed.Get(),
                ISO.Get(),
                ExposureCompensation.Get(),
                snapshot))
        {
            return false;
        }

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
        Aperture = 4.0f;
        ShutterSpeed = 1.0f / 60.0f;
        ISO = 100.0f;
        ExposureCompensation = 0.0f;
    }
} // namespace NorvesLib::Core::Component

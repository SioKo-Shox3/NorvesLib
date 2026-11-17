// 持ち主の変換から局所霧を作り、変更時だけ描画側へ公開する。
#include "Component/LocalFogVolumeComponent.h"

#include "Engine/Engine.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <cmath>

namespace NorvesLib::Core::Component
{
    namespace
    {
        bool IsFinite(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool GetOwnerTransform(const Entity* owner, Math::Transform& out)
        {
            if (!owner || owner->IsPendingDestroy())
            {
                return false;
            }
            out = owner->GetWorldTransform();
            const auto& rotation = out.rotation;
            const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y +
                rotation.z * rotation.z + rotation.w * rotation.w;
            if (!IsFinite(out.position) || !IsFinite(out.scale) ||
                out.scale.x == 0.0f || out.scale.y == 0.0f || out.scale.z == 0.0f ||
                !std::isfinite(lengthSquared) || lengthSquared <= 0.0f)
            {
                return false;
            }
            const float length = std::sqrt(lengthSquared);
            out.rotation = Math::Quaternion(rotation.x / length, rotation.y / length,
                                            rotation.z / length, rotation.w / length);
            return true;
        }
    }

    IMPLEMENT_CLASS(LocalFogVolumeComponent, Component)

    LocalFogVolumeComponent::LocalFogVolumeComponent()
        : Component()
    {
        SetDefaults();
    }

    LocalFogVolumeComponent::LocalFogVolumeComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    LocalFogVolumeComponent::LocalFogVolumeComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    LocalFogVolumeComponent::~LocalFogVolumeComponent() = default;

    void LocalFogVolumeComponent::SetDefaults()
    {
        CenterOffset = Math::Vector3::Zero;
        Radius = Math::Vector3(4.0f, 2.0f, 4.0f);
        Density = 0.05f;
        Color = Math::Vector3::One;
        Falloff = 2.0f;
        NoiseAmount = 0.5f;
        NoiseScaleMeters = 4.0f;
        SetTickGroup(ETickGroup::PreRender);
    }

    void LocalFogVolumeComponent::Initialize()
    {
        Component::Initialize();
    }

    void LocalFogVolumeComponent::Finalize()
    {
        Component::Finalize();
    }

    void LocalFogVolumeComponent::BeginPlay()
    {
        if (bBegunPlay)
        {
            return;
        }
        Component::BeginPlay();

        Entity* owner = GetOwner();
        World* world = owner ? owner->GetWorld() : nullptr;
        if (!world)
        {
            return;
        }
        if (!world->FindComponentTick(StaticClass()))
        {
            ComponentTickRegistration tick;
            tick.Name = "LocalFogVolume.Refresh";
            tick.Run = [](Component& component, float)
            { static_cast<LocalFogVolumeComponent&>(component).RefreshLocalFogVolume(); };
            tick.TimeChannel = Engine::TimeChannel::Unscaled;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
        m_RegisteredEngine = Engine::GEngine;
        RefreshLocalFogVolume();
    }

    void LocalFogVolumeComponent::EndPlay()
    {
        if (m_RegisteredEngine && m_LocalFogVolumeHandle != 0u)
        {
            m_RegisteredEngine->GetRenderWorld().UnregisterLocalFogVolume(m_LocalFogVolumeHandle);
        }
        m_LocalFogVolumeHandle = 0u;
        m_RegisteredEngine = nullptr;
        Component::EndPlay();
    }

    bool LocalFogVolumeComponent::BuildLocalFogVolumeProxy(Rendering::LocalFogVolumeProxy& out) const
    {
        Math::Transform transform;
        if (IsPendingDestroy() || !GetOwnerTransform(GetOwner(), transform))
        {
            return false;
        }

        const Math::Vector3 center = transform.position + transform.TransformDirection(CenterOffset.Get());
        const Math::Vector3 radius = Radius.Get();
        const Math::Vector3 color = Color.Get();
        Rendering::LocalFogVolumeProxy proxy = Rendering::MakeDefaultLocalFogVolumeProxy();
        proxy.Center[0] = center.x;
        proxy.Center[1] = center.y;
        proxy.Center[2] = center.z;
        proxy.Radius[0] = radius.x * std::abs(transform.scale.x);
        proxy.Radius[1] = radius.y * std::abs(transform.scale.y);
        proxy.Radius[2] = radius.z * std::abs(transform.scale.z);
        proxy.Rotation[0] = transform.rotation.x;
        proxy.Rotation[1] = transform.rotation.y;
        proxy.Rotation[2] = transform.rotation.z;
        proxy.Rotation[3] = transform.rotation.w;
        proxy.Density = Density.Get();
        proxy.Color[0] = color.x;
        proxy.Color[1] = color.y;
        proxy.Color[2] = color.z;
        proxy.Falloff = Falloff.Get();
        proxy.NoiseAmount = NoiseAmount.Get();
        proxy.NoiseScaleMeters = NoiseScaleMeters.Get();
        proxy = Rendering::SanitizeLocalFogVolumeProxy(proxy);
        if (!Rendering::IsValidLocalFogVolumeProxy(proxy))
        {
            return false;
        }
        out = proxy;
        return true;
    }

    void LocalFogVolumeComponent::RefreshLocalFogVolume()
    {
        if (!bBegunPlay || !m_RegisteredEngine)
        {
            return;
        }
        Rendering::LocalFogVolumeProxy proxy;
        if (!BuildLocalFogVolumeProxy(proxy))
        {
            if (m_LocalFogVolumeHandle != 0u)
            {
                m_RegisteredEngine->GetRenderWorld().UnregisterLocalFogVolume(m_LocalFogVolumeHandle);
                m_LocalFogVolumeHandle = 0u;
            }
            return;
        }
        if (m_LocalFogVolumeHandle == 0u)
        {
            m_LocalFogVolumeHandle = m_RegisteredEngine->GetRenderWorld().RegisterLocalFogVolume(proxy);
            m_LastProxy = proxy;
        }
        else if (!Rendering::AreEqualLocalFogVolumeProxy(m_LastProxy, proxy))
        {
            m_RegisteredEngine->GetRenderWorld().UpdateLocalFogVolume(m_LocalFogVolumeHandle, proxy);
            m_LastProxy = proxy;
        }
    }
}

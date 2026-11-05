// 持ち主の変換から空・雨の遮蔽物を作り、変更時だけ描画側へ公開する。
#include "Component/SkyOccluderComponent.h"

#include "Engine/Engine.h"
#include "Math/GeometryTypes.h"
#include "Object/Entity.h"
#include "Object/World.h"
#include <algorithm>
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

        float GetOwnerYaw(const Math::Transform& transform)
        {
            // 局所 +Z の水平投影から、+Z が +X へ向く正の Yaw を求める。
            const Math::Vector3 forward = transform.TransformDirection(Math::Vector3(0.0f, 0.0f, 1.0f));
            if (forward.x * forward.x + forward.z * forward.z > 1.0e-8f)
            {
                return std::atan2(forward.x, forward.z);
            }
            // +Z が鉛直を向く姿勢では、局所 +X の水平投影を使う。
            const Math::Vector3 right = transform.TransformDirection(Math::Vector3(1.0f, 0.0f, 0.0f));
            return std::atan2(-right.z, right.x);
        }

        Math::Vector3 Abs(const Math::Vector3& value)
        {
            return Math::Vector3(std::abs(value.x), std::abs(value.y), std::abs(value.z));
        }
    } // namespace

    IMPLEMENT_CLASS(SkyOccluderComponent, Component)

    SkyOccluderComponent::SkyOccluderComponent()
        : Component()
    {
        SetDefaults();
    }

    SkyOccluderComponent::SkyOccluderComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetDefaults();
    }

    SkyOccluderComponent::SkyOccluderComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetDefaults();
    }

    SkyOccluderComponent::~SkyOccluderComponent() = default;

    void SkyOccluderComponent::SetDefaults()
    {
        Shape = Rendering::SkyOccluderShape::Ellipsoid;
        OccluderClass = Rendering::SkyOccluderClass::Solid;
        HalfExtents = Math::Vector3::One;
        CenterOffset = Math::Vector3::Zero;
        YawOffsetRadians = 0.0f;
        BaseOffset = 0.0f;
        Opacity = 1.0f;
        bDynamic = false;
        bBlocksRain = true;
        SetTickGroup(ETickGroup::PreRender);
    }

    void SkyOccluderComponent::Initialize()
    {
        Component::Initialize();
    }

    void SkyOccluderComponent::Finalize()
    {
        Component::Finalize();
    }

    void SkyOccluderComponent::BeginPlay()
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
            tick.Name = "SkyOccluder.Refresh";
            tick.Run = [](Component& component, float)
            { static_cast<SkyOccluderComponent&>(component).RefreshSkyOccluder(); };
            tick.TimeChannel = Engine::TimeChannel::Unscaled;
            world->RegisterComponentTick(StaticClass(), tick);
        }
        SetTickEnabled(true);
        m_RegisteredEngine = Engine::GEngine;
        RefreshSkyOccluder();
    }

    void SkyOccluderComponent::EndPlay()
    {
        if (m_RegisteredEngine && m_SkyOccluderHandle != 0u)
        {
            m_RegisteredEngine->GetRenderWorld().UnregisterSkyOccluder(m_SkyOccluderHandle);
        }
        m_SkyOccluderHandle = 0u;
        m_RegisteredEngine = nullptr;
        Component::EndPlay();
    }

    bool SkyOccluderComponent::BuildSkyOccluderProxy(Rendering::SkyOccluderProxy& out) const
    {
        Math::Transform transform;
        if (IsPendingDestroy() || !GetOwnerTransform(GetOwner(), transform))
        {
            return false;
        }

        const Math::Vector3 center = transform.position + transform.TransformDirection(CenterOffset.Get());
        const Math::Vector3 extents = HalfExtents.Get();
        const Math::Vector3 scale = Abs(transform.scale);
        Rendering::SkyOccluderProxy proxy = Rendering::MakeDefaultSkyOccluderProxy();
        proxy.CenterX = center.x;
        proxy.CenterY = center.y;
        proxy.CenterZ = center.z;
        proxy.Yaw = GetOwnerYaw(transform) + YawOffsetRadians.Get();
        proxy.HalfExtentX = extents.x * scale.x;
        proxy.HalfExtentY = extents.y * scale.y;
        proxy.HalfExtentZ = extents.z * scale.z;
        proxy.BaseOffset = BaseOffset.Get();
        proxy.Opacity = Opacity.Get();
        Rendering::SetSkyOccluderShape(proxy, Shape.Get());
        Rendering::SetSkyOccluderClass(proxy, OccluderClass.Get());
        Rendering::SetSkyOccluderDynamic(proxy, bDynamic.Get());
        Rendering::SetSkyOccluderBlocksRain(proxy, bBlocksRain.Get());
        proxy = Rendering::SanitizeSkyOccluderProxy(proxy);
        if (!Rendering::IsValidSkyOccluderProxy(proxy))
        {
            return false;
        }
        out = proxy;
        return true;
    }

    bool SkyOccluderComponent::InitializeFromOwnerBounds()
    {
        const Entity* owner = GetOwner();
        Math::Transform transform;
        Math::AABB bounds;
        if (!GetOwnerTransform(owner, transform) || !owner->GetWorldAABB(bounds) ||
            !IsFinite(bounds.Min) || !IsFinite(bounds.Max) ||
            bounds.Min.x > bounds.Max.x || bounds.Min.y > bounds.Max.y || bounds.Min.z > bounds.Max.z)
        {
            return false;
        }
        const Math::Quaternion inverseRotation(-transform.rotation.x, -transform.rotation.y,
                                               -transform.rotation.z, transform.rotation.w);
        const Math::Vector3 offset = inverseRotation * (bounds.Center() - transform.position);
        const Math::Vector3 worldExtents = bounds.HalfExtents();
        const Math::Vector3 x = Abs(inverseRotation * Math::Vector3(worldExtents.x, 0.0f, 0.0f));
        const Math::Vector3 y = Abs(inverseRotation * Math::Vector3(0.0f, worldExtents.y, 0.0f));
        const Math::Vector3 z = Abs(inverseRotation * Math::Vector3(0.0f, 0.0f, worldExtents.z));
        Math::Vector3 extents = x + y + z;

        // 描画形状は Yaw だけで回すため、傾いた持ち主でも元の AABB を包む半寸法を確保する。
        const float yaw = GetOwnerYaw(transform) + YawOffsetRadians.Get();
        const float cosine = std::abs(std::cos(yaw));
        const float sine = std::abs(std::sin(yaw));
        extents.x = std::max(extents.x, cosine * worldExtents.x + sine * worldExtents.z);
        extents.y = std::max(extents.y, worldExtents.y);
        extents.z = std::max(extents.z, sine * worldExtents.x + cosine * worldExtents.z);
        const Math::Vector3 scale = Abs(transform.scale);
        extents = Math::Vector3(extents.x / scale.x, extents.y / scale.y, extents.z / scale.z);
        if (!IsFinite(offset) || !IsFinite(extents) || !std::isfinite(yaw))
        {
            return false;
        }
        HalfExtents = extents;
        CenterOffset = offset;
        return true;
    }

    void SkyOccluderComponent::RefreshSkyOccluder()
    {
        if (!bBegunPlay || !m_RegisteredEngine)
        {
            return;
        }
        Rendering::SkyOccluderProxy proxy;
        if (!BuildSkyOccluderProxy(proxy))
        {
            if (m_SkyOccluderHandle != 0u)
            {
                m_RegisteredEngine->GetRenderWorld().UnregisterSkyOccluder(m_SkyOccluderHandle);
                m_SkyOccluderHandle = 0u;
            }
            return;
        }
        if (m_SkyOccluderHandle == 0u)
        {
            m_SkyOccluderHandle = m_RegisteredEngine->GetRenderWorld().RegisterSkyOccluder(proxy);
            m_LastProxy = proxy;
        }
        else if (!Rendering::AreEqualSkyOccluderProxy(m_LastProxy, proxy))
        {
            m_RegisteredEngine->GetRenderWorld().UpdateSkyOccluder(m_SkyOccluderHandle, proxy);
            m_LastProxy = proxy;
        }
    }
} // namespace NorvesLib::Core::Component

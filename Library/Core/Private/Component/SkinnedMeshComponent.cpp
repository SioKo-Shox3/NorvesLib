#include "Component/SkinnedMeshComponent.h"

#include "Math/MatrixUtils.h"
#include "Object/Entity.h"

#include <cmath>

namespace NorvesLib::Core::Component
{
    namespace
    {
        Math::Matrix4x4 LoadMeshNodeGlobalTransform(const Container::FixedArray<float, 16>& values)
        {
            return Math::Matrix4x4(
                values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11],
                values[12], values[13], values[14], values[15]);
        }
    } // namespace

    IMPLEMENT_CLASS(SkinnedMeshComponent, Component)

    SkinnedMeshComponent::SkinnedMeshComponent() = default;

    SkinnedMeshComponent::SkinnedMeshComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
    }

    SkinnedMeshComponent::~SkinnedMeshComponent() = default;

    void SkinnedMeshComponent::Initialize()
    {
        Component::Initialize();
        m_MeshNodeGlobalTransform = Math::Matrix4x4::Identity;
        m_bPoseDirty = true;
    }

    void SkinnedMeshComponent::Finalize()
    {
        m_Pose.Clear();
        m_SkeletalAsset.reset();
        Component::Finalize();
    }

    void SkinnedMeshComponent::Tick(float deltaTime)
    {
        if (!m_bPlaying || !m_SkeletalAsset || !m_SkeletalAsset->GetAnimationClip())
        {
            return;
        }

        const float duration = m_SkeletalAsset->GetAnimationClip()->GetClip().DurationSeconds;
        if (duration <= Math::Constants::EPSILON)
        {
            return;
        }
        m_AnimationTimeSeconds += deltaTime * m_PlaybackRate;
        if (m_bLooping)
        {
            m_AnimationTimeSeconds = std::fmod(m_AnimationTimeSeconds, duration);
            if (m_AnimationTimeSeconds < 0.0f)
            {
                m_AnimationTimeSeconds += duration;
            }
        }
        else
        {
            m_AnimationTimeSeconds = std::fmax(0.0f, std::fmin(m_AnimationTimeSeconds, duration));
        }
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    void SkinnedMeshComponent::SetSkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset)
    {
        m_SkeletalAsset = asset;
        m_MeshNodeGlobalTransform = asset && asset->GetMesh()
            ? LoadMeshNodeGlobalTransform(asset->GetMesh()->GetMeshNodeGlobalTransform())
            : Math::Matrix4x4::Identity;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    const Container::TSharedPtr<SkeletalAssetResource>& SkinnedMeshComponent::GetSkeletalAsset() const
    {
        return m_SkeletalAsset;
    }

    void SkinnedMeshComponent::SetMeshNodeGlobalTransform(const Math::Matrix4x4& transform)
    {
        m_MeshNodeGlobalTransform = transform;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    const Math::Matrix4x4& SkinnedMeshComponent::GetMeshNodeGlobalTransform() const
    {
        return m_MeshNodeGlobalTransform;
    }

    void SkinnedMeshComponent::SetAnimationTimeSeconds(float timeSeconds)
    {
        m_AnimationTimeSeconds = timeSeconds;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    float SkinnedMeshComponent::GetAnimationTimeSeconds() const
    {
        return m_AnimationTimeSeconds;
    }

    void SkinnedMeshComponent::SetPlaying(bool bPlaying)
    {
        m_bPlaying = bPlaying;
    }

    bool SkinnedMeshComponent::IsPlaying() const
    {
        return m_bPlaying;
    }

    void SkinnedMeshComponent::SetLooping(bool bLooping)
    {
        m_bLooping = bLooping;
    }

    bool SkinnedMeshComponent::IsLooping() const
    {
        return m_bLooping;
    }

    void SkinnedMeshComponent::SetPlaybackRate(float playbackRate)
    {
        m_PlaybackRate = playbackRate;
    }

    float SkinnedMeshComponent::GetPlaybackRate() const
    {
        return m_PlaybackRate;
    }

    void SkinnedMeshComponent::SetVisible(bool bVisible)
    {
        m_bVisible = bVisible;
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::IsVisible() const
    {
        return m_bVisible && IsActive();
    }

    void SkinnedMeshComponent::SetMaterial(Rendering::MaterialHandle material)
    {
        m_Material = material;
        MarkRenderStateDirty();
    }

    Rendering::MaterialHandle SkinnedMeshComponent::GetMaterial() const
    {
        return m_Material;
    }

    void SkinnedMeshComponent::SetCastShadow(bool bCastShadow)
    {
        m_bCastShadow = bCastShadow;
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::CastsShadow() const
    {
        return m_bCastShadow;
    }

    bool SkinnedMeshComponent::BuildSkinnedMeshProxy(Rendering::SkinnedMeshProxy& outProxy)
    {
        if (!IsVisible() || !RefreshPose())
        {
            return false;
        }
        outProxy = {};
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = GetComponentId();
        const Container::TSharedPtr<SkinnedMeshResource>& mesh = m_SkeletalAsset->GetMesh();
        outProxy.MeshHandle = mesh->GetRenderMeshHandle();
        outProxy.Material = m_Material;
        outProxy.AssetLease = mesh->GetRenderAssetLease();
        outProxy.WorldTransform = BuildOwnerWorldTransform();
        outProxy.BonePalette = m_Pose.BonePalette;
        outProxy.AnimatedBounds = m_Pose.AnimatedBounds;
        outProxy.bCastShadow = m_bCastShadow;
        outProxy.bHasAnimatedBounds = m_Pose.bHasAnimatedBounds;
        outProxy.bVisible = true;
        return outProxy.IsValid();
    }

    bool SkinnedMeshComponent::RefreshPose()
    {
        if (!m_bPoseDirty)
        {
            return !m_Pose.BonePalette.empty();
        }
        m_Pose.Clear();
        if (!m_SkeletalAsset || !m_SkeletalAsset->IsLoaded() || !m_SkeletalAsset->GetMesh() ||
            !m_SkeletalAsset->GetSkeleton() || !m_SkeletalAsset->GetAnimationClip())
        {
            return false;
        }
        m_bPoseDirty = false;
        return Animation::SkeletalAnimationSampler::Sample(
            *m_SkeletalAsset->GetSkeleton(),
            *m_SkeletalAsset->GetAnimationClip(),
            *m_SkeletalAsset->GetMesh(),
            m_AnimationTimeSeconds,
            m_MeshNodeGlobalTransform,
            m_Pose);
    }

    Math::Matrix4x4 SkinnedMeshComponent::BuildOwnerWorldTransform() const
    {
        const Entity* owner = GetOwner();
        if (!owner)
        {
            return Math::Matrix4x4::Identity;
        }
        const Math::Transform& world = owner->GetWorldTransform();
        return Math::MatrixUtils::CreateWorldRowVector(world.position, world.rotation, world.scale);
    }
} // namespace NorvesLib::Core::Component

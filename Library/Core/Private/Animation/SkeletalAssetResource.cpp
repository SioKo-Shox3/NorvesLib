#include "Animation/SkeletalAssetResource.h"

namespace NorvesLib::Core
{
    namespace
    {
        bool AreChildResourcesValid(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                                    const Container::TSharedPtr<SkeletonResource>& skeleton,
                                    const Container::TSharedPtr<AnimationClipResource>& animationClip)
        {
            return mesh && mesh->IsLoaded() && mesh->IsValid() && skeleton && skeleton->IsLoaded() &&
                   skeleton->IsValid() && animationClip && animationClip->IsLoaded() && animationClip->IsValid();
        }
    } // namespace

    IMPLEMENT_CLASS(SkeletalAssetResource, Resource)

    SkeletalAssetResource::SkeletalAssetResource() = default;

    SkeletalAssetResource::SkeletalAssetResource(const FieldInitializer* initializer)
        : Resource(initializer)
    {
    }

    SkeletalAssetResource::SkeletalAssetResource(const IUnknown* sourceObject)
        : Resource(sourceObject)
    {
    }

    SkeletalAssetResource::~SkeletalAssetResource()
    {
        Finalize();
    }

    void SkeletalAssetResource::Initialize()
    {
        Resource::Initialize();
    }

    void SkeletalAssetResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    bool SkeletalAssetResource::Load()
    {
        const bool bLoaded = AreChildResourcesValid(m_Mesh, m_Skeleton, m_AnimationClip);
        SetResourceState(bLoaded ? ResourceState::Loaded : ResourceState::Failed);
        return bLoaded;
    }

    void SkeletalAssetResource::Unload()
    {
        m_Mesh.reset();
        m_Skeleton.reset();
        m_AnimationClip.reset();
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkeletalAssetResource::GetMemorySize() const
    {
        return sizeof(SkeletalAssetResource);
    }

    void SkeletalAssetResource::SetResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                                             const Container::TSharedPtr<SkeletonResource>& skeleton,
                                             const Container::TSharedPtr<AnimationClipResource>& animationClip)
    {
        m_Mesh = mesh;
        m_Skeleton = skeleton;
        m_AnimationClip = animationClip;
        SetResourceState(AreChildResourcesValid(m_Mesh, m_Skeleton, m_AnimationClip) ? ResourceState::Loaded
                                                                                     : ResourceState::Failed);
    }

    const Container::TSharedPtr<SkinnedMeshResource>& SkeletalAssetResource::GetMesh() const
    {
        return m_Mesh;
    }

    const Container::TSharedPtr<SkeletonResource>& SkeletalAssetResource::GetSkeleton() const
    {
        return m_Skeleton;
    }

    const Container::TSharedPtr<AnimationClipResource>& SkeletalAssetResource::GetAnimationClip() const
    {
        return m_AnimationClip;
    }
} // namespace NorvesLib::Core

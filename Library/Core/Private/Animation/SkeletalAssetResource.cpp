#include "Animation/SkeletalAssetResource.h"
#include <algorithm>

namespace NorvesLib::Core
{
    namespace
    {
        bool AreChildResourcesValid(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                                    const Container::TSharedPtr<SkeletonResource>& skeleton,
                                    const Container::VariableArray<Container::TSharedPtr<AnimationClipResource>>& clips)
        {
            if (!mesh || !mesh->IsLoaded() || !mesh->IsValid() || !skeleton || !skeleton->IsLoaded() ||
                !skeleton->IsValid() || clips.empty())
            {
                return false;
            }
            for (const auto& clip : clips)
            {
                if (!clip || !clip->IsLoaded() || !clip->IsValid())
                {
                    return false;
                }
            }
            return true;
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
        const bool bLoaded = AreChildResourcesValid(m_Mesh, m_Skeleton, m_Clips);
        SetResourceState(bLoaded ? ResourceState::Loaded : ResourceState::Failed);
        return bLoaded;
    }

    void SkeletalAssetResource::Unload()
    {
        m_Mesh.reset();
        m_Skeleton.reset();
        m_AnimationClip.reset();
        m_Clips.clear();
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkeletalAssetResource::GetMemorySize() const
    {
        return sizeof(SkeletalAssetResource) + m_Clips.capacity()*sizeof(Container::TSharedPtr<AnimationClipResource>);
    }

    void SkeletalAssetResource::SetResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                                             const Container::TSharedPtr<SkeletonResource>& skeleton,
                                             const Container::TSharedPtr<AnimationClipResource>& animationClip)
    {
        SetClipResources(mesh,skeleton,{animationClip});
    }

    void SkeletalAssetResource::SetClipResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
        const Container::TSharedPtr<SkeletonResource>& skeleton,
        const Container::VariableArray<Container::TSharedPtr<AnimationClipResource>>& clips)
    {
        m_Clips = clips;
        m_Mesh = mesh;
        m_Skeleton = skeleton;
        m_AnimationClip = m_Clips.empty() ? Container::TSharedPtr<AnimationClipResource>{} : m_Clips.front();
        SetResourceState(AreChildResourcesValid(m_Mesh,m_Skeleton,m_Clips) ? ResourceState::Loaded : ResourceState::Failed);
    }

    size_t SkeletalAssetResource::GetClipCount() const
    {
        return m_Clips.size();
    }

    Container::TSharedPtr<AnimationClipResource> SkeletalAssetResource::GetClip(size_t index) const
    {
        return index < m_Clips.size() ? m_Clips[index] : Container::TSharedPtr<AnimationClipResource>{};
    }

    Container::TSharedPtr<AnimationClipResource> SkeletalAssetResource::GetClip(Container::StringView name) const
    {
        if (name.empty() || !name.data())
        {
            return {};
        }
        Container::TSharedPtr<AnimationClipResource> result;
        for (const auto& clip : m_Clips)
        {
            if (clip)
            {
                const auto& clipName = clip->GetClip().Name;
                if (clipName.size() == name.size() && std::equal(clipName.begin(),clipName.end(),name.begin()))
                {
                    if (result)
                    {
                        return {};
                    }
                    result = clip;
                }
            }
        }
        return result;
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

#include "Animation/AnimationClipResource.h"

#include <utility>

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(AnimationClipResource, Resource)

    AnimationClipResource::AnimationClipResource() = default;

    AnimationClipResource::AnimationClipResource(const FieldInitializer* initializer)
        : Resource(initializer)
    {
    }

    AnimationClipResource::AnimationClipResource(const IUnknown* sourceObject)
        : Resource(sourceObject)
    {
    }

    AnimationClipResource::~AnimationClipResource()
    {
        Finalize();
    }

    void AnimationClipResource::Initialize()
    {
        Resource::Initialize();
    }

    void AnimationClipResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    bool AnimationClipResource::Load()
    {
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void AnimationClipResource::Unload()
    {
        m_Clip = {};
        SetResourceState(ResourceState::Unloaded);
    }

    size_t AnimationClipResource::GetMemorySize() const
    {
        size_t size = sizeof(AnimationClipResource) + m_Clip.Name.size();
        size += m_Clip.Channels.size() * sizeof(Skeletal::SkeletalAnimationChannel);
        for (const Skeletal::SkeletalAnimationChannel& channel : m_Clip.Channels)
        {
            size += channel.Samples.size() * sizeof(Skeletal::SkeletalAnimationSample);
        }
        return size;
    }

    void AnimationClipResource::SetClip(Skeletal::SkeletalAnimationClip&& clip)
    {
        m_Clip = std::move(clip);
    }

    const Skeletal::SkeletalAnimationClip& AnimationClipResource::GetClip() const
    {
        return m_Clip;
    }
} // namespace NorvesLib::Core

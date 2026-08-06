#pragma once

#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core
{
    class AnimationClipResource : public Resource
    {
        REFLECTION_CLASS(AnimationClipResource, Resource)

    public:
        AnimationClipResource();
        explicit AnimationClipResource(const FieldInitializer* initializer);
        explicit AnimationClipResource(const IUnknown* sourceObject);
        ~AnimationClipResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        void SetClip(Skeletal::SkeletalAnimationClip&& clip);
        const Skeletal::SkeletalAnimationClip& GetClip() const;

    private:
        Skeletal::SkeletalAnimationClip m_Clip;
    };
} // namespace NorvesLib::Core

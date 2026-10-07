#pragma once

#include "Animation/SkeletalPoseRuntime.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"

namespace NorvesLib::Core::Skeletal
{
    class RigBoundClipProof;
    class RigBoundClipAccess;
} // namespace NorvesLib::Core::Skeletal
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

        const Animation::ClipPoseRuntime& GetPoseRuntime() const noexcept { return m_PoseRuntime; }
        uint64_t GetPoseRevision() const noexcept { return m_PoseRevision; }

    private:
        Animation::ClipPoseRuntime m_PoseRuntime;
        uint64_t m_PoseRevision = 0;
      friend class Skeletal::RigBoundClipAccess;
      Container::TSharedPtr<const Skeletal::RigBoundClipProof> m_BoundRigProof;
      Skeletal::SkeletalAnimationClip m_Clip;
    };
} // namespace NorvesLib::Core

#pragma once

#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkinnedMeshResource.h"

namespace NorvesLib::Core
{
    class SkeletalAssetResource : public Resource
    {
        REFLECTION_CLASS(SkeletalAssetResource, Resource)

    public:
        SkeletalAssetResource();
        explicit SkeletalAssetResource(const FieldInitializer* initializer);
        explicit SkeletalAssetResource(const IUnknown* sourceObject);
        ~SkeletalAssetResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        void SetResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                          const Container::TSharedPtr<SkeletonResource>& skeleton,
                          const Container::TSharedPtr<AnimationClipResource>& animationClip);

        const Container::TSharedPtr<SkinnedMeshResource>& GetMesh() const;
        const Container::TSharedPtr<SkeletonResource>& GetSkeleton() const;
        const Container::TSharedPtr<AnimationClipResource>& GetAnimationClip() const;

    private:
        Container::TSharedPtr<SkinnedMeshResource> m_Mesh;
        Container::TSharedPtr<SkeletonResource> m_Skeleton;
        Container::TSharedPtr<AnimationClipResource> m_AnimationClip;
    };
} // namespace NorvesLib::Core

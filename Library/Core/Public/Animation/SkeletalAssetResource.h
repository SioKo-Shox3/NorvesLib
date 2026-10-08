#pragma once

#include "Animation/AnimationClipResource.h"
#include "Animation/SkeletonResource.h"
#include "Container/StringView.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkinnedMeshResource.h"

namespace NorvesLib::Core::ResourceIO
{
    class RigSplitPublicationReceipt;
    class RigSplitAssetAccess;
} // namespace NorvesLib::Core::ResourceIO
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

        // 複数clipを強参照で所有する。単数SetResourcesの空brace呼び出しと曖昧にならない別名入口。
        void SetClipResources(const Container::TSharedPtr<SkinnedMeshResource>& mesh,
                              const Container::TSharedPtr<SkeletonResource>& skeleton,
                              const Container::VariableArray<Container::TSharedPtr<AnimationClipResource>>& clips);
        size_t GetClipCount() const;
        Container::TSharedPtr<AnimationClipResource> GetClip(size_t index) const;
        // 空/不在/重複名は空を返す。比較は完全一致で、自動改名しない。
        Container::TSharedPtr<AnimationClipResource> GetClip(Container::StringView name) const;

        const Container::TSharedPtr<SkinnedMeshResource>& GetMesh() const;
        const Container::TSharedPtr<SkeletonResource>& GetSkeleton() const;
        const Container::TSharedPtr<AnimationClipResource>& GetAnimationClip() const;

    private:
      friend class ResourceIO::RigSplitAssetAccess;
      Container::TSharedPtr<const ResourceIO::RigSplitPublicationReceipt> m_SplitReceipt;
      Container::TSharedPtr<SkinnedMeshResource> m_Mesh;
      Container::TSharedPtr<SkeletonResource> m_Skeleton;
      // 旧GetAnimationClipが返す参照先を安定させる先頭の別名。
      Container::TSharedPtr<AnimationClipResource> m_AnimationClip;
      Container::VariableArray<Container::TSharedPtr<AnimationClipResource>> m_Clips;
    };
} // namespace NorvesLib::Core

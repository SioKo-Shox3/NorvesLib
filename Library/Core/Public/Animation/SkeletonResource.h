#pragma once

#include "Container/UnorderedMap.h"
#include "Text/IdentityPool.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"
#include "Animation/SkeletalRestPose.h"

namespace NorvesLib::Core
{
    class SkeletonResource : public Resource
    {
        REFLECTION_CLASS(SkeletonResource, Resource)

    public:
        SkeletonResource();
        explicit SkeletonResource(const FieldInitializer* initializer);
        explicit SkeletonResource(const IUnknown* sourceObject);
        ~SkeletonResource() override;

        void Initialize() override;
        void Finalize() override;
        bool Load() override;
        void Unload() override;
        size_t GetMemorySize() const override;

        void SetJoints(Container::VariableArray<Skeletal::SkeletalJoint>&& joints);
        const Container::VariableArray<Skeletal::SkeletalJoint>& GetJoints() const;
        // 未Load時のみ設定可。現在rigの作者restを明示するv1経路。旧SetJointsはこれを解除する。
        [[nodiscard]] bool SetAuthorRestPose(const Container::VariableArray<Skeletal::SkeletalRestTransform>& rest);
        const Container::VariableArray<Skeletal::SkeletalRestTransform>& GetAuthorRestPose() const;

        // 空名/未発見は-1、重複名は先頭を返す。SetJointsで索引を再構築する。
        int32_t FindJointIndex(Identity name) const;

    private:
        Container::VariableArray<Skeletal::SkeletalJoint> m_Joints;
        Container::VariableArray<Skeletal::SkeletalRestTransform> m_AuthorRestPose;
        Container::UnorderedMap<Identity, uint32_t, Identity::Hasher> m_JointIndices;
    };
} // namespace NorvesLib::Core

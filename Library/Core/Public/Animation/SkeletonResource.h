#pragma once

#include "Container/UnorderedMap.h"
#include "Text/IdentityPool.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"

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

        // 空名/未発見は-1、重複名は先頭を返す。SetJointsで索引を再構築する。
        int32_t FindJointIndex(Identity name) const;

    private:
        Container::VariableArray<Skeletal::SkeletalJoint> m_Joints;
        Container::UnorderedMap<Identity, uint32_t, Identity::Hasher> m_JointIndices;
    };
} // namespace NorvesLib::Core

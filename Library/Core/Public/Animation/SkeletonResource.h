#pragma once

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

    private:
        Container::VariableArray<Skeletal::SkeletalJoint> m_Joints;
    };
} // namespace NorvesLib::Core

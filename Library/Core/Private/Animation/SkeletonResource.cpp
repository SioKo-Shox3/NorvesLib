#include "Animation/SkeletonResource.h"

#include <utility>

namespace NorvesLib::Core
{
    IMPLEMENT_CLASS(SkeletonResource, Resource)

    SkeletonResource::SkeletonResource() = default;

    SkeletonResource::SkeletonResource(const FieldInitializer* initializer)
        : Resource(initializer)
    {
    }

    SkeletonResource::SkeletonResource(const IUnknown* sourceObject)
        : Resource(sourceObject)
    {
    }

    SkeletonResource::~SkeletonResource()
    {
        Finalize();
    }

    void SkeletonResource::Initialize()
    {
        Resource::Initialize();
    }

    void SkeletonResource::Finalize()
    {
        Unload();
        Resource::Finalize();
    }

    bool SkeletonResource::Load()
    {
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void SkeletonResource::Unload()
    {
        m_Joints.clear();
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkeletonResource::GetMemorySize() const
    {
        size_t size = sizeof(SkeletonResource) + m_Joints.size() * sizeof(Skeletal::SkeletalJoint);
        for (const Skeletal::SkeletalJoint& joint : m_Joints)
        {
            size += joint.Name.size();
        }
        return size;
    }

    void SkeletonResource::SetJoints(Container::VariableArray<Skeletal::SkeletalJoint>&& joints)
    {
        m_Joints = std::move(joints);
    }

    const Container::VariableArray<Skeletal::SkeletalJoint>& SkeletonResource::GetJoints() const
    {
        return m_Joints;
    }
} // namespace NorvesLib::Core

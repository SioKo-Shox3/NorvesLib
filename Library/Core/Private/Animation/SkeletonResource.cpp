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
        m_JointIndices.clear();
        SetResourceState(ResourceState::Unloaded);
    }

    size_t SkeletonResource::GetMemorySize() const
    {
        size_t size = sizeof(SkeletonResource) + m_Joints.size() * sizeof(Skeletal::SkeletalJoint);
        for (const Skeletal::SkeletalJoint& joint : m_Joints)
        {
            size += joint.Name.size();
        }
        size += m_JointIndices.size() * (sizeof(Identity) + sizeof(uint32_t));
        return size;
    }

    void SkeletonResource::SetJoints(Container::VariableArray<Skeletal::SkeletalJoint>&& joints)
    {
        m_Joints = std::move(joints);
        m_JointIndices.clear();
        for (size_t index = 0; index < m_Joints.size(); ++index)
        {
            if (!m_Joints[index].Name.empty())
            {
                m_JointIndices.emplace(Identity(m_Joints[index].Name.c_str()), static_cast<uint32_t>(index));
            }
        }
    }

    int32_t SkeletonResource::FindJointIndex(Identity name) const
    {
        const auto found = m_JointIndices.find(name);
        return found == m_JointIndices.end() ? -1 : static_cast<int32_t>(found->second);
    }

    const Container::VariableArray<Skeletal::SkeletalJoint>& SkeletonResource::GetJoints() const
    {
        return m_Joints;
    }
} // namespace NorvesLib::Core

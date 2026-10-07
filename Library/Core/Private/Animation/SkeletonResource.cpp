#include "Animation/SkeletonResource.h"
#include "Asset/CookedSkeletonV1.h"
#include "Asset/RigSplitWire.h"

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
        if (m_bSplitV1 && !m_SplitSkeleton)
        {
            SetResourceState(ResourceState::Failed);
            return false;
        }
        SetResourceState(ResourceState::Loaded);
        return true;
    }

    void SkeletonResource::Unload()
    {
        m_SplitSkeleton.reset();
        m_Joints.clear();
        m_AuthorRestPose.clear();
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
        size += m_AuthorRestPose.size() * sizeof(Skeletal::SkeletalRestTransform);
        if (m_SplitSkeleton)
        {
            size += sizeof(Skeletal::SkeletonV1Data) +
                    m_SplitSkeleton->CurrentRest.Rest.size() * sizeof(Skeletal::SkeletalRestTransform);
            size += m_SplitSkeleton->Topology.CanonicalBytes.size() + m_SplitSkeleton->CurrentRest.Label.size();
            for (const auto& joint : m_SplitSkeleton->Topology.Joints)
            {
                size += sizeof(joint) + joint.Name.size();
            }
        }
        return size;
    }

    bool SkeletonResource::SetSplitSkeleton(const Skeletal::SkeletonV1& skeleton)
    {
        if (IsLoaded() || !skeleton.m_Data)
        {
            return false;
        }
        Container::UnorderedMap<Identity, uint32_t, Identity::Hasher> indices;
        for (size_t i = 0; i < skeleton.m_Data->Topology.Joints.size(); ++i)
        {
            Container::String name;
            if (!Skeletal::SplitWire::NativeName(skeleton.m_Data->Topology.Joints[i].Name, name))
            {
                return false;
            }
            indices.emplace(Identity(name.c_str()), uint32_t(i));
        }
        m_JointIndices = std::move(indices);
        m_Joints.clear();
        m_AuthorRestPose.clear();
        m_SplitSkeleton = skeleton.m_Data;
        m_bSplitV1 = true;
        return true;
    }
    bool SkeletonResource::IsSplitV1() const noexcept
    {
        return m_bSplitV1;
    }
    const Skeletal::SkeletonV1Data* SkeletonResource::GetSplitSkeleton() const noexcept
    {
        return m_SplitSkeleton.get();
    }
    void SkeletonResource::SetJoints(Container::VariableArray<Skeletal::SkeletalJoint>&& joints)
    {
        if (m_bSplitV1)
        {
            return;
        }
        m_AuthorRestPose.clear();
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

    bool SkeletonResource::SetAuthorRestPose(const Container::VariableArray<Skeletal::SkeletalRestTransform>& rest)
    {
        if (m_bSplitV1 || IsLoaded() || rest.empty() || rest.size() != m_Joints.size())
        {
            return false;
        }
        for (const auto& value : rest)
        {
            if (!Skeletal::IsValidSkeletalRestTransform(value))
            {
                return false;
            }
        }
        auto candidate = rest;
        m_AuthorRestPose = std::move(candidate);
        return true;
    }
    const Container::VariableArray<Skeletal::SkeletalRestTransform>& SkeletonResource::GetAuthorRestPose() const
    {
        return m_SplitSkeleton ? m_SplitSkeleton->CurrentRest.Rest : m_AuthorRestPose;
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

#pragma once

#include "Animation/SkeletalPoseRuntime.h"
#include "Animation/SkeletalRestPose.h"
#include "Animation/SocketTypes.h"
#include "Container/UnorderedMap.h"
#include "Object/Reflection.h"
#include "Object/Resource.h"
#include "Resource/SkeletalGltfData.h"
#include "Text/IdentityPool.h"

namespace NorvesLib::Core::Skeletal
{
    class SkeletonV1;
    struct SkeletonV1Data;
} // namespace NorvesLib::Core::Skeletal
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

        // validated値だけをLoad前に設定する。split modeでは旧setterを拒否し、Unload後も旧形へ戻らない。
        [[nodiscard]] bool SetSplitSkeleton(const Skeletal::SkeletonV1& skeleton);
        [[nodiscard]] bool IsSplitV1() const noexcept;
        [[nodiscard]] const Skeletal::SkeletonV1Data* GetSplitSkeleton() const noexcept;
        void SetJoints(Container::VariableArray<Skeletal::SkeletalJoint>&& joints);
        const Container::VariableArray<Skeletal::SkeletalJoint>& GetJoints() const;
        // 未Load時のみ設定可。現在rigの作者restを明示するv1経路。旧SetJointsはこれを解除する。
        [[nodiscard]] bool SetAuthorRestPose(const Container::VariableArray<Skeletal::SkeletalRestTransform>& rest);
        const Container::VariableArray<Skeletal::SkeletalRestTransform>& GetAuthorRestPose() const;

        // 空名/未発見は-1、重複名は先頭を返す。SetJointsで索引を再構築する。
        int32_t FindJointIndex(Identity name) const;

        const Animation::SkeletonPoseRuntime& GetPoseRuntime() const noexcept { return m_PoseRuntime; }
        uint64_t GetPoseRevision() const noexcept { return m_PoseRevision; }
        [[nodiscard]] bool SetSockets(Container::Span<const Animation::SocketDefinition>, Animation::SocketReport&);
        [[nodiscard]] bool ApplySocketsJson(const Container::String&, Animation::SocketReport&);
        [[nodiscard]] bool ApplySocketsFile(const Container::String&, Animation::SocketReport&);
        const Animation::SocketDefinition* FindSocket(Identity) const;
        Container::Span<const Animation::SocketDefinition> GetSockets() const
        {
            return m_Sockets;
        }
        uint64_t GetSocketRevision() const noexcept
        {
            return m_SocketRevision;
        }

    private:
      Container::VariableArray<Animation::SocketDefinition> m_Sockets;
      uint64_t m_SocketRevision = 0;
      Animation::SkeletonPoseRuntime m_PoseRuntime;
      uint64_t m_PoseRevision = 0;
      Container::TSharedPtr<const Skeletal::SkeletonV1Data> m_SplitSkeleton;
      bool m_bSplitV1 = false;
      Container::VariableArray<Skeletal::SkeletalJoint> m_Joints;
      Container::VariableArray<Skeletal::SkeletalRestTransform> m_AuthorRestPose;
      Container::UnorderedMap<Identity, uint32_t, Identity::Hasher> m_JointIndices;
    };
} // namespace NorvesLib::Core

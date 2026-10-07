#include "Component/SkinnedMeshComponent.h"

#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Asset/CookedSkeletalNameCodec.h"

#include <cmath>

namespace NorvesLib::Core::Component
{
    namespace
    {
        Math::Matrix4x4 LoadMeshNodeGlobalTransform(const Container::FixedArray<float, 16>& values)
        {
            return Math::Matrix4x4(
                values[0], values[1], values[2], values[3],
                values[4], values[5], values[6], values[7],
                values[8], values[9], values[10], values[11],
                values[12], values[13], values[14], values[15]);
        }
    } // namespace

    IMPLEMENT_CLASS(SkinnedMeshComponent, Component)

    SkinnedMeshComponent::SkinnedMeshComponent()
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }

    SkinnedMeshComponent::~SkinnedMeshComponent() = default;

    void SkinnedMeshComponent::Initialize()
    {
        Component::Initialize();
        m_MeshNodeGlobalTransform = Math::Matrix4x4::Identity;
        m_bPoseDirty = true;
    }

    void SkinnedMeshComponent::Finalize()
    {
        m_Pose.Clear();
        m_PoseContext = {};
        m_PoseScratch = {};
        m_SkeletalAsset.reset();
        m_SelectedClip.reset();
        Component::Finalize();
    }

    void SkinnedMeshComponent::OnTickGroup(ETickGroup group, float deltaTime)
    {
        if (group == ETickGroup::Animation)
        {
            Tick(deltaTime);
        }
        else if (group == ETickGroup::PoseFinalize && !m_bExternalAnimationDriven)
        {
            (void)EvaluatePose();
        }
    }

    void SkinnedMeshComponent::Tick(float deltaTime)
    {
        if (m_bExternalAnimationDriven || !m_bPlaying || !std::isfinite(deltaTime) || !std::isfinite(m_PlaybackRate) || !m_SkeletalAsset ||
            !GetAnimationClip())
        {
            return;
        }

        const float duration = GetAnimationClip()->GetClip().DurationSeconds;
        if (!std::isfinite(duration) || duration <= Math::Constants::EPSILON)
        {
            return;
        }
        m_AnimationTimeSeconds += deltaTime * m_PlaybackRate;
        if (m_bLooping)
        {
            m_AnimationTimeSeconds = std::fmod(m_AnimationTimeSeconds, duration);
            if (m_AnimationTimeSeconds < 0.0f)
            {
                m_AnimationTimeSeconds += duration;
            }
        }
        else
        {
            m_AnimationTimeSeconds = std::fmax(0.0f, std::fmin(m_AnimationTimeSeconds, duration));
        }
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    void SkinnedMeshComponent::SetSkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset)
    {
        if (m_SkeletalAsset != asset)
        {
            m_SlotMaterials.Clear();
        }
        m_PoseContext = {};
        m_SkeletalAsset = asset;
        m_SelectedClip.reset();
        m_bMeshNodeTransformOverridden = false;
        m_Pose.Clear();
        m_MeshNodeGlobalTransform = asset && asset->GetMesh()
            ? LoadMeshNodeGlobalTransform(asset->GetMesh()->GetMeshNodeGlobalTransform())
            : Math::Matrix4x4::Identity;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    const Container::TSharedPtr<SkeletalAssetResource>& SkinnedMeshComponent::GetSkeletalAsset() const
    {
        return m_SkeletalAsset;
    }

    bool SkinnedMeshComponent::SetAnimationClip(const Container::TSharedPtr<AnimationClipResource>& clip)
    {
        if (!m_SkeletalAsset || !m_SkeletalAsset->IsLoaded() || !m_SkeletalAsset->IsValid() || !clip ||
            !clip->IsLoaded() || !clip->IsValid())
        {
            return false;
        }
        bool bMember = false;
        for (size_t i = 0; i < m_SkeletalAsset->GetClipCount(); ++i)
        {
            if (m_SkeletalAsset->GetClip(i) == clip)
            {
                bMember = true;
                break;
            }
        }
        if (!bMember)
        {
            return false;
        }
        m_PoseContext = {};
        m_SelectedClip = clip;
        m_Pose.Clear();
        m_bPoseDirty = true;
        MarkRenderStateDirty();
        return true;
    }

    Container::TSharedPtr<AnimationClipResource> SkinnedMeshComponent::GetAnimationClip() const
    {
        if (!m_SkeletalAsset)
        {
            return {};
        }
        if (!m_SelectedClip)
        {
            return m_SkeletalAsset->GetAnimationClip();
        }
        if (!m_SelectedClip->IsLoaded() || !m_SelectedClip->IsValid())
        {
            return {};
        }
        for (size_t i = 0; i < m_SkeletalAsset->GetClipCount(); ++i)
        {
            if (m_SkeletalAsset->GetClip(i) == m_SelectedClip)
            {
                return m_SelectedClip;
            }
        }
        return {};
    }

    void SkinnedMeshComponent::SetMeshNodeGlobalTransform(const Math::Matrix4x4& transform)
    {
        m_MeshNodeGlobalTransform = transform;
        m_bMeshNodeTransformOverridden = true;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    const Math::Matrix4x4& SkinnedMeshComponent::GetMeshNodeGlobalTransform() const
    {
        return m_MeshNodeGlobalTransform;
    }

    void SkinnedMeshComponent::SetAnimationTimeSeconds(float timeSeconds)
    {
        m_AnimationTimeSeconds = timeSeconds;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    float SkinnedMeshComponent::GetAnimationTimeSeconds() const
    {
        return m_AnimationTimeSeconds;
    }

    void SkinnedMeshComponent::SetPlaying(bool bPlaying)
    {
        m_bPlaying = bPlaying;
    }

    bool SkinnedMeshComponent::IsPlaying() const
    {
        return m_bPlaying;
    }

    void SkinnedMeshComponent::SetLooping(bool bLooping)
    {
        m_bLooping = bLooping;
    }

    bool SkinnedMeshComponent::IsLooping() const
    {
        return m_bLooping;
    }

    void SkinnedMeshComponent::SetPlaybackRate(float playbackRate)
    {
        m_PlaybackRate = playbackRate;
    }

    float SkinnedMeshComponent::GetPlaybackRate() const
    {
        return m_PlaybackRate;
    }

    void SkinnedMeshComponent::SetVisible(bool bVisible)
    {
        m_bVisible = bVisible;
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::IsVisible() const
    {
        return m_bVisible && IsActive();
    }

    void SkinnedMeshComponent::SetMaterial(Rendering::MaterialHandle material)
    {
        m_Material = material;
        MarkRenderStateDirty();
    }

    Rendering::MaterialHandle SkinnedMeshComponent::GetMaterial() const
    {
        return m_Material;
    }

    bool SkinnedMeshComponent::SetMaterial(uint32_t slot, Rendering::MaterialHandle material)
    {
        return SetSlotMaterial(slot, material);
    }

    Rendering::MaterialHandle SkinnedMeshComponent::GetMaterial(uint32_t slot) const
    {
        Rendering::MaterialHandle material;
        (void)TryGetSlotMaterial(slot, material);
        return material;
    }

    Container::TSharedPtr<const Rendering::SkinnedMeshAssetLease> SkinnedMeshComponent::GetMaterialBindingLease() const
    {
        if (!m_SkeletalAsset || !m_SkeletalAsset->GetMesh() || !m_SkeletalAsset->GetMesh()->IsLoaded())
        {
            return {};
        }
        return m_SkeletalAsset->GetMesh()->GetRenderAssetLease();
    }

    uint32_t SkinnedMeshComponent::GetMaterialSlotCount() const
    {
        const auto lease = GetMaterialBindingLease();
        if (!lease || !lease->GetHandle().IsValid())
        {
            return 0;
        }
        return lease->GetMaterialSlotNames().empty() ? 1u : static_cast<uint32_t>(lease->GetMaterialSlotNames().size());
    }

    int32_t SkinnedMeshComponent::FindMaterialSlot(Container::StringView name) const
    {
        const auto lease = GetMaterialBindingLease();
        if (!lease || !lease->GetHandle().IsValid())
        {
            return -1;
        }
        const auto& names = lease->GetMaterialSlotNames();
        if (names.size() > Skeletal::MaximumMaterialSlotCount)
        {
            return -1;
        }
        // native文字幅はここでUTF8へ揃える。名前の照合や正規化は行わない。
        const auto encode = [](Container::StringView input, Container::VariableArray<uint8_t>& bytes)
        {
            using Char = Container::String::value_type;
            const Container::Span<const Char> source{input.data(), input.size()};
            const auto measured = Asset::MeasureSkeletalNameEncoding(2, source);
            if (!measured.Succeeded())
            {
                return false;
            }
            bytes.resize(measured.ByteCount);
            return Asset::EncodeSkeletalWireName(2, source, {bytes.data(), bytes.size()}).Succeeded();
        };
        Container::VariableArray<uint8_t> query;
        if (!encode(name, query))
        {
            return -1;
        }
        const uint32_t count = names.empty() ? 1u : static_cast<uint32_t>(names.size());
        MaterialIdentityView slots[Skeletal::MaximumMaterialSlotCount]{};
        Container::VariableArray<uint8_t> encoded[Skeletal::MaximumMaterialSlotCount];
        constexpr uint8_t defaultName[] = {'D','e','f','a','u','l','t'};
        for (uint32_t slot = 0; slot < count; ++slot)
        {
            slots[slot].IdentityIndex = slot;
            if (names.empty())
            {
                slots[slot].Name = {defaultName, sizeof(defaultName)};
            }
            else
            {
                if (!encode({names[slot].data(), names[slot].size()}, encoded[slot]))
                {
                    return -1;
                }
                slots[slot].Name = {encoded[slot].data(), encoded[slot].size()};
            }
        }
        return Skeletal::FindSkeletalMaterialSlot({slots, count}, {query.data(), query.size()});
    }

    bool SkinnedMeshComponent::SetSlotMaterial(uint32_t slot, Rendering::MaterialHandle material)
    {
        const auto lease = GetMaterialBindingLease();
        if (!lease)
        {
            return false;
        }
        const auto handle = lease->GetHandle();
        const uint32_t count = lease->GetMaterialSlotNames().empty() ? 1u : static_cast<uint32_t>(lease->GetMaterialSlotNames().size());
        if (!m_SlotMaterials.Set(handle.Id, handle.Generation, count, slot, slot == 0 ? 0 : material.Id))
        {
            return false;
        }
        if (slot == 0)
        {
            m_Material = material;
        }
        MarkRenderStateDirty();
        return true;
    }

    bool SkinnedMeshComponent::SetSlotMaterial(Container::StringView name, Rendering::MaterialHandle material)
    {
        const int32_t slot = FindMaterialSlot(name);
        return slot >= 0 && SetSlotMaterial(static_cast<uint32_t>(slot), material);
    }

    bool SkinnedMeshComponent::TryGetSlotMaterial(uint32_t slot, Rendering::MaterialHandle& out) const
    {
        const auto lease = GetMaterialBindingLease();
        if (!lease)
        {
            return false;
        }
        const auto handle = lease->GetHandle();
        const uint32_t count = lease->GetMaterialSlotNames().empty() ? 1u : static_cast<uint32_t>(lease->GetMaterialSlotNames().size());
        uint64_t id = 0;
        if (!m_SlotMaterials.TryGet(handle.Id, handle.Generation, count, slot, m_Material.Id, id))
        {
            return false;
        }
        out = Rendering::MaterialHandle{id};
        return true;
    }

    void SkinnedMeshComponent::SetCastShadow(bool bCastShadow)
    {
        m_bCastShadow = bCastShadow;
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::CastsShadow() const
    {
        return m_bCastShadow;
    }

    bool SkinnedMeshComponent::BuildSkinnedMeshProxy(Rendering::SkinnedMeshProxy& outProxy)
    {
        if (!IsVisible() || !EvaluatePose())
        {
            return false;
        }
        outProxy = {};
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = GetComponentId();
        const Container::TSharedPtr<SkinnedMeshResource>& mesh = m_SkeletalAsset->GetMesh();
        outProxy.MeshHandle = mesh->GetRenderMeshHandle();
        outProxy.Material = m_Material;
        const uint32_t slotCount = GetMaterialSlotCount();
        if (slotCount == 0 || slotCount > Rendering::MAX_MATERIAL_SLOTS)
        {
            outProxy = {};
            return false;
        }
        outProxy.MaterialCount = slotCount;
        for (uint32_t slot = 0; slot < slotCount; ++slot)
        {
            if (!TryGetSlotMaterial(slot, outProxy.Materials[slot]))
            {
                outProxy = {};
                return false;
            }
        }
        // 旧単一draw経路にもslot0のoverrideを反映する。
        if (slotCount != 0)
        {
            outProxy.Material = outProxy.Materials[0];
        }
        outProxy.AssetLease = mesh->GetRenderAssetLease();
        outProxy.WorldTransform = BuildOwnerWorldTransform();
        outProxy.BonePalette = m_Pose.BonePalette;
        outProxy.AnimatedBounds = m_Pose.AnimatedBounds;
        outProxy.bCastShadow = m_bCastShadow;
        outProxy.bHasAnimatedBounds = m_Pose.bHasAnimatedBounds;
        outProxy.bVisible = true;
        return outProxy.IsValid();
    }

    void SkinnedMeshComponent::SetExternalAnimationDriven(bool enabled)
    {
        if(m_bExternalAnimationDriven==enabled)return;
        m_bExternalAnimationDriven=enabled;
        m_bPoseDirty=true;
        m_Pose.Clear();
        MarkRenderStateDirty();
    }
    bool SkinnedMeshComponent::SubmitLocalPose(const Animation::LocalPose& pose)
    {
        if(!m_bExternalAnimationDriven||!HasValidPoseResources())return false;
        if(!m_bMeshNodeTransformOverridden)
            m_MeshNodeGlobalTransform=LoadMeshNodeGlobalTransform(m_SkeletalAsset->GetMesh()->GetMeshNodeGlobalTransform());
        const auto& skeleton=*m_SkeletalAsset->GetSkeleton();
        const auto& clip=*GetAnimationClip();
        const auto& mesh=*m_SkeletalAsset->GetMesh();
        m_bPoseDirty=true;m_Pose.Clear();MarkRenderStateDirty();
        if(!Animation::SkeletalPoseBuilder::IsPreparedFor(m_PoseContext,skeleton,clip,mesh,m_MeshNodeGlobalTransform)&&
           !Animation::SkeletalPoseBuilder::Prepare(skeleton,clip,mesh,m_MeshNodeGlobalTransform,m_PoseContext,m_PoseBoundsSettings))return false;
        if(!Animation::SkeletalPoseBuilder::BuildPose(m_PoseContext,pose,m_PoseScratch,m_Pose))return false;
        m_EvaluatedMesh=m_SkeletalAsset->GetMesh();m_EvaluatedSkeleton=m_SkeletalAsset->GetSkeleton();
        m_EvaluatedClip=GetAnimationClip();m_bPoseDirty=false;++m_PoseSerial;return true;
    }

    bool SkinnedMeshComponent::SetPoseBoundsSettings(const Animation::PoseBoundsSettings& settings)
    {
        if (!Animation::IsValidPoseBoundsSettings(settings)) return false;
        m_PoseBoundsSettings = settings;
        m_PoseContext = {};
        m_bPoseDirty = true;
        MarkRenderStateDirty();
        return true;
    }

    bool SkinnedMeshComponent::EvaluatePose()
    {
        if (!HasValidPoseResources())
        {
            m_PoseContext = {};
            m_Pose.Clear();
            m_bPoseDirty = true;
            MarkRenderStateDirty();
            return false;
        }
        if (HasCurrentPose())
        {
            return true;
        }
        if (m_bExternalAnimationDriven)
        {
            return false;
        }
        m_Pose.Clear();
        m_bPoseDirty = true;
        MarkRenderStateDirty();
        if (!m_bMeshNodeTransformOverridden)
        {
            m_MeshNodeGlobalTransform = LoadMeshNodeGlobalTransform(m_SkeletalAsset->GetMesh()->GetMeshNodeGlobalTransform());
        }
        const auto& skeleton = *m_SkeletalAsset->GetSkeleton();
        const auto& clip = *GetAnimationClip();
        const auto& mesh = *m_SkeletalAsset->GetMesh();
        const bool sameOwners = m_EvaluatedSkeleton.lock() == m_SkeletalAsset->GetSkeleton() &&
            m_EvaluatedMesh.lock() == m_SkeletalAsset->GetMesh() && m_EvaluatedClip.lock() == GetAnimationClip();
        if ((!sameOwners || !Animation::SkeletalPoseBuilder::IsPreparedFor(m_PoseContext, skeleton, clip, mesh, m_MeshNodeGlobalTransform)) &&
            !Animation::SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, m_MeshNodeGlobalTransform, m_PoseContext, m_PoseBoundsSettings))
        {
            return false;
        }
        const bool bSampled = Animation::SkeletalPoseBuilder::Sample(
            m_PoseContext, clip, m_AnimationTimeSeconds, m_PoseScratch, m_Pose);
        if (bSampled)
        {
            m_bPoseDirty = false;
            m_EvaluatedMesh = m_SkeletalAsset->GetMesh();
            m_EvaluatedSkeleton = m_SkeletalAsset->GetSkeleton();
            m_EvaluatedClip = GetAnimationClip();
            ++m_PoseSerial;
        }
        return bSampled;
    }

    bool SkinnedMeshComponent::HasValidPoseResources() const
    {
        if (!m_SkeletalAsset || !m_SkeletalAsset->IsLoaded() || !m_SkeletalAsset->IsValid()) return false;
        const auto& mesh = m_SkeletalAsset->GetMesh();
        const auto& skeleton = m_SkeletalAsset->GetSkeleton();
        const auto& clip = GetAnimationClip();
        return mesh && mesh->IsLoaded() && mesh->IsValid() && skeleton && skeleton->IsLoaded() &&
            skeleton->IsValid() && clip && clip->IsLoaded() && clip->IsValid();
    }

    bool SkinnedMeshComponent::HasCurrentPose() const
    {
        return !m_bPoseDirty && HasValidPoseResources() && !m_Pose.JointModelMatrices.empty() &&
               m_EvaluatedMesh.lock() == m_SkeletalAsset->GetMesh() &&
               m_EvaluatedSkeleton.lock() == m_SkeletalAsset->GetSkeleton() &&
               m_EvaluatedClip.lock() == GetAnimationClip() &&
               Animation::SkeletalPoseBuilder::IsPreparedFor(m_PoseContext, *m_SkeletalAsset->GetSkeleton(),
                   *GetAnimationClip(), *m_SkeletalAsset->GetMesh(), m_MeshNodeGlobalTransform);
    }

    int32_t SkinnedMeshComponent::FindJointIndex(Identity name) const
    {
        return HasValidPoseResources() ? m_SkeletalAsset->GetSkeleton()->FindJointIndex(name) : -1;
    }

    bool SkinnedMeshComponent::TryGetJointModelMatrix(uint32_t index, Math::Matrix4x4& outMatrix) const
    {
        if (!HasCurrentPose() || index >= m_Pose.JointModelMatrices.size()) return false;
        outMatrix = m_Pose.JointModelMatrices[index];
        return true;
    }

    bool SkinnedMeshComponent::TryGetJointWorldMatrix(uint32_t index, Math::Matrix4x4& outMatrix) const
    {
        Math::Matrix4x4 model;
        if (!GetOwner() || !TryGetJointModelMatrix(index, model)) return false;
        const Math::Matrix4x4 world = model * BuildOwnerWorldTransform();
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                if (!std::isfinite(world.m[row][column])) return false;
        outMatrix = world;
        return true;
    }

    bool SkinnedMeshComponent::TryGetJointWorldTransform(uint32_t index, Math::Transform& outTransform) const
    {
        Math::Matrix4x4 matrix;
        if (!TryGetJointWorldMatrix(index, matrix)) return false;
        // EngineのCreateWorldRowVectorは列ごとのscale。Sampler局所行列の行scaleと区別する。
        Math::Matrix4x4 rotation = Math::Matrix4x4::Identity;
        float scales[3]{};
        for (size_t column = 0; column < 3; ++column)
        {
            const double x = matrix.m[0][column], y = matrix.m[1][column], z = matrix.m[2][column];
            scales[column] = static_cast<float>(std::sqrt(x*x + y*y + z*z));
            if (!std::isfinite(scales[column]) || scales[column] <= Math::Constants::EPSILON) return false;
            for (size_t row = 0; row < 3; ++row) rotation.m[row][column] = matrix.m[row][column] / scales[column];
        }
        const float determinant = rotation.m00 * (rotation.m11*rotation.m22 - rotation.m12*rotation.m21) -
            rotation.m01 * (rotation.m10*rotation.m22 - rotation.m12*rotation.m20) +
            rotation.m02 * (rotation.m10*rotation.m21 - rotation.m11*rotation.m20);
        if (std::fabs(determinant - 1.0f) > 1e-4f) return false;
        Math::Quaternion quaternion = Math::QuaternionUtils::FromRotationMatrix(rotation);
        const float length = std::sqrt(quaternion.x*quaternion.x + quaternion.y*quaternion.y +
            quaternion.z*quaternion.z + quaternion.w*quaternion.w);
        if (!std::isfinite(length) || length <= Math::Constants::EPSILON) return false;
        quaternion = Math::Quaternion(quaternion.x/length, quaternion.y/length, quaternion.z/length, quaternion.w/length);
        const Math::Transform result(matrix.GetTranslationRow(), quaternion,
            Math::Vector3(scales[0], scales[1], scales[2]));
        const auto reconstructed = Math::MatrixUtils::CreateWorldRowVector(result.position, result.rotation, result.scale);
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                if (std::fabs(reconstructed.m[row][column] - matrix.m[row][column]) >
                    1e-5f * std::fmax(1.0f, std::fabs(matrix.m[row][column]))) return false;
        outTransform = result;
        return true;
    }

    Math::Matrix4x4 SkinnedMeshComponent::BuildOwnerWorldTransform() const
    {
        const Entity* owner = GetOwner();
        if (!owner)
        {
            return Math::Matrix4x4::Identity;
        }
        const Math::Transform& world = owner->GetWorldTransform();
        return Math::MatrixUtils::CreateWorldRowVector(world.position, world.rotation, world.scale);
    }
} // namespace NorvesLib::Core::Component

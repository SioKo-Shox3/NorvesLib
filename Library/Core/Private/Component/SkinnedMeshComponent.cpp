#include "Component/SkinnedMeshComponent.h"

#include "Engine/Engine.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Math/QuaternionUtils.h"
#include "Object/Entity.h"
#include "Rendering/RenderResources.h"
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
        bPlaying = true;
        bLooping = true;
        PlaybackRate = 1.0f;
        getbVisible().Set(true);
        getbCastShadow().Set(true);
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const FieldInitializer* initializer)
        : Component(initializer)
    {
        bPlaying = true;
        bLooping = true;
        PlaybackRate = 1.0f;
        getbVisible().Set(true);
        getbCastShadow().Set(true);
        SetTickGroup(ETickGroup::Animation);
        SetTickGroupMask(TickGroupBit(ETickGroup::Animation) | TickGroupBit(ETickGroup::PoseFinalize));
    }

    SkinnedMeshComponent::SkinnedMeshComponent(const IUnknown* sourceObject)
        : Component(sourceObject)
    {
        bPlaying = true;
        bLooping = true;
        PlaybackRate = 1.0f;
        getbVisible().Set(true);
        getbCastShadow().Set(true);
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
        ReleaseOwnedMaterial();
        m_SkeletalBinding.Reset();
        m_MaterialTextureBinding.Reset();
        m_Pose.Clear();
        m_PoseContext = {};
        m_PoseScratch = {};
        m_SkeletalAsset.reset();
        m_SelectedClip.reset();
        Component::Finalize();
    }

    void SkinnedMeshComponent::BeginPlay()
    {
        Component::BeginPlay();
        if (!SkeletalAssetPath->IsEmpty() && !m_SkeletalAsset)
        {
            m_SkeletalBinding.RequestSkeletal(SkeletalAssetPath.Get());
        }
        if (!MaterialTexture->IsEmpty() && !m_OwnedMaterial.IsValid())
        {
            m_MaterialTextureBinding.RequestTexture(MaterialTexture.Get());
        }
        PollAssetBindings();
    }

    void SkinnedMeshComponent::TickPoseFinalizeGroup()
    {
        if (!m_bExternalAnimationDriven)
        {
            (void)EvaluatePose();
        }
    }

    void SkinnedMeshComponent::AdvanceAnimationTime(float deltaTime)
    {
        PollAssetBindings();
        if (m_bExternalAnimationDriven || !bPlaying || !std::isfinite(deltaTime) || !std::isfinite(PlaybackRate.Get()) || !m_SkeletalAsset ||
            !GetAnimationClip())
        {
            return;
        }

        const float duration = GetAnimationClip()->GetClip().DurationSeconds;
        if (!std::isfinite(duration) || duration <= Math::Constants::EPSILON)
        {
            return;
        }
        AnimationTimeSeconds.Get() += deltaTime * PlaybackRate.Get();
        if (bLooping)
        {
            AnimationTimeSeconds = std::fmod(AnimationTimeSeconds.Get(), duration);
            if (AnimationTimeSeconds.Get() < 0.0f)
            {
                AnimationTimeSeconds.Get() += duration;
            }
        }
        else
        {
            AnimationTimeSeconds = std::fmax(0.0f, std::fmin(AnimationTimeSeconds.Get(), duration));
        }
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    void SkinnedMeshComponent::SetSkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset)
    {
        m_SkeletalBinding.Reset();
        SkeletalAssetPath = SkeletalAssetRef{};
        AnimationClipName = Container::String{};
        ApplySkeletalAsset(asset);
    }

    void SkinnedMeshComponent::SetSkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset,
                                                Container::StringView sourcePath)
    {
        if (!SkeletalAssetPath->TrySet(sourcePath))
        {
            SkeletalAssetPath = SkeletalAssetRef{};
        }
        m_SkeletalBinding.MarkExternal(Asset::AssetKind::Skeleton, SkeletalAssetPath->GetLogicalPath());
        AnimationClipName = Container::String{};
        ApplySkeletalAsset(asset);
    }

    void SkinnedMeshComponent::ApplySkeletalAsset(const Container::TSharedPtr<SkeletalAssetResource>& asset)
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
        RefreshMeshHandle();
        MarkRenderStateDirty();
    }

    const Container::TSharedPtr<SkeletalAssetResource>& SkinnedMeshComponent::GetSkeletalAsset() const
    {
        return m_SkeletalAsset;
    }

    void SkinnedMeshComponent::SetSkeletalAssetPath(const SkeletalAssetRef& reference)
    {
        SkeletalAssetPath = reference;
        OnPropertyApplied(*s_PropertyPtr_SkeletalAssetPath);
    }

    void SkinnedMeshComponent::SetMaterialTexture(const TextureAssetRef& reference)
    {
        MaterialTexture = reference;
        OnPropertyApplied(*s_PropertyPtr_MaterialTexture);
    }

    void SkinnedMeshComponent::OnPropertyApplied(const ClassProperty& property)
    {
        Component::OnPropertyApplied(property);
        if (&property == s_PropertyPtr_SkeletalAssetPath)
        {
            if (SkeletalAssetPath->GetLogicalPath() == m_SkeletalBinding.GetLogicalPath())
            {
                return;
            }
            ApplySkeletalAsset({});
            m_SkeletalBinding.Reset();
            if (!SkeletalAssetPath->IsEmpty() && bBegunPlay)
            {
                m_SkeletalBinding.RequestSkeletal(SkeletalAssetPath.Get());
                PollAssetBindings();
            }
        }
        else if (&property == s_PropertyPtr_MaterialTexture)
        {
            if (MaterialTexture->GetLogicalPath() == m_MaterialTextureBinding.GetLogicalPath())
            {
                return;
            }
            StoreMaterial(Rendering::MaterialHandle::Invalid());
            ReleaseOwnedMaterial();
            MarkRenderStateDirty();
            m_MaterialTextureBinding.Reset();
            if (!MaterialTexture->IsEmpty() && bBegunPlay)
            {
                m_MaterialTextureBinding.RequestTexture(MaterialTexture.Get());
                PollAssetBindings();
            }
        }
        else if (&property == s_PropertyPtr_AnimationClipName)
        {
            ApplyAnimationClipName();
        }
        else if (&property == s_PropertyPtr_AnimationTimeSeconds)
        {
            m_bPoseDirty = true;
            MarkRenderStateDirty();
        }
    }

    void SkinnedMeshComponent::ApplyAnimationClipName()
    {
        if (!m_SkeletalAsset)
        {
            return;
        }
        if (!AnimationClipName->empty())
        {
            const auto clip = m_SkeletalAsset->GetClip(Container::StringView(AnimationClipName.Get()));
            if (clip && SetAnimationClip(clip))
            {
                return;
            }
            LOG_WARNING("SKINNED_CLIP_NOT_FOUND name=%s 指定したクリップを選べないため既定を使います", AnimationClipName->c_str());
        }
        m_PoseContext = {};
        m_SelectedClip.reset();
        m_Pose.Clear();
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    void SkinnedMeshComponent::PollAssetBindings()
    {
        Container::TSharedPtr<SkeletalAssetResource> asset;
        if (m_SkeletalBinding.TakeReadySkeletal(asset))
        {
            ApplySkeletalAsset(asset);
            if (!AnimationClipName->empty())
            {
                ApplyAnimationClipName();
            }
        }

        if (Engine::GEngine)
        {
            Rendering::TextureHandle texture;
            if (m_MaterialTextureBinding.TakeReadyTexture(texture))
            {
                Rendering::MaterialCreateData material;
                material.AlbedoTexture = texture;
                m_OwnedMaterial = Engine::GEngine->GetRenderResources().Materials().Create(material);
                if (m_OwnedMaterial.IsValid())
                {
                    StoreMaterial(m_OwnedMaterial);
                    MarkRenderStateDirty();
                }
                else
                {
                    LOG_WARNING("SKINNED_MATERIAL_CREATE_FAILED 基本色テクスチャの材質を作れません");
                }
            }
        }
    }

    void SkinnedMeshComponent::ReleaseOwnedMaterial()
    {
        if (!m_OwnedMaterial.IsValid())
        {
            return;
        }
        if (LoadMaterial() == m_OwnedMaterial)
        {
            StoreMaterial(Rendering::MaterialHandle::Invalid());
        }
        if (Engine::GEngine)
        {
            Engine::GEngine->GetRenderResources().Materials().Release(m_OwnedMaterial);
        }
        m_OwnedMaterial = Rendering::MaterialHandle::Invalid();
    }

    bool SkinnedMeshComponent::SetAnimationClip(const Container::TSharedPtr<AnimationClipResource>& clip)
    {
        if (!clip)
        {
            AnimationClipName = Container::String{};
            m_PoseContext = {};
            m_SelectedClip.reset();
            m_Pose.Clear();
            m_bPoseDirty = true;
            MarkRenderStateDirty();
            return true;
        }
        if (!m_SkeletalAsset || !m_SkeletalAsset->IsLoaded() || !m_SkeletalAsset->IsValid() ||
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
        AnimationClipName = clip->GetClip().Name;
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
        AnimationTimeSeconds = timeSeconds;
        m_bPoseDirty = true;
        MarkRenderStateDirty();
    }

    float SkinnedMeshComponent::GetAnimationTimeSeconds() const
    {
        return AnimationTimeSeconds;
    }

    void SkinnedMeshComponent::SetPlaying(bool bPlaying)
    {
        this->bPlaying = bPlaying;
    }

    bool SkinnedMeshComponent::IsPlaying() const
    {
        return bPlaying;
    }

    void SkinnedMeshComponent::SetLooping(bool bLooping)
    {
        this->bLooping = bLooping;
    }

    bool SkinnedMeshComponent::IsLooping() const
    {
        return bLooping;
    }

    void SkinnedMeshComponent::SetPlaybackRate(float playbackRate)
    {
        PlaybackRate = playbackRate;
    }

    float SkinnedMeshComponent::GetPlaybackRate() const
    {
        return PlaybackRate;
    }

    Rendering::SkinnedMeshHandle SkinnedMeshComponent::LoadMeshHandle() const
    {
        Rendering::SkinnedMeshHandle handle = m_MeshHandle;
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type,
                                     static_cast<uint32_t>(offsetof(Ecs::EcsSkinnedRender, MeshHandle)),
                                     static_cast<uint32_t>(sizeof(handle)), &handle);
        return handle;
    }

    void SkinnedMeshComponent::StoreMeshHandle(Rendering::SkinnedMeshHandle handle)
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsWriteColumnPart(row, type, static_cast<uint32_t>(offsetof(Ecs::EcsSkinnedRender, MeshHandle)),
                                    static_cast<uint32_t>(sizeof(handle)), &handle);
            return;
        }
        m_MeshHandle = handle;
    }

    Rendering::MaterialHandle SkinnedMeshComponent::LoadMaterial() const
    {
        Rendering::MaterialHandle material = m_Material;
        // 行が無い間（列を読めない間）は予備のメンバの値のまま返す
        (void)Ecs::EcsReadColumnPart(GetEcsRow(), Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type,
                                     static_cast<uint32_t>(offsetof(Ecs::EcsSkinnedRender, Material)),
                                     static_cast<uint32_t>(sizeof(material)), &material);
        return material;
    }

    void SkinnedMeshComponent::StoreMaterial(Rendering::MaterialHandle material)
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsSkinnedRender>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsWriteColumnPart(row, type, static_cast<uint32_t>(offsetof(Ecs::EcsSkinnedRender, Material)),
                                    static_cast<uint32_t>(sizeof(material)), &material);
            return;
        }
        m_Material = material;
    }

    void SkinnedMeshComponent::RefreshMeshHandle()
    {
        Rendering::SkinnedMeshHandle handle = Rendering::SkinnedMeshHandle::Invalid();
        if (m_SkeletalAsset && m_SkeletalAsset->GetMesh())
        {
            handle = m_SkeletalAsset->GetMesh()->GetRenderMeshHandle();
        }
        if (LoadMeshHandle() != handle)
        {
            StoreMeshHandle(handle);
        }
    }

    void SkinnedMeshComponent::StorePoseVersion()
    {
        const Ecs::EcsRowRef row = GetEcsRow();
        const TypeId type = Ecs::EcsColumnInfoOf<Ecs::EcsPoseVersion>().Type;
        if (Ecs::EcsRowHasColumn(row, type))
        {
            Ecs::EcsWriteColumnPart(row, type, 0, static_cast<uint32_t>(sizeof(m_PoseSerial)), &m_PoseSerial);
        }
    }

    void SkinnedMeshComponent::CollectRenderResourceRefs(
        Container::VariableArray<Rendering::RenderResourceRef>& out) const
    {
        const Rendering::SkinnedMeshHandle meshHandle = LoadMeshHandle();
        if (meshHandle.IsValid())
        {
            out.push_back(Rendering::RenderResourceRef{Rendering::RenderResourceKind::SkinnedMesh, meshHandle.Id});
        }
        const uint32_t slotCount = GetMaterialSlotCount();
        for (uint32_t slot = 0; slot < slotCount; ++slot)
        {
            Rendering::MaterialHandle material;
            if (TryGetSlotMaterial(slot, material) && material.IsValid())
            {
                out.push_back(Rendering::RenderResourceRef{Rendering::RenderResourceKind::Material, material.Id});
            }
        }
    }

    void SkinnedMeshComponent::OnEcsRowAttached()
    {
        // 可視・影は ECS_PROPERTY が移す。保存しない handle とポーズの版はここで列へ写す。
        StoreMeshHandle(m_MeshHandle);
        StoreMaterial(m_Material);
        StorePoseVersion();
    }

    void SkinnedMeshComponent::OnEcsRowDetaching()
    {
        m_MeshHandle = LoadMeshHandle();
        m_Material = LoadMaterial();
    }

    void SkinnedMeshComponent::SetVisible(bool bVisible)
    {
        getbVisible().Set(bVisible);
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::IsVisible() const
    {
        return getbVisible().Get() && IsActive();
    }

    void SkinnedMeshComponent::SetMaterial(Rendering::MaterialHandle material)
    {
        StoreMaterial(material);
        if (material != m_OwnedMaterial)
        {
            ReleaseOwnedMaterial();
        }
        m_MaterialTextureBinding.Reset();
        MaterialTexture = TextureAssetRef{};
        MarkRenderStateDirty();
    }

    Rendering::MaterialHandle SkinnedMeshComponent::GetMaterial() const
    {
        return LoadMaterial();
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

    void SkinnedMeshComponent::SetShadingParams(uint32_t index, float value)
    {
        if (index >= Rendering::ShadingParamCount)
        {
            return;
        }
        const float sanitized = Rendering::SanitizeShadingParam(value);
        if (m_ShadingParams[index] != sanitized)
        {
            m_ShadingParams[index] = sanitized;
            MarkRenderStateDirty();
        }
    }

    float SkinnedMeshComponent::GetShadingParams(uint32_t index) const
    {
        return index < Rendering::ShadingParamCount ? m_ShadingParams[index] : -1.0f;
    }

    void SkinnedMeshComponent::SetDissolveThreshold(float threshold)
    {
        SetShadingParams(Rendering::ShadingParamP0, threshold);
    }

    float SkinnedMeshComponent::GetDissolveThreshold() const
    {
        return GetShadingParams(Rendering::ShadingParamP0);
    }

    void SkinnedMeshComponent::SetOpacity(float opacity)
    {
        const float sanitized = std::isnan(opacity) ? 1.0f : std::fmax(0.0f, std::fmin(opacity, 1.0f));
        if (m_Opacity != sanitized)
        {
            m_Opacity = sanitized;
            MarkRenderStateDirty();
        }
    }

    float SkinnedMeshComponent::GetOpacity() const
    {
        return m_Opacity;
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
            StoreMaterial(material);
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
        if (!m_SlotMaterials.TryGet(handle.Id, handle.Generation, count, slot, LoadMaterial().Id, id))
        {
            return false;
        }
        out = Rendering::MaterialHandle{id};
        return true;
    }

    void SkinnedMeshComponent::SetCastShadow(bool bCastShadow)
    {
        getbCastShadow().Set(bCastShadow);
        MarkRenderStateDirty();
    }

    bool SkinnedMeshComponent::CastsShadow() const
    {
        return getbCastShadow().Get();
    }

    bool SkinnedMeshComponent::BuildSkinnedMeshProxy(Rendering::SkinnedMeshProxy& outProxy, const Rendering::MaterialResources* materials)
    {
        if ((!SkeletalAssetPath->IsEmpty() && !m_SkeletalAsset) ||
            (!MaterialTexture->IsEmpty() && !m_OwnedMaterial.IsValid()) ||
            !IsVisible() || !EvaluatePose())
        {
            return false;
        }
        outProxy = {};
        for (uint32_t i = 0; i < Rendering::ShadingParamCount; ++i)
        {
            outProxy.ShadingParams[i] = m_ShadingParams[i];
        }
        outProxy.Opacity = m_Opacity;
        outProxy.ObjectId = GetOwnerId();
        outProxy.ComponentId = GetComponentId();
        const Container::TSharedPtr<SkinnedMeshResource>& mesh = m_SkeletalAsset->GetMesh();
        RefreshMeshHandle();
        outProxy.MeshHandle = LoadMeshHandle();
        outProxy.Material = LoadMaterial();
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
        if (materials != nullptr)
        {
            for (uint32_t slot = 0; slot < slotCount; ++slot)
            {
                const Rendering::MaterialHandle handle = outProxy.Materials[slot];
                if (!handle.IsValid())
                {
                    continue;
                }
                const Rendering::MaterialResourceData* data = materials->GetData(handle);
                if (data != nullptr && data->Shading == Rendering::ShadingModel::Fur)
                {
                    outProxy.FurSlots.push_back(Rendering::SkinnedMeshFurSlot{slot, handle, data->Revision, data->Fur});
                }
            }
        }
        outProxy.AssetLease = mesh->GetRenderAssetLease();
        outProxy.WorldTransform = Math::Matrix4x4::Identity;
        if (const auto* owner = GetOwner())
        {
            const auto& renderWorld = owner->GetRenderWorldTransform();
            outProxy.WorldTransform =
                Math::MatrixUtils::CreateWorldRowVector(renderWorld.position, renderWorld.rotation, renderWorld.scale);
        }
        outProxy.BonePalette = m_Pose.BonePalette;
        outProxy.AnimatedBounds = m_Pose.AnimatedBounds;
        outProxy.bCastShadow = getbCastShadow().Get();
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
        if (BeginSubmitLocalPose(pose))
        {
            RunPoseEvaluation();
        }
        return FinishPoseEvaluation();
    }

    bool SkinnedMeshComponent::BeginSubmitLocalPose(const Animation::LocalPose& pose)
    {
        m_PendingClip = nullptr;
        m_PendingLocalPose = nullptr;
        m_bPoseEvaluationSucceeded = false;
        if (!m_bExternalAnimationDriven || !HasValidPoseResources())
        {
            return false;
        }
        if (!m_bMeshNodeTransformOverridden)
        {
            m_MeshNodeGlobalTransform = LoadMeshNodeGlobalTransform(m_SkeletalAsset->GetMesh()->GetMeshNodeGlobalTransform());
        }
        const auto& skeleton = *m_SkeletalAsset->GetSkeleton();
        const auto& clip = *GetAnimationClip();
        const auto& mesh = *m_SkeletalAsset->GetMesh();
        m_bPoseDirty = true;
        m_Pose.Clear();
        MarkRenderStateDirty();
        if (!Animation::SkeletalPoseBuilder::IsPreparedFor(m_PoseContext, skeleton, clip, mesh, m_MeshNodeGlobalTransform) &&
            !Animation::SkeletalPoseBuilder::Prepare(skeleton, clip, mesh, m_MeshNodeGlobalTransform, m_PoseContext, m_PoseBoundsSettings))
        {
            return false;
        }
        ReservePoseEvaluationBuffers(skeleton.GetPoseRuntime().Parents.size());
        m_PendingLocalPose = &pose;
        return true;
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
        if (BeginPoseEvaluation())
        {
            RunPoseEvaluation();
        }
        return FinishPoseEvaluation();
    }

    bool SkinnedMeshComponent::BeginPoseEvaluation()
    {
        m_PendingClip = nullptr;
        m_PendingLocalPose = nullptr;
        m_bPoseEvaluationSucceeded = false;
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
            m_bPoseEvaluationSucceeded = true;
            return false;
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
        ReservePoseEvaluationBuffers(skeleton.GetPoseRuntime().Parents.size());
        m_PendingClip = &clip;
        m_PendingPoseTimeSeconds = AnimationTimeSeconds;
        return true;
    }

    void SkinnedMeshComponent::ReservePoseEvaluationBuffers(size_t jointCount)
    {
        m_PoseScratch.Local.reserve(jointCount);
        m_PoseScratch.LocalMatrices.reserve(jointCount);
        m_PoseScratch.GlobalMatrices.reserve(jointCount);
        m_Pose.BonePalette.reserve(jointCount);
        m_Pose.JointModelMatrices.reserve(jointCount);
    }

    void SkinnedMeshComponent::RunPoseEvaluation()
    {
        if (m_PendingLocalPose)
        {
            m_bPoseEvaluationSucceeded = Animation::SkeletalPoseBuilder::BuildPose(
                m_PoseContext, *m_PendingLocalPose, m_PoseScratch, m_Pose);
        }
        else if (m_PendingClip)
        {
            m_bPoseEvaluationSucceeded = Animation::SkeletalPoseBuilder::Sample(
                m_PoseContext, *m_PendingClip, m_PendingPoseTimeSeconds, m_PoseScratch, m_Pose);
        }
    }

    bool SkinnedMeshComponent::FinishPoseEvaluation()
    {
        if (HasPendingPoseEvaluation() && m_bPoseEvaluationSucceeded)
        {
            // 直列経路の従来の書き込み順を、外部姿勢の提出と通常の評価それぞれで保つ。
            if (!m_PendingLocalPose)
            {
                m_bPoseDirty = false;
            }
            m_EvaluatedMesh = m_SkeletalAsset->GetMesh();
            m_EvaluatedSkeleton = m_SkeletalAsset->GetSkeleton();
            m_EvaluatedClip = GetAnimationClip();
            if (m_PendingLocalPose)
            {
                m_bPoseDirty = false;
            }
            ++m_PoseSerial;
            StorePoseVersion();
        }
        m_PendingClip = nullptr;
        m_PendingLocalPose = nullptr;
        return m_bPoseEvaluationSucceeded;
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

    bool SkinnedMeshComponent::TryGetBonePalette(Container::Span<const Math::Matrix4x4>& outPalette) const
    {
        if (!HasCurrentPose() || m_Pose.BonePalette.empty())
        {
            return false;
        }
        outPalette = Container::Span<const Math::Matrix4x4>(m_Pose.BonePalette.data(), m_Pose.BonePalette.size());
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

    bool SkinnedMeshComponent::GetSocketWorldTransform(Identity name, Math::Transform& out) const
    {
        if (!GetOwner() || !HasCurrentPose() || !m_SkeletalAsset || !m_SkeletalAsset->GetSkeleton())
            return false;
        const auto* socket = m_SkeletalAsset->GetSkeleton()->FindSocket(name);
        if (!socket)
            return false;
        Math::Matrix4x4 joint;
        return TryGetJointModelMatrix(socket->ParentJoint, joint) &&
               Animation::BuildSocketWorldTransform(joint, socket->Offset, GetOwner()->GetWorldTransform(), out);
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

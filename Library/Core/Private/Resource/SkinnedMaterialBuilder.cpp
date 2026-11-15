#include "Resource/SkinnedMaterialBuilder.h"

#include "Animation/SkeletalAssetResource.h"
#include "Component/SkinnedMeshComponent.h"
#include "Logging/LogMacros.h"
#include "Rendering/RenderResources.h"
#include "Rendering/TextureAssetLoader.h"
#include "Rendering/TextureUploadProfile.h"
#include "Resource/ImportedOpaqueRuntime.h"
#include "RHI/ITexture.h"

#include <utility>

namespace NorvesLib::Core::ResourceIO
{
    namespace
    {
        constexpr const char* TextureRole = "main_render";
        constexpr uint32_t TextureRequestId = 0;

        const char* StagingFailureReason(ModelStaging::MaterialStagingStatus status)
        {
            switch (status)
            {
            case ModelStaging::MaterialStagingStatus::InvalidRecord:
                return "imported_opaque: invalid_material_values";
            case ModelStaging::MaterialStagingStatus::InvalidPath:
            case ModelStaging::MaterialStagingStatus::InvalidReference:
                return "imported_opaque: logical_path";
            default:
                return "imported_opaque: layout";
            }
        }

        // CPU 側の検査を終えてから texture を作り、匿名 ARM の所有だけを返す。
        bool FinalizeTextures(const ModelStaging::ImportedMaterialStaging& source,
                              Rendering::TextureResources& textures,
                              Rendering::MaterialCreateData& data,
                              Container::VariableArray<Container::TSharedPtr<const void>>& owners,
                              Container::AnsiString& reason)
        {
            Rendering::PreparedTextureAsset prepared[3];
            const Container::AnsiString* paths[] = {&source.AlbedoPath, &source.NormalPath, &source.ArmPath};
            const char* roles[] = {"albedo", "normal", "arm"};
            for (size_t i = 0; i < 3; ++i)
            {
                if (paths[i]->empty() || (i == 2 && source.ArmMask == 0))
                {
                    continue;
                }
                const Container::String path(Container::StringView(paths[i]->data(), paths[i]->size()));
                prepared[i] = textures.PrepareTextureAssetForWorker(path, {}, TextureRole, TextureRequestId);
                if (prepared[i].Status != Rendering::PreparedTextureAssetStatus::CookedReady || !prepared[i].Payload ||
                    !textures.IsPreparedTextureAssetCurrent(prepared[i]))
                {
                    reason = "imported_opaque: cooked_texture_required role=";
                    reason += roles[i];
                    return false;
                }
                if (!ModelStaging::ValidateImportedTexture(prepared[i].Payload->Texture,
                                                          i == 0 ? Asset::CookedTextureColorSpace::SRGB
                                                                 : Asset::CookedTextureColorSpace::Linear,
                                                          i == 0 && source.Alpha == ModelStaging::ImportedAlphaMode::Opaque,
                                                          reason))
                {
                    reason += " role=";
                    reason += roles[i];
                    return false;
                }
            }

            ModelStaging::ImportedArmChannels arm;
            if (source.ArmMask != 0)
            {
                if (!prepared[2].Payload)
                {
                    reason = "imported_opaque: cooked_texture_required role=arm";
                    return false;
                }
                if (!ModelStaging::SplitImportedArmChannels(prepared[2].Payload->Texture, source.ArmMask, arm, reason))
                {
                    return false;
                }
            }

            // 全 texture の世代を作成直前に再確認する。
            for (const auto& texture : prepared)
            {
                if (texture.Payload && !textures.IsPreparedTextureAssetCurrent(texture))
                {
                    reason = "imported_opaque: texture_generation_changed";
                    return false;
                }
            }

            Rendering::ScopedTextureCreateUploadProfileRole profileRole(TextureRole);
            Rendering::TextureHandle* standard[] = {&data.AlbedoTexture, &data.NormalTexture};
            for (size_t i = 0; i < 2; ++i)
            {
                if (!prepared[i].Payload)
                {
                    continue;
                }
                *standard[i] = textures.FinalizePreparedTextureAsset(prepared[i], TextureRole, TextureRequestId);
                if (!standard[i]->IsValid())
                {
                    reason = "imported_opaque: texture_finalize role=";
                    reason += roles[i];
                    return false;
                }
            }

            Rendering::TextureHandle* channels[] = {&data.AOTexture, &data.RoughnessTexture, &data.MetallicTexture};
            const char* suffixes[] = {"_AO", "_Roughness", "_Metallic"};
            for (size_t i = 0; i < 3; ++i)
            {
                if ((source.ArmMask & (1u << i)) == 0)
                {
                    continue;
                }
                Rendering::TextureCreateInfo info;
                info.Width = arm.Mips[0].Width;
                info.Height = arm.Mips[0].Height;
                info.MipLevels = static_cast<uint32_t>(arm.Mips.size());
                info.PixelFormat = Rendering::TextureCreateInfo::Format::R8_UNORM;
                info.DebugName = data.DebugName + suffixes[i];
                const auto handle = textures.CreateTexture(info);
                if (!handle.IsValid())
                {
                    reason = "imported_opaque: arm_channel_finalize";
                    return false;
                }
                // Adopt が例外を送出した場合は、その中で handle が解放される。
                auto owner = textures.AdoptAnonymousTexture(handle);
                if (!owner)
                {
                    textures.ReleaseTexture(handle);
                    reason = "imported_opaque: arm_channel_owner";
                    return false;
                }
                owners.push_back(std::move(owner));
                *channels[i] = handle;
                const auto texture = textures.GetRHITexturePtr(handle);
                if (!texture)
                {
                    reason = "imported_opaque: arm_channel_pointer";
                    return false;
                }
                // cook 済みの全 mip をそれぞれ送る。
                for (size_t level = 0; level < arm.Mips.size(); ++level)
                {
                    const auto& mip = arm.Mips[level];
                    texture->Update(mip.Pixels[i].data(), mip.Width, static_cast<uint32_t>(mip.Pixels[i].size()),
                                    static_cast<uint32_t>(level), 0);
                }
            }
            // 発光画像は正の nits なら検査で拒否し、0 nits なら寄与しない。
            return true;
        }
    } // namespace

    struct SkinnedMaterialBuilder::Entry
    {
        explicit Entry(Rendering::RenderResources& resources) : Resources(resources) {}

        ~Entry()
        {
            // 材質が texture を参照しなくなってから匿名 texture の所有を離す。
            for (const auto handle : Materials)
            {
                Resources.Materials().Release(handle);
            }
            TextureOwners.clear();
        }

        Rendering::RenderResources& Resources;
        Container::TWeakPtr<SkinnedMeshResource> Mesh;
        uint64_t Generation = 0;
        Container::TSharedPtr<const SkinnedMaterialBuildResult> Result;
        Container::VariableArray<Rendering::MaterialHandle> Materials;
        Container::VariableArray<Container::TSharedPtr<const void>> TextureOwners;
    };

    SkinnedMaterialBuilder::SkinnedMaterialBuilder(Rendering::RenderResources& resources,
                                                   Thread::Thread::ThreadId owner)
        : m_Resources(resources), m_Owner(owner)
    {
    }

    SkinnedMaterialBuilder::~SkinnedMaterialBuilder()
    {
        Clear();
    }

    bool SkinnedMaterialBuilder::IsOwnerThread() const
    {
        return m_Owner != Thread::Thread::ThreadId{} && m_Owner == Thread::Thread::GetCurrentThreadId();
    }

    Container::TSharedPtr<const SkinnedMaterialBuildResult> SkinnedMaterialBuilder::Build(
        const Container::TSharedPtr<SkeletalAssetResource>& asset)
    {
        auto result = Container::MakeShared<SkinnedMaterialBuildResult>();
        if (!IsOwnerThread())
        {
            result->Status = SkinnedMaterialBuildStatus::WrongThread;
            return result;
        }
        if (!asset || !asset->GetMesh())
        {
            result->Status = SkinnedMaterialBuildStatus::InvalidAsset;
            return result;
        }
        const auto mesh = asset->GetMesh();
        if (!mesh->IsSplitV1())
        {
            result->Status = SkinnedMaterialBuildStatus::NotSplitMesh;
            return result;
        }
        if (!mesh->IsLoaded())
        {
            result->Status = SkinnedMaterialBuildStatus::NotLoaded;
            return result;
        }

        Collect();
        const uint64_t generation = mesh->GetSplitMeshGeneration();
        for (const auto& entry : m_Entries)
        {
            if (entry->Generation == generation && entry->Mesh.lock() == mesh)
            {
                return entry->Result;
            }
        }

        const auto& sources = mesh->GetSlotMaterialSources();
        result->MeshResourceId = mesh->GetResourceId();
        result->Generation = generation;
        result->Status = SkinnedMaterialBuildStatus::Success;
        result->Slots.reserve(sources.size());
        m_Entries.reserve(m_Entries.size() + 1);
        auto entry = Container::MakeUnique<Entry>(m_Resources);
        entry->Mesh = mesh;
        entry->Generation = generation;
        entry->Materials.reserve(sources.size());

        for (size_t i = 0; i < sources.size(); ++i)
        {
            const auto& source = sources[i];
            result->Slots.emplace_back();
            auto& slot = result->Slots.back();
            slot.Slot = static_cast<uint32_t>(i);
            slot.Name = source.SlotName;

            const auto buildSlot = [&]()
            {
                ModelStaging::ImportedMaterialStaging staging;
                const auto status = ModelStaging::BuildImportedMaterialStaging(
                    source.Record, Container::Span<const Container::AnsiString>(source.Textures, 4), staging);
                if (status != ModelStaging::MaterialStagingStatus::Success)
                {
                    slot.Status = SkinnedMaterialSlotStatus::InvalidMaterial;
                    slot.Reason = StagingFailureReason(status);
                    return;
                }
                if (!ModelStaging::ValidateImportedOpaqueMaterial(staging, slot.Reason))
                {
                    slot.Status = SkinnedMaterialSlotStatus::Unsupported;
                    return;
                }
                if (staging.Alpha == ModelStaging::ImportedAlphaMode::Mask)
                {
                    slot.Status = SkinnedMaterialSlotStatus::Unsupported;
                    slot.Reason = "imported_opaque: alphaMode_mask_unsupported";
                    return;
                }
                if (staging.bDoubleSided)
                {
                    slot.Status = SkinnedMaterialSlotStatus::Unsupported;
                    slot.Reason = "imported_opaque: doubleSided_unsupported";
                    return;
                }
                if ((staging.ArmMask & 1u) != 0 && staging.OcclusionStrength != 1.0f)
                {
                    slot.Status = SkinnedMaterialSlotStatus::Unsupported;
                    slot.Reason = "imported_opaque: occlusionStrength_requires_shader_support";
                    return;
                }

                Rendering::MaterialCreateData data;
                for (size_t component = 0; component < 4; ++component)
                {
                    data.BaseColor[component] = staging.BaseColor[component];
                }
                for (size_t component = 0; component < 3; ++component)
                {
                    data.EmissiveColor[component] = staging.EmissiveColor[component];
                }
                data.EmissiveLuminanceNits = staging.EmissiveLuminanceNits;
                data.Metallic = staging.Metallic;
                data.Roughness = staging.Roughness;
                data.Blend = Rendering::BlendMode::Opaque;
                data.bTwoSided = false;
                data.Shading = Rendering::ShadingModel::DefaultLit;
                data.DebugName = "Skinned:";
                data.DebugName += slot.Name;

                Container::VariableArray<Container::TSharedPtr<const void>> owners;
                const size_t firstOwner = entry->TextureOwners.size();
                try
                {
                    if (!FinalizeTextures(staging, m_Resources.Textures(), data, owners, slot.Reason))
                    {
                        slot.Status = SkinnedMaterialSlotStatus::TextureFailed;
                        return;
                    }
                    for (auto& owner : owners)
                    {
                        entry->TextureOwners.push_back(std::move(owner));
                    }
                }
                catch (...)
                {
                    entry->TextureOwners.resize(firstOwner);
                    slot.Status = SkinnedMaterialSlotStatus::TextureFailed;
                    slot.Reason = "imported_opaque: texture_exception";
                    return;
                }

                slot.CreateData = std::move(data);

                Rendering::MaterialHandle material;
                try
                {
                    material = m_Resources.Materials().Create(slot.CreateData);
                }
                catch (...)
                {
                    entry->TextureOwners.resize(firstOwner);
                    slot.Status = SkinnedMaterialSlotStatus::CreateFailed;
                    slot.Reason = "imported_opaque: material_create";
                    return;
                }
                if (!material.IsValid())
                {
                    entry->TextureOwners.resize(firstOwner);
                    slot.Status = SkinnedMaterialSlotStatus::CreateFailed;
                    slot.Reason = "imported_opaque: material_create";
                    return;
                }
                try
                {
                    entry->Materials.push_back(material);
                }
                catch (...)
                {
                    m_Resources.Materials().Release(material);
                    throw;
                }
                slot.Material = material;
                slot.Status = SkinnedMaterialSlotStatus::Created;
                slot.Reason.clear();
            };
            buildSlot();
            if (slot.Status != SkinnedMaterialSlotStatus::Created)
            {
                result->Status = SkinnedMaterialBuildStatus::PartialFailure;
                NORVES_LOG_WARNING("SkinnedMaterial", "slot=%u の材質を作成できません: %s", slot.Slot,
                                   slot.Reason.c_str());
            }
        }

        entry->Result = result;
        m_Entries.push_back(std::move(entry));
        return result;
    }

    SkinnedMaterialBindResult SkinnedMaterialBuilder::Bind(Component::SkinnedMeshComponent& component,
                                                           const SkinnedMaterialBuildResult& result) const
    {
        SkinnedMaterialBindResult bound;
        if (!IsOwnerThread())
        {
            return bound;
        }
        const auto& asset = component.GetSkeletalAsset();
        if (!asset || !asset->GetMesh())
        {
            return bound;
        }
        const auto& mesh = asset->GetMesh();
        if (mesh->GetResourceId() != result.MeshResourceId || mesh->GetSplitMeshGeneration() != result.Generation)
        {
            return bound;
        }
        bound.Slots.reserve(result.Slots.size());
        bound.bAccepted = true;
        for (const auto& slot : result.Slots)
        {
            if (slot.Status != SkinnedMaterialSlotStatus::Created)
            {
                bound.Slots.push_back(SkinnedMaterialBindStatus::NotCreated);
            }
            else if (component.SetSlotMaterial(slot.Slot, slot.Material))
            {
                ++bound.BoundCount;
                bound.Slots.push_back(SkinnedMaterialBindStatus::Bound);
            }
            else
            {
                bound.Slots.push_back(SkinnedMaterialBindStatus::Rejected);
            }
        }
        return bound;
    }

    size_t SkinnedMaterialBuilder::Collect()
    {
        if (!IsOwnerThread())
        {
            return 0;
        }
        size_t removed = 0;
        for (auto it = m_Entries.begin(); it != m_Entries.end();)
        {
            const auto mesh = (*it)->Mesh.lock();
            if (!mesh || !mesh->IsLoaded() || mesh->GetSplitMeshGeneration() != (*it)->Generation)
            {
                it = m_Entries.erase(it);
                ++removed;
            }
            else
            {
                ++it;
            }
        }
        return removed;
    }

    void SkinnedMaterialBuilder::Clear()
    {
        if (IsOwnerThread())
        {
            m_Entries.clear();
        }
    }

    size_t SkinnedMaterialBuilder::GetEntryCount() const noexcept
    {
        return m_Entries.size();
    }
} // namespace NorvesLib::Core::ResourceIO

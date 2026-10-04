#include "Rendering/SkinnedMeshGpuStore.h"

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Math/MatrixUtils.h"

#include <cstring>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    SkinnedMeshGpuStore::SkinnedMeshGpuStore(Container::TSharedPtr<RHI::IDevice> device)
        : m_Device(std::move(device))
    {
    }

    void SkinnedMeshGpuStore::BeginFrame(uint64_t completedSubmissionSerial)
    {
        if (m_bFrameOpen)
        {
            AbortFrame();
        }
        if (completedSubmissionSerial > m_CompletedSubmissionSerial)
        {
            m_CompletedSubmissionSerial = completedSubmissionSerial;
        }
        m_ActivePalettes.clear();
        // epochを再利用すると古いpreparedが通るため、枯渇時は新規フレームを開かない。
        if (m_PreparationEpoch == UINT64_MAX)
        {
            m_bFrameOpen = false;
            return;
        }
        ++m_PreparationEpoch;
        m_bFrameOpen = true;
        CollectReleased();
    }

    bool SkinnedMeshGpuStore::PrepareDraw(
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease,
        const Container::VariableArray<Math::Matrix4x4>& bonePalette,
        const Math::Matrix4x4& worldTransform,
        SkinnedMeshPreparedDraw& outPrepared,
        const Container::VariableArray<Math::Matrix4x4>* previousBonePalette,
        const Math::Matrix4x4* previousWorldTransform)
    {
        outPrepared = SkinnedMeshPreparedDraw{};
        if (!m_Device || !m_bFrameOpen || !frameLease || !frameLease->IsValid() || bonePalette.empty() ||
            (previousBonePalette && previousBonePalette->size() != bonePalette.size()))
        {
            return false;
        }

        const bool wantsPrevious = previousBonePalette || previousWorldTransform;
        const auto& effectivePreviousBones = previousBonePalette ? *previousBonePalette : bonePalette;
        const auto& effectivePreviousWorld = previousWorldTransform ? *previousWorldTransform : worldTransform;
        const auto sameMatrix = [](const Math::Matrix4x4& lhs, const Math::Matrix4x4& rhs)
        {
            return std::memcmp(lhs.values,rhs.values,sizeof(lhs.values)) == 0;
        };
        const auto sameBones = [&](const auto& lhs, const auto& rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (size_t index = 0; index < lhs.size(); ++index)
            {
                if (!sameMatrix(lhs[index],rhs[index]))
                {
                    return false;
                }
            }
            return true;
        };
        Container::TSharedPtr<PaletteUse> use;
        bool createCurrent = true;
        if (frameLease->ComponentId != 0)
        {
            const auto found = m_ActivePalettes.find(frameLease->ComponentId);
            if (found != m_ActivePalettes.end())
            {
                use = found->second;
                if (use->Handle != frameLease->AssetLease->GetHandle() ||
                    !sameMatrix(use->World,worldTransform) || !sameBones(use->Bones,bonePalette))
                {
                    return false;
                }
                createCurrent = false;
            }
        }
        Entry* entry = FindOrUpload(frameLease);
        if (!entry)
        {
            return false;
        }
        if (createCurrent)
        {
            use = Container::MakeShared<PaletteUse>();
            use->ComponentId = frameLease->ComponentId;
            use->Epoch = m_PreparationEpoch;
            use->Handle = entry->Handle;
            use->World = worldTransform;
            use->Bones = bonePalette;
            entry->PaletteUses.push_back(use);
            if (use->ComponentId != 0)
            {
                m_ActivePalettes[use->ComponentId] = use;
            }
            Container::VariableArray<float> uploadMatrices;
            uploadMatrices.resize((2 + bonePalette.size() * 2) * 16);
            Math::MatrixUtils::CopyToShaderData(worldTransform, uploadMatrices.data());
            const Math::Matrix4x4 worldNormal = Math::MatrixUtils::CreateNormalMatrix(worldTransform);
            Math::MatrixUtils::CopyToShaderData(worldNormal, uploadMatrices.data() + 16);
            for (size_t matrixIndex = 0; matrixIndex < bonePalette.size(); ++matrixIndex)
            {
                const Math::Matrix4x4& source = bonePalette[matrixIndex];
                float* positionDestination = uploadMatrices.data() + (2 + matrixIndex * 2) * 16;
                float* normalDestination = positionDestination + 16;
                Math::MatrixUtils::CopyToShaderData(source, positionDestination);
                const Math::Matrix4x4 normal = Math::MatrixUtils::CreateNormalMatrix(source);
                Math::MatrixUtils::CopyToShaderData(normal, normalDestination);
            }

            RHI::BufferDesc paletteDesc;
            paletteDesc.Size = static_cast<uint64_t>(uploadMatrices.size() * sizeof(float));
            paletteDesc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
            paletteDesc.CPUAccessible = true;
            paletteDesc.DebugName = "SkinnedPalette";
            RHI::BufferPtr paletteBuffer = m_Device->CreateBuffer(paletteDesc);
            if (!paletteBuffer)
            {
                return false;
            }
            paletteBuffer->Update(uploadMatrices.data(), paletteDesc.Size, 0);
            use->Buffer = paletteBuffer;
        }
        if (!use->Buffer)
        {
            return false;
        }

        // GBufferが初めて要求した時だけpreviousを作る。影のpreparedは常にprevious無し。
        if (wantsPrevious)
        {
            if (use->bPreviousAttempted)
            {
                if (!use->PreviousBuffer || !sameMatrix(use->PreviousWorld,effectivePreviousWorld) ||
                    !sameBones(use->PreviousBones,effectivePreviousBones))
                {
                    return false;
                }
            }
            else
            {
                use->bPreviousAttempted = true;
                use->PreviousWorld = effectivePreviousWorld;
                use->PreviousBones = effectivePreviousBones;
                Container::VariableArray<float> previousMatrices;
                previousMatrices.resize((1 + effectivePreviousBones.size()) * 16);
                Math::MatrixUtils::CopyToShaderData(effectivePreviousWorld,previousMatrices.data());
                for (size_t index = 0; index < effectivePreviousBones.size(); ++index)
                {
                    Math::MatrixUtils::CopyToShaderData(effectivePreviousBones[index],previousMatrices.data() + (1 + index)*16);
                }
                RHI::BufferDesc desc;
                desc.Size = static_cast<uint64_t>(previousMatrices.size()*sizeof(float));
                desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
                desc.CPUAccessible = true;
                desc.DebugName = "SkinnedPreviousPalette";
                use->PreviousBuffer = m_Device->CreateBuffer(desc);
                if (!use->PreviousBuffer)
                {
                    return false;
                }
                use->PreviousBuffer->Update(previousMatrices.data(),desc.Size,0);
            }
        }

        TrackFrameLease(*entry, frameLease);
        bool tracked = false;
        for (const auto& weak : use->FrameLeases)
        {
            tracked = tracked || weak.lock().get() == frameLease.get();
        }
        if (!tracked)
        {
            use->FrameLeases.push_back(frameLease);
        }

        outPrepared.MeshHandle = entry->Handle;
        outPrepared.VertexBuffer = entry->VertexBuffer;
        outPrepared.IndexBuffer = entry->IndexBuffer;
        outPrepared.PaletteBuffer = use->Buffer;
        outPrepared.PreviousPaletteBuffer = wantsPrevious ? use->PreviousBuffer : RHI::BufferPtr{};
        outPrepared.ComponentId = use->ComponentId;
        outPrepared.PreparationEpoch = use->ComponentId != 0 ? use->Epoch : 0;
        outPrepared.bUsesPreviousPalette = wantsPrevious;
        outPrepared.IndexCount = entry->IndexCount;
        return true;
    }

    bool SkinnedMeshGpuStore::MarkLastUse(
        const SkinnedMeshPreparedDraw& prepared,
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease)
    {
        if (!prepared.IsValid() || !frameLease || !frameLease->IsValid() || !m_bFrameOpen ||
            frameLease->AssetLease->GetHandle() != prepared.MeshHandle)
        {
            return false;
        }

        auto entryIt = m_Entries.find(prepared.MeshHandle);
        if (entryIt == m_Entries.end())
        {
            return false;
        }

        Entry& entry = entryIt->second;
        if (entry.VertexBuffer != prepared.VertexBuffer || entry.IndexBuffer != prepared.IndexBuffer || entry.IndexCount != prepared.IndexCount ||
            entry.AssetLease.lock().get() != frameLease->AssetLease.get())
        {
            return false;
        }
        for (const auto& palettePtr : entry.PaletteUses)
        {
            PaletteUse& paletteUse = *palettePtr;
            if (paletteUse.Buffer == prepared.PaletteBuffer)
            {
                bool registered = false;
                for (const auto& weak : paletteUse.FrameLeases)
                {
                    registered = registered || weak.lock().get() == frameLease.get();
                }
                if (paletteUse.ComponentId != frameLease->ComponentId || prepared.ComponentId != paletteUse.ComponentId ||
                    (paletteUse.ComponentId == 0 && paletteUse.PreviousBuffer != prepared.PreviousPaletteBuffer) ||
                    (paletteUse.ComponentId != 0 && (!registered || paletteUse.Epoch != m_PreparationEpoch ||
                        prepared.PreparationEpoch != paletteUse.Epoch)) ||
                    (prepared.bUsesPreviousPalette ? (!paletteUse.PreviousBuffer || paletteUse.PreviousBuffer != prepared.PreviousPaletteBuffer)
                        : bool(prepared.PreviousPaletteBuffer)))
                {
                    return false;
                }
                if (!registered)
                {
                    paletteUse.FrameLeases.push_back(frameLease);
                }
                TrackFrameLease(entry, frameLease);
                for (const PendingUse& pending : m_PendingUses)
                {
                    if (pending.Handle == prepared.MeshHandle &&
                        pending.PaletteBuffer == prepared.PaletteBuffer)
                    {
                        return true;
                    }
                }
                PendingUse pending;
                pending.Handle = prepared.MeshHandle;
                pending.PaletteBuffer = prepared.PaletteBuffer;
                pending.FrameLease = frameLease;
                m_PendingUses.push_back(std::move(pending));
                return true;
            }
        }
        return false;
    }

    bool SkinnedMeshGpuStore::CommitSubmittedFrame(uint64_t submissionSerial)
    {
        if (!m_bFrameOpen || submissionSerial == 0)
        {
            AbortFrame();
            return false;
        }

        for (const PendingUse& pending : m_PendingUses)
        {
            auto entryIt = m_Entries.find(pending.Handle);
            if (entryIt == m_Entries.end())
            {
                continue;
            }
            Entry& entry = entryIt->second;
            if (submissionSerial > entry.LastSubmittedSerial)
            {
                entry.LastSubmittedSerial = submissionSerial;
            }
            for (const auto& palettePtr : entry.PaletteUses)
            {
                PaletteUse& paletteUse = *palettePtr;
                if (paletteUse.Buffer == pending.PaletteBuffer)
                {
                    if (submissionSerial > paletteUse.LastSubmittedSerial)
                    {
                        paletteUse.LastSubmittedSerial = submissionSerial;
                    }
                    break;
                }
            }
        }
        m_PendingUses.clear();
        m_ActivePalettes.clear();
        m_bFrameOpen = false;
        return true;
    }

    void SkinnedMeshGpuStore::AbortFrame()
    {
        m_PendingUses.clear();
        m_ActivePalettes.clear();
        m_bFrameOpen = false;
        CollectReleased();
    }

    bool SkinnedMeshGpuStore::GetLifetimeSnapshot(
        SkinnedMeshHandle handle,
        SkinnedMeshGpuLifetimeSnapshot& outSnapshot) const
    {
        auto entryIt = m_Entries.find(handle);
        if (entryIt == m_Entries.end())
        {
            return false;
        }

        const Entry& entry = entryIt->second;
        outSnapshot = SkinnedMeshGpuLifetimeSnapshot{};
        outSnapshot.MeshHandle = entry.Handle;
        outSnapshot.LastSubmittedSerial = entry.LastSubmittedSerial;
        outSnapshot.CompletedSubmissionSerial = m_CompletedSubmissionSerial;
        const auto assetLease = entry.AssetLease.lock();
        outSnapshot.bAssetLeaseActive = assetLease && assetLease->IsAssetLeaseActive();
        for (const auto& frameLease : entry.FrameLeases)
        {
            if (!frameLease.expired())
            {
                ++outSnapshot.FrameLeaseCount;
            }
        }
        return true;
    }

    bool SkinnedMeshGpuStore::IsResident(SkinnedMeshHandle handle) const
    {
        return m_Entries.find(handle) != m_Entries.end();
    }

    void SkinnedMeshGpuStore::CollectReleasedResources()
    {
        CollectReleased();
    }

    void SkinnedMeshGpuStore::ForceClearAfterWaitIdle()
    {
        m_PendingUses.clear();
        m_ActivePalettes.clear();
        m_Entries.clear();
        m_CompletedSubmissionSerial = 0;
        m_bFrameOpen = false;
    }

    SkinnedMeshGpuStore::Entry* SkinnedMeshGpuStore::FindOrUpload(
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease)
    {
        const Container::TSharedPtr<const SkinnedMeshAssetLease>& assetLease = frameLease->AssetLease;
        const SkinnedMeshHandle handle = assetLease->GetHandle();
        auto entryIt = m_Entries.find(handle);
        if (entryIt != m_Entries.end())
        {
            return entryIt->second.AssetLease.lock().get() == assetLease.get() ? &entryIt->second : nullptr;
        }

        const auto& vertices = assetLease->GetVertices();
        const auto& indices = assetLease->GetIndices();
        RHI::BufferDesc vertexDesc;
        vertexDesc.Size = static_cast<uint64_t>(vertices.size() * sizeof(SkinnedMeshVertex));
        vertexDesc.Usage = RHI::ResourceUsage::VertexBuffer |
                           RHI::ResourceUsage::StorageBuffer |
                           RHI::ResourceUsage::ShaderRead;
        vertexDesc.CPUAccessible = true;
        vertexDesc.DebugName = "SkinnedMeshVB";
        RHI::BufferPtr vertexBuffer = m_Device->CreateBuffer(vertexDesc);
        if (!vertexBuffer)
        {
            return nullptr;
        }
        vertexBuffer->Update(vertices.data(), vertexDesc.Size, 0);

        RHI::BufferDesc indexDesc;
        indexDesc.Size = static_cast<uint64_t>(indices.size() * sizeof(uint32_t));
        indexDesc.Usage = RHI::ResourceUsage::IndexBuffer;
        indexDesc.CPUAccessible = true;
        indexDesc.DebugName = "SkinnedMeshIB";
        RHI::BufferPtr indexBuffer = m_Device->CreateBuffer(indexDesc);
        if (!indexBuffer)
        {
            return nullptr;
        }
        indexBuffer->Update(indices.data(), indexDesc.Size, 0);

        Entry entry;
        entry.Handle = handle;
        entry.VertexBuffer = vertexBuffer;
        entry.IndexBuffer = indexBuffer;
        entry.IndexCount = static_cast<uint32_t>(indices.size());
        entry.AssetLease = assetLease;
        m_Entries[handle] = std::move(entry);
        return &m_Entries.find(handle)->second;
    }

    void SkinnedMeshGpuStore::TrackFrameLease(
        Entry& entry,
        const Container::TSharedPtr<const SkinnedMeshFrameLease>& frameLease)
    {
        for (const auto& trackedWeak : entry.FrameLeases)
        {
            if (trackedWeak.lock() == frameLease)
            {
                return;
            }
        }
        entry.FrameLeases.push_back(frameLease);
    }

    void SkinnedMeshGpuStore::CollectReleased()
    {
        for (auto entryIt = m_Entries.begin(); entryIt != m_Entries.end();)
        {
            Entry& entry = entryIt->second;
            for (auto leaseIt = entry.FrameLeases.begin(); leaseIt != entry.FrameLeases.end();)
            {
                if (leaseIt->expired())
                {
                    leaseIt = entry.FrameLeases.erase(leaseIt);
                }
                else
                {
                    ++leaseIt;
                }
            }

            for (auto paletteIt = entry.PaletteUses.begin(); paletteIt != entry.PaletteUses.end();)
            {
                auto& use = **paletteIt;
                bool hasLease = false;
                for (const auto& weak : use.FrameLeases)
                {
                    hasLease = hasLease || !weak.expired();
                }
                if (!hasLease && !(m_bFrameOpen && use.ComponentId != 0 && use.Epoch == m_PreparationEpoch) &&
                    IsSubmissionComplete(use.LastSubmittedSerial))
                {
                    paletteIt = entry.PaletteUses.erase(paletteIt);
                }
                else
                {
                    ++paletteIt;
                }
            }

            const auto assetLease = entry.AssetLease.lock();
            const bool bAssetLeaseActive = assetLease && assetLease->IsAssetLeaseActive();
            if (!bAssetLeaseActive && entry.FrameLeases.empty() && entry.PaletteUses.empty() &&
                IsSubmissionComplete(entry.LastSubmittedSerial))
            {
                entryIt = m_Entries.erase(entryIt);
            }
            else
            {
                ++entryIt;
            }
        }
    }

    bool SkinnedMeshGpuStore::IsSubmissionComplete(uint64_t submissionSerial) const
    {
        return submissionSerial == 0 || submissionSerial <= m_CompletedSubmissionSerial;
    }
} // namespace NorvesLib::Core::Rendering

#include "Rendering/SkinnedMeshGpuStore.h"

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "Logging/LogMacros.h"
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
        if (!m_PendingUses.empty())
        {
            AbortFrame();
        }
        if (completedSubmissionSerial > m_CompletedSubmissionSerial)
        {
            m_CompletedSubmissionSerial = completedSubmissionSerial;
        }
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

        Entry* entry = FindOrUpload(frameLease);
        if (!entry)
        {
            return false;
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

        // 直前のフレームの変換と骨ごとの位置の行列（velocity 用。法線の行列は要らない）。
        RHI::BufferPtr previousPaletteBuffer;
        if (previousBonePalette || previousWorldTransform)
        {
            const Container::VariableArray<Math::Matrix4x4>& previousPalette =
                previousBonePalette ? *previousBonePalette : bonePalette;
            Container::VariableArray<float> previousMatrices;
            previousMatrices.resize((1 + previousPalette.size()) * 16);
            Math::MatrixUtils::CopyToShaderData(previousWorldTransform ? *previousWorldTransform : worldTransform,
                                                previousMatrices.data());
            for (size_t matrixIndex = 0; matrixIndex < previousPalette.size(); ++matrixIndex)
            {
                Math::MatrixUtils::CopyToShaderData(previousPalette[matrixIndex],
                                                    previousMatrices.data() + (1 + matrixIndex) * 16);
            }
            RHI::BufferDesc previousDesc = paletteDesc;
            previousDesc.Size = static_cast<uint64_t>(previousMatrices.size() * sizeof(float));
            previousDesc.DebugName = "SkinnedPreviousPalette";
            previousPaletteBuffer = m_Device->CreateBuffer(previousDesc);
            if (!previousPaletteBuffer)
            {
                return false;
            }
            previousPaletteBuffer->Update(previousMatrices.data(), previousDesc.Size, 0);
        }

        TrackFrameLease(*entry, frameLease);
        PaletteUse paletteUse;
        paletteUse.Buffer = paletteBuffer;
        paletteUse.PreviousBuffer = previousPaletteBuffer;
        paletteUse.FrameLease = frameLease;
        entry->PaletteUses.push_back(paletteUse);

        outPrepared.MeshHandle = entry->Handle;
        outPrepared.VertexBuffer = entry->VertexBuffer;
        outPrepared.IndexBuffer = entry->IndexBuffer;
        outPrepared.PaletteBuffer = paletteBuffer;
        outPrepared.PreviousPaletteBuffer = previousPaletteBuffer;
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
        TrackFrameLease(entry, frameLease);
        for (PaletteUse& paletteUse : entry.PaletteUses)
        {
            if (paletteUse.Buffer == prepared.PaletteBuffer)
            {
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
            for (PaletteUse& paletteUse : entry.PaletteUses)
            {
                if (paletteUse.Buffer == pending.PaletteBuffer)
                {
                    paletteUse.LastSubmittedSerial = submissionSerial;
                    break;
                }
            }
        }
        m_PendingUses.clear();
        m_bFrameOpen = false;
        return true;
    }

    void SkinnedMeshGpuStore::AbortFrame()
    {
        m_PendingUses.clear();
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

    bool SkinnedMeshGpuStore::TryGetChunks(SkinnedMeshHandle handle,
                                           Container::VariableArray<MeshIndexChunk>& out) const
    {
        out.clear();
        const auto entryIt = m_Entries.find(handle);
        if (entryIt == m_Entries.end())
        {
            return false;
        }
        out = entryIt->second.Chunks;
        return true;
    }

    void SkinnedMeshGpuStore::CollectReleasedResources()
    {
        CollectReleased();
    }

    void SkinnedMeshGpuStore::ForceClearAfterWaitIdle()
    {
        m_PendingUses.clear();
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
            return &entryIt->second;
        }

        const auto& vertices = assetLease->GetVertices();
        const auto& indices = assetLease->GetIndices();
        RHI::BufferDesc vertexDesc;
        vertexDesc.Size = static_cast<uint64_t>(vertices.size() * sizeof(SkinnedMeshVertex));
        vertexDesc.Usage = RHI::ResourceUsage::VertexBuffer |
                           RHI::ResourceUsage::StorageBuffer |
                           RHI::ResourceUsage::ShaderRead |
                           RHI::ResourceUsage::BufferDeviceAddress;
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
        // 頂点と同じく、描画に加えて計算シェーダー（storage）とアドレス参照（BDA）から読めるようにする。
        // BDA が使えないデバイスでは RHI 側が用途を無視する。
        indexDesc.Usage = RHI::ResourceUsage::IndexBuffer |
                          RHI::ResourceUsage::StorageBuffer |
                          RHI::ResourceUsage::ShaderRead |
                          RHI::ResourceUsage::BufferDeviceAddress;
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
        // 区切りの位置を与えないので失敗しないが、戻り値は必ず確かめる
        if (!BuildMeshIndexChunks(entry.IndexCount, nullptr, 0, entry.Chunks))
        {
            NORVES_LOG_ERROR("SkinnedMeshGpuStore", "インデックスを塊に分けられませんでした");
            return nullptr;
        }
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
                if (paletteIt->FrameLease.expired() &&
                    IsSubmissionComplete(paletteIt->LastSubmittedSerial))
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

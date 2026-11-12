#include "Rendering/GrassFieldGpuWindow.h"

#include "Logging/LogMacros.h"
#include "Rendering/GrassFieldRenderData.h"
#include "Rendering/GrassGenerateReference.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        struct UploadCandidate
        {
            const GrassFieldChunkRenderData* Chunk = nullptr;
            double DistanceSquared = 0.0;
        };

        double DistanceToChunk(double position, int32_t coordinate, double chunkSize)
        {
            const double lower = static_cast<double>(coordinate) * chunkSize;
            const double upper = lower + chunkSize;
            if (position < lower)
            {
                return lower - position;
            }
            if (position > upper)
            {
                return position - upper;
            }
            return 0.0;
        }

        bool GetPixelBytes(uint32_t resolution, uint64_t& bytes)
        {
            const uint64_t pixels = static_cast<uint64_t>(resolution) * resolution;
            if (resolution == 0 || pixels > std::numeric_limits<uint64_t>::max() / 4u)
            {
                return false;
            }
            bytes = pixels * 4u;
            return true;
        }

        bool HasPixels(uint32_t offset, uint64_t bytes, size_t available)
        {
            return offset <= available && bytes <= available - offset;
        }
    } // namespace

    GrassFieldGpuWindow::GrassFieldGpuWindow(const Settings& settings)
        : m_Settings(settings)
    {
    }

    void GrassFieldGpuWindow::SetSettings(const Settings& settings)
    {
        if (m_Settings.WindowSize != settings.WindowSize)
        {
            m_bCurrentWindowValid = false;
        }
        m_Settings = settings;
    }

    bool GrassFieldGpuWindow::Update(RHI::IDevice* device, RHI::ICommandList* commandList,
                                    FrameResourceLane* frameResources, const GrassFieldRenderData& field,
                                    float cameraX, float cameraZ)
    {
        m_bCurrentWindowValid = false;
        const uint32_t windowSize = m_Settings.WindowSize;
        const uint64_t layerCount = static_cast<uint64_t>(windowSize) * windowSize;
        uint64_t layerABytes = 0;
        uint64_t layerBBytes = 0;
        if (!device || !commandList || windowSize == 0 ||
            layerCount > std::numeric_limits<uint32_t>::max() ||
            !GetPixelBytes(field.LayerAResolution, layerABytes) ||
            !GetPixelBytes(field.LayerBResolution, layerBBytes) ||
            layerABytes > std::numeric_limits<uint64_t>::max() - layerBBytes ||
            !std::isfinite(field.ChunkSizeMeters) || field.ChunkSizeMeters <= 0.0f ||
            !std::isfinite(cameraX) || !std::isfinite(cameraZ))
        {
            return false;
        }

        const double centerX = std::floor(static_cast<double>(cameraX) / field.ChunkSizeMeters);
        const double centerZ = std::floor(static_cast<double>(cameraZ) / field.ChunkSizeMeters);
        if (centerX < std::numeric_limits<int32_t>::min() || centerX > std::numeric_limits<int32_t>::max() ||
            centerZ < std::numeric_limits<int32_t>::min() || centerZ > std::numeric_limits<int32_t>::max())
        {
            return false;
        }

        m_CurrentField = &field;
        m_CurrentDevice = device;
        m_CurrentRevision = field.ContentRevision;
        m_CurrentLayerAResolution = field.LayerAResolution;
        m_CurrentLayerBResolution = field.LayerBResolution;
        m_MinChunkX = static_cast<int64_t>(centerX) - static_cast<int64_t>(windowSize / 2u);
        m_MinChunkZ = static_cast<int64_t>(centerZ) - static_cast<int64_t>(windowSize / 2u);

        // 提出の成否が分からなければ、未提出のコピーを常駐とみなさない。
        if (!frameResources || !frameResources->IsRecording())
        {
            return false;
        }
        if (m_PendingGeneration.IsSet())
        {
            const FrameSubmissionOutcome outcome = frameResources->GetSubmissionOutcome(m_PendingGeneration);
            if (outcome == FrameSubmissionOutcome::Aborted || outcome == FrameSubmissionOutcome::Unknown)
            {
                // 初回遷移も実行されていない可能性があるため、両テクスチャを作り直す。
                ReleaseTextures();
            }
            else if (outcome == FrameSubmissionOutcome::Submitted)
            {
                m_PendingGeneration = {};
            }
        }

        const FrameResourceGeneration generation = frameResources->GetGeneration();
        if (m_BudgetGeneration.SlotIndex != generation.SlotIndex ||
            m_BudgetGeneration.Generation != generation.Generation)
        {
            m_BudgetGeneration = generation;
            m_UploadsInFrame = 0;
        }

        const bool bRecreate = !m_LayerATexture || !m_LayerBTexture || m_ResourceDevice != device ||
            m_ResourceWindowSize != windowSize || m_ResourceLayerAResolution != field.LayerAResolution ||
            m_ResourceLayerBResolution != field.LayerBResolution;
        m_bCurrentWindowValid = true;

        Container::VariableArray<UploadCandidate> candidates;
        for (const GrassFieldChunkRenderData& chunk : field.Chunks)
        {
            if (static_cast<int64_t>(chunk.ChunkX) < m_MinChunkX ||
                static_cast<int64_t>(chunk.ChunkX) >= m_MinChunkX + windowSize ||
                static_cast<int64_t>(chunk.ChunkZ) < m_MinChunkZ ||
                static_cast<int64_t>(chunk.ChunkZ) >= m_MinChunkZ + windowSize)
            {
                continue;
            }
            const uint32_t index = GrassWindowLayerIndex(chunk.ChunkX, chunk.ChunkZ, windowSize);
            if (!bRecreate && m_Layers[index].bResident && m_Layers[index].Field == &field &&
                m_Layers[index].ContentRevision == field.ContentRevision &&
                m_Layers[index].ChunkX == chunk.ChunkX && m_Layers[index].ChunkZ == chunk.ChunkZ)
            {
                continue;
            }
            if (!HasPixels(chunk.LayerAByteOffset, layerABytes, field.LayerAPixels.size()) ||
                !HasPixels(chunk.LayerBByteOffset, layerBBytes, field.LayerBPixels.size()))
            {
                m_bCurrentWindowValid = false;
                return false;
            }
            const double dx = DistanceToChunk(cameraX, chunk.ChunkX, field.ChunkSizeMeters);
            const double dz = DistanceToChunk(cameraZ, chunk.ChunkZ, field.ChunkSizeMeters);
            candidates.push_back({&chunk, dx * dx + dz * dz});
        }

        std::sort(candidates.begin(), candidates.end(), [](const UploadCandidate& a, const UploadCandidate& b)
        {
            if (a.DistanceSquared != b.DistanceSquared)
            {
                return a.DistanceSquared < b.DistanceSquared;
            }
            if (a.Chunk->ChunkZ != b.Chunk->ChunkZ)
            {
                return a.Chunk->ChunkZ < b.Chunk->ChunkZ;
            }
            return a.Chunk->ChunkX < b.Chunk->ChunkX;
        });

        const uint32_t remainingBudget = m_UploadsInFrame >= m_Settings.MaxUploadsPerFrame
            ? 0u : m_Settings.MaxUploadsPerFrame - m_UploadsInFrame;
        size_t uploadCount = std::min(candidates.size(), static_cast<size_t>(remainingBudget));
        const uint64_t chunkBytes = layerABytes + layerBBytes;
        FrameUploadAllocation upload;
        if (uploadCount != 0)
        {
            // 口の実際の上限を使い、入り切らない分は次のフレームへ残す。
            uint64_t maxBytes = frameResources->GetUploadArena().GetLimits().MaxStorageSize;
            if (maxBytes == 0 || maxBytes > FrameUploadMaxDescriptorValue)
            {
                maxBytes = FrameUploadMaxDescriptorValue;
            }
            maxBytes = std::min<uint64_t>(maxBytes, std::numeric_limits<size_t>::max());
            const uint64_t maxChunks = maxBytes / chunkBytes;
            if (maxChunks == 0)
            {
                SetBacklogged(true);
                return false;
            }
            uploadCount = static_cast<size_t>(std::min<uint64_t>(uploadCount, maxChunks));
            upload = frameResources->AllocateUpload(chunkBytes * uploadCount, FrameUploadUsage::Storage);
            if (!upload.IsValid())
            {
                SetBacklogged(true);
                return false;
            }

            uint8_t* destination = static_cast<uint8_t*>(upload.CPUAddress);
            for (size_t i = 0; i < uploadCount; ++i)
            {
                const GrassFieldChunkRenderData& chunk = *candidates[i].Chunk;
                // 行は +Z、行内は +X。CPU の並びをそのままテクスチャへ写す。
                std::memcpy(destination, field.LayerAPixels.data() + chunk.LayerAByteOffset,
                            static_cast<size_t>(layerABytes));
                std::memcpy(destination + layerABytes, field.LayerBPixels.data() + chunk.LayerBByteOffset,
                            static_cast<size_t>(layerBBytes));
                destination += chunkBytes;
            }
        }

        if (bRecreate)
        {
            // 1 層の窓でも sampler2DArray が使えるよう、未使用の 2 層目を確保する。
            RHI::TextureDesc desc;
            desc.Width = field.LayerAResolution;
            desc.Height = field.LayerAResolution;
            desc.ArraySize = static_cast<uint32_t>(std::max<uint64_t>(layerCount, 2u));
            desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            desc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
            desc.Dimension = RHI::TextureDimension::Texture2D;
            desc.DebugName = "GrassField.LayerA";
            RHI::TexturePtr layerA;
            RHI::TexturePtr layerB;
            Container::VariableArray<LayerRecord> layers;
            try
            {
                layerA = device->CreateTexture(desc);
                desc.Width = field.LayerBResolution;
                desc.Height = field.LayerBResolution;
                desc.DebugName = "GrassField.LayerB";
                layerB = device->CreateTexture(desc);
                if (!layerA || !layerB)
                {
                    m_bCurrentWindowValid = false;
                    return false;
                }
                layers.resize(static_cast<size_t>(layerCount));
            }
            catch (const std::exception&)
            {
                m_bCurrentWindowValid = false;
                return false;
            }
            m_LayerATexture = std::move(layerA);
            m_LayerBTexture = std::move(layerB);
            m_Layers = std::move(layers);
            m_ResourceDevice = device;
            m_ResourceWindowSize = windowSize;
            m_ResourceLayerAResolution = field.LayerAResolution;
            m_ResourceLayerBResolution = field.LayerBResolution;
#if NORVES_BUILD_DEVELOPMENT
            NORVES_LOG_INFO("GrassFieldGpuWindow", "草の場の配列テクスチャを作成しました 窓=%u 解像度=%u/%u",
                            windowSize, field.LayerAResolution, field.LayerBResolution);
#endif
        }

        if (bRecreate || uploadCount != 0)
        {
            // 途中でフレームが中止されたら、次回は状態と常駐の記録を作り直す。
            m_PendingGeneration = generation;
            const RHI::ResourceState before = bRecreate ? RHI::ResourceState::Undefined : RHI::ResourceState::ShaderResource;
            const RHI::ResourceState during = uploadCount != 0 ? RHI::ResourceState::CopyDest : RHI::ResourceState::ShaderResource;
            commandList->TextureBarrier(m_LayerATexture, before, during);
            commandList->TextureBarrier(m_LayerBTexture, before, during);
            if (uploadCount != 0)
            {
                for (size_t i = 0; i < uploadCount; ++i)
                {
                    const GrassFieldChunkRenderData& chunk = *candidates[i].Chunk;
                    const uint32_t index = GrassWindowLayerIndex(chunk.ChunkX, chunk.ChunkZ, windowSize);
                    const uint64_t offset = upload.Offset + chunkBytes * i;
                    commandList->CopyBufferToTexture(upload.Buffer, m_LayerATexture, field.LayerAResolution,
                                                     field.LayerAResolution, offset, 0u, index);
                    commandList->CopyBufferToTexture(upload.Buffer, m_LayerBTexture, field.LayerBResolution,
                                                     field.LayerBResolution, offset + layerABytes, 0u, index);
                    m_Layers[index] = {&field, field.ContentRevision, chunk.ChunkX, chunk.ChunkZ, true};
                }
                commandList->TextureBarrier(m_LayerATexture, RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
                commandList->TextureBarrier(m_LayerBTexture, RHI::ResourceState::CopyDest, RHI::ResourceState::ShaderResource);
                m_UploadsInFrame += static_cast<uint32_t>(uploadCount);
            }
        }
        SetBacklogged(candidates.size() > uploadCount);
        return true;
    }

    bool GrassFieldGpuWindow::IsResident(int32_t cx, int32_t cz) const
    {
        if (!m_bCurrentWindowValid || !m_LayerATexture || !m_LayerBTexture ||
            m_CurrentDevice != m_ResourceDevice || m_Settings.WindowSize != m_ResourceWindowSize ||
            m_CurrentLayerAResolution != m_ResourceLayerAResolution ||
            m_CurrentLayerBResolution != m_ResourceLayerBResolution ||
            static_cast<int64_t>(cx) < m_MinChunkX ||
            static_cast<int64_t>(cx) >= m_MinChunkX + m_Settings.WindowSize ||
            static_cast<int64_t>(cz) < m_MinChunkZ ||
            static_cast<int64_t>(cz) >= m_MinChunkZ + m_Settings.WindowSize)
        {
            return false;
        }
        const LayerRecord& record = m_Layers[GrassWindowLayerIndex(cx, cz, m_Settings.WindowSize)];
        return record.bResident && record.Field == m_CurrentField && record.ContentRevision == m_CurrentRevision &&
            record.ChunkX == cx && record.ChunkZ == cz;
    }

    void GrassFieldGpuWindow::ReleaseTextures()
    {
        m_LayerATexture.reset();
        m_LayerBTexture.reset();
        m_Layers.clear();
        m_ResourceDevice = nullptr;
        m_ResourceWindowSize = 0;
        m_ResourceLayerAResolution = 0;
        m_ResourceLayerBResolution = 0;
        m_PendingGeneration = {};
    }

    void GrassFieldGpuWindow::SetBacklogged(bool bBacklogged)
    {
        if (m_bBacklogged == bBacklogged)
        {
            return;
        }
        m_bBacklogged = bBacklogged;
#if NORVES_BUILD_DEVELOPMENT
        if (bBacklogged)
        {
            NORVES_LOG_INFO("GrassFieldGpuWindow", "草の場に未送信のチャンクがあります");
        }
        else
        {
            NORVES_LOG_INFO("GrassFieldGpuWindow", "草の場のチャンクの転送が追いつきました");
        }
#endif
    }

    void GrassFieldGpuWindow::Reset()
    {
        ReleaseTextures();
        m_CurrentField = nullptr;
        m_CurrentDevice = nullptr;
        m_bCurrentWindowValid = false;
        m_bBacklogged = false;
    }
} // namespace NorvesLib::Core::Rendering

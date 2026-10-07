#pragma once

// ジオメトリの区画へのアップロード（ステージングのリング → プールの塊）を、実デバイスなしで完了させる試験用の補助。
// コピーはバッファの Map / Update で行うので、試験用の偽バッファが書き込みを受ける。
// 実際の GPU のコピーとバリアの記録は GpuUploadRingVulkanTest が確かめる。

#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"

#include <cstdint>

namespace NorvesLib::Test::GeometryUpload
{
    // バッファのコピー（CopyBuffer）だけを実行し、他の命令は何もしないコマンドリスト
    class CopyingCommandList final : public NorvesLib::RHI::ICommandList
    {
    public:
        uint32_t CopyBufferCount = 0;
        uint64_t CopiedBytes = 0;

        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(NorvesLib::RHI::RenderPassPtr, NorvesLib::RHI::FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const NorvesLib::RHI::Viewport &) override {}
        void SetScissor(const NorvesLib::RHI::ScissorRect &) override {}
        void SetPipeline(NorvesLib::RHI::PipelinePtr) override {}
        void SetVertexBuffer(NorvesLib::RHI::BufferPtr, uint64_t = 0, uint32_t = 0) override {}
        void SetIndexBuffer(NorvesLib::RHI::BufferPtr, uint64_t = 0, NorvesLib::RHI::IndexType = NorvesLib::RHI::IndexType::Uint32) override {}
        void SetConstantBuffer(NorvesLib::RHI::BufferPtr, uint32_t, NorvesLib::RHI::ShaderStage) override {}
        void SetTexture(NorvesLib::RHI::TexturePtr, uint32_t, NorvesLib::RHI::ShaderStage) override {}
        void SetSampler(NorvesLib::RHI::SamplerPtr, uint32_t, NorvesLib::RHI::ShaderStage) override {}
        void SetDescriptorSet(NorvesLib::RHI::DescriptorSetPtr, uint32_t = 0) override {}
        void DrawIndexed(uint32_t, uint32_t = 0, int32_t = 0) override {}
        void Draw(uint32_t, uint32_t = 0) override {}
        void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
        void DrawInstanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
        void DrawIndexedIndirect(NorvesLib::RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(NorvesLib::RHI::BufferPtr, uint64_t, NorvesLib::RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void FillBuffer(NorvesLib::RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(NorvesLib::RHI::BufferPtr src, NorvesLib::RHI::BufferPtr dst, uint64_t size = 0,
                        uint64_t srcOffset = 0, uint64_t dstOffset = 0) override
        {
            if (!src || !dst)
            {
                return;
            }
            const uint64_t bytes = size != 0 ? size : src->GetSize();
            const uint8_t *srcData = static_cast<const uint8_t *>(src->Map(0, 0));
            if (srcData != nullptr)
            {
                dst->Update(srcData + srcOffset, bytes, dstOffset);
                ++CopyBufferCount;
                CopiedBytes += bytes;
            }
            src->Unmap();
        }
        void CopyBufferToTexture(NorvesLib::RHI::BufferPtr, NorvesLib::RHI::TexturePtr, uint32_t, uint32_t, uint64_t = 0,
                                 uint32_t = 0, uint32_t = 0) override {}
        void CopyTextureToBuffer(NorvesLib::RHI::TexturePtr, NorvesLib::RHI::BufferPtr, uint32_t, uint32_t, uint64_t = 0,
                                 uint32_t = 0, uint32_t = 0) override {}
        void CopyTexture(NorvesLib::RHI::TexturePtr, NorvesLib::RHI::TexturePtr, uint32_t, uint32_t,
                         uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void GenerateMipmaps(NorvesLib::RHI::TexturePtr) override {}
        void BufferBarrier(NorvesLib::RHI::BufferPtr, NorvesLib::RHI::ResourceState, NorvesLib::RHI::ResourceState,
                           uint64_t = 0, uint64_t = 0) override {}
        void TextureBarrier(NorvesLib::RHI::TexturePtr, NorvesLib::RHI::ResourceState, NorvesLib::RHI::ResourceState,
                            uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override {}
    };

    // 描画フレームを1つ進める: 完了済みの serial を渡し、書き込み待ちの区画の中身をリングへ積んで記録し、提出して、
    // その提出が直ちに完了したことにする。
    inline void StepFrame(NorvesLib::Core::Rendering::RenderResources &resources, CopyingCommandList &commandList,
                          uint64_t &serial)
    {
        resources.BeginRetireFrame(serial);
        resources.RecordTileUploads(commandList);
        ++serial;
        resources.CommitRetireFrame(serial);
        resources.BeginRetireFrame(serial);
    }

    // ジオメトリの書き込みが全て終わる（GPU から読める状態になる）まで、フレームを進める。
    // @return maxFrames のうちに終われば true。進めたフレーム数は outFrames へ（null でもよい）
    inline bool DrainGeometryUploads(NorvesLib::Core::Rendering::RenderResources &resources,
                                     uint32_t maxFrames = 512,
                                     uint32_t *outFrames = nullptr,
                                     CopyingCommandList *outCommandList = nullptr)
    {
        CopyingCommandList localCommandList;
        CopyingCommandList &commandList = outCommandList != nullptr ? *outCommandList : localCommandList;
        uint64_t serial = 0;
        uint32_t frames = 0;
        while (frames < maxFrames && resources.MegaGeometry().HasPendingGpuUploads())
        {
            StepFrame(resources, commandList, serial);
            ++frames;
        }
        if (outFrames != nullptr)
        {
            *outFrames = frames;
        }
        return !resources.MegaGeometry().HasPendingGpuUploads();
    }
} // namespace NorvesLib::Test::GeometryUpload

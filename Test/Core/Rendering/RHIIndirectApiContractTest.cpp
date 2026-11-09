// 間接描画 API の既定実装と、間接コマンドのサイズ・フィールド配置の契約を CPU で確かめる。
#include "RHI/ICommandList.h"
#include "RHI/IndirectCommands.h"

#include <cstddef>
#include <iostream>

namespace
{
    using namespace NorvesLib::RHI;

    static_assert(sizeof(DrawIndirectCommand) == 16);
    static_assert(offsetof(DrawIndirectCommand, VertexCount) == 0);
    static_assert(offsetof(DrawIndirectCommand, InstanceCount) == 4);
    static_assert(offsetof(DrawIndirectCommand, FirstVertex) == 8);
    static_assert(offsetof(DrawIndirectCommand, FirstInstance) == 12);
    static_assert(sizeof(DispatchIndirectCommand) == 12);
    static_assert(offsetof(DispatchIndirectCommand, X) == 0);
    static_assert(offsetof(DispatchIndirectCommand, Y) == 4);
    static_assert(offsetof(DispatchIndirectCommand, Z) == 8);

    class UnsupportedCommandList final : public ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool) override {}
        void BeginRenderPass(RenderPassPtr, FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const Viewport&) override {}
        void SetScissor(const ScissorRect&) override {}
        void SetPipeline(PipelinePtr) override {}
        void SetVertexBuffer(BufferPtr, uint64_t, uint32_t) override {}
        void SetIndexBuffer(BufferPtr, uint64_t, IndexType) override {}
        void SetConstantBuffer(BufferPtr, uint32_t, ShaderStage) override {}
        void SetTexture(TexturePtr, uint32_t, ShaderStage) override {}
        void SetSampler(SamplerPtr, uint32_t, ShaderStage) override {}
        void SetDescriptorSet(DescriptorSetPtr, uint32_t) override {}
        void DrawIndexed(uint32_t, uint32_t, int32_t) override {}
        void Draw(uint32_t, uint32_t) override {}
        void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
        void DrawInstanced(uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirect(BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(BufferPtr, uint64_t, BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void FillBuffer(BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(BufferPtr, BufferPtr, uint64_t, uint64_t, uint64_t) override {}
        void CopyBufferToTexture(BufferPtr, TexturePtr, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t) override {}
        void CopyTextureToBuffer(TexturePtr, BufferPtr, uint32_t, uint32_t, uint64_t, uint32_t, uint32_t) override {}
        void CopyTexture(TexturePtr, TexturePtr, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) override {}
        void GenerateMipmaps(TexturePtr) override {}
        void BufferBarrier(BufferPtr, ResourceState, ResourceState, uint64_t, uint64_t) override {}
        void TextureBarrier(TexturePtr, ResourceState, ResourceState, uint32_t, uint32_t, uint32_t, uint32_t) override {}
    };
}

int main()
{
    UnsupportedCommandList commandList;
    NorvesLib::RHI::BufferPtr buffer;
    int result = 0;
    if (commandList.DrawIndirect(buffer, 0, 1, 16))
    {
        std::cerr << "契約違反: DrawIndirect の既定実装は false を返す必要があります\n";
        result = 1;
    }
    if (commandList.DrawIndirectCount(buffer, 0, buffer, 0, 4, 16))
    {
        std::cerr << "契約違反: DrawIndirectCount の既定実装は false を返す必要があります\n";
        result = 1;
    }
    return result;
}

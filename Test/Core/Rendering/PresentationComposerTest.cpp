#include "Rendering/PresentationComposer.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
namespace RHI = NorvesLib::RHI;

namespace
{
    class FakeTexture final : public RHI::ITexture
    {
    public:
        explicit FakeTexture(const char* name)
        {
            m_Desc = RHI::TextureDesc::RenderTarget(64, 32, RHI::Format::R8G8B8A8_UNORM, name);
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override { return m_Desc.Usage; }
        bool IsCubemap() const override { return m_Desc.IsCubemap; }
        void Update(const void* data,
                    uint32_t rowPitch,
                    uint32_t slicePitch,
                    uint32_t mipLevel = 0,
                    uint32_t arrayIndex = 0) override
        {
            (void)data;
            (void)rowPitch;
            (void)slicePitch;
            (void)mipLevel;
            (void)arrayIndex;
        }

    private:
        RHI::TextureDesc m_Desc;
    };

    class FakeRenderPass final : public RHI::IRenderPass
    {
    public:
        uint32_t GetColorAttachmentCount() const override { return 1; }
        bool HasDepthStencilAttachment() const override { return false; }
        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index == 0 ? RHI::Format::R8G8B8A8_UNORM : RHI::Format::UNKNOWN;
        }
        RHI::Format GetDepthStencilFormat() const override { return RHI::Format::UNKNOWN; }
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(RHI::RenderPassPtr renderPass)
            : m_RenderPass(renderPass)
        {
        }

        uint32_t GetWidth() const override { return 64; }
        uint32_t GetHeight() const override { return 32; }
        RHI::RenderPassPtr GetRenderPass() const override { return m_RenderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            (void)index;
            return nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return nullptr; }
        uint32_t GetColorAttachmentCount() const override { return 1; }
        bool HasDepthStencilAttachment() const override { return false; }

    private:
        RHI::RenderPassPtr m_RenderPass;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        RHI::PipelineType GetPipelineType() const override { return RHI::PipelineType::Graphics; }
        uint32_t GetBindPointCount() const override { return 1; }
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        RHI::FilterMode GetFilterMin() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMag() const override { return RHI::FilterMode::Linear; }
        RHI::FilterMode GetFilterMip() const override { return RHI::FilterMode::Linear; }
        RHI::TextureAddressMode GetAddressModeU() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeV() const override { return RHI::TextureAddressMode::Clamp; }
        RHI::TextureAddressMode GetAddressModeW() const override { return RHI::TextureAddressMode::Clamp; }
        uint32_t GetMaxAnisotropy() const override { return 1; }
        RHI::CompareFunc GetCompareFunc() const override { return RHI::CompareFunc::Never; }
    };

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t offset, uint32_t size) override
        {
            (void)binding;
            (void)buffer;
            (void)offset;
            (void)size;
        }

        void BindTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            if (binding == 0)
            {
                Binding0Texture = texture;
            }
            ++BindTextureCount;
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            (void)binding;
            (void)sampler;
            ++BindSamplerCount;
        }

        void BindStorageBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t offset, uint32_t size) override
        {
            (void)binding;
            (void)buffer;
            (void)offset;
            (void)size;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            (void)binding;
            (void)texture;
        }

        void BindStorageTexture(uint32_t binding, RHI::TexturePtr texture, uint32_t mipLevel) override
        {
            (void)binding;
            (void)texture;
            (void)mipLevel;
        }

        void Update() override { ++UpdateCount; }

        RHI::TexturePtr Binding0Texture;
        uint32_t BindTextureCount = 0;
        uint32_t BindSamplerCount = 0;
        uint32_t UpdateCount = 0;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            assert(renderPass);
            assert(framebuffer);
            ++BeginRenderPassCount;
        }
        void EndRenderPass() override { ++EndRenderPassCount; }
        void SetViewport(const RHI::Viewport& viewport) override { (void)viewport; ++SetViewportCount; }
        void SetScissor(const RHI::ScissorRect& scissor) override { (void)scissor; ++SetScissorCount; }
        void SetPipeline(RHI::PipelinePtr pipeline) override { (void)pipeline; ++SetPipelineCount; }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)buffer;
            (void)offset;
            (void)slot;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0) override
        {
            (void)buffer;
            (void)offset;
        }
        void SetConstantBuffer(RHI::BufferPtr buffer, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)buffer;
            (void)slot;
            (void)stage;
        }
        void SetTexture(RHI::TexturePtr texture, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)texture;
            (void)slot;
            (void)stage;
        }
        void SetSampler(RHI::SamplerPtr sampler, uint32_t slot, RHI::ShaderStage stage) override
        {
            (void)sampler;
            (void)slot;
            (void)stage;
        }
        void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
        {
            (void)descriptorSet;
            (void)slot;
            ++SetDescriptorSetCount;
        }
        void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) override
        {
            (void)indexCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            ++DrawCallCount;
        }
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)vertexCount;
            (void)startVertexLocation;
            ++DrawCallCount;
        }
        void DrawIndexedInstanced(uint32_t indexCount,
                                  uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0,
                                  int32_t baseVertexLocation = 0,
                                  uint32_t startInstanceLocation = 0) override
        {
            (void)indexCount;
            (void)instanceCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            (void)startInstanceLocation;
            ++DrawCallCount;
        }
        void DrawInstanced(uint32_t vertexCount,
                           uint32_t instanceCount,
                           uint32_t startVertexLocation = 0,
                           uint32_t startInstanceLocation = 0) override
        {
            (void)vertexCount;
            (void)instanceCount;
            (void)startVertexLocation;
            (void)startInstanceLocation;
            ++DrawCallCount;
        }
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer,
                                 uint64_t offset,
                                 uint32_t drawCount,
                                 uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)offset;
            (void)drawCount;
            (void)stride;
            ++DrawCallCount;
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer,
                                      uint64_t indirectOffset,
                                      RHI::BufferPtr countBuffer,
                                      uint64_t countOffset,
                                      uint32_t maxDrawCount,
                                      uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)indirectOffset;
            (void)countBuffer;
            (void)countOffset;
            (void)maxDrawCount;
            (void)stride;
            ++DrawCallCount;
        }
        void FillBuffer(RHI::BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override
        {
            (void)buffer;
            (void)offset;
            (void)size;
            (void)value;
        }
        void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override
        {
            (void)threadGroupCountX;
            (void)threadGroupCountY;
            (void)threadGroupCountZ;
        }
        void CopyBuffer(RHI::BufferPtr src, RHI::BufferPtr dst, uint64_t size = 0, uint64_t srcOffset = 0, uint64_t dstOffset = 0) override
        {
            (void)src;
            (void)dst;
            (void)size;
            (void)srcOffset;
            (void)dstOffset;
        }
        void CopyBufferToTexture(RHI::BufferPtr src,
                                 RHI::TexturePtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTextureToBuffer(RHI::TexturePtr src,
                                 RHI::BufferPtr dst,
                                 uint32_t width,
                                 uint32_t height,
                                 uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0,
                                 uint32_t arrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)bufferOffset;
            (void)mipLevel;
            (void)arrayIndex;
        }
        void CopyTexture(RHI::TexturePtr src,
                         RHI::TexturePtr dst,
                         uint32_t width,
                         uint32_t height,
                         uint32_t srcMipLevel = 0,
                         uint32_t srcArrayIndex = 0,
                         uint32_t dstMipLevel = 0,
                         uint32_t dstArrayIndex = 0) override
        {
            (void)src;
            (void)dst;
            (void)width;
            (void)height;
            (void)srcMipLevel;
            (void)srcArrayIndex;
            (void)dstMipLevel;
            (void)dstArrayIndex;
        }
        void GenerateMipmaps(RHI::TexturePtr texture) override { (void)texture; }
        void BufferBarrier(RHI::BufferPtr buffer,
                           RHI::ResourceState beforeState,
                           RHI::ResourceState afterState,
                           uint64_t offset = 0,
                           uint64_t size = 0) override
        {
            (void)buffer;
            (void)beforeState;
            (void)afterState;
            (void)offset;
            (void)size;
        }
        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            (void)texture;
            (void)beforeState;
            (void)afterState;
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
        }

        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
        uint32_t SetViewportCount = 0;
        uint32_t SetScissorCount = 0;
        uint32_t SetPipelineCount = 0;
        uint32_t SetDescriptorSetCount = 0;
        uint32_t DrawCallCount = 0;
    };

    struct ComposerFixture
    {
        RHI::RenderPassPtr RenderPass = RHI::MakeShared<FakeRenderPass>();
        RHI::FramebufferPtr Framebuffer = RHI::MakeShared<FakeFramebuffer>(RenderPass);
        RHI::PipelinePtr BlitPipeline = RHI::MakeShared<FakePipeline>();
        RHI::DescriptorSetPtr BlitDescriptorSet = RHI::MakeShared<FakeDescriptorSet>();
        RHI::SamplerPtr BlitSampler = RHI::MakeShared<FakeSampler>();
        SceneRenderer Renderer;
        PresentationComposer Composer;
        FakeCommandList CommandList;
        SharedResourceRegistry SharedResources;
        ViewRenderContext Context;

        ComposerFixture()
        {
            Context.SharedResources = &SharedResources;
            Context.RenderWidth = 64;
            Context.RenderHeight = 32;
            Context.ScreenWidth = 64;
            Context.ScreenHeight = 32;
        }

        bool Compose()
        {
            PresentationComposeRequest request;
            request.Context = &Context;
            request.Renderer = &Renderer;
            request.CommandList = &CommandList;
            request.ClearRenderPass = RenderPass;
            request.LoadRenderPass = RenderPass;
            request.ClearFramebuffer = Framebuffer;
            request.LoadFramebuffer = Framebuffer;
            request.BlitPipeline = BlitPipeline;
            request.BlitDescriptorSet = BlitDescriptorSet;
            request.BlitSampler = BlitSampler;
            request.bClearPresentation = true;
            return Composer.Compose(request);
        }

        FakeDescriptorSet* GetDescriptorSet() const
        {
            return static_cast<FakeDescriptorSet*>(BlitDescriptorSet.get());
        }
    };

    void AddOutput(RenderGraphExecutionResult& result, NorvesLib::Core::Identity name, RHI::TexturePtr texture)
    {
        RGTextureOutput output;
        output.Name = name;
        output.Texture = texture;
        result.TextureOutputs[name] = output;
    }

    void TestGraphPresentationColorOverridesRegistry()
    {
        ComposerFixture fixture;
        RHI::TexturePtr graphTexture = RHI::MakeShared<FakeTexture>("GraphPresentationColor");
        RHI::TexturePtr registryTexture = RHI::MakeShared<FakeTexture>("RegistryPresentationColor");
        fixture.SharedResources.RegisterTexturePtr(RenderGraphResourceNames::PresentationColor, registryTexture);

        RenderGraphExecutionResult result;
        result.bSuccess = true;
        AddOutput(result, RenderGraphResourceNames::PresentationColor, graphTexture);
        fixture.Context.CurrentGraphExecutionResult = &result;

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture.get() == graphTexture.get());
        assert(fixture.CommandList.SetDescriptorSetCount == 1);
        assert(fixture.CommandList.DrawCallCount == 1);
        std::cout << "TestGraphPresentationColorOverridesRegistry passed\n";
    }

    void TestGraphToneMappedColorUsedWhenPresentationMissing()
    {
        ComposerFixture fixture;
        RHI::TexturePtr graphTexture = RHI::MakeShared<FakeTexture>("GraphToneMappedColor");

        RenderGraphExecutionResult result;
        result.bSuccess = true;
        AddOutput(result, RenderGraphResourceNames::ToneMappedColor, graphTexture);
        fixture.Context.CurrentGraphExecutionResult = &result;

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture.get() == graphTexture.get());
        std::cout << "TestGraphToneMappedColorUsedWhenPresentationMissing passed\n";
    }

    void TestNullGraphResultFallsBackToRegistryPresentation()
    {
        ComposerFixture fixture;
        RHI::TexturePtr registryTexture = RHI::MakeShared<FakeTexture>("RegistryPresentationColor");
        fixture.SharedResources.RegisterTexturePtr(RenderGraphResourceNames::PresentationColor, registryTexture);

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture.get() == registryTexture.get());
        std::cout << "TestNullGraphResultFallsBackToRegistryPresentation passed\n";
    }

    void TestFailedGraphResultFallsBackToRegistryPresentation()
    {
        ComposerFixture fixture;
        RHI::TexturePtr graphTexture = RHI::MakeShared<FakeTexture>("GraphPresentationColor");
        RHI::TexturePtr registryTexture = RHI::MakeShared<FakeTexture>("RegistryPresentationColor");
        fixture.SharedResources.RegisterTexturePtr(RenderGraphResourceNames::PresentationColor, registryTexture);

        RenderGraphExecutionResult result;
        result.bSuccess = false;
        AddOutput(result, RenderGraphResourceNames::PresentationColor, graphTexture);
        fixture.Context.CurrentGraphExecutionResult = &result;

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture.get() == registryTexture.get());
        std::cout << "TestFailedGraphResultFallsBackToRegistryPresentation passed\n";
    }

    void TestMissingGraphOutputsFallBackToRegistryPresentation()
    {
        ComposerFixture fixture;
        RHI::TexturePtr registryTexture = RHI::MakeShared<FakeTexture>("RegistryPresentationColor");
        fixture.SharedResources.RegisterTexturePtr(RenderGraphResourceNames::PresentationColor, registryTexture);

        RenderGraphExecutionResult result;
        result.bSuccess = true;
        fixture.Context.CurrentGraphExecutionResult = &result;

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture.get() == registryTexture.get());
        std::cout << "TestMissingGraphOutputsFallBackToRegistryPresentation passed\n";
    }

    void TestMissingAllPresentationInputsUsesFullscreenFallback()
    {
        ComposerFixture fixture;

        assert(fixture.Compose());
        assert(fixture.GetDescriptorSet()->Binding0Texture == nullptr);
        assert(fixture.GetDescriptorSet()->BindTextureCount == 0);
        assert(fixture.CommandList.BeginRenderPassCount == 1);
        assert(fixture.CommandList.EndRenderPassCount == 1);
        assert(fixture.CommandList.SetPipelineCount == 0);
        assert(fixture.CommandList.SetDescriptorSetCount == 0);
        assert(fixture.CommandList.DrawCallCount == 0);
        std::cout << "TestMissingAllPresentationInputsUsesFullscreenFallback passed\n";
    }
} // namespace

int main()
{
    std::cout << "PresentationComposerTest start\n";

    TestGraphPresentationColorOverridesRegistry();
    TestGraphToneMappedColorUsedWhenPresentationMissing();
    TestNullGraphResultFallsBackToRegistryPresentation();
    TestFailedGraphResultFallsBackToRegistryPresentation();
    TestMissingGraphOutputsFallBackToRegistryPresentation();
    TestMissingAllPresentationInputsUsesFullscreenFallback();

    std::cout << "PresentationComposerTest passed\n";
    return 0;
}

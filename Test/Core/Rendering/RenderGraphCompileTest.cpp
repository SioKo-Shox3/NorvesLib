#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/HiZPyramidPass.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/BloomPass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/LightingPass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/SSRPass.h"
#include "Rendering/ToneMappingPass.h"
#if __has_include("Rendering/VignettePass.h")
#include "Rendering/VignettePass.h"
#define NORVES_HAS_VIGNETTE_PASS 1
#else
#define NORVES_HAS_VIGNETTE_PASS 0
#endif
#include "Rendering/UpscalePass.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Test/Core/Rendering/GeometryUploadTestSupport.h"
#include "Container/PointerTypes.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/TransientResourcePool.h"
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <utility>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace RHI = NorvesLib::RHI;

namespace
{
    // ShaderManagerはGLSLの#include展開のためにファイルを読むため、実在するシェーダー置き場を渡す。
    // compilerはテスト用の偽物なので、読んだ内容から実際のバイトコードは作らない。
    constexpr const char* TestShaderDirectory = NORVES_SOURCE_ROOT "/Assets/Shaders";

    void ConfigureAssertOutput()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }

    struct BarrierEvent
    {
        RGBarrierKind Kind = RGBarrierKind::Texture;
        RHI::ITexture* Texture = nullptr;
        RHI::IBuffer* Buffer = nullptr;
        RHI::ResourceState BeforeState = RHI::ResourceState::Undefined;
        RHI::ResourceState AfterState = RHI::ResourceState::Undefined;
        uint64_t BufferSize = 0;
    };

    enum class FakeRenderEvent
    {
        LightSsboUpdate,
        BindStorageBuffer5,
        DescriptorSetUpdate,
        CommandSetDescriptorSet,
        Draw
    };

    struct FakeBufferLifetimeTracker
    {
        bool bDestroyed = false;
    };

    struct BufferCreationRecord
    {
        RHI::BufferDesc Desc;
        RHI::TSharedPtr<FakeBufferLifetimeTracker> Tracker;
    };

    Container::VariableArray<FakeRenderEvent> GRenderEvents;
    // MegaGeometryPass が毎フレーム書くインスタンスの表（"MegaGeometry_InstanceTable"）の更新の記録（更新ごとの中身）
    Container::VariableArray<Container::VariableArray<uint8_t>> GMegaInstanceTableUpdates;
    Container::VariableArray<uint8_t> GLastDescriptorBinding4UpdateBytes;
    Container::VariableArray<uint8_t> GLastDescriptorBinding5UpdateBytes;
    RHI::IBuffer* GLastDescriptorBinding4Buffer = nullptr;
    RHI::IBuffer* GLastDescriptorBinding5Buffer = nullptr;
    uint32_t GLastDescriptorBinding4Offset = 0;
    uint32_t GLastDescriptorBinding4Size = 0;
    uint32_t GLastDescriptorBinding5Offset = 0;
    uint32_t GLastDescriptorBinding5Size = 0;

    bool HasUsage(RHI::ResourceUsage usage, RHI::ResourceUsage flag)
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
    }

    bool IsDebugName(const char* actual, const char* expected)
    {
        return actual != nullptr && std::strcmp(actual, expected) == 0;
    }

    void PushRenderEvent(FakeRenderEvent event)
    {
        GRenderEvents.push_back(event);
    }

    uint32_t FindRenderEventIndex(FakeRenderEvent event)
    {
        for (uint32_t i = 0; i < GRenderEvents.size(); ++i)
        {
            if (GRenderEvents[i] == event)
            {
                return i;
            }
        }

        assert(false);
        return 0;
    }

    uint32_t FindRenderEventIndexAfter(FakeRenderEvent event, uint32_t afterIndex)
    {
        for (uint32_t i = afterIndex + 1; i < GRenderEvents.size(); ++i)
        {
            if (GRenderEvents[i] == event)
            {
                return i;
            }
        }

        assert(false);
        return 0;
    }

    void ResetLightingDescriptorCapture()
    {
        GRenderEvents.clear();
        GLastDescriptorBinding4UpdateBytes.clear();
        GLastDescriptorBinding5UpdateBytes.clear();
        GLastDescriptorBinding4Buffer = nullptr;
        GLastDescriptorBinding5Buffer = nullptr;
        GLastDescriptorBinding4Offset = 0;
        GLastDescriptorBinding4Size = 0;
        GLastDescriptorBinding5Offset = 0;
        GLastDescriptorBinding5Size = 0;
    }

    class FakeTexture final : public RHI::ITexture
    {
    public:
        FakeTexture()
        {
            m_Desc.Width = 64;
            m_Desc.Height = 32;
            m_Desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
            m_Desc.Usage = RHI::ResourceUsage::RenderTarget | RHI::ResourceUsage::ShaderRead;
        }

        explicit FakeTexture(const RHI::TextureDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.Width; }
        uint32_t GetHeight() const override { return m_Desc.Height; }
        uint32_t GetDepth() const override { return m_Desc.Depth; }
        uint32_t GetMipLevels() const override { return m_Desc.MipLevels; }
        uint32_t GetArraySize() const override { return m_Desc.ArraySize; }
        RHI::Format GetFormat() const override { return m_Desc.TextureFormat; }
        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }
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

    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        FakeBuffer()
            : m_Desc(256, RHI::ResourceUsage::TransferDst | RHI::ResourceUsage::ShaderRead)
        {
        }

        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : m_Desc(desc)
        {
        }

        FakeBuffer(const RHI::BufferDesc& desc, RHI::TSharedPtr<FakeBufferLifetimeTracker> tracker)
            : m_Desc(desc), m_Tracker(tracker)
        {
        }

        ~FakeBuffer() override
        {
            if (m_Tracker)
            {
                m_Tracker->bDestroyed = true;
            }
        }

        uint64_t GetSize() const override { return m_Desc.Size; }
        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            // ジオメトリの区画へ書くステージングのリングだけは、写像して書き込めるようにバイト列を持つ
            if (IsDebugName(m_Desc.DebugName, "TileUploadRing") && m_Desc.Size > 0)
            {
                if (MappedBytes.empty())
                {
                    MappedBytes.resize(static_cast<size_t>(m_Desc.Size));
                }
                return offset < MappedBytes.size() ? MappedBytes.data() + offset : nullptr;
            }
            (void)offset;
            return nullptr;
        }
        void Unmap() override {}
        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            ++UpdateCallCount;
            LastUpdateOffset = offset;
            LastUpdateSize = size;
            LastUpdateBytes.resize(static_cast<size_t>(size));
            if (size > 0)
            {
                assert(data != nullptr);
                std::memcpy(LastUpdateBytes.data(), data, static_cast<size_t>(size));
            }

            if (IsDebugName(m_Desc.DebugName, "LightArraySSBO"))
            {
                PushRenderEvent(FakeRenderEvent::LightSsboUpdate);
            }
            if (IsDebugName(m_Desc.DebugName, "MegaGeometry_InstanceTable"))
            {
                GMegaInstanceTableUpdates.push_back(LastUpdateBytes);
            }
        }
        RHI::ResourceUsage GetUsage() const override
        {
            return m_Desc.Usage;
        }
        uint64_t GetDeviceAddress() const override
        {
            // ジオメトリの共有プールの塊だけが、まとめたカリングの引けるデバイスアドレスを持つ（実機の塊は
            // BufferDeviceAddress の用途で作る）。他のバッファは従来どおり 0（要求されていない）
            if (IsDebugName(m_Desc.DebugName, "GeometryPoolBlock"))
            {
                return 0x100000000ull + (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(this)) & 0xFFFFFFF0ull);
            }
            return 0;
        }

        const RHI::BufferDesc& GetDesc() const
        {
            return m_Desc;
        }

        uint64_t LastUpdateOffset = 0;
        uint64_t LastUpdateSize = 0;
        uint32_t UpdateCallCount = 0;
        Container::VariableArray<uint8_t> LastUpdateBytes;
        Container::VariableArray<uint8_t> MappedBytes;

    private:
        RHI::BufferDesc m_Desc;
        RHI::TSharedPtr<FakeBufferLifetimeTracker> m_Tracker;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        Container::VariableArray<BarrierEvent> Barriers;
        uint32_t BeginRenderPassCount = 0;
        uint32_t EndRenderPassCount = 0;
        uint32_t DrawCallCount = 0;
        uint32_t DispatchCount = 0;
        // 材質のタイル分類の資源（MaterialTile_ で始まる名前）への 0 埋めの記録（バッファの名前と大きさ）
        struct MaterialTileFill
        {
            char BufferName[40] = {};
            uint64_t SizeBytes = 0;
        };
        Container::VariableArray<MaterialTileFill> MaterialTileFills;
        // 「前のフレームで見えた」ビットのバッファへの0埋め（範囲）とコピー（古いバッファからの引き継ぎ）の記録
        struct VisibilityFill
        {
            uint64_t OffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };
        struct VisibilityCopy
        {
            uint64_t SourceOffsetBytes = 0;
            uint64_t DestinationOffsetBytes = 0;
            uint64_t SizeBytes = 0;
        };
        Container::VariableArray<VisibilityFill> VisibilityFills;
        Container::VariableArray<VisibilityCopy> VisibilityCopies;
        // 間接描画の記録（コマンドの先頭のバイト位置と、描画の最大数）。区間ごとに1回ずつ呼ばれる
        struct IndirectDrawRecord
        {
            uint64_t OffsetBytes = 0;
            uint32_t MaxDrawCount = 0;
        };
        Container::VariableArray<IndirectDrawRecord> IndirectDraws;
        // 呼ばれた順の記録（B=BeginRenderPass、E=EndRenderPass、D=Dispatch、I=間接描画）。パスの並びの検査用
        Container::VariableArray<char> CallSequence;
        uint32_t LastDrawIndexedInstancedIndexCount = 0;
        uint32_t LastDrawIndexedInstancedStartIndexLocation = 0;
        int32_t LastDrawIndexedInstancedBaseVertexLocation = 0;

        void Begin() override {}
        void End() override {}
        void Submit(bool waitForCompletion = false) override { (void)waitForCompletion; }
        void BeginRenderPass(RHI::RenderPassPtr renderPass, RHI::FramebufferPtr framebuffer) override
        {
            assert(renderPass);
            assert(framebuffer);
            ++BeginRenderPassCount;
            CallSequence.push_back('B');
        }
        void EndRenderPass() override
        {
            ++EndRenderPassCount;
            CallSequence.push_back('E');
        }
        void SetViewport(const RHI::Viewport& viewport) override { (void)viewport; }
        void SetScissor(const RHI::ScissorRect& scissor) override { (void)scissor; }
        void SetPipeline(RHI::PipelinePtr pipeline) override { (void)pipeline; }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)buffer;
            (void)offset;
            (void)slot;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, RHI::IndexType = RHI::IndexType::Uint32) override
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
            PushRenderEvent(FakeRenderEvent::CommandSetDescriptorSet);
        }
        void DrawIndexed(uint32_t indexCount,
                         uint32_t startIndexLocation = 0,
                         int32_t baseVertexLocation = 0) override
        {
            (void)indexCount;
            (void)startIndexLocation;
            (void)baseVertexLocation;
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override
        {
            (void)vertexCount;
            (void)startVertexLocation;
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void DrawIndexedInstanced(uint32_t indexCount,
                                  uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0,
                                  int32_t baseVertexLocation = 0,
                                  uint32_t startInstanceLocation = 0) override
        {
            (void)instanceCount;
            (void)startInstanceLocation;
            LastDrawIndexedInstancedIndexCount = indexCount;
            LastDrawIndexedInstancedStartIndexLocation = startIndexLocation;
            LastDrawIndexedInstancedBaseVertexLocation = baseVertexLocation;
            PushRenderEvent(FakeRenderEvent::Draw);
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
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
        }
        void DrawIndexedIndirect(RHI::BufferPtr indirectBuffer,
                                 uint64_t offset,
                                 uint32_t drawCount,
                                 uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)stride;
            IndirectDraws.push_back(IndirectDrawRecord{offset, drawCount});
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
            CallSequence.push_back('I');
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr indirectBuffer,
                                      uint64_t indirectOffset,
                                      RHI::BufferPtr countBuffer,
                                      uint64_t countOffset,
                                      uint32_t maxDrawCount,
                                      uint32_t stride) override
        {
            (void)indirectBuffer;
            (void)countBuffer;
            (void)countOffset;
            (void)stride;
            IndirectDraws.push_back(IndirectDrawRecord{indirectOffset, maxDrawCount});
            PushRenderEvent(FakeRenderEvent::Draw);
            ++DrawCallCount;
            CallSequence.push_back('I');
        }
        void FillBuffer(RHI::BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override
        {
            if (buffer &&
                IsDebugName(static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName,
                            "MegaGeometry_VisibleLastFrame"))
            {
                VisibilityFills.push_back(VisibilityFill{offset, size});
            }
            if (buffer)
            {
                const char* name = static_cast<const FakeBuffer*>(buffer.get())->GetDesc().DebugName;
                if (name != nullptr && std::strncmp(name, "MaterialTile_", 13) == 0 && value == 0u)
                {
                    MaterialTileFill fill;
                    std::memcpy(fill.BufferName, name, std::min(std::strlen(name), sizeof(fill.BufferName) - 1));
                    fill.SizeBytes = size;
                    MaterialTileFills.push_back(fill);
                }
            }
            (void)value;
        }
        void Dispatch(uint32_t threadGroupCountX,
                      uint32_t threadGroupCountY,
                      uint32_t threadGroupCountZ) override
        {
            (void)threadGroupCountX;
            (void)threadGroupCountY;
            (void)threadGroupCountZ;
            ++DispatchCount;
            CallSequence.push_back('D');
        }
        void CopyBuffer(RHI::BufferPtr src,
                        RHI::BufferPtr dst,
                        uint64_t size = 0,
                        uint64_t srcOffset = 0,
                        uint64_t dstOffset = 0) override
        {
            (void)src;
            if (dst &&
                IsDebugName(static_cast<const FakeBuffer*>(dst.get())->GetDesc().DebugName,
                            "MegaGeometry_VisibleLastFrame"))
            {
                VisibilityCopies.push_back(VisibilityCopy{srcOffset, dstOffset, size});
            }
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
            (void)offset;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Buffer;
            event.Buffer = buffer.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            event.BufferSize = size;
            Barriers.push_back(event);
        }
        void TextureBarrier(RHI::TexturePtr texture,
                            RHI::ResourceState beforeState,
                            RHI::ResourceState afterState,
                            uint32_t mipLevel = 0,
                            uint32_t arrayIndex = 0,
                            uint32_t mipCount = 0,
                            uint32_t arrayCount = 0) override
        {
            (void)mipLevel;
            (void)arrayIndex;
            (void)mipCount;
            (void)arrayCount;
            BarrierEvent event;
            event.Kind = RGBarrierKind::Texture;
            event.Texture = texture.get();
            event.BeforeState = beforeState;
            event.AfterState = afterState;
            Barriers.push_back(event);
        }
    };

    class FakeRenderPass final : public RHI::IRenderPass
    {
    public:
        explicit FakeRenderPass(const RHI::RenderPassDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorAttachments.size());
        }

        bool HasDepthStencilAttachment() const override
        {
            return m_Desc.hasDepthStencil;
        }

        RHI::Format GetColorAttachmentFormat(uint32_t index) const override
        {
            return index < m_Desc.colorAttachments.size()
                       ? m_Desc.colorAttachments[index].format
                       : RHI::Format::UNKNOWN;
        }

        RHI::Format GetDepthStencilFormat() const override
        {
            return m_Desc.depthStencilAttachment.format;
        }

    private:
        RHI::RenderPassDesc m_Desc;
    };

    class FakeFramebuffer final : public RHI::IFramebuffer
    {
    public:
        explicit FakeFramebuffer(const RHI::FramebufferDesc& desc)
            : m_Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return m_Desc.width; }
        uint32_t GetHeight() const override { return m_Desc.height; }
        RHI::RenderPassPtr GetRenderPass() const override { return m_Desc.renderPass; }
        RHI::TexturePtr GetColorAttachment(uint32_t index) const override
        {
            return index < m_Desc.colorTargets.size() ? m_Desc.colorTargets[index] : nullptr;
        }
        RHI::TexturePtr GetDepthStencilAttachment() const override { return m_Desc.depthStencilTarget; }
        uint32_t GetColorAttachmentCount() const override
        {
            return static_cast<uint32_t>(m_Desc.colorTargets.size());
        }
        bool HasDepthStencilAttachment() const override { return m_Desc.depthStencilTarget != nullptr; }

    private:
        RHI::FramebufferDesc m_Desc;
    };

    class FakeShader final : public RHI::IShader
    {
    public:
        explicit FakeShader(const RHI::ShaderDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::ShaderStage GetStage() const override { return m_Desc.stage; }
        Container::String GetEntryPoint() const override { return m_Desc.entryPoint; }
        const Container::VariableArray<uint8_t>& GetByteCode() const override
        {
            return m_Desc.byteCode;
        }

    private:
        RHI::ShaderDesc m_Desc;
    };

    class FakeSampler final : public RHI::ISampler
    {
    public:
        explicit FakeSampler(const RHI::SamplerDesc& desc)
            : m_Desc(desc)
        {
        }

        RHI::FilterMode GetFilterMin() const override { return m_Desc.filterMin; }
        RHI::FilterMode GetFilterMag() const override { return m_Desc.filterMag; }
        RHI::FilterMode GetFilterMip() const override { return m_Desc.filterMip; }
        RHI::TextureAddressMode GetAddressModeU() const override { return m_Desc.addressU; }
        RHI::TextureAddressMode GetAddressModeV() const override { return m_Desc.addressV; }
        RHI::TextureAddressMode GetAddressModeW() const override { return m_Desc.addressW; }
        uint32_t GetMaxAnisotropy() const override { return m_Desc.maxAnisotropy; }
        RHI::CompareFunc GetCompareFunc() const override { return m_Desc.compareFunc; }

    private:
        RHI::SamplerDesc m_Desc;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        FakePipeline(RHI::PipelineType type, uint32_t bindPointCount)
            : m_Type(type), m_BindPointCount(bindPointCount)
        {
        }

        RHI::PipelineType GetPipelineType() const override { return m_Type; }
        uint32_t GetBindPointCount() const override { return m_BindPointCount; }

    private:
        RHI::PipelineType m_Type = RHI::PipelineType::Graphics;
        uint32_t m_BindPointCount = 0;
    };

    RHI::ITexture* GLastDescriptorBinding6Texture = nullptr;

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t binding,
                                RHI::BufferPtr buffer,
                                uint32_t offset,
                                uint32_t size) override
        {
            if (binding == 4)
            {
                m_Binding4Buffer = buffer;
                GLastDescriptorBinding4Buffer = buffer.get();
                GLastDescriptorBinding4Offset = offset;
                GLastDescriptorBinding4Size = size;
            }
        }

        void BindTexture(uint32_t binding, RHI::TexturePtr texture) override
        {
            if (binding == 6)
            {
                GLastDescriptorBinding6Texture = texture.get();
            }
        }

        void BindSampler(uint32_t binding, RHI::SamplerPtr sampler) override
        {
            (void)binding;
            (void)sampler;
        }

        void BindStorageBuffer(uint32_t binding,
                               RHI::BufferPtr buffer,
                               uint32_t offset,
                               uint32_t size) override
        {
            if (binding == 5)
            {
                m_Binding5Buffer = buffer;
                GLastDescriptorBinding5Buffer = buffer.get();
                GLastDescriptorBinding5Offset = offset;
                GLastDescriptorBinding5Size = size;
                PushRenderEvent(FakeRenderEvent::BindStorageBuffer5);
            }
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

        void Update() override
        {
            CopyLastUpdateBytes(m_Binding4Buffer, GLastDescriptorBinding4UpdateBytes);
            CopyLastUpdateBytes(m_Binding5Buffer, GLastDescriptorBinding5UpdateBytes);
            if (m_Binding4Buffer || m_Binding5Buffer)
            {
                PushRenderEvent(FakeRenderEvent::DescriptorSetUpdate);
            }
        }

    private:
        static void CopyLastUpdateBytes(const RHI::BufferPtr& buffer,
                                        Container::VariableArray<uint8_t>& outBytes)
        {
            outBytes.clear();
            RHI::TSharedPtr<FakeBuffer> fakeBuffer = Container::DynamicPointerCast<FakeBuffer>(buffer);
            if (!fakeBuffer)
            {
                return;
            }

            outBytes.resize(fakeBuffer->LastUpdateBytes.size());
            if (!outBytes.empty())
            {
                std::memcpy(outBytes.data(), fakeBuffer->LastUpdateBytes.data(), outBytes.size());
            }
        }

        RHI::BufferPtr m_Binding4Buffer;
        RHI::BufferPtr m_Binding5Buffer;
    };

    class FakeShaderCompiler final : public RHI::IShaderCompiler
    {
    public:
        RHI::ShaderCompileResult CompileFromSource(const Container::String& source,
                                                   RHI::ShaderStage stage,
                                                   const Container::String& filename = "shader",
                                                   const Container::String& entryPoint = "main") override
        {
            (void)source;
            (void)stage;
            (void)filename;
            (void)entryPoint;
            return MakeResult();
        }

        RHI::ShaderCompileResult CompileFromFile(const Container::String& filePath,
                                                 RHI::ShaderStage stage,
                                                 const Container::String& entryPoint = "main") override
        {
            (void)filePath;
            (void)stage;
            (void)entryPoint;
            return MakeResult();
        }

    private:
        RHI::ShaderCompileResult MakeResult() const
        {
            RHI::ShaderCompileResult result;
            result.bSuccess = true;
            result.ByteCode.push_back(0x03);
            result.ByteCode.push_back(0x02);
            result.ByteCode.push_back(0x23);
            result.ByteCode.push_back(0x07);
            return result;
        }
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            BufferCreationRecord record;
            record.Desc = desc;
            record.Tracker = RHI::MakeShared<FakeBufferLifetimeTracker>();
            CreatedBuffers.push_back(record);

            if (IsDebugName(desc.DebugName, "LightArraySSBO"))
            {
                ++LightArraySSBOCreateCount;
                if (FailLightArraySSBOCreateIndex != 0 &&
                    LightArraySSBOCreateCount == FailLightArraySSBOCreateIndex)
                {
                    return nullptr;
                }
            }

            return RHI::MakeShared<FakeBuffer>(desc, record.Tracker);
        }

        RHI::TexturePtr CreateTexture(const RHI::TextureDesc& desc) override
        {
            return RHI::MakeShared<FakeTexture>(desc);
        }

        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc& desc) override
        {
            return RHI::MakeShared<FakeSampler>(desc);
        }

        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc& desc) override
        {
            return RHI::MakeShared<FakeShader>(desc);
        }

        RHI::CommandListPtr CreateCommandList() override
        {
            return RHI::MakeShared<FakeCommandList>();
        }

        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc& desc) override
        {
            (void)desc;
            return nullptr;
        }

        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc& desc) override
        {
            CreatedRenderPassDescs.push_back(desc);
            return RHI::MakeShared<FakeRenderPass>(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc& desc) override
        {
            return RHI::MakeShared<FakeFramebuffer>(desc);
        }

        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc& desc) override
        {
            LastGraphicsPipelineDescriptorSetLayouts = desc.descriptorSetLayouts;
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Graphics,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()));
        }

        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc& desc) override
        {
            return RHI::MakeShared<FakePipeline>(RHI::PipelineType::Compute,
                                                static_cast<uint32_t>(desc.descriptorSetLayouts.size()));
        }

        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc& desc) override
        {
            LastDescriptorSetDesc = desc;
            return RHI::MakeShared<FakeDescriptorSet>();
        }

        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return RHI::MakeShared<FakeShaderCompiler>();
        }

        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }

        void WaitIdle() override {}
        RHI::API GetAPI() const override { return RHI::API::Vulkan; }
        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return m_Capabilities;
        }

        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4& projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        // MegaGeometryPass のまとめたカリング・描画が要るデバイスの機能（デバイスアドレス・firstInstance つきの間接描画）を
        // 表明する。それ以外のテストの分岐を変えないよう、MegaGeometryPass を使うテストだけが呼ぶ
        void EnableMegaGeometryBatchCapabilities()
        {
            m_Capabilities.bBufferDeviceAddress = true;
            m_Capabilities.bDrawIndirectFirstInstance = true;
        }

        // ビジビリティバッファの描画（フラグメントシェーダーの gl_PrimitiveID）が要る機能を表明する
        void EnableVisibilityBufferCapabilities()
        {
            EnableMegaGeometryBatchCapabilities();
            m_Capabilities.bGeometryShader = true;
        }

        Container::VariableArray<RHI::RenderPassDesc> CreatedRenderPassDescs;
        Container::VariableArray<BufferCreationRecord> CreatedBuffers;
        RHI::DescriptorSetDesc LastDescriptorSetDesc;
        Container::VariableArray<RHI::DescriptorSetDesc> LastGraphicsPipelineDescriptorSetLayouts;
        uint32_t LightArraySSBOCreateCount = 0;
        uint32_t FailLightArraySSBOCreateIndex = 0;

    private:
        RHI::DeviceCapabilities m_Capabilities;
    };

    class MockAllocator final : public RHI::IGPUResourceAllocator
    {
    public:
        RHI::BufferAllocation AllocateBuffer(const RHI::BufferDesc& desc,
                                             RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto buffer = Container::MakeUnique<FakeBuffer>(desc);

            RHI::BufferAllocation allocation;
            allocation.Buffer = buffer.get();
            allocation.Offset = 0;
            allocation.Size = desc.Size;
            allocation.Type = type;

            m_Buffers.push_back(std::move(buffer));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_BufferAllocationCount;
            return allocation;
        }

        void FreeBuffer(RHI::BufferAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Buffers.begin(); it != m_Buffers.end(); ++it)
            {
                if (it->get() == allocation.Buffer)
                {
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Buffers.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        RHI::TextureAllocation AllocateTexture(const RHI::TextureDesc& desc,
                                               RHI::AllocationType type = RHI::AllocationType::Dedicated) override
        {
            auto texture = Container::MakeUnique<FakeTexture>(desc);

            RHI::TextureAllocation allocation;
            allocation.Texture = texture.get();
            allocation.Size = static_cast<uint64_t>(desc.Width) * static_cast<uint64_t>(desc.Height) * 4u;
            allocation.Type = type;

            m_Textures.push_back(std::move(texture));
            m_AllocatedMemory += static_cast<size_t>(allocation.Size);
            m_UsedMemory += static_cast<size_t>(allocation.Size);
            ++m_TextureAllocationCount;
            return allocation;
        }

        void FreeTexture(RHI::TextureAllocation& allocation) override
        {
            if (!allocation.IsValid())
            {
                return;
            }

            for (auto it = m_Textures.begin(); it != m_Textures.end(); ++it)
            {
                if (it->get() == allocation.Texture)
                {
                    m_AllocatedMemory -= static_cast<size_t>(allocation.Size);
                    m_UsedMemory -= static_cast<size_t>(allocation.Size);
                    m_Textures.erase(it);
                    allocation = {};
                    return;
                }
            }

            assert(false);
        }

        size_t GetAllocatedMemory() const override
        {
            return m_AllocatedMemory;
        }

        size_t GetUsedMemory() const override
        {
            return m_UsedMemory;
        }

        void Trim() override
        {
        }

        size_t GetLiveAllocationCount() const
        {
            return m_Textures.size() + m_Buffers.size();
        }

        uint32_t GetTextureAllocationCount() const
        {
            return m_TextureAllocationCount;
        }

        uint32_t GetBufferAllocationCount() const
        {
            return m_BufferAllocationCount;
        }

    private:
        Container::VariableArray<Container::TUniquePtr<FakeTexture>> m_Textures;
        Container::VariableArray<Container::TUniquePtr<FakeBuffer>> m_Buffers;
        size_t m_AllocatedMemory = 0;
        size_t m_UsedMemory = 0;
        uint32_t m_TextureAllocationCount = 0;
        uint32_t m_BufferAllocationCount = 0;
    };

    class EmptyPass final : public IRenderGraphPass
    {
    public:
        EmptyPass(const char* name, uint32_t* executeCount)
            : m_Name(name), m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return m_Name; }
        void Declare(RenderGraphBuilder& builder) override { (void)builder; }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_ExecuteCount)
            {
                ++(*m_ExecuteCount);
            }
        }

    private:
        const char* m_Name = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class LogicalPass final : public IRenderGraphPass
    {
    public:
        explicit LogicalPass(uint32_t id, Container::VariableArray<uint32_t>* executed)
            : m_Id(id), m_Executed(executed)
        {
        }

        const char* GetName() const override { return "LogicalPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            if (m_CreateTarget)
            {
                *m_CreateTarget = builder.CreateLogical("LogicalPass.Resource");
            }

            if (m_ReadA)
            {
                builder.Read(*m_ReadA);
            }

            if (m_ReadB)
            {
                builder.Read(*m_ReadB);
            }

            if (m_CreateTarget)
            {
                builder.Write(*m_CreateTarget);
            }

            if (m_WriteExisting)
            {
                builder.Write(*m_WriteExisting);
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            if (m_Executed)
            {
                m_Executed->push_back(m_Id);
            }
        }

        RGResourceHandle* m_CreateTarget = nullptr;
        RGResourceHandle* m_ReadA = nullptr;
        RGResourceHandle* m_ReadB = nullptr;
        RGResourceHandle* m_WriteExisting = nullptr;

    private:
        uint32_t m_Id = 0;
        Container::VariableArray<uint32_t>* m_Executed = nullptr;
    };

    class ImportedProducerPass final : public IRenderGraphPass
    {
    public:
        ImportedProducerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.ImportTexture(m_Texture, RHI::ResourceState::Undefined, "ImportedTexture");
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            *m_BufferHandle = builder.ImportBuffer(m_Buffer, RHI::ResourceState::Common, "ImportedBuffer");
            builder.Write(*m_BufferHandle, RHI::ResourceState::CopyDest);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class ImportedConsumerPass final : public IRenderGraphPass
    {
    public:
        ImportedConsumerPass(RHI::TexturePtr texture,
                             RHI::BufferPtr buffer,
                             RGResourceHandle* textureHandle,
                             RGResourceHandle* bufferHandle,
                             uint32_t* executeCount)
            : m_Texture(texture),
              m_Buffer(buffer),
              m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "ImportedConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
            builder.Read(*m_BufferHandle, RHI::ResourceState::GenericRead);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            assert(resources.GetTextureRaw(*m_TextureHandle) == m_Texture.get());
            assert(resources.GetBufferRaw(*m_BufferHandle) == m_Buffer.get());
            ++(*m_ExecuteCount);
        }

    private:
        RHI::TexturePtr m_Texture;
        RHI::BufferPtr m_Buffer;
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class TransientProducerPass final : public IRenderGraphPass
    {
    public:
        TransientProducerPass(RGResourceHandle* textureHandle,
                              RGResourceHandle* bufferHandle,
                              RHI::ITexture** resolvedTexture,
                              RHI::IBuffer** resolvedBuffer,
                              uint32_t* executeCount)
            : m_TextureHandle(textureHandle),
              m_BufferHandle(bufferHandle),
              m_ResolvedTexture(resolvedTexture),
              m_ResolvedBuffer(resolvedBuffer),
              m_ExecuteCount(executeCount)
        {
        }

        const char* GetName() const override { return "TransientProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "TransientTexture"));
            builder.Write(*m_TextureHandle, RHI::ResourceState::RenderTarget);

            RGBufferDesc bufferDesc;
            bufferDesc.Size = 512;
            bufferDesc.Usage = RHI::ResourceUsage::StorageBuffer;
            bufferDesc.DebugName = "TransientBuffer";
            *m_BufferHandle = builder.CreateBuffer(bufferDesc);
            builder.Write(*m_BufferHandle, RHI::ResourceState::UnorderedAccess);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)context;
            *m_ResolvedTexture = resources.GetTextureRaw(*m_TextureHandle);
            *m_ResolvedBuffer = resources.GetBufferRaw(*m_BufferHandle);
            assert(*m_ResolvedTexture);
            assert(*m_ResolvedBuffer);
            ++(*m_ExecuteCount);
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
        RGResourceHandle* m_BufferHandle = nullptr;
        RHI::ITexture** m_ResolvedTexture = nullptr;
        RHI::IBuffer** m_ResolvedBuffer = nullptr;
        uint32_t* m_ExecuteCount = nullptr;
    };

    class InvalidHandlePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "InvalidHandlePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(RGResourceHandle{});
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
            assert(false);
        }
    };

    class ContextProbePass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "ContextProbePass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            assert(context);
            m_Width = context->GetActiveRenderWidth();
            m_Height = context->GetActiveRenderHeight();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        uint32_t GetWidth() const
        {
            return m_Width;
        }

        uint32_t GetHeight() const
        {
            return m_Height;
        }

    private:
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };

    class PublishSceneDepthAliasPass final : public IRenderGraphPass
    {
    public:
        explicit PublishSceneDepthAliasPass(RGResourceHandle* publishedHandle)
            : m_PublishedHandle(publishedHandle)
        {
        }

        const char* GetName() const override { return "PublishSceneDepthAliasPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle sceneDepth = builder.CreateTextureHandle(
                RGTextureDesc::DepthStencil(32, 16, RHI::Format::D32_FLOAT, "PrepublishedSceneDepth"));
            assert(sceneDepth.IsValid());
            assert(builder.PublishTexture(RenderGraphResourceNames::SceneDepth, sceneDepth));
            if (m_PublishedHandle)
            {
                *m_PublishedHandle = sceneDepth.ToResourceHandle();
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_PublishedHandle = nullptr;
    };

    class NamedShadowMapProducerPass final : public IRenderGraphPass
    {
    public:
        NamedShadowMapProducerPass(RHI::TexturePtr texture, RGResourceHandle* publishedHandle)
            : m_Texture(texture), m_PublishedHandle(publishedHandle)
        {
        }

        const char* GetName() const override { return "NamedShadowMapProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle shadowMap = builder.ImportTexture(m_Texture,
                                                               RHI::ResourceState::ShaderResource,
                                                               "NamedShadowMap");
            assert(shadowMap.IsValid());
            builder.Read(shadowMap, RHI::ResourceState::ShaderResource);
            assert(builder.PublishTexture(RenderGraphResourceNames::ShadowMap, shadowMap));
            if (m_PublishedHandle)
            {
                *m_PublishedHandle = shadowMap;
            }
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
        RGResourceHandle* m_PublishedHandle = nullptr;
    };

    class NamedGBufferAlbedoProducerPass final : public IRenderGraphPass
    {
    public:
        explicit NamedGBufferAlbedoProducerPass(RHI::TexturePtr texture)
            : m_Texture(texture)
        {
        }

        const char* GetName() const override { return "NamedGBufferAlbedoProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGResourceHandle albedo = builder.ImportTexture(m_Texture,
                                                            RHI::ResourceState::ShaderResource,
                                                            "PartialGBufferAlbedo");
            assert(albedo.IsValid());
            builder.Read(albedo, RHI::ResourceState::ShaderResource);
            assert(builder.PublishTexture(RenderGraphResourceNames::GBufferAlbedo, albedo));
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RHI::TexturePtr m_Texture;
    };

    class FinalStateProducerPass final : public IRenderGraphPass
    {
    public:
        explicit FinalStateProducerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "FinalStateProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            *m_TextureHandle = builder.CreateTexture(
                RGTextureDesc::RenderTarget(32, 16, RHI::Format::R8G8B8A8_UNORM, "FinalStateTexture"));
            builder.Write(*m_TextureHandle,
                          RHI::ResourceState::RenderTarget,
                          RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class ShaderReadConsumerPass final : public IRenderGraphPass
    {
    public:
        explicit ShaderReadConsumerPass(RGResourceHandle* textureHandle)
            : m_TextureHandle(textureHandle)
        {
        }

        const char* GetName() const override { return "ShaderReadConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            builder.Read(*m_TextureHandle, RHI::ResourceState::ShaderResource);
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        RGResourceHandle* m_TextureHandle = nullptr;
    };

    class LegacyFXAAProducerPass final : public FXAAPass
    {
    public:
        const char* GetName() const override { return "LegacyFXAAProducerPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            const ViewRenderContext* context = builder.GetContext();
            const uint32_t width = context ? context->GetActiveRenderWidth() : 1u;
            const uint32_t height = context ? context->GetActiveRenderHeight() : 1u;

            m_OutputTextureHandle = builder.CreateTextureHandle(
                RGTextureDesc::RenderTarget(width, height, RHI::Format::R8G8B8A8_UNORM, "LegacyFXAAOutput"));
            m_OutputHandle = m_OutputTextureHandle.ToResourceHandle();
            builder.Write(m_OutputHandle, RHI::ResourceState::RenderTarget, RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    void AssertOrder(const Container::VariableArray<uint32_t>& order,
                     uint32_t a,
                     uint32_t b,
                     uint32_t c)
    {
        assert(order.size() == 3);
        assert(order[0] == a);
        assert(order[1] == b);
        assert(order[2] == c);
    }

    void TestLinearDependencyOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        EmptyPass passC("C", &executeCount);

        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        const uint32_t indexC = graph.AddPass(&passC);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexC));

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), indexA, indexB, indexC);
    }

    void TestDiamondStableOrder()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle root;
        RGResourceHandle left;
        RGResourceHandle right;
        Container::VariableArray<uint32_t> executed;

        LogicalPass rootPass(0, &executed);
        rootPass.m_CreateTarget = &root;
        LogicalPass leftPass(1, &executed);
        leftPass.m_ReadA = &root;
        leftPass.m_CreateTarget = &left;
        LogicalPass rightPass(2, &executed);
        rightPass.m_ReadA = &root;
        rightPass.m_CreateTarget = &right;
        LogicalPass sinkPass(3, &executed);
        sinkPass.m_ReadA = &left;
        sinkPass.m_ReadB = &right;

        graph.AddPass(&rootPass);
        graph.AddPass(&leftPass);
        graph.AddPass(&rightPass);
        graph.AddPass(&sinkPass);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == 0);
        assert(order[1] == 1);
        assert(order[2] == 2);
        assert(order[3] == 3);
    }

    void TestWriteAfterWriteDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass firstWrite(0, &executed);
        firstWrite.m_CreateTarget = &resource;
        LogicalPass secondWrite(1, &executed);
        secondWrite.m_WriteExisting = &resource;

        graph.AddPass(&firstWrite);
        graph.AddPass(&secondWrite);

        assert(graph.Compile());
        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    void TestWriteAfterReadDependency()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle resource;
        Container::VariableArray<uint32_t> executed;

        LogicalPass producer(0, &executed);
        producer.m_CreateTarget = &resource;
        LogicalPass reader(1, &executed);
        reader.m_ReadA = &resource;
        LogicalPass laterWrite(2, &executed);
        laterWrite.m_WriteExisting = &resource;

        graph.AddPass(&producer);
        graph.AddPass(&reader);
        graph.AddPass(&laterWrite);

        assert(graph.Compile());
        AssertOrder(graph.GetCompiledPassOrder(), 0, 1, 2);
    }

    void TestImportedResourceBarriers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RHI::TexturePtr texture = RHI::MakeShared<FakeTexture>();
        RHI::BufferPtr buffer = RHI::MakeShared<FakeBuffer>();
        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        uint32_t executeCount = 0;

        ImportedProducerPass producer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        ImportedConsumerPass consumer(texture, buffer, &textureHandle, &bufferHandle, &executeCount);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 4);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].BeforeState == RHI::ResourceState::Common);
        assert(barriers[1].AfterState == RHI::ResourceState::CopyDest);
        assert(barriers[1].CompiledOrderIndex == 0);
        assert(barriers[1].BufferSize == buffer->GetSize());
        assert(barriers[2].Kind == RGBarrierKind::Texture);
        assert(barriers[2].BeforeState == RHI::ResourceState::RenderTarget);
        assert(barriers[2].AfterState == RHI::ResourceState::ShaderResource);
        assert(barriers[2].CompiledOrderIndex == 1);
        assert(barriers[3].Kind == RGBarrierKind::Buffer);
        assert(barriers[3].BeforeState == RHI::ResourceState::CopyDest);
        assert(barriers[3].AfterState == RHI::ResourceState::GenericRead);
        assert(barriers[3].CompiledOrderIndex == 1);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));
        assert(executeCount == 2);
        assert(graph.GetLastExecutedPassCount() == 2);
        assert(commandList.Barriers.size() == 4);
        assert(commandList.Barriers[0].Texture == texture.get());
        assert(commandList.Barriers[1].Buffer == buffer.get());
        assert(commandList.Barriers[1].BufferSize == buffer->GetSize());
        assert(commandList.Barriers[2].Texture == texture.get());
        assert(commandList.Barriers[3].Buffer == buffer.get());
    }

    void TestTransientResourcesResolveThroughPool()
    {
        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        RGResourceHandle textureHandle;
        RGResourceHandle bufferHandle;
        RHI::ITexture* resolvedTexture = nullptr;
        RHI::IBuffer* resolvedBuffer = nullptr;
        uint32_t executeCount = 0;
        TransientProducerPass pass(&textureHandle,
                                   &bufferHandle,
                                   &resolvedTexture,
                                   &resolvedBuffer,
                                   &executeCount);
        graph.AddPass(&pass);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 2);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[1].Kind == RGBarrierKind::Buffer);
        assert(barriers[1].AfterState == RHI::ResourceState::UnorderedAccess);
        assert(barriers[1].BufferSize == 512);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        assert(graph.Execute(context));

        assert(executeCount == 1);
        assert(resolvedTexture);
        assert(resolvedBuffer);
        assert(allocator.GetTextureAllocationCount() == 1);
        assert(allocator.GetBufferAllocationCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(commandList.Barriers[0].Texture == resolvedTexture);
        assert(commandList.Barriers[1].Buffer == resolvedBuffer);
        assert(commandList.Barriers[1].BufferSize == 512);

        pool.EndFrame();
        graph.Shutdown();
        pool.Shutdown();
        assert(allocator.GetLiveAllocationCount() == 0);
    }

    void TestCycleDetectionDoesNotExecute()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        uint32_t executeCount = 0;
        EmptyPass passA("A", &executeCount);
        EmptyPass passB("B", &executeCount);
        const uint32_t indexA = graph.AddPass(&passA);
        const uint32_t indexB = graph.AddPass(&passB);
        assert(graph.AddDependency(indexA, indexB));
        assert(graph.AddDependency(indexB, indexA));

        assert(!graph.Compile());
        ViewRenderContext context;
        assert(!graph.Execute(context));
        assert(executeCount == 0);
        assert(graph.GetLastExecutedPassCount() == 0);
    }

    void TestInvalidHandleRejected()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        InvalidHandlePass pass;
        graph.AddPass(&pass);
        assert(!graph.Compile());
    }

    void TestCompileContextPassedToDeclare()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ContextProbePass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 320;
        context.RenderHeight = 180;

        assert(graph.Compile(context));
        assert(pass.GetWidth() == 320);
        assert(pass.GetHeight() == 180);
    }

    void TestWriteFinalStateSuppressesFollowupReadBarrier()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        RGResourceHandle textureHandle;
        FinalStateProducerPass producer(&textureHandle);
        ShaderReadConsumerPass consumer(&textureHandle);
        graph.AddPass(&producer);
        graph.AddPass(&consumer);

        assert(graph.Compile());
        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 1);
        assert(barriers[0].Kind == RGBarrierKind::Texture);
        assert(barriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[0].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[0].CompiledOrderIndex == 0);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == 0);
        assert(order[1] == 1);
    }

    // ビジビリティバッファの資源（VisBuffer.Id と描画の記録の表）を名前つきで書くパス
    class VisibilityBufferProducerPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "VisibilityBufferProducerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            const RGTextureHandle id = builder.WriteTextureAttachment(
                RenderGraphResourceNames::VisBufferId,
                VisibilityBuffer::MakeIdTextureDesc(64, 32),
                RGAttachmentKind::Color,
                RHI::AttachmentLoadOp::Clear,
                RHI::AttachmentStoreOp::Store,
                RHI::ResourceState::RenderTarget,
                RHI::ResourceState::ShaderResource);
            const RGBufferHandle records = builder.WriteBuffer(RenderGraphResourceNames::VisBufferDrawRecords,
                                                               VisibilityBuffer::MakeRecordTableBufferDesc(16),
                                                               RHI::ResourceState::UnorderedAccess,
                                                               RHI::ResourceState::GenericRead);
            assert(id.IsValid() && records.IsValid());
            builder.PreserveInsertionOrder();
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    // 名前で VisBuffer.Id と記録の表を引いて読むパス（後の材質の分類・解決が同じ形で読む）
    class VisibilityBufferConsumerPass final : public IRenderGraphPass
    {
    public:
        explicit VisibilityBufferConsumerPass(bool* foundBoth)
            : m_FoundBoth(foundBoth)
        {
        }

        const char* GetName() const override { return "VisibilityBufferConsumerPass"; }
        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle id;
            RGBufferHandle records;
            *m_FoundBoth = builder.TryGetTexture(RenderGraphResourceNames::VisBufferId, id) &&
                           builder.TryGetBuffer(RenderGraphResourceNames::VisBufferDrawRecords, records);
            if (*m_FoundBoth)
            {
                builder.Read(id.ToResourceHandle(), RHI::ResourceState::ShaderResource);
                builder.Read(records.ToResourceHandle(), RHI::ResourceState::GenericRead);
            }
        }
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        bool* m_FoundBoth = nullptr;
    };

    void TestVisibilityBufferResourcesDeclareAndRead()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        bool consumerFoundBoth = false;
        VisibilityBufferProducerPass producer;
        VisibilityBufferConsumerPass consumer(&consumerFoundBoth);
        const uint32_t producerIndex = graph.AddPass(&producer);
        const uint32_t consumerIndex = graph.AddPass(&consumer);

        assert(graph.Compile());
        assert(consumerFoundBoth);

        uint32_t version = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferId, version));
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferDrawRecords, version));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == producerIndex);
        assert(order[1] == consumerIndex);

        // 書く側の最初の状態（添付・UAV）への遷移が積まれる。
        bool sawIdTarget = false;
        bool sawRecordsWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.Kind == RGBarrierKind::Texture && barrier.AfterState == RHI::ResourceState::RenderTarget)
            {
                sawIdTarget = true;
            }
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::UnorderedAccess)
            {
                sawRecordsWrite = true;
            }
        }
        assert(sawIdTarget);
        assert(sawRecordsWrite);
    }

    // ビジビリティバッファの描画（--visibility-buffer=on）の記録に使う、2つの MegaMesh インスタンスを持つシーンの一式。
    // GBufferPass → MegaGeometryPass（描画の写しを作る）→ VisibilityRasterPass の順に RenderGraph で実行する
    struct VisibilityRasterScene
    {
        RHI::TSharedPtr<FakeDevice> Device;
        ShaderManager ShaderMgr;
        MockAllocator Allocator;
        RHI::TransientResourcePool Pool;
        RenderResources Resources;
        SceneRenderer Renderer;
        RenderGraph Graph;
        GBufferPass GBuffer;
        MegaGeometryPass Mega;
        VisibilityRasterPass Raster;
        MaterialTileClassifyPass Classify;
        FakeCommandList CommandList;
        Container::VariableArray<DrawCommand> OpaqueCommands;
        Container::VariableArray<FrameCommand> PendingFrameCommands;
        Container::VariableArray<MegaGeometryProxy> Proxies;
        CameraProxy Camera;
        ViewRenderContext Context;
    };

    // 材質のタイル分類のパスの足し方
    enum class ClassifyMode
    {
        None,
        /** @brief 描画のパスから記録の表を受け取る */
        LinkedToRaster,
        /** @brief 記録の表の取り出し元を渡さない（分類できないときの安全側の動き） */
        WithoutRaster,
        /** @brief SceneView と同じく、記録の表の取り出し元を渡してグラフへ足すが、有効にしない（既定のまま） */
        AddedDisabled,
    };

    // bVisibilityPlan=false は --visibility-buffer=off（MegaGeometryPass が描画の写しを作らず、VisibilityRasterPass も足さない）
    void RunVisibilityRasterScene(VisibilityRasterScene& scene,
                                  bool bVisibilityPlan,
                                  bool bOcclusionCulling,
                                  ClassifyMode classifyMode = ClassifyMode::None)
    {
        scene.Device = RHI::MakeShared<FakeDevice>();
        scene.Device->EnableVisibilityBufferCapabilities();
        assert(scene.ShaderMgr.Initialize(scene.Device.get(), TestShaderDirectory));
        assert(scene.Pool.Initialize(&scene.Allocator, 1));
        scene.Pool.BeginFrame(0);
        assert(scene.Resources.Initialize(scene.Device));
        scene.Resources.MegaGeometry().SetOcclusionCullingEnabled(bOcclusionCulling);
        assert(scene.Renderer.Initialize(scene.Device.get(), nullptr, &scene.Pool));
        assert(scene.Graph.Initialize(&scene.Pool));
        scene.Graph.BeginFrame(0);
        scene.GBuffer.SetSceneRenderer(&scene.Renderer);

        float vertices[12] = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};
        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;
        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "VisRasterMegaA";
        const auto megaMeshA = scene.Resources.MegaGeometry().CreateMegaMesh(createInfo);
        createInfo.DebugName = "VisRasterMegaB";
        const auto megaMeshB = scene.Resources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshA.IsValid() && megaMeshB.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(scene.Resources));

        MegaGeometryProxy proxyA;
        proxyA.ObjectId = 1;
        proxyA.ComponentId = 10;
        proxyA.MegaMeshHandle = megaMeshA;
        proxyA.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxyA.WorldBounds = createInfo.TotalBounds;
        scene.Proxies.push_back(proxyA);
        MegaGeometryProxy proxyB = proxyA;
        proxyB.ObjectId = 2;
        proxyB.ComponentId = 20;
        proxyB.MegaMeshHandle = megaMeshB;
        scene.Proxies.push_back(proxyB);

        scene.Camera.Viewport.Width = 128.0f;
        scene.Camera.Viewport.Height = 64.0f;

        ViewRenderContext& context = scene.Context;
        context.CommandList = &scene.CommandList;
        context.Device = scene.Device.get();
        context.TransientPool = &scene.Pool;
        context.ShaderMgr = &scene.ShaderMgr;
        context.Renderer = &scene.Renderer;
        context.PendingFrameCommands = &scene.PendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.MainCamera = &scene.Camera;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(scene.OpaqueCommands);
        context.SnapshotMegaGeometryProxies = &scene.Proxies;
        context.Resources.Textures = &scene.Resources.Textures();
        context.Resources.Materials = &scene.Resources.Materials();
        context.Resources.Meshes = &scene.Resources.Meshes();
        context.Resources.MegaGeometry = &scene.Resources.MegaGeometry();

        scene.Mega.SetVisibilityDrawPlanEnabled(bVisibilityPlan);
        scene.Raster.SetMegaGeometryPass(&scene.Mega);
        assert(scene.Mega.Initialize(context));
        scene.Graph.AddPass(&scene.GBuffer);
        scene.Graph.AddPass(&scene.Mega);
        if (bVisibilityPlan)
        {
            assert(scene.Raster.Initialize(context));
            scene.Graph.AddPass(&scene.Raster);
            if (classifyMode != ClassifyMode::None)
            {
                if (classifyMode != ClassifyMode::AddedDisabled)
                {
                    scene.Classify.SetEnabled(true);
                }
                if (classifyMode == ClassifyMode::LinkedToRaster || classifyMode == ClassifyMode::AddedDisabled)
                {
                    scene.Classify.SetRasterPass(&scene.Raster);
                }
                assert(scene.Classify.Initialize(context));
                // View::Render と同じく、無効なパスはグラフへ足さない
                if (scene.Classify.IsEnabled())
                {
                    scene.Graph.AddPass(&scene.Classify);
                }
            }
        }
        assert(scene.Graph.Compile(context));
        const RenderGraphExecutionResult result = scene.Graph.ExecuteWithResult(context);
        assert(result.bSuccess);
    }

    void ShutdownVisibilityRasterScene(VisibilityRasterScene& scene)
    {
        scene.Classify.Shutdown();
        scene.Raster.Shutdown();
        scene.Mega.Shutdown();
        scene.GBuffer.Shutdown();
        scene.Renderer.Shutdown();
        scene.Resources.Shutdown();
        scene.Graph.Shutdown();
        scene.Pool.EndFrame();
        scene.Pool.Shutdown();
        scene.ShaderMgr.Shutdown();
    }

    // --visibility-buffer=on: MegaGeometry の2パスのコマンドを、そのまま ID と深度のレンダーパスで描き直す。
    // コマンドは MegaGeometryPass が積んだ範囲（1パス目・2パス目）を使い、記録は GPU（計算）が1回の dispatch で書く。
    void TestVisibilityRasterOnRecordsMegaDrawsAndIdPass()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（2パス）・ビジビリティバッファ（ID）の4つのレンダーパス
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);

        // 間接描画: MegaGeometry の 1・2 パス目（コマンドの範囲は 0 と 2 * 20 バイト）の後に、同じ範囲をもう一度
        assert(commandList.IndirectDraws.size() == 4);
        for (size_t drawIndex = 0; drawIndex < 2; ++drawIndex)
        {
            assert(commandList.IndirectDraws[2 + drawIndex].OffsetBytes == commandList.IndirectDraws[drawIndex].OffsetBytes);
            assert(commandList.IndirectDraws[2 + drawIndex].MaxDrawCount == commandList.IndirectDraws[drawIndex].MaxDrawCount);
        }
        assert(commandList.IndirectDraws[3].OffsetBytes == 2 * 20);

        // dispatch: カリング 2 回 + HZB 7 段 + 記録を書く計算 1 回
        assert(commandList.DispatchCount == 10);

        // 並びの最後は、記録を書く計算（D）→ ID のレンダーパス（B → 間接描画 2 回 → E）
        const auto& sequence = commandList.CallSequence;
        const char tail[] = {'D', 'B', 'I', 'I', 'E'};
        assert(sequence.size() > sizeof(tail));
        for (size_t i = 0; i < sizeof(tail); ++i)
        {
            assert(sequence[sequence.size() - sizeof(tail) + i] == tail[i]);
        }

        // 記録の枠: 0 番の空 + MegaGeometry のコマンド（1 パス 2 つ × 2 パス）。手続き・スキニングの描画は無い
        const VisibilityRasterFrameStats& stats = scene.Raster.GetLastFrameStats();
        assert(stats.bRendered);
        assert(stats.MegaCommandSlots == 4);
        assert(stats.ProceduralRecords == 0 && stats.SkinnedRecords == 0);
        assert(stats.TotalSlots == 1 + 4);
        assert(scene.Raster.GetRecordTable());
        assert(scene.Raster.GetRecordTableBytes() == 5 * sizeof(VisibilityBuffer::DrawRecord));

        // ID は R32_UINT の1枚のカラー添付（空の ID で消す）と、GBuffer が書いた深度の Load。どちらも ShaderResource で終わる
        bool bFoundIdRenderPass = false;
        for (const RHI::RenderPassDesc& desc : scene.Device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 1 || desc.colorAttachments[0].format != RHI::Format::R32_UINT)
            {
                continue;
            }
            const RHI::AttachmentDesc& id = desc.colorAttachments[0];
            assert(id.loadOp == RHI::AttachmentLoadOp::Clear);
            assert(id.clearColorUint[0] == VisibilityBuffer::EMPTY_ID);
            assert(id.initialState == RHI::ResourceState::RenderTarget);
            assert(id.finalState == RHI::ResourceState::ShaderResource);
            assert(desc.hasDepthStencil);
            assert(desc.depthStencilAttachment.loadOp == RHI::AttachmentLoadOp::Load);
            assert(desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite);
            assert(desc.depthStencilAttachment.finalState == RHI::ResourceState::ShaderResource);
            bFoundIdRenderPass = true;
        }
        assert(bFoundIdRenderPass);

        // MegaGeometryPass が残した IndirectDraw・カウンタ・描画情報は、ID の描画の後に Common へ戻る（記録を書く計算のために
        // コマンド・カウンタは GenericRead へ渡され、そこから戻る）。ID の描画より前には戻らない
        size_t commonBarriers = 0;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.AfterState == RHI::ResourceState::Common &&
                barrier.BeforeState == RHI::ResourceState::GenericRead)
            {
                const auto* fakeBuffer = static_cast<const FakeBuffer*>(barrier.Buffer);
                const char* name = fakeBuffer->GetDesc().DebugName;
                if (IsDebugName(name, "MegaGeometry_IndirectDraw") || IsDebugName(name, "MegaGeometry_DrawCount") ||
                    IsDebugName(name, "MegaGeometry_DrawInfo"))
                {
                    ++commonBarriers;
                }
            }
        }
        assert(commonBarriers == 3);

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質のタイル分類のパス: VisBuffer.Id を読み、引数・一覧・カーソル・統計を書く。3 回の dispatch で分類し、
    // 引数は間接 dispatch の引数として読める用途と状態（GenericRead）で後のパスへ渡す
    void TestMaterialTileClassifyDispatchesAndPublishesArgs()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::LinkedToRaster);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（2パス）・ビジビリティ・材質のタイル分類の4つのパス
        assert(scene.Graph.GetLastExecutedPassCount() == 4);
        // 記録を書く計算まで 10 回 + 分類の 3 回（数える・位置を決める・書き出す）
        assert(commandList.DispatchCount == 13);
        assert(scene.Classify.WasClassified());

        // 画面は 128x64 なので 16x8 のタイル。一覧は 1 タイルが出せる材質の最大数ぶん
        const MaterialTiles::Layout& layout = scene.Classify.GetLayout();
        assert(layout.IsValid());
        assert(layout.TilesX == 16 && layout.TilesY == 8 && layout.TileCount == 128);
        assert(layout.MaxMaterials == MaterialTiles::DEFAULT_MAX_MATERIALS);
        assert(layout.ListCapacity == 128 * MaterialTiles::MAX_MATERIALS_PER_TILE);
        assert(layout.ArgsBytes() ==
               static_cast<uint64_t>(MaterialTiles::DEFAULT_MAX_MATERIALS) * MaterialTiles::ARGS_STRIDE_BYTES);
        // 引数の x は実機が保証する上限（65535）以下に抑える。超える分は y へ広げる
        assert(layout.GroupCountXLimit == MaterialTiles::MAX_GROUP_COUNT_X);

        // 数え始める前に、引数と統計だけを 0 にする（一覧とカーソルは分類が書く範囲しか読まれない）
        bool bArgsFilled = false;
        bool bStatsFilled = false;
        for (const FakeCommandList::MaterialTileFill& fill : commandList.MaterialTileFills)
        {
            if (IsDebugName(fill.BufferName, "MaterialTile_Args"))
            {
                assert(fill.SizeBytes == layout.ArgsBytes());
                bArgsFilled = true;
            }
            else if (IsDebugName(fill.BufferName, "MaterialTile_Stats"))
            {
                assert(fill.SizeBytes == MaterialTiles::STATS_BYTES);
                bStatsFilled = true;
            }
            else
            {
                assert(false);
            }
        }
        assert(bArgsFilled && bStatsFilled);

        // 引数と一覧は、後の材質の解決が読めるよう、このパスが GenericRead へ遷移させて渡す。引数のバッファは間接引数の用途を持つ
        bool bArgsToGenericRead = false;
        bool bListToGenericRead = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind != RGBarrierKind::Buffer || barrier.BeforeState != RHI::ResourceState::UnorderedAccess ||
                barrier.AfterState != RHI::ResourceState::GenericRead)
            {
                continue;
            }
            const auto* fakeBuffer = static_cast<const FakeBuffer*>(barrier.Buffer);
            const char* name = fakeBuffer->GetDesc().DebugName;
            if (IsDebugName(name, "MaterialTile_Args"))
            {
                assert((fakeBuffer->GetDesc().Usage & RHI::ResourceUsage::IndirectBuffer) == RHI::ResourceUsage::IndirectBuffer);
                assert((fakeBuffer->GetDesc().Usage & RHI::ResourceUsage::StorageBuffer) == RHI::ResourceUsage::StorageBuffer);
                bArgsToGenericRead = true;
            }
            else if (IsDebugName(name, "MaterialTile_List"))
            {
                bListToGenericRead = true;
            }
        }
        assert(bArgsToGenericRead && bListToGenericRead);

        ShutdownVisibilityRasterScene(scene);
    }

    // 記録の表の取り出し元が無く分類できないときは、dispatch せずに引数と統計を 0 にして終える
    // （後の材質の解決が、材質ごとの dispatch のグループ数 0 として安全に読める）
    void TestMaterialTileClassifyWithoutRecordTableClearsArgs()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::WithoutRaster);
        FakeCommandList& commandList = scene.CommandList;

        assert(scene.Graph.GetLastExecutedPassCount() == 4);
        assert(!scene.Classify.WasClassified());
        assert(commandList.DispatchCount == 10); // 分類の dispatch は無い

        bool bArgsCleared = false;
        bool bStatsCleared = false;
        for (const FakeCommandList::MaterialTileFill& fill : commandList.MaterialTileFills)
        {
            bArgsCleared = bArgsCleared || IsDebugName(fill.BufferName, "MaterialTile_Args");
            bStatsCleared = bStatsCleared || IsDebugName(fill.BufferName, "MaterialTile_Stats");
        }
        assert(bArgsCleared && bStatsCleared);
        assert(commandList.MaterialTileFills.size() == 2);

        // 分類できなくても、宣言した最終の状態（GenericRead）へ渡す
        bool bArgsToGenericRead = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Kind == RGBarrierKind::Buffer && barrier.BeforeState == RHI::ResourceState::UnorderedAccess &&
                barrier.AfterState == RHI::ResourceState::GenericRead &&
                IsDebugName(static_cast<const FakeBuffer*>(barrier.Buffer)->GetDesc().DebugName, "MaterialTile_Args"))
            {
                bArgsToGenericRead = true;
            }
        }
        assert(bArgsToGenericRead);

        ShutdownVisibilityRasterScene(scene);
    }

    // 材質のタイル分類を足さない構成では、資源も dispatch も増えない
    void TestMaterialTileClassifyAbsentWhenNotAdded()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true);
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(scene.CommandList.DispatchCount == 10);
        assert(scene.CommandList.MaterialTileFills.empty());
        assert(!scene.Classify.GetLayout().IsValid());
        ShutdownVisibilityRasterScene(scene);
    }

    // 実際の既定: SceneView は --visibility-buffer=on|debug でパスを View へ足すが、有効にしない。
    // View::Render は無効なパスをグラフへ足さないので、有効にするまでは資源も dispatch も増えず、起動画面の描画を変えない。
    // パスの生成時の既定が無効でなくなると、グラフへ足されて 4 パスになりこのテストが落ちる
    void TestMaterialTileClassifyAddedButDisabledByDefault()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, true, ClassifyMode::AddedDisabled);
        assert(!scene.Classify.IsEnabled());
        assert(scene.Graph.GetLastExecutedPassCount() == 3);
        assert(scene.CommandList.DispatchCount == 10);
        assert(scene.CommandList.MaterialTileFills.empty());
        assert(!scene.Classify.GetLayout().IsValid());
        assert(!scene.Classify.WasClassified());
        ShutdownVisibilityRasterScene(scene);
    }

    // 遮蔽カリングを使わない（1パスだけの）経路でも、その1パスのコマンドを描き直す
    void TestVisibilityRasterOnSinglePassMegaGeometry()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, true, false);
        FakeCommandList& commandList = scene.CommandList;

        // GBuffer（空）・MegaGeometry（1パス）・ID
        assert(commandList.BeginRenderPassCount == 3);
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[1].OffsetBytes == commandList.IndirectDraws[0].OffsetBytes);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 2);
        // カリング 1 回 + 記録を書く計算 1 回
        assert(commandList.DispatchCount == 2);
        assert(scene.Raster.GetLastFrameStats().MegaCommandSlots == 2);
        assert(scene.Raster.GetLastFrameStats().TotalSlots == 3);

        ShutdownVisibilityRasterScene(scene);
    }

    // --visibility-buffer=off: 描画の写しを作らず、MegaGeometry のバッファは今まで通りその場で Common へ戻る
    void TestVisibilityRasterOffKeepsExistingMegaGeometryRecording()
    {
        VisibilityRasterScene scene;
        RunVisibilityRasterScene(scene, false, true);
        FakeCommandList& commandList = scene.CommandList;

        assert(!scene.Mega.IsVisibilityDrawPlanEnabled());
        assert(commandList.BeginRenderPassCount == 3); // GBuffer（空）・MegaGeometry 2 パス
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.DispatchCount == 9);        // カリング 2 回 + HZB 7 段
        MegaGeometryPass::VisibilityDrawPlan plan;
        assert(!scene.Mega.TakeVisibilityDrawPlan(plan));

        ShutdownVisibilityRasterScene(scene);
    }

    // GBuffer の深度が無い構成（RenderGraph に深度が無い）では、VisibilityRasterPass は何も宣言せず何も描かない
    void TestVisibilityRasterWithoutGBufferDepthDoesNothing()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableVisibilityBufferCapabilities();
        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        VisibilityRasterPass pass;
        assert(pass.Initialize(context));
        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);
        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);
        assert(!pass.GetLastFrameStats().bRendered);
        uint32_t version = 0;
        assert(!graph.TryGetNamedResourceVersion(RenderGraphResourceNames::VisBufferId, version));

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestShadowMapNativeDeclareImportsDepthOutput()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetShadowMapHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
        uint32_t shadowMapVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::ShadowMap, shadowMapVersion));
        assert(shadowMapVersion == 0);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetShadowMapHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::DepthWrite);
        assert(finalState == RHI::ResourceState::ShaderResource);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestShadowMapNativeExecuteRegistersBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;
        context.SharedResources = &sharedResources;

        ShadowMapPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr shadowMap = graphResources.GetTexture(pass.GetShadowMapHandle());
        assert(shadowMap.get() == pass.GetShadowMapTexture());
        RHI::TexturePtr exportedShadowMap;
        assert(result.TryGetTexture(RenderGraphResourceNames::ShadowMap, exportedShadowMap));
        assert(exportedShadowMap.get() == pass.GetShadowMapTexture());
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ShadowMap"));
        assert(sharedResources.GetTexturePtr("ShadowMap").get() == pass.GetShadowMapTexture());
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestNeuralDecodeNativeDeclareWritesLogicalCompletion()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        NeuralMaterialDecodePass pass;
        const uint32_t passIndex = graph.AddPass(&pass);

        ViewRenderContext context;
        assert(graph.Compile(context));
        assert(pass.GetDecodeCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        assert(graph.TryGetDeclaredPassAccess(passIndex, 0, resource, mode, state, finalState));
        assert(resource == pass.GetDecodeCompleteHandle());
        assert(mode == RGAccessMode::Write);
        assert(state == RHI::ResourceState::Common);
        assert(finalState == RHI::ResourceState::Common);
        assert(graph.GetCompiledBarriers().empty());
    }

    void TestNeuralDecodeNativeExecuteSkipsUnsupportedPath()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        RHI::DeviceCapabilities capabilities;
        capabilities.NeuralShaders.bSupported = false;

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.Capabilities = &capabilities;

        NeuralMaterialDecodePass pass;
        assert(pass.Initialize(context));
        assert(!pass.IsCooperativeVectorSupported());

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
    }

    void TestMegaGeometryNativeDeclareImportsPersistentBuffers()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t passIndex = graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(pass.GetIndirectDrawBufferHandle().IsValid());
        assert(pass.GetDrawCountBufferHandle().IsValid());
        assert(pass.GetMegaGeometryCompleteHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(passIndex) == 3);

        bool bHasIndirectBufferWrite = false;
        bool bHasDrawCountBufferWrite = false;
        bool bHasCompleteWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(passIndex, accessIndex, resource, mode, state, finalState));
            assert(mode == RGAccessMode::Write);
            assert(state == RHI::ResourceState::Common);
            assert(finalState == RHI::ResourceState::Common);

            if (resource == pass.GetIndirectDrawBufferHandle())
            {
                bHasIndirectBufferWrite = true;
            }
            else if (resource == pass.GetDrawCountBufferHandle())
            {
                bHasDrawCountBufferWrite = true;
            }
            else if (resource == pass.GetMegaGeometryCompleteHandle())
            {
                bHasCompleteWrite = true;
            }
        }

        RenderGraphResources graphResources(&graph);
        assert(graphResources.GetBuffer(pass.GetIndirectDrawBufferHandle()));
        assert(graphResources.GetBuffer(pass.GetDrawCountBufferHandle()));
        assert(bHasIndirectBufferWrite);
        assert(bHasDrawCountBufferWrite);
        assert(bHasCompleteWrite);
        assert(graph.GetCompiledBarriers().empty());

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeDeclareUsesNamedGBufferAttachments()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        GBufferPass gbufferPass;
        MegaGeometryPass megaGeometryPass;
        assert(megaGeometryPass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(megaGeometryPassIndex) == 9);

        auto hasAttachmentAccess = [&graph](uint32_t passIndex,
                                            RGResourceHandle expected,
                                            RGAttachmentKind expectedKind,
                                            RHI::ResourceState expectedState) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Read;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                bool bColorAttachmentLoadStore = false;
                RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
                RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
                RGAttachmentKind kind = RGAttachmentKind::Color;
                RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState,
                                                      &bColorAttachmentLoadStore,
                                                      &loadOp,
                                                      &storeOp,
                                                      &kind,
                                                      &mutability));

                if (resource == expected)
                {
                    const bool bExpectedColorLoadStore = expectedKind == RGAttachmentKind::Color;
                    return mode == RGAccessMode::Write &&
                           state == expectedState &&
                           finalState == RHI::ResourceState::ShaderResource &&
                           bColorAttachmentLoadStore == bExpectedColorLoadStore &&
                           loadOp == RHI::AttachmentLoadOp::Load &&
                           storeOp == RHI::AttachmentStoreOp::Store &&
                           kind == expectedKind &&
                           mutability == RGAttachmentMutability::Write;
                }
            }

            return false;
        };

        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetAlbedoHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetNormalHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetMaterialHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetEmissiveHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetVelocityHandle(),
                                   RGAttachmentKind::Color,
                                   RHI::ResourceState::RenderTarget));
        assert(hasAttachmentAccess(megaGeometryPassIndex,
                                   gbufferPass.GetDepthHandle(),
                                   RGAttachmentKind::DepthStencil,
                                   RHI::ResourceState::DepthWrite));

        uint32_t gbufferVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferAlbedo, gbufferVersion));
        assert(gbufferVersion == 1);
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferDepth, gbufferVersion));
        assert(gbufferVersion == 1);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == megaGeometryPassIndex);

        megaGeometryPass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeExecuteEnqueuesEmptyAttachmentPass()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        MegaGeometryPass megaGeometryPass;

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(megaGeometryPass.Initialize(context));

        graph.AddPass(&gbufferPass);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(megaGeometryPassIndex) == 9);
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        assert(graph.GetLastExecutedPassCount() == 2);
        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());

        bool bFoundMegaGeometryRenderPassDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bColorAttachmentStatesMatch = true;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bColorAttachmentStatesMatch =
                    bColorAttachmentStatesMatch &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.storeOp == RHI::AttachmentStoreOp::Store &&
                    attachment.initialState == RHI::ResourceState::RenderTarget &&
                    attachment.finalState == RHI::ResourceState::ShaderResource;
            }

            if (!bColorAttachmentStatesMatch)
            {
                continue;
            }

            assert(desc.depthStencilAttachment.loadOp == RHI::AttachmentLoadOp::Load);
            assert(desc.depthStencilAttachment.storeOp == RHI::AttachmentStoreOp::Store);
            assert(desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite);
            assert(desc.depthStencilAttachment.finalState == RHI::ResourceState::ShaderResource);
            bFoundMegaGeometryRenderPassDesc = true;
        }
        assert(bFoundMegaGeometryRenderPassDesc);

        megaGeometryPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryNativeExecuteRecreatesRenderPassWhenAttachmentStateModeChanges()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        SharedResourceRegistry sharedResources;
        auto legacyAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "LegacyCacheAlbedo"));
        auto legacyNormalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "LegacyCacheNormal"));
        auto legacyMaterialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "LegacyCacheMaterial"));
        auto legacyEmissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "LegacyCacheEmissive"));
        auto legacyDepthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "LegacyCacheDepth"));
        auto legacyVelocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "LegacyCacheVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", legacyAlbedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", legacyNormalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", legacyMaterialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", legacyEmissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", legacyDepthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", legacyVelocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "CacheModeMega";
        const auto megaMesh = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMesh.IsValid());
        // 区画への書き込みが GPU で完了したメッシュだけが描かれる
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxy;
        proxy.MegaMeshHandle = megaMesh;
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxy.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxy);

        MegaGeometryPass megaGeometryPass;
        ViewRenderContext context;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;
        context.Resources.MegaGeometry = &renderResources.MegaGeometry();
        assert(megaGeometryPass.Initialize(context));

        megaGeometryPass.Setup(context);
        assert(!device->CreatedRenderPassDescs.empty());

        bool bFoundLegacyMegaGeometryRenderPassDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bLegacyStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bLegacyStates =
                    bLegacyStates &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.initialState == RHI::ResourceState::ShaderResource;
            }
            bFoundLegacyMegaGeometryRenderPassDesc = bFoundLegacyMegaGeometryRenderPassDesc || bLegacyStates;
        }
        assert(bFoundLegacyMegaGeometryRenderPassDesc);

        const size_t legacyRenderPassDescCount = device->CreatedRenderPassDescs.size();

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&megaGeometryPass);

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;
        context.CommandList = &commandList;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);
        assert(device->CreatedRenderPassDescs.size() > legacyRenderPassDescCount);

        bool bFoundRecreatedRenderGraphRenderPassDesc = false;
        for (size_t descIndex = legacyRenderPassDescCount;
             descIndex < device->CreatedRenderPassDescs.size();
             ++descIndex)
        {
            const RHI::RenderPassDesc& desc = device->CreatedRenderPassDescs[descIndex];
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bRenderGraphStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                bRenderGraphStates =
                    bRenderGraphStates &&
                    attachment.loadOp == RHI::AttachmentLoadOp::Load &&
                    attachment.initialState == RHI::ResourceState::RenderTarget &&
                    attachment.finalState == RHI::ResourceState::ShaderResource;
            }
            bFoundRecreatedRenderGraphRenderPassDesc =
                bFoundRecreatedRenderGraphRenderPassDesc || bRenderGraphStates;
        }
        assert(bFoundRecreatedRenderGraphRenderPassDesc);

        megaGeometryPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestMegaGeometryPartialNamedGBufferFallsBackToLegacyAttachmentStates()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SharedResourceRegistry sharedResources;
        auto namedAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialNamedAlbedo"));
        auto fallbackAlbedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialFallbackAlbedo"));
        auto fallbackNormalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "PartialFallbackNormal"));
        auto fallbackMaterialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "PartialFallbackMaterial"));
        auto fallbackEmissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "PartialFallbackEmissive"));
        auto fallbackDepthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "PartialFallbackDepth"));
        auto fallbackVelocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "PartialFallbackVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", fallbackAlbedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", fallbackNormalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", fallbackMaterialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", fallbackEmissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", fallbackDepthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", fallbackVelocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "PartialFallbackMega";
        const auto megaMesh = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMesh.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxy;
        proxy.MegaMeshHandle = megaMesh;
        proxy.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxy.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxy);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        NamedGBufferAlbedoProducerPass albedoProducer(namedAlbedoTexture);
        MegaGeometryPass megaGeometryPass;
        graph.AddPass(&albedoProducer);
        const uint32_t megaGeometryPassIndex = graph.AddPass(&megaGeometryPass);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;
        context.Resources.MegaGeometry = &renderResources.MegaGeometry();
        assert(megaGeometryPass.Initialize(context));

        assert(graph.Compile(context));
        uint32_t gbufferAlbedoVersion = 99;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferAlbedo,
                                                gbufferAlbedoVersion));
        assert(gbufferAlbedoVersion == 0);

        bool bHasGBufferAttachmentAccess = false;
        for (uint32_t accessIndex = 0;
             accessIndex < graph.GetDeclaredPassAccessCount(megaGeometryPassIndex);
             ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            bool bAttachment = false;
            RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
            RGAttachmentKind kind = RGAttachmentKind::Color;
            RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;
            assert(graph.TryGetDeclaredPassAccess(megaGeometryPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState,
                                                  &bAttachment,
                                                  &loadOp,
                                                  &storeOp,
                                                  &kind,
                                                  &mutability));
            bHasGBufferAttachmentAccess = bHasGBufferAttachmentAccess || bAttachment;
        }
        assert(!bHasGBufferAttachmentAccess);

        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        bool bSawNamedAlbedoBarrier = false;
        for (const BarrierEvent& barrier : commandList.Barriers)
        {
            if (barrier.Texture != namedAlbedoTexture.get())
            {
                continue;
            }

            bSawNamedAlbedoBarrier = true;
        }
        assert(!bSawNamedAlbedoBarrier);

        bool bFoundLegacyAttachmentStateDesc = false;
        bool bFoundRenderGraphAttachmentStateDesc = false;
        for (const RHI::RenderPassDesc& desc : device->CreatedRenderPassDescs)
        {
            if (desc.colorAttachments.size() != 5 || !desc.hasDepthStencil)
            {
                continue;
            }

            bool bLegacyStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::ShaderResource;
            bool bRenderGraphStates = desc.depthStencilAttachment.initialState == RHI::ResourceState::DepthWrite;
            for (const RHI::AttachmentDesc& attachment : desc.colorAttachments)
            {
                if (attachment.loadOp != RHI::AttachmentLoadOp::Load)
                {
                    bLegacyStates = false;
                    bRenderGraphStates = false;
                    continue;
                }

                bLegacyStates =
                    bLegacyStates &&
                    attachment.initialState == RHI::ResourceState::ShaderResource;
                bRenderGraphStates =
                    bRenderGraphStates &&
                    attachment.initialState == RHI::ResourceState::RenderTarget;
            }

            bFoundLegacyAttachmentStateDesc = bFoundLegacyAttachmentStateDesc || bLegacyStates;
            bFoundRenderGraphAttachmentStateDesc =
                bFoundRenderGraphAttachmentStateDesc || bRenderGraphStates;
        }
        assert(bFoundLegacyAttachmentStateDesc);
        assert(!bFoundRenderGraphAttachmentStateDesc);

        megaGeometryPass.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    // フレームごとに、描くインスタンスの並び（プロキシ）を入れ替える関数。a・b は最初のフレームのプロキシ
    using MegaProxyScript = void (*)(uint32_t frameIndex,
                                     const MegaGeometryProxy &a,
                                     const MegaGeometryProxy &b,
                                     Container::VariableArray<MegaGeometryProxy> &outProxies);

    // 1フレームの記録が「前のフレームで見えた」ビットのバッファへ行った0埋め（範囲）とコピー（古いバッファからの引き継ぎ）
    struct MegaVisibilityFrameRecord
    {
        Container::VariableArray<FakeCommandList::VisibilityFill> Fills;
        Container::VariableArray<FakeCommandList::VisibilityCopy> Copies;
    };

    // 2つのMegaMeshインスタンスを持つパスのフレームコマンドを記録する（bOcclusionCulling=false は --mega-occlusion=off）。
    // frameCount 回記録し、script があればフレームごとにプロキシを入れ替える。outVisibilityFrames にはフレームごとの
    // 見えたビットの0埋めとコピーを入れる。bSeparateMaterials なら2つ目のメッシュだけ別の材質（ベースカラー）にする。
    void RecordMegaGeometryTwoInstances(bool bOcclusionCulling,
                                        FakeCommandList &commandList,
                                        float maxDepth = 1.0f,
                                        uint32_t frameCount = 1,
                                        MegaProxyScript script = nullptr,
                                        Container::VariableArray<MegaVisibilityFrameRecord> *outVisibilityFrames = nullptr,
                                        bool bSeparateMaterials = false)
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        RenderResources renderResources;
        assert(renderResources.Initialize(device));
        renderResources.MegaGeometry().SetOcclusionCullingEnabled(bOcclusionCulling);

        SharedResourceRegistry sharedResources;
        auto albedoTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "RecordAlbedo"));
        auto normalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RecordNormal"));
        auto materialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "RecordMaterial"));
        auto emissiveTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RecordEmissive"));
        auto depthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "RecordDepth"));
        // MegaGeometryPass は velocity も GBuffer へ書くため、velocity が無いとフレームバッファを作らず記録しない
        auto velocityTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16_FLOAT, "RecordVelocity"));
        sharedResources.RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", materialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Emissive", emissiveTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Velocity", velocityTexture);

        float vertices[12] = {
            0.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            0.0f, 1.0f, 0.0f, 1.0f};
        uint32_t indices[3] = {0, 1, 2};

        MegaGeometry::MeshCluster cluster;
        cluster.IndexOffset = 0;
        cluster.IndexCount = 3;
        cluster.VertexOffset = 0;
        cluster.VertexCount = 3;
        cluster.Bounds.CenterX = 0.5f;
        cluster.Bounds.CenterY = 0.5f;
        cluster.Bounds.CenterZ = 0.0f;
        cluster.Bounds.Radius = 0.75f;
        cluster.ConeAxisZ = 1.0f;
        cluster.ConeCutoff = 0.25f;

        MegaGeometry::MegaMeshCreateInfo createInfo;
        createInfo.VertexData = vertices;
        createInfo.VertexDataSize = sizeof(vertices);
        createInfo.VertexCount = 3;
        createInfo.VertexStride = 4 * sizeof(float);
        createInfo.IndexData = indices;
        createInfo.IndexCount = 3;
        createInfo.Clusters.push_back(cluster);
        createInfo.TotalBounds.CenterX = 0.5f;
        createInfo.TotalBounds.CenterY = 0.5f;
        createInfo.TotalBounds.CenterZ = 0.0f;
        createInfo.TotalBounds.Radius = 1.25f;
        createInfo.bBuildLODHierarchy = false;
        createInfo.DebugName = "RecordMegaA";
        const auto megaMeshA = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshA.IsValid());

        createInfo.DebugName = "RecordMegaB";
        if (bSeparateMaterials)
        {
            createInfo.Material.BaseColor[0] = 0.25f;
        }
        const auto megaMeshB = renderResources.MegaGeometry().CreateMegaMesh(createInfo);
        assert(megaMeshB.IsValid());
        assert(NorvesLib::Test::GeometryUpload::DrainGeometryUploads(renderResources));

        Container::VariableArray<MegaGeometryProxy> proxies;
        MegaGeometryProxy proxyA;
        proxyA.ObjectId = 1;
        proxyA.ComponentId = 10;
        proxyA.MegaMeshHandle = megaMeshA;
        proxyA.WorldTransform = NorvesLib::Math::Matrix4x4::Identity;
        proxyA.WorldBounds = createInfo.TotalBounds;
        proxies.push_back(proxyA);

        MegaGeometryProxy proxyB = proxyA;
        proxyB.ObjectId = 2;
        proxyB.ComponentId = 20;
        proxyB.MegaMeshHandle = megaMeshB;
        proxies.push_back(proxyB);

        ViewRenderContext context;
        context.Device = device.get();
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotMegaGeometryProxies = &proxies;

        MegaGeometryPass megaGeometryPass;
        assert(megaGeometryPass.Initialize(context));
        megaGeometryPass.Setup(context);

        CameraProxy camera;
        camera.Viewport.Width = 128.0f;
        camera.Viewport.Height = 64.0f;
        RHI::Viewport viewport;
        viewport.width = 128.0f;
        viewport.height = 64.0f;
        viewport.maxDepth = maxDepth;
        RHI::ScissorRect scissor;
        scissor.right = 128;
        scissor.bottom = 64;

        FrameCommand frameCommand = FrameCommand::CreateMegaGeometryPass(&megaGeometryPass,
                                                                         &renderResources.MegaGeometry(),
                                                                         camera,
                                                                         true,
                                                                         viewport,
                                                                         scissor,
                                                                         DebugViewMode::Normal);
        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            if (script)
            {
                script(frameIndex, proxyA, proxyB, proxies);
                megaGeometryPass.Setup(context);
            }

            const size_t fillsBefore = commandList.VisibilityFills.size();
            const size_t copiesBefore = commandList.VisibilityCopies.size();
            megaGeometryPass.RecordFrameCommand(frameCommand.MegaGeometry, &commandList);
            if (outVisibilityFrames)
            {
                MegaVisibilityFrameRecord frameRecord;
                for (size_t index = fillsBefore; index < commandList.VisibilityFills.size(); ++index)
                {
                    frameRecord.Fills.push_back(commandList.VisibilityFills[index]);
                }
                for (size_t index = copiesBefore; index < commandList.VisibilityCopies.size(); ++index)
                {
                    frameRecord.Copies.push_back(commandList.VisibilityCopies[index]);
                }
                outVisibilityFrames->push_back(frameRecord);
            }
        }

        megaGeometryPass.Shutdown();
        renderResources.Shutdown();
        shaderManager.Shutdown();
    }

    // 遮蔽カリングを使わない（--mega-occlusion=off）と、全インスタンスを1回のカリングで選び、1回のrender passで描く。
    // 同じ材質・同じプールの塊のインスタンスは1つの区間にまとまり、区間ごとに1回の間接描画になる
    void TestMegaGeometryRecordFrameCommandBatchesInstancesInSingleRenderPass()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(false, commandList);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 1);

        // 並び: 全インスタンスのカリング（1回）→ render pass 1つの中で区間ごとに1回描く
        const char expected[] = {'D', 'B', 'I', 'E'};
        assert(commandList.CallSequence.size() == sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(commandList.CallSequence[i] == expected[i]);
        }

        // 区間のコマンドの最大数は、区間のインスタンスのクラスタ数の合計（1 + 1）
        assert(commandList.IndirectDraws.size() == 1);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 2);
    }

    // 材質が違うインスタンスは別の区間になり、区間ごとに1回の間接描画を発行する。カリングは材質によらず全インスタンスで1回
    void TestMegaGeometryRecordFrameCommandSplitsSectionsByMaterial()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(false, commandList, 1.0f, 1, nullptr, nullptr, true);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 2);

        const char expected[] = {'D', 'B', 'I', 'I', 'E'};
        assert(commandList.CallSequence.size() == sizeof(expected));
        for (size_t i = 0; i < sizeof(expected); ++i)
        {
            assert(commandList.CallSequence[i] == expected[i]);
        }

        // 区間のコマンドは連続した範囲（1つ目の区間のクラスタ数 1 の次から2つ目）。コマンドは20バイト
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 1);
        assert(commandList.IndirectDraws[1].OffsetBytes == 20);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 1);
    }

    // インスタンスの表（1つの storage buffer）: 並びはインスタンスの並び。ワークグループ・区間・見えたビットの区画・
    // 頂点とインデックスの基点（プールの塊の先頭から）が入る
    void TestMegaGeometryRecordFrameCommandWritesInstanceTable()
    {
        GMegaInstanceTableUpdates.clear();
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 1, nullptr, nullptr, true);

        assert(GMegaInstanceTableUpdates.size() == 1);
        const Container::VariableArray<uint8_t> &bytes = GMegaInstanceTableUpdates[0];
        // MegaGeometryPass::GPUMegaInstance は 192 バイト（world 0・previousWorld 64・LODSphere 128・
        // クラスタ配列のアドレス下位/上位・クラスタ数・最初のワークグループ 144〜156・区間・頂点の基点・インデックスの基点・見えたビットの先頭 160〜172・
        // グループの BVH の節の配列のアドレス下位/上位・節の数・予約 176〜188）
        constexpr size_t InstanceBytes = 192;
        assert(bytes.size() == 2 * InstanceBytes);
        auto readUint = [&bytes](size_t instanceIndex, size_t byteOffset) -> uint32_t
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes.data() + instanceIndex * InstanceBytes + byteOffset, sizeof(value));
            return value;
        };

        for (size_t instanceIndex = 0; instanceIndex < 2; ++instanceIndex)
        {
            assert((readUint(instanceIndex, 144) | readUint(instanceIndex, 148)) != 0); // クラスタ配列のアドレス
            assert(readUint(instanceIndex, 152) == 1);                                  // クラスタ数
            // 1クラスタ = 1ワークグループなので、最初のワークグループの通し番号はインスタンスの番号
            assert(readUint(instanceIndex, 156) == instanceIndex);
            // 材質が違うので区間も別
            assert(readUint(instanceIndex, 160) == instanceIndex);
            // 見えたビットは、全インスタンスで1本の配列にクラスタ数ずつ詰める
            assert(readUint(instanceIndex, 172) == instanceIndex);
        }
        // 2つのメッシュは同じプールの塊の中にあり、2つ目は1つ目の後ろ
        assert(readUint(0, 164) < readUint(1, 164));
        assert(readUint(0, 168) < readUint(1, 168));
    }

    // 遮蔽カリング（既定）は2パス: 1パス目のカリングと描画 → HZBの生成 → 2パス目のカリングと描画。
    // どちらのパスも、カリングは全インスタンスで1回、描画は区間ごとに1回
    void TestMegaGeometryRecordFrameCommandRecordsTwoPassOcclusion()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList);

        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        // 同じ材質の2インスタンスは1つの区間なので、1パス目と2パス目で1回ずつ描く
        assert(commandList.DrawCallCount == 2);

        // 並び: [1パス目のカリング] [render pass: 描画] [HZBの各ミップ（Dispatchだけ）] [2パス目のカリング] [render pass: 描画]
        const auto &sequence = commandList.CallSequence;
        const char head[] = {'D', 'B', 'I', 'E'};
        const char tail[] = {'D', 'B', 'I', 'E'};
        assert(sequence.size() > sizeof(head) + sizeof(tail));
        for (size_t i = 0; i < sizeof(head); ++i)
        {
            assert(sequence[i] == head[i]);
        }
        for (size_t i = 0; i < sizeof(tail); ++i)
        {
            assert(sequence[sequence.size() - sizeof(tail) + i] == tail[i]);
        }
        // 間は HZB の生成で、描画なしのディスパッチだけ（128x64 の深度は、ミップ0 が 64x32 の7段）
        const size_t middleBegin = sizeof(head);
        const size_t middleEnd = sequence.size() - sizeof(tail);
        assert(middleEnd - middleBegin == 7);
        for (size_t i = middleBegin; i < middleEnd; ++i)
        {
            assert(sequence[i] == 'D');
        }

        // コマンドの範囲はパスごとに別（2パス目は1パス目の範囲の後ろ。1パスのコマンド数 2、1コマンド20バイト）
        assert(commandList.IndirectDraws.size() == 2);
        assert(commandList.IndirectDraws[0].OffsetBytes == 0);
        assert(commandList.IndirectDraws[0].MaxDrawCount == 2);
        assert(commandList.IndirectDraws[1].OffsetBytes == 2 * 20);
        assert(commandList.IndirectDraws[1].MaxDrawCount == 2);
    }

    // 深度の範囲が 0〜1 でないと、保存される深度は NDC の深度と一致せず HZB の判定が成り立たないので、従来の経路で描く
    void TestMegaGeometryTwoPassFallsBackWhenDepthRangeIsNotUnit()
    {
        FakeCommandList commandList;
        RecordMegaGeometryTwoInstances(true, commandList, 0.5f);

        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DispatchCount == 1);
        assert(commandList.DrawCallCount == 1);
    }

    // 並びは A（ObjectId 1）と B（ObjectId 2）。見えたビットは、直前のフレームにも描かれた同じコンポーネントのインスタンスだけが引き継ぐ
    void MegaVisibilityReaddScript(uint32_t frameIndex,
                                   const MegaGeometryProxy &a,
                                   const MegaGeometryProxy &b,
                                   Container::VariableArray<MegaGeometryProxy> &outProxies)
    {
        outProxies.clear();
        MegaGeometryProxy readded = a;
        // 3・4: 同じ ObjectId・メッシュで新しいコンポーネント（ComponentId 11）。5: 描かれ続けたまま ComponentId だけ変わる。
        // 7: 描かれなかった後の再追加（ComponentId は5と同じ）
        if (frameIndex == 3 || frameIndex == 4)
        {
            readded.ComponentId = 11;
        }
        else if (frameIndex >= 5)
        {
            readded.ComponentId = 12;
        }
        const bool bHasA = frameIndex != 2 && frameIndex != 6;
        if (bHasA)
        {
            outProxies.push_back(readded);
        }
        outProxies.push_back(b);
    }

    // 追加されたインスタンス・再追加・コンポーネントの作り直しでは、前のフレームで見えたビットを捨てる（0で埋め直す）。
    // 配置（インスタンスの並びとクラスタ数）が変わると、ぴったりの大きさで作り直して0で埋め、描かれ続けたインスタンスの
    // 区画だけを古いバッファから写す。1インスタンスの区画は 1 クラスタぶん（4バイト）
    void TestMegaGeometryTwoPassDiscardsVisibilityOnReaddAndComponentChange()
    {
        FakeCommandList commandList;
        Container::VariableArray<MegaVisibilityFrameRecord> frames;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 8, &MegaVisibilityReaddScript, &frames);

        assert(frames.size() == 8);

        auto expectWholeFill = [](const MegaVisibilityFrameRecord &frame, uint64_t sizeBytes) -> void
        {
            assert(frame.Fills.size() == 1);
            assert(frame.Fills[0].OffsetBytes == 0);
            assert(frame.Fills[0].SizeBytes == sizeBytes);
        };
        auto expectCarriedOver = [](const MegaVisibilityFrameRecord &frame, uint64_t sourceOffset, uint64_t destinationOffset) -> void
        {
            assert(frame.Copies.size() == 1);
            assert(frame.Copies[0].SourceOffsetBytes == sourceOffset);
            assert(frame.Copies[0].DestinationOffsetBytes == destinationOffset);
            assert(frame.Copies[0].SizeBytes == sizeof(uint32_t));
        };

        // 0: A・B が新規 → 作って0で埋める（引き継ぎ無し）
        expectWholeFill(frames[0], 2 * sizeof(uint32_t));
        assert(frames[0].Copies.empty());
        // 1: 配置が同じで、2つとも引き継ぐ → 何もしない
        assert(frames[1].Fills.empty());
        assert(frames[1].Copies.empty());
        // 2: A が外れる → [B] へ作り直し、B の区画（古い位置 4）を先頭へ写す
        expectWholeFill(frames[2], sizeof(uint32_t));
        expectCarriedOver(frames[2], 1 * sizeof(uint32_t), 0);
        // 3: A を新しいコンポーネントで再追加 → [A, B] へ作り直し、B（古い位置 0）を2番目へ写す。A は0から
        expectWholeFill(frames[3], 2 * sizeof(uint32_t));
        expectCarriedOver(frames[3], 0, 1 * sizeof(uint32_t));
        // 4: 引き継ぐ → 何もしない
        assert(frames[4].Fills.empty());
        assert(frames[4].Copies.empty());
        // 5: 描かれ続けたまま ComponentId だけ変わる → 配置は同じで、A の区画（先頭の4バイト）だけ0に戻す。B は触らない
        assert(frames[5].Fills.size() == 1);
        assert(frames[5].Fills[0].OffsetBytes == 0);
        assert(frames[5].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[5].Copies.empty());
        // 6: A が外れる → [B] へ作り直し、B を写す
        expectWholeFill(frames[6], sizeof(uint32_t));
        expectCarriedOver(frames[6], 1 * sizeof(uint32_t), 0);
        // 7: 描かれなかった後の再追加 → [A, B] へ作り直し、B だけ写す（A は同じ ComponentId でも0から）
        expectWholeFill(frames[7], 2 * sizeof(uint32_t));
        expectCarriedOver(frames[7], 0, 1 * sizeof(uint32_t));
    }

    // 全インスタンスが一度消える（A → 空 → A）。同じ ObjectId・ComponentId・メッシュでも、再追加でビットを捨てる
    void MegaVisibilityEmptyGapScript(uint32_t frameIndex,
                                      const MegaGeometryProxy &a,
                                      const MegaGeometryProxy &,
                                      Container::VariableArray<MegaGeometryProxy> &outProxies)
    {
        outProxies.clear();
        if (frameIndex != 1)
        {
            outProxies.push_back(a);
        }
    }

    // 空のフレームは記録を省くので、記録の中だけで連続性を見ると再追加が引き継ぎに見える。Setup で脱落を検出して捨てる
    void TestMegaGeometryTwoPassDiscardsVisibilityWhenAllInstancesVanish()
    {
        FakeCommandList commandList;
        Container::VariableArray<MegaVisibilityFrameRecord> frames;
        RecordMegaGeometryTwoInstances(true, commandList, 1.0f, 3, &MegaVisibilityEmptyGapScript, &frames);

        assert(frames.size() == 3);
        // 0: A が新規 → 作って0で埋める
        assert(frames[0].Fills.size() == 1);
        assert(frames[0].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[0].Copies.empty());
        // 1: 空（記録なし）
        assert(frames[1].Fills.empty());
        assert(frames[1].Copies.empty());
        // 2: A を同じ ObjectId・ComponentId・メッシュで再追加 → 配置は同じでも引き継がず、A の区画を0に戻す
        assert(frames[2].Fills.size() == 1);
        assert(frames[2].Fills[0].OffsetBytes == 0);
        assert(frames[2].Fills[0].SizeBytes == sizeof(uint32_t));
        assert(frames[2].Copies.empty());
    }

    void TestMegaGeometryNativeExecuteSkipsWhenNoInstances()
    {
        auto device = RHI::MakeShared<FakeDevice>();
        device->EnableMegaGeometryBatchCapabilities();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        FakeCommandList commandList;
        ViewRenderContext context;
        context.Device = device.get();
        context.CommandList = &commandList;
        context.ShaderMgr = &shaderManager;

        MegaGeometryPass pass;
        assert(pass.Initialize(context));

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        graph.AddPass(&pass);

        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.BeginRenderPassCount == 0);
        assert(commandList.DrawCallCount == 0);

        pass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeDeclareCreatesTransientOutputs()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass pass;
        graph.AddPass(&pass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(pass.GetAlbedoHandle().IsValid());
        assert(pass.GetNormalHandle().IsValid());
        assert(pass.GetMaterialHandle().IsValid());
        assert(pass.GetEmissiveHandle().IsValid());
        assert(pass.GetVelocityHandle().IsValid());
        assert(pass.GetDepthHandle().IsValid());

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 6);
        for (uint32_t i = 0; i < 5; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == 0);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[5].Kind == RGBarrierKind::Texture);
        assert(barriers[5].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[5].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[5].PassIndex == 0);
        assert(barriers[5].CompiledOrderIndex == 0);
    }

    void TestGBufferSSAONativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAORawHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);

        bool bHasDepthRead = false;
        bool bHasNormalRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssaoPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Write;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssaoPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
        }
        assert(bHasDepthRead);
        assert(bHasNormalRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 8);
        for (uint32_t i = 0; i < 5; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == gbufferPassIndex);
            assert(barriers[i].CompiledOrderIndex == 0);
        }
        assert(barriers[5].Kind == RGBarrierKind::Texture);
        assert(barriers[5].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[5].AfterState == RHI::ResourceState::DepthWrite);
        assert(barriers[5].PassIndex == gbufferPassIndex);
        assert(barriers[5].CompiledOrderIndex == 0);

        assert(barriers[6].Kind == RGBarrierKind::Texture);
        assert(barriers[6].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[6].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[6].PassIndex == ssaoPassIndex);
        assert(barriers[6].CompiledOrderIndex == 1);
        assert(barriers[7].Kind == RGBarrierKind::Texture);
        assert(barriers[7].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[7].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[7].PassIndex == ssaoPassIndex);
        assert(barriers[7].CompiledOrderIndex == 1);
    }

    void TestGBufferSSAOLightingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(lightingPass.GetIndirectSpecularHandle().IsValid());
        assert(lightingPass.GetSpecularReflectanceHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 11);

        bool bHasAlbedoRead = false;
        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasEmissiveRead = false;
        bool bHasSSAORead = false;
        bool bHasSceneColorWrite = false;
        bool bHasIndirectSpecularWrite = false;
        bool bHasSpecularReflectanceWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(lightingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(lightingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetAlbedoHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasAlbedoRead = true;
            }
            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == gbufferPass.GetEmissiveHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasEmissiveRead = true;
            }
            if (resource == ssaoPass.GetSSAOBlurredHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSSAORead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorWrite = true;
            }
            // SSRPassが読む環境光の鏡面反射とその反射率も、描画先として書いて読める状態で終える
            if (resource == lightingPass.GetIndirectSpecularHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasIndirectSpecularWrite = true;
            }
            if (resource == lightingPass.GetSpecularReflectanceHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSpecularReflectanceWrite = true;
            }
        }

        assert(bHasAlbedoRead);
        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasEmissiveRead);
        assert(bHasSSAORead);
        assert(bHasSceneColorWrite);
        assert(bHasIndirectSpecularWrite);
        assert(bHasSpecularReflectanceWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 12);
        assert(barriers[8].Kind == RGBarrierKind::Texture);
        assert(barriers[8].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[8].AfterState == RHI::ResourceState::UnorderedAccess);
        assert(barriers[8].PassIndex == lightingPassIndex);
        assert(barriers[8].CompiledOrderIndex == 2);
        assert(barriers[9].Kind == RGBarrierKind::Texture);
        assert(barriers[9].BeforeState == RHI::ResourceState::Undefined);
        assert(barriers[9].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[9].PassIndex == lightingPassIndex);
        assert(barriers[9].CompiledOrderIndex == 2);
        for (size_t i = 10; i < 12; ++i)
        {
            assert(barriers[i].Kind == RGBarrierKind::Texture);
            assert(barriers[i].BeforeState == RHI::ResourceState::Undefined);
            assert(barriers[i].AfterState == RHI::ResourceState::RenderTarget);
            assert(barriers[i].PassIndex == lightingPassIndex);
            assert(barriers[i].CompiledOrderIndex == 2);
        }
    }

    void TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(gbufferPass.GetDepthHandle().IsValid());
        assert(gbufferPass.GetNormalHandle().IsValid());
        assert(ssaoPass.GetSSAOBlurredHandle().IsValid());
        assert(lightingPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssaoPassIndex) == 4);
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 11);

        auto hasShaderReadAccess = [&graph](uint32_t passIndex, RGResourceHandle expected) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Write;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == RGAccessMode::Read &&
                           state == RHI::ResourceState::ShaderResource &&
                           finalState == RHI::ResourceState::ShaderResource;
                }
            }

            return false;
        };

        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(ssaoPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetAlbedoHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetNormalHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetMaterialHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetDepthHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, gbufferPass.GetEmissiveHandle()));
        assert(hasShaderReadAccess(lightingPassIndex, ssaoPass.GetSSAOBlurredHandle()));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
    }

    void TestLightingNativeDeclareReadsNamedShadowMap()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        ShadowMapPass shadowMapPass;
        assert(shadowMapPass.Initialize(context));
        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t shadowMapPassIndex = graph.AddPass(&shadowMapPass);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(lightingPassIndex) == 12);

        bool bHasShadowMapRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(lightingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Write;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(lightingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));
            if (resource == shadowMapPass.GetShadowMapHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasShadowMapRead = true;
            }
        }

        assert(bHasShadowMapRead);

        const auto& order = graph.GetCompiledPassOrder();
        uint32_t shadowMapOrder = RGInvalidPassIndex;
        uint32_t lightingOrder = RGInvalidPassIndex;
        for (uint32_t orderIndex = 0; orderIndex < order.size(); ++orderIndex)
        {
            if (order[orderIndex] == shadowMapPassIndex)
            {
                shadowMapOrder = orderIndex;
            }
            if (order[orderIndex] == lightingPassIndex)
            {
                lightingOrder = orderIndex;
            }
        }

        assert(shadowMapOrder != RGInvalidPassIndex);
        assert(lightingOrder != RGInvalidPassIndex);
        assert(shadowMapOrder < lightingOrder);

        shadowMapPass.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingSceneDepthAliasDuplicateFailsCompile()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        RGResourceHandle prepublishedSceneDepth;
        PublishSceneDepthAliasPass prepublishPass(&prepublishedSceneDepth);
        LightingPass lightingPass;

        graph.AddPass(&gbufferPass);
        graph.AddPass(&prepublishPass);
        graph.AddPass(&lightingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(!graph.Compile(context));
        assert(prepublishedSceneDepth.IsValid());
    }

    void TestForwardTransparentNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);

        bool bHasSceneColorLoadStore = false;
        bool bHasDepthRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(forwardPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            bool bColorAttachmentLoadStore = false;
            RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
            RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
            assert(graph.TryGetDeclaredPassAccess(forwardPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState,
                                                  &bColorAttachmentLoadStore,
                                                  &loadOp,
                                                  &storeOp));

            if (resource == lightingPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                assert(bColorAttachmentLoadStore);
                assert(loadOp == RHI::AttachmentLoadOp::Load);
                assert(storeOp == RHI::AttachmentStoreOp::Store);
                bHasSceneColorLoadStore = true;
            }

            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::DepthRead);
                assert(finalState == RHI::ResourceState::DepthRead);
                assert(!bColorAttachmentLoadStore);
                bHasDepthRead = true;
            }
        }

        assert(bHasSceneColorLoadStore);
        assert(bHasDepthRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 14);
        assert(barriers[12].Kind == RGBarrierKind::Texture);
        assert(barriers[12].Resource == lightingPass.GetSceneColorHandle());
        assert(barriers[12].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[12].AfterState == RHI::ResourceState::RenderTarget);
        assert(barriers[12].PassIndex == forwardPassIndex);
        assert(barriers[12].CompiledOrderIndex == 3);
        assert(barriers[13].Kind == RGBarrierKind::Texture);
        assert(barriers[13].Resource == gbufferPass.GetDepthHandle());
        assert(barriers[13].BeforeState == RHI::ResourceState::ShaderResource);
        assert(barriers[13].AfterState == RHI::ResourceState::DepthRead);
        assert(barriers[13].PassIndex == forwardPassIndex);
        assert(barriers[13].CompiledOrderIndex == 3);
    }

    void TestSSRNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(ssrPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 7);

        bool bHasNormalRead = false;
        bool bHasMaterialRead = false;
        bool bHasDepthRead = false;
        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        bool bHasIndirectSpecularRead = false;
        bool bHasSpecularReflectanceRead = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(ssrPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(ssrPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == gbufferPass.GetNormalHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasNormalRead = true;
            }
            if (resource == gbufferPass.GetMaterialHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasMaterialRead = true;
            }
            if (resource == gbufferPass.GetDepthHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasDepthRead = true;
            }
            if (resource == lightingPass.GetSceneColorHandle() && mode == RGAccessMode::Read)
            {
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }
            // 環境光の鏡面反射とその反射率（LightingPassの出力）をシェーダーから読む
            if (resource == lightingPass.GetIndirectSpecularHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasIndirectSpecularRead = true;
            }
            if (resource == lightingPass.GetSpecularReflectanceHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSpecularReflectanceRead = true;
            }
            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasNormalRead);
        assert(bHasMaterialRead);
        assert(bHasDepthRead);
        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);
        assert(bHasIndirectSpecularRead);
        assert(bHasSpecularReflectanceRead);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 5);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);

        bool bHasDepthToShaderResource = false;
        bool bHasSSROutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != ssrPassIndex)
            {
                continue;
            }

            if (barrier.Resource == gbufferPass.GetDepthHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::DepthRead);
                assert(barrier.AfterState == RHI::ResourceState::ShaderResource);
                bHasDepthToShaderResource = true;
            }

            if (barrier.Resource == ssrPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasSSROutputWrite = true;
            }
        }

        assert(bHasDepthToShaderResource);
        assert(bHasSSROutputWrite);
    }

    void TestBloomNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(bloomPass.GetSceneColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(bloomPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(bloomPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == ssrPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 6);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);

        bool bHasBloomOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != bloomPassIndex)
            {
                continue;
            }

            if (barrier.Resource == bloomPass.GetSceneColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasBloomOutputWrite = true;
            }
        }

        assert(bHasBloomOutputWrite);
    }

    void TestToneMappingNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        bool bHasSceneColorRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(toneMappingPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(toneMappingPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == bloomPass.GetSceneColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasSceneColorRead = true;
            }

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasSceneColorRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);

        bool bHasToneMappedOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != toneMappingPassIndex)
            {
                continue;
            }

            if (barrier.Resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasToneMappedOutputWrite = true;
            }
        }

        assert(bHasToneMappedOutputWrite);
    }

    void TestVignetteNativeDeclareDependencies()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(vignettePass.GetToneMappedColorHandle() != toneMappingPass.GetToneMappedColorHandle());
        assert(graph.GetDeclaredPassAccessCount(vignettePassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(vignettePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(vignettePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);

        RGCompiledResourceLifetime vignetteLifetime;
        assert(graph.TryGetCompiledResourceLifetime(vignettePass.GetToneMappedColorHandle(),
                                                    vignetteLifetime));
        assert(vignetteLifetime.bExported);
        assert(vignetteLifetime.bPinnedUntilGraphEnd);
        assert(vignetteLifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());

        bool bHasVignetteOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != vignettePassIndex)
            {
                continue;
            }

            if (barrier.Resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasVignetteOutputWrite = true;
            }
        }

        assert(bHasVignetteOutputWrite);
#endif
    }

    void TestToneMappedVersionChainFeedsFXAAAndUpscale()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;
        FXAAPass fxaaPass;
        UpscalePass upscalePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(fxaaPass.GetToneMappedColorHandle().IsValid());
        assert(upscalePass.GetPresentationColorHandle().IsValid());

        bool bFXAAReadsVignette = false;
        bool bFXAAReadsToneMapping = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(fxaaPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(fxaaPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bFXAAReadsVignette = true;
            }

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                bFXAAReadsToneMapping = true;
            }
        }

        assert(bFXAAReadsVignette);
        assert(!bFXAAReadsToneMapping);

        bool bUpscaleReadsFXAA = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bUpscaleReadsFXAA = true;
            }
        }

        assert(bUpscaleReadsFXAA);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 4);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);
        assert(order[2] == fxaaPassIndex);
        assert(order[3] == upscalePassIndex);
#endif
    }

    void TestToneMappedVersionChainFeedsUpscaleWithoutFXAA()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        ToneMappingPass toneMappingPass;
        VignettePass vignettePass;
        UpscalePass upscalePass;

        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t vignettePassIndex = graph.AddPass(&vignettePass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(vignettePass.GetToneMappedColorHandle().IsValid());
        assert(upscalePass.GetPresentationColorHandle().IsValid());

        bool bUpscaleReadsVignette = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == vignettePass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bUpscaleReadsVignette = true;
            }
        }

        assert(bUpscaleReadsVignette);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 3);
        assert(order[0] == toneMappingPassIndex);
        assert(order[1] == vignettePassIndex);
        assert(order[2] == upscalePassIndex);
#endif
    }

    void TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        SSRPass ssrPass;
        BloomPass bloomPass;
        ToneMappingPass toneMappingPass;

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(forwardPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(ssrPassIndex) == 7);
        assert(graph.GetDeclaredPassAccessCount(bloomPassIndex) == 2);
        assert(graph.GetDeclaredPassAccessCount(toneMappingPassIndex) == 2);

        auto hasAccess = [&graph](uint32_t passIndex,
                                  RGResourceHandle expected,
                                  RGAccessMode expectedMode,
                                  RHI::ResourceState expectedState,
                                  RHI::ResourceState expectedFinalState) -> bool
        {
            for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(passIndex); ++accessIndex)
            {
                RGResourceHandle resource;
                RGAccessMode mode = RGAccessMode::Read;
                RHI::ResourceState state = RHI::ResourceState::Undefined;
                RHI::ResourceState finalState = RHI::ResourceState::Undefined;
                assert(graph.TryGetDeclaredPassAccess(passIndex,
                                                      accessIndex,
                                                      resource,
                                                      mode,
                                                      state,
                                                      finalState));
                if (resource == expected)
                {
                    return mode == expectedMode &&
                           state == expectedState &&
                           finalState == expectedFinalState;
                }
            }

            return false;
        };

        assert(hasAccess(forwardPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Write,
                         RHI::ResourceState::RenderTarget,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(ssrPassIndex,
                         lightingPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(bloomPassIndex,
                         ssrPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));
        assert(hasAccess(toneMappingPassIndex,
                         bloomPass.GetSceneColorHandle(),
                         RGAccessMode::Read,
                         RHI::ResourceState::ShaderResource,
                         RHI::ResourceState::ShaderResource));

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 7);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
    }

    void TestPostProcessMissingNamedInputsCompileWithoutErrors()
    {
        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ForwardPass forwardPass(nullptr, nullptr);
            forwardPass.SetTransparentOnly(true);
            const uint32_t passIndex = graph.AddPass(&forwardPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 0);
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            SSRPass ssrPass;
            const uint32_t passIndex = graph.AddPass(&ssrPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(ssrPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BloomPass bloomPass;
            const uint32_t passIndex = graph.AddPass(&bloomPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(bloomPass.GetSceneColorHandle().IsValid());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            ToneMappingPass toneMappingPass;
            const uint32_t passIndex = graph.AddPass(&toneMappingPass);

            assert(graph.Compile(context));
            assert(graph.GetDeclaredPassAccessCount(passIndex) == 1);
            assert(toneMappingPass.GetToneMappedColorHandle().IsValid());
        }
    }

    void TestFXAANativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(fxaaPass.GetToneMappedColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(fxaaPassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasOutputWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(fxaaPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(fxaaPassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == toneMappingPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasOutputWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasOutputWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 8);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);

        bool bHasFXAAOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != fxaaPassIndex)
            {
                continue;
            }

            if (barrier.Resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasFXAAOutputWrite = true;
            }
        }

        assert(bHasFXAAOutputWrite);
    }

    void TestUpscaleNativeDeclareDependencies()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        GBufferPass gbufferPass;
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(nullptr, nullptr);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t ssaoPassIndex = graph.AddPass(&ssaoPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        const uint32_t forwardPassIndex = graph.AddPass(&forwardPass);
        const uint32_t ssrPassIndex = graph.AddPass(&ssrPass);
        const uint32_t bloomPassIndex = graph.AddPass(&bloomPass);
        const uint32_t toneMappingPassIndex = graph.AddPass(&toneMappingPass);
        const uint32_t fxaaPassIndex = graph.AddPass(&fxaaPass);
        const uint32_t upscalePassIndex = graph.AddPass(&upscalePass);

        ViewRenderContext context;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;

        assert(graph.Compile(context));
        assert(upscalePass.GetPresentationColorHandle().IsValid());
        assert(graph.GetDeclaredPassAccessCount(upscalePassIndex) == 2);

        bool bHasToneMappedRead = false;
        bool bHasPresentationWrite = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(upscalePassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(upscalePassIndex,
                                                  accessIndex,
                                                  resource,
                                                  mode,
                                                  state,
                                                  finalState));

            if (resource == fxaaPass.GetToneMappedColorHandle())
            {
                assert(mode == RGAccessMode::Read);
                assert(state == RHI::ResourceState::ShaderResource);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasToneMappedRead = true;
            }

            if (resource == upscalePass.GetPresentationColorHandle())
            {
                assert(mode == RGAccessMode::Write);
                assert(state == RHI::ResourceState::RenderTarget);
                assert(finalState == RHI::ResourceState::ShaderResource);
                bHasPresentationWrite = true;
            }
        }

        assert(bHasToneMappedRead);
        assert(bHasPresentationWrite);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 9);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == ssaoPassIndex);
        assert(order[2] == lightingPassIndex);
        assert(order[3] == forwardPassIndex);
        assert(order[4] == ssrPassIndex);
        assert(order[5] == bloomPassIndex);
        assert(order[6] == toneMappingPassIndex);
        assert(order[7] == fxaaPassIndex);
        assert(order[8] == upscalePassIndex);

        RGCompiledResourceLifetime presentationLifetime;
        assert(graph.TryGetCompiledResourceLifetime(upscalePass.GetPresentationColorHandle(),
                                                    presentationLifetime));
        assert(presentationLifetime.bExported);
        assert(presentationLifetime.bPinnedUntilGraphEnd);
        assert(presentationLifetime.LifetimeEndOrderIndex == graph.GetCompiledPassOrder().size());

        bool bHasPresentationOutputWrite = false;
        for (const RGCompiledBarrier& barrier : graph.GetCompiledBarriers())
        {
            if (barrier.PassIndex != upscalePassIndex)
            {
                continue;
            }

            if (barrier.Resource == upscalePass.GetPresentationColorHandle())
            {
                assert(barrier.BeforeState == RHI::ResourceState::Undefined);
                assert(barrier.AfterState == RHI::ResourceState::RenderTarget);
                bHasPresentationOutputWrite = true;
            }
        }

        assert(bHasPresentationOutputWrite);
    }

    void TestGBufferSSAOLightingNativeExecuteWithoutSharedResources()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr sceneColorTexture = graphResources.GetTexture(lightingPass.GetSceneColorHandle());
        assert(sceneColorTexture);
        assert(graph.GetLastExecutedPassCount() == 3);
        assert(result.TextureOutputs.size() == 1);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::SceneColor, exportedTexture));
        assert(exportedTexture.get() == sceneColorTexture.get());
        exportedTexture.reset();
        assert(!result.TryGetTexture(RenderGraphResourceNames::SceneDepth, exportedTexture));
        assert(exportedTexture == nullptr);
        // 環境光の鏡面反射とその反射率は、SceneColorと同じ描画で書く（書き出さず、同じRenderGraphの中で読む）
        assert(graphResources.GetTexture(lightingPass.GetIndirectSpecularHandle()));
        assert(graphResources.GetTexture(lightingPass.GetSpecularReflectanceHandle()));
        assert(commandList.Barriers.size() == 12);
        assert(commandList.BeginRenderPassCount == 4);
        assert(commandList.EndRenderPassCount == 4);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    LightProxy MakeLightingBufferPointLight(uint32_t index)
    {
        LightProxy proxy;
        proxy.LightId = index + 1;
        proxy.Type = LightType::Point;
        proxy.PositionX = 1.0f + static_cast<float>(index);
        proxy.PositionY = 2.0f + static_cast<float>(index);
        proxy.PositionZ = 3.0f + static_cast<float>(index);
        proxy.DirectionX = -0.1f;
        proxy.DirectionY = -0.2f;
        proxy.DirectionZ = -0.3f;
        proxy.InnerConeAngle = 0.81f;
        proxy.OuterConeAngle = 0.62f;
        proxy.ColorR = 1.0f;
        proxy.ColorG = 1.0f;
        proxy.ColorB = 1.0f;
        proxy.Intensity = 2.0f + static_cast<float>(index);
        proxy.Range = 25.0f + static_cast<float>(index);
        proxy.bVisible = true;
        return proxy;
    }

    RHI::ResourceBindType FindDescriptorBindingType(const RHI::DescriptorSetDesc& desc,
                                                    uint32_t binding)
    {
        for (const RHI::DescriptorBinding& descriptorBinding : desc.bindings)
        {
            if (descriptorBinding.binding == binding)
            {
                return descriptorBinding.type;
            }
        }

        assert(false);
        return RHI::ResourceBindType::ConstantBuffer;
    }

    void AssertBinding5IsStructuredBuffer(const FakeDevice& device)
    {
        assert(FindDescriptorBindingType(device.LastDescriptorSetDesc, 5) ==
               RHI::ResourceBindType::StructuredBuffer);
        assert(!device.LastGraphicsPipelineDescriptorSetLayouts.empty());
        assert(FindDescriptorBindingType(device.LastGraphicsPipelineDescriptorSetLayouts[0], 5) ==
               RHI::ResourceBindType::StructuredBuffer);
    }

    const BufferCreationRecord& FindLastLightArraySSBOCreation(const FakeDevice& device)
    {
        for (size_t i = device.CreatedBuffers.size(); i > 0; --i)
        {
            const BufferCreationRecord& record = device.CreatedBuffers[i - 1];
            if (IsDebugName(record.Desc.DebugName, "LightArraySSBO"))
            {
                return record;
            }
        }

        assert(false);
        return device.CreatedBuffers[0];
    }

    uint32_t CountLightArraySSBOCreations(const FakeDevice& device)
    {
        uint32_t count = 0;
        for (const BufferCreationRecord& record : device.CreatedBuffers)
        {
            if (IsDebugName(record.Desc.DebugName, "LightArraySSBO"))
            {
                ++count;
            }
        }

        return count;
    }

    bool HasRenderEvent(FakeRenderEvent event)
    {
        for (FakeRenderEvent recordedEvent : GRenderEvents)
        {
            if (recordedEvent == event)
            {
                return true;
            }
        }

        return false;
    }

    void DecodeLightingParams(GPULightingParams& outParams)
    {
        assert(GLastDescriptorBinding4UpdateBytes.size() == sizeof(GPULightingParams));
        std::memcpy(&outParams, GLastDescriptorBinding4UpdateBytes.data(), sizeof(GPULightingParams));
    }

    float ReadPackedLightFloat(const GPULightData& light, size_t byteOffset)
    {
        float value = 0.0f;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&light);
        std::memcpy(&value, bytes + byteOffset, sizeof(value));
        return value;
    }

    FakeBuffer& GetBoundLightArrayBuffer()
    {
        assert(GLastDescriptorBinding5Buffer != nullptr);
        return *static_cast<FakeBuffer*>(GLastDescriptorBinding5Buffer);
    }

    GPULightData DecodeLightDataAt(uint32_t index)
    {
        const size_t offset = static_cast<size_t>(index) * sizeof(GPULightData);
        assert(GLastDescriptorBinding5UpdateBytes.size() >= offset + sizeof(GPULightData));

        GPULightData light = {};
        std::memcpy(&light, GLastDescriptorBinding5UpdateBytes.data() + offset, sizeof(GPULightData));
        return light;
    }

    void ExecuteLightingGraphWithLights(FakeDevice& device,
                                        ShaderManager& shaderManager,
                                        RHI::TransientResourcePool& pool,
                                        RenderResources& renderResources,
                                        SceneRenderer& renderer,
                                        LightingPass& lightingPass,
                                        Container::VariableArray<LightProxy>& lightProxies,
                                        FakeCommandList& commandList,
                                        uint64_t frameIndex)
    {
        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(frameIndex);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&lightingPass);

        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = &device;
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotLightProxies = &lightProxies;
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        ResetLightingDescriptorCapture();
        assert(graph.Compile(context));
        assert(graph.Execute(context));
        assert(pendingFrameCommands.empty());

        gbufferPass.Shutdown();
        graph.Shutdown();
    }

    void TestLightingNativeExecuteBindsExpandedLightStorageBuffer()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;
        Container::VariableArray<LightProxy> lightProxies;
        for (uint32_t i = 0; i < 20; ++i)
        {
            lightProxies.push_back(MakeLightingBufferPointLight(i));
        }

        FakeCommandList commandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       lightProxies,
                                       commandList,
                                       0);

        const uint32_t expectedBytes = 20u * static_cast<uint32_t>(sizeof(GPULightData));
        const BufferCreationRecord& lightBufferRecord = FindLastLightArraySSBOCreation(*device);
        assert(lightBufferRecord.Desc.Size >= expectedBytes);
        assert(HasUsage(lightBufferRecord.Desc.Usage, RHI::ResourceUsage::StorageBuffer));
        assert(HasUsage(lightBufferRecord.Desc.Usage, RHI::ResourceUsage::ShaderRead));
        assert(lightBufferRecord.Desc.CPUAccessible);
        AssertBinding5IsStructuredBuffer(*device);

        assert(GLastDescriptorBinding5Buffer != nullptr);
        assert(GLastDescriptorBinding5Offset == 0);
        assert(GLastDescriptorBinding5Size >= expectedBytes);
        assert(GLastDescriptorBinding5UpdateBytes.size() == expectedBytes);

        GPULightingParams params = {};
        DecodeLightingParams(params);
        assert(params.lightCount == 20);
        assert(params.ddgi.info[0] == 0u);
        assert(params.ddgi.probeCounts[3] == 0u);

        const GPULightData first = DecodeLightDataAt(0);
        const GPULightData last = DecodeLightDataAt(19);
        assert(first.position[0] == lightProxies[0].PositionX);
        assert(first.position[1] == lightProxies[0].PositionY);
        assert(first.position[2] == lightProxies[0].PositionZ);
        assert(first.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(first.direction[3] == lightProxies[0].InnerConeAngle);
        assert(ReadPackedLightFloat(first, 44) == lightProxies[0].Intensity);
        assert(ReadPackedLightFloat(first, 48) == lightProxies[0].Range);
        assert(ReadPackedLightFloat(first, 52) == lightProxies[0].OuterConeAngle);
        assert(last.position[0] == lightProxies[19].PositionX);
        assert(ReadPackedLightFloat(last, 32) == 1.0f);
        assert(ReadPackedLightFloat(last, 44) == lightProxies[19].Intensity);
        assert(ReadPackedLightFloat(last, 48) == lightProxies[19].Range);

        const uint32_t ssboUpdateIndex = FindRenderEventIndex(FakeRenderEvent::LightSsboUpdate);
        const uint32_t bindStorageIndex = FindRenderEventIndex(FakeRenderEvent::BindStorageBuffer5);
        const uint32_t descriptorUpdateIndex =
            FindRenderEventIndexAfter(FakeRenderEvent::DescriptorSetUpdate, bindStorageIndex);
        const uint32_t commandSetDescriptorIndex =
            FindRenderEventIndexAfter(FakeRenderEvent::CommandSetDescriptorSet, descriptorUpdateIndex);
        assert(ssboUpdateIndex < bindStorageIndex);
        assert(bindStorageIndex < descriptorUpdateIndex);
        assert(descriptorUpdateIndex < commandSetDescriptorIndex);

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteZeroLightsKeepsLogicalCountZeroAndSkipsSsboUpdate()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;
        Container::VariableArray<LightProxy> lightProxies;
        FakeCommandList commandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       lightProxies,
                                       commandList,
                                       0);

        GPULightingParams params = {};
        DecodeLightingParams(params);
        assert(params.lightCount == 0);

        assert(GLastDescriptorBinding5Buffer != nullptr);
        assert(GLastDescriptorBinding5Size >= sizeof(GPULightData));
        const FakeBuffer& lightBuffer = GetBoundLightArrayBuffer();
        assert(lightBuffer.GetSize() >= sizeof(GPULightData));
        assert(lightBuffer.UpdateCallCount == 0);
        assert(GLastDescriptorBinding5UpdateBytes.empty());
        assert(!HasRenderEvent(FakeRenderEvent::LightSsboUpdate));
        assert(commandList.DrawCallCount > 0);
        assert(HasRenderEvent(FakeRenderEvent::Draw));

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingLightBufferGrowthRetainsRetiredBuffersAndReusesCapacity()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;

        Container::VariableArray<LightProxy> oneLight;
        oneLight.push_back(MakeLightingBufferPointLight(0));
        FakeCommandList firstCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       oneLight,
                                       firstCommandList,
                                       0);
        const uint32_t firstSsboCount = CountLightArraySSBOCreations(*device);
        assert(firstSsboCount >= 1);
        RHI::TSharedPtr<FakeBufferLifetimeTracker> firstTracker =
            FindLastLightArraySSBOCreation(*device).Tracker;
        assert(firstTracker);
        assert(!firstTracker->bDestroyed);

        Container::VariableArray<LightProxy> twentyLights;
        for (uint32_t i = 0; i < 20; ++i)
        {
            twentyLights.push_back(MakeLightingBufferPointLight(i));
        }
        FakeCommandList secondCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twentyLights,
                                       secondCommandList,
                                       1);
        const uint32_t grownSsboCount = CountLightArraySSBOCreations(*device);
        assert(grownSsboCount > firstSsboCount);
        RHI::TSharedPtr<FakeBufferLifetimeTracker> grownTracker =
            FindLastLightArraySSBOCreation(*device).Tracker;
        assert(grownTracker);
        assert(!firstTracker->bDestroyed);
        assert(!grownTracker->bDestroyed);

        Container::VariableArray<LightProxy> twelveLights;
        for (uint32_t i = 0; i < 12; ++i)
        {
            twelveLights.push_back(MakeLightingBufferPointLight(i));
        }
        FakeCommandList thirdCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twelveLights,
                                       thirdCommandList,
                                       2);
        assert(CountLightArraySSBOCreations(*device) == grownSsboCount);

        lightingPass.Shutdown();
        assert(firstTracker->bDestroyed);
        assert(grownTracker->bDestroyed);

        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingLightBufferGrowthFailureSkipsDescriptorUpdateAndDraw()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        LightingPass lightingPass;

        Container::VariableArray<LightProxy> oneLight;
        oneLight.push_back(MakeLightingBufferPointLight(0));
        FakeCommandList firstCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       oneLight,
                                       firstCommandList,
                                       0);

        device->FailLightArraySSBOCreateIndex = device->LightArraySSBOCreateCount + 1;

        Container::VariableArray<LightProxy> twentyLights;
        for (uint32_t i = 0; i < 20; ++i)
        {
            twentyLights.push_back(MakeLightingBufferPointLight(i));
        }

        FakeCommandList secondCommandList;
        ExecuteLightingGraphWithLights(*device,
                                       shaderManager,
                                       pool,
                                       renderResources,
                                       renderer,
                                       lightingPass,
                                       twentyLights,
                                       secondCommandList,
                                       1);

        assert(GLastDescriptorBinding5Buffer == nullptr);
        assert(!HasRenderEvent(FakeRenderEvent::BindStorageBuffer5));
        assert(!HasRenderEvent(FakeRenderEvent::DescriptorSetUpdate));
        assert(!HasRenderEvent(FakeRenderEvent::CommandSetDescriptorSet));
        assert(!HasRenderEvent(FakeRenderEvent::Draw));
        assert(secondCommandList.DrawCallCount == 0);

        lightingPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestProductionDeferredNativeExecuteSkipsLegacyBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        ShadowMapPass shadowMapPass;
        shadowMapPass.SetSceneView(&sceneView);
        shadowMapPass.SetSceneRenderer(&renderer);
        shadowMapPass.SetRegisterLegacyBridge(false);

        GBufferPass gbufferPass;
        gbufferPass.SetSceneView(&sceneView);
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);

        SSAOPass ssaoPass;

        LightingPass lightingPass;
        lightingPass.SetSceneView(&sceneView);
        lightingPass.SetRegisterLegacyBridge(false);

        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);

        SSRPass ssrPass;
        BloomPass bloomPass;
        ToneMappingPass toneMappingPass;
        FXAAPass fxaaPass;
        UpscalePass upscalePass;

        graph.AddPass(&shadowMapPass);
        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        transparentCommands.push_back(DrawCommand{});
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        assert(graph.GetLastExecutedPassCount() == 10);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ShadowMap"));
        assert(!sharedResources.HasTexture("GBuffer_Albedo"));
        assert(!sharedResources.HasTexture("GBuffer_Normal"));
        assert(!sharedResources.HasTexture("GBuffer_Material"));
        assert(!sharedResources.HasTexture("GBuffer_Emissive"));
        assert(!sharedResources.HasTexture("GBuffer_Depth"));
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("SceneDepth"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        shadowMapPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestForwardTransparentNativeExecuteSkipsBridgeWhenRegisterOutputsDisabled()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        lightingPass.SetRegisterLegacyBridge(false);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        transparentCommands.push_back(DrawCommand{});
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 4);
        assert(commandList.Barriers.size() == 14);
        assert(commandList.BeginRenderPassCount == 5);
        assert(commandList.EndRenderPassCount == 5);
        assert(commandList.DrawCallCount == 3);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("GBuffer_Depth"));
        assert(!sharedResources.HasTexture("SceneDepth"));

        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 5);
        assert(commandList.Barriers.size() == 16);
        assert(commandList.BeginRenderPassCount == 6);
        assert(commandList.EndRenderPassCount == 6);
        assert(commandList.DrawCallCount == 4);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);

        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 6);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));

        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == toneMappedOutput.get());

        assert(graph.GetLastExecutedPassCount() == 7);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestVignetteNativeExecuteExportsToneMappedColorWithoutBridge()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        VignettePass vignettePass;

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&vignettePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr vignetteOutput = graphResources.GetTexture(vignettePass.GetToneMappedColorHandle());
        assert(vignetteOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == vignetteOutput.get());

        assert(graph.GetLastExecutedPassCount() == 8);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        vignettePass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
#endif
    }

    void TestFXAANativeExecuteExportsToneMappedColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 8);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("ToneMappedColor"));

        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        SSAOPass ssaoPass;
        ssaoPass.SetGBufferPass(&gbufferPass);
        LightingPass lightingPass;
        lightingPass.SetGBufferPass(&gbufferPass);
        lightingPass.SetSSAOPass(&ssaoPass);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        forwardPass.SetLightingPass(&lightingPass);
        forwardPass.SetGBufferPass(&gbufferPass);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 256;
        context.ScreenHeight = 128;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == presentationOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSRPass ssrPass;
        graph.AddPass(&ssrPass);

        RHI::TexturePtr normalTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal"));
        RHI::TexturePtr materialTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackMaterial"));
        RHI::TexturePtr depthTexture = device->CreateTexture(
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth"));
        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(normalTexture);
        assert(materialTexture);
        assert(depthTexture);
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Material", materialTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr ssrOutput = graphResources.GetTexture(ssrPass.GetSceneColorHandle());
        assert(ssrOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == ssrOutput.get());

        ssrPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        BloomPass bloomPass;
        graph.AddPass(&bloomPass);

        RHI::TexturePtr sceneColorTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackSceneColor"));
        assert(sceneColorTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("SceneColor", sceneColorTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr bloomOutput = graphResources.GetTexture(bloomPass.GetSceneColorHandle());
        assert(bloomOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("SceneColor"));
        assert(sharedResources.GetTexturePtr("SceneColor").get() == bloomOutput.get());

        bloomPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestVignetteNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        assert(NORVES_HAS_VIGNETTE_PASS == 1);

#if NORVES_HAS_VIGNETTE_PASS
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        VignettePass vignettePass;
        graph.AddPass(&vignettePass);

        RHI::TexturePtr toneMappedTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackToneMappedColor"));
        assert(toneMappedTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ToneMappedColor", toneMappedTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr vignetteOutput = graphResources.GetTexture(vignettePass.GetToneMappedColorHandle());
        assert(vignetteOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == vignetteOutput.get());

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == vignetteOutput.get());

        vignettePass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
#endif
    }

    void TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        FXAAPass fxaaPass;
        graph.AddPass(&fxaaPass);

        RHI::TexturePtr toneMappedTexture = device->CreateTexture(
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackToneMappedColor"));
        assert(toneMappedTexture);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ToneMappedColor", toneMappedTexture);
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::ToneMappedColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == fxaaOutput.get());

        fxaaPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SceneView sceneView;
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        gbufferPass.SetRegisterLegacyBridge(false);
        SSAOPass ssaoPass;
        LightingPass lightingPass;
        lightingPass.SetRegisterLegacyBridge(false);
        ForwardPass forwardPass(&sceneView, &renderer);
        forwardPass.SetTransparentOnly(true);
        forwardPass.SetRegisterOutputs(false);
        SSRPass ssrPass;
        ssrPass.SetGBufferPass(&gbufferPass);
        ssrPass.SetLightingPass(&lightingPass);
        BloomPass bloomPass;
        bloomPass.SetInputPass(&ssrPass);
        ToneMappingPass toneMappingPass;
        toneMappingPass.SetInputPass(&bloomPass);
        FXAAPass fxaaPass;
        fxaaPass.SetInputPass(&toneMappingPass);
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&fxaaPass);

        graph.AddPass(&gbufferPass);
        graph.AddPass(&ssaoPass);
        graph.AddPass(&lightingPass);
        graph.AddPass(&forwardPass);
        graph.AddPass(&ssrPass);
        graph.AddPass(&bloomPass);
        graph.AddPass(&toneMappingPass);
        graph.AddPass(&fxaaPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<DrawCommand> transparentCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.SnapshotTransparentCommands = DrawCommandView::FromArray(transparentCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr fxaaOutput = graphResources.GetTexture(fxaaPass.GetToneMappedColorHandle());
        assert(fxaaOutput);
        RHI::TexturePtr presentationOutput = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationOutput.get() == fxaaOutput.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == fxaaOutput.get());

        assert(graph.GetLastExecutedPassCount() == 9);
        assert(commandList.BeginRenderPassCount == commandList.EndRenderPassCount);
        assert(commandList.BeginRenderPassCount > 0);
        assert(commandList.DrawCallCount > 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("PresentationColor"));

        upscalePass.Shutdown();
        fxaaPass.Shutdown();
        toneMappingPass.Shutdown();
        bloomPass.Shutdown();
        ssrPass.Shutdown();
        forwardPass.Shutdown();
        lightingPass.Shutdown();
        ssaoPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LegacyFXAAProducerPass legacyFXAAPass;
        UpscalePass upscalePass;
        upscalePass.SetInputPass(&legacyFXAAPass);

        graph.AddPass(&legacyFXAAPass);
        graph.AddPass(&upscalePass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.ScreenWidth = 128;
        context.ScreenHeight = 64;

        assert(graph.Compile(context));
        uint32_t toneMappedVersion = 0;
        assert(!graph.TryGetNamedResourceVersion(RenderGraphResourceNames::ToneMappedColor, toneMappedVersion));
        RenderGraphExecutionResult result = graph.ExecuteWithResult(context);
        assert(result.bSuccess);

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr inputTexture = graphResources.GetTexture(legacyFXAAPass.GetToneMappedColorHandle());
        assert(inputTexture);
        RHI::TexturePtr presentationTexture = graphResources.GetTexture(upscalePass.GetPresentationColorHandle());
        assert(presentationTexture.get() == inputTexture.get());
        RHI::TexturePtr exportedTexture;
        assert(result.TryGetTexture(RenderGraphResourceNames::PresentationColor, exportedTexture));
        assert(exportedTexture.get() == inputTexture.get());

        assert(graph.GetLastExecutedPassCount() == 2);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("PresentationColor"));
        assert(sharedResources.GetTexturePtr("PresentationColor").get() == inputTexture.get());

        upscalePass.Shutdown();
        legacyFXAAPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestToneMappingNativeExecuteAcceptsRawSceneColorBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        ToneMappingPass toneMappingPass;
        graph.AddPass(&toneMappingPass);

        RHI::TextureDesc rawSceneColorDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "RawForwardSceneColor");
        RHI::TexturePtr rawSceneColor = device->CreateTexture(rawSceneColorDesc);
        assert(rawSceneColor);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexture("SceneColor", rawSceneColor.get());
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr toneMappedOutput = graphResources.GetTexture(toneMappingPass.GetToneMappedColorHandle());
        assert(toneMappedOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(sharedResources.HasTexture("ToneMappedColor"));
        assert(sharedResources.GetTexturePtr("ToneMappedColor").get() == toneMappedOutput.get());

        toneMappingPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = nullptr;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        // SceneColorと、SSRPassへ渡す環境光の鏡面反射・反射率の3枚を消して読める状態へ移す（反射率0でSSRは何も足さない）
        assert(commandList.Barriers.size() == 4);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecutePrefersNamedShadowMapOverRegistry()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RHI::TextureDesc namedShadowDesc =
            RHI::TextureDesc::DepthStencil(128, 128, RHI::Format::D32_FLOAT, "NamedShadowMap");
        namedShadowDesc.ArraySize = PhysicalLightingShadowCascadeCount;
        RHI::TextureDesc legacyShadowDesc =
            RHI::TextureDesc::DepthStencil(128, 128, RHI::Format::D32_FLOAT, "LegacyShadowMap");
        RHI::TexturePtr namedShadowMap = device->CreateTexture(namedShadowDesc);
        RHI::TexturePtr legacyShadowMap = device->CreateTexture(legacyShadowDesc);
        assert(namedShadowMap);
        assert(legacyShadowMap);

        SharedResourceRegistry sharedResources;
        sharedResources.RegisterTexturePtr("ShadowMap", legacyShadowMap);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        RGResourceHandle namedShadowHandle;
        NamedShadowMapProducerPass shadowProducer(namedShadowMap, &namedShadowHandle);
        GBufferPass gbufferPass;
        gbufferPass.SetSceneRenderer(&renderer);
        LightingPass lightingPass;

        const uint32_t shadowPassIndex = graph.AddPass(&shadowProducer);
        graph.AddPass(&gbufferPass);
        const uint32_t lightingPassIndex = graph.AddPass(&lightingPass);
        assert(graph.AddDependency(shadowPassIndex, lightingPassIndex));

        FakeCommandList commandList;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        GLastDescriptorBinding6Texture = nullptr;
        assert(graph.Compile(context));
        assert(namedShadowHandle.IsValid());
        assert(graph.Execute(context));

        assert(GLastDescriptorBinding6Texture == namedShadowMap.get());
        assert(GLastDescriptorBinding6Texture != legacyShadowMap.get());
        assert(sharedResources.GetTexturePtr("ShadowMap").get() == legacyShadowMap.get());
        assert(commandList.BeginRenderPassCount == 2);
        assert(commandList.EndRenderPassCount == 2);
        assert(commandList.DrawCallCount == 1);
        assert(pendingFrameCommands.empty());

        lightingPass.Shutdown();
        gbufferPass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        LightingPass lightingPass;
        graph.AddPass(&lightingPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc albedoDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R8G8B8A8_UNORM, "FallbackAlbedo");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TexturePtr albedoTexture = device->CreateTexture(albedoDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        assert(albedoTexture);
        assert(normalTexture);
        assert(depthTexture);

        sharedResources.RegisterTexturePtr("GBuffer_Albedo", albedoTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 4);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(!sharedResources.HasTexture("SceneColor"));
        assert(!sharedResources.HasTexture("SceneDepth"));
        assert(!sharedResources.HasTexture("LightingIndirectSpecular"));
        assert(!sharedResources.HasTexture("LightingSpecularReflectance"));

        lightingPass.Shutdown();
        renderer.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        SceneRenderer renderer;
        assert(renderer.Initialize(device.get(), nullptr, &pool));

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        GBufferPass pass;
        pass.SetSceneRenderer(&renderer);
        graph.AddPass(&pass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<DrawCommand> opaqueCommands;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.Renderer = &renderer;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;
        context.SnapshotOpaqueCommands = DrawCommandView::FromArray(opaqueCommands);
        context.Resources.Textures = &renderResources.Textures();
        context.Resources.Materials = &renderResources.Materials();
        context.Resources.Meshes = &renderResources.Meshes();

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 6);
        assert(commandList.BeginRenderPassCount == 1);
        assert(commandList.EndRenderPassCount == 1);
        assert(commandList.DrawCallCount == 0);
        assert(pendingFrameCommands.empty());
        assert(sharedResources.HasTexture("GBuffer_Albedo"));
        assert(sharedResources.HasTexture("GBuffer_Normal"));
        assert(sharedResources.HasTexture("GBuffer_Material"));
        assert(sharedResources.HasTexture("GBuffer_Emissive"));
        assert(sharedResources.HasTexture("GBuffer_Velocity"));
        assert(sharedResources.HasTexture("GBuffer_Depth"));

        pass.Shutdown();
        renderer.Shutdown();
        renderResources.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        SSAOPass ssaoPass;
        graph.AddPass(&ssaoPass);

        FakeCommandList commandList;
        SharedResourceRegistry sharedResources;
        Container::VariableArray<FrameCommand> pendingFrameCommands;

        RHI::TextureDesc depthDesc =
            RHI::TextureDesc::DepthStencil(128, 64, RHI::Format::D32_FLOAT, "FallbackDepth");
        RHI::TextureDesc normalDesc =
            RHI::TextureDesc::RenderTarget(128, 64, RHI::Format::R16G16B16A16_FLOAT, "FallbackNormal");
        RHI::TexturePtr depthTexture = device->CreateTexture(depthDesc);
        RHI::TexturePtr normalTexture = device->CreateTexture(normalDesc);
        assert(depthTexture);
        assert(normalTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Depth", depthTexture);
        sharedResources.RegisterTexturePtr("GBuffer_Normal", normalTexture);

        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.SharedResources = &sharedResources;
        context.ShaderMgr = &shaderManager;
        context.PendingFrameCommands = &pendingFrameCommands;
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RenderGraphResources graphResources(&graph);
        RHI::TexturePtr blurredOutput = graphResources.GetTexture(ssaoPass.GetSSAOBlurredHandle());
        assert(blurredOutput);

        assert(graph.GetLastExecutedPassCount() == 1);
        assert(commandList.Barriers.size() == 2);
        assert(sharedResources.HasTexture("SSAO"));
        assert(sharedResources.GetTexturePtr("SSAO").get() == blurredOutput.get());

        ssaoPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }

    void TestRecordMeshDrawCallUsesDrawCommandRangesAndFallback()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        RenderResources renderResources;
        assert(renderResources.Initialize(device));

        MeshDataHandle meshHandle;
        meshHandle.Id = 4401;
        const float vertices[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
        const uint32_t indices[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6};
        assert(renderResources.Meshes().Register(meshHandle, vertices, sizeof(vertices), indices, 12));

        SceneRenderer renderer;
        FakeCommandList commandList;

        DrawCommand firstSubMesh;
        firstSubMesh.Draw.MeshHandle = meshHandle;
        firstSubMesh.Draw.IndexOffset = 3;
        firstSubMesh.Draw.IndexCount = 6;
        firstSubMesh.Draw.VertexOffset = 2;
        assert(renderer.RecordMeshDrawCall(firstSubMesh, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 6);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 3);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 2);

        DrawCommand secondSubMesh;
        secondSubMesh.Draw.MeshHandle = meshHandle;
        secondSubMesh.Draw.IndexOffset = 9;
        secondSubMesh.Draw.IndexCount = 3;
        secondSubMesh.Draw.VertexOffset = 5;
        assert(renderer.RecordMeshDrawCall(secondSubMesh, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 3);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 9);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 5);

        DrawCommand fallback;
        fallback.Draw.MeshHandle = meshHandle;
        fallback.Draw.IndexOffset = 9;
        fallback.Draw.IndexCount = 0;
        fallback.Draw.VertexOffset = 5;
        assert(renderer.RecordMeshDrawCall(fallback, &commandList, &renderResources.Meshes(), nullptr));
        assert(commandList.LastDrawIndexedInstancedIndexCount == 12);
        assert(commandList.LastDrawIndexedInstancedStartIndexLocation == 0);
        assert(commandList.LastDrawIndexedInstancedBaseVertexLocation == 0);

        assert(commandList.DrawCallCount == 3);
        assert(renderer.GetStats().DrawCallCount == 3);
        assert(renderer.GetStats().TriangleCount == 7);

        renderResources.Shutdown();
    }

    // GBuffer 深度を読むだけの HZB のパス。深度の Load/Store の宣言には触れず（版が増えず）、GBuffer の後ろに並ぶ。
    void TestHiZPyramidNativeDeclareReadsGBufferDepthWithoutWritingIt()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ViewRenderContext context;
        context.Device = device.get();
        context.RenderWidth = 128;
        context.RenderHeight = 64;

        GBufferPass gbufferPass;
        HiZPyramidPass hizPass;

        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t gbufferPassIndex = graph.AddPass(&gbufferPass);
        const uint32_t hizPassIndex = graph.AddPass(&hizPass);

        assert(graph.Compile(context));

        // 深度の読み取りと、完了を表す論理資源の書き込みだけ
        assert(graph.GetDeclaredPassAccessCount(hizPassIndex) == 2);
        bool bReadsDepth = false;
        bool bWritesComplete = false;
        for (uint32_t accessIndex = 0; accessIndex < graph.GetDeclaredPassAccessCount(hizPassIndex); ++accessIndex)
        {
            RGResourceHandle resource;
            RGAccessMode mode = RGAccessMode::Read;
            RHI::ResourceState state = RHI::ResourceState::Undefined;
            RHI::ResourceState finalState = RHI::ResourceState::Undefined;
            assert(graph.TryGetDeclaredPassAccess(hizPassIndex, accessIndex, resource, mode, state, finalState));
            if (resource == gbufferPass.GetDepthHandle())
            {
                bReadsDepth = mode == RGAccessMode::Read && state == RHI::ResourceState::ShaderResource;
            }
            else if (resource == hizPass.GetCompleteHandle())
            {
                bWritesComplete = mode == RGAccessMode::Write;
            }
        }
        assert(bReadsDepth);
        assert(bWritesComplete);

        uint32_t depthVersion = 0;
        assert(graph.TryGetNamedResourceVersion(RenderGraphResourceNames::GBufferDepth, depthVersion));
        // GBufferPass が作った版（0）のまま。書き込みなら版が進む
        assert(depthVersion == 0);

        const auto& order = graph.GetCompiledPassOrder();
        assert(order.size() == 2);
        assert(order[0] == gbufferPassIndex);
        assert(order[1] == hizPassIndex);
    }

    // 深度の名前付き資源が無い構成では、何も宣言せず（完了の論理資源も作らず）、ピラミッドも作らない。
    void TestHiZPyramidNativeDeclareWithoutDepthDeclaresNothing()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ViewRenderContext context;
        context.Device = device.get();

        HiZPyramidPass hizPass;
        RenderGraph graph;
        assert(graph.Initialize(nullptr));
        const uint32_t hizPassIndex = graph.AddPass(&hizPass);
        assert(graph.Compile(context));
        assert(graph.GetDeclaredPassAccessCount(hizPassIndex) == 0);
        assert(!hizPass.GetCompleteHandle().IsValid());
        assert(!hizPass.GetPyramidTexture());
    }

    // 深度を作るだけの何もしないパス（HZB のパスの実行を確かめるための入力）
    class HiZTestDepthProducerPass final : public IRenderGraphPass
    {
    public:
        HiZTestDepthProducerPass(uint32_t width, uint32_t height) : m_Width(width), m_Height(height) {}

        const char* GetName() const override { return "HiZTestDepthProducerPass"; }

        void Declare(RenderGraphBuilder& builder) override
        {
            builder.WriteTextureAttachment(RenderGraphResourceNames::GBufferDepth,
                                           RGTextureDesc::DepthStencil(m_Width, m_Height, RHI::Format::D32_FLOAT, "HiZTestDepth"),
                                           RGAttachmentKind::DepthStencil,
                                           RHI::AttachmentLoadOp::Clear,
                                           RHI::AttachmentStoreOp::Store,
                                           RHI::ResourceState::DepthWrite,
                                           RHI::ResourceState::ShaderResource);
            builder.PreserveInsertionOrder();
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        uint32_t m_Width;
        uint32_t m_Height;
    };

    // 奇数の深度（37x23）から、半分（切り上げ）の 19x12・5 段の HZB を、全ミップ UnorderedAccess のまま作って
    // 最後に ShaderResource へ遷移する。ミップごとに 1 つ前の書き込みを待つバリアを 1 つずつ置き、ディスパッチは 5 回。
    void TestHiZPyramidNativeExecuteBuildsAllMipsInOneSubmission()
    {
        auto device = RHI::MakeShared<FakeDevice>();

        ShaderManager shaderManager;
        assert(shaderManager.Initialize(device.get(), TestShaderDirectory));

        MockAllocator allocator;
        RHI::TransientResourcePool pool;
        assert(pool.Initialize(&allocator, 1));
        pool.BeginFrame(0);

        RenderGraph graph;
        assert(graph.Initialize(&pool));
        graph.BeginFrame(0);

        HiZTestDepthProducerPass depthPass(37, 23);
        HiZPyramidPass hizPass;
        graph.AddPass(&depthPass);
        graph.AddPass(&hizPass);

        FakeCommandList commandList;
        ViewRenderContext context;
        context.CommandList = &commandList;
        context.Device = device.get();
        context.TransientPool = &pool;
        context.ShaderMgr = &shaderManager;
        context.RenderWidth = 37;
        context.RenderHeight = 23;

        assert(graph.Compile(context));
        assert(graph.Execute(context));

        RHI::TexturePtr pyramid = hizPass.GetPyramidTexture();
        assert(pyramid);
        assert(pyramid->GetWidth() == 19);
        assert(pyramid->GetHeight() == 12);
        assert(pyramid->GetMipLevels() == 5);
        assert(hizPass.GetMipCount() == 5);
        assert(commandList.DispatchCount == 5);

        Container::VariableArray<BarrierEvent> pyramidBarriers;
        for (const BarrierEvent& event : commandList.Barriers)
        {
            if (event.Kind == RGBarrierKind::Texture && event.Texture == pyramid.get())
            {
                pyramidBarriers.push_back(event);
            }
        }
        assert(pyramidBarriers.size() == 6);
        assert(pyramidBarriers[0].BeforeState == RHI::ResourceState::Undefined);
        assert(pyramidBarriers[0].AfterState == RHI::ResourceState::UnorderedAccess);
        for (size_t index = 1; index < 5; ++index)
        {
            assert(pyramidBarriers[index].BeforeState == RHI::ResourceState::UnorderedAccess);
            assert(pyramidBarriers[index].AfterState == RHI::ResourceState::UnorderedAccess);
        }
        assert(pyramidBarriers[5].BeforeState == RHI::ResourceState::UnorderedAccess);
        assert(pyramidBarriers[5].AfterState == RHI::ResourceState::ShaderResource);

        hizPass.Shutdown();
        graph.Shutdown();
        pool.EndFrame();
        pool.Shutdown();
        shaderManager.Shutdown();
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "RenderGraphCompileTest start\n";

    TestLinearDependencyOrder();
    TestDiamondStableOrder();
    TestWriteAfterWriteDependency();
    TestWriteAfterReadDependency();
    TestImportedResourceBarriers();
    TestTransientResourcesResolveThroughPool();
    TestCycleDetectionDoesNotExecute();
    TestInvalidHandleRejected();
    TestCompileContextPassedToDeclare();
    TestWriteFinalStateSuppressesFollowupReadBarrier();
    TestVisibilityBufferResourcesDeclareAndRead();
    TestVisibilityRasterOnRecordsMegaDrawsAndIdPass();
    TestMaterialTileClassifyDispatchesAndPublishesArgs();
    TestMaterialTileClassifyWithoutRecordTableClearsArgs();
    TestMaterialTileClassifyAbsentWhenNotAdded();
    TestMaterialTileClassifyAddedButDisabledByDefault();
    TestVisibilityRasterOnSinglePassMegaGeometry();
    TestVisibilityRasterOffKeepsExistingMegaGeometryRecording();
    TestVisibilityRasterWithoutGBufferDepthDoesNothing();
    TestShadowMapNativeDeclareImportsDepthOutput();
    TestNeuralDecodeNativeDeclareWritesLogicalCompletion();
    TestMegaGeometryNativeDeclareImportsPersistentBuffers();
    TestMegaGeometryNativeDeclareUsesNamedGBufferAttachments();
    TestHiZPyramidNativeDeclareReadsGBufferDepthWithoutWritingIt();
    TestHiZPyramidNativeDeclareWithoutDepthDeclaresNothing();
    TestMegaGeometryNativeExecuteEnqueuesEmptyAttachmentPass();
    TestMegaGeometryNativeExecuteRecreatesRenderPassWhenAttachmentStateModeChanges();
    TestMegaGeometryPartialNamedGBufferFallsBackToLegacyAttachmentStates();
    TestMegaGeometryRecordFrameCommandBatchesInstancesInSingleRenderPass();
    TestMegaGeometryRecordFrameCommandSplitsSectionsByMaterial();
    TestMegaGeometryRecordFrameCommandWritesInstanceTable();
    TestMegaGeometryRecordFrameCommandRecordsTwoPassOcclusion();
    TestMegaGeometryTwoPassFallsBackWhenDepthRangeIsNotUnit();
    TestMegaGeometryTwoPassDiscardsVisibilityOnReaddAndComponentChange();
    TestMegaGeometryTwoPassDiscardsVisibilityWhenAllInstancesVanish();
    TestGBufferNativeDeclareCreatesTransientOutputs();
    TestGBufferSSAONativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareDependencies();
    TestGBufferSSAOLightingNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestLightingNativeDeclareReadsNamedShadowMap();
    TestLightingSceneDepthAliasDuplicateFailsCompile();
    TestForwardTransparentNativeDeclareDependencies();
    TestSSRNativeDeclareDependencies();
    TestBloomNativeDeclareDependencies();
    TestToneMappingNativeDeclareDependencies();
    TestVignetteNativeDeclareDependencies();
    TestToneMappedVersionChainFeedsFXAAAndUpscale();
    TestToneMappedVersionChainFeedsUpscaleWithoutFXAA();
    TestPostProcessNativeDeclareUsesNamedResourcesWithoutPassPointers();
    TestPostProcessMissingNamedInputsCompileWithoutErrors();
    TestFXAANativeDeclareDependencies();
    TestUpscaleNativeDeclareDependencies();
    TestShadowMapNativeExecuteRegistersBridge();
    TestNeuralDecodeNativeExecuteSkipsUnsupportedPath();
    TestMegaGeometryNativeExecuteSkipsWhenNoInstances();
    TestHiZPyramidNativeExecuteBuildsAllMipsInOneSubmission();
    TestGBufferNativeExecuteClearsWhenOpaqueCommandsEmpty();
    TestRecordMeshDrawCallUsesDrawCommandRangesAndFallback();
    TestSSAONativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestGBufferSSAOLightingNativeExecuteWithoutSharedResources();
    TestLightingNativeExecuteBindsExpandedLightStorageBuffer();
    TestLightingNativeExecuteZeroLightsKeepsLogicalCountZeroAndSkipsSsboUpdate();
    TestLightingLightBufferGrowthRetainsRetiredBuffersAndReusesCapacity();
    TestLightingLightBufferGrowthFailureSkipsDescriptorUpdateAndDraw();
    TestProductionDeferredNativeExecuteSkipsLegacyBridge();
    TestLightingNativeExecuteClearsWhenInputsMissingWithoutBridge();
    TestLightingNativeExecutePrefersNamedShadowMapOverRegistry();
    TestLightingNativeExecuteDoesNotPublishBridgeWhenFallbackInputsMissing();
    TestForwardTransparentNativeExecuteSkipsBridgeWhenRegisterOutputsDisabled();
    TestSSRNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestBloomNativeExecuteUsesGraphSceneColorWithoutBridge();
    TestToneMappingNativeExecuteExportsToneMappedColorWithoutBridge();
    TestVignetteNativeExecuteExportsToneMappedColorWithoutBridge();
    TestFXAANativeExecuteExportsToneMappedColorWithoutBridge();
    TestUpscaleNativeExecuteExportsPresentationColorWithoutBridge();
    TestSSRNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestBloomNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestVignetteNativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestFXAANativeExecuteRegistersBridgeWhenUsingSharedResourceFallback();
    TestUpscaleNativeExecuteExportsPresentationAliasWhenNoUpscale();
    TestUpscaleNativeExecuteExportsPresentationAliasFromInputPassFallback();
    TestToneMappingNativeExecuteAcceptsRawSceneColorBridge();

    std::cout << "RenderGraphCompileTest passed\n";
    return 0;
}

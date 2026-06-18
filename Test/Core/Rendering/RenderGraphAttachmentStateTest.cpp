#include "Rendering/RenderGraph/RenderGraph.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "Container/PointerTypes.h"
#include <cassert>
#include <iostream>

using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Identity;
namespace RHI = NorvesLib::RHI;

namespace
{
    RGTextureDesc MakeColorDesc(const char* name)
    {
        return RGTextureDesc::RenderTarget(64, 32, RHI::Format::R8G8B8A8_UNORM, name);
    }

    RGTextureDesc MakeDepthDesc(const char* name)
    {
        return RGTextureDesc::DepthStencil(64, 32, RHI::Format::D32_FLOAT, name);
    }

    RGBufferDesc MakeBufferDesc(const char* name)
    {
        RGBufferDesc desc;
        desc.Size = 64;
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead;
        desc.DebugName = name;
        return desc;
    }

    class AttachmentWritePass final : public IRenderGraphPass
    {
    public:
        AttachmentWritePass(Identity colorName, Identity depthName)
            : m_ColorName(colorName), m_DepthName(depthName)
        {
        }

        const char* GetName() const override
        {
            return "AttachmentWritePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            m_Color = builder.WriteTextureAttachment(m_ColorName,
                                                     MakeColorDesc("AttachmentColor"),
                                                     RGAttachmentKind::Color,
                                                     RHI::AttachmentLoadOp::Clear,
                                                     RHI::AttachmentStoreOp::Store,
                                                     RHI::ResourceState::RenderTarget,
                                                     RHI::ResourceState::ShaderResource);
            m_Depth = builder.WriteTextureAttachment(m_DepthName,
                                                     MakeDepthDesc("AttachmentDepth"),
                                                     RGAttachmentKind::DepthStencil,
                                                     RHI::AttachmentLoadOp::Clear,
                                                     RHI::AttachmentStoreOp::Store,
                                                     RHI::ResourceState::DepthWrite,
                                                     RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

        RGTextureHandle GetColor() const
        {
            return m_Color;
        }

    private:
        Identity m_ColorName;
        Identity m_DepthName;
        RGTextureHandle m_Color;
        RGTextureHandle m_Depth;
    };

    class ReadOnlyDepthAttachmentPass final : public IRenderGraphPass
    {
    public:
        explicit ReadOnlyDepthAttachmentPass(Identity depthName)
            : m_DepthName(depthName)
        {
        }

        const char* GetName() const override
        {
            return "ReadOnlyDepthAttachmentPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle depth;
            assert(builder.TryUseAttachment(m_DepthName,
                                            depth,
                                            RGAttachmentKind::DepthStencil,
                                            RGAttachmentMutability::ReadOnly,
                                            RHI::AttachmentLoadOp::Load,
                                            RHI::AttachmentStoreOp::Store,
                                            RHI::ResourceState::DepthRead,
                                            RHI::ResourceState::DepthRead));
            m_Depth = depth;
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_DepthName;
        RGTextureHandle m_Depth;
    };

    class SamePassSampledReadAndAttachmentPass final : public IRenderGraphPass
    {
    public:
        explicit SamePassSampledReadAndAttachmentPass(Identity colorName)
            : m_ColorName(colorName)
        {
        }

        const char* GetName() const override
        {
            return "SamePassSampledReadAndAttachmentPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle color;
            assert(builder.TryUseAttachment(m_ColorName,
                                            color,
                                            RGAttachmentKind::Color,
                                            RGAttachmentMutability::Write,
                                            RHI::AttachmentLoadOp::Load,
                                            RHI::AttachmentStoreOp::Store,
                                            RHI::ResourceState::RenderTarget,
                                            RHI::ResourceState::ShaderResource));
            builder.Read(color.ToResourceHandle(), RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_ColorName;
    };

    class SamePassReadOnlyDepthAndReadPass final : public IRenderGraphPass
    {
    public:
        explicit SamePassReadOnlyDepthAndReadPass(Identity depthName)
            : m_DepthName(depthName)
        {
        }

        const char* GetName() const override
        {
            return "SamePassReadOnlyDepthAndReadPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            RGTextureHandle depth;
            assert(builder.TryUseAttachment(m_DepthName,
                                            depth,
                                            RGAttachmentKind::DepthStencil,
                                            RGAttachmentMutability::ReadOnly,
                                            RHI::AttachmentLoadOp::Load,
                                            RHI::AttachmentStoreOp::Store,
                                            RHI::ResourceState::DepthRead,
                                            RHI::ResourceState::DepthRead));
            builder.Read(depth.ToResourceHandle(), RHI::ResourceState::DepthRead);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        Identity m_DepthName;
    };

    class LoadNewAttachmentPass final : public IRenderGraphPass
    {
    public:
        const char* GetName() const override
        {
            return "LoadNewAttachmentPass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            builder.WriteTextureAttachment(Identity("Attachment.LoadNew"),
                                           MakeColorDesc("LoadNewAttachment"),
                                           RGAttachmentKind::Color,
                                           RHI::AttachmentLoadOp::Load,
                                           RHI::AttachmentStoreOp::Store,
                                           RHI::ResourceState::RenderTarget,
                                           RHI::ResourceState::ShaderResource);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }
    };

    class BufferRangePass final : public IRenderGraphPass
    {
    public:
        BufferRangePass(uint64_t offset, uint64_t size)
            : m_Offset(offset), m_Size(size)
        {
        }

        const char* GetName() const override
        {
            return "BufferRangePass";
        }

        void Declare(RenderGraphBuilder& builder) override
        {
            m_Buffer = builder.WriteBuffer(Identity("Attachment.BufferRange"),
                                           MakeBufferDesc("BufferRange"),
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::ShaderResource,
                                           m_Offset,
                                           m_Size);
        }

        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override
        {
            (void)resources;
            (void)context;
        }

    private:
        uint64_t m_Offset = 0;
        uint64_t m_Size = 0;
        RGBufferHandle m_Buffer;
    };

    void TestAttachmentDeclarationState()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        AttachmentWritePass pass(Identity("Attachment.SceneColor"), Identity("Attachment.Depth"));
        graph.AddPass(&pass);

        assert(graph.Compile());
        assert(graph.GetDeclaredPassAccessCount(0) == 2);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Read;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        bool bColorAttachment = false;
        RHI::AttachmentLoadOp loadOp = RHI::AttachmentLoadOp::DontCare;
        RHI::AttachmentStoreOp storeOp = RHI::AttachmentStoreOp::DontCare;
        RGAttachmentKind kind = RGAttachmentKind::Color;
        RGAttachmentMutability mutability = RGAttachmentMutability::ReadOnly;

        assert(graph.TryGetDeclaredPassAccess(0,
                                              0,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              &bColorAttachment,
                                              &loadOp,
                                              &storeOp,
                                              &kind,
                                              &mutability));
        assert(mode == RGAccessMode::Write);
        assert(bColorAttachment);
        assert(kind == RGAttachmentKind::Color);
        assert(mutability == RGAttachmentMutability::Write);
        assert(loadOp == RHI::AttachmentLoadOp::Clear);
        assert(storeOp == RHI::AttachmentStoreOp::Store);
        assert(state == RHI::ResourceState::RenderTarget);
        assert(finalState == RHI::ResourceState::ShaderResource);

        assert(graph.TryGetDeclaredPassAccess(0,
                                              1,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              &bColorAttachment,
                                              &loadOp,
                                              &storeOp,
                                              &kind,
                                              &mutability));
        assert(mode == RGAccessMode::Write);
        assert(!bColorAttachment);
        assert(kind == RGAttachmentKind::DepthStencil);
        assert(mutability == RGAttachmentMutability::Write);
        assert(state == RHI::ResourceState::DepthWrite);
        assert(finalState == RHI::ResourceState::ShaderResource);

        const auto& barriers = graph.GetCompiledBarriers();
        assert(barriers.size() == 2);
        assert(barriers[0].PassName != nullptr);
        assert(barriers[0].ResourceDebugName != nullptr);
        assert(barriers[0].NamedResourceIdentity == Identity("Attachment.SceneColor"));

        std::cout << "TestAttachmentDeclarationState passed\n";
    }

    void TestReadOnlyDepthDoesNotAdvanceVersion()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        const Identity depthName("Attachment.ReadOnlyDepth");
        AttachmentWritePass writePass(Identity("Attachment.UnusedColor"), depthName);
        ReadOnlyDepthAttachmentPass readOnlyPass(depthName);
        graph.AddPass(&writePass);
        const uint32_t readOnlyPassIndex = graph.AddPass(&readOnlyPass);

        assert(graph.Compile());
        uint32_t version = 999;
        assert(graph.TryGetNamedResourceVersion(depthName, version));
        assert(version == 0);

        RGResourceHandle resource;
        RGAccessMode mode = RGAccessMode::Write;
        RHI::ResourceState state = RHI::ResourceState::Undefined;
        RHI::ResourceState finalState = RHI::ResourceState::Undefined;
        RGAttachmentKind kind = RGAttachmentKind::Color;
        RGAttachmentMutability mutability = RGAttachmentMutability::Write;
        assert(graph.TryGetDeclaredPassAccess(readOnlyPassIndex,
                                              0,
                                              resource,
                                              mode,
                                              state,
                                              finalState,
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              &kind,
                                              &mutability));
        assert(mode == RGAccessMode::Read);
        assert(kind == RGAttachmentKind::DepthStencil);
        assert(mutability == RGAttachmentMutability::ReadOnly);
        assert(state == RHI::ResourceState::DepthRead);
        assert(finalState == RHI::ResourceState::DepthRead);

        std::cout << "TestReadOnlyDepthDoesNotAdvanceVersion passed\n";
    }

    void TestSamePassAttachmentConflictsFail()
    {
        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            const Identity colorName("Attachment.SamePassColor");
            AttachmentWritePass writePass(colorName, Identity("Attachment.SamePassDepth"));
            SamePassSampledReadAndAttachmentPass conflictPass(colorName);
            graph.AddPass(&writePass);
            graph.AddPass(&conflictPass);
            assert(!graph.Compile());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            const Identity depthName("Attachment.SamePassReadOnlyDepth");
            AttachmentWritePass writePass(Identity("Attachment.SamePassUnusedColor"), depthName);
            SamePassReadOnlyDepthAndReadPass conflictPass(depthName);
            graph.AddPass(&writePass);
            graph.AddPass(&conflictPass);
            assert(!graph.Compile());
        }

        std::cout << "TestSamePassAttachmentConflictsFail passed\n";
    }

    void TestLoadRequiresExistingContents()
    {
        RenderGraph graph;
        assert(graph.Initialize(nullptr));

        LoadNewAttachmentPass pass;
        graph.AddPass(&pass);

        assert(!graph.Compile());

        std::cout << "TestLoadRequiresExistingContents passed\n";
    }

    void TestBufferRangeNormalizationAndOverflow()
    {
        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BufferRangePass pass(16, 0);
            graph.AddPass(&pass);
            assert(graph.Compile());

            const auto& barriers = graph.GetCompiledBarriers();
            assert(barriers.size() == 1);
            assert(barriers[0].Kind == RGBarrierKind::Buffer);
            assert(barriers[0].BufferOffset == 16);
            assert(barriers[0].BufferSize == 48);
            assert(barriers[0].PassName != nullptr);
            assert(barriers[0].ResourceDebugName != nullptr);
            assert(barriers[0].NamedResourceIdentity == Identity("Attachment.BufferRange"));
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BufferRangePass pass(60, 8);
            graph.AddPass(&pass);
            assert(!graph.Compile());
        }

        {
            RenderGraph graph;
            assert(graph.Initialize(nullptr));

            BufferRangePass pass(65, 0);
            graph.AddPass(&pass);
            assert(!graph.Compile());
        }

        std::cout << "TestBufferRangeNormalizationAndOverflow passed\n";
    }
} // namespace

int main()
{
    std::cout << "RenderGraphAttachmentStateTest start\n";

    TestAttachmentDeclarationState();
    TestReadOnlyDepthDoesNotAdvanceVersion();
    TestSamePassAttachmentConflictsFail();
    TestLoadRequiresExistingContents();
    TestBufferRangeNormalizationAndOverflow();

    std::cout << "RenderGraphAttachmentStateTest passed\n";
    return 0;
}

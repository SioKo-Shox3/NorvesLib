#include "Component/ImpostorComponent.h"
#include "Object/World.h"
#include "Rendering/ImpostorBake.h"
#include "Rendering/RenderResources.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"
#include "RHI/ISampler.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ISwapChain.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#undef assert
#define assert(expression)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expression))                                                                                             \
        {                                                                                                              \
            std::cerr << "Assertion failed: " << #expression << " at " << __FILE__ << ":" << __LINE__ << "\n";       \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Component;
using namespace NorvesLib::Core::Rendering;
using NorvesLib::Core::Container::MakeShared;
namespace Math = NorvesLib::Math;

namespace
{
    bool HasUsage(NorvesLib::RHI::ResourceUsage value, NorvesLib::RHI::ResourceUsage test)
    {
        return (static_cast<uint32_t>(value & test)) != 0u;
    }

    class FakeTexture final : public NorvesLib::RHI::ITexture
    {
    public:
        explicit FakeTexture(const NorvesLib::RHI::TextureDesc &desc)
            : Desc(desc)
        {
        }

        uint32_t GetWidth() const override { return Desc.Width; }
        uint32_t GetHeight() const override { return Desc.Height; }
        uint32_t GetDepth() const override { return Desc.Depth; }
        uint32_t GetMipLevels() const override { return Desc.MipLevels; }
        uint32_t GetArraySize() const override { return Desc.ArraySize; }
        NorvesLib::RHI::Format GetFormat() const override { return Desc.TextureFormat; }
        NorvesLib::RHI::ResourceUsage GetUsage() const override { return Desc.Usage; }
        bool IsCubemap() const override { return Desc.IsCubemap; }

        void Update(const void *data,
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
            ++UpdateCount;
        }

        NorvesLib::RHI::TextureDesc Desc;
        uint32_t UpdateCount = 0;
    };

    class FakeCommandList final : public NorvesLib::RHI::ICommandList
    {
    public:
        explicit FakeCommandList(uint32_t *beginCount,
                                 uint32_t *endCount,
                                 uint32_t *submitCount,
                                 bool *submitWaited)
            : BeginCount(beginCount),
              EndCount(endCount),
              SubmitCount(submitCount),
              SubmitWaited(submitWaited)
        {
        }

        void Begin() override { ++(*BeginCount); }
        void End() override { ++(*EndCount); }
        void Submit(bool waitForCompletion = false) override
        {
            ++(*SubmitCount);
            *SubmitWaited = waitForCompletion;
        }

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
        void DrawIndexedIndirectCount(NorvesLib::RHI::BufferPtr,
                                      uint64_t,
                                      NorvesLib::RHI::BufferPtr,
                                      uint64_t,
                                      uint32_t,
                                      uint32_t) override {}
        void FillBuffer(NorvesLib::RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(NorvesLib::RHI::BufferPtr, NorvesLib::RHI::BufferPtr, uint64_t = 0, uint64_t = 0, uint64_t = 0) override {}
        void CopyBufferToTexture(NorvesLib::RHI::BufferPtr,
                                 NorvesLib::RHI::TexturePtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override {}
        void CopyTextureToBuffer(NorvesLib::RHI::TexturePtr,
                                 NorvesLib::RHI::BufferPtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override {}
        void CopyTexture(NorvesLib::RHI::TexturePtr,
                         NorvesLib::RHI::TexturePtr,
                         uint32_t,
                         uint32_t,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0) override {}
        void GenerateMipmaps(NorvesLib::RHI::TexturePtr) override {}
        void BufferBarrier(NorvesLib::RHI::BufferPtr,
                           NorvesLib::RHI::ResourceState,
                           NorvesLib::RHI::ResourceState,
                           uint64_t = 0,
                           uint64_t = 0) override {}
        void TextureBarrier(NorvesLib::RHI::TexturePtr,
                            NorvesLib::RHI::ResourceState,
                            NorvesLib::RHI::ResourceState,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0) override {}

    private:
        uint32_t *BeginCount = nullptr;
        uint32_t *EndCount = nullptr;
        uint32_t *SubmitCount = nullptr;
        bool *SubmitWaited = nullptr;
    };

    class FakeDevice final : public NorvesLib::RHI::IDevice
    {
    public:
        NorvesLib::RHI::TexturePtr CreateTexture(const NorvesLib::RHI::TextureDesc &desc) override
        {
            CreatedTextureDescs.push_back(desc);
            LastTexture = MakeShared<FakeTexture>(desc);
            return LastTexture;
        }

        NorvesLib::RHI::CommandListPtr CreateCommandList() override
        {
            ++CreateCommandListCount;
            if (bFailCommandList)
            {
                return {};
            }
            return MakeShared<FakeCommandList>(&CommandBeginCount,
                                               &CommandEndCount,
                                               &CommandSubmitCount,
                                               &bSubmitWaited);
        }

        NorvesLib::RHI::BufferPtr CreateBuffer(const NorvesLib::RHI::BufferDesc &) override { return {}; }
        NorvesLib::RHI::SamplerPtr CreateSampler(const NorvesLib::RHI::SamplerDesc &) override { return {}; }
        NorvesLib::RHI::ShaderPtr CreateShader(const NorvesLib::RHI::ShaderDesc &) override { return {}; }
        NorvesLib::RHI::SwapChainPtr CreateSwapChain(const NorvesLib::RHI::SwapChainDesc &) override { return {}; }
        NorvesLib::RHI::RenderPassPtr CreateRenderPass(const NorvesLib::RHI::RenderPassDesc &) override { return {}; }
        NorvesLib::RHI::FramebufferPtr CreateFramebuffer(const NorvesLib::RHI::FramebufferDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateGraphicsPipeline(const NorvesLib::RHI::GraphicsPipelineDesc &) override { return {}; }
        NorvesLib::RHI::PipelinePtr CreateComputePipeline(const NorvesLib::RHI::ComputePipelineDesc &) override { return {}; }
        NorvesLib::RHI::DescriptorSetPtr CreateDescriptorSet(const NorvesLib::RHI::DescriptorSetDesc &) override { return {}; }
        NorvesLib::RHI::ShaderCompilerPtr CreateShaderCompiler() override { return {}; }
        NorvesLib::RHI::IGPUResourceAllocator* GetResourceAllocator() override { return nullptr; }
        void WaitIdle() override {}
        NorvesLib::RHI::API GetAPI() const override { return NorvesLib::RHI::API::None; }
        const NorvesLib::RHI::DeviceCapabilities &GetCapabilities() const override { return Capabilities; }
        NorvesLib::Math::Matrix4x4 AdjustProjectionForClipSpace(
            const NorvesLib::Math::Matrix4x4 &projection,
            bool bApplyYFlip = true) const override
        {
            (void)bApplyYFlip;
            return projection;
        }

        NorvesLib::RHI::DeviceCapabilities Capabilities;
        Container::VariableArray<NorvesLib::RHI::TextureDesc> CreatedTextureDescs;
        NorvesLib::Core::Container::TSharedPtr<FakeTexture> LastTexture;
        uint32_t CreateCommandListCount = 0;
        uint32_t CommandBeginCount = 0;
        uint32_t CommandEndCount = 0;
        uint32_t CommandSubmitCount = 0;
        bool bSubmitWaited = false;
        bool bFailCommandList = false;
    };

    void MakeTriangle(Container::VariableArray<Mesh3DVertex> &vertices,
                      Container::VariableArray<uint32_t> &indices)
    {
        vertices.clear();
        indices.clear();

        vertices.push_back(Mesh3DVertex{{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}});
        vertices.push_back(Mesh3DVertex{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}});
        vertices.push_back(Mesh3DVertex{{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}});

        indices.push_back(0u);
        indices.push_back(1u);
        indices.push_back(2u);
    }

    ImpostorBakeRequest MakeRequest(RenderResources &resources,
                                    FakeDevice &device,
                                    const Container::VariableArray<Mesh3DVertex> &vertices,
                                    const Container::VariableArray<uint32_t> &indices)
    {
        ImpostorBakeRequest request;
        request.Textures = &resources.Textures();
        request.Device = &device;
        request.Input.Vertices = Container::Span<const Mesh3DVertex>(vertices);
        request.Input.Indices = Container::Span<const uint32_t>(indices);
        request.Input.CellResolution = 64;
        request.Input.AxisCellCountX = 4;
        request.Input.AxisCellCountY = 2;
        request.Input.DebugName = "ImpostorBakeServiceTestAtlas";
        return request;
    }

    void TestBakeValidationRejectsInvalidInputs()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));

        ImpostorBakeService service;
        ImpostorBakeResult result;
        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MakeTriangle(vertices, indices);

        ImpostorBakeRequest request = MakeRequest(resources, *device, vertices, indices);
        request.Input.Vertices = Container::Span<const Mesh3DVertex>();
        assert(!service.BakeProceduralAtlas(request, result));
        assert(!result.AtlasTexture.IsValid());
        assert(device->CreatedTextureDescs.empty());

        request = MakeRequest(resources, *device, vertices, indices);
        request.Input.Indices = Container::Span<const uint32_t>(indices.data(), 2u);
        assert(!service.BakeProceduralAtlas(request, result));
        assert(device->CreatedTextureDescs.empty());

        request = MakeRequest(resources, *device, vertices, indices);
        request.Input.CellResolution = 0;
        assert(!service.BakeProceduralAtlas(request, result));
        assert(device->CreatedTextureDescs.empty());

        request = MakeRequest(resources, *device, vertices, indices);
        request.Textures = nullptr;
        assert(!service.BakeProceduralAtlas(request, result));
        assert(device->CreatedTextureDescs.empty());

        Container::VariableArray<uint32_t> invalidIndices = indices;
        invalidIndices[2] = 99u;
        request = MakeRequest(resources, *device, vertices, invalidIndices);
        assert(!service.BakeProceduralAtlas(request, result));
        assert(device->CreatedTextureDescs.empty());

        resources.Shutdown();
        std::cout << "TestBakeValidationRejectsInvalidInputs passed\n";
    }

    void TestBakeValidationRejectsDepthAtlasFormatsBeforeResourceCreation()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));

        ImpostorBakeService service;
        ImpostorBakeResult result;
        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MakeTriangle(vertices, indices);

        ImpostorBakeRequest request = MakeRequest(resources, *device, vertices, indices);
        request.Input.PixelFormat = TextureCreateInfo::Format::D24_S8;
        assert(!service.BakeProceduralAtlas(request, result));
        assert(!result.AtlasTexture.IsValid());
        assert(device->CreatedTextureDescs.empty());
        assert(device->CreateCommandListCount == 0u);

        request = MakeRequest(resources, *device, vertices, indices);
        request.Input.PixelFormat = TextureCreateInfo::Format::D32_FLOAT;
        assert(!service.BakeProceduralAtlas(request, result));
        assert(!result.AtlasTexture.IsValid());
        assert(device->CreatedTextureDescs.empty());
        assert(device->CreateCommandListCount == 0u);

        resources.Shutdown();
        std::cout << "TestBakeValidationRejectsDepthAtlasFormatsBeforeResourceCreation passed\n";
    }

    void TestBakeCreatesRenderTargetAtlasAndSynchronousCommandList()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));

        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MakeTriangle(vertices, indices);

        ImpostorBakeService service;
        ImpostorBakeResult result;
        ImpostorBakeRequest request = MakeRequest(resources, *device, vertices, indices);
        assert(service.BakeProceduralAtlas(request, result));
        assert(result.Succeeded());
        assert(result.AtlasTexture.IsValid());
        assert(result.Metadata.CellResolution == 64u);
        assert(result.Metadata.AxisCellCountX == 4u);
        assert(result.Metadata.AxisCellCountY == 2u);
        assert(result.Metadata.AtlasWidth == 256u);
        assert(result.Metadata.AtlasHeight == 128u);
        assert(result.Metadata.VertexCount == 3u);
        assert(result.Metadata.IndexCount == 3u);

        assert(device->CreatedTextureDescs.size() == 1);
        const NorvesLib::RHI::TextureDesc &desc = device->CreatedTextureDescs[0];
        assert(desc.Width == 256u);
        assert(desc.Height == 128u);
        assert(desc.TextureFormat == NorvesLib::RHI::Format::R8G8B8A8_UNORM);
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::RenderTarget));
        assert(HasUsage(desc.Usage, NorvesLib::RHI::ResourceUsage::ShaderRead));
        assert(device->LastTexture);
        assert(device->LastTexture->UpdateCount == 0u);
        assert(device->CreateCommandListCount == 1u);
        assert(device->CommandBeginCount == 1u);
        assert(device->CommandEndCount == 1u);
        assert(device->CommandSubmitCount == 1u);
        assert(device->bSubmitWaited);

        resources.Textures().ReleaseTexture(result.AtlasTexture);
        resources.Shutdown();
        std::cout << "TestBakeCreatesRenderTargetAtlasAndSynchronousCommandList passed\n";
    }

    void TestCommandListFailureReleasesCreatedTexture()
    {
        auto device = MakeShared<FakeDevice>();
        device->bFailCommandList = true;
        RenderResources resources;
        assert(resources.Initialize(device));

        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MakeTriangle(vertices, indices);

        ImpostorBakeService service;
        ImpostorBakeResult result;
        ImpostorBakeRequest request = MakeRequest(resources, *device, vertices, indices);
        assert(!service.BakeProceduralAtlas(request, result));
        assert(!result.AtlasTexture.IsValid());
        assert(device->CreatedTextureDescs.size() == 1);
        assert(device->CreateCommandListCount == 1u);
        assert(resources.Textures().GetRHITexture(TextureHandle{1u}) == nullptr);

        resources.Shutdown();
        std::cout << "TestCommandListFailureReleasesCreatedTexture passed\n";
    }

    void TestImpostorComponentProxyAndExplicitRelease()
    {
        auto device = MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));

        Container::VariableArray<Mesh3DVertex> vertices;
        Container::VariableArray<uint32_t> indices;
        MakeTriangle(vertices, indices);

        ImpostorBakeService service;
        ImpostorBakeResult bakeResult;
        ImpostorBakeRequest request = MakeRequest(resources, *device, vertices, indices);
        assert(service.BakeProceduralAtlas(request, bakeResult));

        World world;
        world.Initialize();
        Entity *entity = world.SpawnObject<Entity>();
        assert(entity);
        entity->SetLocalPosition(2.0f, 3.0f, 4.0f);

        ImpostorComponent *impostor = world.CreateComponent<ImpostorComponent>(entity);
        assert(impostor);
        assert(CastTo<ImpostorComponent>(impostor) == impostor);
        assert(CastTo<BillboardComponent>(impostor) == impostor);
        assert(impostor->GetBoardSpace() == BoardSpace::WorldSpace);
        assert(ImpostorComponent::StaticClass()->GetProperty(Identity("LODSwitchDistance")) != nullptr);
        assert(ImpostorComponent::StaticClass()->GetProperty(Identity("SourceMeshComponentId")) == nullptr);

        const TextureHandle fallbackTexture{900u};
        impostor->SetTextureHandle(fallbackTexture);
        impostor->SetSizeWorld(Math::Vector2(2.0f, 2.0f));
        world.UpdateWorldTransforms();
        impostor->RefreshRenderTransformCache();

        BoardProxy proxy;
        assert(impostor->BuildBoardProxy(proxy));
        assert(proxy.Texture == fallbackTexture);
        assert(proxy.Space == BoardSpace::WorldSpace);

        assert(impostor->SetBakedAtlas(bakeResult.AtlasTexture, bakeResult.Metadata));
        assert(impostor->HasBakedAtlas());
        constexpr uint64_t sourceMeshComponentId = 123456u;
        impostor->SetSourceMeshComponentId(sourceMeshComponentId);
        assert(impostor->GetSourceMeshComponentId() == sourceMeshComponentId);
        assert(impostor->BuildBoardProxy(proxy));
        assert(proxy.Texture == bakeResult.AtlasTexture);
        assert(proxy.SourceMeshComponentId == sourceMeshComponentId);
        assert(proxy.WorldBounds.CenterX == 2.0f);
        assert(proxy.WorldBounds.CenterY == 3.0f);
        assert(proxy.WorldBounds.CenterZ == 4.0f);

        const TextureHandle replacementTexture{901u};
        assert(!impostor->SetBakedAtlas(replacementTexture, bakeResult.Metadata));
        assert(impostor->GetBakedAtlasTextureHandle() == bakeResult.AtlasTexture);

        impostor->ReleaseBakedAtlas(resources.Textures());
        assert(!impostor->HasBakedAtlas());
        assert(!impostor->GetBakedAtlasTextureHandle().IsValid());
        assert(resources.Textures().GetRHITexture(bakeResult.AtlasTexture) == nullptr);
        assert(impostor->BuildBoardProxy(proxy));
        assert(proxy.Texture == fallbackTexture);

        world.Finalize();
        resources.Shutdown();
        std::cout << "TestImpostorComponentProxyAndExplicitRelease passed\n";
    }
}

int main()
{
    std::cout << "ImpostorBakeServiceTest start\n";

    TestBakeValidationRejectsInvalidInputs();
    TestBakeValidationRejectsDepthAtlasFormatsBeforeResourceCreation();
    TestBakeCreatesRenderTargetAtlasAndSynchronousCommandList();
    TestCommandListFailureReleasesCreatedTexture();
    TestImpostorComponentProxyAndExplicitRelease();

    std::cout << "ImpostorBakeServiceTest passed\n";
    return 0;
}

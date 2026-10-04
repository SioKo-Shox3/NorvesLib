// 既存の停止中テストとは別exe。実Coreと小さなRHI test doubleでCPU記録契約を検査する。
#include "Rendering/SkinnedDrawCommands.h"
#include "Rendering/RenderResources.h"
#include "Rendering/SceneRenderer.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IPipeline.h"
#include "RHI/IShader.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"
#include "RHI/ISwapChain.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

#undef assert
#define assert(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "assert failed: " << #condition << " line=" << __LINE__ << "\n"; \
            std::abort(); \
        } \
    } while (false)
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace Container = NorvesLib::Core::Container;
namespace Math = NorvesLib::Math;
namespace RHI = NorvesLib::RHI;
namespace Skeletal = NorvesLib::Core::Skeletal;

namespace
{
    class FakeBuffer final : public RHI::IBuffer
    {
    public:
        explicit FakeBuffer(const RHI::BufferDesc& desc)
            : Desc(desc), Bytes(static_cast<size_t>(desc.Size))
        {
        }

        uint64_t GetSize() const override
        {
            return Desc.Size;
        }

        void* Map(uint64_t offset = 0, uint64_t size = 0) override
        {
            (void)size;
            return offset < Bytes.size() ? Bytes.data() + static_cast<size_t>(offset) : nullptr;
        }

        void Unmap() override
        {
        }

        void Update(const void* data, uint64_t size, uint64_t offset = 0) override
        {
            if (!data || offset > Bytes.size() || size > Bytes.size() - offset)
            {
                return;
            }
            std::memcpy(Bytes.data() + static_cast<size_t>(offset), data, static_cast<size_t>(size));
        }

        RHI::ResourceUsage GetUsage() const override
        {
            return Desc.Usage;
        }

        RHI::BufferDesc Desc;
        Container::VariableArray<uint8_t> Bytes;
    };

    class FakePipeline final : public RHI::IPipeline
    {
    public:
        RHI::PipelineType GetPipelineType() const override
        {
            return RHI::PipelineType::Graphics;
        }
        uint32_t GetBindPointCount() const override
        {
            return 1;
        }
    };

    class FakeDescriptorSet final : public RHI::IDescriptorSet
    {
    public:
        void BindConstantBuffer(uint32_t, RHI::BufferPtr, uint32_t, uint32_t) override
        {
        }
        void BindTexture(uint32_t, RHI::TexturePtr) override
        {
        }
        void BindSampler(uint32_t, RHI::SamplerPtr) override
        {
        }
        void BindStorageBuffer(uint32_t binding, RHI::BufferPtr buffer, uint32_t, uint32_t) override
        {
            StorageBuffers[binding] = buffer;
        }
        void BindStorageTexture(uint32_t, RHI::TexturePtr) override
        {
        }
        void BindStorageTexture(uint32_t, RHI::TexturePtr, uint32_t) override
        {
        }
        void Update() override
        {
            ++UpdateCount;
        }

        Container::Map<uint32_t, RHI::BufferPtr> StorageBuffers;
        uint32_t UpdateCount = 0;
    };

    class FakeCommandList final : public RHI::ICommandList
    {
    public:
        void Begin() override
        {
        }
        void End() override
        {
        }
        void Submit(bool waitForCompletion = false) override
        {
            (void)waitForCompletion;
        }
        void BeginRenderPass(RHI::RenderPassPtr, RHI::FramebufferPtr) override
        {
        }
        void EndRenderPass() override
        {
        }
        void SetViewport(const RHI::Viewport&) override
        {
        }
        void SetScissor(const RHI::ScissorRect&) override
        {
        }
        void SetPipeline(RHI::PipelinePtr pipeline) override
        {
            LastPipeline = pipeline;
        }
        void SetVertexBuffer(RHI::BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override
        {
            (void)offset;
            (void)slot;
            LastVertexBuffer = buffer;
            ++RecordedCommandCount;
        }
        void SetIndexBuffer(RHI::BufferPtr buffer,
                            uint64_t offset = 0,
                            RHI::IndexType type = RHI::IndexType::Uint32) override
        {
            (void)offset;
            (void)type;
            LastIndexBuffer = buffer;
            ++RecordedCommandCount;
        }
        void SetConstantBuffer(RHI::BufferPtr, uint32_t, RHI::ShaderStage) override
        {
        }
        void SetTexture(RHI::TexturePtr, uint32_t, RHI::ShaderStage) override
        {
        }
        void SetSampler(RHI::SamplerPtr, uint32_t, RHI::ShaderStage) override
        {
        }
        void SetDescriptorSet(RHI::DescriptorSetPtr descriptorSet, uint32_t slot = 0) override
        {
            LastDescriptorSet = descriptorSet;
            LastDescriptorSetSlot = slot;
            ++RecordedCommandCount;
        }
        void DrawIndexed(uint32_t indexCount,
                         uint32_t startIndexLocation = 0,
                         int32_t baseVertexLocation = 0) override
        {
            LastIndexOffset = startIndexLocation;
            LastVertexOffset = baseVertexLocation;
            ++DrawIndexedCount;
            LastIndexCount = indexCount;
            ++RecordedCommandCount;
        }
        void Draw(uint32_t, uint32_t = 0) override
        {
        }
        void DrawIndexedInstanced(uint32_t,
                                  uint32_t,
                                  uint32_t = 0,
                                  int32_t = 0,
                                  uint32_t = 0) override
        {
            ++DrawIndexedInstancedCount;
        }
        void DrawInstanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override
        {
        }
        void DrawIndexedIndirect(RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override
        {
        }
        void DrawIndexedIndirectCount(RHI::BufferPtr,
                                      uint64_t,
                                      RHI::BufferPtr,
                                      uint64_t,
                                      uint32_t,
                                      uint32_t) override
        {
        }
        void FillBuffer(RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override
        {
        }
        void Dispatch(uint32_t, uint32_t, uint32_t) override
        {
        }
        void CopyBuffer(RHI::BufferPtr, RHI::BufferPtr, uint64_t = 0, uint64_t = 0, uint64_t = 0) override
        {
        }
        void CopyBufferToTexture(RHI::BufferPtr,
                                 RHI::TexturePtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override
        {
        }
        void CopyTextureToBuffer(RHI::TexturePtr,
                                 RHI::BufferPtr,
                                 uint32_t,
                                 uint32_t,
                                 uint64_t = 0,
                                 uint32_t = 0,
                                 uint32_t = 0) override
        {
        }
        void CopyTexture(RHI::TexturePtr,
                         RHI::TexturePtr,
                         uint32_t,
                         uint32_t,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0,
                         uint32_t = 0) override
        {
        }
        void GenerateMipmaps(RHI::TexturePtr) override
        {
        }
        void BufferBarrier(RHI::BufferPtr,
                           RHI::ResourceState,
                           RHI::ResourceState,
                           uint64_t = 0,
                           uint64_t = 0) override
        {
        }
        void TextureBarrier(RHI::TexturePtr,
                            RHI::ResourceState,
                            RHI::ResourceState,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0,
                            uint32_t = 0) override
        {
        }

        RHI::BufferPtr LastVertexBuffer;
        RHI::BufferPtr LastIndexBuffer;
        RHI::PipelinePtr LastPipeline;
        RHI::DescriptorSetPtr LastDescriptorSet;
        uint32_t LastDescriptorSetSlot = 0;
        uint32_t DrawIndexedCount = 0;
        uint32_t DrawIndexedInstancedCount = 0;
        uint32_t LastIndexCount = 0;
        uint32_t LastIndexOffset = 0;
        int32_t LastVertexOffset = 0;
        uint32_t RecordedCommandCount = 0;
    };

    class FakeDevice final : public RHI::IDevice
    {
    public:
        RHI::BufferPtr CreateBuffer(const RHI::BufferDesc& desc) override
        {
            auto buffer = Container::MakeShared<FakeBuffer>(desc);
            CreatedBuffers.push_back(buffer);
            return buffer;
        }
        RHI::TexturePtr CreateTexture(const RHI::TextureDesc&) override
        {
            return {};
        }
        RHI::SamplerPtr CreateSampler(const RHI::SamplerDesc&) override
        {
            return {};
        }
        RHI::ShaderPtr CreateShader(const RHI::ShaderDesc&) override
        {
            return {};
        }
        RHI::CommandListPtr CreateCommandList() override
        {
            return {};
        }
        RHI::SwapChainPtr CreateSwapChain(const RHI::SwapChainDesc&) override
        {
            return {};
        }
        RHI::RenderPassPtr CreateRenderPass(const RHI::RenderPassDesc&) override
        {
            return {};
        }
        RHI::FramebufferPtr CreateFramebuffer(const RHI::FramebufferDesc&) override
        {
            return {};
        }
        RHI::PipelinePtr CreateGraphicsPipeline(const RHI::GraphicsPipelineDesc&) override
        {
            return {};
        }
        RHI::PipelinePtr CreateComputePipeline(const RHI::ComputePipelineDesc&) override
        {
            return {};
        }
        RHI::DescriptorSetPtr CreateDescriptorSet(const RHI::DescriptorSetDesc&) override
        {
            return {};
        }
        RHI::ShaderCompilerPtr CreateShaderCompiler() override
        {
            return {};
        }
        RHI::IGPUResourceAllocator* GetResourceAllocator() override
        {
            return nullptr;
        }
        void WaitIdle() override
        {
        }
        RHI::API GetAPI() const override
        {
            return RHI::API::None;
        }
        const RHI::DeviceCapabilities& GetCapabilities() const override
        {
            return Capabilities;
        }
        Math::Matrix4x4 AdjustProjectionForClipSpace(const Math::Matrix4x4& value, bool = true) const override
        {
            return value;
        }
        RHI::DeviceCapabilities Capabilities;
        Container::VariableArray<Container::TSharedPtr<FakeBuffer>> CreatedBuffers;
    };
    Container::TSharedPtr<SkinnedMeshAssetLease> MakeAsset(uint32_t count, bool legacy = false, uint64_t id = 100)
    {
        Container::VariableArray<SkinnedMeshVertex> vertices(count*3);
        Container::VariableArray<uint32_t> indices;
        Container::VariableArray<Skeletal::SkeletalSubMesh> submeshes;
        Container::VariableArray<Container::String> names;
        for (uint32_t index = 0; index < count*3; ++index)
        {
            vertices[index].Normal[2] = 1;
            vertices[index].BoneWeights[0] = 1;
            indices.push_back(index);
        }
        if (legacy)
        {
            return Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{id,1},std::move(vertices),std::move(indices));
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            Skeletal::SkeletalSubMesh part{index*3,3,index};
            part.bNoShadow = index == 1;
            submeshes.push_back(part);
            names.push_back(index == 0 ? "Body" : "Part");
        }
        return Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{id,1},std::move(vertices),std::move(indices),
            std::move(submeshes),std::move(names));
    }
    SkinnedMeshProxy MakeProxy(const Container::TSharedPtr<SkinnedMeshAssetLease>& asset, uint64_t component = 501)
    {
        SkinnedMeshProxy proxy;
        proxy.MeshHandle = asset->GetHandle();
        proxy.AssetLease = asset;
        proxy.ObjectId = 400;
        proxy.ComponentId = component;
        proxy.WorldTransform = Math::Matrix4x4::Identity;
        proxy.BonePalette.push_back(Math::Matrix4x4::Identity);
        proxy.bHasAnimatedBounds = true;
        proxy.Material = MaterialHandle{11};
        proxy.MaterialCount = 2;
        proxy.Materials[0] = MaterialHandle{11};
        proxy.Materials[1] = MaterialHandle{22};
        return proxy;
    }
    void TestCommandsAndInvalidLeases()
    {
        for (uint32_t count : {1u,2u,8u})
        {
            auto asset = MakeAsset(count,false,100+count);
            auto proxy = MakeProxy(asset);
            FramePacket packet;
            const auto range = AppendSkinnedDrawCommands(&packet,{proxy});
            assert(range.First == 0 && range.Count == count && packet.SkinnedMeshFrameLeases.size() == 1);
            for (uint32_t index = 0; index < count; ++index)
            {
                const auto& command = packet.DrawCommands[index];
                assert(command.Draw.SubMeshIndex == index && command.Draw.IndexOffset == index*3 && command.Draw.IndexCount == 3);
                assert(command.Draw.VertexOffset == 0 && command.Draw.MaterialIndex == index);
                assert(command.Draw.MaterialHandle.Id == (index == 1 ? 22 : 11));
                assert(command.Draw.bCastShadow == (index != 1) && command.Skinned.FrameLeaseIndex == 0);
            }
            FramePacket disabled;
            proxy.bCastShadow = false;
            assert(AppendSkinnedDrawCommands(&disabled,{proxy}).Count == count);
            for (const auto& command : disabled.DrawCommands)
            {
                assert(!command.Draw.bCastShadow);
            }
            proxy.MeshHandle.Generation = 2;
            FramePacket mismatch;
            assert(AppendSkinnedDrawCommands(&mismatch,{proxy}).Count == 0 && mismatch.SkinnedMeshFrameLeases.empty());
            proxy.MeshHandle = asset->GetHandle();
            proxy.MaterialCount = 9;
            assert(AppendSkinnedDrawCommands(&mismatch,{proxy}).Count == 0);
        }
        auto legacy = MakeAsset(1,true,200);
        SkinnedMeshFrameLease oldFrame(legacy);
        assert(oldFrame.IsValid() && oldFrame.ComponentId == 0);
        FramePacket packet;
        assert(AppendSkinnedDrawCommands(&packet,{MakeProxy(legacy)}).Count == 1);
        assert(packet.DrawCommands[0].Draw.IndexOffset == 0 && packet.DrawCommands[0].Draw.IndexCount == 3);
        auto tooMany = MakeAsset(9,false,201);
        SkinnedMeshFrameLease invalid(tooMany);
        assert(!invalid.IsValid());
        FramePacket badPacket;
        assert(AppendSkinnedDrawCommands(&badPacket,{MakeProxy(tooMany)}).Count == 0 && badPacket.SkinnedMeshFrameLeases.empty());
        Container::VariableArray<SkinnedMeshVertex> vertices(3);
        Container::VariableArray<uint32_t> indices{0,1,3};
        auto badIndex = Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{202,1},std::move(vertices),std::move(indices));
        assert(!badIndex->HasValidRenderData());
    }
    void TestRecordedRangesAndForgery()
    {
        auto asset = MakeAsset(3);
        FramePacket packet;
        assert(AppendSkinnedDrawCommands(&packet,{MakeProxy(asset)}).Count == 3);
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);
        SceneRenderer renderer;
        FakeCommandList list;
        auto descriptor = Container::MakeShared<FakeDescriptorSet>();
        auto pipeline = Container::MakeShared<FakePipeline>();
        for (uint32_t index = 0; index < 3; ++index)
        {
            auto command = packet.DrawCommands[index];
            command.Skinned.FrameLease = packet.SkinnedMeshFrameLeases[0];
            command.Skinned.PassKind = SkinnedMeshPassKind::GBuffer;
            command.Pipeline = pipeline;
            assert(resources.SkinnedMeshes().PrepareDraw(command.Skinned.FrameLease,command.Skinned.BonePalette,
                command.Draw.WorldMatrix,command.Skinned.Prepared,&command.Skinned.BonePalette,&command.Draw.WorldMatrix));
            assert(renderer.RecordSkinnedDrawCall(command,&list,&resources.SkinnedMeshes(),descriptor));
            assert(list.LastIndexCount == 3 && list.LastIndexOffset == index*3 && list.LastVertexOffset == 0);
            const auto reject = [&](DrawCommand invalid)
            {
                const auto before = list.RecordedCommandCount;
                const auto triangles = renderer.GetStats().TriangleCount;
                assert(!renderer.RecordSkinnedDrawCall(invalid,&list,&resources.SkinnedMeshes(),descriptor));
                assert(list.RecordedCommandCount == before && renderer.GetStats().TriangleCount == triangles);
            };
            auto invalid = command; invalid.Draw.IndexCount = 0; reject(invalid);
            invalid = command; invalid.Draw.IndexOffset = UINT32_MAX; reject(invalid);
            invalid = command; invalid.Draw.VertexOffset = 1; reject(invalid);
            invalid = command; invalid.Draw.MaterialIndex = 99; reject(invalid);
            invalid = command; invalid.Draw.bInstanced = true; reject(invalid);
            invalid = command; invalid.Draw.SourceMeshComponentId += 1; reject(invalid);
            invalid = command; invalid.Skinned.Prepared.IndexCount += 3; reject(invalid);
            invalid = command;
            invalid.Skinned.FrameLease = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
            reject(invalid); // 同componentでも準備に関連付いたframe以外へ差し替えない。
            invalid = command; invalid.Skinned.Prepared.PreviousPaletteBuffer.reset(); reject(invalid);
            RHI::BufferDesc desc; desc.Size = 64;
            invalid = command; invalid.Skinned.Prepared.VertexBuffer = Container::MakeShared<FakeBuffer>(desc); reject(invalid);
            command.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
            if (index == 1)
            {
                reject(command);
                command.Draw.bCastShadow = true;
                reject(command); // NoShadowはcommandフラグ改変でも解除しない。
            }
            else
            {
                assert(renderer.RecordSkinnedDrawCall(command,&list,&resources.SkinnedMeshes(),descriptor));
            }
        }
        assert(renderer.GetStats().SkinnedGBufferDrawCallCount == 3 && renderer.GetStats().SkinnedShadowDrawCallCount == 2);
        assert(renderer.GetStats().TriangleCount == 5);
        auto oldAsset = MakeAsset(1,true,300);
        auto oldFrame = Container::MakeShared<SkinnedMeshFrameLease>(oldAsset);
        DrawCommand oldCommand = DrawCommand::CreateDrawIndexed();
        oldCommand.Draw.PayloadKind = DrawPayloadKind::Skinned;
        oldCommand.Skinned.FrameLease = oldFrame;
        oldCommand.Skinned.PassKind = SkinnedMeshPassKind::GBuffer;
        oldCommand.Pipeline = pipeline;
        Container::VariableArray<Math::Matrix4x4> oldPalette{Math::Matrix4x4::Identity};
        assert(oldCommand.Draw.IndexCount == 0);
        assert(resources.SkinnedMeshes().PrepareDraw(oldFrame,oldPalette,Math::Matrix4x4::Identity,oldCommand.Skinned.Prepared));
        assert(renderer.RecordSkinnedDrawCall(oldCommand,&list,&resources.SkinnedMeshes(),descriptor));
        assert(list.LastIndexCount == 3 && list.LastIndexOffset == 0 && list.LastVertexOffset == 0);
        auto collidingAsset = MakeAsset(3);
        auto collidingFrame = Container::MakeShared<SkinnedMeshFrameLease>(collidingAsset,501);
        SkinnedMeshPreparedDraw collision;
        const size_t before = device->CreatedBuffers.size();
        Container::VariableArray<Math::Matrix4x4> palette{Math::Matrix4x4::Identity};
        assert(!resources.SkinnedMeshes().PrepareDraw(collidingFrame,palette,Math::Matrix4x4::Identity,collision));
        assert(device->CreatedBuffers.size() == before && !collision.IsValid());
        resources.SkinnedMeshes().AbortFrame();
        resources.Shutdown();
    }
}
int main()
{
    TestCommandsAndInvalidLeases();
    TestRecordedRangesAndForgery();
    std::cout << "SkinnedSubmeshDrawContractTest PASS: CPU commands_ranges_materials_shadow_forgery; GPU acceptance separate\n";
    return 0;
}

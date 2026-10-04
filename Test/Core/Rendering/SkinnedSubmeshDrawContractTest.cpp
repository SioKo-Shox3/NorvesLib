// 既存の停止中テストとは別exe。実Coreと小さなRHI test doubleでCPU記録契約を検査する。
#include "Rendering/SkinnedDrawCommands.h"
#include "Rendering/SkinnedMeshGpuStore.h"
#include "Rendering/SkinnedShadowComponentBindings.h"
#include "Rendering/SkinnedShadowStorage.h"
#include "Rendering/SkinnedPoseHistory.h"
#include "Rendering/RenderedObjectHistory.h"
#include "Math/MatrixUtils.h"
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
            if (desc.DebugName && std::strcmp(desc.DebugName,"SkinnedPalette") == 0)
            {
                ++CurrentCreates;
                if (FailCurrent)
                {
                    return {};
                }
            }
            if (desc.DebugName && std::strcmp(desc.DebugName,"SkinnedPreviousPalette") == 0)
            {
                ++PreviousCreates;
                if (FailPrevious)
                {
                    return {};
                }
            }
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
        uint32_t CurrentCreates = 0;
        uint32_t PreviousCreates = 0;
        bool FailCurrent = false;
        bool FailPrevious = false;
        RHI::DeviceCapabilities Capabilities;
        Container::VariableArray<Container::TSharedPtr<FakeBuffer>> CreatedBuffers;
    };
    Container::TSharedPtr<SkinnedMeshAssetLease> MakeAsset(uint32_t count, bool legacy = false, uint64_t id = 100, uint64_t generation = 1, bool noShadow = true)
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
            return Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{id,generation},std::move(vertices),std::move(indices));
        }
        for (uint32_t index = 0; index < count; ++index)
        {
            Skeletal::SkeletalSubMesh part{index*3,3,index};
            part.bNoShadow = noShadow && index == 1;
            submeshes.push_back(part);
            names.push_back(index == 0 ? "Body" : "Part");
        }
        return Container::MakeShared<SkinnedMeshAssetLease>(SkinnedMeshHandle{id,generation},std::move(vertices),std::move(indices),
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
            invalid = command; invalid.Skinned.Prepared.PreparationEpoch += 1; reject(invalid);
            invalid = command; invalid.Skinned.Prepared.ComponentId += 1; reject(invalid);
            invalid = command; invalid.Skinned.PassKind = SkinnedMeshPassKind::Shadow; reject(invalid);
            invalid = command;
            invalid.Skinned.Prepared.bUsesPreviousPalette = false;
            invalid.Skinned.Prepared.PreviousPaletteBuffer.reset();
            reject(invalid);

            RHI::BufferDesc desc; desc.Size = 64;
            invalid = command; invalid.Skinned.Prepared.VertexBuffer = Container::MakeShared<FakeBuffer>(desc); reject(invalid);
            command.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
            assert(resources.SkinnedMeshes().PrepareDraw(command.Skinned.FrameLease,command.Skinned.BonePalette,
                command.Draw.WorldMatrix,command.Skinned.Prepared));
            assert(!command.Skinned.Prepared.PreviousPaletteBuffer && !command.Skinned.Prepared.bUsesPreviousPalette);
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
        assert(device->CurrentCreates == 1 && device->PreviousCreates == 1);
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
    void TestPaletteSharingOrdersAndIdentity()
    {
        for (bool gbufferFirst : {false,true})
        {
            auto device = Container::MakeShared<FakeDevice>();
            SkinnedMeshGpuStore store(device);
            auto asset = MakeAsset(8);
            auto frame = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
            auto viewport = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
            Container::VariableArray<Math::Matrix4x4> bones{Math::Matrix4x4::Identity};
            auto previousBones = bones;
            previousBones[0].values[12] = 2;
            auto world = Math::Matrix4x4::Identity;
            auto previousWorld = world; previousWorld.values[13] = 3;
            store.BeginFrame(0);
            SkinnedMeshPreparedDraw shadow,gbuffer;
            const auto prepareShadow = [&]()
            {
                assert(store.PrepareDraw(frame,bones,world,shadow));
                assert(!shadow.PreviousPaletteBuffer && !shadow.bUsesPreviousPalette);
            };
            const auto prepareGBuffer = [&]()
            {
                assert(store.PrepareDraw(viewport,bones,world,gbuffer,&previousBones,&previousWorld));
                assert(gbuffer.PreviousPaletteBuffer && gbuffer.bUsesPreviousPalette);
            };
            if (gbufferFirst)
            {
                prepareGBuffer(); prepareShadow();
            }
            else
            {
                prepareShadow();
                assert(device->CurrentCreates == 1 && device->PreviousCreates == 0);
                prepareGBuffer();
            }
            assert(shadow.PaletteBuffer == gbuffer.PaletteBuffer);
            assert(store.MarkLastUse(shadow,frame) && store.MarkLastUse(gbuffer,viewport));
            // 後からpreviousが追加されても、先行した影preparedは有効なまま。
            for (uint32_t part = 0; part < 8; ++part)
            {
                prepareShadow(); prepareGBuffer();
                assert(store.MarkLastUse(shadow,frame));
            }
            assert(device->CurrentCreates == 1 && device->PreviousCreates == 1);
            auto* previous = static_cast<FakeBuffer*>(gbuffer.PreviousPaletteBuffer.get());
            float expected[32];
            Math::MatrixUtils::CopyToShaderData(previousWorld,expected);
            Math::MatrixUtils::CopyToShaderData(previousBones[0],expected+16);
            assert(previous->Bytes.size() == sizeof(expected) && std::memcmp(previous->Bytes.data(),expected,sizeof(expected)) == 0);
            auto fresh = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
            assert(!store.MarkLastUse(shadow,fresh));
            SkinnedMeshPreparedDraw rejected;
            auto changed = bones; changed[0].values[12] = 8;
            assert(!store.PrepareDraw(frame,changed,world,rejected));
            auto changedWorld = world; changedWorld.values[13] = 8;
            assert(!store.PrepareDraw(frame,bones,changedWorld,rejected));
            assert(!store.PrepareDraw(frame,bones,world,rejected,&changed,&previousWorld));
            auto changedAsset = MakeAsset(8,false,100,2);
            auto changedFrame = Container::MakeShared<SkinnedMeshFrameLease>(changedAsset,501);
            assert(!store.PrepareDraw(changedFrame,bones,world,rejected));
            assert(device->CurrentCreates == 1 && device->PreviousCreates == 1);
            auto other = Container::MakeShared<SkinnedMeshFrameLease>(asset,502);
            SkinnedMeshPreparedDraw otherPrepared;
            assert(store.PrepareDraw(other,bones,world,otherPrepared));
            assert(otherPrepared.PaletteBuffer != shadow.PaletteBuffer && device->CurrentCreates == 2);
            assert(store.CommitSubmittedFrame(7));
            store.BeginFrame(0);
            assert(!store.MarkLastUse(shadow,frame));
            assert(store.PrepareDraw(frame,bones,world,rejected));
            assert(rejected.PaletteBuffer != shadow.PaletteBuffer && device->CurrentCreates == 3);
            store.AbortFrame();
            store.BeginFrame(7);
            assert(!store.MarkLastUse(rejected,frame));
            assert(store.PrepareDraw(changedFrame,bones,world,rejected));
            assert(device->CurrentCreates == 4);
            store.AbortFrame();
        }
    }
    void TestPaletteFailureAndLifetime()
    {
        auto device = Container::MakeShared<FakeDevice>();
        SkinnedMeshGpuStore store(device);
        auto asset = MakeAsset(2);
        auto frame = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
        auto viewport = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
        Container::VariableArray<Math::Matrix4x4> bones{Math::Matrix4x4::Identity};
        auto world = Math::Matrix4x4::Identity;
        store.BeginFrame(0);
        device->FailPrevious = true;
        SkinnedMeshPreparedDraw prepared;
        assert(!store.PrepareDraw(frame,bones,world,prepared,&bones,&world));
        assert(!store.PrepareDraw(frame,bones,world,prepared,&bones,&world));
        assert(device->CurrentCreates == 1 && device->PreviousCreates == 1);
        assert(store.PrepareDraw(frame,bones,world,prepared) && store.MarkLastUse(prepared,frame));
        assert(store.PrepareDraw(viewport,bones,world,prepared) && store.MarkLastUse(prepared,viewport));
        assert(device->CurrentCreates == 1 && !prepared.PreviousPaletteBuffer);
        Container::TWeakPtr<RHI::IBuffer> weak = prepared.PaletteBuffer;
        prepared = {};
        device->CreatedBuffers.clear();
        assert(store.CommitSubmittedFrame(9));
        frame.reset();
        store.BeginFrame(9); store.CollectReleasedResources();
        assert(!weak.expired()); // 別viewportのleaseが残る。
        viewport.reset(); store.CollectReleasedResources();
        assert(weak.expired());
        store.AbortFrame();
        frame = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
        store.BeginFrame(9);
        device->FailCurrent = true;
        assert(!store.PrepareDraw(frame,bones,world,prepared));
        assert(!store.PrepareDraw(frame,bones,world,prepared));
        assert(device->CurrentCreates == 2);
        store.AbortFrame();
        device->FailCurrent = false;
        device->FailPrevious = false;
        store.BeginFrame(9);
        assert(store.PrepareDraw(frame,bones,world,prepared));
        weak = prepared.PaletteBuffer;
        assert(store.MarkLastUse(prepared,frame));
        assert(store.CommitSubmittedFrame(12));
        prepared = {}; frame.reset(); device->CreatedBuffers.clear();
        store.BeginFrame(11); store.CollectReleasedResources();
        assert(!weak.expired()); // 全lease破棄済みでもGPU完了前は保持。
        store.BeginFrame(12); store.CollectReleasedResources();
        assert(weak.expired());
        // 成功したcurrent/previousも同じleaseとserial条件まで両方保持する。
        frame = Container::MakeShared<SkinnedMeshFrameLease>(asset,501);
        assert(store.PrepareDraw(frame,bones,world,prepared,&bones,&world));
        Container::TWeakPtr<RHI::IBuffer> pairCurrent = prepared.PaletteBuffer;
        Container::TWeakPtr<RHI::IBuffer> pairPrevious = prepared.PreviousPaletteBuffer;
        assert(store.MarkLastUse(prepared,frame) && store.CommitSubmittedFrame(15));
        prepared = {}; device->CreatedBuffers.clear();
        store.BeginFrame(15); store.CollectReleasedResources();
        assert(!pairCurrent.expired() && !pairPrevious.expired());
        frame.reset(); store.CollectReleasedResources();
        assert(pairCurrent.expired() && pairPrevious.expired());
        // 匿名の旧APIはBeginFrameを跨いでも互換を保つ。
        auto anonymous = Container::MakeShared<SkinnedMeshFrameLease>(asset);
        assert(store.PrepareDraw(anonymous,bones,world,prepared));
        store.BeginFrame(12);
        assert(store.MarkLastUse(prepared,anonymous));
        store.AbortFrame();
    }
    void TestPointShadowComponentBudget()
    {
        auto asset = MakeAsset(8,false,700,1,false);
        Container::VariableArray<SkinnedMeshProxy> proxies;
        for (uint64_t component=1;component<=17;++component)
        {
            proxies.push_back(MakeProxy(asset,component));
        }
        FramePacket packet;
        assert(AppendSkinnedDrawCommands(&packet,proxies).Count == 17*8);
        auto device = Container::MakeShared<FakeDevice>();
        RenderResources resources;
        assert(resources.Initialize(device));
        resources.SkinnedMeshes().BeginFrame(0);
        SceneRenderer renderer;
        FakeCommandList list;
        auto pipeline = Container::MakeShared<FakePipeline>();
        Container::VariableArray<RHI::DescriptorSetPtr> setsKept;
        for (uint32_t face=0;face<6;++face)
        {
            SkinnedShadowComponentBindings<RHI::DescriptorSetPtr> bindings;
            for (uint32_t part=0;part<8;++part)
            {
                for (uint32_t component=0;component<17;++component)
                {
                    auto command = packet.DrawCommands[component*8+part];
                    command.Skinned.FrameLease = packet.SkinnedMeshFrameLeases[component];
                    command.Skinned.PassKind = SkinnedMeshPassKind::Shadow;
                    command.Pipeline = pipeline;
                    assert(resources.SkinnedMeshes().PrepareDraw(command.Skinned.FrameLease,command.Skinned.BonePalette,
                        command.Draw.WorldMatrix,command.Skinned.Prepared));
                    const auto& prepared = command.Skinned.Prepared;
                    const SkinnedShadowBindingKey key{prepared.ComponentId,prepared.PreparationEpoch,
                        prepared.MeshHandle.Id,prepared.MeshHandle.Generation,prepared.PaletteBuffer.get(),prepared.VertexBuffer.get()};
                    const auto create = [&]() -> RHI::DescriptorSetPtr
                    {
                        auto descriptor = Container::MakeShared<FakeDescriptorSet>();
                        assert(BindSkinnedShadowStorage(prepared,descriptor.get()));
                        assert(descriptor->StorageBuffers.size()==2 && descriptor->StorageBuffers.find(10)==descriptor->StorageBuffers.end());
                        assert(descriptor->StorageBuffers[8]==prepared.PaletteBuffer && descriptor->StorageBuffers[9]==prepared.VertexBuffer);
                        setsKept.push_back(descriptor);
                        return descriptor;
                    };
                    const bool accepted = bindings.TryGet(key,create,command.DescriptorSet);
                    assert(accepted == (component<16));
                    if (accepted)
                    {
                        assert(command.DescriptorSet == setsKept[face*16+component]);
                        assert(renderer.RecordSkinnedDrawCall(command,&list,&resources.SkinnedMeshes(),command.DescriptorSet));
                        assert(list.LastIndexOffset == part*3 && list.LastIndexCount == 3);
                    }
                }
            }
            assert(bindings.Count()==16 && setsKept.size()==(face+1)*16);
            assert(list.DrawIndexedCount==(face+1)*128);
        }
        assert(device->CurrentCreates==17 && device->PreviousCreates==0);
        auto bad = packet.DrawCommands[0];
        SkinnedMeshPreparedDraw prepared;
        assert(resources.SkinnedMeshes().PrepareDraw(packet.SkinnedMeshFrameLeases[0],bad.Skinned.BonePalette,
            bad.Draw.WorldMatrix,prepared,&bad.Skinned.BonePalette,&bad.Draw.WorldMatrix));
        FakeDescriptorSet rejected;
        assert(!BindSkinnedShadowStorage(prepared,&rejected) && rejected.StorageBuffers.empty());
        resources.SkinnedMeshes().AbortFrame();
        resources.Shutdown();
    }
    void TestPoseHistoryAssetGeneration()
    {
        auto asset = MakeAsset(8,false,900);
        const auto build = [&](FramePacket& packet, const auto& source, uint64_t number, float position, uint64_t component=501)
        {
            packet.FrameNumber = number;
            auto proxy = MakeProxy(source,component);
            proxy.WorldTransform.values[12] = position;
            proxy.BonePalette[0].values[13] = position;
            assert(AppendSkinnedDrawCommands(&packet,{proxy}).Count == 8);
            assert(AppendSkinnedDrawCommands(&packet,{proxy}).Count == 8); // 2viewportのleaseを保持する。
        };
        const auto check = [](const FramePacket& packet, bool expected, float position)
        {
            for (const auto& command : packet.DrawCommands)
            {
                assert(command.Skinned.bHasPrevious == expected);
                assert(command.Skinned.PreviousWorldMatrix.values[12] == position);
                if (expected)
                {
                    assert(command.Skinned.PreviousBonePalette.size()==1 && command.Skinned.PreviousBonePalette[0].values[13]==position);
                }
                else
                {
                    assert(command.Skinned.PreviousBonePalette.empty());
                }
            }
        };
        FramePacket first;
        build(first,asset,10,1);
        SkinnedPoseHistory history;
        history.Record(first);
        FramePacket next;
        build(next,asset,11,2);
        history.Apply(next,true); check(next,true,1);
        history.Apply(next,false); check(next,false,2);
        auto reload = MakeAsset(8,false,900,2);
        FramePacket changed;
        build(changed,reload,11,2);
        history.Apply(changed,true); check(changed,false,2);
        auto foreign = MakeAsset(8,false,900,1);
        FramePacket sameHandleOtherObject;
        build(sameHandleOtherObject,foreign,11,2);
        history.Apply(sameHandleOtherObject,true); check(sameHandleOtherObject,false,2);
        auto otherAsset = MakeAsset(8,false,901,1);
        FramePacket differentAsset;
        build(differentAsset,otherAsset,11,2);
        history.Apply(differentAsset,true); check(differentAsset,false,2);
        FramePacket otherComponent;
        build(otherComponent,asset,11,2,502);
        history.Apply(otherComponent,true); check(otherComponent,false,2);
        next.DrawCommands[0].Skinned.BonePalette.push_back(Math::Matrix4x4::Identity);
        next.DrawCommands[1].Skinned.FrameLeaseIndex = UINT32_MAX;
        history.Apply(next,true);
        assert(!next.DrawCommands[0].Skinned.bHasPrevious && !next.DrawCommands[1].Skinned.bHasPrevious);
        assert(next.DrawCommands[2].Skinned.bHasPrevious);
        next.DrawCommands[0].Skinned.BonePalette.pop_back();
        next.DrawCommands[1].Skinned.FrameLeaseIndex = 0;
        first.DrawCommands[1].Draw.WorldMatrix.values[12] = 7;
        history.Record(first);
        history.Apply(next,true); check(next,false,2); // 同componentの衝突した履歴を使わない。
        first.DrawCommands[1].Draw.WorldMatrix.values[12] = 1;
        first.DrawCommands[1].Skinned.BonePalette[0].values[13] = 7;
        history.Record(first);
        history.Apply(next,true); check(next,false,2);
        first.DrawCommands[1].Skinned.BonePalette[0].values[13] = 1;
        auto firstFrame = first.SkinnedMeshFrameLeases[1];
        first.SkinnedMeshFrameLeases[1] = Container::MakeShared<SkinnedMeshFrameLease>(reload,501);
        history.Record(first);
        history.Apply(next,true); check(next,false,2);
        first.SkinnedMeshFrameLeases[1] = firstFrame;
        history.Record(first);
        auto savedFrame = next.SkinnedMeshFrameLeases[0];
        next.SkinnedMeshFrameLeases[0].reset();
        history.Apply(next,true);
        assert(!next.DrawCommands[0].Skinned.bHasPrevious && next.DrawCommands[8].Skinned.bHasPrevious);
        next.SkinnedMeshFrameLeases[0] = Container::MakeShared<SkinnedMeshFrameLease>(asset,777);
        history.Apply(next,true);
        assert(!next.DrawCommands[0].Skinned.bHasPrevious && next.DrawCommands[8].Skinned.bHasPrevious);
        next.SkinnedMeshFrameLeases[0] = savedFrame;
        FramePacket anonymous;
        build(anonymous,asset,11,2);
        for (auto& command : anonymous.DrawCommands)
        {
            command.Draw.SourceMeshComponentId = 0;
        }
        for (auto& frame : anonymous.SkinnedMeshFrameLeases)
        {
            frame = Container::MakeShared<SkinnedMeshFrameLease>(asset);
        }
        history.Apply(anonymous,true); check(anonymous,false,2);
        history.Record(first);
        history.Reset(); history.Apply(next,true); check(next,false,2);
        // RenderThreadのframe gapも同じ資産照合を通り、別世代を同骨数だからと再利用しない。
        RenderedObjectHistory rendered;
        assert(!rendered.Apply(first,true).bRebased);
        next.FrameNumber = 12;
        assert(rendered.Apply(next,true).bRebased); check(next,true,1);
        changed.FrameNumber = 14;
        assert(rendered.Apply(changed,true).bRebased); check(changed,false,2);
        FramePacket afterReload;
        build(afterReload,reload,16,3);
        assert(rendered.Apply(afterReload,true).bRebased); check(afterReload,true,2);
        // 履歴がResource/packetの所有権を延命しない。
        Container::TWeakPtr<SkinnedMeshAssetLease> weak;
        {
            auto temporary = MakeAsset(8,false,902);
            weak = temporary;
            FramePacket packet;
            build(packet,temporary,20,4);
            history.Record(packet);
        }
        assert(weak.expired());
    }
}
int main()
{
    TestCommandsAndInvalidLeases();
    TestRecordedRangesAndForgery();
    TestPaletteSharingOrdersAndIdentity();
    TestPaletteFailureAndLifetime();
    TestPointShadowComponentBudget();
    TestPoseHistoryAssetGeneration();
    std::cout << "SkinnedSubmeshDrawContractTest PASS: CPU commands_ranges_materials_shadow_forgery; GPU acceptance separate\n";
    return 0;
}

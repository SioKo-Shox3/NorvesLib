#include "Rendering/VisibilityRasterPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    using namespace Container;

    namespace
    {
        constexpr uint32_t FrameUniformBytes = sizeof(float) * 32; // view + projection
        constexpr uint32_t RecordParamsBytes = 16;                 // uvec4 counts
        constexpr uint32_t DebugParamsBytes = 16;                  // vec4 params
        constexpr uint32_t IndirectCommandBytes = static_cast<uint32_t>(sizeof(MegaGeometry::DrawIndexedIndirectCommand));
        constexpr uint32_t RecordBytes = static_cast<uint32_t>(sizeof(VisibilityBuffer::DrawRecord));
        constexpr uint32_t RecordThreadsPerGroup = 64;

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        uint32_t NextPowerOfTwo(uint32_t value)
        {
            uint32_t result = 1;
            while (result < value && result < (1u << 31))
            {
                result <<= 1;
            }
            return result;
        }

        void AddBinding(RHI::DescriptorSetDesc& desc,
                        uint32_t binding,
                        RHI::ResourceBindType type,
                        RHI::ShaderStage stages)
        {
            RHI::DescriptorBinding descriptorBinding;
            descriptorBinding.binding = binding;
            descriptorBinding.type = type;
            descriptorBinding.stages = stages;
            desc.bindings.push_back(descriptorBinding);
        }

        // 描画のディスクリプタセット（3種類の描画で同じ形）: 0 = view・projection、1 = 種類ごとの表、2 = 種類ごとの表
        //   MegaGeometry: 1 = インスタンスの表、2 = コマンドごとの描画情報
        //   手続き: 1 = 描画のインスタンスの表、2 = 記録の表
        //   スキニング: 1 = 変形した頂点、2 = 記録の表（使わない）
        RHI::DescriptorSetDesc MakeDrawDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Vertex);
            AddBinding(desc, 1, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Vertex);
            AddBinding(desc, 2, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Vertex);
            return desc;
        }

        // 記録を書く計算のディスクリプタセット（visbuffer_records.comp の binding 0〜6）
        RHI::DescriptorSetDesc MakeRecordDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute);
            for (uint32_t binding = 1; binding <= 5; ++binding)
            {
                AddBinding(desc, binding, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute);
            }
            AddBinding(desc, 6, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute);
            return desc;
        }

        // 検証表示のディスクリプタセット（visbuffer_debug.frag の binding 0〜2）
        RHI::DescriptorSetDesc MakeDebugDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::CombinedImageSampler, RHI::ShaderStage::Pixel);
            AddBinding(desc, 1, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Pixel);
            AddBinding(desc, 2, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Pixel);
            return desc;
        }

        VisibilityBuffer::DrawRecord MakeChunkRecord(VisibilityBuffer::RecordKind kind,
                                                     uint32_t instanceIndex,
                                                     uint32_t materialIndex,
                                                     uint32_t triangleCount,
                                                     uint32_t firstIndex,
                                                     uint32_t vertexBase)
        {
            VisibilityBuffer::DrawRecord record;
            record.Kind = static_cast<uint32_t>(kind);
            record.InstanceIndex = instanceIndex;
            record.MaterialIndex = materialIndex;
            record.TriangleCount = triangleCount;
            record.FirstIndex = firstIndex;
            record.VertexBase = vertexBase;
            return record;
        }
    } // namespace

    // ========================================
    // VisibilityRasterPass
    // ========================================

    // ビジビリティバッファを使わない設定（VisibilityBufferMode::Off）のときは、SceneView がこのパスを足さない
    VisibilityRasterPass::VisibilityRasterPass() = default;

    VisibilityRasterPass::~VisibilityRasterPass()
    {
        Shutdown();
    }

    bool VisibilityRasterPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        // gl_PrimitiveID（フラグメントシェーダー）には geometryShader の機能が要る
        const RHI::DeviceCapabilities& caps = m_Device->GetCapabilities();
        if (!caps.bGeometryShader || !caps.bDrawIndirectFirstInstance)
        {
            if (!m_bLoggedUnsupported)
            {
                NORVES_LOG_WARNING("VisibilityRasterPass",
                                   "VIS_RASTER_UNSUPPORTED geometry_shader=%d draw_indirect_first_instance=%d "
                                   "ビジビリティバッファの描画に対応しないため、このパスは何もしません",
                                   caps.bGeometryShader ? 1 : 0,
                                   caps.bDrawIndirectFirstInstance ? 1 : 0);
                m_bLoggedUnsupported = true;
            }
            return true;
        }

        m_MegaVertexShader = context.ShaderMgr->LoadShader("visbuffer_mega.vert", RHI::ShaderStage::Vertex);
        m_MeshVertexShader = context.ShaderMgr->LoadShader("visbuffer_mesh.vert", RHI::ShaderStage::Vertex);
        m_SkinnedVertexShader = context.ShaderMgr->LoadShader("visbuffer_skinned.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("visbuffer.frag", RHI::ShaderStage::Pixel);
        m_RecordsShader = context.ShaderMgr->LoadShader("visbuffer_records.comp", RHI::ShaderStage::Compute);
        if (!m_MegaVertexShader || !m_MeshVertexShader || !m_SkinnedVertexShader || !m_FragmentShader ||
            !m_RecordsShader)
        {
            NORVES_LOG_WARNING("VisibilityRasterPass", "ビジビリティバッファのシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        if (!CreateRenderPass() || !CreatePipelines(context))
        {
            m_MegaPipeline.reset();
            m_MeshPipeline.reset();
            m_SkinnedPipeline.reset();
            m_RecordsPipeline.reset();
            NORVES_LOG_WARNING("VisibilityRasterPass", "ビジビリティバッファのパイプラインの作成に失敗。このパスは何もしません");
        }
        return true;
    }

    void VisibilityRasterPass::Shutdown()
    {
        for (FrameSlot& slot : m_FrameSlots)
        {
            slot = FrameSlot{};
        }
        m_MegaPipeline.reset();
        m_MeshPipeline.reset();
        m_SkinnedPipeline.reset();
        m_RecordsPipeline.reset();
        m_MegaVertexShader.reset();
        m_MeshVertexShader.reset();
        m_SkinnedVertexShader.reset();
        m_FragmentShader.reset();
        m_RecordsShader.reset();
        m_Framebuffer.reset();
        m_FramebufferId = nullptr;
        m_FramebufferDepth = nullptr;
        m_RenderPass.reset();
        m_IdHandle = {};
        m_DepthHandle = {};
        m_SkinnedVerticesHandle = {};
        m_LastRecordTable.reset();
        m_LastRecordTableBytes = 0;
        m_Stats = VisibilityRasterFrameStats{};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void VisibilityRasterPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VisibilityRasterPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    bool VisibilityRasterPass::CreateRenderPass()
    {
        RHI::RenderPassDesc desc;

        // ID: 空（0）で消し、描いた画素へ ID を書く。後の材質の解決が ShaderResource として読む
        RHI::AttachmentDesc idAttachment;
        idAttachment.format = RHI::Format::R32_UINT;
        idAttachment.isDepthStencil = false;
        idAttachment.clear = true;
        idAttachment.clearColorUint[0] = VisibilityBuffer::EMPTY_ID;
        idAttachment.loadOp = RHI::AttachmentLoadOp::Clear;
        idAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        idAttachment.initialState = RHI::ResourceState::RenderTarget;
        idAttachment.finalState = RHI::ResourceState::ShaderResource;
        desc.colorAttachments.push_back(idAttachment);

        // 深度: GBuffer が書いた深度を読み、同じ値以下の描画で書き直す
        desc.hasDepthStencil = true;
        desc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        desc.depthStencilAttachment.isDepthStencil = true;
        desc.depthStencilAttachment.clear = false;
        desc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        desc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        desc.depthStencilAttachment.initialState = RHI::ResourceState::DepthWrite;
        desc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_RenderPass = m_Device->CreateRenderPass(desc);
        return m_RenderPass != nullptr;
    }

    bool VisibilityRasterPass::CreatePipelines(ViewRenderContext& /*context*/)
    {
        if (!m_Device || !m_RenderPass)
        {
            return false;
        }

        // 3種類の描画のパイプライン。ID を書く1枚のカラー添付と深度だけが違わず、頂点シェーダーと頂点入力だけが違う
        const auto createGraphics = [this](const RHI::ShaderPtr& vertexShader,
                                           bool bVertexInput,
                                           RHI::PipelinePtr& outPipeline) -> bool
        {
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = vertexShader;
            pipelineDesc.pixelShader = m_FragmentShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            if (bVertexInput)
            {
                // 頂点入力は Mesh3DVertex と同じ並び（位置だけを読む。法線・UV は GBuffer と同じ頂点バッファを使うため並びを合わせる）
                RHI::VertexBindingDesc vertexBinding;
                vertexBinding.binding = 0;
                vertexBinding.stride = sizeof(Mesh3DVertex);
                vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
                pipelineDesc.vertexBindings.push_back(vertexBinding);

                RHI::VertexAttributeDesc positionAttribute;
                positionAttribute.location = 0;
                positionAttribute.binding = 0;
                positionAttribute.format = RHI::Format::R32G32B32_FLOAT;
                positionAttribute.offset = 0;
                pipelineDesc.vertexAttributes.push_back(positionAttribute);
            }

            // ラスタライザは GBuffer と同じ（裏面を捨て、時計回りが表）
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;

            // GBuffer がすでに同じ形を書いた深度に対して、同じ値以下なら ID を書く（同じ式の位置は同じ深度になる）
            pipelineDesc.depthStencilState.depthTestEnable = true;
            pipelineDesc.depthStencilState.depthWriteEnable = true;
            pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::LessOrEqual;

            // 整数の添付はブレンドしない
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(MakeDrawDescriptorSetDesc());

            outPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            return outPipeline != nullptr;
        };

        if (!createGraphics(m_MegaVertexShader, true, m_MegaPipeline) ||
            !createGraphics(m_MeshVertexShader, true, m_MeshPipeline) ||
            !createGraphics(m_SkinnedVertexShader, false, m_SkinnedPipeline))
        {
            return false;
        }

        RHI::ComputePipelineDesc computeDesc;
        computeDesc.computeShader = m_RecordsShader;
        computeDesc.descriptorSetLayouts.push_back(MakeRecordDescriptorSetDesc());
        m_RecordsPipeline = m_Device->CreateComputePipeline(computeDesc);
        return m_RecordsPipeline != nullptr;
    }

    bool VisibilityRasterPass::EnsureFramebuffer(const RHI::TexturePtr& idTexture, const RHI::TexturePtr& depthTexture)
    {
        if (m_Framebuffer && m_FramebufferId == idTexture.get() && m_FramebufferDepth == depthTexture.get())
        {
            return true;
        }

        RHI::FramebufferDesc desc;
        desc.renderPass = m_RenderPass;
        desc.colorTargets.push_back(idTexture);
        desc.depthStencilTarget = depthTexture;
        desc.width = idTexture->GetWidth();
        desc.height = idTexture->GetHeight();
        m_Framebuffer = m_Device->CreateFramebuffer(desc);
        m_FramebufferId = m_Framebuffer ? idTexture.get() : nullptr;
        m_FramebufferDepth = m_Framebuffer ? depthTexture.get() : nullptr;
        return m_Framebuffer != nullptr;
    }

    bool VisibilityRasterPass::EnsureFrameSlot(FrameSlot& slot, uint32_t recordCapacity, uint32_t sectionCount)
    {
        if (!m_Device)
        {
            return false;
        }

        // 直前に使ったのは FrameSlotCount フレーム前で、そのGPUの仕事は終わっているので、作り直して置き換えてよい
        if (!slot.RecordTable || slot.RecordCapacity < recordCapacity)
        {
            const uint32_t capacity = std::max(64u, NextPowerOfTwo(recordCapacity));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * RecordBytes,
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "VisBuffer_DrawRecords");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.RecordTable = buffer;
            slot.RecordCapacity = capacity;
            slot.RecordState = RHI::ResourceState::Common;
        }

        if (!slot.SectionAddresses || slot.SectionAddressCapacity < sectionCount)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(sectionCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * 4u * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "VisBuffer_SectionAddresses");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.SectionAddresses = buffer;
            slot.SectionAddressCapacity = capacity;
        }

        if (!slot.FrameUniform)
        {
            slot.FrameUniform = m_Device->CreateBuffer(
                RHI::BufferDesc(FrameUniformBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_FrameUBO"));
        }
        if (!slot.RecordParams)
        {
            slot.RecordParams = m_Device->CreateBuffer(
                RHI::BufferDesc(RecordParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_RecordParams"));
        }
        if (!slot.MegaSet)
        {
            slot.MegaSet = m_Device->CreateDescriptorSet(MakeDrawDescriptorSetDesc());
        }
        if (!slot.MeshSet)
        {
            slot.MeshSet = m_Device->CreateDescriptorSet(MakeDrawDescriptorSetDesc());
        }
        if (!slot.SkinnedSet)
        {
            slot.SkinnedSet = m_Device->CreateDescriptorSet(MakeDrawDescriptorSetDesc());
        }
        if (!slot.RecordSet)
        {
            slot.RecordSet = m_Device->CreateDescriptorSet(MakeRecordDescriptorSetDesc());
        }
        return slot.FrameUniform && slot.RecordParams && slot.MegaSet && slot.MeshSet && slot.SkinnedSet &&
               slot.RecordSet;
    }

    void VisibilityRasterPass::Declare(RenderGraphBuilder& builder)
    {
        m_IdHandle = {};
        m_DepthHandle = {};
        m_SkinnedVerticesHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        if (!context)
        {
            return;
        }
        // 初期化を済ませたのにパイプラインが無い（対応しないデバイス・シェーダーの失敗）ときは何も宣言しない。
        // ID の添付を宣言すると、描かないまま ShaderResource の状態を前提に読まれてしまうため
        if (m_bInitialized && !m_MegaPipeline)
        {
            return;
        }
        const uint32_t width = context->GetActiveRenderWidth();
        const uint32_t height = context->GetActiveRenderHeight();
        if (width == 0 || height == 0)
        {
            return;
        }

        // 深度は GBuffer が書いたものを読み、描き直す（MegaGeometryPass と同じ宣言）。無ければ（GBuffer が無い構成）何もしない
        RGTextureHandle depthHandle;
        if (!builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth,
                                      depthHandle,
                                      RGAttachmentKind::DepthStencil,
                                      RGAttachmentMutability::Write,
                                      RHI::AttachmentLoadOp::Load,
                                      RHI::AttachmentStoreOp::Store,
                                      RHI::ResourceState::DepthWrite,
                                      RHI::ResourceState::ShaderResource))
        {
            return;
        }
        m_DepthHandle = depthHandle.ToResourceHandle();

        m_IdHandle = builder.WriteTextureAttachment(RenderGraphResourceNames::VisBufferId,
                                                    VisibilityBuffer::MakeIdTextureDesc(width, height),
                                                    RGAttachmentKind::Color,
                                                    RHI::AttachmentLoadOp::Clear,
                                                    RHI::AttachmentStoreOp::Store,
                                                    RHI::ResourceState::RenderTarget,
                                                    RHI::ResourceState::ShaderResource);

        // スキニングの変形した頂点を、頂点シェーダーが storage buffer として読む。SkinningComputePass が、このフレームに
        // 変形するインスタンスを持つときだけ宣言する（名前を持たないフレームに TryGetBuffer すると、グラフのエラーになる）
        if (m_SkinningComputePass)
        {
            const RGResourceHandle skinnedVertices = m_SkinningComputePass->GetCurrentVerticesHandle();
            if (skinnedVertices.IsValid())
            {
                builder.Read(skinnedVertices, RHI::ResourceState::GenericRead);
                m_SkinnedVerticesHandle = skinnedVertices;
            }
        }

        builder.PreserveInsertionOrder();
    }

    void VisibilityRasterPass::CollectProceduralChunks(ViewRenderContext& context,
                                                       uint32_t recordBase,
                                                       VariableArray<VisibilityBuffer::DrawRecord>& records,
                                                       VariableArray<ChunkDraw>& draws)
    {
        MeshResources* meshes = context.Resources.Meshes;
        if (!meshes || !context.InstanceDataBuffer)
        {
            return;
        }

        const DrawCommandView commands = context.GetActiveOpaqueCommands();
        VariableArray<MeshIndexChunk> chunks;
        for (uint32_t commandIndex = 0; commandIndex < commands.Count; ++commandIndex)
        {
            const DrawCommand& command = commands.Data[commandIndex];
            if (command.Draw.PayloadKind != DrawPayloadKind::Mesh || !command.Draw.MeshHandle.IsValid())
            {
                continue;
            }
            const auto* gpuData = meshes->GetGPUData(command.Draw.MeshHandle);
            if (!gpuData || !gpuData->VertexBuffer || !gpuData->IndexBuffer)
            {
                continue;
            }

            // GBuffer の経路（SceneRenderer::RecordMeshDrawCall）と同じ範囲・インスタンスの選び方
            const bool bHasRange = command.Draw.IndexCount > 0;
            const uint32_t indexCount = bHasRange ? command.Draw.IndexCount : gpuData->IndexCount;
            const uint32_t firstIndex = bHasRange ? command.Draw.IndexOffset : 0u;
            const uint32_t vertexOffset = bHasRange ? command.Draw.VertexOffset : 0u;
            const uint32_t instanceCount = std::max(1u, command.Draw.InstanceCount);

            BuildMeshIndexChunks(indexCount, nullptr, 0, chunks);
            const uint64_t vertexAddress = gpuData->VertexBuffer->GetDeviceAddress();
            const uint64_t indexAddress = gpuData->IndexBuffer->GetDeviceAddress();
            for (uint32_t instanceOffset = 0; instanceOffset < instanceCount; ++instanceOffset)
            {
                const uint32_t instanceIndex = command.Draw.FirstInstance + instanceOffset;
                for (const MeshIndexChunk& chunk : chunks)
                {
                    const uint32_t recordNumber = recordBase + static_cast<uint32_t>(records.size());
                    if (!VisibilityBuffer::IsValidRecordNumber(recordNumber))
                    {
                        ++m_Stats.DroppedChunks;
                        continue;
                    }

                    VisibilityBuffer::DrawRecord record = MakeChunkRecord(VisibilityBuffer::RecordKind::ProceduralChunk,
                                                                          instanceIndex,
                                                                          command.Draw.MaterialIndex,
                                                                          chunk.IndexCount / 3u,
                                                                          firstIndex + chunk.FirstIndex,
                                                                          vertexOffset);
                    // 前のフレームの変換は、描画のインスタンスの表の同じ要素（previousWorld）にある
                    record.PreviousTransformIndex = instanceIndex;
                    record.VertexAddress = vertexAddress;
                    record.IndexAddress = indexAddress;
                    if (!VisibilityBuffer::IsValidRecord(record))
                    {
                        ++m_Stats.DroppedChunks;
                        continue;
                    }
                    records.push_back(record);

                    ChunkDraw draw;
                    draw.VertexBuffer = gpuData->VertexBuffer;
                    draw.IndexBuffer = gpuData->IndexBuffer;
                    draw.IndexCount = chunk.IndexCount;
                    draw.FirstIndex = record.FirstIndex;
                    draw.VertexOffset = static_cast<int32_t>(vertexOffset);
                    draw.RecordNumber = recordNumber;
                    draws.push_back(draw);
                    ++m_Stats.ProceduralRecords;
                }
            }
        }
    }

    void VisibilityRasterPass::CollectSkinnedChunks(ViewRenderContext& context,
                                                    uint32_t recordBase,
                                                    VariableArray<VisibilityBuffer::DrawRecord>& records,
                                                    VariableArray<ChunkDraw>& draws)
    {
        if (!m_SkinningComputePass)
        {
            return;
        }

        const VariableArray<SkinningComputeInstance>& instances = m_SkinningComputePass->GetInstances();
        VariableArray<MeshIndexChunk> chunks;
        for (uint32_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex)
        {
            const SkinningComputeInstance& instance = instances[instanceIndex];
            if (!instance.IndexBuffer || instance.IndexCount < 3)
            {
                continue;
            }

            // 登録時に分けた塊（サブメッシュの境目で区切る）を使い、無ければインデックスの全体を分ける
            if (!context.SkinnedMeshes || !context.SkinnedMeshes->TryGetChunks(instance.MeshHandle, chunks) ||
                chunks.empty())
            {
                BuildMeshIndexChunks(instance.IndexCount, nullptr, 0, chunks);
            }

            const uint64_t indexAddress = instance.IndexBuffer->GetDeviceAddress();
            for (const MeshIndexChunk& chunk : chunks)
            {
                const uint32_t recordNumber = recordBase + static_cast<uint32_t>(records.size());
                if (!VisibilityBuffer::IsValidRecordNumber(recordNumber))
                {
                    ++m_Stats.DroppedChunks;
                    continue;
                }

                VisibilityBuffer::DrawRecord record = MakeChunkRecord(VisibilityBuffer::RecordKind::SkinnedChunk,
                                                                      instanceIndex,
                                                                      instance.MaterialIndex,
                                                                      chunk.IndexCount / 3u,
                                                                      chunk.FirstIndex,
                                                                      0u);
                // 頂点のアドレスはインスタンスの先頭まで加算済みなので、記録の頂点の基点は0にする（二重に加えない）
                record.VertexAddress = instance.CurrentVertexAddress;
                record.IndexAddress = indexAddress;
                record.PreviousVertexAddress = instance.PreviousVertexAddress;
                if (!VisibilityBuffer::IsValidRecord(record))
                {
                    ++m_Stats.DroppedChunks;
                    continue;
                }
                records.push_back(record);

                ChunkDraw draw;
                draw.IndexBuffer = instance.IndexBuffer;
                draw.IndexCount = chunk.IndexCount;
                draw.FirstIndex = chunk.FirstIndex;
                draw.VertexOffset = static_cast<int32_t>(instance.VertexBase);
                draw.RecordNumber = recordNumber;
                draws.push_back(draw);
                ++m_Stats.SkinnedRecords;
            }
        }
    }

    void VisibilityRasterPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Stats = VisibilityRasterFrameStats{};
        m_LastRecordTable.reset();
        m_LastRecordTableBytes = 0;

        // そのフレームの MegaGeometry の描画の写し。取り出すと、MegaGeometryPass が残したバッファの戻しはこのパスの責任になる
        MegaGeometryPass::VisibilityDrawPlan plan;
        const bool bHasPlan = m_MegaGeometryPass && m_MegaGeometryPass->TakeVisibilityDrawPlan(plan);
        RHI::ICommandList* commandList = context.CommandList;

        // 描けないときは、残してあったバッファをそのまま戻して終える
        const auto bail = [&]() -> void
        {
            if (m_MegaGeometryPass)
            {
                m_MegaGeometryPass->ReleaseVisibilityDrawBuffers(commandList);
            }
        };

        if (!m_bInitialized && !Initialize(context))
        {
            bail();
            return;
        }
        if (!commandList || !m_RenderPass || !m_MegaPipeline || !m_MeshPipeline || !m_SkinnedPipeline ||
            !m_RecordsPipeline || !m_IdHandle.IsValid() || !m_DepthHandle.IsValid())
        {
            bail();
            return;
        }

        const RHI::TexturePtr idTexture = resources.GetTexture(m_IdHandle);
        const RHI::TexturePtr depthTexture = resources.GetTexture(m_DepthHandle);
        if (!idTexture || !depthTexture || idTexture->GetWidth() != depthTexture->GetWidth() ||
            idTexture->GetHeight() != depthTexture->GetHeight() || !EnsureFramebuffer(idTexture, depthTexture))
        {
            bail();
            return;
        }

        const RHI::Viewport viewport = context.GetActiveLocalViewport();
        const RHI::ScissorRect scissor = context.GetActiveLocalScissor();
        const CameraProxy* camera = context.GetActiveCamera();

        // 描くものの記録と描画を集める（MegaGeometry の記録の枠 → 手続き → スキニングの順に番号を振る）
        const uint32_t megaSlots = bHasPlan ? plan.CommandsTotal : 0u;
        const uint32_t recordBase = 1u + megaSlots;
        VariableArray<VisibilityBuffer::DrawRecord> cpuRecords;
        VariableArray<ChunkDraw> meshDraws;
        VariableArray<ChunkDraw> skinnedDraws;
        if (camera)
        {
            CollectProceduralChunks(context, recordBase, cpuRecords, meshDraws);
            CollectSkinnedChunks(context, recordBase, cpuRecords, skinnedDraws);
        }
        const bool bHasMegaDraw = bHasPlan && camera && megaSlots > 0 && VisibilityBuffer::IsValidRecordNumber(megaSlots) &&
                                  plan.InstanceBuffer && plan.DrawInfoBuffer && plan.IndirectBuffer &&
                                  plan.CountBuffer && plan.SectionBuffer;
        if (!camera || (!bHasMegaDraw && cpuRecords.empty()))
        {
            // 描くものが無いフレームも、ID を空で消して ShaderResource へ渡す
            commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
            commandList->SetViewport(viewport);
            commandList->SetScissor(scissor);
            commandList->EndRenderPass();
            m_Stats.bRendered = true;
            bail();
            return;
        }

        const uint32_t totalSlots = recordBase + static_cast<uint32_t>(cpuRecords.size());
        m_Stats.MegaCommandSlots = bHasMegaDraw ? megaSlots : 0u;
        m_Stats.TotalSlots = totalSlots;

        FrameSlot& slot = m_FrameSlots[m_FrameCounter % FrameSlotCount];
        ++m_FrameCounter;
        if (!EnsureFrameSlot(slot, totalSlots, bHasMegaDraw ? plan.SectionCount : 1u))
        {
            NORVES_LOG_ERROR("VisibilityRasterPass", "ビジビリティバッファの資源を用意できませんでした");
            bail();
            return;
        }

        // view・projection（GBuffer・MegaGeometry と同じカメラの定数）
        {
            const CameraViewConstants cameraConstants =
                CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
            float frameData[32];
            cameraConstants.CopyShaderView(frameData);
            cameraConstants.CopyShaderProjection(frameData + 16);
            slot.FrameUniform->Update(frameData, sizeof(frameData));
        }

        // ホストが書く記録（手続き・スキニング）。MegaGeometry の範囲の後ろに置く
        if (!cpuRecords.empty())
        {
            slot.RecordTable->Update(cpuRecords.data(),
                                     static_cast<uint64_t>(cpuRecords.size()) * RecordBytes,
                                     static_cast<uint64_t>(recordBase) * RecordBytes);
        }
        const uint64_t tableBytes = static_cast<uint64_t>(totalSlots) * RecordBytes;

        // MegaGeometry の記録は、そのフレームに積まれたコマンドから GPU が書く
        RHI::ResourceState indirectState = RHI::ResourceState::IndirectArgument;
        if (bHasMegaDraw)
        {
            // コマンド・カウンタを計算が読めるようにする（描画情報は GenericRead で渡される）
            commandList->BufferBarrier(plan.IndirectBuffer, indirectState, RHI::ResourceState::GenericRead);
            commandList->BufferBarrier(plan.CountBuffer, indirectState, RHI::ResourceState::GenericRead);
            indirectState = RHI::ResourceState::GenericRead;

            VariableArray<uint32_t> addresses;
            addresses.reserve(static_cast<size_t>(plan.SectionCount) * 4u);
            uint32_t maxCapacity = 0;
            for (const MegaGeometryPass::VisibilityDrawPlan::Section& section : plan.Sections)
            {
                const uint64_t vertexAddress = section.VertexBuffer ? section.VertexBuffer->GetDeviceAddress() : 0ull;
                const uint64_t indexAddress = section.IndexBuffer ? section.IndexBuffer->GetDeviceAddress() : 0ull;
                addresses.push_back(static_cast<uint32_t>(vertexAddress & 0xFFFFFFFFull));
                addresses.push_back(static_cast<uint32_t>(vertexAddress >> 32));
                addresses.push_back(static_cast<uint32_t>(indexAddress & 0xFFFFFFFFull));
                addresses.push_back(static_cast<uint32_t>(indexAddress >> 32));
                maxCapacity = std::max(maxCapacity, section.Capacity);
            }
            slot.SectionAddresses->Update(addresses.data(), addresses.size() * sizeof(uint32_t));

            const uint32_t params[4] = {plan.SectionCount, plan.SectionCount * plan.PassCount, 0u, 0u};
            slot.RecordParams->Update(params, sizeof(params));

            slot.RecordSet->BindConstantBuffer(0, slot.RecordParams, 0, RecordParamsBytes);
            slot.RecordSet->BindStorageBuffer(1, plan.IndirectBuffer, 0, ClampBindSize(plan.IndirectBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(2, plan.CountBuffer, 0, ClampBindSize(plan.CountBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(3, plan.SectionBuffer, 0, ClampBindSize(plan.SectionBufferBytes));
            slot.RecordSet->BindStorageBuffer(4, plan.DrawInfoBuffer, 0, ClampBindSize(plan.DrawInfoBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(5, slot.SectionAddresses, 0, ClampBindSize(addresses.size() * sizeof(uint32_t)));
            slot.RecordSet->BindStorageBuffer(6, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.RecordSet->Update();

            commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::UnorderedAccess);
            commandList->SetPipeline(m_RecordsPipeline);
            commandList->SetDescriptorSet(slot.RecordSet, 0);
            const uint32_t groupsX = std::max(1u, std::min((maxCapacity + RecordThreadsPerGroup - 1u) / RecordThreadsPerGroup, 65535u));
            commandList->Dispatch(groupsX, std::max(1u, plan.SectionCount * plan.PassCount), 1u);
            commandList->BufferBarrier(slot.RecordTable, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        }
        else
        {
            commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::GenericRead);
        }
        slot.RecordState = RHI::ResourceState::GenericRead;

        // 描画のディスクリプタセット
        if (bHasMegaDraw)
        {
            slot.MegaSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.MegaSet->BindStorageBuffer(1, plan.InstanceBuffer, 0, ClampBindSize(plan.InstanceBufferBytes));
            slot.MegaSet->BindStorageBuffer(2, plan.DrawInfoBuffer, 0, ClampBindSize(plan.DrawInfoBuffer->GetSize()));
            slot.MegaSet->Update();
        }
        const bool bDrawMesh = !meshDraws.empty();
        if (bDrawMesh)
        {
            slot.MeshSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.MeshSet->BindStorageBuffer(1, context.InstanceDataBuffer, 0, ClampBindSize(context.InstanceDataBuffer->GetSize()));
            slot.MeshSet->BindStorageBuffer(2, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.MeshSet->Update();
        }
        const RHI::BufferPtr skinnedVertices =
            m_SkinnedVerticesHandle.IsValid() ? resources.GetBuffer(m_SkinnedVerticesHandle) : RHI::BufferPtr{};
        const bool bDrawSkinned = !skinnedDraws.empty() && skinnedVertices;
        if (bDrawSkinned)
        {
            slot.SkinnedSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.SkinnedSet->BindStorageBuffer(1, skinnedVertices, 0, ClampBindSize(skinnedVertices->GetSize()));
            slot.SkinnedSet->BindStorageBuffer(2, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.SkinnedSet->Update();
        }

        // ID と深度へ描く
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);

        if (bHasMegaDraw)
        {
            commandList->SetPipeline(m_MegaPipeline);
            commandList->SetDescriptorSet(slot.MegaSet, 0);
            for (uint32_t passIndex = 0; passIndex < plan.PassCount; ++passIndex)
            {
                for (uint32_t sectionIndex = 0; sectionIndex < plan.Sections.size(); ++sectionIndex)
                {
                    const MegaGeometryPass::VisibilityDrawPlan::Section& section = plan.Sections[sectionIndex];
                    if (section.Capacity == 0 || !section.VertexBuffer || !section.IndexBuffer)
                    {
                        continue;
                    }
                    commandList->SetVertexBuffer(section.VertexBuffer, 0, 0);
                    commandList->SetIndexBuffer(section.IndexBuffer, 0);

                    const uint64_t commandOffsetBytes =
                        (static_cast<uint64_t>(passIndex) * plan.CommandsPerPass + section.CommandBase) * IndirectCommandBytes;
                    if (plan.bUseIndirectCount)
                    {
                        commandList->DrawIndexedIndirectCount(
                            plan.IndirectBuffer, commandOffsetBytes,
                            plan.CountBuffer,
                            static_cast<uint64_t>(passIndex * plan.SectionCount + sectionIndex) * sizeof(uint32_t),
                            section.Capacity,
                            IndirectCommandBytes);
                    }
                    else
                    {
                        commandList->DrawIndexedIndirect(plan.IndirectBuffer, commandOffsetBytes, section.Capacity,
                                                         IndirectCommandBytes);
                    }
                }
            }
        }

        if (bDrawMesh)
        {
            commandList->SetPipeline(m_MeshPipeline);
            commandList->SetDescriptorSet(slot.MeshSet, 0);
            const RHI::IBuffer* boundVertex = nullptr;
            const RHI::IBuffer* boundIndex = nullptr;
            for (const ChunkDraw& draw : meshDraws)
            {
                if (draw.VertexBuffer.get() != boundVertex)
                {
                    commandList->SetVertexBuffer(draw.VertexBuffer, 0, 0);
                    boundVertex = draw.VertexBuffer.get();
                }
                if (draw.IndexBuffer.get() != boundIndex)
                {
                    commandList->SetIndexBuffer(draw.IndexBuffer, 0);
                    boundIndex = draw.IndexBuffer.get();
                }
                commandList->DrawIndexedInstanced(draw.IndexCount, 1, draw.FirstIndex, draw.VertexOffset, draw.RecordNumber);
            }
        }

        if (bDrawSkinned)
        {
            commandList->SetPipeline(m_SkinnedPipeline);
            commandList->SetDescriptorSet(slot.SkinnedSet, 0);
            const RHI::IBuffer* boundIndex = nullptr;
            for (const ChunkDraw& draw : skinnedDraws)
            {
                if (draw.IndexBuffer.get() != boundIndex)
                {
                    commandList->SetIndexBuffer(draw.IndexBuffer, 0);
                    boundIndex = draw.IndexBuffer.get();
                }
                commandList->DrawIndexedInstanced(draw.IndexCount, 1, draw.FirstIndex, draw.VertexOffset, draw.RecordNumber);
            }
        }

        commandList->EndRenderPass();

        // MegaGeometryPass が残したバッファを次のフレーム用に戻す
        if (m_MegaGeometryPass)
        {
            m_MegaGeometryPass->ReleaseVisibilityDrawBuffers(commandList, indirectState);
        }

        m_Stats.bRendered = true;
        m_LastRecordTable = slot.RecordTable;
        m_LastRecordTableBytes = tableBytes;

        // 描画の内訳が変わったときだけ記録する（毎フレームは書かない）
        if (!m_bLoggedStats || m_LoggedStats.MegaCommandSlots != m_Stats.MegaCommandSlots ||
            m_LoggedStats.ProceduralRecords != m_Stats.ProceduralRecords ||
            m_LoggedStats.SkinnedRecords != m_Stats.SkinnedRecords ||
            m_LoggedStats.DroppedChunks != m_Stats.DroppedChunks)
        {
            m_LoggedStats = m_Stats;
            m_bLoggedStats = true;
            NORVES_LOG_INFO("VisibilityRasterPass",
                            "VIS_RASTER mega_command_slots=%u procedural_chunks=%u skinned_chunks=%u dropped_chunks=%u "
                            "record_slots=%u",
                            m_Stats.MegaCommandSlots,
                            m_Stats.ProceduralRecords,
                            m_Stats.SkinnedRecords,
                            m_Stats.DroppedChunks,
                            m_Stats.TotalSlots);
        }
    }

    // ========================================
    // VisibilityDebugPass
    // ========================================

    VisibilityDebugPass::VisibilityDebugPass() = default;

    VisibilityDebugPass::~VisibilityDebugPass()
    {
        Shutdown();
    }

    bool VisibilityDebugPass::Initialize(ViewRenderContext& context)
    {
        m_Device = context.Device;
        m_bInitialized = true;
        if (!m_Device || !context.ShaderMgr)
        {
            return true;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = context.ShaderMgr->LoadShader("visbuffer_debug.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("VisibilityDebugPass", "検証表示のシェーダーの読み込みに失敗。このパスは何もしません");
            return true;
        }

        // ID は整数なのでフィルターしない（texelFetch で読む）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = m_Device->CreateSampler(samplerDesc);
        for (uint32_t index = 0; index < 2; ++index)
        {
            m_DescriptorSet[index] = m_Device->CreateDescriptorSet(MakeDebugDescriptorSetDesc());
            m_ParamsUniform[index] = m_Device->CreateBuffer(
                RHI::BufferDesc(DebugParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_DebugParams"));
        }
        if (!m_Sampler || !m_DescriptorSet[0] || !m_DescriptorSet[1] || !m_ParamsUniform[0] || !m_ParamsUniform[1])
        {
            NORVES_LOG_WARNING("VisibilityDebugPass", "検証表示の資源の作成に失敗。このパスは何もしません");
            m_Sampler.reset();
        }
        return true;
    }

    void VisibilityDebugPass::Shutdown()
    {
        m_Pipeline.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_FramebufferColor = nullptr;
        m_ColorFormat = RHI::Format::UNKNOWN;
        m_Sampler.reset();
        for (uint32_t index = 0; index < 2; ++index)
        {
            m_DescriptorSet[index].reset();
            m_ParamsUniform[index].reset();
        }
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ColorHandle = {};
        m_IdHandle = {};
        m_Device = nullptr;
        m_bInitialized = false;
    }

    void VisibilityDebugPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VisibilityDebugPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void VisibilityDebugPass::Declare(RenderGraphBuilder& builder)
    {
        m_ColorHandle = {};
        m_IdHandle = {};

        // ID は VisibilityRasterPass が書いたもの。無ければ（描画のパスが何も宣言しなかった）色の添付も宣言しない
        RGTextureHandle idHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::VisBufferId, idHandle, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        // 最後のシーンの色（SSR があればその出力）へ書く
        RGTextureHandle colorHandle;
        if (!builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                 colorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource) &&
            !builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                 colorHandle,
                                                 RHI::AttachmentLoadOp::Load,
                                                 RHI::AttachmentStoreOp::Store,
                                                 RHI::ResourceState::RenderTarget,
                                                 RHI::ResourceState::ShaderResource))
        {
            return;
        }

        m_ColorHandle = colorHandle.ToResourceHandle();
        m_IdHandle = idHandle;
        builder.PreserveInsertionOrder();
    }

    void VisibilityDebugPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        if (!context.CommandList || !m_Device || !m_ColorHandle.IsValid() || !m_IdHandle.IsValid())
        {
            return;
        }

        const RHI::TexturePtr colorTexture = resources.GetTexture(m_ColorHandle);
        const RHI::TexturePtr idTexture = resources.GetTexture(m_IdHandle);
        if (!colorTexture || !idTexture)
        {
            return;
        }

        // 色の添付の形式が変わったときだけ、レンダーパス・パイプラインを作り直す
        if (!m_RenderPass || m_ColorFormat != colorTexture->GetFormat())
        {
            m_Pipeline.reset();
            m_RenderPass.reset();
            m_Framebuffer.reset();
            m_FramebufferColor = nullptr;

            RHI::RenderPassDesc renderPassDesc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = colorTexture->GetFormat();
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::Load;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            renderPassDesc.colorAttachments.push_back(colorAttachment);
            m_RenderPass = m_Device->CreateRenderPass(renderPassDesc);
            m_ColorFormat = colorTexture->GetFormat();

            if (m_RenderPass && m_VertexShader && m_FragmentShader)
            {
                RHI::GraphicsPipelineDesc pipelineDesc;
                pipelineDesc.vertexShader = m_VertexShader;
                pipelineDesc.pixelShader = m_FragmentShader;
                pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
                pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
                pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
                pipelineDesc.rasterState.lineWidth = 1.0f;
                pipelineDesc.depthStencilState.depthTestEnable = false;
                pipelineDesc.depthStencilState.depthWriteEnable = false;
                RHI::BlendAttachmentDesc blendAttachment;
                blendAttachment.blendEnable = false;
                blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
                pipelineDesc.blendState.attachments.push_back(blendAttachment);
                pipelineDesc.renderPass = m_RenderPass;
                pipelineDesc.descriptorSetLayouts.push_back(MakeDebugDescriptorSetDesc());
                m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            }
        }
        if (!m_RenderPass)
        {
            return;
        }

        if (!m_Framebuffer || m_FramebufferColor != colorTexture.get())
        {
            RHI::FramebufferDesc framebufferDesc;
            framebufferDesc.renderPass = m_RenderPass;
            framebufferDesc.colorTargets.push_back(colorTexture);
            framebufferDesc.width = colorTexture->GetWidth();
            framebufferDesc.height = colorTexture->GetHeight();
            m_Framebuffer = m_Device->CreateFramebuffer(framebufferDesc);
            m_FramebufferColor = m_Framebuffer ? colorTexture.get() : nullptr;
        }
        if (!m_Framebuffer)
        {
            return;
        }

        // 記録の表が無い（VisibilityRasterPass がこのフレームに書かなかった）ときは、色を触らずに添付の状態だけを進める
        const RHI::BufferPtr recordTable = m_RasterPass ? m_RasterPass->GetRecordTable() : RHI::BufferPtr{};
        const uint32_t slotIndex = static_cast<uint32_t>(m_FrameCounter % 2u);
        ++m_FrameCounter;
        const bool bCanDraw = m_Pipeline && recordTable && m_Sampler && m_DescriptorSet[slotIndex] && m_ParamsUniform[slotIndex];
        if (bCanDraw)
        {
            const float params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            m_ParamsUniform[slotIndex]->Update(params, sizeof(params));
            RHI::DescriptorSetPtr& descriptorSet = m_DescriptorSet[slotIndex];
            descriptorSet->BindTexture(0, idTexture);
            descriptorSet->BindSampler(0, m_Sampler);
            descriptorSet->BindStorageBuffer(1, recordTable, 0, ClampBindSize(m_RasterPass->GetRecordTableBytes()));
            descriptorSet->BindConstantBuffer(2, m_ParamsUniform[slotIndex], 0, DebugParamsBytes);
            descriptorSet->Update();
        }

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        if (bCanDraw)
        {
            commandList->SetPipeline(m_Pipeline);
            commandList->SetDescriptorSet(m_DescriptorSet[slotIndex], 0);
            commandList->Draw(3, 0);
        }
        commandList->EndRenderPass();
    }

} // namespace NorvesLib::Core::Rendering

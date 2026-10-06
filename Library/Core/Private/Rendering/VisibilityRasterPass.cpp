#include "Rendering/VisibilityRasterPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/RenderResources.h"
#include "Rendering/RenderTypes.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ScopedGpuTimestamp.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VisibilityResolvePass.h"
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
#include <cstdlib>
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
        // 間接 dispatch の引数（VkDispatchIndirectCommand 3 語 + ワークグループの総数 1 語）の後ろに、区間の表の添え字ごとの
        // 先頭のワークグループの番号が続く（最後の 1 語は総数）。visbuffer_records_args.comp が書く
        constexpr uint32_t RecordArgsHeaderWords = 4;
        // 記録を書く計算の 1 ワークグループのスレッド数と、間接 dispatch の x の上限（visbuffer_records*.comp と一致させる）
        constexpr uint32_t RecordThreadsPerGroup = 64;
        constexpr uint32_t RecordMaxGroupsX = 65535;

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

        // 記録を書く計算のディスクリプタセット（visbuffer_records.comp の binding 0〜8。引数を作る計算
        // visbuffer_records_args.comp も同じ組を使う。binding 8 は引数を書く側が RW、記録を書く側が読み取り）
        RHI::DescriptorSetDesc MakeRecordDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute);
            for (uint32_t binding = 1; binding <= 5; ++binding)
            {
                AddBinding(desc, binding, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute);
            }
            AddBinding(desc, 6, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute);
            AddBinding(desc, 7, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute);
            AddBinding(desc, 8, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute);
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
    // VisibilityMerge
    // ========================================

    namespace
    {
        // 合流のパスのディスクリプタセット（visbuffer_merge.frag の binding 0・1）
        RHI::DescriptorSetDesc MakeMergeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            AddBinding(desc, 0, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Pixel);
            AddBinding(desc, 1, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Pixel);
            return desc;
        }

        constexpr uint32_t MergeParamsBytes = 16; // uvec4（x = 幅、y = 高さ）
        // 作り直しで手放した 64bit のバッファを持っておくフレーム数（飛行中のフレームの数より長く）
        constexpr uint64_t MergeRetiredBufferFrames = FrameUseRingMaxInFlightSlots + 1;
        constexpr double BytesPerMegabyte = 1024.0 * 1024.0;
    } // namespace

    VisibilityMerge::VisibilityMerge() = default;

    VisibilityMerge::~VisibilityMerge()
    {
        Shutdown();
    }

    bool VisibilityMerge::IsSupported(const RHI::DeviceCapabilities& capabilities)
    {
        // bShaderBufferInt64Atomics は、64bit 整数（shaderInt64）が有効になったときだけ true になる
        return capabilities.bShaderBufferInt64Atomics && capabilities.bShaderInt64;
    }

    RHI::RenderPassDesc VisibilityMerge::MakeLoadRenderPassDesc()
    {
        RHI::RenderPassDesc desc;

        // ID: 前の描画の内容へ重ねる。ShaderResource の状態から始まり、ShaderResource で終わる
        RHI::AttachmentDesc idAttachment;
        idAttachment.format = RHI::Format::R32_UINT;
        idAttachment.isDepthStencil = false;
        idAttachment.clear = false;
        idAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        idAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        idAttachment.initialState = RHI::ResourceState::ShaderResource;
        idAttachment.finalState = RHI::ResourceState::ShaderResource;
        desc.colorAttachments.push_back(idAttachment);

        desc.hasDepthStencil = true;
        desc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        desc.depthStencilAttachment.isDepthStencil = true;
        desc.depthStencilAttachment.clear = false;
        desc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        desc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        desc.depthStencilAttachment.initialState = RHI::ResourceState::ShaderResource;
        desc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;
        return desc;
    }

    bool VisibilityMerge::Initialize(RHI::IDevice* device,
                                     ShaderManager* shaderManager,
                                     const RHI::RenderPassPtr& loadRenderPass)
    {
        Shutdown();
        if (!device || !shaderManager || !loadRenderPass || !IsSupported(device->GetCapabilities()))
        {
            return false;
        }

        m_VertexShader = shaderManager->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = shaderManager->LoadShader("visbuffer_merge.frag", RHI::ShaderStage::Pixel);
        if (!m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("VisibilityMerge", "64bit のバッファの合流のシェーダーの読み込みに失敗。合流は行いません");
            Shutdown();
            return false;
        }

        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_VertexShader;
        pipelineDesc.pixelShader = m_FragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;
        // ハードのラスタ（LessOrEqual）と同じ比較。同じ深度ならソフトウェアラスタの ID が残る
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::LessOrEqual;
        RHI::BlendAttachmentDesc blendAttachment;
        blendAttachment.blendEnable = false;
        blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
        pipelineDesc.blendState.attachments.push_back(blendAttachment);
        pipelineDesc.renderPass = loadRenderPass;
        pipelineDesc.descriptorSetLayouts.push_back(MakeMergeDescriptorSetDesc());
        m_Pipeline = device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("VisibilityMerge", "64bit のバッファの合流のパイプラインの作成に失敗。合流は行いません");
            Shutdown();
            return false;
        }

        m_Device = device;
        return true;
    }

    void VisibilityMerge::Shutdown()
    {
        m_Uses.Clear();
        m_RetiredBuffers.clear();
        m_KeyBuffer.reset();
        m_KeyWidth = 0;
        m_KeyHeight = 0;
        m_KeyBufferCreateCount = 0;
        m_KeyState = RHI::ResourceState::Common;
        m_FrameSerial = 0;
        m_Pipeline.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_Device = nullptr;
    }

    void VisibilityMerge::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
        m_FrameSerial = frameSerial;
        ReleaseStaleBuffers();
    }

    void VisibilityMerge::ReleaseStaleBuffers()
    {
        for (size_t index = 0; index < m_RetiredBuffers.size();)
        {
            if (m_FrameSerial > m_RetiredBuffers[index].RetiredSerial &&
                m_FrameSerial - m_RetiredBuffers[index].RetiredSerial > MergeRetiredBufferFrames)
            {
                m_RetiredBuffers[index] = m_RetiredBuffers.back();
                m_RetiredBuffers.pop_back();
            }
            else
            {
                ++index;
            }
        }
    }

    bool VisibilityMerge::EnsureKeyBuffer(uint32_t width, uint32_t height)
    {
        if (!IsReady() || !m_Device || width == 0 || height == 0)
        {
            return false;
        }
        if (m_KeyBuffer && m_KeyWidth == width && m_KeyHeight == height)
        {
            return true;
        }

        // 大きさが変わったので作り直す。古いバッファは、GPU が前のフレームで使っているかもしれないので、数フレーム持っておく
        const uint64_t bytes = static_cast<uint64_t>(width) * height * VisibilityBuffer::KEY_BYTES;
        RHI::BufferDesc desc(bytes, RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst, false, "VisBuffer_Key64");
        RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
        if (!buffer)
        {
            NORVES_LOG_ERROR("VisibilityMerge", "64bit のバッファ（%ux%u、%llu バイト）を作れませんでした",
                             width, height, static_cast<unsigned long long>(bytes));
            return false;
        }
        if (m_KeyBuffer)
        {
            m_RetiredBuffers.push_back(RetiredBuffer{m_KeyBuffer, m_FrameSerial});
        }
        m_KeyBuffer = buffer;
        m_KeyWidth = width;
        m_KeyHeight = height;
        m_KeyState = RHI::ResourceState::Common;
        ++m_KeyBufferCreateCount;
        NORVES_LOG_INFO("VisibilityMerge",
                        "VRAM_LEDGER visbuffer64 width=%u height=%u mb=%.2f",
                        width,
                        height,
                        static_cast<double>(bytes) / BytesPerMegabyte);
        return true;
    }

    bool VisibilityMerge::RecordClear(RHI::ICommandList* commandList)
    {
        if (!IsReady() || !commandList || !m_KeyBuffer)
        {
            return false;
        }

        // すべてのビットを 1 にする（32bit の語を 0xFFFFFFFF で埋める）。次の書き込み（転送）は前のフレームの読み取りの後に並ぶ
        const uint64_t bytes = m_KeyBuffer->GetSize();
        commandList->BufferBarrier(m_KeyBuffer, m_KeyState, RHI::ResourceState::CopyDest);
        commandList->FillBuffer(m_KeyBuffer, 0, bytes, VisibilityBuffer::KEY_EMPTY_WORD);
        commandList->BufferBarrier(m_KeyBuffer, RHI::ResourceState::CopyDest, RHI::ResourceState::GenericRead);
        m_KeyState = RHI::ResourceState::GenericRead;
        return true;
    }

    bool VisibilityMerge::RecordMerge(RHI::ICommandList* commandList,
                                      const RHI::RenderPassPtr& renderPass,
                                      const RHI::FramebufferPtr& framebuffer,
                                      const RHI::Viewport& viewport,
                                      const RHI::ScissorRect& scissor)
    {
        if (!IsReady() || !m_Device || !commandList || !renderPass || !framebuffer || !m_KeyBuffer ||
            m_KeyState != RHI::ResourceState::GenericRead)
        {
            return false;
        }

        // 資源は呼び出しの回数ではなくフレームの枠で決める（提出前・GPU が読み終わる前の資源を上書きしない）
        Use& use = m_Uses.Acquire();
        if (!use.Params)
        {
            use.Params = m_Device->CreateBuffer(
                RHI::BufferDesc(MergeParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_MergeParams"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeMergeDescriptorSetDesc());
        }
        if (!use.Params || !use.DescriptorSet)
        {
            NORVES_LOG_WARNING("VisibilityMerge", "合流の資源の作成に失敗。この合流は何もしません");
            return false;
        }

        const uint32_t params[4] = {m_KeyWidth, m_KeyHeight, 0u, 0u};
        use.Params->Update(params, sizeof(params));
        use.DescriptorSet->BindStorageBuffer(0, m_KeyBuffer, 0, ClampBindSize(m_KeyBuffer->GetSize()));
        use.DescriptorSet->BindConstantBuffer(1, use.Params, 0, MergeParamsBytes);
        use.DescriptorSet->Update();

        commandList->BeginRenderPass(renderPass, framebuffer);
        commandList->SetViewport(viewport);
        commandList->SetScissor(scissor);
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        commandList->Draw(3, 0);
        commandList->EndRenderPass();
        return true;
    }

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
        m_RecordArgsShader = context.ShaderMgr->LoadShader("visbuffer_records_args.comp", RHI::ShaderStage::Compute);
        if (!m_MegaVertexShader || !m_MeshVertexShader || !m_SkinnedVertexShader || !m_FragmentShader ||
            !m_RecordsShader || !m_RecordArgsShader)
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
            m_RecordArgsPipeline.reset();
            m_MegaWireframePipeline.reset();
            m_MeshWireframePipeline.reset();
            m_SkinnedWireframePipeline.reset();
            NORVES_LOG_WARNING("VisibilityRasterPass", "ビジビリティバッファのパイプラインの作成に失敗。このパスは何もしません");
            return true;
        }

        // 64bit のバッファの合流は、ソフトウェアラスタが有効で、対応する装置のときだけ作る。作れなくても ID の描画は使える（合流だけが無い）
        if (m_bSwRasterEnabled && VisibilityMerge::IsSupported(caps))
        {
            m_Merge.Initialize(m_Device, context.ShaderMgr, m_SecondRenderPass);
        }
        return true;
    }

    bool VisibilityRasterPass::IsDrawReady(DebugViewMode mode) const
    {
        const bool bFillReady = m_bInitialized && m_RenderPass && m_MegaPipeline && m_MeshPipeline &&
                                m_SkinnedPipeline && m_RecordsPipeline && m_RecordArgsPipeline;
        return bFillReady && (mode != DebugViewMode::Wireframe || HasWireframePipelines());
    }

    bool VisibilityRasterPass::HasWireframePipelines() const
    {
#if NORVES_BUILD_DEVELOPMENT
        return m_MegaWireframePipeline && m_MeshWireframePipeline && m_SkinnedWireframePipeline;
#else
        return false;
#endif
    }

    void VisibilityRasterPass::Shutdown()
    {
        m_Merge.Shutdown();
        m_FrameSlots.Clear();
        m_ProceduralChunkScratch = Container::VariableArray<MeshIndexChunk>{};
        m_SkinnedChunkScratch = Container::VariableArray<MeshIndexChunk>{};
        m_MaterialEntryScratch = Container::VariableArray<VisibilityBuffer::MaterialEntry>{};
        m_SectionMaterialScratch = Container::VariableArray<uint32_t>{};
        m_MegaPipeline.reset();
        m_MeshPipeline.reset();
        m_SkinnedPipeline.reset();
        m_RecordsPipeline.reset();
        m_RecordArgsPipeline.reset();
        m_MegaWireframePipeline.reset();
        m_MeshWireframePipeline.reset();
        m_SkinnedWireframePipeline.reset();
        m_MegaVertexShader.reset();
        m_MeshVertexShader.reset();
        m_SkinnedVertexShader.reset();
        m_FragmentShader.reset();
        m_RecordsShader.reset();
        m_RecordArgsShader.reset();
        m_Framebuffer.reset();
        m_SecondFramebuffer.reset();
        m_SecondRenderPass.reset();
        m_Work = FrameWork{};
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
        if (!m_RenderPass)
        {
            return false;
        }

        // 2 回目の render pass: 1 回目の描画の後に続けて開くので、ID・深度とも ShaderResource の状態から始まり、内容を Load する
        // （2 パスの遮蔽で、HZB を作った後に MegaGeometry の 2 パス目を描く。64bit のバッファの合流も同じ形で描く）
        m_SecondRenderPass = m_Device->CreateRenderPass(VisibilityMerge::MakeLoadRenderPassDesc());
        return m_SecondRenderPass != nullptr;
    }

    bool VisibilityRasterPass::CreatePipelines(ViewRenderContext& /*context*/)
    {
        if (!m_Device || !m_RenderPass)
        {
            return false;
        }

        // 3種類の描画のパイプライン。ID を書く1枚のカラー添付と深度だけが違わず、頂点シェーダーと頂点入力だけが違う。
        // 線の描き方（ワイヤーフレーム）は polygonMode だけが違い、ID は塗りと同じく三角形の gl_PrimitiveID を書く
        const auto createGraphics = [this](const RHI::ShaderPtr& vertexShader,
                                           bool bVertexInput,
                                           RHI::PolygonMode polygonMode,
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
            pipelineDesc.rasterState.polygonMode = polygonMode;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;

            // 同じ値以下なら ID を書く（同じ式の位置は同じ深度になる）。GBuffer の描画（Less）と比較をそろえない理由:
            //  - 解決を使わないとき（Debug と、On でも GetFallbackReason が予備を選んだとき）は、GBuffer が先に同じ式の位置の
            //    深度を書き、このパスはその深度を Load して描く。Less だと手続きメッシュと MegaGeometry の同じ深度の画素の ID が
            //    落ちる（スキニングは計算シェーダーで変形するので深度はビット単位では一致しない）。LessOrEqual が必須。
            //  - 解決を使うときは GBuffer が描かずクリアだけを行い、このパスだけが深度を書く。同じ深度（共有の辺・同一平面の面）では
            //    後に描いた側が勝つ（GBuffer の Less は先に描いた側が勝つ）。塗りでは top-left 規則で共有の辺が二重に塗られないので
            //    起動画面の差は 0〜2 階調だが、ワイヤーフレームの表示では材質の境目の線の色が入れ替わる（約 50 階調）。
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

        if (!createGraphics(m_MegaVertexShader, true, RHI::PolygonMode::Fill, m_MegaPipeline) ||
            !createGraphics(m_MeshVertexShader, true, RHI::PolygonMode::Fill, m_MeshPipeline) ||
            !createGraphics(m_SkinnedVertexShader, false, RHI::PolygonMode::Fill, m_SkinnedPipeline))
        {
            return false;
        }

#if NORVES_BUILD_DEVELOPMENT
        // 線のパイプラインが作れなくても塗りの描画は使える。3 種が揃わないときは作れたぶんも捨て、
        // IsDrawReady(Wireframe) を false にして GBuffer のワイヤーフレームの描画へ戻す
        if (!createGraphics(m_MegaVertexShader, true, RHI::PolygonMode::Line, m_MegaWireframePipeline) ||
            !createGraphics(m_MeshVertexShader, true, RHI::PolygonMode::Line, m_MeshWireframePipeline) ||
            !createGraphics(m_SkinnedVertexShader, false, RHI::PolygonMode::Line, m_SkinnedWireframePipeline))
        {
            m_MegaWireframePipeline.reset();
            m_MeshWireframePipeline.reset();
            m_SkinnedWireframePipeline.reset();
            NORVES_LOG_WARNING("VisibilityRasterPass",
                               "VIS_RASTER_WIREFRAME_UNAVAILABLE 線の描き方のパイプラインを作れません。"
                               "ワイヤーフレームの表示では従来の GBuffer の描画を使います");
        }
#endif

        RHI::ComputePipelineDesc computeDesc;
        computeDesc.computeShader = m_RecordsShader;
        computeDesc.descriptorSetLayouts.push_back(MakeRecordDescriptorSetDesc());
        m_RecordsPipeline = m_Device->CreateComputePipeline(computeDesc);
        if (!m_RecordsPipeline)
        {
            return false;
        }

        // 引数を作る計算は、記録を書く計算と同じディスクリプタセットの形を使う
        computeDesc.computeShader = m_RecordArgsShader;
        m_RecordArgsPipeline = m_Device->CreateComputePipeline(computeDesc);
        return m_RecordArgsPipeline != nullptr;
    }

    bool VisibilityRasterPass::EnsureFramebuffer(const RHI::TexturePtr& idTexture, const RHI::TexturePtr& depthTexture)
    {
        if (m_Framebuffer && m_SecondFramebuffer && m_FramebufferId == idTexture.get() &&
            m_FramebufferDepth == depthTexture.get())
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
        m_SecondFramebuffer.reset();
        if (m_Framebuffer)
        {
            RHI::FramebufferDesc secondDesc = desc;
            secondDesc.renderPass = m_SecondRenderPass;
            m_SecondFramebuffer = m_Device->CreateFramebuffer(secondDesc);
        }
        const bool bCreated = m_Framebuffer && m_SecondFramebuffer;
        m_FramebufferId = bCreated ? idTexture.get() : nullptr;
        m_FramebufferDepth = bCreated ? depthTexture.get() : nullptr;
        return bCreated;
    }

    bool VisibilityRasterPass::EnsureFrameSlot(FrameSlot& slot,
                                               uint32_t recordCapacity,
                                               uint32_t sectionCount,
                                               uint32_t sectionSlotCount,
                                               uint32_t cpuRecordCount,
                                               uint32_t materialCount)
    {
        if (!m_Device)
        {
            return false;
        }

        // この組を最後に使ったのは、同じ飛行中のフレームの番号の前のフレーム（フェンスで GPU の完了を待ってある）なので、
        // 作り直して置き換えてよい。同じフレームの別の Execute には別の組が渡る
        if (!slot.RecordTable || slot.RecordCapacity < recordCapacity)
        {
            const uint32_t capacity = std::max(64u, NextPowerOfTwo(recordCapacity));
            // GPU 専用のメモリ（ホストが書く記録は RecordUpload からのコピーで入れる）
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * RecordBytes,
                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::TransferDst,
                                 false,
                                 "VisBuffer_DrawRecords");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.RecordTable = buffer;
            slot.RecordCapacity = capacity;
            slot.RecordState = RHI::ResourceState::Common;
            NORVES_LOG_INFO("VisibilityRasterPass",
                            "VRAM_LEDGER visbuffer_records mb=%.2f",
                            static_cast<double>(desc.Size) / BytesPerMegabyte);
        }

        if (cpuRecordCount > 0 && (!slot.RecordUpload || slot.RecordUploadCapacity < cpuRecordCount))
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(cpuRecordCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * RecordBytes,
                                 RHI::ResourceUsage::TransferSrc,
                                 true,
                                 "VisBuffer_RecordUpload");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.RecordUpload = buffer;
            slot.RecordUploadCapacity = capacity;
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

        if (!slot.SectionMaterials || slot.SectionMaterialCapacity < sectionCount)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(sectionCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "VisBuffer_SectionMaterials");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.SectionMaterials = buffer;
            slot.SectionMaterialCapacity = capacity;
        }

        if (!slot.RecordArgs || slot.RecordArgsCapacity < sectionSlotCount)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(sectionSlotCount));
            // GPU だけが書き、記録を書く計算の間接 dispatch の引数として読む
            RHI::BufferDesc desc(static_cast<uint64_t>(RecordArgsHeaderWords + capacity + 1u) * sizeof(uint32_t),
                                 RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
                                 false,
                                 "VisBuffer_RecordArgs");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.RecordArgs = buffer;
            slot.RecordArgsCapacity = capacity;
            slot.RecordArgsState = RHI::ResourceState::Common;
        }

        if (!slot.MaterialTable || slot.MaterialTableCapacity < materialCount)
        {
            const uint32_t capacity = std::max(16u, NextPowerOfTwo(materialCount));
            RHI::BufferDesc desc(static_cast<uint64_t>(capacity) * sizeof(VisibilityBuffer::MaterialEntry),
                                 RHI::ResourceUsage::StorageBuffer,
                                 true,
                                 "VisBuffer_MaterialTable");
            RHI::BufferPtr buffer = m_Device->CreateBuffer(desc);
            if (!buffer)
            {
                return false;
            }
            slot.MaterialTable = buffer;
            slot.MaterialTableCapacity = capacity;
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
               slot.RecordSet && slot.RecordArgs;
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
        // 解決が使えず予備の GBuffer の描画へ戻るフレームは、ID を誰も読まない。描かない（分類も ID が無いので何もしない）
        if (m_ResolvePass && !m_ResolvePass->CanResolve(context->Device, context->GetActiveDebugMode()))
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

    void VisibilityRasterPass::LogChunkFailureOnce()
    {
        if (m_bLoggedChunkFailure)
        {
            return;
        }
        m_bLoggedChunkFailure = true;
        NORVES_LOG_WARNING("VisibilityRasterPass", "インデックスを塊に分けられないメッシュがあります。そのメッシュはビジビリティバッファへ描きません");
    }

    void VisibilityRasterPass::CollectProceduralChunks(ViewRenderContext& context,
                                                       uint32_t recordBase,
                                                       VisibilityBuffer::MaterialTable& materials,
                                                       VariableArray<VisibilityBuffer::DrawRecord>& records,
                                                       VariableArray<ChunkDraw>& draws)
    {
        MeshResources* meshes = context.Resources.Meshes;
        if (!meshes || !context.InstanceDataBuffer)
        {
            return;
        }

        const DrawCommandView commands = context.GetActiveOpaqueCommands();
        VariableArray<MeshIndexChunk>& chunks = m_ProceduralChunkScratch;
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

            // 実物の材質（GBuffer の経路が引くものと同じ）を、フレームの材質の表の番号にする
            const MaterialResourceData* materialData =
                (command.Draw.MaterialHandle.IsValid() && context.Resources.Materials)
                    ? context.Resources.Materials->GetData(command.Draw.MaterialHandle)
                    : nullptr;
            const uint32_t materialIndex = materials.Add(VisibilityBuffer::MakeMaterialEntry(materialData));

            // 描画の範囲（サブメッシュ・インスタンス）ごとに範囲が違うので、塊は範囲から作る。
            // 登録時にメッシュ全体で分けた塊は持たない（範囲に合わないので読まない）
            if (!BuildMeshIndexChunks(indexCount, nullptr, 0, chunks))
            {
                LogChunkFailureOnce();
                continue;
            }
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
                                                                          materialIndex,
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
                                                    VisibilityBuffer::MaterialTable& materials,
                                                    VariableArray<VisibilityBuffer::DrawRecord>& records,
                                                    VariableArray<ChunkDraw>& draws)
    {
        if (!m_SkinningComputePass)
        {
            return;
        }

        const VariableArray<SkinningComputeInstance>& instances = m_SkinningComputePass->GetInstances();
        VariableArray<MeshIndexChunk>& chunks = m_SkinnedChunkScratch;
        for (uint32_t instanceIndex = 0; instanceIndex < instances.size(); ++instanceIndex)
        {
            const SkinningComputeInstance& instance = instances[instanceIndex];
            if (!instance.IndexBuffer || instance.IndexCount < 3)
            {
                continue;
            }

            // 登録時に分けた塊を使い、無ければインデックスの全体を分ける
            if (!context.SkinnedMeshes || !context.SkinnedMeshes->TryGetChunks(instance.MeshHandle, chunks) ||
                chunks.empty())
            {
                if (!BuildMeshIndexChunks(instance.IndexCount, nullptr, 0, chunks))
                {
                    LogChunkFailureOnce();
                    continue;
                }
            }

            const uint64_t indexAddress = instance.IndexBuffer->GetDeviceAddress();
            const MaterialResourceData* materialData =
                (instance.Material.IsValid() && context.Resources.Materials)
                    ? context.Resources.Materials->GetData(instance.Material)
                    : nullptr;
            const uint32_t materialIndex = materials.Add(VisibilityBuffer::MakeMaterialEntry(materialData));
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
                                                                      materialIndex,
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

    VisibilityRasterPass::PrepareResult VisibilityRasterPass::PrepareFrame(const MegaGeometryPass::VisibilityDrawPlan* plan)
    {
        ViewRenderContext& context = *m_Work.Context;
        const CameraProxy* camera = m_Work.Camera;
        const bool bHasPlan = plan != nullptr;

        // 描くものの記録と描画を集める（MegaGeometry の記録の枠 → 手続き → スキニングの順に番号を振る）
        const uint32_t megaSlots = bHasPlan ? plan->CommandsTotal : 0u;
        const uint32_t recordBase = 1u + megaSlots;
        VariableArray<VisibilityBuffer::DrawRecord>& cpuRecords = m_Work.CpuRecords;
        VariableArray<ChunkDraw>& meshDraws = m_Work.MeshDraws;
        VariableArray<ChunkDraw>& skinnedDraws = m_Work.SkinnedDraws;

        // フレームの材質の表: MegaGeometry の区間 → 手続き → スキニングの順に、実物の材質を 0 から詰めた番号にする
        m_MaterialTable.Clear();
        VariableArray<uint32_t>& sectionMaterials = m_SectionMaterialScratch;
        sectionMaterials.clear();
        if (bHasPlan && camera)
        {
            sectionMaterials.reserve(plan->Sections.size());
            for (const MegaGeometryPass::VisibilityDrawPlan::Section& section : plan->Sections)
            {
                sectionMaterials.push_back(m_MaterialTable.Add(VisibilityBuffer::MakeMaterialEntry(section.Material)));
            }
        }
        if (camera)
        {
            CollectProceduralChunks(context, recordBase, m_MaterialTable, cpuRecords, meshDraws);
            CollectSkinnedChunks(context, recordBase, m_MaterialTable, cpuRecords, skinnedDraws);
        }
        m_Work.bHasMegaDraw = bHasPlan && camera && megaSlots > 0 && VisibilityBuffer::IsValidRecordNumber(megaSlots) &&
                              plan->InstanceBuffer && plan->DrawInfoBuffer && plan->IndirectBuffer &&
                              plan->CountBuffer && plan->SectionBuffer;
        const bool bHasMegaDraw = m_Work.bHasMegaDraw;
        if (!camera || (!bHasMegaDraw && cpuRecords.empty()))
        {
            return PrepareResult::ClearOnly;
        }

        const uint32_t totalSlots = recordBase + static_cast<uint32_t>(cpuRecords.size());
        m_Stats.MegaCommandSlots = bHasMegaDraw ? megaSlots : 0u;
        m_Stats.TotalSlots = totalSlots;

        m_FrameSlots.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        FrameSlot& slot = m_FrameSlots.Acquire();
        m_Work.Slot = &slot;
        m_MaterialTable.BuildGpuEntriesInto(m_MaterialEntryScratch);
        const VariableArray<VisibilityBuffer::MaterialEntry>& materialEntries = m_MaterialEntryScratch;
        m_Stats.MaterialUnique = m_MaterialTable.GetUniqueCount();
        m_Stats.MaterialLimit = m_MaterialTable.GetLimit();
        m_Stats.MaterialOverflowed = m_MaterialTable.GetOverflowedCount();
        if (!EnsureFrameSlot(slot,
                             totalSlots,
                             bHasMegaDraw ? plan->SectionCount : 1u,
                             bHasMegaDraw ? plan->SectionCount * plan->PassCount : 1u,
                             static_cast<uint32_t>(cpuRecords.size()),
                             static_cast<uint32_t>(materialEntries.size())))
        {
            NORVES_LOG_ERROR("VisibilityRasterPass", "ビジビリティバッファの資源を用意できませんでした");
            return PrepareResult::Failed;
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

        // ホストが書く記録（手続き・スキニング）。MegaGeometry の範囲の後ろに置く。ホスト可視の置き場へ書き、
        // 記録の表（GPU 専用）へは GPU がコピーする（RecordCpuRecordUpload）
        m_Work.UploadBytes = 0;
        m_Work.UploadDstOffset = 0;
        if (!cpuRecords.empty())
        {
            const uint64_t uploadBytes = static_cast<uint64_t>(cpuRecords.size()) * RecordBytes;
            slot.RecordUpload->Update(cpuRecords.data(), uploadBytes, 0);
            m_Work.UploadBytes = uploadBytes;
            m_Work.UploadDstOffset = static_cast<uint64_t>(recordBase) * RecordBytes;
        }
        const uint64_t tableBytes = static_cast<uint64_t>(totalSlots) * RecordBytes;
        m_Work.TableBytes = tableBytes;

        // 材質の表（ホストが書く。材質の解決が記録の MaterialIndex で引く）
        if (!materialEntries.empty())
        {
            slot.MaterialTable->Update(materialEntries.data(),
                                       static_cast<uint64_t>(materialEntries.size()) * sizeof(VisibilityBuffer::MaterialEntry));
        }

        // MegaGeometry の記録は、そのフレームに積まれたコマンドから GPU が書く（計算は RecordMegaRecords）。
        // ここでは、計算が読む区間のアドレス・材質の番号・パラメータと、ディスクリプタセットをホストで書く
        if (bHasMegaDraw)
        {
            VariableArray<uint32_t>& addresses = m_Work.SectionAddresses;
            addresses.clear();
            addresses.reserve(static_cast<size_t>(plan->SectionCount) * 4u);
            for (const MegaGeometryPass::VisibilityDrawPlan::Section& section : plan->Sections)
            {
                const uint64_t vertexAddress = section.VertexBuffer ? section.VertexBuffer->GetDeviceAddress() : 0ull;
                const uint64_t indexAddress = section.IndexBuffer ? section.IndexBuffer->GetDeviceAddress() : 0ull;
                addresses.push_back(static_cast<uint32_t>(vertexAddress & 0xFFFFFFFFull));
                addresses.push_back(static_cast<uint32_t>(vertexAddress >> 32));
                addresses.push_back(static_cast<uint32_t>(indexAddress & 0xFFFFFFFFull));
                addresses.push_back(static_cast<uint32_t>(indexAddress >> 32));
            }
            slot.SectionAddresses->Update(addresses.data(), addresses.size() * sizeof(uint32_t));

            // 区間ごとの材質の表の番号（plan.Sections と同じ並び）
            slot.SectionMaterials->Update(sectionMaterials.data(), sectionMaterials.size() * sizeof(uint32_t));

            const uint32_t params[4] = {plan->SectionCount, plan->SectionCount * plan->PassCount, 0u, 0u};
            slot.RecordParams->Update(params, sizeof(params));

            slot.RecordSet->BindConstantBuffer(0, slot.RecordParams, 0, RecordParamsBytes);
            slot.RecordSet->BindStorageBuffer(1, plan->IndirectBuffer, 0, ClampBindSize(plan->IndirectBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(2, plan->CountBuffer, 0, ClampBindSize(plan->CountBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(3, plan->SectionBuffer, 0, ClampBindSize(plan->SectionBufferBytes));
            slot.RecordSet->BindStorageBuffer(4, plan->DrawInfoBuffer, 0, ClampBindSize(plan->DrawInfoBuffer->GetSize()));
            slot.RecordSet->BindStorageBuffer(5, slot.SectionAddresses, 0, ClampBindSize(addresses.size() * sizeof(uint32_t)));
            slot.RecordSet->BindStorageBuffer(6, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.RecordSet->BindStorageBuffer(7, slot.SectionMaterials, 0, ClampBindSize(sectionMaterials.size() * sizeof(uint32_t)));
            slot.RecordSet->BindStorageBuffer(8, slot.RecordArgs, 0, ClampBindSize(slot.RecordArgs->GetSize()));
            slot.RecordSet->Update();

            m_Work.MegaInstanceBuffer = plan->InstanceBuffer;
            m_Work.MegaInstanceBytes = plan->InstanceBufferBytes;
        }

        // 描画のディスクリプタセット
        if (bHasMegaDraw)
        {
            slot.MegaSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.MegaSet->BindStorageBuffer(1, plan->InstanceBuffer, 0, ClampBindSize(plan->InstanceBufferBytes));
            slot.MegaSet->BindStorageBuffer(2, plan->DrawInfoBuffer, 0, ClampBindSize(plan->DrawInfoBuffer->GetSize()));
            slot.MegaSet->Update();
        }
        m_Work.bDrawMesh = !meshDraws.empty();
        if (m_Work.bDrawMesh)
        {
            slot.MeshSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.MeshSet->BindStorageBuffer(1, context.InstanceDataBuffer, 0, ClampBindSize(context.InstanceDataBuffer->GetSize()));
            slot.MeshSet->BindStorageBuffer(2, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.MeshSet->Update();
        }
        const RHI::BufferPtr& skinnedVertices = m_Work.SkinnedVertices;
        m_Work.bDrawSkinned = !skinnedDraws.empty() && skinnedVertices;
        if (m_Work.bDrawSkinned)
        {
            slot.SkinnedSet->BindConstantBuffer(0, slot.FrameUniform, 0, FrameUniformBytes);
            slot.SkinnedSet->BindStorageBuffer(1, skinnedVertices, 0, ClampBindSize(skinnedVertices->GetSize()));
            slot.SkinnedSet->BindStorageBuffer(2, slot.RecordTable, 0, ClampBindSize(tableBytes));
            slot.SkinnedSet->Update();
        }
        return PrepareResult::Ready;
    }

    void VisibilityRasterPass::RecordClearOnlyRenderPass()
    {
        // 描くものが無いフレームも、ID を空で消して ShaderResource へ渡す
        RHI::ICommandList* commandList = m_Work.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(m_Work.Viewport);
        commandList->SetScissor(m_Work.Scissor);
        commandList->EndRenderPass();
    }

    bool VisibilityRasterPass::RecordCpuRecordUpload()
    {
        if (m_Work.UploadBytes == 0)
        {
            return false;
        }

        RHI::ICommandList* commandList = m_Work.CommandList;
        FrameSlot& slot = *m_Work.Slot;
        commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::CopyDest);
        commandList->CopyBuffer(slot.RecordUpload, slot.RecordTable, m_Work.UploadBytes, 0, m_Work.UploadDstOffset);
        commandList->BufferBarrier(slot.RecordTable, RHI::ResourceState::CopyDest, RHI::ResourceState::GenericRead);
        slot.RecordState = RHI::ResourceState::GenericRead;
        return true;
    }

    void VisibilityRasterPass::RecordMegaRecords(const MegaGeometryPass::VisibilityDrawPlan& plan)
    {
        RHI::ICommandList* commandList = m_Work.CommandList;
        FrameSlot& slot = *m_Work.Slot;
        ScopedGpuTimestamp gpuTimestamp(commandList, "VisRasterRecords");

        // コマンド・カウンタを計算が読めるようにする（描画情報は GenericRead で渡される）
        commandList->BufferBarrier(plan.IndirectBuffer, m_Work.IndirectState, RHI::ResourceState::GenericRead);
        commandList->BufferBarrier(plan.CountBuffer, m_Work.IndirectState, RHI::ResourceState::GenericRead);
        m_Work.IndirectState = RHI::ResourceState::GenericRead;

        // 区間ごとに積まれたコマンドの数から、記録を書く計算の dispatch の引数を作る（1 スレッドの小さな計算）
        commandList->BufferBarrier(slot.RecordArgs, slot.RecordArgsState, RHI::ResourceState::UnorderedAccess);
        commandList->SetPipeline(m_RecordArgsPipeline);
        commandList->SetDescriptorSet(slot.RecordSet, 0);
        commandList->Dispatch(1u, 1u, 1u);
        commandList->BufferBarrier(slot.RecordArgs, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        slot.RecordArgsState = RHI::ResourceState::GenericRead;

        // 積まれたコマンドの数だけのワークグループで記録を書く
        commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::UnorderedAccess);
        commandList->SetPipeline(m_RecordsPipeline);
        commandList->SetDescriptorSet(slot.RecordSet, 0);
        if (!commandList->DispatchIndirect(slot.RecordArgs, 0))
        {
            // 間接 dispatch を断るコマンドリスト: 区間の容量から数えた上限のグループ数で直接 dispatch する。
            // 余りのグループはシェーダーが引数の合計（[3]）で捨てる
            uint64_t groupLimit = 0;
            for (const MegaGeometryPass::VisibilityDrawPlan::Section& section : plan.Sections)
            {
                groupLimit += (static_cast<uint64_t>(section.Capacity) + RecordThreadsPerGroup - 1u) / RecordThreadsPerGroup;
            }
            groupLimit *= std::max(plan.PassCount, 1u);
            groupLimit = std::max<uint64_t>(groupLimit, 1u);
            const uint32_t groupsX = static_cast<uint32_t>(std::min<uint64_t>(groupLimit, RecordMaxGroupsX));
            const uint32_t groupsY = static_cast<uint32_t>((groupLimit + RecordMaxGroupsX - 1u) / RecordMaxGroupsX);
            commandList->Dispatch(groupsX, groupsY, 1u);
            if (!m_bLoggedRecordsDirectFallback)
            {
                m_bLoggedRecordsDirectFallback = true;
                NORVES_LOG_WARNING("VisibilityRasterPass",
                                   "VIS_RASTER_RECORDS_DIRECT_FALLBACK groups=%llu "
                                   "間接 dispatch を断られたため、記録の計算を直接の dispatch で走らせます",
                                   static_cast<unsigned long long>(groupLimit));
            }
        }
        commandList->BufferBarrier(slot.RecordTable, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        slot.RecordState = RHI::ResourceState::GenericRead;
    }

    void VisibilityRasterPass::RecordMegaDraws(const MegaGeometryPass::VisibilityDrawPlan& plan,
                                               uint32_t firstPass,
                                               uint32_t endPass)
    {
        RHI::ICommandList* commandList = m_Work.CommandList;
        commandList->SetPipeline(m_Work.MegaPipeline);
        commandList->SetDescriptorSet(m_Work.Slot->MegaSet, 0);
        for (uint32_t passIndex = firstPass; passIndex < endPass; ++passIndex)
        {
            // 区間の名前は MegaGeometryPass の予備の描画と同じ（off と比べられる）
            ScopedGpuTimestamp drawTimestamp(commandList, passIndex == 0 ? "MegaGeometryDraw1" : "MegaGeometryDraw2");
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

    void VisibilityRasterPass::RecordChunkDraws()
    {
        if (!m_Work.bDrawMesh && !m_Work.bDrawSkinned)
        {
            return;
        }

        RHI::ICommandList* commandList = m_Work.CommandList;
        FrameSlot& slot = *m_Work.Slot;
        ScopedGpuTimestamp gpuTimestamp(commandList, "VisRasterChunks");

        if (m_Work.bDrawMesh)
        {
            commandList->SetPipeline(m_Work.MeshPipeline);
            commandList->SetDescriptorSet(slot.MeshSet, 0);
            const RHI::IBuffer* boundVertex = nullptr;
            const RHI::IBuffer* boundIndex = nullptr;
            for (const ChunkDraw& draw : m_Work.MeshDraws)
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

        if (m_Work.bDrawSkinned)
        {
            commandList->SetPipeline(m_Work.SkinnedPipeline);
            commandList->SetDescriptorSet(slot.SkinnedSet, 0);
            const RHI::IBuffer* boundIndex = nullptr;
            for (const ChunkDraw& draw : m_Work.SkinnedDraws)
            {
                if (draw.IndexBuffer.get() != boundIndex)
                {
                    commandList->SetIndexBuffer(draw.IndexBuffer, 0);
                    boundIndex = draw.IndexBuffer.get();
                }
                commandList->DrawIndexedInstanced(draw.IndexCount, 1, draw.FirstIndex, draw.VertexOffset, draw.RecordNumber);
            }
        }
    }

    void VisibilityRasterPass::RecordFirstPassDraws(RHI::ICommandList* /*commandList*/,
                                                    const MegaGeometryPass::VisibilityDrawPlan& plan)
    {
        m_Work.bSinkUsed = true;
        if (PrepareFrame(&plan) != PrepareResult::Ready)
        {
            // 描くものが無い・準備できない: ID を空で消すだけ（MegaGeometryPass の HZB は空の深度から作られ、判定は描く側に倒れる）
            RecordClearOnlyRenderPass();
            return;
        }

        RHI::ICommandList* commandList = m_Work.CommandList;
        FrameSlot& slot = *m_Work.Slot;

        // ホストが書いた記録（手続き・スキニング）を表へコピーし、頂点シェーダーが読めるようにする。
        // MegaGeometry の範囲は 2 パス目の後に計算が書く
        if (!RecordCpuRecordUpload())
        {
            commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::GenericRead);
            slot.RecordState = RHI::ResourceState::GenericRead;
        }

        // 64bit のバッファを空で埋める（ソフトウェアラスタが書く前。1 回目の合流が読む）
        RecordMergeClear();

        // 1 回目: 手続き・スキニングの塊と MegaGeometry の 1 パス目。この深度から MegaGeometryPass が HZB を作る
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(m_Work.Viewport);
        commandList->SetScissor(m_Work.Scissor);
        RecordChunkDraws();
        if (m_Work.bHasMegaDraw)
        {
            RecordMegaDraws(plan, 0, 1);
        }
        commandList->EndRenderPass();
        m_Work.bStagedReady = true;
    }

    void VisibilityRasterPass::RecordMergeBeforeHiZ(RHI::ICommandList* /*commandList*/,
                                                    const MegaGeometryPass::VisibilityDrawPlan& /*plan*/)
    {
        if (!m_Work.bStagedReady)
        {
            return;
        }

        // 1 回目の描画の後の深度（HZB の元）へ、64bit のバッファの値を合流させる
        RecordMergePass("VisRasterMerge1");
    }

    void VisibilityRasterPass::RecordSecondPassDraws(RHI::ICommandList* /*commandList*/,
                                                     const MegaGeometryPass::VisibilityDrawPlan& plan)
    {
        if (!m_Work.bStagedReady || !m_Work.bHasMegaDraw)
        {
            return;
        }

        // 2 パス目のカリングの結果から MegaGeometry の記録を書き、2 回目の render pass で 2 パス目を描く
        RecordMegaRecords(plan);
        RHI::ICommandList* commandList = m_Work.CommandList;
        commandList->BeginRenderPass(m_SecondRenderPass, m_SecondFramebuffer);
        commandList->SetViewport(m_Work.Viewport);
        commandList->SetScissor(m_Work.Scissor);
        RecordMegaDraws(plan, 1, 2);
        commandList->EndRenderPass();

        // 2 パス目の後の ID・深度へ、64bit のバッファの値（1 パス目・2 パス目のぶん）を合流させる
        RecordMergePass("VisRasterMerge2");
    }

    void VisibilityRasterPass::RecordMergeClear()
    {
        if (!m_Work.bMerge)
        {
            return;
        }
        ScopedGpuTimestamp gpuTimestamp(m_Work.CommandList, "VisRasterMergeClear");
        m_Merge.RecordClear(m_Work.CommandList);
    }

    void VisibilityRasterPass::RecordMergePass(const char* timestampName)
    {
        if (!m_Work.bMerge)
        {
            return;
        }

        ScopedGpuTimestamp gpuTimestamp(m_Work.CommandList, timestampName);
        if (m_Merge.RecordMerge(m_Work.CommandList, m_SecondRenderPass, m_SecondFramebuffer, m_Work.Viewport, m_Work.Scissor))
        {
            ++m_Stats.MergeCount;
        }
    }

    void VisibilityRasterPass::FinishFrame()
    {
        // MegaGeometryPass が残したバッファを次のフレーム用に戻す
        if (m_MegaGeometryPass)
        {
            m_MegaGeometryPass->ReleaseVisibilityDrawBuffers(m_Work.CommandList, m_Work.IndirectState);
        }

        FrameSlot& slot = *m_Work.Slot;
        const VariableArray<VisibilityBuffer::MaterialEntry>& materialEntries = m_MaterialEntryScratch;
        m_Stats.bRendered = true;
        m_Stats.bMerged = m_Stats.MergeCount > 0;
        m_Stats.KeyBufferBytes = m_Work.bMerge ? m_Merge.GetKeyBufferBytes() : 0u;
        m_LastRecordTable = slot.RecordTable;
        m_LastRecordTableBytes = m_Work.TableBytes;
        m_LastMaterialTable = slot.MaterialTable;
        m_LastMaterialTableCount = static_cast<uint32_t>(materialEntries.size());
        m_LastMaterialEntries.assign(materialEntries.begin(), materialEntries.end());
        if (m_Work.bHasMegaDraw)
        {
            m_LastMegaInstanceBuffer = m_Work.MegaInstanceBuffer;
            m_LastMegaInstanceBytes = m_Work.MegaInstanceBytes;
        }

        // 材質の数は、変わったときだけログへ書く。上限を超えたら、通知を一度だけ出す
        if (!m_bLoggedMaterials || m_LoggedMaterialUnique != m_Stats.MaterialUnique ||
            m_LoggedMaterialLimit != m_Stats.MaterialLimit)
        {
            m_bLoggedMaterials = true;
            m_LoggedMaterialUnique = m_Stats.MaterialUnique;
            m_LoggedMaterialLimit = m_Stats.MaterialLimit;
            NORVES_LOG_INFO("VisibilityRasterPass",
                            "VISBUFFER_MATERIALS unique=%u limit=%u",
                            m_Stats.MaterialUnique,
                            m_Stats.MaterialLimit);
        }
        if (m_Stats.MaterialOverflowed > 0 && !m_bLoggedMaterialOverflow)
        {
            m_bLoggedMaterialOverflow = true;
            NORVES_LOG_WARNING("VisibilityRasterPass",
                               "VISBUFFER_MATERIAL_OVERFLOW unique=%u limit=%u overflowed=%u 材質の数が表の上限を超えました。"
                               "溢れた材質は予備の番号（%u）へ寄せます",
                               m_Stats.MaterialUnique,
                               m_Stats.MaterialLimit,
                               m_Stats.MaterialOverflowed,
                               m_MaterialTable.GetFallbackIndex());
        }

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

    void VisibilityRasterPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_Stats = VisibilityRasterFrameStats{};
        m_LastRecordTable.reset();
        m_LastRecordTableBytes = 0;
        m_LastMaterialTable.reset();
        m_LastMaterialTableCount = 0;
        m_LastMaterialEntries.clear();
        m_LastMegaInstanceBuffer.reset();
        m_LastMegaInstanceBytes = 0;
        m_Work = FrameWork{};

        RHI::ICommandList* commandList = context.CommandList;

        // GBuffer の描画を止める構成は、MegaGeometryPass が記録をこのパスへ移す。必要なものを確かめた後、下でここから記録を駆動する。
        // 移さないフレームは、MegaGeometryPass が先に記録して残した描画の写しを取り出す。
        // 取り出すと、MegaGeometryPass が残したバッファの戻しはこのパスの責任になる
        const bool bDeferred = m_MegaGeometryPass && m_MegaGeometryPass->TakeFrameRecordDeferred();
        MegaGeometryPass::VisibilityDrawPlan plan;
        bool bHasPlan = !bDeferred && m_MegaGeometryPass && m_MegaGeometryPass->TakeVisibilityDrawPlan(plan);

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
        if (!commandList || !m_RenderPass || !m_SecondRenderPass || !m_MegaPipeline || !m_MeshPipeline ||
            !m_SkinnedPipeline || !m_RecordsPipeline || !m_RecordArgsPipeline || !m_IdHandle.IsValid() || !m_DepthHandle.IsValid())
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

        m_Work.Context = &context;
        m_Work.CommandList = commandList;
        m_Work.Viewport = context.GetActiveLocalViewport();
        m_Work.Scissor = context.GetActiveLocalScissor();
        m_Work.Camera = context.GetActiveCamera();
        m_Work.SkinnedVertices =
            m_SkinnedVerticesHandle.IsValid() ? resources.GetBuffer(m_SkinnedVerticesHandle) : RHI::BufferPtr{};

        // ワイヤーフレームの表示では、塗りの代わりに線のパイプラインで描く（GBuffer のワイヤーフレームと同じ線になる。
        // 解決が線の画素を GBuffer へ書く）。線のパイプラインが揃わないときは、解決のパスが従来の GBuffer の描画へ戻している
        const bool bWireframe = context.GetActiveDebugMode() == DebugViewMode::Wireframe && HasWireframePipelines();
        m_Work.MegaPipeline = bWireframe ? m_MegaWireframePipeline : m_MegaPipeline;
        m_Work.MeshPipeline = bWireframe ? m_MeshWireframePipeline : m_MeshPipeline;
        m_Work.SkinnedPipeline = bWireframe ? m_SkinnedWireframePipeline : m_SkinnedPipeline;

        // 64bit のバッファ: 対応する装置で、ワイヤーフレームの表示でないときだけ使う（ワイヤーフレームは誰も書かない）
        if (m_Merge.IsReady() && !bWireframe)
        {
            m_Merge.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
            m_Work.bMerge = m_Merge.EnsureKeyBuffer(idTexture->GetWidth(), idTexture->GetHeight());
        }

        if (bDeferred)
        {
            // 2 パスの遮蔽になるときは、MegaGeometryPass の記録の途中（IDrawSink）で ID・深度へ描く。
            // 1 回の判定になったとき・記録できなかったときは呼ばれないので、残った描画の写しを下で取り出して全部を 1 回で描く
            FrameCommand command = context.BuildMegaGeometryPassCommand(m_MegaGeometryPass);
            m_MegaGeometryPass->RecordFrameCommand(command.MegaGeometry, commandList, this);
            if (!m_Work.bSinkUsed)
            {
                bHasPlan = m_MegaGeometryPass->TakeVisibilityDrawPlan(plan);
            }
        }

        if (m_Work.bSinkUsed)
        {
            if (!m_Work.bStagedReady)
            {
                // 1 回目の呼び出しで描くものが無かった・準備できなかった（ID は空で消してある）
                m_Stats.bRendered = true;
                bail();
                return;
            }
            FinishFrame();
            return;
        }

        const PrepareResult prepared = PrepareFrame(bHasPlan ? &plan : nullptr);
        if (prepared == PrepareResult::ClearOnly)
        {
            RecordClearOnlyRenderPass();
            m_Stats.bRendered = true;
            bail();
            return;
        }
        if (prepared == PrepareResult::Failed)
        {
            bail();
            return;
        }

        // MegaGeometry の記録は、そのフレームに積まれたコマンドから GPU が書く
        FrameSlot& slot = *m_Work.Slot;
        const bool bUploaded = RecordCpuRecordUpload();
        if (m_Work.bHasMegaDraw)
        {
            RecordMegaRecords(plan);
        }
        else if (!bUploaded)
        {
            commandList->BufferBarrier(slot.RecordTable, slot.RecordState, RHI::ResourceState::GenericRead);
        }
        slot.RecordState = RHI::ResourceState::GenericRead;

        // 64bit のバッファを空で埋める（ソフトウェアラスタが書く前。描画の後の合流が読む）
        RecordMergeClear();

        // ID と深度へ、MegaGeometry の全パス → 手続き・スキニングの塊の順に 1 回の render pass で描く
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(m_Work.Viewport);
        commandList->SetScissor(m_Work.Scissor);
        if (m_Work.bHasMegaDraw)
        {
            RecordMegaDraws(plan, 0, plan.PassCount);
        }
        RecordChunkDraws();
        commandList->EndRenderPass();

        // 1 回の判定は HZB を作らないので、描画の後に 1 回だけ 64bit のバッファの値を合流させる
        RecordMergePass("VisRasterMerge1");

        FinishFrame();
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
        if (!m_Sampler)
        {
            NORVES_LOG_WARNING("VisibilityDebugPass", "検証表示のサンプラーの作成に失敗。このパスは何もしません");
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
        m_Uses.Clear();
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
        // 資源は Execute の回数ではなくフレームの枠で決める（同じフレームに何回 Execute されても提出前の資源を上書きしない）
        m_Uses.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        Use* use = nullptr;
        if (m_Pipeline && recordTable && m_Sampler)
        {
            use = &m_Uses.Acquire();
            if (!use->ParamsUniform)
            {
                use->ParamsUniform = m_Device->CreateBuffer(
                    RHI::BufferDesc(DebugParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisBuffer_DebugParams"));
            }
            if (!use->DescriptorSet)
            {
                use->DescriptorSet = m_Device->CreateDescriptorSet(MakeDebugDescriptorSetDesc());
            }
            if (!use->ParamsUniform || !use->DescriptorSet)
            {
                NORVES_LOG_WARNING("VisibilityDebugPass", "検証表示の資源の作成に失敗。この描画は何もしません");
                use = nullptr;
            }
        }
        const bool bCanDraw = use != nullptr;
        if (bCanDraw)
        {
            const float params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            use->ParamsUniform->Update(params, sizeof(params));
            RHI::DescriptorSetPtr& descriptorSet = use->DescriptorSet;
            descriptorSet->BindTexture(0, idTexture);
            descriptorSet->BindSampler(0, m_Sampler);
            descriptorSet->BindStorageBuffer(1, recordTable, 0, ClampBindSize(m_RasterPass->GetRecordTableBytes()));
            descriptorSet->BindConstantBuffer(2, use->ParamsUniform, 0, DebugParamsBytes);
            descriptorSet->Update();
        }

        RHI::ICommandList* commandList = context.CommandList;
        commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
        commandList->SetViewport(context.GetActiveLocalViewport());
        commandList->SetScissor(context.GetActiveLocalScissor());
        if (bCanDraw)
        {
            commandList->SetPipeline(m_Pipeline);
            commandList->SetDescriptorSet(use->DescriptorSet, 0);
            commandList->Draw(3, 0);
        }
        commandList->EndRenderPass();
    }

} // namespace NorvesLib::Core::Rendering

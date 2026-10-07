#include "Rendering/VirtualShadowMapRaster.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/ScopedGpuTimestamp.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IPipeline.h"
#include "RHI/IRenderPass.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダー（vsm_expand.comp・vsm_draw.vert）の VsmRasterParams（std140）と同じ並び
        struct GPURasterParams
        {
            float lightRight[4];
            float lightUp[4];
            float lightDirection[4];
            float depth[4];      // x = 深度の原点、y = 1 / (2 × 深度の範囲)
            uint32_t counts[4];  // x = 塊の数、y = 段の数、z = インスタンスの容量
            float levelInfo[VirtualShadowMapMaxLevels][4];     // x = ページの一辺（m）、y = texel の一辺（m）
            int32_t levelOrigin[VirtualShadowMapMaxLevels][4]; // x, y = 範囲の最小の絶対のページの番号
        };
        static_assert(sizeof(GPURasterParams) == 592, "vsm_expand.comp・vsm_draw.vert の VsmRasterParams と同じ大きさにすること");

        // 絶対のページの番号を int32 でシェーダーへ渡せる範囲（範囲の端 + 128 ページが溢れない余裕を持つ）
        constexpr int64_t MaxOriginMagnitude = 1ll << 30;
        constexpr uint32_t DrawCommandStride = VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS * sizeof(uint32_t);
        constexpr uint32_t DrawHeaderBytes = VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS * sizeof(uint32_t);
        constexpr uint32_t InstanceBytes = 4u * sizeof(uint32_t);
        // 1 つの塊の中の頂点の番号の最大（三角形 128 個 × 3）。描画の中で使う連番のインデックスの数
        constexpr uint32_t MaxChunkVertices = VisibilityBuffer::MAX_TRIANGLES_PER_RECORD * 3u;

        constexpr uint32_t ExpandBindParams = 0;
        constexpr uint32_t ExpandBindChunks = 1;
        constexpr uint32_t ExpandBindPageTable = 2;
        constexpr uint32_t ExpandBindDraws = 3;
        constexpr uint32_t ExpandBindInstances = 4;
        constexpr uint32_t ExpandBindStats = 5;

        constexpr uint32_t DrawBindParams = 0;
        constexpr uint32_t DrawBindInstances = 1;
        constexpr uint32_t DrawBindChunks = 2;
        constexpr uint32_t DrawBindPool = 3;

        RHI::DescriptorBinding MakeBinding(uint32_t binding, RHI::ResourceBindType type, RHI::ShaderStage stages)
        {
            RHI::DescriptorBinding result;
            result.binding = binding;
            result.type = type;
            result.stages = stages;
            return result;
        }

        RHI::DescriptorSetDesc MakeExpandLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(ExpandBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute));
            for (const uint32_t binding :
                 {ExpandBindChunks, ExpandBindPageTable, ExpandBindDraws, ExpandBindInstances, ExpandBindStats})
            {
                desc.bindings.push_back(MakeBinding(binding, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            }
            return desc;
        }

        RHI::DescriptorSetDesc MakeDrawLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(DrawBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindInstances, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindChunks, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindPool, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Pixel));
            return desc;
        }

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        bool IsOriginInRange(int64_t origin)
        {
            return origin > -MaxOriginMagnitude && origin < MaxOriginMagnitude;
        }

        // クリップマップが使える入力か。使えるなら params へ値を書く
        bool FillParams(const VirtualShadowMapClipmap* clipmap, GPURasterParams& params)
        {
            if (clipmap == nullptr || !clipmap->bEnabled || clipmap->LevelCount == 0u ||
                clipmap->LevelCount > VirtualShadowMap::LEVEL_COUNT ||
                clipmap->PagesPerAxis != VirtualShadowMap::TABLE_DIMENSION ||
                clipmap->Settings.PageResolution != VirtualShadowMap::PAGE_RESOLUTION ||
                !(clipmap->Settings.DepthRangeMeters > 0.0f))
            {
                return false;
            }
            for (uint32_t level = 0; level < clipmap->LevelCount; ++level)
            {
                const VirtualShadowMapClipmapLevel& data = clipmap->Levels[level];
                if (!IsOriginInRange(data.OriginPageX) || !IsOriginInRange(data.OriginPageY) || !(data.PageMeters > 0.0f) ||
                    !(data.TexelMeters > 0.0f))
                {
                    return false;
                }
                params.levelInfo[level][0] = data.PageMeters;
                params.levelInfo[level][1] = data.TexelMeters;
                params.levelOrigin[level][0] = static_cast<int32_t>(data.OriginPageX);
                params.levelOrigin[level][1] = static_cast<int32_t>(data.OriginPageY);
            }
            params.lightRight[0] = clipmap->LightRight.x;
            params.lightRight[1] = clipmap->LightRight.y;
            params.lightRight[2] = clipmap->LightRight.z;
            params.lightUp[0] = clipmap->LightUp.x;
            params.lightUp[1] = clipmap->LightUp.y;
            params.lightUp[2] = clipmap->LightUp.z;
            params.lightDirection[0] = clipmap->Direction.x;
            params.lightDirection[1] = clipmap->Direction.y;
            params.lightDirection[2] = clipmap->Direction.z;
            params.depth[0] = static_cast<float>(clipmap->DepthCenter);
            params.depth[1] = 0.5f / clipmap->Settings.DepthRangeMeters;
            params.counts[1] = clipmap->LevelCount;
            return true;
        }
    } // namespace

    VirtualShadowMapRaster::VirtualShadowMapRaster() = default;

    VirtualShadowMapRaster::~VirtualShadowMapRaster()
    {
        Shutdown();
    }

    bool VirtualShadowMapRaster::IsReady() const
    {
        return m_Device != nullptr && m_ExpandPipeline && m_DrawPipeline && m_RenderPass && m_Framebuffer && m_IdentityIndices;
    }

    bool VirtualShadowMapRaster::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }
        const RHI::DeviceCapabilities& capabilities = device->GetCapabilities();
        if (!capabilities.bDrawIndirectFirstInstance || !capabilities.bBufferDeviceAddress || !capabilities.bFragmentStoresAndAtomics)
        {
            NORVES_LOG_WARNING("VirtualShadowMapRaster",
                               "VSM の描画に要る機能が無い（firstInstance の間接描画・バッファのアドレス・断片シェーダーのアトミック）");
            return false;
        }
        m_Device = device;

        m_ExpandShader = shaderManager->LoadShader("vsm_expand.comp", RHI::ShaderStage::Compute);
        m_VertexShader = shaderManager->LoadShader("vsm_draw.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = shaderManager->LoadShader("vsm_draw.frag", RHI::ShaderStage::Pixel);
        if (!m_ExpandShader || !m_VertexShader || !m_FragmentShader)
        {
            NORVES_LOG_WARNING("VirtualShadowMapRaster", "VSM の展開・描画のシェーダーの読み込みに失敗");
            Shutdown();
            return false;
        }

        // 展開・描画のパイプライン、添付の無い 128×128 のレンダーパスとフレームバッファ（RHI の作成は失敗すると例外を投げる）
        try
        {
            RHI::ComputePipelineDesc expandDesc;
            expandDesc.computeShader = m_ExpandShader;
            expandDesc.descriptorSetLayouts.push_back(MakeExpandLayout());
            m_ExpandPipeline = device->CreateComputePipeline(expandDesc);

            m_RenderPass = device->CreateRenderPass(RHI::RenderPassDesc{});
            if (m_RenderPass)
            {
                RHI::FramebufferDesc framebufferDesc;
                framebufferDesc.renderPass = m_RenderPass;
                framebufferDesc.width = VirtualShadowMap::RASTER_VIEWPORT;
                framebufferDesc.height = VirtualShadowMap::RASTER_VIEWPORT;
                m_Framebuffer = device->CreateFramebuffer(framebufferDesc);

                RHI::GraphicsPipelineDesc drawDesc;
                drawDesc.vertexShader = m_VertexShader;
                drawDesc.pixelShader = m_FragmentShader;
                drawDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
                // 背面も省かない（投影物の向きに依らず影を落とす）
                drawDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
                drawDesc.rasterState.cullMode = RHI::CullMode::None;
                drawDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
                drawDesc.rasterState.lineWidth = 1.0f;
                drawDesc.depthStencilState.depthTestEnable = false;
                drawDesc.depthStencilState.depthWriteEnable = false;
                drawDesc.renderPass = m_RenderPass;
                drawDesc.descriptorSetLayouts.push_back(MakeDrawLayout());
                m_DrawPipeline = device->CreateGraphicsPipeline(drawDesc);
            }

            // 頂点の番号（gl_VertexIndex）を 0, 1, 2, … にするための連番のインデックス
            m_IdentityIndices = device->CreateBuffer(RHI::BufferDesc(static_cast<uint64_t>(MaxChunkVertices) * sizeof(uint32_t),
                                                                     RHI::ResourceUsage::IndexBuffer | RHI::ResourceUsage::TransferDst,
                                                                     false,
                                                                     "VsmRasterIdentityIndices"));
            if (m_IdentityIndices)
            {
                Container::VariableArray<uint32_t> indices;
                indices.resize(MaxChunkVertices);
                for (uint32_t index = 0; index < MaxChunkVertices; ++index)
                {
                    indices[index] = index;
                }
                m_IdentityIndices->Update(indices.data(), static_cast<uint64_t>(MaxChunkVertices) * sizeof(uint32_t));
            }
        }
        catch (...)
        {
            NORVES_LOG_WARNING("VirtualShadowMapRaster", "VSM の描画のパイプラインかレンダーパスの作成に失敗");
            Shutdown();
            return false;
        }

        if (!IsReady())
        {
            NORVES_LOG_WARNING("VirtualShadowMapRaster", "VSM の描画のパイプラインを作れなかった");
            Shutdown();
            return false;
        }
        return true;
    }

    void VirtualShadowMapRaster::Shutdown()
    {
        m_Uses.Clear();
        m_IdentityIndices.reset();
        m_Framebuffer.reset();
        m_DrawPipeline.reset();
        m_ExpandPipeline.reset();
        m_RenderPass.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_ExpandShader.reset();
        m_Device = nullptr;
        m_LastDrawCount = 0;
    }

    void VirtualShadowMapRaster::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VirtualShadowMapRaster::AcquireUse(Use*& outUse)
    {
        Use& use = m_Uses.Acquire();
        if (!use.Uniform)
        {
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPURasterParams), RHI::ResourceUsage::ConstantBuffer, true, "VsmRasterParams"));
        }
        if (!use.ExpandSet)
        {
            use.ExpandSet = m_Device->CreateDescriptorSet(MakeExpandLayout());
        }
        if (!use.DrawSet)
        {
            use.DrawSet = m_Device->CreateDescriptorSet(MakeDrawLayout());
        }
        outUse = &use;
        return use.Uniform && use.ExpandSet && use.DrawSet;
    }

    bool VirtualShadowMapRaster::Record(RHI::ICommandList* commandList, const VirtualShadowMapRasterDispatch& dispatch)
    {
        m_LastDrawCount = 0;
        if (!IsReady() || !commandList || dispatch.PoolPages == 0 || dispatch.PoolPages > VirtualShadowMap::MAX_POOL_PAGES ||
            !dispatch.Pool || !dispatch.PageTable || !dispatch.Stats || !dispatch.Chunks || !dispatch.Instances || !dispatch.Draws)
        {
            return false;
        }
        if (dispatch.Pool->GetSize() < VirtualShadowMap::PoolBytes(dispatch.PoolPages) ||
            dispatch.PageTable->GetSize() < VirtualShadowMap::PageTableBytes() ||
            dispatch.Stats->GetSize() < VirtualShadowMap::STATS_BYTES ||
            dispatch.Chunks->GetSize() < VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount) ||
            dispatch.Draws->GetSize() < VirtualShadowMap::RasterDrawBytes(dispatch.ChunkCount) ||
            dispatch.Instances->GetSize() < VirtualShadowMap::RasterInstanceBytes(1u))
        {
            return false;
        }

        GPURasterParams params = {};
        if (!FillParams(dispatch.Clipmap, params))
        {
            return false;
        }
        const uint64_t instanceCapacity = dispatch.Instances->GetSize() / InstanceBytes;
        params.counts[0] = dispatch.ChunkCount;
        params.counts[2] = instanceCapacity > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                                                    : static_cast<uint32_t>(instanceCapacity);

        Use* use = nullptr;
        if (!AcquireUse(use))
        {
            return false;
        }

        // ----- 展開 -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmExpand");
            // 間接描画の引数の頭（インスタンスの確保の位置）を 0 にする
            commandList->BufferBarrier(dispatch.Draws, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest, 0u, DrawHeaderBytes);
            commandList->FillBuffer(dispatch.Draws, 0u, DrawHeaderBytes, 0u);
            commandList->BufferBarrier(dispatch.Draws, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, 0u, DrawHeaderBytes);

            if (dispatch.ChunkCount == 0u)
            {
                return true;
            }

            use->Uniform->Update(&params, sizeof(params));
            use->ExpandSet->BindConstantBuffer(ExpandBindParams, use->Uniform, 0, sizeof(params));
            use->ExpandSet->BindStorageBuffer(ExpandBindChunks, dispatch.Chunks, 0,
                                              ClampBindSize(VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
            use->ExpandSet->BindStorageBuffer(ExpandBindPageTable, dispatch.PageTable, 0,
                                              ClampBindSize(VirtualShadowMap::PageTableBytes()));
            use->ExpandSet->BindStorageBuffer(ExpandBindDraws, dispatch.Draws, 0, ClampBindSize(dispatch.Draws->GetSize()));
            use->ExpandSet->BindStorageBuffer(ExpandBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
            use->ExpandSet->BindStorageBuffer(ExpandBindStats, dispatch.Stats, 0, ClampBindSize(VirtualShadowMap::STATS_BYTES));
            use->ExpandSet->Update();

            const uint32_t groupsX = dispatch.ChunkCount < VirtualShadowMap::GROUP_COUNT_X_LIMIT ? dispatch.ChunkCount
                                                                                                 : VirtualShadowMap::GROUP_COUNT_X_LIMIT;
            const uint32_t groupsY = (dispatch.ChunkCount + VirtualShadowMap::GROUP_COUNT_X_LIMIT - 1u) / VirtualShadowMap::GROUP_COUNT_X_LIMIT;
            commandList->SetPipeline(m_ExpandPipeline);
            commandList->SetDescriptorSet(use->ExpandSet, 0);
            commandList->Dispatch(groupsX, groupsY, 1u);
        }

        // 展開の書き込みを、間接描画の引数・頂点シェーダーの読み取りへ見せる。物理ページは消去（計算）の書き込みを断片シェーダーへ見せる
        const RHI::BufferPtr readBuffers[] = {dispatch.Draws, dispatch.Instances, dispatch.Chunks};
        for (const RHI::BufferPtr& buffer : readBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        }
        commandList->BufferBarrier(dispatch.Pool, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::PixelShaderWrite);

        // ----- 描画 -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmDraw");
            use->DrawSet->BindConstantBuffer(DrawBindParams, use->Uniform, 0, sizeof(params));
            use->DrawSet->BindStorageBuffer(DrawBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
            use->DrawSet->BindStorageBuffer(DrawBindChunks, dispatch.Chunks, 0,
                                            ClampBindSize(VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
            use->DrawSet->BindStorageBuffer(DrawBindPool, dispatch.Pool, 0,
                                            ClampBindSize(VirtualShadowMap::PoolBytes(dispatch.PoolPages)));
            use->DrawSet->Update();

            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(VirtualShadowMap::RASTER_VIEWPORT);
            viewport.height = static_cast<float>(VirtualShadowMap::RASTER_VIEWPORT);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32_t>(VirtualShadowMap::RASTER_VIEWPORT);
            scissor.bottom = static_cast<int32_t>(VirtualShadowMap::RASTER_VIEWPORT);

            commandList->BeginRenderPass(m_RenderPass, m_Framebuffer);
            commandList->SetViewport(viewport);
            commandList->SetScissor(scissor);
            commandList->SetPipeline(m_DrawPipeline);
            commandList->SetDescriptorSet(use->DrawSet, 0);
            commandList->SetIndexBuffer(m_IdentityIndices, 0, RHI::IndexType::Uint32);
            // 塊ごとに 1 回（instanceCount = その塊を描くページの数、firstInstance = インスタンスの範囲の先頭）
            for (uint32_t chunk = 0; chunk < dispatch.ChunkCount; ++chunk)
            {
                commandList->DrawIndexedIndirect(dispatch.Draws, DrawHeaderBytes + static_cast<uint64_t>(chunk) * DrawCommandStride, 1u,
                                                 DrawCommandStride);
                ++m_LastDrawCount;
            }
            commandList->EndRenderPass();
        }

        // 入力の状態（UnorderedAccess）へ戻す
        for (const RHI::BufferPtr& buffer : readBuffers)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        }
        commandList->BufferBarrier(dispatch.Pool, RHI::ResourceState::PixelShaderWrite, RHI::ResourceState::UnorderedAccess);
        return true;
    }

    bool VirtualShadowMapRasterStatsReporter::Report(uint32_t chunks, uint32_t instances, uint32_t overflow)
    {
        const uint32_t values[3] = {chunks, instances, overflow};
        const bool bNonZero = chunks != 0u || instances != 0u || overflow != 0u;
        if (!m_bEverNonZero && !bNonZero)
        {
            return false;
        }
        m_bEverNonZero = true;
        ++m_ReportsSinceLog;
        bool bChanged = !m_bLogged;
        for (uint32_t index = 0; index < 3u; ++index)
        {
            bChanged = bChanged || values[index] != m_Logged[index];
        }
        if (!bChanged && m_ReportsSinceLog < LogIntervalReports)
        {
            return false;
        }
        for (uint32_t index = 0; index < 3u; ++index)
        {
            m_Logged[index] = values[index];
        }
        m_bLogged = true;
        m_ReportsSinceLog = 0;
        NORVES_LOG_INFO("VirtualShadowMapRaster", "VSM_RASTER chunks=%u instances=%u overflow=%u", chunks, instances, overflow);
        return true;
    }

} // namespace NorvesLib::Core::Rendering

#include "Rendering/VirtualShadowMapRaster.h"

#include "Container/Containers.h"
#include "Logging/LogMacros.h"
#include "Rendering/MegaGeometry/MegaGeometryCullUniforms.h"
#include "Rendering/ScopedGpuTimestamp.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/VirtualShadowMapPages.h"
#include "Rendering/VirtualShadowMapSample.h"
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
            uint32_t counts[4];  // x = 塊の数、y = スライスの数、z = インスタンスの容量、w = MegaGeometry のクラスタの記録の容量
        };
        // ページの一辺・texel・範囲の原点・投影の行列はスライスの表（GPUVsmSlice。ExpandBindSlices・DrawBindSlices）にある
        static_assert(sizeof(GPURasterParams) == 80, "vsm_expand.comp・vsm_draw.vert の VsmRasterParams と同じ大きさにすること");

        // 絶対のページの番号を int32 でシェーダーへ渡せる範囲（範囲の端 + 128 ページが溢れない余裕を持つ）
        constexpr int64_t MaxOriginMagnitude = 1ll << 30;
        constexpr uint32_t DrawCommandStride = VirtualShadowMap::RASTER_DRAW_COMMAND_WORDS * sizeof(uint32_t);
        constexpr uint32_t DrawHeaderBytes = VirtualShadowMap::RASTER_DRAWS_HEADER_WORDS * sizeof(uint32_t);
        // 間接描画の引数の頭の語 1〜3 = 展開の間接 dispatch の引数（vsm_expand_args.comp が書く）
        constexpr uint64_t ExpandArgsOffsetBytes = sizeof(uint32_t);
        constexpr uint32_t InstanceBytes = 4u * sizeof(uint32_t);
        // 1 つの塊の中の頂点の番号の最大（三角形 128 個 × 3）。描画の中で使う連番のインデックスの数
        constexpr uint32_t MaxChunkVertices = VisibilityBuffer::MAX_TRIANGLES_PER_RECORD * 3u;

        constexpr uint32_t ExpandBindParams = 0;
        constexpr uint32_t ExpandBindChunks = 1;
        constexpr uint32_t ExpandBindPageTable = 2;
        constexpr uint32_t ExpandBindDraws = 3;
        constexpr uint32_t ExpandBindInstances = 4;
        constexpr uint32_t ExpandBindStats = 5;
        // MegaGeometry のクラスタの記録と、カリングの一覧（語 0 = 選んだクラスタの数）
        constexpr uint32_t ExpandBindMegaChunks = 6;
        constexpr uint32_t ExpandBindMegaList = 7;
        // スライスの表（読み取り専用）
        constexpr uint32_t ExpandBindSlices = 8;

        // vsm_mega_cull.comp・vsm_dirty_mips.comp の VsmMegaCullParams（std140。Common/VirtualShadowMapMegaCull.glsl）と同じ並び
        struct GPUMegaCullParams
        {
            float lightRight[4];
            float lightUp[4];
            float lightDirection[4];
            float depth[4];     // x = 深度の原点、y = 深度の範囲の片側（m）
            uint32_t counts[4]; // x = スライス（段）の数、y = 出力の一覧の容量、z = 影の判定の全ワークグループ数
        };
        // ページの一辺・texel・範囲の原点はスライスの表（GPUVsmSlice。DirtyBindSlices・MegaBindSlices）にある
        static_assert(sizeof(GPUMegaCullParams) == 80, "Common/VirtualShadowMapMegaCull.glsl の VsmMegaCullParams と同じ大きさにすること");

        // vsm_dirty_mips.comp: 14 = 定数、15 = VSM のページの表、16 = dirty の階層、17 = スライスの表
        constexpr uint32_t DirtyBindParams = 14;
        constexpr uint32_t DirtyBindPageTable = 15;
        constexpr uint32_t DirtyBindBits = 16;
        constexpr uint32_t DirtyBindSlices = 17;
        // vsm_mega_cull.comp: 0 = カリングの定数（CullUniforms）、1 = インスタンスの表、11 = ジオメトリのページの表、
        // 14 = 定数、15 = 出力の一覧、16 = dirty の階層、17 = 統計、18 = 影の表、19 = VSM のページの表（溢れたクラスタの範囲へ再描画の印を書く）、
        // 20 = スライスの表
        constexpr uint32_t MegaBindCullData = 0;
        constexpr uint32_t MegaBindInstances = 1;
        constexpr uint32_t MegaBindPageTable = 11;
        constexpr uint32_t MegaBindParams = 14;
        constexpr uint32_t MegaBindList = 15;
        constexpr uint32_t MegaBindDirtyBits = 16;
        constexpr uint32_t MegaBindStats = 17;
        constexpr uint32_t MegaBindShadowInstances = 18;
        constexpr uint32_t MegaBindVsmPageTable = 19;
        constexpr uint32_t MegaBindSlices = 20;
        // vsm_mega_chunks.comp: 1 = インスタンスの表、14 = 定数、15 = 出力の一覧、18 = 影の表、19 = 影の塊の記録の出力
        constexpr uint32_t ChunkBindInstances = 1;
        constexpr uint32_t ChunkBindParams = 14;
        constexpr uint32_t ChunkBindList = 15;
        constexpr uint32_t ChunkBindShadowInstances = 18;
        constexpr uint32_t ChunkBindChunks = 19;

        constexpr uint32_t DrawBindParams = 0;
        constexpr uint32_t DrawBindInstances = 1;
        constexpr uint32_t DrawBindChunks = 2;
        constexpr uint32_t DrawBindPool = 3;
        constexpr uint32_t DrawBindMegaChunks = 4;
        constexpr uint32_t DrawBindSlices = 5;

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
            for (const uint32_t binding : {ExpandBindChunks, ExpandBindPageTable, ExpandBindDraws, ExpandBindInstances,
                                           ExpandBindStats, ExpandBindMegaChunks, ExpandBindMegaList})
            {
                desc.bindings.push_back(MakeBinding(binding, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            }
            desc.bindings.push_back(MakeBinding(ExpandBindSlices, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            return desc;
        }

        RHI::DescriptorSetDesc MakeDrawLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(DrawBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindInstances, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindChunks, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindPool, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Pixel));
            desc.bindings.push_back(MakeBinding(DrawBindMegaChunks, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Vertex));
            desc.bindings.push_back(MakeBinding(DrawBindSlices, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Vertex));
            return desc;
        }

        RHI::DescriptorSetDesc MakeDirtyLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(DirtyBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(DirtyBindPageTable, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(DirtyBindBits, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(DirtyBindSlices, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            return desc;
        }

        RHI::DescriptorSetDesc MakeMegaCullLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(MegaBindCullData, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindInstances, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindPageTable, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindList, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindDirtyBits, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindStats, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindShadowInstances, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindVsmPageTable, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(MegaBindSlices, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            return desc;
        }

        RHI::DescriptorSetDesc MakeMegaChunksLayout()
        {
            RHI::DescriptorSetDesc desc;
            desc.bindings.push_back(MakeBinding(ChunkBindInstances, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(ChunkBindParams, RHI::ResourceBindType::ConstantBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(ChunkBindList, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(ChunkBindShadowInstances, RHI::ResourceBindType::StructuredBuffer, RHI::ShaderStage::Compute));
            desc.bindings.push_back(MakeBinding(ChunkBindChunks, RHI::ResourceBindType::RWBuffer, RHI::ShaderStage::Compute));
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

        // 展開・描画・MegaGeometry の投影物のカリングが使えるクリップマップか（段の数・ページの格子・深度の範囲・各段の範囲が使える値）
        bool IsUsableClipmap(const VirtualShadowMapClipmap* clipmap)
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
            }
            return true;
        }

        // スライスの数を決める。0 はクリップマップの段の数。使えない数（上限超え・クリップマップの段より少ない）なら 0
        uint32_t ResolveSliceCount(uint32_t requested, const VirtualShadowMapClipmap* clipmap)
        {
            if (clipmap == nullptr)
            {
                return 0u;
            }
            const uint32_t count = requested != 0u ? requested : clipmap->LevelCount;
            return count >= clipmap->LevelCount && count <= VirtualShadowMap::MAX_SLICES ? count : 0u;
        }

        // MegaGeometry の投影物のカリングの定数を書く。クリップマップが使えなければ false
        bool FillMegaCullParams(const VirtualShadowMapClipmap* clipmap,
                                uint32_t sliceCount,
                                uint32_t listCapacity,
                                uint32_t totalGroups,
                                GPUMegaCullParams& params)
        {
            if (!IsUsableClipmap(clipmap))
            {
                return false;
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
            params.depth[1] = clipmap->Settings.DepthRangeMeters;
            params.counts[0] = sliceCount;
            params.counts[1] = listCapacity;
            params.counts[2] = totalGroups;
            return true;
        }

        // クリップマップが使える入力か。使えるなら params へ値を書く。
        // 使えなくても bAllowNoSun（外から渡したスライスの表で、太陽の段を使わない）なら、基底・深度の範囲を既定にして true
        // （点光源の面の透視のスライスはこの値を読まず、スライスの行列で投影する）
        bool FillParams(const VirtualShadowMapClipmap* clipmap, uint32_t sliceCount, bool bAllowNoSun, GPURasterParams& params)
        {
            if (!IsUsableClipmap(clipmap))
            {
                if (!bAllowNoSun)
                {
                    return false;
                }
                params.lightRight[0] = 1.0f;
                params.lightUp[1] = 1.0f;
                params.lightDirection[2] = 1.0f;
                params.depth[1] = 0.5f;
                params.counts[1] = sliceCount;
                return true;
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
            params.counts[1] = sliceCount;
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
        return m_Device != nullptr && m_ExpandPipeline && m_ExpandArgsPipeline && m_DrawPipeline && m_RenderPass && m_Framebuffer && m_IdentityIndices;
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
        // MegaGeometry のクラスタの記録は、件数を GPU から読む間接描画（DrawIndexedIndirectCount）で描く
        m_bMegaSupported = capabilities.bDrawIndirectCount;

        m_ExpandShader = shaderManager->LoadShader("vsm_expand.comp", RHI::ShaderStage::Compute);
        m_ExpandArgsShader = shaderManager->LoadShader("vsm_expand_args.comp", RHI::ShaderStage::Compute);
        m_VertexShader = shaderManager->LoadShader("vsm_draw.vert", RHI::ShaderStage::Vertex);
        m_FragmentShader = shaderManager->LoadShader("vsm_draw.frag", RHI::ShaderStage::Pixel);
        if (!m_ExpandShader || !m_ExpandArgsShader || !m_VertexShader || !m_FragmentShader)
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

            // 間接 dispatch の引数を作る小さな計算。展開と同じ記述子の並びを使う（使うのは 0・3・7 だけ）
            RHI::ComputePipelineDesc expandArgsDesc;
            expandArgsDesc.computeShader = m_ExpandArgsShader;
            expandArgsDesc.descriptorSetLayouts.push_back(MakeExpandLayout());
            m_ExpandArgsPipeline = device->CreateComputePipeline(expandArgsDesc);

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
        m_ExpandArgsPipeline.reset();
        m_RenderPass.reset();
        m_FragmentShader.reset();
        m_VertexShader.reset();
        m_ExpandArgsShader.reset();
        m_ExpandShader.reset();
        m_Device = nullptr;
        m_LastDrawCount = 0;
        m_bLastMegaDraw = false;
        m_bMegaSupported = false;
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
        if (!use.Slices)
        {
            use.Slices = m_Device->CreateBuffer(RHI::BufferDesc(
                sizeof(GPUVsmSlice) * VirtualShadowMapMaxSlices, RHI::ResourceUsage::StorageBuffer, true, "VsmRasterSlices"));
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
        return use.Uniform && use.Slices && use.ExpandSet && use.DrawSet;
    }

    bool VirtualShadowMapRaster::Record(RHI::ICommandList* commandList, const VirtualShadowMapRasterDispatch& dispatch)
    {
        m_LastDrawCount = 0;
        m_bLastMegaDraw = false;
        if (!IsReady() || !commandList || dispatch.PoolPages == 0 || dispatch.PoolPages > VirtualShadowMap::MAX_POOL_PAGES ||
            !dispatch.Pool || !dispatch.PageTable || !dispatch.Stats || !dispatch.Chunks || !dispatch.Instances || !dispatch.Draws)
        {
            return false;
        }
        // MegaGeometry のクラスタの記録: 描ける装置で、記録・一覧・引数の容量が揃っているときだけ
        const bool bMega = dispatch.MegaCapacity != 0u;
        constexpr uint32_t MaxChunkTotal = 1u << 24;
        if (bMega && (!m_bMegaSupported || !dispatch.MegaChunks || !dispatch.MegaList || dispatch.MegaCapacity > MaxChunkTotal ||
                      dispatch.ChunkCount > MaxChunkTotal ||
                      dispatch.MegaChunks->GetSize() < VirtualShadowMap::RasterChunkBytes(dispatch.MegaCapacity) ||
                      dispatch.MegaList->GetSize() < sizeof(uint32_t)))
        {
            return false;
        }
        const uint32_t sliceCount = ResolveSliceCount(dispatch.SliceCount, dispatch.Clipmap);
        const uint32_t chunkTotal = dispatch.ChunkCount + (bMega ? dispatch.MegaCapacity : 0u);
        if (sliceCount == 0u || dispatch.Pool->GetSize() < VirtualShadowMap::PoolBytes(dispatch.PoolPages) ||
            dispatch.PageTable->GetSize() < VirtualShadowMap::PageTableBytes(sliceCount) ||
            dispatch.Stats->GetSize() < VirtualShadowMap::STATS_BYTES ||
            dispatch.Chunks->GetSize() < VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount) ||
            dispatch.Draws->GetSize() < VirtualShadowMap::RasterDrawBytes(chunkTotal) ||
            dispatch.Instances->GetSize() < VirtualShadowMap::RasterInstanceBytes(1u))
        {
            return false;
        }

        GPURasterParams params = {};
        if (!FillParams(dispatch.Clipmap, sliceCount, dispatch.Slices != nullptr, params))
        {
            return false;
        }
        const uint64_t instanceCapacity = dispatch.Instances->GetSize() / InstanceBytes;
        params.counts[0] = dispatch.ChunkCount;
        params.counts[3] = bMega ? dispatch.MegaCapacity : 0u;
        params.counts[2] = instanceCapacity > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                                                    : static_cast<uint32_t>(instanceCapacity);

        // スライスの表（ページの一辺・texel・範囲の原点・投影の行列）。外から渡されなければクリップマップから作る
        GPUVsmSlice sliceStorage[VirtualShadowMapMaxSlices];
        const GPUVsmSlice* slices = dispatch.Slices;
        if (slices == nullptr)
        {
            BuildVirtualShadowMapSlices(dispatch.Clipmap, nullptr, sliceCount, sliceStorage);
            slices = sliceStorage;
        }
        const uint32_t sliceBytes = sliceCount * static_cast<uint32_t>(sizeof(GPUVsmSlice));

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

            if (chunkTotal == 0u)
            {
                return true;
            }

            use->Uniform->Update(&params, sizeof(params));
            use->Slices->Update(slices, sliceBytes);
            use->ExpandSet->BindConstantBuffer(ExpandBindParams, use->Uniform, 0, sizeof(params));
            use->ExpandSet->BindStorageBuffer(ExpandBindSlices, use->Slices, 0, sliceBytes);
            use->ExpandSet->BindStorageBuffer(ExpandBindChunks, dispatch.Chunks, 0,
                                              ClampBindSize(VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
            use->ExpandSet->BindStorageBuffer(ExpandBindPageTable, dispatch.PageTable, 0,
                                              ClampBindSize(VirtualShadowMap::PageTableBytes(sliceCount)));
            use->ExpandSet->BindStorageBuffer(ExpandBindDraws, dispatch.Draws, 0, ClampBindSize(dispatch.Draws->GetSize()));
            use->ExpandSet->BindStorageBuffer(ExpandBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
            use->ExpandSet->BindStorageBuffer(ExpandBindStats, dispatch.Stats, 0, ClampBindSize(VirtualShadowMap::STATS_BYTES));
            // クラスタの記録が無いときも束縛は埋める（シェーダーは counts.w が 0 なら読まない）
            use->ExpandSet->BindStorageBuffer(ExpandBindMegaChunks, bMega ? dispatch.MegaChunks : dispatch.Chunks, 0,
                                              ClampBindSize(bMega ? VirtualShadowMap::RasterChunkBytes(dispatch.MegaCapacity)
                                                                  : VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
            use->ExpandSet->BindStorageBuffer(ExpandBindMegaList, bMega ? dispatch.MegaList : dispatch.Stats, 0,
                                              ClampBindSize(bMega ? dispatch.MegaList->GetSize() : VirtualShadowMap::STATS_BYTES));
            use->ExpandSet->Update();

            // ホストが書いた塊（1 塊 = 1 ワークグループ）の後ろに、クラスタの記録の件数ぶんのワークグループを並べる。
            // 件数は GPU が決めるので、引数（件数を容量で頭打ちにした数）を計算で作って間接 dispatch で出す。
            // 間接 dispatch を記録できないコマンドリストでは、容量ぶんを直接 dispatch する（件数より後ろは何もしない）
            bool bIndirectExpand = false;
            if (bMega)
            {
                commandList->SetPipeline(m_ExpandArgsPipeline);
                commandList->SetDescriptorSet(use->ExpandSet, 0);
                commandList->Dispatch(1u, 1u, 1u);
                // 引数は間接 dispatch の読み取り。展開が書く前に、書き込みを見せてから戻す
                commandList->BufferBarrier(dispatch.Draws, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead, 0u,
                                           DrawHeaderBytes);
                commandList->SetPipeline(m_ExpandPipeline);
                commandList->SetDescriptorSet(use->ExpandSet, 0);
                bIndirectExpand = commandList->DispatchIndirect(dispatch.Draws, ExpandArgsOffsetBytes);
                commandList->BufferBarrier(dispatch.Draws, RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess, 0u,
                                           DrawHeaderBytes);
            }
            if (!bIndirectExpand)
            {
                const uint32_t groupsX = chunkTotal < VirtualShadowMap::GROUP_COUNT_X_LIMIT ? chunkTotal : VirtualShadowMap::GROUP_COUNT_X_LIMIT;
                const uint32_t groupsY = (chunkTotal + VirtualShadowMap::GROUP_COUNT_X_LIMIT - 1u) / VirtualShadowMap::GROUP_COUNT_X_LIMIT;
                commandList->SetPipeline(m_ExpandPipeline);
                commandList->SetDescriptorSet(use->ExpandSet, 0);
                commandList->Dispatch(groupsX, groupsY, 1u);
            }
        }

        // 展開の書き込みを、間接描画の引数・頂点シェーダーの読み取りへ見せる。物理ページは消去（計算）の書き込みを断片シェーダーへ見せる
        RHI::BufferPtr readBuffers[5] = {dispatch.Draws, dispatch.Instances, dispatch.Chunks};
        uint32_t readBufferCount = 3;
        if (bMega)
        {
            readBuffers[readBufferCount++] = dispatch.MegaChunks;
            readBuffers[readBufferCount++] = dispatch.MegaList;
        }
        for (uint32_t index = 0; index < readBufferCount; ++index)
        {
            commandList->BufferBarrier(readBuffers[index], RHI::ResourceState::UnorderedAccess, RHI::ResourceState::GenericRead);
        }
        commandList->BufferBarrier(dispatch.Pool, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::PixelShaderWrite);

        // ----- 描画 -----
        {
            ScopedGpuTimestamp timestamp(commandList, "VsmDraw");
            use->DrawSet->BindConstantBuffer(DrawBindParams, use->Uniform, 0, sizeof(params));
            use->DrawSet->BindStorageBuffer(DrawBindSlices, use->Slices, 0, sliceBytes);
            use->DrawSet->BindStorageBuffer(DrawBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
            use->DrawSet->BindStorageBuffer(DrawBindChunks, dispatch.Chunks, 0,
                                            ClampBindSize(VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
            use->DrawSet->BindStorageBuffer(DrawBindPool, dispatch.Pool, 0,
                                            ClampBindSize(VirtualShadowMap::PoolBytes(dispatch.PoolPages)));
            use->DrawSet->BindStorageBuffer(DrawBindMegaChunks, bMega ? dispatch.MegaChunks : dispatch.Chunks, 0,
                                            ClampBindSize(bMega ? VirtualShadowMap::RasterChunkBytes(dispatch.MegaCapacity)
                                                                : VirtualShadowMap::RasterChunkBytes(dispatch.ChunkCount)));
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
            // MegaGeometry のクラスタの記録: 塊の番号 ChunkCount から続く引数を、カリングの一覧の件数（語 0。容量で頭打ち）ぶん 1 回で描く。
            // インスタンスごとの定数バッファは要らない（CSM の MegaGeometry の影のように DynamicUniformAllocator のスロットを使わない）
            if (bMega)
            {
                commandList->DrawIndexedIndirectCount(dispatch.Draws,
                                                      DrawHeaderBytes + static_cast<uint64_t>(dispatch.ChunkCount) * DrawCommandStride,
                                                      dispatch.MegaList, 0u, dispatch.MegaCapacity, DrawCommandStride);
                m_bLastMegaDraw = true;
            }
            commandList->EndRenderPass();
        }

        // 入力の状態（UnorderedAccess）へ戻す
        for (uint32_t index = 0; index < readBufferCount; ++index)
        {
            commandList->BufferBarrier(readBuffers[index], RHI::ResourceState::GenericRead, RHI::ResourceState::UnorderedAccess);
        }
        commandList->BufferBarrier(dispatch.Pool, RHI::ResourceState::PixelShaderWrite, RHI::ResourceState::UnorderedAccess);
        return true;
    }

    VirtualShadowMapMegaCull::VirtualShadowMapMegaCull() = default;

    VirtualShadowMapMegaCull::~VirtualShadowMapMegaCull()
    {
        Shutdown();
    }

    bool VirtualShadowMapMegaCull::IsReady() const
    {
        return m_Device != nullptr && m_DirtyPipeline && m_CullPipeline && m_ChunkPipeline;
    }

    bool VirtualShadowMapMegaCull::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }
        // クラスタの配列は buffer_reference（デバイスアドレス）で引く
        if (!device->GetCapabilities().bBufferDeviceAddress)
        {
            NORVES_LOG_WARNING("VirtualShadowMapMegaCull", "MegaGeometry の投影物のカリングに要るバッファのアドレスが無い");
            return false;
        }
        m_Device = device;

        m_DirtyShader = shaderManager->LoadShader("vsm_dirty_mips.comp", RHI::ShaderStage::Compute);
        m_CullShader = shaderManager->LoadShader("vsm_mega_cull.comp", RHI::ShaderStage::Compute);
        m_ChunkShader = shaderManager->LoadShader("vsm_mega_chunks.comp", RHI::ShaderStage::Compute);
        if (!m_DirtyShader || !m_CullShader || !m_ChunkShader)
        {
            NORVES_LOG_WARNING("VirtualShadowMapMegaCull", "MegaGeometry の投影物のカリングのシェーダーの読み込みに失敗");
            Shutdown();
            return false;
        }

        try
        {
            RHI::ComputePipelineDesc dirtyDesc;
            dirtyDesc.computeShader = m_DirtyShader;
            dirtyDesc.descriptorSetLayouts.push_back(MakeDirtyLayout());
            m_DirtyPipeline = device->CreateComputePipeline(dirtyDesc);

            RHI::ComputePipelineDesc cullDesc;
            cullDesc.computeShader = m_CullShader;
            cullDesc.descriptorSetLayouts.push_back(MakeMegaCullLayout());
            m_CullPipeline = device->CreateComputePipeline(cullDesc);

            RHI::ComputePipelineDesc chunkDesc;
            chunkDesc.computeShader = m_ChunkShader;
            chunkDesc.descriptorSetLayouts.push_back(MakeMegaChunksLayout());
            m_ChunkPipeline = device->CreateComputePipeline(chunkDesc);
        }
        catch (...)
        {
            NORVES_LOG_WARNING("VirtualShadowMapMegaCull", "MegaGeometry の投影物のカリングのパイプラインの作成に失敗");
            Shutdown();
            return false;
        }

        if (!IsReady())
        {
            NORVES_LOG_WARNING("VirtualShadowMapMegaCull", "MegaGeometry の投影物のカリングのパイプラインを作れなかった");
            Shutdown();
            return false;
        }
        return true;
    }

    void VirtualShadowMapMegaCull::Shutdown()
    {
        m_Uses.Clear();
        m_ChunkPipeline.reset();
        m_CullPipeline.reset();
        m_DirtyPipeline.reset();
        m_ChunkShader.reset();
        m_CullShader.reset();
        m_DirtyShader.reset();
        m_Device = nullptr;
        m_LastGroupCount = 0;
        m_bLastChunkBuilt = false;
    }

    void VirtualShadowMapMegaCull::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VirtualShadowMapMegaCull::AcquireUse(Use*& outUse)
    {
        Use& use = m_Uses.Acquire();
        if (!use.CullUniform)
        {
            use.CullUniform = m_Device->CreateBuffer(RHI::BufferDesc(
                sizeof(MegaGeometry::CullUniformData), RHI::ResourceUsage::ConstantBuffer, true, "VsmMegaCullUniform"));
        }
        if (!use.ParamsUniform)
        {
            use.ParamsUniform = m_Device->CreateBuffer(
                RHI::BufferDesc(sizeof(GPUMegaCullParams), RHI::ResourceUsage::ConstantBuffer, true, "VsmMegaCullParams"));
        }
        if (!use.Slices)
        {
            use.Slices = m_Device->CreateBuffer(RHI::BufferDesc(
                sizeof(GPUVsmSlice) * VirtualShadowMapMaxSlices, RHI::ResourceUsage::StorageBuffer, true, "VsmMegaCullSlices"));
        }
        if (!use.DirtySet)
        {
            use.DirtySet = m_Device->CreateDescriptorSet(MakeDirtyLayout());
        }
        if (!use.CullSet)
        {
            use.CullSet = m_Device->CreateDescriptorSet(MakeMegaCullLayout());
        }
        if (!use.ChunkSet)
        {
            use.ChunkSet = m_Device->CreateDescriptorSet(MakeMegaChunksLayout());
        }
        outUse = &use;
        return use.CullUniform && use.ParamsUniform && use.Slices && use.DirtySet && use.CullSet && use.ChunkSet;
    }

    bool VirtualShadowMapMegaCull::Record(RHI::ICommandList* commandList, const VirtualShadowMapMegaCullDispatch& dispatch)
    {
        m_LastGroupCount = 0;
        m_bLastChunkBuilt = false;
        if (!IsReady() || !commandList || !dispatch.PageTable || !dispatch.Stats || !dispatch.DirtyBits || !dispatch.List ||
            !dispatch.Instances || !dispatch.ShadowInstances || !dispatch.MegaPageTable || dispatch.InstanceCount == 0u ||
            dispatch.TotalGroups == 0u)
        {
            return false;
        }
        const uint64_t listBytes = dispatch.List->GetSize();
        const uint64_t headerBytes = static_cast<uint64_t>(VirtualShadowMap::MEGA_CULL_LIST_HEADER_WORDS) * sizeof(uint32_t);
        const uint32_t sliceCount = ResolveSliceCount(dispatch.SliceCount, dispatch.Clipmap);
        if (sliceCount == 0u || dispatch.PageTable->GetSize() < VirtualShadowMap::PageTableBytes(sliceCount) ||
            dispatch.Stats->GetSize() < VirtualShadowMap::STATS_BYTES ||
            dispatch.DirtyBits->GetSize() < VirtualShadowMap::MegaDirtyBitsBytes(sliceCount) ||
            listBytes < VirtualShadowMap::MegaCullListBytes(1u))
        {
            return false;
        }
        const uint64_t capacity = (listBytes - headerBytes) / (4u * sizeof(uint32_t));
        // 影の塊の記録: 一覧の容量ぶんの大きさがあり、1 回の dispatch（x 方向）で一覧の全件に届くこと
        constexpr uint32_t ChunkGroupSize = 64;
        const uint64_t chunkGroups = (capacity + ChunkGroupSize - 1u) / ChunkGroupSize;
        if (dispatch.Chunks && (dispatch.Chunks->GetSize() < VirtualShadowMap::RasterChunkBytes(static_cast<uint32_t>(
                                                                 capacity > std::numeric_limits<uint32_t>::max()
                                                                     ? std::numeric_limits<uint32_t>::max()
                                                                     : capacity)) ||
                                chunkGroups > VirtualShadowMap::GROUP_COUNT_X_LIMIT))
        {
            return false;
        }

        GPUMegaCullParams params = {};
        if (!FillMegaCullParams(dispatch.Clipmap,
                                sliceCount,
                                capacity > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                                                : static_cast<uint32_t>(capacity),
                                dispatch.TotalGroups,
                                params))
        {
            return false;
        }

        // カリングの定数: LOD の許容は texel（正射影）。透視の値・遮蔽・ソフトウェアラスタ・ページの要求は使わない（0）
        MegaGeometry::CullUniformData cullUniform = {};
        cullUniform.InstanceCount = dispatch.InstanceCount;
        cullUniform.TotalGroupCount = dispatch.TotalGroups;
        cullUniform.LODBias = dispatch.LodThresholdTexels;
        cullUniform.PageRequestCapacity = 0u;
        cullUniform.OrthoLod = 1u;

        Use* use = nullptr;
        if (!AcquireUse(use))
        {
            return false;
        }

        ScopedGpuTimestamp timestamp(commandList, "VsmCullMega");

        // 階層と、出力の一覧の頭を 0 にする。階層のバッファは専用なので、スライスの数より大きければ全体を 0 にする（使わない部分を残さない）
        const uint64_t dirtyClearBytes = dispatch.DirtyBits->GetSize() & ~static_cast<uint64_t>(3u);
        commandList->BufferBarrier(dispatch.DirtyBits, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest, 0u, dirtyClearBytes);
        commandList->FillBuffer(dispatch.DirtyBits, 0u, dirtyClearBytes, 0u);
        commandList->BufferBarrier(dispatch.DirtyBits, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, 0u, dirtyClearBytes);
        commandList->BufferBarrier(dispatch.List, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest, 0u, headerBytes);
        commandList->FillBuffer(dispatch.List, 0u, headerBytes, 0u);
        commandList->BufferBarrier(dispatch.List, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, 0u, headerBytes);

        use->CullUniform->Update(&cullUniform, sizeof(cullUniform));
        use->ParamsUniform->Update(&params, sizeof(params));
        GPUVsmSlice sliceStorage[VirtualShadowMapMaxSlices];
        const GPUVsmSlice* slices = dispatch.Slices;
        if (slices == nullptr)
        {
            BuildVirtualShadowMapSlices(dispatch.Clipmap, nullptr, sliceCount, sliceStorage);
            slices = sliceStorage;
        }
        const uint32_t sliceBytes = sliceCount * static_cast<uint32_t>(sizeof(GPUVsmSlice));
        use->Slices->Update(slices, sliceBytes);

        // ----- dirty のページの階層: 1 スレッド = 1 ページ（ローカルは 8×8）、段ごとに 16×16 グループ -----
        use->DirtySet->BindConstantBuffer(DirtyBindParams, use->ParamsUniform, 0, sizeof(params));
        use->DirtySet->BindStorageBuffer(DirtyBindPageTable, dispatch.PageTable, 0, ClampBindSize(VirtualShadowMap::PageTableBytes(sliceCount)));
        use->DirtySet->BindStorageBuffer(DirtyBindBits, dispatch.DirtyBits, 0, ClampBindSize(VirtualShadowMap::MegaDirtyBitsBytes(sliceCount)));
        use->DirtySet->BindStorageBuffer(DirtyBindSlices, use->Slices, 0, sliceBytes);
        use->DirtySet->Update();
        commandList->SetPipeline(m_DirtyPipeline);
        commandList->SetDescriptorSet(use->DirtySet, 0);
        commandList->Dispatch(VirtualShadowMap::TABLE_DIMENSION / 8u, VirtualShadowMap::TABLE_DIMENSION / 8u, sliceCount);
        commandList->BufferBarrier(dispatch.DirtyBits, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);

        // ----- クラスタの選択: x, y = 影の判定のワークグループ（x の上限を超える分は y へ折り返す）、z = 段 -----
        use->CullSet->BindConstantBuffer(MegaBindCullData, use->CullUniform, 0, sizeof(cullUniform));
        use->CullSet->BindStorageBuffer(MegaBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
        use->CullSet->BindStorageBuffer(MegaBindPageTable, dispatch.MegaPageTable, 0, ClampBindSize(dispatch.MegaPageTable->GetSize()));
        use->CullSet->BindConstantBuffer(MegaBindParams, use->ParamsUniform, 0, sizeof(params));
        use->CullSet->BindStorageBuffer(MegaBindList, dispatch.List, 0, ClampBindSize(listBytes));
        use->CullSet->BindStorageBuffer(MegaBindDirtyBits, dispatch.DirtyBits, 0, ClampBindSize(VirtualShadowMap::MegaDirtyBitsBytes(sliceCount)));
        use->CullSet->BindStorageBuffer(MegaBindStats, dispatch.Stats, 0, ClampBindSize(VirtualShadowMap::STATS_BYTES));
        use->CullSet->BindStorageBuffer(MegaBindShadowInstances, dispatch.ShadowInstances, 0, ClampBindSize(dispatch.ShadowInstances->GetSize()));
        use->CullSet->BindStorageBuffer(MegaBindVsmPageTable, dispatch.PageTable, 0, ClampBindSize(VirtualShadowMap::PageTableBytes(sliceCount)));
        use->CullSet->BindStorageBuffer(MegaBindSlices, use->Slices, 0, sliceBytes);
        use->CullSet->Update();

        const uint32_t groupsX = dispatch.TotalGroups < VirtualShadowMap::GROUP_COUNT_X_LIMIT ? dispatch.TotalGroups
                                                                                              : VirtualShadowMap::GROUP_COUNT_X_LIMIT;
        const uint32_t groupsY = (dispatch.TotalGroups + VirtualShadowMap::GROUP_COUNT_X_LIMIT - 1u) / VirtualShadowMap::GROUP_COUNT_X_LIMIT;
        commandList->SetPipeline(m_CullPipeline);
        commandList->SetDescriptorSet(use->CullSet, 0);
        commandList->Dispatch(groupsX, groupsY, sliceCount);
        commandList->BufferBarrier(dispatch.List, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        commandList->BufferBarrier(dispatch.Stats, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        // 溢れたクラスタの範囲のページへ書いた再描画の印を、後続の展開・次フレームの引き継ぎが読めるようにする
        commandList->BufferBarrier(dispatch.PageTable, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        m_LastGroupCount = dispatch.TotalGroups;

        // ----- 影の塊の記録: 一覧の 1 件 = 1 スレッド（容量ぶんのスレッドを x 方向に並べ、件数より後ろは何もしない） -----
        if (dispatch.Chunks)
        {
            use->ChunkSet->BindStorageBuffer(ChunkBindInstances, dispatch.Instances, 0, ClampBindSize(dispatch.Instances->GetSize()));
            use->ChunkSet->BindConstantBuffer(ChunkBindParams, use->ParamsUniform, 0, sizeof(params));
            use->ChunkSet->BindStorageBuffer(ChunkBindList, dispatch.List, 0, ClampBindSize(listBytes));
            use->ChunkSet->BindStorageBuffer(ChunkBindShadowInstances, dispatch.ShadowInstances, 0,
                                             ClampBindSize(dispatch.ShadowInstances->GetSize()));
            use->ChunkSet->BindStorageBuffer(ChunkBindChunks, dispatch.Chunks, 0, ClampBindSize(dispatch.Chunks->GetSize()));
            use->ChunkSet->Update();
            commandList->SetPipeline(m_ChunkPipeline);
            commandList->SetDescriptorSet(use->ChunkSet, 0);
            commandList->Dispatch(static_cast<uint32_t>(chunkGroups), 1u, 1u);
            commandList->BufferBarrier(dispatch.Chunks, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
            m_bLastChunkBuilt = true;
        }
        return true;
    }

    bool VirtualShadowMapMegaCullStatsReporter::Report(uint32_t instances, uint32_t clusters, uint32_t overflow)
    {
        ++m_ReportsSinceLog;
        if (m_bLogged && m_ReportsSinceLog < LogIntervalReports)
        {
            return false;
        }
        m_bLogged = true;
        m_ReportsSinceLog = 0;
        NORVES_LOG_INFO("VirtualShadowMapMegaCull", "VSM_MEGA_CULL instances=%u clusters=%u overflow=%u", instances, clusters, overflow);
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

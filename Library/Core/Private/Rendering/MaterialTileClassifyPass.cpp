#include "Rendering/MaterialTileClassifyPass.h"

#include "Logging/LogMacros.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityRasterPass.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ITexture.h"

#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // シェーダーの ClassifyParams（uvec4 screen、uvec4 limits）と同じ並び
        constexpr uint32_t ParamsBytes = 32;
        constexpr uint32_t StageCount = 0;
        constexpr uint32_t StageOffsets = 1;
        constexpr uint32_t StageScatter = 2;

        RHI::DescriptorSetDesc MakeDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::CombinedImageSampler, // 0 VisBuffer.Id
                RHI::ResourceBindType::StructuredBuffer,     // 1 描画の記録の表
                RHI::ResourceBindType::ConstantBuffer,       // 2 パラメータ
                RHI::ResourceBindType::RWBuffer,             // 3 間接 dispatch の引数
                RHI::ResourceBindType::RWBuffer,             // 4 タイルの一覧
                RHI::ResourceBindType::RWBuffer,             // 5 材質ごとの書き込み位置
                RHI::ResourceBindType::RWBuffer,             // 6 統計
            };
            for (uint32_t bindingIndex = 0; bindingIndex < 7u; ++bindingIndex)
            {
                RHI::DescriptorBinding binding;
                binding.binding = bindingIndex;
                binding.type = types[bindingIndex];
                binding.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(binding);
            }
            return desc;
        }

        uint32_t ClampBindSize(uint64_t size)
        {
            return size > std::numeric_limits<uint32_t>::max() ? std::numeric_limits<uint32_t>::max()
                                                               : static_cast<uint32_t>(size);
        }

        // 引数・統計を 0 で埋める。バッファは呼び出し前に UnorderedAccess の状態で、終わっても UnorderedAccess
        void ZeroFill(RHI::ICommandList* commandList, const RHI::BufferPtr& buffer, uint64_t size)
        {
            commandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::CopyDest, 0u, size);
            commandList->FillBuffer(buffer, 0u, size, 0u);
            commandList->BufferBarrier(buffer, RHI::ResourceState::CopyDest, RHI::ResourceState::UnorderedAccess, 0u, size);
        }
    } // namespace

    // ========================================
    // MaterialTileClassify
    // ========================================

    MaterialTileClassify::MaterialTileClassify() = default;

    MaterialTileClassify::~MaterialTileClassify()
    {
        Shutdown();
    }

    bool MaterialTileClassify::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        m_Shader = shaderManager->LoadShader("material_tile_classify.comp", RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("MaterialTileClassify", "材質のタイル分類の計算シェーダーの読み込みに失敗");
            return false;
        }

        // ID は整数なのでフィルターしない（texelFetch で読む）
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
        m_Sampler = device->CreateSampler(samplerDesc);
        if (!m_Sampler)
        {
            NORVES_LOG_WARNING("MaterialTileClassify", "材質のタイル分類のサンプラーの作成に失敗");
            Shutdown();
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc());
        m_Pipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("MaterialTileClassify", "材質のタイル分類の計算パイプラインの作成に失敗");
            Shutdown();
            return false;
        }

        m_Device = device;
        return true;
    }

    void MaterialTileClassify::Shutdown()
    {
        m_Uses.Clear();
        m_Pipeline.reset();
        m_Sampler.reset();
        m_Shader.reset();
        m_Device = nullptr;
    }

    void MaterialTileClassify::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool MaterialTileClassify::AcquireUses(Use* outUses)
    {
        // 資源の作成に失敗した Use は、次の分類が作り直す（位置は進める）
        for (uint32_t index = 0; index < DispatchesPerRecord; ++index)
        {
            Use& use = m_Uses.Acquire();
            if (!use.Uniform)
            {
                use.Uniform = m_Device->CreateBuffer(
                    RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "MaterialTileClassifyParams"));
            }
            if (!use.DescriptorSet)
            {
                use.DescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc());
            }
            if (!use.Uniform || !use.DescriptorSet)
            {
                return false;
            }
            outUses[index] = use;
        }
        return true;
    }

    bool MaterialTileClassify::RecordClear(RHI::ICommandList* commandList,
                                           const RHI::BufferPtr& args,
                                           const RHI::BufferPtr& stats)
    {
        if (!commandList || !args || !stats)
        {
            return false;
        }
        ZeroFill(commandList, args, args->GetSize());
        ZeroFill(commandList, stats, stats->GetSize());
        return true;
    }

    bool MaterialTileClassify::Record(RHI::ICommandList* commandList, const MaterialTileClassifyDispatch& dispatch)
    {
        const MaterialTiles::Layout& layout = dispatch.Layout;
        if (!m_Device || !m_Pipeline || !commandList || !layout.IsValid() || !dispatch.IdTexture || !dispatch.RecordTable ||
            dispatch.RecordTableBytes == 0 || !dispatch.Args || !dispatch.List || !dispatch.Cursors || !dispatch.Stats ||
            dispatch.Width == 0 || dispatch.Height == 0)
        {
            return false;
        }

        // 画面の外の画素は ID の画像の外を読むことになるので、画面は ID のテクスチャの大きさ以下であること。
        // 画面がタイルの数に収まり、出力のバッファが Layout の大きさ以上であること
        if (dispatch.Width > dispatch.IdTexture->GetWidth() || dispatch.Height > dispatch.IdTexture->GetHeight())
        {
            return false;
        }
        const uint32_t tilesX = (dispatch.Width + MaterialTiles::TILE_SIZE - 1) / MaterialTiles::TILE_SIZE;
        const uint32_t tilesY = (dispatch.Height + MaterialTiles::TILE_SIZE - 1) / MaterialTiles::TILE_SIZE;
        if (tilesX != layout.TilesX || tilesY != layout.TilesY ||
            dispatch.Args->GetSize() < layout.ArgsBytes() || dispatch.List->GetSize() < layout.ListBytes() ||
            dispatch.Cursors->GetSize() < layout.CursorsBytes() || dispatch.Stats->GetSize() < MaterialTiles::STATS_BYTES)
        {
            return false;
        }

        Use uses[DispatchesPerRecord];
        if (!AcquireUses(uses))
        {
            return false;
        }

        const uint64_t recordTableBytes = dispatch.RecordTableBytes < dispatch.RecordTable->GetSize()
                                              ? dispatch.RecordTableBytes
                                              : dispatch.RecordTable->GetSize();
        const uint32_t stages[DispatchesPerRecord] = {StageCount, StageOffsets, StageScatter};
        for (uint32_t index = 0; index < DispatchesPerRecord; ++index)
        {
            const uint32_t params[8] = {dispatch.Width,      dispatch.Height,     layout.TilesX,
                                        layout.TilesY,       layout.MaxMaterials, layout.ListCapacity,
                                        stages[index],       layout.GroupCountXLimit};
            Use& use = uses[index];
            use.Uniform->Update(params, ParamsBytes);
            use.DescriptorSet->BindTexture(0, dispatch.IdTexture);
            use.DescriptorSet->BindSampler(0, m_Sampler);
            use.DescriptorSet->BindStorageBuffer(1, dispatch.RecordTable, 0, ClampBindSize(recordTableBytes));
            use.DescriptorSet->BindConstantBuffer(2, use.Uniform, 0, ParamsBytes);
            use.DescriptorSet->BindStorageBuffer(3, dispatch.Args, 0, ClampBindSize(layout.ArgsBytes()));
            use.DescriptorSet->BindStorageBuffer(4, dispatch.List, 0, ClampBindSize(layout.ListBytes()));
            use.DescriptorSet->BindStorageBuffer(5, dispatch.Cursors, 0, ClampBindSize(layout.CursorsBytes()));
            use.DescriptorSet->BindStorageBuffer(6, dispatch.Stats, 0, MaterialTiles::STATS_BYTES);
            use.DescriptorSet->Update();
        }

        // 数え始める前に、材質ごとのタイルの数と統計を 0 にする
        ZeroFill(commandList, dispatch.Args, layout.ArgsBytes());
        ZeroFill(commandList, dispatch.Stats, MaterialTiles::STATS_BYTES);

        auto barrierAll = [&]()
        {
            commandList->BufferBarrier(dispatch.Args, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess,
                                       0u, layout.ArgsBytes());
            commandList->BufferBarrier(dispatch.Cursors, RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess, 0u, layout.CursorsBytes());
            commandList->BufferBarrier(dispatch.Stats, RHI::ResourceState::UnorderedAccess,
                                       RHI::ResourceState::UnorderedAccess, 0u, MaterialTiles::STATS_BYTES);
        };

        commandList->SetPipeline(m_Pipeline);

        // 段階 0: タイルごとに、そのタイルに出る材質を数える
        commandList->SetDescriptorSet(uses[0].DescriptorSet, 0);
        commandList->Dispatch(layout.TilesX, layout.TilesY, 1u);
        barrierAll();

        // 段階 1: 材質の番号の順に、一覧の先頭位置と引数を決める
        commandList->SetDescriptorSet(uses[1].DescriptorSet, 0);
        commandList->Dispatch(1u, 1u, 1u);
        barrierAll();

        // 段階 2: 同じ分類をやり直して、材質ごとの一覧へタイルの番号を書く
        commandList->SetDescriptorSet(uses[2].DescriptorSet, 0);
        commandList->Dispatch(layout.TilesX, layout.TilesY, 1u);
        commandList->BufferBarrier(dispatch.List, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess,
                                   0u, layout.ListBytes());
        barrierAll();
        return true;
    }

    // ========================================
    // MaterialTileClassifyPass
    // ========================================

    MaterialTileClassifyPass::MaterialTileClassifyPass()
    {
        // 材質の解決が使うときまで無効にしておく（起動画面の描画を変えない）。
        m_bEnabled = false;
    }

    MaterialTileClassifyPass::~MaterialTileClassifyPass()
    {
        Shutdown();
    }

    bool MaterialTileClassifyPass::Initialize(ViewRenderContext& context)
    {
        // 計算パイプラインを作れなくても描画全体は止めない（Declare が何も宣言せず、このパスは何もしない）。
        if (!m_Classify.Initialize(context.Device, context.ShaderMgr))
        {
            NORVES_LOG_WARNING("MaterialTileClassifyPass", "材質のタイル分類を使えないので、このパスは何もしない");
        }
        m_bInitialized = true;
        return true;
    }

    void MaterialTileClassifyPass::Shutdown()
    {
        m_Classify.Shutdown();
        m_Layout = {};
        m_IdHandle = {};
        m_ArgsHandle = {};
        m_ListHandle = {};
        m_CursorsHandle = {};
        m_StatsHandle = {};
        m_bClassified = false;
        m_ClassifiedTilesX = 0;
        m_bInitialized = false;
    }

    void MaterialTileClassifyPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void MaterialTileClassifyPass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void MaterialTileClassifyPass::Declare(RenderGraphBuilder& builder)
    {
        m_Layout = {};
        m_IdHandle = {};
        m_ArgsHandle = {};
        m_ListHandle = {};
        m_CursorsHandle = {};
        m_StatsHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        // 初期化を済ませたのにパイプラインが無いときは何も宣言しない（引数を 0 にするだけのパスにもしない）
        if (!context || (m_bInitialized && !m_Classify.IsReady()))
        {
            return;
        }
        const MaterialTiles::Layout layout =
            MaterialTiles::ComputeLayout(context->GetActiveRenderWidth(), context->GetActiveRenderHeight());
        if (!layout.IsValid())
        {
            return;
        }

        // ID は VisibilityRasterPass が書いたもの。無ければ（描画のパスが何も宣言しなかった）何も宣言しない
        RGTextureHandle idHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::VisBufferId, idHandle, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        // 引数・一覧・統計は、後の材質の解決が読む。カーソルはこのパスの内部の作業用
        RGBufferDesc desc;
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        desc.bCPUAccessible = false;

        // 引数は間接 dispatch の引数としても読まれる
        desc.Size = layout.ArgsBytes();
        desc.Usage = desc.Usage | RHI::ResourceUsage::IndirectBuffer;
        desc.DebugName = "MaterialTile_Args";
        m_ArgsHandle = builder.WriteBuffer(RenderGraphResourceNames::MaterialTileArgs,
                                           desc,
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::GenericRead);
        desc.Usage = RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
        desc.Size = layout.ListBytes();
        desc.DebugName = "MaterialTile_List";
        m_ListHandle = builder.WriteBuffer(RenderGraphResourceNames::MaterialTileList,
                                           desc,
                                           RHI::ResourceState::UnorderedAccess,
                                           RHI::ResourceState::GenericRead);
        desc.Size = layout.CursorsBytes();
        desc.DebugName = "MaterialTile_Cursors";
        m_CursorsHandle = builder.WriteBuffer(RenderGraphResourceNames::MaterialTileCursors,
                                              desc,
                                              RHI::ResourceState::UnorderedAccess,
                                              RHI::ResourceState::GenericRead);
        desc.Size = MaterialTiles::STATS_BYTES;
        desc.DebugName = "MaterialTile_Stats";
        m_StatsHandle = builder.WriteBuffer(RenderGraphResourceNames::MaterialTileStats,
                                            desc,
                                            RHI::ResourceState::UnorderedAccess,
                                            RHI::ResourceState::GenericRead);
        if (!m_ArgsHandle.IsValid() || !m_ListHandle.IsValid() || !m_CursorsHandle.IsValid() || !m_StatsHandle.IsValid())
        {
            m_ArgsHandle = {};
            m_ListHandle = {};
            m_CursorsHandle = {};
            m_StatsHandle = {};
            return;
        }

        m_Layout = layout;
        m_IdHandle = idHandle;
        builder.PreserveInsertionOrder();
    }

    void MaterialTileClassifyPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_bClassified = false;
        m_ClassifiedTilesX = 0;
        if (!m_Layout.IsValid() || !m_ArgsHandle.IsValid() || !m_StatsHandle.IsValid() || !context.CommandList)
        {
            return;
        }

        const RHI::BufferPtr args = resources.GetBuffer(m_ArgsHandle);
        const RHI::BufferPtr list = resources.GetBuffer(m_ListHandle);
        const RHI::BufferPtr cursors = resources.GetBuffer(m_CursorsHandle);
        const RHI::BufferPtr stats = resources.GetBuffer(m_StatsHandle);
        if (!args || !stats)
        {
            return;
        }

        const RHI::TexturePtr idTexture = m_IdHandle.IsValid() ? resources.GetTexture(m_IdHandle) : RHI::TexturePtr{};
        const RHI::BufferPtr recordTable = m_RasterPass ? m_RasterPass->GetRecordTable() : RHI::BufferPtr{};

        MaterialTileClassifyDispatch dispatch;
        dispatch.IdTexture = idTexture;
        dispatch.RecordTable = recordTable;
        dispatch.RecordTableBytes = m_RasterPass ? m_RasterPass->GetRecordTableBytes() : 0;
        dispatch.Args = args;
        dispatch.List = list;
        dispatch.Cursors = cursors;
        dispatch.Stats = stats;
        if (idTexture)
        {
            dispatch.Width = idTexture->GetWidth();
            dispatch.Height = idTexture->GetHeight();
            dispatch.Layout = MaterialTiles::ComputeLayout(dispatch.Width,
                                                           dispatch.Height,
                                                           m_Layout.MaxMaterials,
                                                           m_Layout.ListCapacity / m_Layout.TileCount,
                                                           m_Layout.GroupCountXLimit);
        }

        m_Classify.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        if (m_Classify.Record(context.CommandList, dispatch))
        {
            m_bClassified = true;
            m_ClassifiedTilesX = dispatch.Layout.TilesX;
        }
        else
        {
            // 分類できないときは、引数を 0 にして、後の材質の解決が何も dispatch しないようにする
            MaterialTileClassify::RecordClear(context.CommandList, args, stats);
        }

        // 宣言した最終の状態（GenericRead）へ渡す。RenderGraph は終わった状態を信じて、後のパスの読み取りの前に
        // バリアを足さないので、書き込んだこのパスが遷移させる。
        for (const RHI::BufferPtr& buffer : {args, list, cursors, stats})
        {
            if (buffer)
            {
                context.CommandList->BufferBarrier(buffer, RHI::ResourceState::UnorderedAccess,
                                                   RHI::ResourceState::GenericRead, 0u, buffer->GetSize());
            }
        }
    }

} // namespace NorvesLib::Core::Rendering

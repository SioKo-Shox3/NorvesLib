#include "Rendering/GrassPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/GrassBladeMesh.h"
#include "Rendering/FoliageTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/Wind.h"
#include "Logging/LogMacros.h"
#include "Math/MatrixUtils.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include <cmath>
#include <exception>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        RHI::DescriptorSetDesc MakeGrassDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            const auto add = [&desc](uint32_t binding, RHI::ResourceBindType type)
            {
                RHI::DescriptorBinding entry;
                entry.binding = binding;
                entry.type = type;
                entry.stages = RHI::ShaderStage::Compute;
                desc.bindings.push_back(entry);
            };
            add(0, RHI::ResourceBindType::ConstantBuffer);
            add(1, RHI::ResourceBindType::StructuredBuffer);
            add(2, RHI::ResourceBindType::StructuredBuffer);
            add(3, RHI::ResourceBindType::CombinedImageSampler);
            add(4, RHI::ResourceBindType::CombinedImageSampler);
            add(5, RHI::ResourceBindType::RWBuffer);
            add(6, RHI::ResourceBindType::RWBuffer);
            add(7, RHI::ResourceBindType::CombinedImageSampler);
            return desc;
        }

        bool HasGrassField(const ViewRenderContext& context)
        {
            const GrassFieldRenderData* field = context.SnapshotGrassField;
            return field && !field->Chunks.empty() && !field->Species.empty();
        }

        GrassGenerateBufferRange ToBufferRange(const FrameUploadAllocation& allocation)
        {
            return {allocation.Buffer, allocation.GetOffset32(), allocation.GetSize32()};
        }

        bool HasRange(const GrassGenerateBufferRange& range, uint64_t requiredBytes)
        {
            return range.Buffer && range.Size >= requiredBytes &&
                   static_cast<uint64_t>(range.Offset) + range.Size <= range.Buffer->GetSize();
        }

        RHI::DescriptorSetDesc MakeGrassDrawDescriptorSetDesc()
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t binding = 0; binding < 4; ++binding)
            {
                RHI::DescriptorBinding entry;
                entry.binding = binding;
                entry.type = binding == 0 ? RHI::ResourceBindType::ConstantBuffer :
                             binding == 3 ? RHI::ResourceBindType::CombinedImageSampler :
                                            RHI::ResourceBindType::StructuredBuffer;
                entry.stages = binding == 1 || binding == 3 ? RHI::ShaderStage::Vertex :
                    RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
                desc.bindings.push_back(entry);
            }
            return desc;
        }

        const Identity& GrassColorTargetName(uint32_t index)
        {
            static const Identity names[] = {
                RenderGraphResourceNames::GBufferAlbedo, RenderGraphResourceNames::GBufferNormal,
                RenderGraphResourceNames::GBufferMaterial, RenderGraphResourceNames::GBufferEmissive,
                RenderGraphResourceNames::GBufferVelocity,
            };
            return names[index];
        }
    }

    bool GrassGenerateGpu::Initialize(RHI::IDevice* device)
    {
        if (!device)
        {
            return false;
        }
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device, NORVES_SHADER_DIR))
        {
            return false;
        }
        const bool result = Initialize(device, &shaderManager);
        shaderManager.Shutdown();
        return result;
    }

    bool GrassGenerateGpu::Initialize(RHI::IDevice* device, ShaderManager* shaderManager)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        const RHI::DescriptorSetDesc layoutDesc = MakeGrassDescriptorSetDesc();
        RHI::ShaderPtr argsShader = shaderManager->LoadShader("Grass/GrassArgsInit.comp", RHI::ShaderStage::Compute);
        RHI::ShaderPtr generateShader = shaderManager->LoadShader("Grass/GrassGenerate.comp", RHI::ShaderStage::Compute);
        RHI::DescriptorSetLayoutPtr layout = device->GetOrCreateDescriptorSetLayout(layoutDesc);
        if (!argsShader || !generateShader || !layout)
        {
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.descriptorSetLayouts.push_back(layoutDesc);
        pipelineDesc.computeShader = argsShader;
        RHI::PipelinePtr argsPipeline = device->CreateComputePipeline(pipelineDesc);
        pipelineDesc.computeShader = generateShader;
        RHI::PipelinePtr generatePipeline = device->CreateComputePipeline(pipelineDesc);
        if (!argsPipeline || !generatePipeline)
        {
            return false;
        }

        m_Device = device;
        m_ArgsInitShader = argsShader;
        m_GenerateShader = generateShader;
        m_ArgsInitPipeline = argsPipeline;
        m_GeneratePipeline = generatePipeline;
        m_DescriptorSetLayout = layout;
        return true;
    }

    void GrassGenerateGpu::Shutdown()
    {
        m_RecordBuffer.reset();
        m_ArgsBuffer.reset();
        m_ArgsInitPipeline.reset();
        m_GeneratePipeline.reset();
        m_ArgsInitShader.reset();
        m_GenerateShader.reset();
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
        m_RecordBytes = 0;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            m_LodBase[lod] = 0;
            m_LodCapacity[lod] = 0;
        }
    }

    bool GrassGenerateGpu::EnsureBuffers(const GrassGenerateSettings& settings)
    {
        if (!m_Device || !m_ArgsInitPipeline || !m_GeneratePipeline)
        {
            return false;
        }
        uint64_t recordCount = 0;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            recordCount += settings.LodCapacity[lod];
        }
        const uint64_t requestedBytes = recordCount * sizeof(GrassBladeRecord);
        // 容量が 0 のときも、有効なストレージ記述子を作れる大きさを確保する。
        const uint64_t recordBytes = requestedBytes != 0 ? requestedBytes : sizeof(GrassBladeRecord);
        const uint64_t maxRange = m_Device->GetCapabilities().MaxStorageBufferRange;
        if (recordBytes > UINT32_MAX || (maxRange != 0 && recordBytes > maxRange))
        {
            return false;
        }

        RHI::BufferPtr records = m_RecordBuffer;
        RHI::BufferPtr args = m_ArgsBuffer;
        if (!records || m_RecordBytes != recordBytes)
        {
            const RHI::BufferDesc desc(recordBytes, RHI::ResourceUsage::StorageBuffer |
                RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite |
                RHI::ResourceUsage::TransferSrc, false, "Grass.Records");
            records = m_Device->CreateBuffer(desc);
        }
        if (!args)
        {
            const RHI::BufferDesc desc(kGrassArgsBufferSize, RHI::ResourceUsage::StorageBuffer |
                RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite |
                RHI::ResourceUsage::IndirectBuffer | RHI::ResourceUsage::TransferSrc, false, "Grass.Args");
            args = m_Device->CreateBuffer(desc);
        }
        if (!records || !args)
        {
            return false;
        }

        m_RecordBuffer = records;
        m_ArgsBuffer = args;
        m_RecordBytes = static_cast<uint32_t>(recordBytes);
        uint32_t base = 0;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            m_LodBase[lod] = base;
            m_LodCapacity[lod] = settings.LodCapacity[lod];
            base += settings.LodCapacity[lod];
        }
        return true;
    }

    bool GrassGenerateGpu::Record(RHI::ICommandList* commandList, const GrassGenerateRecordInputs& inputs)
    {
        if (!commandList || !inputs.DescriptorSet || inputs.DescriptorSet->IsExpired() ||
            !m_ArgsInitPipeline || !m_GeneratePipeline || !m_RecordBuffer || !m_ArgsBuffer ||
            !HasRange(inputs.Constants, sizeof(GrassGenerateConstants)))
        {
            return false;
        }
        const RHI::TexturePtr hiZ = inputs.HiZ ? inputs.HiZ : inputs.FallbackHiZ;
        if (inputs.TileCount != 0 &&
            (!HasRange(inputs.Tiles, static_cast<uint64_t>(inputs.TileCount) * sizeof(GrassTileInfo)) ||
             !HasRange(inputs.Species, sizeof(GrassSpeciesGpuData)) || !inputs.LayerA || !inputs.LayerB ||
             !hiZ || !inputs.Sampler))
        {
            return false;
        }

        inputs.DescriptorSet->BindConstantBuffer(0, inputs.Constants.Buffer, inputs.Constants.Offset, inputs.Constants.Size);
        inputs.DescriptorSet->BindStorageBuffer(6, m_ArgsBuffer, 0, kGrassArgsBufferSize);
        if (inputs.TileCount != 0)
        {
            inputs.DescriptorSet->BindStorageBuffer(1, inputs.Tiles.Buffer, inputs.Tiles.Offset, inputs.Tiles.Size);
            inputs.DescriptorSet->BindStorageBuffer(2, inputs.Species.Buffer, inputs.Species.Offset, inputs.Species.Size);
            inputs.DescriptorSet->BindTexture(3, inputs.LayerA);
            inputs.DescriptorSet->BindSampler(3, inputs.Sampler);
            inputs.DescriptorSet->BindTexture(4, inputs.LayerB);
            inputs.DescriptorSet->BindSampler(4, inputs.Sampler);
            inputs.DescriptorSet->BindStorageBuffer(5, m_RecordBuffer, 0, m_RecordBytes);
            inputs.DescriptorSet->BindTexture(7, hiZ);
            inputs.DescriptorSet->BindSampler(7, inputs.Sampler);
        }
        inputs.DescriptorSet->Update();

        // 初回のバッファにも、前回の読み取りを待つバリアをそのまま使える。
        commandList->BufferBarrier(m_RecordBuffer, RHI::ResourceState::ShaderResource, RHI::ResourceState::UnorderedAccess);
        commandList->BufferBarrier(m_ArgsBuffer, RHI::ResourceState::IndirectArgument, RHI::ResourceState::UnorderedAccess);
        commandList->SetPipeline(m_ArgsInitPipeline);
        commandList->SetDescriptorSet(inputs.DescriptorSet, 0);
        commandList->Dispatch(1, 1, 1);
        commandList->BufferBarrier(m_ArgsBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        if (inputs.TileCount != 0)
        {
            commandList->SetPipeline(m_GeneratePipeline);
            commandList->SetDescriptorSet(inputs.DescriptorSet, 0);
            commandList->Dispatch(inputs.TileCount, 1, 1);
        }
        commandList->BufferBarrier(m_RecordBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::ShaderResource);
        commandList->BufferBarrier(m_ArgsBuffer, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::IndirectArgument);
        return true;
    }

    bool GrassDrawGpu::Initialize(RHI::IDevice* device)
    {
        if (!device)
        {
            return false;
        }
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device, NORVES_SHADER_DIR))
        {
            return false;
        }
        const bool result = Initialize(device, &shaderManager);
        shaderManager.Shutdown();
        return result;
    }

    bool GrassDrawGpu::Initialize(RHI::IDevice* device, ShaderManager* shaderManager,
                                  const GrassGenerateSettings& settings)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }
        for (uint32_t segments : settings.LodSegments)
        {
            if (segments > 32767)
            {
                return false;
            }
        }
        GBufferOverlayPipelineDesc desc;
        desc.vertexShader = shaderManager->LoadShader("Grass/grass.vert", RHI::ShaderStage::Vertex);
        desc.pixelShader = shaderManager->LoadShader("Grass/grass.frag", RHI::ShaderStage::Pixel);
        const RHI::DescriptorSetDesc layoutDesc = MakeGrassDrawDescriptorSetDesc();
        RHI::DescriptorSetLayoutPtr layout = device->GetOrCreateDescriptorSetLayout(layoutDesc);
        if (!desc.vertexShader || !desc.pixelShader || !layout)
        {
            return false;
        }
        Container::VariableArray<uint32_t> indices;
        uint32_t firstIndex[kGrassLodCount] = {};
        BuildGrassBladeIndices(settings.LodSegments, indices, firstIndex);
        // 全 LOD の区切り数が 0 のときも、有効なインデックスバッファを束縛する。
        const uint32_t emptyIndex = 0;
        const uint64_t indexBytes = indices.empty() ? sizeof(emptyIndex) : indices.size() * sizeof(uint32_t);
        const RHI::BufferDesc bufferDesc(indexBytes, RHI::ResourceUsage::IndexBuffer, true, "Grass.Indices");
        RHI::BufferPtr indexBuffer = device->CreateBuffer(bufferDesc);
        if (!indexBuffer)
        {
            return false;
        }
        indexBuffer->Update(indices.empty() ? &emptyIndex : indices.data(), indexBytes);
        desc.descriptorSetLayouts.push_back(layoutDesc);
        desc.depthCompareOp = RHI::CompareOp::Less;
        desc.cullMode = RHI::CullMode::None;
        desc.debugName = "GrassPass";
        desc.failureLogPrefix = "GRASS_DRAW";
        m_Device = device;
        m_DescriptorSetLayout = layout;
        m_IndexBuffer = indexBuffer;
        m_OverlayPipelineDesc = desc;
        return true;
    }

    void GrassDrawGpu::Shutdown()
    {
        m_OverlayTargets.Reset();
        m_OverlayPipelineDesc = {};
        m_IndexBuffer.reset();
        m_DescriptorSetLayout.reset();
        m_Device = nullptr;
    }

    bool GrassDrawGpu::Record(RHI::ICommandList* commandList, const GrassDrawRecordInputs& inputs)
    {
        if (!commandList || !m_Device || !m_IndexBuffer || !m_DescriptorSetLayout || !inputs.DepthTarget ||
            !inputs.LayerA || !inputs.Sampler || !inputs.RecordBuffer || !inputs.ArgsBuffer ||
            inputs.ArgsBuffer->GetSize() < kGrassLodCount * kGrassArgsStride ||
            !HasRange(inputs.Constants, sizeof(GrassDrawConstants)) ||
            !HasRange(inputs.Species, sizeof(GrassSpeciesGpuData)))
        {
            return false;
        }
        for (const RHI::TexturePtr& target : inputs.ColorTargets)
        {
            if (!target)
            {
                return false;
            }
        }
        const auto& capabilities = m_Device->GetCapabilities();
        const uint64_t alignment = capabilities.MinStorageBufferOffsetAlignment;
        const uint64_t maxRange = capabilities.MaxStorageBufferRange;
        if ((alignment != 0 && inputs.Species.Offset % alignment != 0) ||
            (maxRange != 0 && inputs.Species.Size > maxRange))
        {
            return false;
        }
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            const uint64_t offset = static_cast<uint64_t>(inputs.LodBase[lod]) * sizeof(GrassBladeRecord);
            const uint64_t size = static_cast<uint64_t>(inputs.LodCapacity[lod]) * sizeof(GrassBladeRecord);
            if (!inputs.DescriptorSets[lod] || inputs.DescriptorSets[lod]->IsExpired() || size == 0 ||
                offset > UINT32_MAX || size > UINT32_MAX || offset + size > inputs.RecordBuffer->GetSize() ||
                (alignment != 0 && offset % alignment != 0) || (maxRange != 0 && size > maxRange))
            {
                return false;
            }
            for (uint32_t previous = 0; previous < lod; ++previous)
            {
                if (inputs.DescriptorSets[lod] == inputs.DescriptorSets[previous])
                {
                    return false;
                }
            }
        }
        if (!m_OverlayTargets.Ensure(m_Device, inputs.ColorTargets, inputs.DepthTarget, m_OverlayPipelineDesc))
        {
            return false;
        }
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            const RHI::DescriptorSetPtr& set = inputs.DescriptorSets[lod];
            set->BindConstantBuffer(0, inputs.Constants.Buffer, inputs.Constants.Offset, inputs.Constants.Size);
            set->BindStorageBuffer(1, inputs.RecordBuffer, inputs.LodBase[lod] * sizeof(GrassBladeRecord),
                                   inputs.LodCapacity[lod] * sizeof(GrassBladeRecord));
            set->BindStorageBuffer(2, inputs.Species.Buffer, inputs.Species.Offset, inputs.Species.Size);
            set->BindTexture(3, inputs.LayerA);
            set->BindSampler(3, inputs.Sampler);
            set->Update();
        }
        // 生成した記録を頂点シェーダーからも読める状態にする。
        commandList->BufferBarrier(inputs.RecordBuffer, RHI::ResourceState::ShaderResource,
                                   RHI::ResourceState::GenericRead, 0, inputs.RecordBuffer->GetSize());
        commandList->BeginRenderPass(m_OverlayTargets.GetRenderPass(), m_OverlayTargets.GetFramebuffer());
        commandList->SetViewport(inputs.Viewport);
        RHI::ScissorRect scissor = inputs.Scissor;
        if (scissor.right <= scissor.left || scissor.bottom <= scissor.top)
        {
            scissor.left = static_cast<int32_t>(inputs.Viewport.x);
            scissor.top = static_cast<int32_t>(inputs.Viewport.y);
            scissor.right = static_cast<int32_t>(inputs.Viewport.x + inputs.Viewport.width);
            scissor.bottom = static_cast<int32_t>(inputs.Viewport.y + inputs.Viewport.height);
        }
        commandList->SetScissor(scissor);
        commandList->SetPipeline(m_OverlayTargets.GetPipeline());
        commandList->SetIndexBuffer(m_IndexBuffer, 0, RHI::IndexType::Uint32);
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            commandList->SetDescriptorSet(inputs.DescriptorSets[lod], 0);
            commandList->DrawIndexedIndirect(inputs.ArgsBuffer, lod * kGrassArgsStride, 1, kGrassArgsStride);
        }
        commandList->EndRenderPass();
        commandList->BufferBarrier(inputs.RecordBuffer, RHI::ResourceState::GenericRead,
                                   RHI::ResourceState::ShaderResource, 0, inputs.RecordBuffer->GetSize());
        return true;
    }

    GrassPass::~GrassPass()
    {
        Shutdown();
    }

    bool GrassPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        bool bReady = false;
        try
        {
            if (context.Device && context.ShaderMgr && m_Generator.Initialize(context.Device, context.ShaderMgr) &&
                m_Drawer.Initialize(context.Device, context.ShaderMgr, m_Settings))
            {
                RHI::TextureDesc textureDesc;
                textureDesc.Width = 1;
                textureDesc.Height = 1;
                textureDesc.TextureFormat = RHI::Format::R32_FLOAT;
                textureDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::TransferDst;
                textureDesc.DebugName = "Grass.DummyHiZ";
                m_DummyHiZ = context.Device->CreateTexture(textureDesc);

                RHI::SamplerDesc samplerDesc;
                samplerDesc.filterMin = RHI::FilterMode::Point;
                samplerDesc.filterMag = RHI::FilterMode::Point;
                samplerDesc.filterMip = RHI::FilterMode::Point;
                samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
                samplerDesc.addressW = RHI::TextureAddressMode::Clamp;
                m_Sampler = context.Device->CreateSampler(samplerDesc);
                if (m_DummyHiZ && m_Sampler)
                {
                    // 初期化時の一度の転送で ShaderResource にし、中止されたフレームにも依存させない。
                    const float depth = 1.0f;
                    m_DummyHiZ->Update(&depth, sizeof(depth), sizeof(depth));
                    bReady = true;
                }
            }
        }
        catch (const std::exception&)
        {
            bReady = false;
        }

        if (!bReady)
        {
            m_Generator.Shutdown();
            m_Drawer.Shutdown();
            m_DummyHiZ.reset();
            m_Sampler.reset();
            m_bEnabled = false;
            NORVES_LOG_WARNING("GrassPass", "草の生成・描画資源を用意できないため、このパスを無効にします");
        }
        // 初期化に失敗しても、ほかのパスは続ける。
        m_bInitialized = true;
        return true;
    }

    void GrassPass::Shutdown()
    {
        m_Generator.Shutdown();
        m_Drawer.Shutdown();
        m_FieldWindow.Reset();
        m_TileSelector = GrassTileSelector{};
        m_SelectedTiles.clear();
        m_DummyHiZ.reset();
        m_Sampler.reset();
        m_bHasGrass = false;
        for (RGResourceHandle& handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};
#if NORVES_BUILD_DEVELOPMENT
        m_LastLoggedTileCount = UINT32_MAX;
#endif
        m_bInitialized = false;
    }

    void GrassPass::Setup(ViewRenderContext& context)
    {
        m_bHasGrass = m_bEnabled && HasGrassField(context);
    }

    void GrassPass::Declare(RenderGraphBuilder& builder)
    {
        for (RGResourceHandle& handle : m_ColorHandles)
        {
            handle = {};
        }
        m_DepthHandle = {};
        // ネイティブの RenderGraph パスでは Setup が呼ばれないため、宣言時にも同じ条件を決める。
        const ViewRenderContext* context = builder.GetContext();
        m_bHasGrass = m_bEnabled && context && HasGrassField(*context);
        if (!m_bHasGrass)
        {
            return;
        }

        // 描き先が無い構成で生成だけを行うときも、直前の描画の後に記録する。
        builder.PreserveInsertionOrder();
        RGTextureHandle available;
        for (uint32_t index = 0; index < GBufferOverlayTargets::kColorTargetCount; ++index)
        {
            if (!builder.TryGetTexture(GrassColorTargetName(index), available))
            {
                return;
            }
        }
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferDepth, available))
        {
            return;
        }
        for (uint32_t index = 0; index < GBufferOverlayTargets::kColorTargetCount; ++index)
        {
            RGTextureHandle handle;
            if (builder.TryUseAttachment(GrassColorTargetName(index), handle, RGAttachmentKind::Color,
                                        RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                        RHI::AttachmentStoreOp::Store, RHI::ResourceState::RenderTarget,
                                        RHI::ResourceState::ShaderResource))
            {
                m_ColorHandles[index] = handle.ToResourceHandle();
            }
        }
        RGTextureHandle depthHandle;
        if (builder.TryUseAttachment(RenderGraphResourceNames::GBufferDepth, depthHandle, RGAttachmentKind::DepthStencil,
                                    RGAttachmentMutability::Write, RHI::AttachmentLoadOp::Load,
                                    RHI::AttachmentStoreOp::Store, RHI::ResourceState::DepthWrite,
                                    RHI::ResourceState::ShaderResource))
        {
            m_DepthHandle = depthHandle.ToResourceHandle();
        }
    }

    void GrassPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        ExecuteInternal(context, &resources);
    }

    void GrassPass::Execute(ViewRenderContext& context)
    {
        ExecuteInternal(context, nullptr);
    }

    void GrassPass::ExecuteInternal(ViewRenderContext& context, RenderGraphResources* resources)
    {
        const GrassFieldRenderData* field = context.SnapshotGrassField;
        const CameraProxy* camera = context.GetActiveCamera();
        FrameResourceLane* frameResources = context.FrameResources;
        if (!m_bHasGrass || !m_bEnabled || !field || !camera || !context.Device ||
            !context.CommandList || !frameResources || !m_Generator.EnsureBuffers(m_Settings))
        {
            return;
        }

        const float tilesPerChunkValue = std::round(field->ChunkSizeMeters / m_Settings.TileSizeMeters);
        if (!std::isfinite(tilesPerChunkValue) || tilesPerChunkValue < 1.0f ||
            static_cast<double>(tilesPerChunkValue) > static_cast<double>(INT32_MAX) ||
            field->LayerAResolution == 0 || field->LayerBResolution == 0)
        {
            return;
        }
        const uint32_t tilesPerChunk = static_cast<uint32_t>(tilesPerChunkValue);

        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, context.GetActiveAspectRatio(), context.Device);
        const Math::Vector3 cameraPosition(cameraConstants.CameraPosition[0], cameraConstants.CameraPosition[1],
                                           cameraConstants.CameraPosition[2]);
        const Math::ClipSpaceFrustumPlanes planes = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            cameraConstants.ViewProjectionMatrix, Math::ClipSpaceDepthRange::ZeroToOne);

        const bool bWindowReady = m_FieldWindow.Update(context.Device, context.CommandList, frameResources,
                                                       *field, cameraPosition.x, cameraPosition.z);
        m_SelectedTiles.clear();
        if (bWindowReady)
        {
            GrassTileSelectionInput selection;
            selection.CameraPosition = cameraPosition;
            selection.WindowSize = m_FieldWindow.GetWindowSize();
            for (uint32_t index = 0; index < 6; ++index)
            {
                selection.FrustumPlanes[index][0] = planes.Planes[index].x;
                selection.FrustumPlanes[index][1] = planes.Planes[index].y;
                selection.FrustumPlanes[index][2] = planes.Planes[index].z;
                selection.FrustumPlanes[index][3] = planes.Planes[index].w;
            }
            m_TileSelector.Select(*field, m_Settings, selection, m_SelectedTiles);

            size_t kept = 0;
            for (const GrassTileInfo& tile : m_SelectedTiles)
            {
                const int32_t cx = GrassFloorDiv(tile.TileX, tilesPerChunk);
                const int32_t cz = GrassFloorDiv(tile.TileZ, tilesPerChunk);
                if (m_FieldWindow.IsResident(cx, cz))
                {
                    m_SelectedTiles[kept++] = tile;
                }
            }
            m_SelectedTiles.resize(kept);
        }
#if NORVES_BUILD_DEVELOPMENT
        const uint32_t selectedCount = static_cast<uint32_t>(m_SelectedTiles.size());
        if (selectedCount != m_LastLoggedTileCount)
        {
            m_LastLoggedTileCount = selectedCount;
            NORVES_LOG_INFO("GrassPass", "GRASS_TILES count=%u 選択した草のタイル数", selectedCount);
        }
#endif

        RHI::TexturePtr hiZ;
        MegaHiZContract hiZContract;
        uint32_t hiZMipCount = 0;
        const uint32_t viewId = context.CurrentViewport ? context.CurrentViewport->ViewId : UINT32_MAX;
        const uint32_t viewportId = context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        const bool bUseHiZ = m_MegaGeometryPass &&
            m_MegaGeometryPass->GetLatestHiZForRead(hiZ, hiZContract, hiZMipCount) &&
            hiZContract.bValid && hiZMipCount != 0 &&
            hiZContract.DepthWidth == context.GetActiveRenderWidth() &&
            hiZContract.DepthHeight == context.GetActiveRenderHeight() &&
            hiZContract.ViewId == viewId && hiZContract.ViewportId == viewportId &&
            hiZContract.CameraId == camera->CameraId && hiZContract.SourceCameraId == camera->SourceCameraId;

        GrassGenerateConstants constants = BuildGrassGenerateConstants(
            m_Settings, cameraPosition, planes.Planes, field->LayerAResolution, field->LayerBResolution,
            field->ChunkSizeMeters, bUseHiZ ? &hiZContract : nullptr);
        if (bUseHiZ)
        {
            constants.HiZDimensions[2] = hiZMipCount;
        }

        const FrameUploadAllocation constantAllocation = frameResources->AllocateConstants(constants);
        const FrameUploadAllocation speciesAllocation = frameResources->AllocateUploadData(
            field->Species.data(), static_cast<uint64_t>(field->Species.size()) * sizeof(GrassSpeciesGpuData),
            FrameUploadUsage::Storage);
        FrameUploadAllocation tileAllocation;
        if (!m_SelectedTiles.empty())
        {
            tileAllocation = frameResources->AllocateUploadData(
                m_SelectedTiles.data(), static_cast<uint64_t>(m_SelectedTiles.size()) * sizeof(GrassTileInfo),
                FrameUploadUsage::Storage);
        }
        const FrameDescriptorAllocation descriptorAllocation =
            frameResources->AllocateDescriptorSet(m_Generator.GetDescriptorSetLayout());
        if (!constantAllocation.IsValid() || !descriptorAllocation.IsValid())
        {
            return;
        }

        GrassGenerateRecordInputs inputs;
        inputs.DescriptorSet = descriptorAllocation.Set;
        inputs.Constants = ToBufferRange(constantAllocation);
        if (speciesAllocation.IsValid() && tileAllocation.IsValid())
        {
            inputs.Tiles = ToBufferRange(tileAllocation);
            inputs.Species = ToBufferRange(speciesAllocation);
            inputs.TileCount = static_cast<uint32_t>(m_SelectedTiles.size());
        }
        inputs.LayerA = m_FieldWindow.GetLayerATexture();
        inputs.LayerB = m_FieldWindow.GetLayerBTexture();
        inputs.HiZ = bUseHiZ ? hiZ : RHI::TexturePtr{};
        inputs.FallbackHiZ = m_DummyHiZ;
        inputs.Sampler = m_Sampler;
        if (!m_Generator.Record(context.CommandList, inputs) || !resources || !m_DepthHandle.IsValid())
        {
            return;
        }
        GrassDrawRecordInputs drawInputs;
        for (uint32_t index = 0; index < GBufferOverlayTargets::kColorTargetCount; ++index)
        {
            if (!m_ColorHandles[index].IsValid())
            {
                return;
            }
            drawInputs.ColorTargets[index] = resources->GetTexture(m_ColorHandles[index]);
            if (!drawInputs.ColorTargets[index])
            {
                return;
            }
        }
        drawInputs.DepthTarget = resources->GetTexture(m_DepthHandle);
        if (!drawInputs.DepthTarget || !speciesAllocation.IsValid() || !inputs.LayerA)
        {
            return;
        }

        GrassDrawConstants drawConstants;
        const CameraProxy* previousCamera = context.GetPreviousCamera();
        const CameraViewConstants previousConstants = previousCamera ?
            CameraViewConstants::BuildForDevice(*previousCamera, context.GetActiveAspectRatio(), context.Device) :
            cameraConstants;
        cameraConstants.CopyShaderView(&drawConstants.View[0][0]);
        cameraConstants.CopyShaderProjection(&drawConstants.Projection[0][0]);
        previousConstants.CopyShaderView(&drawConstants.PreviousView[0][0]);
        previousConstants.CopyShaderProjection(&drawConstants.PreviousProjection[0][0]);
        cameraConstants.CopyCameraPosition(drawConstants.CameraPosition);
        previousConstants.CopyCameraPosition(drawConstants.PreviousCameraPosition);
        const uint32_t renderHeight = context.GetActiveRenderHeight();
        const auto pixelSize = [renderHeight](const CameraProxy& proxy)
        {
            const float halfFovRadians = proxy.FieldOfView * 0.5f * 3.14159265f / 180.0f;
            return renderHeight > 0 ? 2.0f * std::tan(halfFovRadians) / static_cast<float>(renderHeight) : 0.0f;
        };
        drawConstants.CameraPosition[3] = pixelSize(*camera);
        drawConstants.PreviousCameraPosition[3] = previousCamera ? pixelSize(*previousCamera) :
            drawConstants.CameraPosition[3];
        drawConstants.Flags[0] = previousCamera ? kGrassDrawFlagPreviousCamera : 0;
        if (context.Wind.bEnabled)
        {
            drawConstants.Flags[0] |= kGrassDrawFlagWind;
            WindDirectionXZ(context.Wind, drawConstants.Wind[0], drawConstants.Wind[1]);
            drawConstants.Wind[2] = WindClockSeconds(context.ExtensionFrame.GameTotalTime);
            drawConstants.Wind[3] = drawConstants.Wind[2] - context.ExtensionFrame.GameDeltaTime;
            drawConstants.WindParams[0] = context.Wind.BaseSpeed;
            drawConstants.WindParams[1] = context.Wind.GustStrength;
            drawConstants.WindParams[2] = context.Wind.GustFrequencyHz;
            drawConstants.WindParams[3] = context.Wind.GustScaleMeters;
            drawConstants.WindExtra[0] = context.Wind.Turbulence;
        }
        const FoliageBatch foliageDefaults;
        drawConstants.Surface[1] = foliageDefaults.MinPixelWidth;
        drawConstants.Surface[2] = foliageDefaults.MaxWidenRatio;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            drawConstants.BandSpacingMeters[lod] = m_Settings.BandSpacingMeters[lod];
            drawConstants.LodSegments[lod] = m_Settings.LodSegments[lod];
            drawInputs.LodBase[lod] = m_Generator.GetLodBase(lod);
            drawInputs.LodCapacity[lod] = m_Generator.GetLodCapacity(lod);
            const FrameDescriptorAllocation allocation = frameResources->AllocateDescriptorSet(m_Drawer.GetDescriptorSetLayout());
            if (!allocation.IsValid())
            {
                return;
            }
            drawInputs.DescriptorSets[lod] = allocation.Set;
        }
        drawConstants.Quant[0] = m_Settings.HeightQuantMaxMeters;
        drawConstants.Quant[1] = m_Settings.WidthQuantMaxMeters;
        drawConstants.LayerWindow[0] = field->ChunkSizeMeters;
        drawConstants.LayerWindow[1] = static_cast<float>(m_FieldWindow.GetWindowSize());
        drawConstants.LayerWindow[2] = static_cast<float>(field->LayerAResolution);
        const FrameUploadAllocation drawConstantAllocation = frameResources->AllocateConstants(drawConstants);
        if (!drawConstantAllocation.IsValid())
        {
            return;
        }
        drawInputs.Constants = ToBufferRange(drawConstantAllocation);
        drawInputs.Species = ToBufferRange(speciesAllocation);
        drawInputs.RecordBuffer = m_Generator.GetRecordBuffer();
        drawInputs.ArgsBuffer = m_Generator.GetArgsBuffer();
        drawInputs.LayerA = inputs.LayerA;
        drawInputs.Sampler = m_Sampler;
        drawInputs.Viewport = context.GetActiveLocalViewport();
        drawInputs.Scissor = context.GetActiveLocalScissor();
        if (!m_Drawer.Record(context.CommandList, drawInputs))
        {
            m_bEnabled = false;
            NORVES_LOG_WARNING("GrassPass", "草の描画資源を用意できないため、このパスを無効にします");
        }
    }
} // namespace NorvesLib::Core::Rendering

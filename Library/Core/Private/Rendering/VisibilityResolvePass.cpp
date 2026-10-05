#include "Rendering/VisibilityResolvePass.h"

#include "Logging/LogMacros.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/VisibilityBuffer.h"
#include "Rendering/VisibilityRasterPass.h"
#include "RHI/DeviceCapabilities.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"

#include <cstring>
#include <limits>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        constexpr uint32_t ParamsBytes = static_cast<uint32_t>(sizeof(VisibilityResolveGeometry::ResolveParams));
        // 空の表（読まれない）の大きさ。MegaInstance（192 バイト）・InstanceData（208 バイト）より大きい
        constexpr uint64_t PlaceholderBytes = 256;

        RHI::DescriptorSetDesc MakeDescriptorSetDesc(bool bDump)
        {
            RHI::DescriptorSetDesc desc;
            const RHI::ResourceBindType types[] = {
                RHI::ResourceBindType::ConstantBuffer,       // 0 パラメータ
                RHI::ResourceBindType::StructuredBuffer,     // 1 描画の記録の表
                RHI::ResourceBindType::CombinedImageSampler, // 2 VisBuffer.Id
                RHI::ResourceBindType::StructuredBuffer,     // 3 材質の表
                RHI::ResourceBindType::StructuredBuffer,     // 4 MegaGeometry のインスタンスの表
                RHI::ResourceBindType::StructuredBuffer,     // 5 描画のインスタンスの表
                RHI::ResourceBindType::RWTexture,            // 6 GBuffer.Albedo
                RHI::ResourceBindType::RWTexture,            // 7 GBuffer.Normal
                RHI::ResourceBindType::RWTexture,            // 8 GBuffer.Velocity
                RHI::ResourceBindType::RWBuffer,             // 9 検証用の書き出し（検証用の版だけ）
            };
            const uint32_t bindingCount = bDump ? 10u : 9u;
            for (uint32_t bindingIndex = 0; bindingIndex < bindingCount; ++bindingIndex)
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

        // 使っているバイト数（0 なら全体）をバッファの大きさに収めた束ねる大きさ
        uint32_t BindBytes(const RHI::BufferPtr& buffer, uint64_t usedBytes)
        {
            const uint64_t size = buffer->GetSize();
            return ClampBindSize(usedBytes != 0 && usedBytes < size ? usedBytes : size);
        }
    } // namespace

    namespace VisibilityResolveGeometry
    {
        ResolveParams BuildParams(const CameraViewConstants& current,
                                  const CameraViewConstants* previous,
                                  const RHI::Viewport& viewport,
                                  uint32_t width,
                                  uint32_t height,
                                  uint32_t materialCount)
        {
            ResolveParams params;
            current.CopyShaderInverseView(params.InvView);
            current.CopyShaderInverseProjection(params.InvProj);
            current.CopyCameraPosition(params.CameraPosition);
            if (previous)
            {
                previous->CopyShaderViewProjection(params.PreviousViewProj);
            }
            params.Viewport[0] = viewport.x;
            params.Viewport[1] = viewport.y;
            params.Viewport[2] = viewport.width;
            params.Viewport[3] = viewport.height;
            params.Screen[0] = width;
            params.Screen[1] = height;
            params.Screen[2] = previous ? FLAG_PREVIOUS_VALID : 0u;
            params.Screen[3] = materialCount;
            return params;
        }

        bool IsSupported(const RHI::DeviceCapabilities& capabilities)
        {
            return capabilities.bGeometryShader && capabilities.bDrawIndirectFirstInstance &&
                   capabilities.bBufferDeviceAddress && capabilities.bShaderStorageImageExtendedFormats;
        }

        const char* GetFallbackReasonName(FallbackReason reason)
        {
            switch (reason)
            {
            case FallbackReason::None:
                return "none";
            case FallbackReason::PassUnavailable:
                return "pass_unavailable";
            case FallbackReason::DeviceUnsupported:
                return "device_unsupported";
            case FallbackReason::RasterUnavailable:
                return "raster_unavailable";
            case FallbackReason::ResolveUnavailable:
                return "resolve_unavailable";
            }
            return "unknown";
        }
    } // namespace VisibilityResolveGeometry

    // ========================================
    // VisibilityResolve
    // ========================================

    VisibilityResolve::VisibilityResolve() = default;

    VisibilityResolve::~VisibilityResolve()
    {
        Shutdown();
    }

    bool VisibilityResolve::Initialize(RHI::IDevice* device, ShaderManager* shaderManager, bool bDump)
    {
        Shutdown();
        if (!device || !shaderManager)
        {
            return false;
        }

        m_Shader = shaderManager->LoadShader(bDump ? "visbuffer_resolve_dump.comp" : "visbuffer_resolve.comp",
                                             RHI::ShaderStage::Compute);
        if (!m_Shader)
        {
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決の計算シェーダーの読み込みに失敗");
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
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決のサンプラーの作成に失敗");
            Shutdown();
            return false;
        }

        RHI::ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = m_Shader;
        pipelineDesc.descriptorSetLayouts.push_back(MakeDescriptorSetDesc(bDump));
        m_Pipeline = device->CreateComputePipeline(pipelineDesc);
        if (!m_Pipeline)
        {
            NORVES_LOG_WARNING("VisibilityResolve", "ビジビリティバッファの幾何の解決の計算パイプラインの作成に失敗");
            Shutdown();
            return false;
        }

        m_Device = device;
        m_bDump = bDump;
        return true;
    }

    void VisibilityResolve::Shutdown()
    {
        m_Uses.Clear();
        m_Pipeline.reset();
        m_Sampler.reset();
        m_Placeholder.reset();
        m_Shader.reset();
        m_Device = nullptr;
        m_bDump = false;
    }

    void VisibilityResolve::BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial)
    {
        m_Uses.BeginFrame(inFlightIndex, frameSerial);
    }

    bool VisibilityResolve::EnsurePlaceholder()
    {
        if (!m_Placeholder)
        {
            m_Placeholder = m_Device->CreateBuffer(RHI::BufferDesc(
                PlaceholderBytes, RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead, true,
                "VisibilityResolvePlaceholder"));
            if (m_Placeholder)
            {
                // 読まれないが、未初期化の値を残さない
                uint8_t zero[PlaceholderBytes] = {};
                m_Placeholder->Update(zero, PlaceholderBytes);
            }
        }
        return m_Placeholder != nullptr;
    }

    bool VisibilityResolve::Record(RHI::ICommandList* commandList, const VisibilityResolveDispatch& dispatch)
    {
        const VisibilityResolveGeometry::ResolveParams& params = dispatch.Params;
        const uint32_t width = params.Screen[0];
        const uint32_t height = params.Screen[1];
        if (!m_Device || !m_Pipeline || !commandList || !dispatch.IdTexture || !dispatch.RecordTable ||
            !dispatch.MaterialTable || !dispatch.Albedo || !dispatch.Normal || !dispatch.Velocity || width == 0 ||
            height == 0 || (m_bDump && !dispatch.Dump))
        {
            return false;
        }

        // 画面の外の画素は読み書きしないが、画面は入力の ID と出力の大きさに収まること
        if (width > dispatch.IdTexture->GetWidth() || height > dispatch.IdTexture->GetHeight() ||
            width > dispatch.Albedo->GetWidth() || height > dispatch.Albedo->GetHeight() ||
            width > dispatch.Normal->GetWidth() || height > dispatch.Normal->GetHeight() ||
            width > dispatch.Velocity->GetWidth() || height > dispatch.Velocity->GetHeight())
        {
            return false;
        }
        if (m_bDump &&
            dispatch.Dump->GetSize() < static_cast<uint64_t>(width) * height * VisibilityResolveGeometry::DUMP_STRIDE_BYTES)
        {
            return false;
        }
        if (!EnsurePlaceholder())
        {
            return false;
        }

        Use& use = m_Uses.Acquire();
        if (!use.Uniform)
        {
            use.Uniform = m_Device->CreateBuffer(
                RHI::BufferDesc(ParamsBytes, RHI::ResourceUsage::ConstantBuffer, true, "VisibilityResolveParams"));
        }
        if (!use.DescriptorSet)
        {
            use.DescriptorSet = m_Device->CreateDescriptorSet(MakeDescriptorSetDesc(m_bDump));
        }
        if (!use.Uniform || !use.DescriptorSet)
        {
            return false;
        }

        use.Uniform->Update(&params, ParamsBytes);
        RHI::IDescriptorSet& set = *use.DescriptorSet;
        set.BindConstantBuffer(0, use.Uniform, 0, ParamsBytes);
        set.BindStorageBuffer(1, dispatch.RecordTable, 0, BindBytes(dispatch.RecordTable, dispatch.RecordTableBytes));
        set.BindTexture(2, dispatch.IdTexture);
        set.BindSampler(2, m_Sampler);
        set.BindStorageBuffer(3, dispatch.MaterialTable, 0, BindBytes(dispatch.MaterialTable, dispatch.MaterialTableBytes));
        const RHI::BufferPtr& megaInstances = dispatch.MegaInstances ? dispatch.MegaInstances : m_Placeholder;
        set.BindStorageBuffer(4, megaInstances, 0,
                              dispatch.MegaInstances ? BindBytes(megaInstances, dispatch.MegaInstancesBytes)
                                                     : ClampBindSize(PlaceholderBytes));
        const RHI::BufferPtr& drawInstances = dispatch.DrawInstances ? dispatch.DrawInstances : m_Placeholder;
        set.BindStorageBuffer(5, drawInstances, 0,
                              dispatch.DrawInstances ? BindBytes(drawInstances, dispatch.DrawInstancesBytes)
                                                     : ClampBindSize(PlaceholderBytes));
        set.BindStorageTexture(6, dispatch.Albedo);
        set.BindStorageTexture(7, dispatch.Normal);
        set.BindStorageTexture(8, dispatch.Velocity);
        if (m_bDump)
        {
            set.BindStorageBuffer(9, dispatch.Dump, 0, ClampBindSize(dispatch.Dump->GetSize()));
        }
        set.Update();

        const uint32_t tile = VisibilityResolveGeometry::TILE_SIZE;
        commandList->SetPipeline(m_Pipeline);
        commandList->SetDescriptorSet(use.DescriptorSet, 0);
        commandList->Dispatch((width + tile - 1) / tile, (height + tile - 1) / tile, 1u);
        return true;
    }

    // ========================================
    // VisibilityResolvePass
    // ========================================

    VisibilityResolvePass::VisibilityResolvePass() = default;

    VisibilityResolvePass::~VisibilityResolvePass()
    {
        Shutdown();
    }

    bool VisibilityResolvePass::Initialize(ViewRenderContext& context)
    {
        // 計算パイプラインを作れなくても描画全体は止めない（Execute が何もしない）
        if (!m_Resolve.Initialize(context.Device, context.ShaderMgr))
        {
            NORVES_LOG_WARNING("VisibilityResolvePass", "ビジビリティバッファの幾何の解決を使えないので、このパスは何もしない");
        }
        m_bInitialized = true;
        return true;
    }

    void VisibilityResolvePass::Shutdown()
    {
        m_Resolve.Shutdown();
        m_IdHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_VelocityHandle = {};
        m_bResolved = false;
        m_bInitialized = false;
    }

    VisibilityResolveGeometry::FallbackReason VisibilityResolvePass::GetFallbackReason(const RHI::IDevice* device) const
    {
        using VisibilityResolveGeometry::FallbackReason;
        if (!m_bEnabled || !m_bInitialized)
        {
            return FallbackReason::PassUnavailable;
        }
        if (!device || !VisibilityResolveGeometry::IsSupported(device->GetCapabilities()))
        {
            return FallbackReason::DeviceUnsupported;
        }
        if (!m_RasterPass || !m_RasterPass->IsDrawReady())
        {
            return FallbackReason::RasterUnavailable;
        }
        if (!m_Resolve.IsReady())
        {
            return FallbackReason::ResolveUnavailable;
        }
        return FallbackReason::None;
    }

    void VisibilityResolvePass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void VisibilityResolvePass::Execute(ViewRenderContext& /*context*/)
    {
        // RenderGraph 経由（Execute(resources, context)）でだけ動く。
    }

    void VisibilityResolvePass::Declare(RenderGraphBuilder& builder)
    {
        m_IdHandle = {};
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_VelocityHandle = {};

        const ViewRenderContext* context = builder.GetContext();
        if (!context || !context->Device)
        {
            return;
        }
        // 対応しない装置では、GBufferPass・MegaGeometryPass が描画を止めていない。何も宣言しない
        const VisibilityResolveGeometry::FallbackReason fallbackReason = GetFallbackReason(context->Device);
        if (fallbackReason != VisibilityResolveGeometry::FallbackReason::None)
        {
            if (!m_bLoggedFallback)
            {
                m_bLoggedFallback = true;
                NORVES_LOG_WARNING("VisibilityResolvePass",
                                   "VISBUFFER_FALLBACK reason=%s ビジビリティバッファの解決を使えないので、従来の GBuffer の描画のまま動かします",
                                   VisibilityResolveGeometry::GetFallbackReasonName(fallbackReason));
            }
            return;
        }

        // ID は VisibilityRasterPass が書いたもの。無ければ（描画のパスが何も宣言しなかった）何も宣言しない
        RGTextureHandle idHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::VisBufferId, idHandle, RHI::ResourceState::ShaderResource))
        {
            return;
        }

        RGTextureHandle albedo;
        RGTextureHandle normal;
        RGTextureHandle velocity;
        if (!builder.TryGetTexture(RenderGraphResourceNames::GBufferAlbedo, albedo) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferNormal, normal) ||
            !builder.TryGetTexture(RenderGraphResourceNames::GBufferVelocity, velocity))
        {
            return;
        }

        // GBuffer の 3 枚を storage image として書く。終わった後の状態も UnorderedAccess にして、後のパスの読み取りの前に
        // グラフが ShaderResource への遷移を足すようにする
        m_AlbedoHandle = albedo.ToResourceHandle();
        m_NormalHandle = normal.ToResourceHandle();
        m_VelocityHandle = velocity.ToResourceHandle();
        builder.Write(m_AlbedoHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_NormalHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);
        builder.Write(m_VelocityHandle, RHI::ResourceState::UnorderedAccess, RHI::ResourceState::UnorderedAccess);

        // スキニングの変形した頂点（今・前）はデバイスアドレスで読む。計算シェーダーの書き込みを、この読み取りへ見せる
        if (m_SkinningComputePass)
        {
            const RGResourceHandle currentVertices = m_SkinningComputePass->GetCurrentVerticesHandle();
            if (currentVertices.IsValid())
            {
                builder.Read(currentVertices, RHI::ResourceState::GenericRead);
            }
            const RGResourceHandle previousVertices = m_SkinningComputePass->GetPreviousVerticesHandle();
            if (previousVertices.IsValid())
            {
                builder.Read(previousVertices, RHI::ResourceState::GenericRead);
            }
        }

        m_IdHandle = idHandle;
        builder.PreserveInsertionOrder();
    }

    void VisibilityResolvePass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        m_bResolved = false;
        if (!m_IdHandle.IsValid() || !m_AlbedoHandle.IsValid() || !context.CommandList)
        {
            return;
        }
        if (!m_bInitialized && !Initialize(context))
        {
            return;
        }
        if (!m_Resolve.IsReady() || !m_RasterPass)
        {
            return;
        }

        // 記録の表は、ラスタのパスが描いたフレームだけある（何も描かれなかったフレームは解決するものが無い）
        const RHI::BufferPtr recordTable = m_RasterPass->GetRecordTable();
        const RHI::BufferPtr materialTable = m_RasterPass->GetMaterialTable();
        const CameraProxy* camera = context.GetActiveCamera();
        if (!recordTable || !materialTable || !camera)
        {
            return;
        }

        VisibilityResolveDispatch dispatch;
        dispatch.IdTexture = resources.GetTexture(m_IdHandle);
        dispatch.Albedo = resources.GetTexture(m_AlbedoHandle);
        dispatch.Normal = resources.GetTexture(m_NormalHandle);
        dispatch.Velocity = resources.GetTexture(m_VelocityHandle);
        if (!dispatch.IdTexture || !dispatch.Albedo || !dispatch.Normal || !dispatch.Velocity)
        {
            return;
        }
        dispatch.RecordTable = recordTable;
        dispatch.RecordTableBytes = m_RasterPass->GetRecordTableBytes();
        dispatch.MaterialTable = materialTable;
        dispatch.MaterialTableBytes =
            static_cast<uint64_t>(m_RasterPass->GetMaterialTableCount()) * sizeof(VisibilityBuffer::MaterialEntry);
        dispatch.MegaInstances = m_RasterPass->GetMegaInstanceBuffer();
        dispatch.MegaInstancesBytes = m_RasterPass->GetMegaInstanceBufferBytes();
        dispatch.DrawInstances = context.InstanceDataBuffer;
        dispatch.DrawInstancesBytes = context.InstanceDataBuffer ? context.InstanceDataBuffer->GetSize() : 0;

        // ラスタ（GBufferPass）と同じカメラの定数。前のカメラが無いときは速度を 0 にする
        const float aspect = context.GetActiveAspectRatio();
        const CameraViewConstants current = CameraViewConstants::BuildForDevice(*camera, aspect, context.Device);
        const CameraProxy* previousCamera = context.GetPreviousCamera();
        CameraViewConstants previous;
        if (previousCamera)
        {
            previous = CameraViewConstants::BuildForDevice(*previousCamera, aspect, context.Device);
        }
        dispatch.Params = VisibilityResolveGeometry::BuildParams(current,
                                                                 previousCamera ? &previous : nullptr,
                                                                 context.GetActiveLocalViewport(),
                                                                 dispatch.IdTexture->GetWidth(),
                                                                 dispatch.IdTexture->GetHeight(),
                                                                 m_RasterPass->GetMaterialTableCount());

        m_Resolve.BeginFrame(context.FrameIndex, context.ResolveRenderFrameSerial());
        m_bResolved = m_Resolve.Record(context.CommandList, dispatch);
    }

} // namespace NorvesLib::Core::Rendering

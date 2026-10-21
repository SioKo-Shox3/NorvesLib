// TAA のパス（TemporalAAPass）の実装。Coreの登録済みのSceneView.cppから取り込む。
#include "Rendering/TemporalAAPass.h"
#include "Rendering/TemporalAAParamOverride.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraphBuilder.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "Rendering/RenderGraph/RenderGraphResources.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IPipeline.h"
#include "RHI/ISampler.h"
#include "RHI/ITexture.h"
#include "Logging/LogMacros.h"

#include <cmath>
#include <initializer_list>

namespace NorvesLib::Core::Rendering
{
    namespace TemporalAAPassDetail
    {
        struct alignas(16) GPUTemporalAAParams
        {
            float InverseViewProjection[16];
            // 前のカメラ（今のフレームと同じジッタを掛けたもの）
            float PreviousView[16];
            float PreviousProjection[16];
            // xyz: カメラ位置、w: 履歴を使うか（1/0）
            float CameraPositionAndHistory[4];
            // xy: 画像の寸法、zw: その逆数
            float ImageSize[4];
            // x: 現在の色の割合、y: 履歴へ掛ける露出の比、z: クリップの箱の半幅（標準偏差の倍数）、
            // w: 書き戻すときのシャープ化の強さ
            float Blend[4];
            // xy: このフレームのジッタ（画素）
            float Jitter[4];
        };
        static_assert(sizeof(GPUTemporalAAParams) == 256u);

        constexpr uint32_t ParamsBinding = 4u;

        // 解決: SceneColor・履歴・velocity・深度。書き戻し: 解決の結果。
        RHI::DescriptorSetDesc CreateDescriptorSetDesc(uint32_t textureCount)
        {
            RHI::DescriptorSetDesc desc;
            for (uint32_t binding = 0u; binding < textureCount; ++binding)
            {
                RHI::DescriptorBinding textureBinding;
                textureBinding.binding = binding;
                textureBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                textureBinding.stages = RHI::ShaderStage::Pixel;
                desc.bindings.push_back(textureBinding);
            }
            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = ParamsBinding;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            desc.bindings.push_back(paramsBinding);
            return desc;
        }

        RHI::DescriptorSetDesc CreateResolveDescriptorSetDesc() { return CreateDescriptorSetDesc(4u); }
        RHI::DescriptorSetDesc CreateCopyDescriptorSetDesc() { return CreateDescriptorSetDesc(1u); }

        RHI::SamplerPtr CreateSampler(RHI::IDevice& device, RHI::FilterMode filter)
        {
            RHI::SamplerDesc desc;
            desc.filterMin = filter;
            desc.filterMag = filter;
            desc.filterMip = RHI::FilterMode::Point;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            return device.CreateSampler(desc);
        }

        // 全画素を上書きするfullscreen passのrender pass（読み込まず、ShaderResourceで終える）。
        RHI::RenderPassPtr CreateOverwriteRenderPass(RHI::IDevice& device, RHI::Format format)
        {
            RHI::RenderPassDesc desc;
            RHI::AttachmentDesc colorAttachment;
            colorAttachment.format = format;
            colorAttachment.isDepthStencil = false;
            colorAttachment.clear = false;
            colorAttachment.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttachment.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttachment.initialState = RHI::ResourceState::RenderTarget;
            colorAttachment.finalState = RHI::ResourceState::ShaderResource;
            desc.colorAttachments.push_back(colorAttachment);
            desc.hasDepthStencil = false;
            return device.CreateRenderPass(desc);
        }

        RHI::PipelinePtr CreateFullscreenPipeline(RHI::IDevice& device,
                                                  const RHI::ShaderPtr& vertexShader,
                                                  const RHI::ShaderPtr& pixelShader,
                                                  const RHI::RenderPassPtr& renderPass,
                                                  const RHI::DescriptorSetDesc& layout)
        {
            RHI::GraphicsPipelineDesc desc;
            desc.vertexShader = vertexShader;
            desc.pixelShader = pixelShader;
            desc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;
            desc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            desc.rasterState.cullMode = RHI::CullMode::None;
            desc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            desc.rasterState.lineWidth = 1.0f;
            desc.depthStencilState.depthTestEnable = false;
            desc.depthStencilState.depthWriteEnable = false;
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::R | RHI::ColorWriteMask::G |
                                             RHI::ColorWriteMask::B | RHI::ColorWriteMask::A;
            desc.blendState.attachments.push_back(blendAttachment);
            desc.renderPass = renderPass;
            desc.descriptorSetLayouts.push_back(layout);
            return device.CreateGraphicsPipeline(desc);
        }

        RHI::FramebufferPtr CreateFramebuffer(RHI::IDevice& device, const RHI::RenderPassPtr& renderPass,
                                              const RHI::TexturePtr& target)
        {
            RHI::FramebufferDesc desc;
            desc.renderPass = renderPass;
            desc.colorTargets.push_back(target);
            desc.width = target->GetWidth();
            desc.height = target->GetHeight();
            return device.CreateFramebuffer(desc);
        }

        uint32_t GetViewportId(const ViewRenderContext& context)
        {
            return context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        }

        // 記述子セットをフレームの持ち主から借りる。持ち主の無い文脈（RenderingCoordinator を通さない検査）では
        // 使い回さないセットを作り、提出の完了を知る手段が無いのでパスを手放すまで unownedSets に持つ
        RHI::DescriptorSetPtr BorrowDescriptorSet(ViewRenderContext& context,
                                                  RHI::IDevice& device,
                                                  const RHI::DescriptorSetLayoutPtr& layout,
                                                  const RHI::DescriptorSetDesc& desc,
                                                  Container::VariableArray<RHI::DescriptorSetPtr>& unownedSets)
        {
            if (FrameResourceLane* frameResources = context.FrameResources)
            {
                return frameResources->AllocateDescriptorSet(layout).Set;
            }
            RHI::DescriptorSetPtr descriptorSet = device.CreateDescriptorSet(desc);
            if (descriptorSet)
            {
                unownedSets.push_back(descriptorSet);
            }
            return descriptorSet;
        }

        // 定数をフレームの持ち主から借りて params を書き、解決と書き戻しのセットの ParamsBinding へ結ぶ。持ち主の無い
        // 文脈では使い回さない専用のバッファを作り、パスを手放すまで unownedBuffers に持つ（BorrowDescriptorSet と同じ理由）
        bool BindFrameParams(ViewRenderContext& context,
                             RHI::IDevice& device,
                             std::initializer_list<RHI::IDescriptorSet*> sets,
                             const GPUTemporalAAParams& params,
                             Container::VariableArray<RHI::BufferPtr>& unownedBuffers)
        {
            RHI::BufferPtr buffer;
            uint32_t offset = 0u;
            if (FrameResourceLane* frameResources = context.FrameResources)
            {
                const FrameUploadAllocation allocation = frameResources->AllocateConstants(params);
                if (!allocation.IsValid())
                {
                    return false;
                }
                buffer = allocation.Buffer;
                offset = allocation.GetOffset32();
            }
            else
            {
                buffer = device.CreateBuffer(RHI::BufferDesc(sizeof(GPUTemporalAAParams),
                                                             RHI::ResourceUsage::ConstantBuffer, true,
                                                             "TemporalAAParams.Unowned"));
                if (!buffer)
                {
                    return false;
                }
                buffer->Update(&params, sizeof(params));
                unownedBuffers.push_back(buffer);
            }
            for (RHI::IDescriptorSet* set : sets)
            {
                set->BindConstantBuffer(ParamsBinding, buffer, offset, sizeof(GPUTemporalAAParams));
            }
            return true;
        }
    } // namespace TemporalAAPassDetail

    TemporalAAPass::TemporalAAPass()
    {
        // 既定は無効。カメラが TAA を選んだとき SceneView が有効にする。
        m_bEnabled = false;
    }

    TemporalAAPass::~TemporalAAPass()
    {
        Shutdown();
    }

    bool TemporalAAPass::Initialize(ViewRenderContext& context)
    {
        using namespace TemporalAAPassDetail;
        if (m_bInitialized)
        {
            return true;
        }
        if (!context.Device || !context.ShaderMgr)
        {
            NORVES_LOG_ERROR("TemporalAAPass", "Device または ShaderManager が無い");
            return false;
        }
        m_Device = context.Device;
        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        m_ResolveShader = context.ShaderMgr->LoadShader("temporal_aa.frag", RHI::ShaderStage::Pixel);
        m_CopyShader = context.ShaderMgr->LoadShader("temporal_aa_copy.frag", RHI::ShaderStage::Pixel);
        m_PointSampler = CreateSampler(*m_Device, RHI::FilterMode::Point);
        m_LinearSampler = CreateSampler(*m_Device, RHI::FilterMode::Linear);
        // セットは Execute ごとに借りる。レイアウトは同じ形のパスと共有する 1 つをデバイスから引く
        m_ResolveDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateResolveDescriptorSetDesc());
        m_CopyDescriptorSetLayout = m_Device->GetOrCreateDescriptorSetLayout(CreateCopyDescriptorSetDesc());
        if (!m_VertexShader || !m_ResolveShader || !m_CopyShader || !m_PointSampler || !m_LinearSampler ||
            !m_ResolveDescriptorSetLayout || !m_CopyDescriptorSetLayout)
        {
            NORVES_LOG_ERROR("TemporalAAPass", "シェーダー・サンプラー・ディスクリプタセットのレイアウトの作成に失敗");
            Shutdown();
            return false;
        }
        m_bInitialized = true;
        return true;
    }

    void TemporalAAPass::ReleaseSizedResources()
    {
        m_CopyPipeline.reset();
        m_CopyFramebuffer.reset();
        m_CopyRenderPass.reset();
        m_ResolvePipeline.reset();
        m_HistoryRenderPass.reset();
        for (uint32_t index = 0u; index < 2u; ++index)
        {
            m_HistoryFramebuffers[index].reset();
            m_HistoryTextures[index].reset();
        }
        m_FramebufferSceneColorTexture = nullptr;
        m_CurrentWidth = 0u;
        m_CurrentHeight = 0u;
        m_CurrentFormat = RHI::Format::UNKNOWN;
        m_History.Invalidate();
    }

    void TemporalAAPass::Shutdown()
    {
        if (m_HistoryReusedFrameCount > 0u || m_HistoryRejectedFrameCount > 0u)
        {
            NORVES_LOG_INFO("TemporalAAPass",
                            "history_reused_frames=%llu history_rebased_frames=%llu history_rejected_frames=%llu "
                            "history_object_state_mismatch_frames=%llu",
                            static_cast<unsigned long long>(m_HistoryReusedFrameCount),
                            static_cast<unsigned long long>(m_HistoryRebasedFrameCount),
                            static_cast<unsigned long long>(m_HistoryRejectedFrameCount),
                            static_cast<unsigned long long>(m_HistoryObjectStateMismatchCount));
            m_HistoryReusedFrameCount = 0u;
            m_HistoryRebasedFrameCount = 0u;
            m_HistoryRejectedFrameCount = 0u;
            m_HistoryObjectStateMismatchCount = 0u;
        }
        ReleaseSizedResources();
        m_UnownedDescriptorSets.clear();
        m_UnownedBuffers.clear();
        m_CopyDescriptorSetLayout.reset();
        m_ResolveDescriptorSetLayout.reset();
        m_LinearSampler.reset();
        m_PointSampler.reset();
        m_CopyShader.reset();
        m_ResolveShader.reset();
        m_VertexShader.reset();
        m_Device = nullptr;
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_VelocityHandle = {};
        m_bFrameBegun = false;
        m_bInitialized = false;
    }

    void TemporalAAPass::Setup(ViewRenderContext& /*context*/)
    {
    }

    void TemporalAAPass::Execute(ViewRenderContext& /*context*/)
    {
    }

    bool TemporalAAPass::BeginFrame(uint64_t frameNumber,
                                    uint32_t viewportId,
                                    uint32_t width,
                                    uint32_t height,
                                    TemporalAAJitter& outJitter)
    {
        if (m_bFrameBegun && m_FrameNumber == frameNumber)
        {
            // 同じフレームの2つ目以降の Viewport には TAA を掛けない（1つの履歴を共有できない）。
            if (m_FrameViewportId != viewportId)
            {
                return false;
            }
            outJitter = m_FrameJitter;
            return true;
        }
        m_FrameJitter = ComputeTemporalAAJitter(m_JitterIndex, width, height);
        // 開発ビルドの --taa-jitter-scale（既定 1）。解決の段もこのずらし量を読むので、ここで一緒に縮める
        const float jitterScale = GetTemporalAABlendParams().JitterScale;
        m_FrameJitter.PixelX *= jitterScale;
        m_FrameJitter.PixelY *= jitterScale;
        m_FrameJitter.NdcX *= jitterScale;
        m_FrameJitter.NdcY *= jitterScale;
        ++m_JitterIndex;
        m_bFrameBegun = true;
        m_FrameNumber = frameNumber;
        m_FrameViewportId = viewportId;
        outJitter = m_FrameJitter;
        return true;
    }

    bool TemporalAAPass::IsActiveFor(const ViewRenderContext& context) const
    {
        return m_bEnabled && m_bFrameBegun && m_FrameNumber == context.FrameNumber &&
               m_FrameViewportId == TemporalAAPassDetail::GetViewportId(context) &&
               context.GetActiveCamera() != nullptr && context.GetActiveDebugMode() == DebugViewMode::Normal;
    }

    void TemporalAAPass::Declare(RenderGraphBuilder& builder)
    {
        m_SceneColorHandle = {};
        m_SceneDepthHandle = {};
        m_VelocityHandle = {};
        const ViewRenderContext* context = builder.GetContext();
        if (!context || !IsActiveFor(*context))
        {
            // TAA を掛けない Viewport（同じフレームの2つ目以降など）では履歴に触れない。履歴が途切れたかは
            // 次に働くフレームで、履歴を書いたフレームの番号と比べて決める。
            return;
        }
        RGTextureHandle sceneDepthHandle;
        RGTextureHandle velocityHandle;
        if (!builder.TryReadTexture(RenderGraphResourceNames::SceneDepth, sceneDepthHandle,
                                    RHI::ResourceState::ShaderResource) ||
            !builder.TryReadTexture(RenderGraphResourceNames::GBufferVelocity, velocityHandle,
                                    RHI::ResourceState::ShaderResource))
        {
            m_History.Invalidate();
            return;
        }
        RGTextureHandle sceneColorHandle;
        const bool bHasSceneColor =
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SSRSceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource) ||
            builder.TryLoadStoreColorAttachment(RenderGraphResourceNames::SceneColor,
                                                sceneColorHandle,
                                                RHI::AttachmentLoadOp::Load,
                                                RHI::AttachmentStoreOp::Store,
                                                RHI::ResourceState::RenderTarget,
                                                RHI::ResourceState::ShaderResource);
        if (!bHasSceneColor)
        {
            m_History.Invalidate();
            return;
        }
        m_SceneDepthHandle = sceneDepthHandle.ToResourceHandle();
        m_VelocityHandle = velocityHandle.ToResourceHandle();
        m_SceneColorHandle = sceneColorHandle.ToResourceHandle();
        builder.PreserveInsertionOrder();
    }

    void TemporalAAPass::Execute(RenderGraphResources& resources, ViewRenderContext& context)
    {
        using namespace TemporalAAPassDetail;
        if (!m_SceneColorHandle.IsValid() || !m_SceneDepthHandle.IsValid() || !m_VelocityHandle.IsValid())
        {
            return;
        }
        const RHI::TexturePtr sceneColor = resources.GetTexture(m_SceneColorHandle);
        const RHI::TexturePtr sceneDepth = resources.GetTexture(m_SceneDepthHandle);
        const RHI::TexturePtr velocity = resources.GetTexture(m_VelocityHandle);
        if (!sceneColor)
        {
            m_History.Invalidate();
            return;
        }
        const auto sameSize = [&](const RHI::TexturePtr& texture)
        {
            return texture && texture->GetWidth() == sceneColor->GetWidth() &&
                   texture->GetHeight() == sceneColor->GetHeight();
        };
        const CameraProxy* camera = context.GetActiveCamera();
        if ((!m_bInitialized && !Initialize(context)) || !camera || !sameSize(sceneDepth) ||
            !sameSize(velocity) || !PrepareResources(sceneColor))
        {
            // 働けないフレームは SceneColor を変えずに渡し、履歴を捨てる。
            m_History.Invalidate();
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return;
        }

        // 履歴は、同じ Viewport が前に描いたフレームで書き、同じカメラで、露出が有限で、velocity の基準
        // （物体の前の変換と前のカメラ）が履歴を書いたフレームのときだけ使う。描画がゲームのフレームを
        // 飛ばしたときは、RenderingCoordinator が物体の前の変換を、SceneView が前のカメラを、最後に描いた
        // （履歴を書いた）フレームのものへ付け替えている。
        const CameraProxy* previousCamera = context.GetPreviousCamera();
        TemporalAAHistoryQuery historyQuery;
        historyQuery.FrameNumber = context.FrameNumber;
        historyQuery.ViewportId = GetViewportId(context);
        SetTemporalAAHistoryCamera(historyQuery, *camera);
        historyQuery.bHasPreviousCamera = previousCamera != nullptr;
        historyQuery.PreviousObjectStateFrameNumber = context.PreviousObjectStateFrameNumber;
        historyQuery.bPreviousObjectStateComplete = context.bPreviousObjectStateComplete;
        const TemporalAAHistoryDecision historyDecision = m_History.Evaluate(historyQuery);
        const bool bUseHistory = historyDecision == TemporalAAHistoryDecision::Reuse;
        if (bUseHistory)
        {
            ++m_HistoryReusedFrameCount;
            m_HistoryRebasedFrameCount += m_History.IsContiguous(historyQuery) ? 0u : 1u;
        }
        else if (historyDecision != TemporalAAHistoryDecision::NoHistory)
        {
            ++m_HistoryRejectedFrameCount;
            m_HistoryObjectStateMismatchCount +=
                historyDecision == TemporalAAHistoryDecision::ObjectStateMismatch ? 1u : 0u;
        }

        GPUTemporalAAParams params{};
        const float aspect = context.GetActiveAspectRatio();
        const CameraViewConstants cameraConstants =
            CameraViewConstants::BuildForDevice(*camera, aspect, context.Device);
        const CameraViewConstants previousConstants =
            CameraViewConstants::BuildForDevice(previousCamera ? *previousCamera : *camera, aspect, context.Device);
        cameraConstants.CopyShaderInverseViewProjection(params.InverseViewProjection);
        previousConstants.CopyShaderView(params.PreviousView);
        previousConstants.CopyShaderProjection(params.PreviousProjection);
        cameraConstants.CopyCameraPosition(params.CameraPositionAndHistory, 3u);
        params.CameraPositionAndHistory[3] = bUseHistory ? 1.0f : 0.0f;
        params.ImageSize[0] = static_cast<float>(sceneColor->GetWidth());
        params.ImageSize[1] = static_cast<float>(sceneColor->GetHeight());
        params.ImageSize[2] = 1.0f / params.ImageSize[0];
        params.ImageSize[3] = 1.0f / params.ImageSize[1];
        const TemporalAABlendParams& blendParams = GetTemporalAABlendParams();
        params.Blend[0] = blendParams.CurrentFrameWeight;
        params.Blend[1] = bUseHistory ? camera->PreExposure / m_History.GetPreExposure() : 1.0f;
        params.Blend[2] = blendParams.VarianceClipGamma;
        params.Blend[3] = blendParams.SharpenStrength;
        params.Jitter[0] = m_FrameJitter.PixelX;
        params.Jitter[1] = m_FrameJitter.PixelY;

        // 定数と記述子セットはこの Execute の分をフレームの持ち主から借りる（解決と書き戻しで同じ定数を読む）
        const RHI::DescriptorSetPtr resolveDescriptorSet =
            BorrowDescriptorSet(context, *m_Device, m_ResolveDescriptorSetLayout, CreateResolveDescriptorSetDesc(),
                                m_UnownedDescriptorSets);
        const RHI::DescriptorSetPtr copyDescriptorSet =
            BorrowDescriptorSet(context, *m_Device, m_CopyDescriptorSetLayout, CreateCopyDescriptorSetDesc(),
                                m_UnownedDescriptorSets);
        if (!resolveDescriptorSet || !copyDescriptorSet ||
            !BindFrameParams(context, *m_Device, {resolveDescriptorSet.get(), copyDescriptorSet.get()}, params,
                             m_UnownedBuffers))
        {
            NORVES_LOG_ERROR("TemporalAAPass", "定数か記述子セットを借りられないため、このフレームは TAA を掛けません");
            m_History.Invalidate();
            context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                          RHI::ResourceState::ShaderResource);
            return;
        }

        const uint32_t writeIndex = m_HistoryWriteIndex;
        const uint32_t readIndex = 1u - writeIndex;
        // 履歴が無いときは読む方の履歴が未初期化のことがあるので、代わりに SceneColor を結ぶ（シェーダーは読まない）。
        const RHI::TexturePtr& historyInput = bUseHistory ? m_HistoryTextures[readIndex] : sceneColor;
        resolveDescriptorSet->BindTexture(0u, sceneColor);
        resolveDescriptorSet->BindSampler(0u, m_PointSampler);
        resolveDescriptorSet->BindTexture(1u, historyInput);
        resolveDescriptorSet->BindSampler(1u, m_LinearSampler);
        resolveDescriptorSet->BindTexture(2u, velocity);
        resolveDescriptorSet->BindSampler(2u, m_PointSampler);
        resolveDescriptorSet->BindTexture(3u, sceneDepth);
        resolveDescriptorSet->BindSampler(3u, m_PointSampler);
        resolveDescriptorSet->Update();
        copyDescriptorSet->BindTexture(0u, m_HistoryTextures[writeIndex]);
        copyDescriptorSet->BindSampler(0u, m_PointSampler);
        copyDescriptorSet->Update();

        const RHI::Viewport viewport = context.GetActiveLocalViewport();
        const RHI::ScissorRect scissor = context.GetActiveLocalScissor();
        // 1. SceneColor・履歴・velocity・深度から、混ぜた色を書く方の履歴へ書く（前の内容は捨てる）。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::RenderTarget,
                                      RHI::ResourceState::ShaderResource);
        context.EnqueueTextureBarrier(m_HistoryTextures[writeIndex], RHI::ResourceState::Undefined,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_HistoryRenderPass, m_HistoryFramebuffers[writeIndex], viewport, scissor,
                                      m_ResolvePipeline, resolveDescriptorSet);
        // 2. SceneColor へ、軽くシャープ化して書き戻す（履歴はシャープ化の前の色のまま）。
        context.EnqueueTextureBarrier(sceneColor, RHI::ResourceState::ShaderResource,
                                      RHI::ResourceState::RenderTarget);
        context.EnqueueFullscreenPass(m_CopyRenderPass, m_CopyFramebuffer, viewport, scissor,
                                      m_CopyPipeline, copyDescriptorSet);

        m_HistoryWriteIndex = readIndex;
        m_History.Record(historyQuery, *camera);
    }

    bool TemporalAAPass::PrepareResources(const RHI::TexturePtr& sceneColor)
    {
        using namespace TemporalAAPassDetail;
        const uint32_t width = sceneColor->GetWidth();
        const uint32_t height = sceneColor->GetHeight();
        const RHI::Format format = sceneColor->GetFormat();
        if (!m_Device || width == 0u || height == 0u)
        {
            return false;
        }
        const bool bSizeChanged = !m_HistoryTextures[0] || m_CurrentWidth != width ||
                                  m_CurrentHeight != height || m_CurrentFormat != format;
        if (bSizeChanged)
        {
            // 寸法・形式が変わったら履歴を作り直し、次のフレームは現在の色だけを使う。
            ReleaseSizedResources();
            m_HistoryTextures[0] = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, format, "TemporalAA.History0"));
            m_HistoryTextures[1] = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, format, "TemporalAA.History1"));
            m_HistoryRenderPass = m_HistoryTextures[0] && m_HistoryTextures[1]
                                      ? CreateOverwriteRenderPass(*m_Device, format)
                                      : RHI::RenderPassPtr{};
            m_CopyRenderPass = CreateOverwriteRenderPass(*m_Device, format);
            for (uint32_t index = 0u; index < 2u && m_HistoryRenderPass; ++index)
            {
                m_HistoryFramebuffers[index] =
                    CreateFramebuffer(*m_Device, m_HistoryRenderPass, m_HistoryTextures[index]);
            }
            m_ResolvePipeline = m_HistoryFramebuffers[0] && m_HistoryFramebuffers[1]
                                    ? CreateFullscreenPipeline(*m_Device, m_VertexShader, m_ResolveShader,
                                                               m_HistoryRenderPass,
                                                               CreateResolveDescriptorSetDesc())
                                    : RHI::PipelinePtr{};
            m_CopyPipeline = m_CopyRenderPass
                                 ? CreateFullscreenPipeline(*m_Device, m_VertexShader, m_CopyShader,
                                                            m_CopyRenderPass, CreateCopyDescriptorSetDesc())
                                 : RHI::PipelinePtr{};
            if (!m_ResolvePipeline || !m_CopyPipeline)
            {
                NORVES_LOG_ERROR("TemporalAAPass", "履歴のテクスチャ・フレームバッファ・パイプラインの作成に失敗");
                ReleaseSizedResources();
                return false;
            }
            m_CurrentWidth = width;
            m_CurrentHeight = height;
            m_CurrentFormat = format;
            m_HistoryWriteIndex = 0u;
        }
        if (m_FramebufferSceneColorTexture != sceneColor.get() || !m_CopyFramebuffer)
        {
            m_CopyFramebuffer = CreateFramebuffer(*m_Device, m_CopyRenderPass, sceneColor);
            m_FramebufferSceneColorTexture = m_CopyFramebuffer ? sceneColor.get() : nullptr;
            if (!m_CopyFramebuffer)
            {
                NORVES_LOG_ERROR("TemporalAAPass", "SceneColor のフレームバッファの作成に失敗");
                return false;
            }
        }
        return true;
    }
} // namespace NorvesLib::Core::Rendering

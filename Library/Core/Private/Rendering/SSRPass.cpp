#include "Rendering/SSRPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SceneProxy.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    // SSRパラメータUBO = mat4 x4 + vec4 + 7floats + 1uint + pad = 304 bytes
    // projection(64) + invProjection(64) + view(64) + invView(64) + screenSize(16) + 7*4 + 4 + 4pad = 320
    static constexpr uint32_t SSR_PARAMS_SIZE = 320;

    struct GPUSSRParams
    {
        float projection[16];
        float invProjection[16];
        float view[16];
        float invView[16];
        float screenSize[4];
        float maxDistance;
        float thickness;
        float maxSteps;
        float fadeStart;
        float fadeEnd;
        float roughnessCutoff;
        float intensity;
        uint32_t bEnabled;
    };

    SSRPass::SSRPass(const SSRSettings& settings)
        : m_Settings(settings)
    {
    }

    SSRPass::~SSRPass()
    {
        if (m_bInitialized)
        {
            Shutdown();
        }
    }

    bool SSRPass::Initialize(ViewRenderContext& context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("SSRPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("SSRPass", "ShaderManager is null");
            return false;
        }

        m_VertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_VertexShader)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to load fullscreen vertex shader");
            return false;
        }

        m_FragmentShader = context.ShaderMgr->LoadShader("ssr.frag", RHI::ShaderStage::Pixel);
        if (!m_FragmentShader)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to load SSR fragment shader");
            return false;
        }

        // リニアサンプラー
        {
            RHI::SamplerDesc desc;
            desc.filterMin = RHI::FilterMode::Linear;
            desc.filterMag = RHI::FilterMode::Linear;
            desc.filterMip = RHI::FilterMode::Linear;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            m_LinearSampler = m_Device->CreateSampler(desc);
        }

        // ポイントサンプラー（深度用）
        {
            RHI::SamplerDesc desc;
            desc.filterMin = RHI::FilterMode::Point;
            desc.filterMag = RHI::FilterMode::Point;
            desc.filterMip = RHI::FilterMode::Point;
            desc.addressU = RHI::TextureAddressMode::Clamp;
            desc.addressV = RHI::TextureAddressMode::Clamp;
            desc.addressW = RHI::TextureAddressMode::Clamp;
            m_PointSampler = m_Device->CreateSampler(desc);
        }

        if (!m_LinearSampler || !m_PointSampler)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create samplers");
            return false;
        }

        // パラメータUBO
        RHI::BufferDesc paramsUboDesc(
            SSR_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "SSRParamsUBO");
        m_ParamsBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_ParamsBuffer)
        {
            NORVES_LOG_ERROR("SSRPass", "Failed to create params buffer");
            return false;
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("SSRPass", "SSRPass initialized");
        return true;
    }

    void SSRPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_OutputTexture.reset();
        m_RenderPass.reset();
        m_Framebuffer.reset();
        m_Pipeline.reset();
        m_VertexShader.reset();
        m_FragmentShader.reset();
        m_ParamsBuffer.reset();
        m_DescriptorSet.reset();
        m_LinearSampler.reset();
        m_PointSampler.reset();
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("SSRPass", "SSRPass shutdown");
    }

    void SSRPass::Setup(ViewRenderContext& context)
    {
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // 出力テクスチャ（HDRフォーマット）
            m_OutputTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SSROutput"));

            if (!m_OutputTexture)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create output texture");
                return;
            }

            // レンダーパス
            RHI::RenderPassDesc rpDesc;
            RHI::AttachmentDesc colorAttach;
            colorAttach.format = m_Settings.OutputFormat;
            colorAttach.isDepthStencil = false;
            colorAttach.clear = false;
            colorAttach.loadOp = RHI::AttachmentLoadOp::DontCare;
            colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttach.initialState = RHI::ResourceState::Undefined;
            colorAttach.finalState = RHI::ResourceState::ShaderResource;
            rpDesc.colorAttachments.push_back(colorAttach);
            rpDesc.hasDepthStencil = false;

            m_RenderPass = m_Device->CreateRenderPass(rpDesc);
            if (!m_RenderPass)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create render pass");
                return;
            }

            // フレームバッファ
            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_RenderPass;
            fbDesc.colorTargets.push_back(m_OutputTexture);
            fbDesc.width = width;
            fbDesc.height = height;

            m_Framebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!m_Framebuffer)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create framebuffer");
                return;
            }

            // ディスクリプタセット
            // binding 0: gbufferNormal (CombinedImageSampler)
            // binding 1: gbufferMaterial (CombinedImageSampler)
            // binding 2: gbufferDepth (CombinedImageSampler)
            // binding 3: sceneColor (CombinedImageSampler)
            // binding 4: SSRParams (UBO)
            // binding 5: noise (CombinedImageSampler) - 未使用だが予約
            RHI::DescriptorSetDesc dsDesc;

            for (uint32_t i = 0; i < 4; i++)
            {
                RHI::DescriptorBinding binding;
                binding.binding = i;
                binding.type = RHI::ResourceBindType::CombinedImageSampler;
                binding.stages = RHI::ShaderStage::Pixel;
                dsDesc.bindings.push_back(binding);
            }

            {
                RHI::DescriptorBinding binding;
                binding.binding = 4;
                binding.type = RHI::ResourceBindType::ConstantBuffer;
                binding.stages = RHI::ShaderStage::Pixel;
                dsDesc.bindings.push_back(binding);
            }

            {
                RHI::DescriptorBinding binding;
                binding.binding = 5;
                binding.type = RHI::ResourceBindType::CombinedImageSampler;
                binding.stages = RHI::ShaderStage::Pixel;
                dsDesc.bindings.push_back(binding);
            }

            m_DescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
            if (!m_DescriptorSet)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create descriptor set");
                return;
            }

            // UBOバインド
            m_DescriptorSet->BindConstantBuffer(4, m_ParamsBuffer, 0, SSR_PARAMS_SIZE);

            // パイプライン
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = m_VertexShader;
            pipelineDesc.pixelShader = m_FragmentShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;

            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = m_RenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

            m_Pipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            if (!m_Pipeline)
            {
                NORVES_LOG_ERROR("SSRPass", "Failed to create pipeline");
                return;
            }

            NORVES_LOG_INFO("SSRPass", "Resources created");
        }
    }

    void SSRPass::Execute(ViewRenderContext& context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_RenderPass || !m_Framebuffer || !m_Pipeline)
        {
            NORVES_LOG_WARNING("SSRPass", "Resources not ready, skipping");
            return;
        }

        // 入力テクスチャ取得
        RHI::TexturePtr normalTex, materialTex, depthTex, sceneColorTex;
        if (context.SharedResources)
        {
            normalTex = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            materialTex = context.SharedResources->GetTexturePtr("GBuffer_Material");
            depthTex = context.SharedResources->GetTexturePtr("GBuffer_Depth");
            sceneColorTex = context.SharedResources->GetTexturePtr("SceneColor");
        }

        if (!normalTex || !materialTex || !depthTex || !sceneColorTex)
        {
            NORVES_LOG_WARNING("SSRPass", "Required textures not available, skipping");
            return;
        }

        // プロジェクション行列をカメラから計算
        using namespace NorvesLib::Math;

        auto transposeToFloat = [](const Matrix4x4& mat, float* out)
        {
            for (int col = 0; col < 4; col++)
            {
                for (int row = 0; row < 4; row++)
                {
                    out[col * 4 + row] = mat.m[row][col];
                }
            }
        };

        GPUSSRParams params = {};
        params.screenSize[0] = static_cast<float>(m_CurrentWidth);
        params.screenSize[1] = static_cast<float>(m_CurrentHeight);
        params.screenSize[2] = 1.0f / static_cast<float>(m_CurrentWidth);
        params.screenSize[3] = 1.0f / static_cast<float>(m_CurrentHeight);
        params.maxDistance = m_Settings.MaxDistance;
        params.thickness = m_Settings.Thickness;
        params.maxSteps = m_Settings.MaxSteps;
        params.fadeStart = m_Settings.FadeStart;
        params.fadeEnd = m_Settings.FadeEnd;
        params.roughnessCutoff = m_Settings.RoughnessCutoff;
        params.intensity = m_Settings.Intensity;
        params.bEnabled = m_Settings.bEnabled ? 1u : 0u;

        // ビューとプロジェクション行列
        if (context.MainCamera)
        {
            const auto& cam = *context.MainCamera;

            float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
            float aspectRatio = static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight);

            Matrix4x4 projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);
            // Vulkanクリップ空間補正
            projMat.m22 *= -1.0f;
            projMat.m32 *= -1.0f;
            projMat.m11 *= -1.0f;

            Matrix4x4 viewMat = MatrixUtils::CreateLookAt(
                {cam.PositionX, cam.PositionY, cam.PositionZ},
                {cam.PositionX + cam.ForwardX, cam.PositionY + cam.ForwardY, cam.PositionZ + cam.ForwardZ},
                {cam.UpX, cam.UpY, cam.UpZ});

            transposeToFloat(projMat, params.projection);
            transposeToFloat(viewMat, params.view);

            // 逆行列
            Matrix4x4 invProj = MatrixUtils::Inverse(projMat);
            Matrix4x4 invView = MatrixUtils::Inverse(viewMat);
            transposeToFloat(invProj, params.invProjection);
            transposeToFloat(invView, params.invView);
        }

        m_ParamsBuffer->Update(&params, sizeof(GPUSSRParams));

        // 出力テクスチャ登録（SceneColorを上書き）
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_OutputTexture);
        }

        // テクスチャバインド
        m_DescriptorSet->BindTexture(0, normalTex);
        m_DescriptorSet->BindSampler(0, m_LinearSampler);

        m_DescriptorSet->BindTexture(1, materialTex);
        m_DescriptorSet->BindSampler(1, m_LinearSampler);

        m_DescriptorSet->BindTexture(2, depthTex);
        m_DescriptorSet->BindSampler(2, m_PointSampler);

        m_DescriptorSet->BindTexture(3, sceneColorTex);
        m_DescriptorSet->BindSampler(3, m_LinearSampler);

        // binding 5 (noise) - sceneColorをダミーとしてバインド
        m_DescriptorSet->BindTexture(5, sceneColorTex);
        m_DescriptorSet->BindSampler(5, m_LinearSampler);

        m_DescriptorSet->Update();

        // レンダーパス
        context.CommandList->BeginRenderPass(m_RenderPass, m_Framebuffer);

        RHI::Viewport viewport;
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_CurrentWidth);
        viewport.height = static_cast<float>(m_CurrentHeight);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        context.CommandList->SetViewport(viewport);

        RHI::ScissorRect scissor;
        scissor.left = 0;
        scissor.top = 0;
        scissor.right = static_cast<int32_t>(m_CurrentWidth);
        scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
        context.CommandList->SetScissor(scissor);

        context.CommandList->SetPipeline(m_Pipeline);
        context.CommandList->SetDescriptorSet(m_DescriptorSet, 0);
        context.CommandList->Draw(3, 0);

        context.CommandList->EndRenderPass();
    }

} // namespace NorvesLib::Core::Rendering

#include "Rendering/LightingPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{
    // ========================================
    // GPU側ライトデータ構造（シェーダーのUBOレイアウトに対応）
    // ========================================

    /** @brief GPUライトデータ（std140アライメント） */
    struct GPULightData
    {
        float position[4];    // xyz=position, w=type (0:Dir, 1:Point, 2:Spot)
        float direction[4];   // xyz=direction, w=innerAngle
        float color[4];       // xyz=color, w=intensity
        float attenuation[4]; // x=range, y=outerAngle, z=unused, w=unused
    };

    /** @brief ライティングパラメータUBO */
    struct GPULightingParams
    {
        float invViewProjection[16]; // mat4
        float cameraPosition[4];     // vec4
        float ambientColor[4];       // vec4 (xyz=color, w=intensity)
        float lightView[16];         // mat4 （シャドウマップ用ライトビュー行列）
        float lightProjection[16];   // mat4 （シャドウマップ用ライトプロジェクション行列）
        uint32_t lightCount;         // uint
        uint32_t bShadowEnabled;     // uint （シャドウマップ有効フラグ）
        uint32_t _pad0;
        uint32_t _pad1;
    };

    static constexpr uint32_t MAX_LIGHTS = 16;
    static constexpr uint32_t LIGHTING_PARAMS_SIZE = sizeof(GPULightingParams);
    static constexpr uint32_t LIGHT_BUFFER_SIZE = sizeof(GPULightData) * MAX_LIGHTS;

    LightingPass::LightingPass(const LightingPassSettings &settings)
        : m_Settings(settings)
    {
    }

    LightingPass::~LightingPass()
    {
        Shutdown();
    }

    bool LightingPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("LightingPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // フルスクリーン頂点シェーダー作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("LightingPass", "ShaderManager is null");
            return false;
        }

        m_LightingVertexShader = context.ShaderMgr->LoadShader("fullscreen.vert", RHI::ShaderStage::Vertex);
        if (!m_LightingVertexShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create fullscreen vertex shader");
            return false;
        }

        // ========================================
        // ライティングフラグメントシェーダー作成
        // ========================================
        m_LightingFragmentShader = context.ShaderMgr->LoadShader("lighting.frag", RHI::ShaderStage::Pixel);
        if (!m_LightingFragmentShader)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting fragment shader");
            return false;
        }

        // ========================================
        // GBufferサンプラー作成
        // ========================================
        RHI::SamplerDesc samplerDesc;
        samplerDesc.filterMin = RHI::FilterMode::Point;
        samplerDesc.filterMag = RHI::FilterMode::Point;
        samplerDesc.filterMip = RHI::FilterMode::Point;
        samplerDesc.addressU = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressV = RHI::TextureAddressMode::Clamp;
        samplerDesc.addressW = RHI::TextureAddressMode::Clamp;

        m_GBufferSampler = m_Device->CreateSampler(samplerDesc);
        if (!m_GBufferSampler)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create GBuffer sampler");
            return false;
        }

        // ========================================
        // ライティングパラメータUBOバッファ作成
        // ========================================
        RHI::BufferDesc paramsUboDesc(
            LIGHTING_PARAMS_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "LightingParamsUBO");
        m_LightDataBuffer = m_Device->CreateBuffer(paramsUboDesc);
        if (!m_LightDataBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create lighting params buffer");
            return false;
        }

        // ========================================
        // ライト配列UBOバッファ作成（binding=5用）
        // ========================================
        RHI::BufferDesc lightArrayUboDesc(
            LIGHT_BUFFER_SIZE, RHI::ResourceUsage::ConstantBuffer, true, "LightArrayUBO");
        m_LightArrayBuffer = m_Device->CreateBuffer(lightArrayUboDesc);
        if (!m_LightArrayBuffer)
        {
            NORVES_LOG_ERROR("LightingPass", "Failed to create light array buffer");
            return false;
        }

        // ========================================
        // ライトバッファ作成（ライト配列用別UBO）
        // ========================================
        // Note: ライトバッファはm_LightDataBufferの後ろに配置するのではなく、
        //       別のバインディングポイント（binding=5）として作成

        m_bInitialized = true;
        NORVES_LOG_INFO("LightingPass", "LightingPass initialized");
        return true;
    }

    void LightingPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_SceneColorTexture.reset();
        m_LightingRenderPass.reset();
        m_LightingFramebuffer.reset();
        m_LightingPipeline.reset();
        m_LightingVertexShader.reset();
        m_LightingFragmentShader.reset();
        m_LightDataBuffer.reset();
        m_LightArrayBuffer.reset();
        m_LightingDescriptorSet.reset();
        m_GBufferSampler.reset();
        m_Device = nullptr;

        m_bInitialized = false;
        NORVES_LOG_INFO("LightingPass", "LightingPass shutdown");
    }

    void LightingPass::Setup(ViewRenderContext &context)
    {
        uint32_t width = context.ScreenWidth;
        uint32_t height = context.ScreenHeight;

        if (width == 0 || height == 0)
        {
            return;
        }

        // サイズ変更があればレンダーパス・フレームバッファを再作成
        if (width != m_CurrentWidth || height != m_CurrentHeight)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // HDRシーンカラーテクスチャ作成
            m_SceneColorTexture = m_Device->CreateTexture(
                RHI::TextureDesc::RenderTarget(width, height, m_Settings.OutputFormat, "SceneColor"));

            if (!m_SceneColorTexture)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create SceneColor texture");
                return;
            }

            // ========================================
            // ライティング用レンダーパス作成（1カラー、デプス無し）
            // ========================================
            RHI::RenderPassDesc rpDesc;

            RHI::AttachmentDesc colorAttach;
            colorAttach.format = m_Settings.OutputFormat;
            colorAttach.isDepthStencil = false;
            colorAttach.clear = true;
            colorAttach.clearColor[0] = 0.0f;
            colorAttach.clearColor[1] = 0.0f;
            colorAttach.clearColor[2] = 0.0f;
            colorAttach.clearColor[3] = 1.0f;
            colorAttach.loadOp = RHI::AttachmentLoadOp::Clear;
            colorAttach.storeOp = RHI::AttachmentStoreOp::Store;
            colorAttach.initialState = RHI::ResourceState::Undefined;
            colorAttach.finalState = RHI::ResourceState::ShaderResource;
            rpDesc.colorAttachments.push_back(colorAttach);

            rpDesc.hasDepthStencil = false;

            m_LightingRenderPass = m_Device->CreateRenderPass(rpDesc);
            if (!m_LightingRenderPass)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create lighting render pass");
                return;
            }

            // ========================================
            // フレームバッファ作成
            // ========================================
            RHI::FramebufferDesc fbDesc;
            fbDesc.renderPass = m_LightingRenderPass;
            fbDesc.colorTargets.push_back(m_SceneColorTexture);
            fbDesc.width = width;
            fbDesc.height = height;

            m_LightingFramebuffer = m_Device->CreateFramebuffer(fbDesc);
            if (!m_LightingFramebuffer)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create lighting framebuffer");
                return;
            }

            // ========================================
            // ディスクリプタセット作成
            // ========================================
            // binding 0-3: GBufferテクスチャ（combined image sampler）
            // binding 4: ライティングパラメータUBO
            // binding 5: ライトバッファUBO
            RHI::DescriptorSetDesc dsDesc;

            // GBuffer Albedo (binding=0, combined image sampler)
            RHI::DescriptorBinding albedoBinding;
            albedoBinding.binding = 0;
            albedoBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            albedoBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(albedoBinding);

            // GBuffer Normal (binding=1)
            RHI::DescriptorBinding normalBinding;
            normalBinding.binding = 1;
            normalBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            normalBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(normalBinding);

            // GBuffer Material (binding=2)
            RHI::DescriptorBinding materialBinding;
            materialBinding.binding = 2;
            materialBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            materialBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(materialBinding);

            // GBuffer Depth (binding=3)
            RHI::DescriptorBinding depthBinding;
            depthBinding.binding = 3;
            depthBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            depthBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(depthBinding);

            // Lighting params UBO (binding=4)
            RHI::DescriptorBinding paramsBinding;
            paramsBinding.binding = 4;
            paramsBinding.type = RHI::ResourceBindType::ConstantBuffer;
            paramsBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(paramsBinding);

            // Light buffer UBO (binding=5)
            RHI::DescriptorBinding lightBinding;
            lightBinding.binding = 5;
            lightBinding.type = RHI::ResourceBindType::ConstantBuffer;
            lightBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(lightBinding);

            // Shadow map (binding=6, combined image sampler)
            RHI::DescriptorBinding shadowBinding;
            shadowBinding.binding = 6;
            shadowBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            shadowBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(shadowBinding);

            m_LightingDescriptorSet = m_Device->CreateDescriptorSet(dsDesc);
            if (!m_LightingDescriptorSet)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create lighting descriptor set");
                return;
            }

            // UBOバインド（テクスチャバインドはExecute時にGBufferが確定してから行う）
            m_LightingDescriptorSet->BindConstantBuffer(4, m_LightDataBuffer, 0, LIGHTING_PARAMS_SIZE);
            m_LightingDescriptorSet->BindConstantBuffer(5, m_LightArrayBuffer, 0, LIGHT_BUFFER_SIZE);

            // ========================================
            // パイプライン作成（フルスクリーンの頂点バッファなし描画）
            // ========================================
            RHI::GraphicsPipelineDesc pipelineDesc;
            pipelineDesc.vertexShader = m_LightingVertexShader;
            pipelineDesc.pixelShader = m_LightingFragmentShader;
            pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

            // 頂点入力なし（フルスクリーントライアングルはシェーダー内で生成）

            // ラスタライザ
            pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
            pipelineDesc.rasterState.cullMode = RHI::CullMode::None;
            pipelineDesc.rasterState.frontFace = RHI::FrontFace::CounterClockwise;
            pipelineDesc.rasterState.lineWidth = 1.0f;

            // デプステスト無効（フルスクリーン描画）
            pipelineDesc.depthStencilState.depthTestEnable = false;
            pipelineDesc.depthStencilState.depthWriteEnable = false;

            // ブレンド無効
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);

            pipelineDesc.renderPass = m_LightingRenderPass;
            pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

            m_LightingPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
            if (!m_LightingPipeline)
            {
                NORVES_LOG_ERROR("LightingPass", "Failed to create lighting pipeline");
                return;
            }

            NORVES_LOG_INFO("LightingPass", "Lighting resources resized (%ux%u)", width, height);
        }
    }

    void LightingPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_LightingRenderPass || !m_LightingFramebuffer || !m_LightingPipeline)
        {
            NORVES_LOG_WARNING("LightingPass", "Lighting resources not ready, skipping");
            return;
        }

        // GBufferテクスチャをSharedResourceRegistryから取得
        RHI::ITexture *gbufferAlbedo = nullptr;
        RHI::ITexture *gbufferNormal = nullptr;
        RHI::ITexture *gbufferMaterial = nullptr;
        RHI::ITexture *gbufferDepth = nullptr;

        if (context.SharedResources)
        {
            gbufferAlbedo = context.SharedResources->GetTexture("GBuffer_Albedo");
            gbufferNormal = context.SharedResources->GetTexture("GBuffer_Normal");
            gbufferMaterial = context.SharedResources->GetTexture("GBuffer_Material");
            gbufferDepth = context.SharedResources->GetTexture("GBuffer_Depth");
        }

        // GBufferが無い場合はスキップ
        if (!gbufferAlbedo || !gbufferNormal || !gbufferMaterial || !gbufferDepth)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer textures not available, skipping lighting");
            return;
        }

        // シャドウマップをSharedResourceRegistryから取得
        RHI::TexturePtr shadowMapPtr;
        bool bShadowAvailable = false;
        if (context.SharedResources)
        {
            shadowMapPtr = context.SharedResources->GetTexturePtr("ShadowMap");
            bShadowAvailable = (shadowMapPtr != nullptr);
        }

        // ライト情報をGPUバッファに転送
        UpdateLightBuffer(context, bShadowAvailable);

        // HDRシーンカラーをSharedResourceRegistryに登録
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("SceneColor", m_SceneColorTexture);
            context.SharedResources->RegisterTexture("SceneDepth", gbufferDepth);
        }

        // GBufferテクスチャ（TexturePtr版）をディスクリプタセットにバインド
        RHI::TexturePtr albedoPtr;
        RHI::TexturePtr normalPtr;
        RHI::TexturePtr materialPtr;
        RHI::TexturePtr depthPtr;

        if (context.SharedResources)
        {
            albedoPtr = context.SharedResources->GetTexturePtr("GBuffer_Albedo");
            normalPtr = context.SharedResources->GetTexturePtr("GBuffer_Normal");
            materialPtr = context.SharedResources->GetTexturePtr("GBuffer_Material");
            depthPtr = context.SharedResources->GetTexturePtr("GBuffer_Depth");
        }

        if (!albedoPtr || !normalPtr || !materialPtr || !depthPtr)
        {
            NORVES_LOG_WARNING("LightingPass", "GBuffer TexturePtr not available, skipping");
            return;
        }

        m_LightingDescriptorSet->BindTexture(0, albedoPtr);
        m_LightingDescriptorSet->BindTexture(1, normalPtr);
        m_LightingDescriptorSet->BindTexture(2, materialPtr);
        m_LightingDescriptorSet->BindTexture(3, depthPtr);
        m_LightingDescriptorSet->BindSampler(0, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(1, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(2, m_GBufferSampler);
        m_LightingDescriptorSet->BindSampler(3, m_GBufferSampler);

        // シャドウマップバインド（利用不可の場合はGBuffer深度をフォールバック）
        if (bShadowAvailable)
        {
            m_LightingDescriptorSet->BindTexture(6, shadowMapPtr);
        }
        else
        {
            m_LightingDescriptorSet->BindTexture(6, depthPtr);
        }
        m_LightingDescriptorSet->BindSampler(6, m_GBufferSampler);

        m_LightingDescriptorSet->Update();

        // ライティングレンダーパス開始
        context.CommandList->BeginRenderPass(m_LightingRenderPass, m_LightingFramebuffer);

        // ビューポート・シザー設定
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

        // パイプラインとディスクリプタセットをバインド
        context.CommandList->SetPipeline(m_LightingPipeline);
        context.CommandList->SetDescriptorSet(m_LightingDescriptorSet, 0);

        // フルスクリーントライアングル描画（頂点数3）
        context.CommandList->Draw(3, 0);

        // レンダーパス終了
        context.CommandList->EndRenderPass();
    }

    void LightingPass::UpdateLightBuffer(ViewRenderContext &context, bool bShadowAvailable)
    {
        // ライティングパラメータを構築
        GPULightingParams params = {};

        using namespace NorvesLib::Math;

        // RowMajor → ColumnMajor転置ヘルパー
        auto TransposeToFloat = [](const Matrix4x4 &mat, float *out)
        {
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    out[col * 4 + row] = mat.m[row][col];
                }
            }
        };

        // ========================================
        // カメラ行列の計算（GBufferPassと同じロジック）
        // ========================================
        Matrix4x4 viewMat = Matrix4x4::Identity;
        Matrix4x4 projMat = Matrix4x4::Identity;

        if (context.MainCamera)
        {
            const auto &cam = *context.MainCamera;
            Vector3 camPos(cam.PositionX, cam.PositionY, cam.PositionZ);
            Vector3 forward(cam.ForwardX, cam.ForwardY, cam.ForwardZ);
            Vector3 lookAt = camPos + forward;
            Vector3 upDir(cam.UpX, cam.UpY, cam.UpZ);

            viewMat = MatrixUtils::CreateLookAt(camPos, lookAt, upDir);

            float aspectRatio = (m_CurrentHeight > 0)
                ? static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight)
                : 16.0f / 9.0f;
            float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
            projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);

            // 右手系変換
            projMat.m22 *= -1.0f;
            projMat.m32 *= -1.0f;
            // Vulkan Y反転
            projMat.m11 *= -1.0f;

            params.cameraPosition[0] = cam.PositionX;
            params.cameraPosition[1] = cam.PositionY;
            params.cameraPosition[2] = cam.PositionZ;
            params.cameraPosition[3] = 1.0f;
        }
        else
        {
            params.cameraPosition[0] = 0.0f;
            params.cameraPosition[1] = 2.0f;
            params.cameraPosition[2] = 5.0f;
            params.cameraPosition[3] = 1.0f;
        }

        // invViewProjection行列の計算
        Matrix4x4 vpMat = projMat * viewMat;
        Matrix4x4 invVPMat = MatrixUtils::Inverse(vpMat);
        TransposeToFloat(invVPMat, params.invViewProjection);

        // アンビエントカラー
        params.ambientColor[0] = m_Settings.AmbientColor[0];
        params.ambientColor[1] = m_Settings.AmbientColor[1];
        params.ambientColor[2] = m_Settings.AmbientColor[2];
        params.ambientColor[3] = m_Settings.AmbientIntensity;

        // ========================================
        // シャドウマップ用ライトビュー・プロジェクション行列
        // ========================================
        if (bShadowAvailable)
        {
            // ShadowMapPassと同じライト方向・パラメータ
            float lightDirX = -0.577f;
            float lightDirY = -0.577f;
            float lightDirZ = -0.577f;

            float lightDistance = 20.0f;
            Vector3 lightPos(-lightDirX * lightDistance, -lightDirY * lightDistance, -lightDirZ * lightDistance);
            Vector3 lightTarget(0.0f, 0.0f, 0.0f);
            Vector3 upDir(0.0f, 1.0f, 0.0f);

            Matrix4x4 lightViewMat = MatrixUtils::CreateLookAt(lightPos, lightTarget, upDir);

            float orthoSize = 20.0f;
            Matrix4x4 lightProjMat = MatrixUtils::CreateOrthographic(
                orthoSize * 2.0f, orthoSize * 2.0f, 0.1f, 50.0f);

            // 右手系変換（Vulkan深度[0,1]）
            lightProjMat.m22 *= -1.0f;
            lightProjMat.m32 *= -1.0f;
            // シャドウマップではY反転は適用しない

            TransposeToFloat(lightViewMat, params.lightView);
            TransposeToFloat(lightProjMat, params.lightProjection);
            params.bShadowEnabled = 1;
        }
        else
        {
            // シャドウ無効時は単位行列を設定
            for (int i = 0; i < 16; ++i)
            {
                params.lightView[i] = (i % 5 == 0) ? 1.0f : 0.0f;
                params.lightProjection[i] = (i % 5 == 0) ? 1.0f : 0.0f;
            }
            params.bShadowEnabled = 0;
        }

        // デフォルトのディレクショナルライトを1つ追加
        params.lightCount = 1;

        m_LightDataBuffer->Update(&params, sizeof(GPULightingParams));

        // ライト配列バッファ更新（デフォルトのディレクショナルライト）
        GPULightData defaultLight = {};
        // position.w = 0 → ディレクショナルライト
        defaultLight.position[0] = 0.0f;
        defaultLight.position[1] = 0.0f;
        defaultLight.position[2] = 0.0f;
        defaultLight.position[3] = 0.0f; // type: Directional
        // ディレクショナルライトの方向（正規化済み）
        defaultLight.direction[0] = -0.577f;
        defaultLight.direction[1] = -0.577f;
        defaultLight.direction[2] = -0.577f;
        defaultLight.direction[3] = 0.0f; // innerAngle
        // 白色、強度1.0
        defaultLight.color[0] = 1.0f;
        defaultLight.color[1] = 1.0f;
        defaultLight.color[2] = 1.0f;
        defaultLight.color[3] = 1.0f; // intensity
        // 減衰パラメータ
        defaultLight.attenuation[0] = 100.0f; // range
        defaultLight.attenuation[1] = 0.0f;
        defaultLight.attenuation[2] = 0.0f;
        defaultLight.attenuation[3] = 0.0f;

        m_LightArrayBuffer->Update(&defaultLight, sizeof(GPULightData));
    }

} // namespace NorvesLib::Core::Rendering

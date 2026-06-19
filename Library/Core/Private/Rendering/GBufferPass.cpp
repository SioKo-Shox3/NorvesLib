#include "Rendering/GBufferPass.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/RenderResources.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Rendering/CameraViewConstants.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/RenderGraph/RenderGraphResourceNames.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ITexture.h"
#include "RHI/ISampler.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/TransientResourcePool.h"
#include "Math/MatrixUtils.h"
#include "Logging/LogMacros.h"

namespace NorvesLib::Core::Rendering
{

    GBufferPass::GBufferPass(const GBufferPassSettings& settings)
        : m_Settings(settings)
    {
    }

    GBufferPass::~GBufferPass()
    {
        Shutdown();
    }

    bool GBufferPass::Initialize(ViewRenderContext &context)
    {
        if (m_bInitialized)
        {
            return true;
        }

        if (!context.Device)
        {
            NORVES_LOG_ERROR("GBufferPass", "Device is null");
            return false;
        }

        m_Device = context.Device;

        // ========================================
        // GBuffer用シェーダーの作成
        // ========================================
        if (!context.ShaderMgr)
        {
            NORVES_LOG_ERROR("GBufferPass", "ShaderManager is null");
            return false;
        }

        m_GBufferVertexShader = context.ShaderMgr->LoadShader("gbuffer.vert", RHI::ShaderStage::Vertex);
        if (!m_GBufferVertexShader)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer vertex shader");
            return false;
        }

        m_GBufferFragmentShader = context.ShaderMgr->LoadShader("gbuffer.frag", RHI::ShaderStage::Pixel);
        if (!m_GBufferFragmentShader)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer fragment shader");
            return false;
        }

        m_bInitialized = true;

        // ========================================
        // DynamicUniformAllocator初期化
        // ========================================
        {
            // UBOレイアウト: view(64) + projection(64) + cameraPos(16) + emissiveColor(16) + pomParams(16) = 176 bytes
            constexpr uint32_t UBO_SIZE = 176;
            constexpr uint32_t MAX_OBJECTS = 256; // 1フレームあたりの最大オブジェクト数

            RHI::DescriptorSetDesc uboDescSetDesc;
            RHI::DescriptorBinding uboBinding;
            uboBinding.binding = 0;
            uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
            uboDescSetDesc.bindings.push_back(uboBinding);

            // PBRテクスチャサンプラー（binding 1-6: albedo, normal, metallic, roughness, ao, height）
            for (uint32_t i = 1; i <= 6; ++i)
            {
                RHI::DescriptorBinding texBinding;
                texBinding.binding = i;
                texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                texBinding.stages = RHI::ShaderStage::Pixel;
                uboDescSetDesc.bindings.push_back(texBinding);
            }

            RHI::DescriptorBinding instanceBinding;
            instanceBinding.binding = 7;
            instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
            instanceBinding.stages = RHI::ShaderStage::Vertex;
            uboDescSetDesc.bindings.push_back(instanceBinding);

            if (!m_UniformAllocator.Initialize(m_Device, UBO_SIZE, MAX_OBJECTS, uboDescSetDesc))
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to initialize DynamicUniformAllocator");
                return false;
            }
        }

        // ========================================
        // デフォルトPBRテクスチャとLinearサンプラーの作成
        // ========================================
        {
            // ヘルパー: 1x1テクスチャ作成
            auto CreateDefault1x1 = [this](const char* debugName, uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> RHI::TexturePtr
            {
                RHI::TextureDesc texDesc;
                texDesc.Width = 1;
                texDesc.Height = 1;
                texDesc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
                texDesc.Usage = RHI::ResourceUsage::ShaderRead;
                texDesc.DebugName = debugName;

                auto tex = m_Device->CreateTexture(texDesc);
                if (tex)
                {
                    uint8_t pixel[4] = {r, g, b, a};
                    tex->Update(pixel, 4, 4);
                }
                return tex;
            };

            // 1x1白テクスチャ（アルベド・AOデフォルト）
            m_DefaultWhiteTexture = CreateDefault1x1("DefaultWhite1x1", 255, 255, 255, 255);
            if (!m_DefaultWhiteTexture)
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to create default white texture");
                return false;
            }

            // 1x1フラット法線テクスチャ（ノーマルマップデフォルト: (0.5, 0.5, 1.0) → (128, 128, 255)）
            m_DefaultFlatNormalTexture = CreateDefault1x1("DefaultFlatNormal1x1", 128, 128, 255, 255);
            if (!m_DefaultFlatNormalTexture)
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to create default flat normal texture");
                return false;
            }

            // 1x1黒テクスチャ（メタリックデフォルト: 0.0 = 非金属）
            m_DefaultBlackTexture = CreateDefault1x1("DefaultBlack1x1", 0, 0, 0, 255);
            if (!m_DefaultBlackTexture)
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to create default black texture");
                return false;
            }

            // 1x1中間灰テクスチャ（ラフネスデフォルト: 0.5）
            m_DefaultMidGrayTexture = CreateDefault1x1("DefaultMidGray1x1", 128, 128, 128, 255);
            if (!m_DefaultMidGrayTexture)
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to create default mid-gray texture");
                return false;
            }

            RHI::SamplerDesc sampDesc;
            sampDesc.filterMin = RHI::FilterMode::Anisotropic;
            sampDesc.filterMag = RHI::FilterMode::Anisotropic;
            sampDesc.filterMip = RHI::FilterMode::Anisotropic;
            sampDesc.addressU = RHI::TextureAddressMode::Wrap;
            sampDesc.addressV = RHI::TextureAddressMode::Wrap;
            sampDesc.addressW = RHI::TextureAddressMode::Wrap;
            sampDesc.maxAnisotropy = 4;

            m_DefaultLinearSampler = m_Device->CreateSampler(sampDesc);
            if (!m_DefaultLinearSampler)
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to create default sampler");
                return false;
            }
        }

        NORVES_LOG_INFO("GBufferPass", "GBufferPass initialized");
        return true;
    }

    void GBufferPass::Shutdown()
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();
        m_AlbedoHandle = {};
        m_NormalHandle = {};
        m_MaterialHandle = {};
        m_EmissiveHandle = {};
        m_DepthHandle = {};
        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_GBufferPipeline.reset();
        m_GBufferVertexShader.reset();
        m_GBufferFragmentShader.reset();
        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultMidGrayTexture.reset();
        m_DefaultLinearSampler.reset();
        m_UniformAllocator.Shutdown();
        m_SceneView = nullptr;
        m_SceneRenderer = nullptr;
        m_Device = nullptr;
        m_CurrentWidth = 0;
        m_CurrentHeight = 0;
        m_bUsingRenderGraphResources = false;
        m_bRenderPassUsesRenderGraphInitialStates = false;
        m_FramebufferAlbedoTexture = nullptr;
        m_FramebufferNormalTexture = nullptr;
        m_FramebufferMaterialTexture = nullptr;
        m_FramebufferEmissiveTexture = nullptr;
        m_FramebufferDepthTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;

        m_bInitialized = false;
        NORVES_LOG_INFO("GBufferPass", "GBufferPass shutdown");
    }

    void GBufferPass::Setup(ViewRenderContext &context)
    {
        // GBufferサイズを決定
        uint32_t width = ResolveGBufferWidth(context);
        uint32_t height = ResolveGBufferHeight(context);

        if (width == 0 || height == 0)
        {
            return;
        }

        // サイズ変更があればGBufferリソースを再作成
        if (width != m_CurrentWidth ||
            height != m_CurrentHeight ||
            m_bUsingRenderGraphResources ||
            !m_AlbedoTexture ||
            !m_NormalTexture ||
            !m_MaterialTexture ||
            !m_EmissiveTexture ||
            !m_DepthTexture ||
            !m_GBufferRenderPass ||
            !m_GBufferFramebuffer ||
            !m_GBufferPipeline)
        {
            CreateGBufferResources(width, height, context);
        }
    }

    void GBufferPass::Declare(RenderGraphBuilder &builder)
    {
        const ViewRenderContext *context = builder.GetContext();

        uint32_t width = m_Settings.Width;
        uint32_t height = m_Settings.Height;
        if (context)
        {
            width = ResolveGBufferWidth(*context);
            height = ResolveGBufferHeight(*context);
        }

        if (width == 0)
        {
            width = m_CurrentWidth > 0 ? m_CurrentWidth : 1;
        }

        if (height == 0)
        {
            height = m_CurrentHeight > 0 ? m_CurrentHeight : 1;
        }

        m_AlbedoHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::GBufferAlbedo,
            RGTextureDesc::RenderTarget(width, height, m_Settings.AlbedoFormat, "GBuffer_Albedo"),
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        m_NormalHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::GBufferNormal,
            RGTextureDesc::RenderTarget(width, height, m_Settings.NormalFormat, "GBuffer_Normal"),
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        m_MaterialHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::GBufferMaterial,
            RGTextureDesc::RenderTarget(width, height, m_Settings.MaterialFormat, "GBuffer_Material"),
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        m_EmissiveHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::GBufferEmissive,
            RGTextureDesc::RenderTarget(width, height, m_Settings.EmissiveFormat, "GBuffer_Emissive"),
            RGAttachmentKind::Color,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::RenderTarget,
            RHI::ResourceState::ShaderResource);

        m_DepthHandle = builder.WriteTextureAttachment(
            RenderGraphResourceNames::GBufferDepth,
            RGTextureDesc::DepthStencil(width, height, m_Settings.DepthFormat, "GBuffer_Depth"),
            RGAttachmentKind::DepthStencil,
            RHI::AttachmentLoadOp::Clear,
            RHI::AttachmentStoreOp::Store,
            RHI::ResourceState::DepthWrite,
            RHI::ResourceState::ShaderResource);

        builder.PreserveInsertionOrder();
    }

    void GBufferPass::Execute(RenderGraphResources &resources, ViewRenderContext &context)
    {
        if (!m_bInitialized)
        {
            if (!Initialize(context))
            {
                NORVES_LOG_ERROR("GBufferPass", "Failed to initialize native RenderGraph execution");
                return;
            }
        }

        RHI::TexturePtr albedo = resources.GetTexture(m_AlbedoHandle);
        RHI::TexturePtr normal = resources.GetTexture(m_NormalHandle);
        RHI::TexturePtr material = resources.GetTexture(m_MaterialHandle);
        RHI::TexturePtr emissive = resources.GetTexture(m_EmissiveHandle);
        RHI::TexturePtr depth = resources.GetTexture(m_DepthHandle);

        if (!albedo || !normal || !material || !emissive || !depth)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to resolve native GBuffer textures");
            return;
        }

        if (!PrepareGBufferAttachments(albedo->GetWidth(),
                                       albedo->GetHeight(),
                                       albedo,
                                       normal,
                                       material,
                                       emissive,
                                       depth,
                                       true))
        {
            return;
        }

        Execute(context);
    }

    void GBufferPass::Execute(ViewRenderContext &context)
    {
        if (!context.CommandList)
        {
            return;
        }

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !m_GBufferPipeline)
        {
            NORVES_LOG_WARNING("GBufferPass", "GBuffer resources not ready, skipping");
            return;
        }

        // Phase8: MegaGeometry がまだ named attachment へ未移行のため、
        // GBuffer の native 実行でも bridge 公開は限定的に維持する。
        if (context.SharedResources)
        {
            context.SharedResources->RegisterTexturePtr("GBuffer_Albedo", m_AlbedoTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Normal", m_NormalTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Material", m_MaterialTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Emissive", m_EmissiveTexture);
            context.SharedResources->RegisterTexturePtr("GBuffer_Depth", m_DepthTexture);
        }

        RHI::Viewport viewport = context.GetActiveLocalViewport();
        RHI::ScissorRect scissor = context.GetActiveLocalScissor();

        // ========================================
        // DrawCommand駆動の描画
        // ========================================
        const DrawCommandView activeOpaqueCommands = context.GetActiveOpaqueCommands();
        auto *materials = context.Resources.Materials;
        auto *textures = context.Resources.Textures;
        auto *meshes = context.Resources.Meshes;
        if (!m_SceneRenderer || !materials || !textures || !meshes)
        {
            TryEnqueueNativeClearPass(context, viewport, scissor, meshes);
            return;
        }

        // DrawCommand配列を取得（GameThreadでスナップショット済み）
        const DrawCommandView opaqueCommands = activeOpaqueCommands;
        if (opaqueCommands.empty())
        {
            TryEnqueueNativeClearPass(context, viewport, scissor, meshes);
            return;
        }

        if (!context.InstanceDataBuffer)
        {
            NORVES_LOG_WARNING("GBufferPass", "Instance data buffer is null, skipping GBuffer geometry");
            TryEnqueueNativeClearPass(context, viewport, scissor, meshes);
            return;
        }

        const uint64_t instanceDataSize64 = context.InstanceDataBuffer->GetSize();
        if (instanceDataSize64 == 0)
        {
            NORVES_LOG_WARNING("GBufferPass", "Instance data buffer is empty, skipping GBuffer geometry");
            TryEnqueueNativeClearPass(context, viewport, scissor, meshes);
            return;
        }

        const uint32_t instanceDataSize = instanceDataSize64 > 0xFFFFFFFFull
                                              ? 0xFFFFFFFFu
                                              : static_cast<uint32_t>(instanceDataSize64);

        // カメラ行列の構築
        using namespace NorvesLib::Math;

        CameraViewConstants cameraConstants;
        float cameraPos[4] = {0.0f, 1.5f, 4.0f, 1.0f};

        const CameraProxy *activeCamera = context.GetActiveCamera();
        if (activeCamera)
        {
            cameraConstants =
                CameraViewConstants::BuildForDevice(*activeCamera, context.GetActiveAspectRatio(), context.Device);
            cameraConstants.CopyCameraPosition(cameraPos);
        }

        // UBOデータ構造体（std140レイアウト）
        struct PerObjectUBO
        {
            float view[16];
            float projection[16];
            float cameraPosition[4];
            float emissiveColor[4]; // rgb=エミッシブカラー, a=エミッシブ強度
            float pomParams[4];     // x=heightScale, y=hasHeightMap(0 or 1), z=unused, w=unused
        };

        // ビュー・プロジェクション行列を事前変換
        float viewData[16];
        float projData[16];
        cameraConstants.CopyShaderView(viewData);
        cameraConstants.CopyShaderProjection(projData);

        // フレーム開始時にアロケータリセット
        m_UniformAllocator.Reset();

        auto gBufferCommands = MakeShared<Container::VariableArray<DrawCommand>>();

        for (const auto& cmd : opaqueCommands)
        {
            // UBOスロット確保
            auto allocation = m_UniformAllocator.Allocate();
            if (!allocation.UniformBuffer)
            {
                NORVES_LOG_WARNING("GBufferPass", "UBO allocation failed, skipping remaining objects");
                break;
            }

            // UBOデータ構築
            PerObjectUBO uboData;
            std::memcpy(uboData.view, viewData, sizeof(viewData));
            std::memcpy(uboData.projection, projData, sizeof(projData));
            std::memcpy(uboData.cameraPosition, cameraPos, sizeof(cameraPos));

            // マテリアルからテクスチャとエミッシブを取得
            TextureHandle matAlbedo;
            TextureHandle matNormal;
            TextureHandle matMetallic;
            TextureHandle matRoughness;
            TextureHandle matAO;
            TextureHandle matHeight;
            float matHeightScale = 0.05f;
            float matEmissiveR = 0.0f, matEmissiveG = 0.0f, matEmissiveB = 0.0f;
            float matEmissiveStrength = 0.0f;

            if (cmd.Draw.MaterialHandle.IsValid() && materials)
            {
                const auto *matData = materials->GetData(cmd.Draw.MaterialHandle);
                if (matData)
                {
                    matAlbedo = matData->AlbedoTexture;
                    matNormal = matData->NormalTexture;
                    matMetallic = matData->MetallicTexture;
                    matRoughness = matData->RoughnessTexture;
                    matAO = matData->AOTexture;
                    matHeight = matData->HeightTexture;
                    matHeightScale = matData->HeightScale;
                    matEmissiveR = matData->EmissiveColor[0];
                    matEmissiveG = matData->EmissiveColor[1];
                    matEmissiveB = matData->EmissiveColor[2];
                    matEmissiveStrength = matData->EmissiveStrength;
                }
            }

            // エミッシブカラー（rgb=色, a=強度）
            uboData.emissiveColor[0] = matEmissiveR;
            uboData.emissiveColor[1] = matEmissiveG;
            uboData.emissiveColor[2] = matEmissiveB;
            uboData.emissiveColor[3] = matEmissiveStrength;

            // POMパラメータ
            uboData.pomParams[0] = matHeightScale;
            uboData.pomParams[1] = matHeight.IsValid() ? 1.0f : 0.0f;
            uboData.pomParams[2] = 0.0f;
            uboData.pomParams[3] = 0.0f;

            // UBO更新
            allocation.UniformBuffer->Update(&uboData, sizeof(PerObjectUBO));

            // PBRテクスチャバインド（マテリアル経由、未設定ならデフォルトテクスチャ）
            auto ResolveTexture = [&](TextureHandle handle, const RHI::TexturePtr& defaultTex) -> RHI::TexturePtr
            {
                if (handle.IsValid() && textures)
                {
                    auto tex = textures->GetRHITexturePtr(handle);
                    if (tex)
                    {
                        return tex;
                    }
                }
                return defaultTex;
            };

            RHI::TexturePtr albedoTex = ResolveTexture(matAlbedo, m_DefaultWhiteTexture);
            RHI::TexturePtr normalTex = ResolveTexture(matNormal, m_DefaultFlatNormalTexture);
            RHI::TexturePtr metallicTex = ResolveTexture(matMetallic, m_DefaultBlackTexture);
            RHI::TexturePtr roughnessTex = ResolveTexture(matRoughness, m_DefaultMidGrayTexture);
            RHI::TexturePtr aoTex = ResolveTexture(matAO, m_DefaultWhiteTexture);
            RHI::TexturePtr heightTex = ResolveTexture(matHeight, m_DefaultBlackTexture);

            allocation.DescriptorSet->BindTexture(1, albedoTex);
            allocation.DescriptorSet->BindSampler(1, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(2, normalTex);
            allocation.DescriptorSet->BindSampler(2, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(3, metallicTex);
            allocation.DescriptorSet->BindSampler(3, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(4, roughnessTex);
            allocation.DescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(5, aoTex);
            allocation.DescriptorSet->BindSampler(5, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindTexture(6, heightTex);
            allocation.DescriptorSet->BindSampler(6, m_DefaultLinearSampler);
            allocation.DescriptorSet->BindStorageBuffer(7,
                                                        context.InstanceDataBuffer,
                                                        0,
                                                        instanceDataSize);
            allocation.DescriptorSet->Update();

            DrawCommand drawCommand = cmd;
            drawCommand.Pipeline = m_GBufferPipeline;
            drawCommand.DescriptorSet = allocation.DescriptorSet;
            drawCommand.DescriptorSetSlot = 0;
            gBufferCommands->push_back(drawCommand);
        }

        EnqueueGBufferGeometryPass(context, gBufferCommands, viewport, scissor, meshes);
    }

    void GBufferPass::EnqueueGBufferGeometryPass(
        ViewRenderContext &context,
        Container::TSharedPtr<Container::VariableArray<DrawCommand>> drawCommands,
        const RHI::Viewport &viewport,
        const RHI::ScissorRect &scissor,
        MeshResources *meshes) const
    {
        if (!drawCommands)
        {
            drawCommands = MakeShared<Container::VariableArray<DrawCommand>>();
        }

        context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass(m_GBufferRenderPass,
                                                                     m_GBufferFramebuffer,
                                                                     drawCommands,
                                                                     viewport,
                                                                     scissor,
                                                                     meshes));
    }

    bool GBufferPass::TryEnqueueNativeClearPass(ViewRenderContext &context,
                                                const RHI::Viewport &viewport,
                                                const RHI::ScissorRect &scissor,
                                                MeshResources *meshes) const
    {
        if (!m_bUsingRenderGraphResources)
        {
            return false;
        }

        EnqueueGBufferGeometryPass(context,
                                   MakeShared<Container::VariableArray<DrawCommand>>(),
                                   viewport,
                                   scissor,
                                   meshes);
        return true;
    }

    bool GBufferPass::CreateGBufferResources(uint32_t width, uint32_t height, ViewRenderContext &context)
    {
        (void)context;

        if (!m_Device)
        {
            return false;
        }

        m_CurrentWidth = width;
        m_CurrentHeight = height;

        // ========================================
        // GBufferテクスチャ作成
        // ========================================
        m_AlbedoTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.AlbedoFormat, "GBuffer_Albedo"));
        m_NormalTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.NormalFormat, "GBuffer_Normal"));
        m_MaterialTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.MaterialFormat, "GBuffer_Material"));
        m_EmissiveTexture = m_Device->CreateTexture(
            RHI::TextureDesc::RenderTarget(width, height, m_Settings.EmissiveFormat, "GBuffer_Emissive"));
        m_DepthTexture = m_Device->CreateTexture(
            RHI::TextureDesc::DepthStencil(width, height, m_Settings.DepthFormat, "GBuffer_Depth"));

        if (!m_AlbedoTexture || !m_NormalTexture || !m_MaterialTexture || !m_EmissiveTexture || !m_DepthTexture)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer textures");
            return false;
        }

        return PrepareGBufferAttachments(width,
                                         height,
                                         m_AlbedoTexture,
                                         m_NormalTexture,
                                         m_MaterialTexture,
                                         m_EmissiveTexture,
                                         m_DepthTexture,
                                         false);
    }

    uint32_t GBufferPass::ResolveGBufferWidth(const ViewRenderContext &context) const
    {
        return m_Settings.Width > 0 ? m_Settings.Width : context.GetActiveRenderWidth();
    }

    uint32_t GBufferPass::ResolveGBufferHeight(const ViewRenderContext &context) const
    {
        return m_Settings.Height > 0 ? m_Settings.Height : context.GetActiveRenderHeight();
    }

    bool GBufferPass::PrepareGBufferAttachments(uint32_t width,
                                                uint32_t height,
                                                const RHI::TexturePtr& albedo,
                                                const RHI::TexturePtr& normal,
                                                const RHI::TexturePtr& material,
                                                const RHI::TexturePtr& emissive,
                                                const RHI::TexturePtr& depth,
                                                bool bUseRenderGraphInitialStates)
    {
        if (!m_Device)
        {
            return false;
        }

        if (!albedo || !normal || !material || !emissive || !depth)
        {
            NORVES_LOG_ERROR("GBufferPass", "GBuffer attachment textures are incomplete");
            return false;
        }

        m_AlbedoTexture = albedo;
        m_NormalTexture = normal;
        m_MaterialTexture = material;
        m_EmissiveTexture = emissive;
        m_DepthTexture = depth;
        m_CurrentWidth = width;
        m_CurrentHeight = height;
        m_bUsingRenderGraphResources = bUseRenderGraphInitialStates;

        const RenderPassSignature signature = CreateGBufferRenderPassSignature(width,
                                                                               height,
                                                                               albedo,
                                                                               normal,
                                                                               material,
                                                                               emissive,
                                                                               depth,
                                                                               bUseRenderGraphInitialStates);

        if (!EnsureGBufferRenderPass(signature))
        {
            return false;
        }

        if (!EnsureGBufferFramebuffer(width, height, albedo, normal, material, emissive, depth))
        {
            return false;
        }

        return EnsureGBufferPipeline();
    }

    bool GBufferPass::AttachmentSignatureEquals(const AttachmentSignature& lhs,
                                                const AttachmentSignature& rhs) const
    {
        return lhs.Kind == rhs.Kind &&
               lhs.Format == rhs.Format &&
               lhs.LoadOp == rhs.LoadOp &&
               lhs.StoreOp == rhs.StoreOp &&
               lhs.InitialState == rhs.InitialState &&
               lhs.FinalState == rhs.FinalState &&
               lhs.Target == rhs.Target &&
               lhs.Width == rhs.Width &&
               lhs.Height == rhs.Height &&
               lhs.bDepthReadOnly == rhs.bDepthReadOnly;
    }

    bool GBufferPass::RenderPassSignatureEquals(const RenderPassSignature& lhs,
                                                const RenderPassSignature& rhs) const
    {
        return lhs.bValid == rhs.bValid &&
               AttachmentSignatureEquals(lhs.Albedo, rhs.Albedo) &&
               AttachmentSignatureEquals(lhs.Normal, rhs.Normal) &&
               AttachmentSignatureEquals(lhs.Material, rhs.Material) &&
               AttachmentSignatureEquals(lhs.Emissive, rhs.Emissive) &&
               AttachmentSignatureEquals(lhs.Depth, rhs.Depth);
    }

    GBufferPass::RenderPassSignature GBufferPass::CreateGBufferRenderPassSignature(
        uint32_t width,
        uint32_t height,
        const RHI::TexturePtr& albedo,
        const RHI::TexturePtr& normal,
        const RHI::TexturePtr& material,
        const RHI::TexturePtr& emissive,
        const RHI::TexturePtr& depth,
        bool bUseRenderGraphInitialStates) const
    {
        const RHI::ResourceState colorInitialState = bUseRenderGraphInitialStates
                                                         ? RHI::ResourceState::RenderTarget
                                                         : RHI::ResourceState::Undefined;
        const RHI::ResourceState depthInitialState = bUseRenderGraphInitialStates
                                                         ? RHI::ResourceState::DepthWrite
                                                         : RHI::ResourceState::Undefined;

        RenderPassSignature signature;
        signature.bValid = true;
        signature.Albedo = {RGAttachmentKind::Color,
                            albedo ? albedo->GetFormat() : m_Settings.AlbedoFormat,
                            RHI::AttachmentLoadOp::Clear,
                            RHI::AttachmentStoreOp::Store,
                            colorInitialState,
                            RHI::ResourceState::ShaderResource,
                            albedo.get(),
                            width,
                            height,
                            false};
        signature.Normal = {RGAttachmentKind::Color,
                            normal ? normal->GetFormat() : m_Settings.NormalFormat,
                            RHI::AttachmentLoadOp::Clear,
                            RHI::AttachmentStoreOp::Store,
                            colorInitialState,
                            RHI::ResourceState::ShaderResource,
                            normal.get(),
                            width,
                            height,
                            false};
        signature.Material = {RGAttachmentKind::Color,
                              material ? material->GetFormat() : m_Settings.MaterialFormat,
                              RHI::AttachmentLoadOp::Clear,
                              RHI::AttachmentStoreOp::Store,
                              colorInitialState,
                              RHI::ResourceState::ShaderResource,
                              material.get(),
                              width,
                              height,
                              false};
        signature.Emissive = {RGAttachmentKind::Color,
                              emissive ? emissive->GetFormat() : m_Settings.EmissiveFormat,
                              RHI::AttachmentLoadOp::Clear,
                              RHI::AttachmentStoreOp::Store,
                              colorInitialState,
                              RHI::ResourceState::ShaderResource,
                              emissive.get(),
                              width,
                              height,
                              false};
        signature.Depth = {RGAttachmentKind::DepthStencil,
                           depth ? depth->GetFormat() : m_Settings.DepthFormat,
                           RHI::AttachmentLoadOp::Clear,
                           RHI::AttachmentStoreOp::Store,
                           depthInitialState,
                           RHI::ResourceState::ShaderResource,
                           depth.get(),
                           width,
                           height,
                           false};
        return signature;
    }

    bool GBufferPass::EnsureGBufferRenderPass(const RenderPassSignature &signature)
    {
        if (!m_Device)
        {
            return false;
        }

        if (m_GBufferRenderPass &&
            RenderPassSignatureEquals(m_RenderPassSignature, signature))
        {
            return true;
        }

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_GBufferPipeline.reset();
        m_FramebufferAlbedoTexture = nullptr;
        m_FramebufferNormalTexture = nullptr;
        m_FramebufferMaterialTexture = nullptr;
        m_FramebufferEmissiveTexture = nullptr;
        m_FramebufferDepthTexture = nullptr;
        m_FramebufferWidth = 0;
        m_FramebufferHeight = 0;
        m_RenderPassSignature = {};

        // ========================================
        // MRT対応レンダーパス作成（4カラー + 1デプス）
        // ========================================
        RHI::RenderPassDesc rpDesc;

        // Albedo アタッチメント
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = signature.Albedo.Format;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = true;
        albedoAttach.clearColor[0] = 0.0f;
        albedoAttach.clearColor[1] = 0.0f;
        albedoAttach.clearColor[2] = 0.0f;
        albedoAttach.clearColor[3] = 0.0f;
        albedoAttach.loadOp = signature.Albedo.LoadOp;
        albedoAttach.storeOp = signature.Albedo.StoreOp;
        albedoAttach.initialState = signature.Albedo.InitialState;
        albedoAttach.finalState = signature.Albedo.FinalState;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal アタッチメント
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = signature.Normal.Format;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = true;
        normalAttach.clearColor[0] = 0.0f;
        normalAttach.clearColor[1] = 0.0f;
        normalAttach.clearColor[2] = 0.0f;
        normalAttach.clearColor[3] = 0.0f;
        normalAttach.loadOp = signature.Normal.LoadOp;
        normalAttach.storeOp = signature.Normal.StoreOp;
        normalAttach.initialState = signature.Normal.InitialState;
        normalAttach.finalState = signature.Normal.FinalState;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material アタッチメント
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = signature.Material.Format;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = true;
        materialAttach.clearColor[0] = 0.0f;
        materialAttach.clearColor[1] = 0.0f;
        materialAttach.clearColor[2] = 0.0f;
        materialAttach.clearColor[3] = 0.0f;
        materialAttach.loadOp = signature.Material.LoadOp;
        materialAttach.storeOp = signature.Material.StoreOp;
        materialAttach.initialState = signature.Material.InitialState;
        materialAttach.finalState = signature.Material.FinalState;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Emissive アタッチメント（HDR自発光カラー）
        RHI::AttachmentDesc emissiveAttach;
        emissiveAttach.format = signature.Emissive.Format;
        emissiveAttach.isDepthStencil = false;
        emissiveAttach.clear = true;
        emissiveAttach.clearColor[0] = 0.0f;
        emissiveAttach.clearColor[1] = 0.0f;
        emissiveAttach.clearColor[2] = 0.0f;
        emissiveAttach.clearColor[3] = 0.0f;
        emissiveAttach.loadOp = signature.Emissive.LoadOp;
        emissiveAttach.storeOp = signature.Emissive.StoreOp;
        emissiveAttach.initialState = signature.Emissive.InitialState;
        emissiveAttach.finalState = signature.Emissive.FinalState;
        rpDesc.colorAttachments.push_back(emissiveAttach);

        // Depth アタッチメント
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = signature.Depth.Format;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = true;
        rpDesc.depthStencilAttachment.clearDepth = 1.0f;
        rpDesc.depthStencilAttachment.clearStencil = 0;
        rpDesc.depthStencilAttachment.loadOp = signature.Depth.LoadOp;
        rpDesc.depthStencilAttachment.storeOp = signature.Depth.StoreOp;
        rpDesc.depthStencilAttachment.initialState = signature.Depth.InitialState;
        rpDesc.depthStencilAttachment.finalState = signature.Depth.FinalState;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer render pass");
            return false;
        }

        m_bRenderPassUsesRenderGraphInitialStates =
            signature.Albedo.InitialState == RHI::ResourceState::RenderTarget &&
            signature.Depth.InitialState == RHI::ResourceState::DepthWrite;
        m_RenderPassSignature = signature;
        return true;
    }

    bool GBufferPass::EnsureGBufferFramebuffer(uint32_t width,
                                               uint32_t height,
                                               const RHI::TexturePtr& albedo,
                                               const RHI::TexturePtr& normal,
                                               const RHI::TexturePtr& material,
                                               const RHI::TexturePtr& emissive,
                                               const RHI::TexturePtr& depth)
    {
        if (m_GBufferFramebuffer &&
            m_FramebufferWidth == width &&
            m_FramebufferHeight == height &&
            m_FramebufferAlbedoTexture == albedo.get() &&
            m_FramebufferNormalTexture == normal.get() &&
            m_FramebufferMaterialTexture == material.get() &&
            m_FramebufferEmissiveTexture == emissive.get() &&
            m_FramebufferDepthTexture == depth.get())
        {
            return true;
        }

        // ========================================
        // フレームバッファ作成
        // ========================================
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.colorTargets.push_back(m_EmissiveTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = width;
        fbDesc.height = height;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer framebuffer");
            return false;
        }

        m_FramebufferAlbedoTexture = albedo.get();
        m_FramebufferNormalTexture = normal.get();
        m_FramebufferMaterialTexture = material.get();
        m_FramebufferEmissiveTexture = emissive.get();
        m_FramebufferDepthTexture = depth.get();
        m_FramebufferWidth = width;
        m_FramebufferHeight = height;
        return true;
    }

    bool GBufferPass::EnsureGBufferPipeline()
    {
        if (m_GBufferPipeline)
        {
            return true;
        }

        if (!m_Device || !m_GBufferRenderPass || !m_GBufferVertexShader || !m_GBufferFragmentShader)
        {
            return false;
        }

        // ========================================
        // GBuffer用パイプライン作成（初回のみ、またはレンダーパス変更時）
        // ========================================
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_GBufferVertexShader;
        pipelineDesc.pixelShader = m_GBufferFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Position + Normal = Mesh3DVertexと同じ）
        RHI::VertexBindingDesc vertexBinding;
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(Mesh3DVertex);
        vertexBinding.inputRate = RHI::VertexInputRate::Vertex;
        pipelineDesc.vertexBindings.push_back(vertexBinding);

        // Position: location=0, vec3
        RHI::VertexAttributeDesc posAttr;
        posAttr.location = 0;
        posAttr.binding = 0;
        posAttr.format = RHI::Format::R32G32B32_FLOAT;
        posAttr.offset = 0;
        pipelineDesc.vertexAttributes.push_back(posAttr);

        // Normal: location=1, vec3
        RHI::VertexAttributeDesc normalAttr;
        normalAttr.location = 1;
        normalAttr.binding = 0;
        normalAttr.format = RHI::Format::R32G32B32_FLOAT;
        normalAttr.offset = sizeof(float) * 3;
        pipelineDesc.vertexAttributes.push_back(normalAttr);

        // TexCoord: location=2, vec2
        RHI::VertexAttributeDesc texCoordAttr;
        texCoordAttr.location = 2;
        texCoordAttr.binding = 0;
        texCoordAttr.format = RHI::Format::R32G32_FLOAT;
        texCoordAttr.offset = sizeof(float) * 6;
        pipelineDesc.vertexAttributes.push_back(texCoordAttr);

        // ラスタライザ
        pipelineDesc.rasterState.polygonMode = RHI::PolygonMode::Fill;
        pipelineDesc.rasterState.cullMode = RHI::CullMode::Back;
        pipelineDesc.rasterState.frontFace = RHI::FrontFace::Clockwise;
        pipelineDesc.rasterState.lineWidth = 1.0f;

        // デプステスト有効
        pipelineDesc.depthStencilState.depthTestEnable = true;
        pipelineDesc.depthStencilState.depthWriteEnable = true;
        pipelineDesc.depthStencilState.depthCompareOp = RHI::CompareOp::Less;

        // MRT用ブレンドステート（4カラーアタッチメント分）
        for (int i = 0; i < 4; ++i)
        {
            RHI::BlendAttachmentDesc blendAttachment;
            blendAttachment.blendEnable = false;
            blendAttachment.colorWriteMask = RHI::ColorWriteMask::All;
            pipelineDesc.blendState.attachments.push_back(blendAttachment);
        }

        pipelineDesc.renderPass = m_GBufferRenderPass;

        // ディスクリプタセットレイアウト（set=0: MVP UBO + PBRテクスチャ x6）
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex | RHI::ShaderStage::Pixel;
        dsDesc.bindings.push_back(uboBinding);

        // binding 1-6: albedo, normal, metallic, roughness, ao, height
        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(texBinding);
        }

        RHI::DescriptorBinding instanceBinding;
        instanceBinding.binding = 7;
        instanceBinding.type = RHI::ResourceBindType::StructuredBuffer;
        instanceBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(instanceBinding);

        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_GBufferPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_GBufferPipeline)
        {
            NORVES_LOG_ERROR("GBufferPass", "Failed to create GBuffer pipeline");
            return false;
        }

        NORVES_LOG_INFO("GBufferPass", "GBuffer resources created (%ux%u)", m_CurrentWidth, m_CurrentHeight);
        return true;
    }

} // namespace NorvesLib::Core::Rendering

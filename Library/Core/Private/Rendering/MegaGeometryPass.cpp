#include "Rendering/MegaGeometryPass.h"
#include "Rendering/FrameCommand.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/RenderResourceManager.h"
#include "Rendering/SceneView.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SharedResourceRegistry.h"
#include "Rendering/ProceduralMeshGenerator.h"
#include "Rendering/SceneProxy.h"
#include "Math/VectorUtils.h"
#include "Math/MatrixUtils.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/ISampler.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/DeviceCapabilities.h"
#include "Text/IdentityPool.h"
#include "Logging/LogMacros.h"
#include <cstring>
#include <cmath>

namespace NorvesLib::Core::Rendering
{
    using namespace Container;

    // ========================================
    // コンストラクタ / デストラクタ
    // ========================================

    MegaGeometryPass::MegaGeometryPass(const MegaGeometryPassSettings &settings)
        : m_Settings(settings)
    {
    }

    MegaGeometryPass::~MegaGeometryPass()
    {
        if (IsInitialized())
        {
            Shutdown();
        }
    }

    // ========================================
    // Initialize
    // ========================================

    bool MegaGeometryPass::Initialize(ViewRenderContext &context)
    {
        m_Device = context.Device;
        if (!m_Device)
        {
            return false;
        }

        // カリングコンピュートシェーダーの読み込み
        if (context.ShaderMgr)
        {
            m_CullShader = context.ShaderMgr->LoadShader(
                "cluster_cull.comp", RHI::ShaderStage::Compute);
        }
        if (!m_CullShader)
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "カリングシェーダーの読み込みに失敗。パスは無効化されます");
            m_bInitialized = true;
            return true;
        }

        // カリング用GPUリソース作成
        if (!CreateCullResources(m_Device))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリング用GPUリソースの作成に失敗");
            return false;
        }

        // カリングコンピュートパイプライン作成
        RHI::ComputePipelineDesc cullPipelineDesc;
        cullPipelineDesc.computeShader = m_CullShader;
        {
            RHI::DescriptorSetDesc cullDsDesc;

            RHI::DescriptorBinding uniformBinding;
            uniformBinding.binding = 0;
            uniformBinding.type = RHI::ResourceBindType::ConstantBuffer;
            uniformBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(uniformBinding);

            RHI::DescriptorBinding clusterBinding;
            clusterBinding.binding = 1;
            clusterBinding.type = RHI::ResourceBindType::RWBuffer;
            clusterBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(clusterBinding);

            RHI::DescriptorBinding indirectBinding;
            indirectBinding.binding = 2;
            indirectBinding.type = RHI::ResourceBindType::RWBuffer;
            indirectBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(indirectBinding);

            RHI::DescriptorBinding drawCountBinding;
            drawCountBinding.binding = 3;
            drawCountBinding.type = RHI::ResourceBindType::RWBuffer;
            drawCountBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(drawCountBinding);

            RHI::DescriptorBinding hiZBinding;
            hiZBinding.binding = 4;
            hiZBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            hiZBinding.stages = RHI::ShaderStage::Compute;
            cullDsDesc.bindings.push_back(hiZBinding);

            cullPipelineDesc.descriptorSetLayouts.push_back(cullDsDesc);
        }
        m_CullPipeline = m_Device->CreateComputePipeline(cullPipelineDesc);
        if (!m_CullPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "カリングパイプラインの作成に失敗");
            return false;
        }

        // GBuffer描画用シェーダーの読み込み（通常のGBufferシェーダーを再利用）
        if (context.ShaderMgr)
        {
            m_DrawVertexShader = context.ShaderMgr->LoadShader(
                "gbuffer.vert", RHI::ShaderStage::Vertex);
            m_DrawFragmentShader = context.ShaderMgr->LoadShader(
                "gbuffer.frag", RHI::ShaderStage::Pixel);
        }
        if (!m_DrawVertexShader || !m_DrawFragmentShader)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer描画シェーダーの読み込みに失敗");
            return false;
        }

        // デフォルトPBRテクスチャの作成
        auto createDefault1x1 = [this](const char *debugName, uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> RHI::TexturePtr
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

        m_DefaultWhiteTexture = createDefault1x1("MegaGeometry_DefaultWhite", 255, 255, 255, 255);
        m_DefaultFlatNormalTexture = createDefault1x1("MegaGeometry_DefaultFlatNormal", 128, 128, 255, 255);
        m_DefaultBlackTexture = createDefault1x1("MegaGeometry_DefaultBlack", 0, 0, 0, 255);

        if (!m_DefaultWhiteTexture || !m_DefaultFlatNormalTexture || !m_DefaultBlackTexture)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトPBRテクスチャの作成に失敗");
            return false;
        }

        // デフォルトLinearサンプラーの作成
        RHI::SamplerDesc sampDesc;
        sampDesc.filterMin = RHI::FilterMode::Linear;
        sampDesc.filterMag = RHI::FilterMode::Linear;
        sampDesc.filterMip = RHI::FilterMode::Linear;
        sampDesc.addressU = RHI::TextureAddressMode::Wrap;
        sampDesc.addressV = RHI::TextureAddressMode::Wrap;
        sampDesc.addressW = RHI::TextureAddressMode::Wrap;
        m_DefaultLinearSampler = m_Device->CreateSampler(sampDesc);
        if (!m_DefaultLinearSampler)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "デフォルトサンプラーの作成に失敗");
            return false;
        }

        // Hi-Z 深度ピラミッド用リソース作成
        if (context.ShaderMgr)
        {
            m_HiZShader = context.ShaderMgr->LoadShader(
                "hiz_generate.comp", RHI::ShaderStage::Compute);
        }
        if (m_HiZShader)
        {
            RHI::ComputePipelineDesc hiZPipelineDesc;
            hiZPipelineDesc.computeShader = m_HiZShader;
            {
                RHI::DescriptorSetDesc hiZDsDesc;

                RHI::DescriptorBinding srcBinding;
                srcBinding.binding = 0;
                srcBinding.type = RHI::ResourceBindType::CombinedImageSampler;
                srcBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(srcBinding);

                RHI::DescriptorBinding dstBinding;
                dstBinding.binding = 1;
                dstBinding.type = RHI::ResourceBindType::RWTexture;
                dstBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(dstBinding);

                RHI::DescriptorBinding paramBinding;
                paramBinding.binding = 2;
                paramBinding.type = RHI::ResourceBindType::ConstantBuffer;
                paramBinding.stages = RHI::ShaderStage::Compute;
                hiZDsDesc.bindings.push_back(paramBinding);

                hiZPipelineDesc.descriptorSetLayouts.push_back(hiZDsDesc);
            }
            m_HiZPipeline = m_Device->CreateComputePipeline(hiZPipelineDesc);

            // Hi-Z用Nearestサンプラー（Clamp）
            RHI::SamplerDesc hiZSampDesc;
            hiZSampDesc.filterMin = RHI::FilterMode::Point;
            hiZSampDesc.filterMag = RHI::FilterMode::Point;
            hiZSampDesc.filterMip = RHI::FilterMode::Point;
            hiZSampDesc.addressU = RHI::TextureAddressMode::Clamp;
            hiZSampDesc.addressV = RHI::TextureAddressMode::Clamp;
            hiZSampDesc.addressW = RHI::TextureAddressMode::Clamp;
            m_HiZNearestSampler = m_Device->CreateSampler(hiZSampDesc);

            // Hi-ZパラメータUBO (ivec2 destSize = 8 bytes, pad to 16)
            RHI::BufferDesc hiZUboDesc(
                16,
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_HiZParamsUBO");
            m_HiZParamsBuffer = m_Device->CreateBuffer(hiZUboDesc);

            if (!m_HiZPipeline || !m_HiZNearestSampler || !m_HiZParamsBuffer)
            {
                NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zリソースの作成に失敗。オクルージョンカリング無効");
                m_HiZPipeline.reset();
                m_HiZShader.reset();
                m_HiZNearestSampler.reset();
                m_HiZParamsBuffer.reset();
            }
            else
            {
                NORVES_LOG_INFO("MegaGeometryPass", "Hi-Zオクルージョンカリング有効");
            }
        }
        else
        {
            NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zシェーダーの読み込みに失敗。オクルージョンカリング無効");
        }

        m_bInitialized = true;
        NORVES_LOG_INFO("MegaGeometryPass", "初期化完了 (MaxDrawCount: %u)", m_Settings.MaxDrawCount);
        return true;
    }

    // ========================================
    // Shutdown
    // ========================================

    void MegaGeometryPass::Shutdown()
    {
        m_CullPipeline.reset();
        m_CullShader.reset();
        m_IndirectDrawBuffer.reset();
        m_DrawCountBuffer.reset();
        m_CullUniformBuffers.clear();
        m_CullDescriptorSets.clear();

        m_DrawPipeline.reset();
        m_DrawVertexShader.reset();
        m_DrawFragmentShader.reset();
        m_DrawUniformBuffers.clear();
        m_DrawDescriptorSets.clear();

        m_GBufferRenderPass.reset();
        m_GBufferFramebuffer.reset();
        m_AlbedoTexture.reset();
        m_NormalTexture.reset();
        m_MaterialTexture.reset();
        m_EmissiveTexture.reset();
        m_DepthTexture.reset();

        m_DefaultWhiteTexture.reset();
        m_DefaultFlatNormalTexture.reset();
        m_DefaultBlackTexture.reset();
        m_DefaultLinearSampler.reset();

        m_HiZTexture.reset();
        m_HiZShader.reset();
        m_HiZPipeline.reset();
        m_HiZDescriptorSet.reset();
        m_HiZParamsBuffer.reset();
        m_HiZNearestSampler.reset();
        m_HiZMipCount = 0;

        m_Instances.clear();

        m_bInitialized = false;
    }

    // ========================================
    // Setup
    // ========================================

    void MegaGeometryPass::Setup(ViewRenderContext &context)
    {
        m_Instances.clear();

        if (context.SnapshotMegaGeometryProxies)
        {
            const auto &megaGeometryProxies = *context.SnapshotMegaGeometryProxies;
            m_Instances.reserve(megaGeometryProxies.size());

            for (const auto &proxy : megaGeometryProxies)
            {
                if (!proxy.IsValid())
                {
                    continue;
                }

                MegaMeshInstance instance;
                instance.Handle = proxy.MegaMeshHandle;
                std::memcpy(instance.WorldMatrix, &proxy.WorldTransform, sizeof(float) * 16);
                m_Instances.push_back(instance);
            }
        }

        // MegaMeshインスタンスが登録されていなければスキップ
        if (m_Instances.empty() || !m_CullPipeline)
        {
            return;
        }

        // GBufferテクスチャをSharedResourcesから取得
        if (context.SharedResources)
        {
            m_AlbedoTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Albedo"));
            m_NormalTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Normal"));
            m_MaterialTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Material"));
            m_EmissiveTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Emissive"));
            m_DepthTexture = context.SharedResources->GetTexturePtr(Identity("GBuffer_Depth"));
        }

        // GBufferテクスチャが利用可能か確認
        if (!m_AlbedoTexture || !m_DepthTexture)
        {
            return;
        }

        // 画面サイズ変更に対応
        uint32_t width = context.RenderWidth;
        uint32_t height = context.RenderHeight;

        if (width != m_CurrentWidth || height != m_CurrentHeight || !m_GBufferRenderPass)
        {
            m_CurrentWidth = width;
            m_CurrentHeight = height;

            // GBuffer互換レンダーパス作成（Load既存内容）
            if (!CreateDrawPipeline(context))
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "描画パイプラインの作成に失敗");
                return;
            }

            // Hi-Z深度ピラミッドの作成・再作成
            if (m_HiZPipeline)
            {
                if (!CreateHiZResources(context))
                {
                    NORVES_LOG_WARNING("MegaGeometryPass", "Hi-Zテクスチャの作成に失敗。オクルージョンカリング無効");
                }
            }
        }
    }

    // ========================================
    // Execute
    // ========================================

    void MegaGeometryPass::Execute(ViewRenderContext &context)
    {
        if (m_Instances.empty() || !m_CullPipeline || !context.ResourceManager)
        {
            return;
        }

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !m_DrawPipeline)
        {
            return;
        }

        if (!context.MainCamera)
        {
            return;
        }

        context.EnqueueMegaGeometryPass(this);
    }

    void MegaGeometryPass::RecordFrameCommand(const MegaGeometryPassCommand &command, RHI::ICommandList *commandList)
    {
        if (m_Instances.empty() || !m_CullPipeline || !commandList || !command.ResourceManager || !command.bHasMainCamera)
        {
            return;
        }

        if (!m_GBufferRenderPass || !m_GBufferFramebuffer || !m_DrawPipeline)
        {
            return;
        }

        if (!EnsurePerInstanceBindings(static_cast<uint32_t>(m_Instances.size())))
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Failed to prepare per-instance bindings");
            return;
        }

        auto *cmdList = commandList;

        // ========================================
        // Hi-Z 深度ピラミッド生成（オクルージョンカリング用）
        // ========================================
        // 現状のMegaGeometryは、クラスタ球ベースのHi-Z判定が角度・距離依存で過剰カリングを起こし、
        // モデル全体の消失や穴あきに繋がるため一旦無効化する。
        // 専用の深度プレパスや、より保守的な遮蔽判定が整うまではFrustum/Backfaceのみで描画安定性を優先する。

        // ========================================
        // 各MegaMeshインスタンスに対してカリング + IndirectDraw
        // ========================================
        for (size_t instanceIndex = 0; instanceIndex < m_Instances.size(); ++instanceIndex)
        {
            const auto &instance = m_Instances[instanceIndex];
            const auto *gpuData = command.ResourceManager->GetMegaMeshGPUData(instance.Handle);
            if (!gpuData || gpuData->ClusterCount == 0)
            {
                continue;
            }

            auto cullUniformBuffer = m_CullUniformBuffers[instanceIndex];
            auto cullDescriptorSet = m_CullDescriptorSets[instanceIndex];
            auto drawUniformBuffer = m_DrawUniformBuffers[instanceIndex];
            auto drawDescriptorSet = m_DrawDescriptorSets[instanceIndex];

            if (!cullUniformBuffer || !cullDescriptorSet || !drawUniformBuffer || !drawDescriptorSet)
            {
                NORVES_LOG_ERROR("MegaGeometryPass", "Invalid per-instance MegaGeometry binding at slot %zu", instanceIndex);
                return;
            }

            // ----------------------------------------
            // 1. DrawCountバッファをゼロクリア
            // ----------------------------------------
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::Common,
                                   RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_DrawCountBuffer, 0, sizeof(uint32_t), 0);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::CopyDest,
                                   RHI::ResourceState::UnorderedAccess);

            // IndirectDrawバッファもゼロクリアしてからUAV状態にする
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::CopyDest);
            cmdList->FillBuffer(m_IndirectDrawBuffer,
                                0,
                                static_cast<uint64_t>(m_Settings.MaxDrawCount) *
                                    sizeof(MegaGeometry::DrawIndexedIndirectCommand),
                                0);
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::CopyDest,
                                   RHI::ResourceState::UnorderedAccess);

            // ----------------------------------------
            // 2. カリングユニフォーム更新
            // ----------------------------------------
            CullUniformData uniformData{};

            // CameraProxyからView/Projection行列を構築
            using namespace NorvesLib::Math;

            const auto &cam = command.MainCamera;
            Vector3 camPos(cam.PositionX, cam.PositionY, cam.PositionZ);
            Vector3 forward(cam.ForwardX, cam.ForwardY, cam.ForwardZ);
            Vector3 lookAt = camPos + forward;
            Vector3 upDir(cam.UpX, cam.UpY, cam.UpZ);

            Matrix4x4 viewMat = MatrixUtils::CreateLookAt(camPos, lookAt, upDir);

            float aspectRatio = static_cast<float>(m_CurrentWidth) / static_cast<float>(m_CurrentHeight);
            float fovRadians = cam.FieldOfView * (3.14159265f / 180.0f);
            Matrix4x4 projMat = MatrixUtils::CreatePerspectiveFieldOfView(
                fovRadians, aspectRatio, cam.NearPlane, cam.FarPlane);
            projMat = m_Device->AdjustProjectionForClipSpace(projMat);

            MatrixUtils::TransposeToShaderData(viewMat, uniformData.ViewMatrix);
            MatrixUtils::TransposeToShaderData(projMat, uniformData.ProjectionMatrix);

            uniformData.CameraPosition[0] = cam.PositionX;
            uniformData.CameraPosition[1] = cam.PositionY;
            uniformData.CameraPosition[2] = cam.PositionZ;
            uniformData.CameraPosition[3] = 0.0f;

            // ViewProjection行列を計算して視錐台平面を抽出
            Matrix4x4 vpMat = projMat * viewMat;
            float viewProj[16];
            std::memcpy(viewProj, &vpMat, sizeof(float) * 16);
            ExtractFrustumPlanes(viewProj, uniformData.FrustumPlanes);

            uniformData.TotalClusterCount = gpuData->ClusterCount;
            uniformData.MaxDrawCount = m_Settings.MaxDrawCount;
            uniformData.LODBias = m_Settings.LODBias;
            uniformData.ScreenHeight = static_cast<float>(m_CurrentHeight);

            // projectionFactor = screenHeight / (2 * tan(fov/2))
            float halfFovTan = std::tan(fovRadians * 0.5f);
            uniformData.ProjectionFactor = (halfFovTan > 1e-6f)
                                               ? static_cast<float>(m_CurrentHeight) / (2.0f * halfFovTan)
                                               : 1.0f;

            // Hi-Zオクルージョンカリングは現状のクラスタ球近似が攻めすぎているため無効化
            uniformData.HiZWidth = 0;
            uniformData.HiZHeight = 0;
            uniformData.HiZMipCount = 0;
            uniformData.bHiZEnabled = 0;

            std::memcpy(uniformData.WorldMatrix, instance.WorldMatrix, sizeof(float) * 16);
            cullUniformBuffer->Update(&uniformData, sizeof(CullUniformData));

            // ----------------------------------------
            // 3. カリングディスクリプタセット更新
            // ----------------------------------------
            cullDescriptorSet->BindConstantBuffer(0, cullUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(CullUniformData)));
            cullDescriptorSet->BindStorageBuffer(1, gpuData->ClusterBuffer, 0,
                                                 static_cast<uint32_t>(gpuData->ClusterCount * sizeof(MegaGeometry::GPUClusterData)));
            cullDescriptorSet->BindStorageBuffer(2, m_IndirectDrawBuffer, 0,
                                                 static_cast<uint32_t>(m_Settings.MaxDrawCount * sizeof(MegaGeometry::DrawIndexedIndirectCommand)));
            cullDescriptorSet->BindStorageBuffer(3, m_DrawCountBuffer, 0,
                                                 sizeof(uint32_t));

            // Hi-Zテクスチャバインド（無い場合はデフォルトテクスチャでフォールバック）
            if (m_HiZTexture)
            {
                cullDescriptorSet->BindTexture(4, m_HiZTexture);
                cullDescriptorSet->BindSampler(4, m_HiZNearestSampler);
            }
            else
            {
                cullDescriptorSet->BindTexture(4, m_DefaultBlackTexture);
                cullDescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            }

            cullDescriptorSet->Update();

            // ----------------------------------------
            // 4. カリングコンピュートディスパッチ
            // ----------------------------------------
            cmdList->SetPipeline(m_CullPipeline);
            cmdList->SetDescriptorSet(cullDescriptorSet, 0);

            uint32_t groupCount = (gpuData->ClusterCount + 63) / 64;
            cmdList->Dispatch(groupCount, 1, 1);

            // ----------------------------------------
            // 5. バリア: Compute UAV → IndirectArgument
            // ----------------------------------------
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::UnorderedAccess,
                                   RHI::ResourceState::IndirectArgument);

            // ----------------------------------------
            // 6. GBuffer描画（IndirectDraw）
            // ----------------------------------------

            // PerObject UBO更新（ワールド変換行列）
            struct PerObjectUBO
            {
                float World[16];
                float View[16];
                float Projection[16];
                float CameraPosition[4];
                float ObjectColor[4];
                float EmissiveColor[4];
                float PomParams[4];
            };

            PerObjectUBO perObject{};
            std::memcpy(perObject.World, instance.WorldMatrix, sizeof(float) * 16);
            MatrixUtils::TransposeToShaderData(viewMat, perObject.View);
            MatrixUtils::TransposeToShaderData(projMat, perObject.Projection);
            perObject.CameraPosition[0] = cam.PositionX;
            perObject.CameraPosition[1] = cam.PositionY;
            perObject.CameraPosition[2] = cam.PositionZ;
            perObject.CameraPosition[3] = 1.0f;

            // マテリアル値を設定
            const auto &mat = gpuData->Material;
            perObject.ObjectColor[0] = mat.BaseColor[0];
            perObject.ObjectColor[1] = mat.BaseColor[1];
            perObject.ObjectColor[2] = mat.BaseColor[2];
            perObject.ObjectColor[3] = mat.BaseColor[3];
            perObject.EmissiveColor[0] = mat.EmissiveColor[0];
            perObject.EmissiveColor[1] = mat.EmissiveColor[1];
            perObject.EmissiveColor[2] = mat.EmissiveColor[2];
            perObject.EmissiveColor[3] = mat.EmissiveColor[3];
            perObject.PomParams[0] = mat.HeightScale;
            perObject.PomParams[1] = mat.bHasHeightMap ? 1.0f : 0.0f;
            perObject.PomParams[2] = 0.0f;
            perObject.PomParams[3] = 0.0f;

            drawUniformBuffer->Update(&perObject, sizeof(PerObjectUBO));

            // GBufferレンダーパス開始
            cmdList->BeginRenderPass(m_GBufferRenderPass, m_GBufferFramebuffer);

            // ビューポート・シザー設定
            RHI::Viewport viewport;
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(m_CurrentWidth);
            viewport.height = static_cast<float>(m_CurrentHeight);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            cmdList->SetViewport(viewport);

            RHI::ScissorRect scissor;
            scissor.left = 0;
            scissor.top = 0;
            scissor.right = static_cast<int32_t>(m_CurrentWidth);
            scissor.bottom = static_cast<int32_t>(m_CurrentHeight);
            cmdList->SetScissor(scissor);

            // パイプラインとデスクリプタ設定
            cmdList->SetPipeline(m_DrawPipeline);

            drawDescriptorSet->BindConstantBuffer(0, drawUniformBuffer, 0,
                                                  static_cast<uint32_t>(sizeof(PerObjectUBO)));

            // PBRテクスチャバインド（マテリアルテクスチャまたはデフォルトにフォールバック）
            auto albedo = mat.AlbedoTexture ? mat.AlbedoTexture : m_DefaultWhiteTexture;
            auto normal = mat.NormalTexture ? mat.NormalTexture : m_DefaultFlatNormalTexture;
            auto metallic = mat.MetallicTexture ? mat.MetallicTexture : m_DefaultBlackTexture;
            auto roughness = mat.RoughnessTexture ? mat.RoughnessTexture : m_DefaultWhiteTexture;
            auto ao = mat.AOTexture ? mat.AOTexture : m_DefaultWhiteTexture;
            auto height = mat.HeightTexture ? mat.HeightTexture : m_DefaultBlackTexture;

            drawDescriptorSet->BindTexture(1, albedo);
            drawDescriptorSet->BindSampler(1, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(2, normal);
            drawDescriptorSet->BindSampler(2, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(3, metallic);
            drawDescriptorSet->BindSampler(3, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(4, roughness);
            drawDescriptorSet->BindSampler(4, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(5, ao);
            drawDescriptorSet->BindSampler(5, m_DefaultLinearSampler);
            drawDescriptorSet->BindTexture(6, height);
            drawDescriptorSet->BindSampler(6, m_DefaultLinearSampler);

            drawDescriptorSet->Update();
            cmdList->SetDescriptorSet(drawDescriptorSet, 0);

            // 頂点/インデックスバッファ設定
            cmdList->SetVertexBuffer(gpuData->VertexBuffer, 0, 0);
            cmdList->SetIndexBuffer(gpuData->IndexBuffer, 0);

            // IndirectDraw発行
            // DrawIndirectCount対応の場合はGPU側カウントを参照し、
            // 実際に可視なクラスタ数だけドローコールを発行する。
            // 非対応の場合はMaxDrawCountをそのまま使用（instanceCount=0で空振り）。
            const auto &caps = m_Device->GetCapabilities();
            if (caps.bDrawIndirectCount)
            {
                cmdList->DrawIndexedIndirectCount(
                    m_IndirectDrawBuffer, 0,
                    m_DrawCountBuffer, 0,
                    m_Settings.MaxDrawCount,
                    sizeof(MegaGeometry::DrawIndexedIndirectCommand));
            }
            else
            {
                cmdList->DrawIndexedIndirect(
                    m_IndirectDrawBuffer, 0,
                    m_Settings.MaxDrawCount,
                    sizeof(MegaGeometry::DrawIndexedIndirectCommand));
            }

            cmdList->EndRenderPass();

            // IndirectDrawバッファを次のフレーム用に戻す
            cmdList->BufferBarrier(m_IndirectDrawBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
            cmdList->BufferBarrier(m_DrawCountBuffer,
                                   RHI::ResourceState::IndirectArgument,
                                   RHI::ResourceState::Common);
        }
    }

    // ========================================
    // MegaMeshインスタンス管理
    // ========================================

    void MegaGeometryPass::AddMegaMeshInstance(MegaGeometry::MegaMeshHandle handle, const float *worldMatrix)
    {
        MegaMeshInstance instance;
        instance.Handle = handle;
        if (worldMatrix)
        {
            std::memcpy(instance.WorldMatrix, worldMatrix, sizeof(float) * 16);
        }
        else
        {
            // 単位行列
            std::memset(instance.WorldMatrix, 0, sizeof(float) * 16);
            instance.WorldMatrix[0] = 1.0f;
            instance.WorldMatrix[5] = 1.0f;
            instance.WorldMatrix[10] = 1.0f;
            instance.WorldMatrix[15] = 1.0f;
        }
        m_Instances.push_back(instance);
    }

    void MegaGeometryPass::ClearMegaMeshInstances()
    {
        m_Instances.clear();
    }

    // ========================================
    // カリング用GPUリソース作成
    // ========================================

    bool MegaGeometryPass::CreateCullResources(RHI::IDevice *device)
    {
        // IndirectDrawコマンドバッファ (SSBO + IndirectBuffer)
        uint64_t indirectSize = static_cast<uint64_t>(m_Settings.MaxDrawCount) * sizeof(MegaGeometry::DrawIndexedIndirectCommand);
        RHI::BufferDesc indirectDesc(
            indirectSize,
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
            false,
            "MegaGeometry_IndirectDraw");
        m_IndirectDrawBuffer = device->CreateBuffer(indirectDesc);
        if (!m_IndirectDrawBuffer)
        {
            return false;
        }

        // DrawCountバッファ（atomic counter用 SSBO）
        RHI::BufferDesc countDesc(
            sizeof(uint32_t),
            RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::IndirectBuffer,
            false,
            "MegaGeometry_DrawCount");
        m_DrawCountBuffer = device->CreateBuffer(countDesc);
        if (!m_DrawCountBuffer)
        {
            return false;
        }

        return true;
    }

    // ========================================
    // GBuffer互換グラフィックスパイプライン作成
    // ========================================

    bool MegaGeometryPass::CreateDrawPipeline(ViewRenderContext &context)
    {
        if (!m_Device || !m_DrawVertexShader || !m_DrawFragmentShader)
        {
            return false;
        }

        // GBuffer互換レンダーパス作成（Load既存内容）
        RHI::RenderPassDesc rpDesc;

        // Albedo: Load（GBufferPassで書いた内容を保持）
        RHI::AttachmentDesc albedoAttach;
        albedoAttach.format = RHI::Format::R8G8B8A8_UNORM;
        albedoAttach.isDepthStencil = false;
        albedoAttach.clear = false;
        albedoAttach.loadOp = RHI::AttachmentLoadOp::Load;
        albedoAttach.storeOp = RHI::AttachmentStoreOp::Store;
        albedoAttach.initialState = RHI::ResourceState::ShaderResource;
        albedoAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(albedoAttach);

        // Normal: Load
        RHI::AttachmentDesc normalAttach;
        normalAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        normalAttach.isDepthStencil = false;
        normalAttach.clear = false;
        normalAttach.loadOp = RHI::AttachmentLoadOp::Load;
        normalAttach.storeOp = RHI::AttachmentStoreOp::Store;
        normalAttach.initialState = RHI::ResourceState::ShaderResource;
        normalAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(normalAttach);

        // Material: Load
        RHI::AttachmentDesc materialAttach;
        materialAttach.format = RHI::Format::R8G8B8A8_UNORM;
        materialAttach.isDepthStencil = false;
        materialAttach.clear = false;
        materialAttach.loadOp = RHI::AttachmentLoadOp::Load;
        materialAttach.storeOp = RHI::AttachmentStoreOp::Store;
        materialAttach.initialState = RHI::ResourceState::ShaderResource;
        materialAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(materialAttach);

        // Emissive: Load
        RHI::AttachmentDesc emissiveAttach;
        emissiveAttach.format = RHI::Format::R16G16B16A16_FLOAT;
        emissiveAttach.isDepthStencil = false;
        emissiveAttach.clear = false;
        emissiveAttach.loadOp = RHI::AttachmentLoadOp::Load;
        emissiveAttach.storeOp = RHI::AttachmentStoreOp::Store;
        emissiveAttach.initialState = RHI::ResourceState::ShaderResource;
        emissiveAttach.finalState = RHI::ResourceState::ShaderResource;
        rpDesc.colorAttachments.push_back(emissiveAttach);

        // Depth: Load + DepthTest
        rpDesc.hasDepthStencil = true;
        rpDesc.depthStencilAttachment.format = RHI::Format::D32_FLOAT;
        rpDesc.depthStencilAttachment.isDepthStencil = true;
        rpDesc.depthStencilAttachment.clear = false;
        rpDesc.depthStencilAttachment.loadOp = RHI::AttachmentLoadOp::Load;
        rpDesc.depthStencilAttachment.storeOp = RHI::AttachmentStoreOp::Store;
        rpDesc.depthStencilAttachment.initialState = RHI::ResourceState::ShaderResource;
        rpDesc.depthStencilAttachment.finalState = RHI::ResourceState::ShaderResource;

        m_GBufferRenderPass = m_Device->CreateRenderPass(rpDesc);
        if (!m_GBufferRenderPass)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "GBuffer互換レンダーパスの作成に失敗");
            return false;
        }

        // フレームバッファ作成（GBufferPassと同じテクスチャを参照）
        RHI::FramebufferDesc fbDesc;
        fbDesc.renderPass = m_GBufferRenderPass;
        fbDesc.colorTargets.push_back(m_AlbedoTexture);
        fbDesc.colorTargets.push_back(m_NormalTexture);
        fbDesc.colorTargets.push_back(m_MaterialTexture);
        fbDesc.colorTargets.push_back(m_EmissiveTexture);
        fbDesc.depthStencilTarget = m_DepthTexture;
        fbDesc.width = m_CurrentWidth;
        fbDesc.height = m_CurrentHeight;

        m_GBufferFramebuffer = m_Device->CreateFramebuffer(fbDesc);
        if (!m_GBufferFramebuffer)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "フレームバッファの作成に失敗");
            return false;
        }

        // グラフィックスパイプライン作成
        RHI::GraphicsPipelineDesc pipelineDesc;
        pipelineDesc.vertexShader = m_DrawVertexShader;
        pipelineDesc.pixelShader = m_DrawFragmentShader;
        pipelineDesc.primitiveTopology = RHI::PrimitiveTopology::TriangleList;

        // 頂点入力レイアウト（Mesh3DVertex互換）
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

        // ディスクリプタセットレイアウト（GBufferPassと同一: set=0, binding 0=UBO, 1-6=textures）
        RHI::DescriptorSetDesc dsDesc;
        RHI::DescriptorBinding uboBinding;
        uboBinding.binding = 0;
        uboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        uboBinding.stages = RHI::ShaderStage::Vertex;
        dsDesc.bindings.push_back(uboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            dsDesc.bindings.push_back(texBinding);
        }

        pipelineDesc.descriptorSetLayouts.push_back(dsDesc);

        m_DrawPipeline = m_Device->CreateGraphicsPipeline(pipelineDesc);
        if (!m_DrawPipeline)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "グラフィックスパイプラインの作成に失敗");
            return false;
        }

        return true;
    }

    bool MegaGeometryPass::EnsurePerInstanceBindings(uint32_t requiredCount)
    {
        if (!m_Device)
        {
            return false;
        }

        RHI::DescriptorSetDesc cullDsDesc;
        RHI::DescriptorBinding cullUboBinding;
        cullUboBinding.binding = 0;
        cullUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        cullUboBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(cullUboBinding);

        RHI::DescriptorBinding clusterBinding;
        clusterBinding.binding = 1;
        clusterBinding.type = RHI::ResourceBindType::StructuredBuffer;
        clusterBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(clusterBinding);

        RHI::DescriptorBinding indirectBinding;
        indirectBinding.binding = 2;
        indirectBinding.type = RHI::ResourceBindType::RWBuffer;
        indirectBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(indirectBinding);

        RHI::DescriptorBinding countBinding;
        countBinding.binding = 3;
        countBinding.type = RHI::ResourceBindType::RWBuffer;
        countBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(countBinding);

        RHI::DescriptorBinding hiZBinding;
        hiZBinding.binding = 4;
        hiZBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        hiZBinding.stages = RHI::ShaderStage::Compute;
        cullDsDesc.bindings.push_back(hiZBinding);

        RHI::DescriptorSetDesc drawDsDesc;
        RHI::DescriptorBinding drawUboBinding;
        drawUboBinding.binding = 0;
        drawUboBinding.type = RHI::ResourceBindType::ConstantBuffer;
        drawUboBinding.stages = RHI::ShaderStage::Vertex;
        drawDsDesc.bindings.push_back(drawUboBinding);

        for (uint32_t i = 1; i <= 6; ++i)
        {
            RHI::DescriptorBinding texBinding;
            texBinding.binding = i;
            texBinding.type = RHI::ResourceBindType::CombinedImageSampler;
            texBinding.stages = RHI::ShaderStage::Pixel;
            drawDsDesc.bindings.push_back(texBinding);
        }

        while (m_CullUniformBuffers.size() < requiredCount)
        {
            RHI::BufferDesc cullUboDesc(
                sizeof(CullUniformData),
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_CullUBO");
            auto cullUniformBuffer = m_Device->CreateBuffer(cullUboDesc);
            auto cullDescriptorSet = m_Device->CreateDescriptorSet(cullDsDesc);
            if (!cullUniformBuffer || !cullDescriptorSet)
            {
                return false;
            }

            constexpr uint32_t PER_OBJECT_UBO_SIZE = 256;
            RHI::BufferDesc drawUboDesc(
                PER_OBJECT_UBO_SIZE,
                RHI::ResourceUsage::ConstantBuffer,
                true,
                "MegaGeometry_DrawUBO");
            auto drawUniformBuffer = m_Device->CreateBuffer(drawUboDesc);
            auto drawDescriptorSet = m_Device->CreateDescriptorSet(drawDsDesc);
            if (!drawUniformBuffer || !drawDescriptorSet)
            {
                return false;
            }

            m_CullUniformBuffers.push_back(cullUniformBuffer);
            m_CullDescriptorSets.push_back(cullDescriptorSet);
            m_DrawUniformBuffers.push_back(drawUniformBuffer);
            m_DrawDescriptorSets.push_back(drawDescriptorSet);
        }

        return true;
    }

    // ========================================
    // Hi-Z 深度ピラミッドリソース作成
    // ========================================

    bool MegaGeometryPass::CreateHiZResources(ViewRenderContext &context)
    {
        if (!m_Device || !m_HiZPipeline || m_CurrentWidth == 0 || m_CurrentHeight == 0)
        {
            return false;
        }

        // 既存リソースをリセット
        m_HiZTexture.reset();
        m_HiZDescriptorSet.reset();

        // Hi-Zテクスチャ: 半解像度ベース、ミップチェーン付き
        uint32_t hiZWidth = (m_CurrentWidth + 1) / 2;
        uint32_t hiZHeight = (m_CurrentHeight + 1) / 2;
        uint32_t maxDim = (hiZWidth > hiZHeight) ? hiZWidth : hiZHeight;
        m_HiZMipCount = static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(maxDim)))) + 1;

        RHI::TextureDesc hiZTexDesc;
        hiZTexDesc.Width = hiZWidth;
        hiZTexDesc.Height = hiZHeight;
        hiZTexDesc.MipLevels = m_HiZMipCount;
        hiZTexDesc.TextureFormat = RHI::Format::R32_FLOAT;
        hiZTexDesc.Usage = RHI::ResourceUsage::ShaderRead | RHI::ResourceUsage::ShaderWrite;
        hiZTexDesc.DebugName = "MegaGeometry_HiZPyramid";

        m_HiZTexture = m_Device->CreateTexture(hiZTexDesc);
        if (!m_HiZTexture)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Hi-Zテクスチャの作成に失敗 (%ux%u, %u mips)", hiZWidth, hiZHeight, m_HiZMipCount);
            m_HiZMipCount = 0;
            return false;
        }

        // Hi-Z生成用ディスクリプタセット
        // binding 0: ソースミップ (CombinedImageSampler)
        // binding 1: デストミップ (RWTexture / StorageImage)
        // binding 2: HiZParams UBO (ConstantBuffer)
        RHI::DescriptorSetDesc hiZDsDesc;

        RHI::DescriptorBinding srcBinding;
        srcBinding.binding = 0;
        srcBinding.type = RHI::ResourceBindType::CombinedImageSampler;
        srcBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(srcBinding);

        RHI::DescriptorBinding dstBinding;
        dstBinding.binding = 1;
        dstBinding.type = RHI::ResourceBindType::RWTexture;
        dstBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(dstBinding);

        RHI::DescriptorBinding paramBinding;
        paramBinding.binding = 2;
        paramBinding.type = RHI::ResourceBindType::ConstantBuffer;
        paramBinding.stages = RHI::ShaderStage::Compute;
        hiZDsDesc.bindings.push_back(paramBinding);

        m_HiZDescriptorSet = m_Device->CreateDescriptorSet(hiZDsDesc);
        if (!m_HiZDescriptorSet)
        {
            NORVES_LOG_ERROR("MegaGeometryPass", "Hi-Zディスクリプタセットの作成に失敗");
            m_HiZTexture.reset();
            m_HiZMipCount = 0;
            return false;
        }

        NORVES_LOG_INFO("MegaGeometryPass", "Hi-Z深度ピラミッド作成 (%ux%u, %u mips)", hiZWidth, hiZHeight, m_HiZMipCount);
        return true;
    }

    // ========================================
    // Hi-Z 深度ピラミッド生成
    // ========================================

    void MegaGeometryPass::GenerateHiZPyramid(RHI::ICommandList *cmdList)
    {
        if (!m_HiZTexture || !m_HiZPipeline || !m_HiZDescriptorSet || m_HiZMipCount == 0)
        {
            return;
        }

        // GBuffer深度テクスチャをシェーダーリソースとして使用（既にShaderResource状態のはず）
        // Hi-Zテクスチャ全ミップをUAV状態に遷移
        cmdList->TextureBarrier(m_HiZTexture,
                                RHI::ResourceState::Undefined,
                                RHI::ResourceState::UnorderedAccess,
                                0, 0, m_HiZMipCount, 0);

        cmdList->SetPipeline(m_HiZPipeline);

        uint32_t srcWidth = m_CurrentWidth;
        uint32_t srcHeight = m_CurrentHeight;

        for (uint32_t mip = 0; mip < m_HiZMipCount; ++mip)
        {
            uint32_t destWidth = (m_HiZTexture->GetWidth() >> mip);
            uint32_t destHeight = (m_HiZTexture->GetHeight() >> mip);
            if (destWidth < 1) destWidth = 1;
            if (destHeight < 1) destHeight = 1;

            // HiZParams UBO更新
            int32_t params[4] = {
                static_cast<int32_t>(destWidth),
                static_cast<int32_t>(destHeight),
                0, 0
            };
            m_HiZParamsBuffer->Update(params, 16);

            // ソースバインド
            if (mip == 0)
            {
                // 初回: GBuffer深度テクスチャを読み取り
                m_HiZDescriptorSet->BindTexture(0, m_DepthTexture);
                m_HiZDescriptorSet->BindSampler(0, m_HiZNearestSampler);
            }
            else
            {
                // 前のミップをシェーダーリソースに遷移
                cmdList->TextureBarrier(m_HiZTexture,
                                        RHI::ResourceState::UnorderedAccess,
                                        RHI::ResourceState::ShaderResource,
                                        mip - 1, 0, 1, 0);

                // 前のミップをソースとしてバインド
                m_HiZDescriptorSet->BindTexture(0, m_HiZTexture);
                m_HiZDescriptorSet->BindSampler(0, m_HiZNearestSampler);
            }

            // デストミップをストレージテクスチャとしてバインド
            m_HiZDescriptorSet->BindStorageTexture(1, m_HiZTexture, mip);

            // パラメータUBO
            m_HiZDescriptorSet->BindConstantBuffer(2, m_HiZParamsBuffer, 0, 16);
            m_HiZDescriptorSet->Update();
            cmdList->SetDescriptorSet(m_HiZDescriptorSet, 0);

            // ディスパッチ（8x8ワークグループ）
            uint32_t groupX = (destWidth + 7) / 8;
            uint32_t groupY = (destHeight + 7) / 8;
            cmdList->Dispatch(groupX, groupY, 1);

            srcWidth = destWidth;
            srcHeight = destHeight;
        }

        // 最後のミップをシェーダーリソースに遷移
        cmdList->TextureBarrier(m_HiZTexture,
                                RHI::ResourceState::UnorderedAccess,
                                RHI::ResourceState::ShaderResource,
                                m_HiZMipCount - 1, 0, 1, 0);
    }

    // ========================================
    // 視錐台平面抽出（Gribb/Hartmann法）
    // ========================================

    void MegaGeometryPass::ExtractFrustumPlanes(const float *vp, float planes[6][4])
    {
        // ViewProjection行列（列優先）から6つの視錐台平面を抽出
        // 各平面: ax + by + cz + d = 0 (法線は内側を向く)

        // Left plane
        planes[0][0] = vp[3] + vp[0];
        planes[0][1] = vp[7] + vp[4];
        planes[0][2] = vp[11] + vp[8];
        planes[0][3] = vp[15] + vp[12];

        // Right plane
        planes[1][0] = vp[3] - vp[0];
        planes[1][1] = vp[7] - vp[4];
        planes[1][2] = vp[11] - vp[8];
        planes[1][3] = vp[15] - vp[12];

        // Bottom plane
        planes[2][0] = vp[3] + vp[1];
        planes[2][1] = vp[7] + vp[5];
        planes[2][2] = vp[11] + vp[9];
        planes[2][3] = vp[15] + vp[13];

        // Top plane
        planes[3][0] = vp[3] - vp[1];
        planes[3][1] = vp[7] - vp[5];
        planes[3][2] = vp[11] - vp[9];
        planes[3][3] = vp[15] - vp[13];

        // Near plane
        planes[4][0] = vp[3] + vp[2];
        planes[4][1] = vp[7] + vp[6];
        planes[4][2] = vp[11] + vp[10];
        planes[4][3] = vp[15] + vp[14];

        // Far plane
        planes[5][0] = vp[3] - vp[2];
        planes[5][1] = vp[7] - vp[6];
        planes[5][2] = vp[11] - vp[10];
        planes[5][3] = vp[15] - vp[14];

        // 正規化
        for (int i = 0; i < 6; ++i)
        {
            float len = std::sqrt(planes[i][0] * planes[i][0] +
                                  planes[i][1] * planes[i][1] +
                                  planes[i][2] * planes[i][2]);
            if (len > 1e-8f)
            {
                planes[i][0] /= len;
                planes[i][1] /= len;
                planes[i][2] /= len;
                planes[i][3] /= len;
            }
        }
    }

} // namespace NorvesLib::Core::Rendering

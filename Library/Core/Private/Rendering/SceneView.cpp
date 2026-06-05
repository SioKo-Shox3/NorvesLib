#include "Rendering/SceneView.h"
#include "Rendering/Viewport.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/ForwardPass.h"
#include "Rendering/BloomPass.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/UpscalePass.h"
#include "Rendering/SSRPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Math/MatrixUtils.h"
#include "Math/VectorUtils.h"
#include "Logging/LogMacros.h"
#include <chrono>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        Math::Matrix4x4 BuildCullingViewProjection(const CameraProxy &camera, float aspectRatio)
        {
            if (aspectRatio <= 0.0f)
            {
                aspectRatio = 1.0f;
            }

            const Math::Vector3 cameraPosition(
                camera.PositionX,
                camera.PositionY,
                camera.PositionZ);
            const Math::Vector3 forward(camera.ForwardX, camera.ForwardY, camera.ForwardZ);
            const Math::Vector3 up(camera.UpX, camera.UpY, camera.UpZ);
            const Math::Vector3 target = cameraPosition + forward;

            const Math::Matrix4x4 viewMatrix = Math::MatrixUtils::CreateLookAt(cameraPosition, target, up);

            Math::Matrix4x4 projectionMatrix;
            if (camera.Projection == ProjectionType::Orthographic)
            {
                projectionMatrix = Math::MatrixUtils::CreateOrthographic(
                    camera.OrthoWidth,
                    camera.OrthoWidth / aspectRatio,
                    camera.NearPlane,
                    camera.FarPlane);
            }
            else
            {
                projectionMatrix = Math::MatrixUtils::CreatePerspectiveFieldOfView(
                    camera.FieldOfView * (3.14159265f / 180.0f),
                    aspectRatio,
                    camera.NearPlane,
                    camera.FarPlane);
            }

            const Math::Matrix4x4 clipSpaceCorrection = Math::MatrixUtils::CreateScale(1.0f, 1.0f, -1.0f);

            return projectionMatrix * clipSpaceCorrection * viewMatrix;
        }
    }

    bool SceneView::Initialize(const SceneViewSettings &settings)
    {
        // 基底クラスの初期化
        ViewSettings baseSettings;
        baseSettings.Type = ViewType::Scene;
        baseSettings.Width = settings.Width;
        baseSettings.Height = settings.Height;
        baseSettings.bClearColor = settings.bClearColor;
        baseSettings.ClearColor[0] = settings.ClearColor[0];
        baseSettings.ClearColor[1] = settings.ClearColor[1];
        baseSettings.ClearColor[2] = settings.ClearColor[2];
        baseSettings.ClearColor[3] = settings.ClearColor[3];
        baseSettings.bClearDepth = settings.bClearDepth;
        baseSettings.ClearDepth = settings.ClearDepth;

        if (!View::Initialize(baseSettings))
        {
            return false;
        }

        // SceneView固有の設定
        m_bEnableFrustumCulling = settings.bEnableFrustumCulling;
        m_bEnableOcclusionCulling = settings.bEnableOcclusionCulling;
        m_bEnableDistanceCulling = settings.bEnableDistanceCulling;
        m_MaxDrawDistance = settings.MaxDrawDistance;
        m_bEnableInstancing = settings.bEnableInstancing;
        m_MinInstanceCount = settings.MinInstanceCount;

        return true;
    }

    void SceneView::Shutdown()
    {
        // Proxyをクリア
        ClearAllProxies();

        // DrawCommandをクリア
        m_DrawCommands.clear();
        m_OpaqueCommands.clear();
        m_TransparentCommands.clear();

        // Batcherをクリア
        m_Batcher.Clear();

        View::Shutdown();
    }

    // ========================================
    // Proxy操作（WorldからSceneViewへ直接渡す）
    // ========================================

    void SceneView::AddMeshProxy(const MeshProxy &proxy)
    {
        if (proxy.IsValid())
        {
            m_MeshProxies.push_back(proxy);
        }
    }

    void SceneView::RemoveMeshProxy(uint64_t objectId)
    {
        auto it = std::remove_if(m_MeshProxies.begin(), m_MeshProxies.end(),
                                 [objectId](const MeshProxy &proxy)
                                 { return proxy.ObjectId == objectId; });
        m_MeshProxies.erase(it, m_MeshProxies.end());
    }

    void SceneView::AddLightProxy(const LightProxy &proxy)
    {
        if (proxy.IsValid())
        {
            m_LightProxies.push_back(proxy);
        }
    }

    void SceneView::AddMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        if (proxy.IsValid())
        {
            m_MegaGeometryProxies.push_back(proxy);
        }
    }

    void SceneView::RemoveLightProxy(uint64_t objectId)
    {
        auto it = std::remove_if(m_LightProxies.begin(), m_LightProxies.end(),
                                 [objectId](const LightProxy &proxy)
                                 { return proxy.LightId == objectId; });
        m_LightProxies.erase(it, m_LightProxies.end());
    }

    void SceneView::RemoveMegaGeometryProxy(uint64_t objectId)
    {
        auto it = std::remove_if(m_MegaGeometryProxies.begin(), m_MegaGeometryProxies.end(),
                                 [objectId](const MegaGeometryProxy &proxy)
                                 { return proxy.ObjectId == objectId; });
        m_MegaGeometryProxies.erase(it, m_MegaGeometryProxies.end());
    }

    void SceneView::UpdateMeshProxy(const MeshProxy &proxy)
    {
        for (auto &existingProxy : m_MeshProxies)
        {
            if (existingProxy.ObjectId == proxy.ObjectId)
            {
                existingProxy = proxy;
                return;
            }
        }
        // 見つからなければ追加
        AddMeshProxy(proxy);
    }

    void SceneView::UpdateLightProxy(const LightProxy &proxy)
    {
        for (auto &existingProxy : m_LightProxies)
        {
            if (existingProxy.LightId == proxy.LightId)
            {
                existingProxy = proxy;
                return;
            }
        }
        // 見つからなければ追加
        AddLightProxy(proxy);
    }

    void SceneView::UpdateMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        for (auto &existingProxy : m_MegaGeometryProxies)
        {
            if (existingProxy.ComponentId == proxy.ComponentId)
            {
                existingProxy = proxy;
                return;
            }
        }
        AddMegaGeometryProxy(proxy);
    }

    void SceneView::ClearAllProxies()
    {
        m_MeshProxies.clear();
        m_MegaGeometryProxies.clear();
        m_LightProxies.clear();
        m_VisibleMeshProxies.clear();
    }

    void SceneView::ClearMegaGeometryProxies()
    {
        m_MegaGeometryProxies.clear();
    }

    // ========================================
    // 描画フロー
    // ========================================

    void SceneView::Render()
    {
        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        // 統計を更新
        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        // 各Viewportに対してカリング・描画
        for (auto &viewport : m_Viewports)
        {
            if (viewport && viewport->IsEnabled())
            {
                // カリング
                CullProxies(viewport.get());

                // バッチング
                BatchProxies();

                // DrawCommand生成
                GenerateCommands();

                // 描画実行
                RenderCommands(viewport.get());
            }
        }

        // Viewportの結果を合成
        CompositeViewports();
    }

    void SceneView::Render(ViewRenderContext &context)
    {
        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        // 統計を更新
        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        // パスチェーンが存在すれば基底クラスのパスベース描画を実行
        if (GetPassCount() > 0)
        {
            // DrawCommandはGameThreadのGenerateDrawCommands()でスナップショット済み
            // context.SnapshotDrawCommands等から各パスが参照する
            View::Render(context);
        }
        else
        {
            // パス未登録の場合はレガシー描画にフォールバック
            Render();
        }
    }

    // ========================================
    // パイプライン構築ヘルパー
    // ========================================

    void SceneView::SetupDeferredPipeline(SceneRenderer *sceneRenderer)
    {
        // 既存のパスをクリア
        while (GetPassCount() > 0)
        {
            auto &passes = m_Passes;
            if (!passes.empty())
            {
                if (passes.back() && passes.back()->IsInitialized())
                {
                    passes.back()->Shutdown();
                }
                passes.pop_back();
            }
        }

        // ShadowMapPass: ライト視点の深度描画
        ShadowMapPassSettings shadowSettings;
        auto shadowMapPass = MakeUnique<ShadowMapPass>(shadowSettings);
        shadowMapPass->SetSceneView(this);
        shadowMapPass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(shadowMapPass));

        // NeuralMaterialDecodePass: ニューラルマテリアルの事前デコード（Compute）
        auto neuralDecodePass = MakeUnique<NeuralMaterialDecodePass>();
        neuralDecodePass->SetSceneView(this);
        neuralDecodePass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(neuralDecodePass));

        // GBufferPass: ジオメトリ→GBuffer MRT
        GBufferPassSettings gbufferSettings;
        auto gbufferPass = MakeUnique<GBufferPass>(gbufferSettings);
        gbufferPass->SetSceneView(this);
        gbufferPass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(gbufferPass));

        // MegaGeometryPass: GPU駆動クラスターカリング + GBufferへのIndirectDraw
        MegaGeometryPassSettings megaGeoSettings;
        auto megaGeometryPass = MakeUnique<MegaGeometryPass>(megaGeoSettings);
        megaGeometryPass->SetSceneView(this);
        megaGeometryPass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(megaGeometryPass));

        // SSAOPass: GBufferの深度・法線からスクリーンスペースAOを計算
        SSAOSettings ssaoSettings;
        ssaoSettings.Radius = 0.5f;
        ssaoSettings.Bias = 0.025f;
        ssaoSettings.Intensity = 2.0f;
        auto ssaoPass = MakeUnique<SSAOPass>(ssaoSettings);
        AddPass(std::move(ssaoPass));

        // LightingPass: GBuffer→HDRシーンカラー
        LightingPassSettings lightingSettings;
        lightingSettings.EnvironmentMapPath = "Textures/Atmosphere/grasslands_sunset_4k.hdr";
        lightingSettings.IBLIntensity = 1.0f;
        lightingSettings.NeuralBRDFWeightPath = "Data/disney.ns.bin";
        auto lightingPass = MakeUnique<LightingPass>(lightingSettings);
        lightingPass->SetSceneView(this);
        AddPass(std::move(lightingPass));

        // PostProcessStack: SSR -> Bloom -> ToneMapping -> FXAA
        auto postProcessStack = MakeUnique<PostProcessStack>();

        // SSR（スクリーンスペース反射、HDR空間で適用）
        SSRSettings ssrSettings;
        ssrSettings.MaxDistance = 15.0f;
        ssrSettings.Thickness = 0.3f;
        ssrSettings.MaxSteps = 64.0f;
        ssrSettings.Intensity = 0.8f;
        ssrSettings.RoughnessCutoff = 0.5f;
        postProcessStack->AddPass(MakeUnique<SSRPass>(ssrSettings));

        // Bloom（ToneMappingの前にHDR空間でブルーム適用）
        BloomSettings bloomSettings;
        bloomSettings.Threshold = 1.15f;
        bloomSettings.Intensity = 0.85f;
        bloomSettings.Radius = 2.5f;
        bloomSettings.SoftKnee = 0.35f;
        postProcessStack->AddPass(MakeUnique<BloomPass>(bloomSettings));

        // ToneMapping（HDR→LDR変換 + Vignette + Color Grading）
        ToneMappingSettings toneMappingSettings;
        toneMappingSettings.Operator = ToneMappingOperator::ACES;
        postProcessStack->AddPass(MakeUnique<ToneMappingPass>(toneMappingSettings));

        // FXAA（アンチエイリアシング、最終パス）
        FXAASettings fxaaSettings;
        fxaaSettings.EdgeThreshold = 0.0312f;
        fxaaSettings.SubpixelQuality = 0.75f;
        postProcessStack->AddPass(MakeUnique<FXAAPass>(fxaaSettings));

        // Upscale（内部解像度描画時のみ最終画像をスクリーン解像度へ拡大）
        postProcessStack->AddPass(MakeUnique<UpscalePass>());

        SetPostProcessStack(std::move(postProcessStack));

        NORVES_LOG_INFO("SceneView", "Deferred pipeline: ShadowMap -> GBuffer -> SSAO -> Lighting -> SSR -> Bloom -> ToneMapping -> FXAA -> Upscale");
    }

    void SceneView::SetupForwardPipeline(SceneRenderer *sceneRenderer)
    {
        // 既存のパスをクリア
        while (GetPassCount() > 0)
        {
            auto &passes = m_Passes;
            if (!passes.empty())
            {
                if (passes.back() && passes.back()->IsInitialized())
                {
                    passes.back()->Shutdown();
                }
                passes.pop_back();
            }
        }

        // ForwardPass: 従来のフォワード描画
        auto forwardPass = MakeUnique<ForwardPass>(this, sceneRenderer);
        AddPass(std::move(forwardPass));

        // PostProcessStack: ToneMapping
        auto postProcessStack = MakeUnique<PostProcessStack>();
        ToneMappingSettings toneMappingSettings;
        toneMappingSettings.Operator = ToneMappingOperator::ACES;
        postProcessStack->AddPass(MakeUnique<ToneMappingPass>(toneMappingSettings));
        postProcessStack->AddPass(MakeUnique<UpscalePass>());
        SetPostProcessStack(std::move(postProcessStack));

        NORVES_LOG_INFO("SceneView", "Forward pipeline configured: Forward -> ToneMapping -> Upscale");
    }

    void SceneView::CullProxies(Viewport *viewport)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_VisibleMeshProxies.clear();

        if (!viewport)
        {
            return;
        }

        const CameraProxy &camera = viewport->GetCamera();
        const Math::Matrix4x4 viewProjection = BuildCullingViewProjection(camera, viewport->GetAspectRatio());

        // カメラ位置を取得（CameraProxyから直接取得）
        Math::Vector3 cameraPosition(
            camera.PositionX,
            camera.PositionY,
            camera.PositionZ);

        for (MeshProxy &proxy : m_MeshProxies)
        {
            bool bVisible = true;

            // 視錐台カリング
            if (m_bEnableFrustumCulling && bVisible)
            {
                bVisible = FrustumCull(proxy, viewProjection);
            }

            // 距離カリング
            if (m_bEnableDistanceCulling && bVisible)
            {
                bVisible = DistanceCull(proxy, cameraPosition);
            }

            if (bVisible)
            {
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.CullingTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    void SceneView::CullProxies(const ViewportSnapshot &viewport)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_VisibleMeshProxies.clear();

        if (!viewport.bHasCamera)
        {
            m_Stats.VisibleProxies = 0;
            m_Stats.CulledProxies = m_Stats.CollectedProxies;
            auto endTime = std::chrono::high_resolution_clock::now();
            m_Stats.CullingTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
            return;
        }

        const CameraProxy &camera = viewport.Camera;
        Math::Vector3 cameraPosition(
            camera.PositionX,
            camera.PositionY,
            camera.PositionZ);
        float aspectRatio = camera.AspectRatio;
        if (viewport.PixelRect.Height > 0.0f)
        {
            aspectRatio = viewport.PixelRect.Width / viewport.PixelRect.Height;
        }
        else if (camera.Viewport.Height > 0.0f)
        {
            aspectRatio = camera.Viewport.Width / camera.Viewport.Height;
        }
        else if (viewport.RenderHeight > 0)
        {
            aspectRatio = static_cast<float>(viewport.RenderWidth) / static_cast<float>(viewport.RenderHeight);
        }
        if (aspectRatio <= 0.0f)
        {
            aspectRatio = 1.0f;
        }

        const Math::Matrix4x4 viewProjection = BuildCullingViewProjection(camera, aspectRatio);

        for (MeshProxy &proxy : m_MeshProxies)
        {
            bool bVisible = true;

            if (m_bEnableFrustumCulling && bVisible)
            {
                bVisible = FrustumCull(proxy, viewProjection);
            }

            if (m_bEnableDistanceCulling && bVisible)
            {
                bVisible = DistanceCull(proxy, cameraPosition);
            }

            if (bVisible)
            {
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.CullingTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    void SceneView::BatchProxies()
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_Batcher.BeginBatching();

        for (MeshProxy *proxy : m_VisibleMeshProxies)
        {
            if (proxy)
            {
                m_Batcher.AddMeshProxy(*proxy);
            }
        }

        m_Batcher.EndBatching();

        m_Stats.BatchCount = m_Batcher.GetStats().TotalBatches;

        auto endTime = std::chrono::high_resolution_clock::now();
        m_Stats.BatchingTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    }

    void SceneView::GenerateCommands()
    {
        m_DrawCommands.clear();
        m_OpaqueCommands.clear();
        m_TransparentCommands.clear();

        // バッチャーからDrawCommandを生成
        m_Batcher.GenerateDrawCommands(m_DrawCommands);

        // 不透明と透明を分離してソート
        DrawCommandSorter::SortAndSeparate(m_DrawCommands, m_OpaqueCommands, m_TransparentCommands);

        // ソート
        DrawCommandSorter::Sort(m_OpaqueCommands, DrawCommandSorter::SortMode::FrontToBack);
        DrawCommandSorter::Sort(m_TransparentCommands, DrawCommandSorter::SortMode::BackToFront);

        m_Stats.DrawCommandCount = static_cast<uint32_t>(m_DrawCommands.size());
        m_Stats.InstancedDrawCalls = m_Batcher.GetStats().InstancedDrawCalls;
    }

    void SceneView::RenderCommands(Viewport *viewport)
    {
        if (!viewport)
        {
            return;
        }

        // TODO: 実際の描画実行
        // 1. レンダーターゲットを設定
        // 2. ビューポートを設定
        // 3. 不透明オブジェクトを描画
        // 4. 透明オブジェクトを描画
    }

    void SceneView::PrepareDrawCommands()
    {
        if (!m_bInitialized)
        {
            return;
        }

        // 統計の更新
        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        // カメラ非依存のカリング: 可視フラグのみでフィルタリング
        m_VisibleMeshProxies.clear();
        for (MeshProxy &proxy : m_MeshProxies)
        {
            if (proxy.bVisible)
            {
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        // バッチング
        BatchProxies();

        // DrawCommand生成
        GenerateCommands();
    }

    void SceneView::PrepareDrawCommandsForViewport(const ViewportSnapshot &viewport)
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        if (viewport.bHasCamera)
        {
            CullProxies(viewport);
        }
        else
        {
            m_VisibleMeshProxies.clear();
            for (MeshProxy &proxy : m_MeshProxies)
            {
                if (proxy.bVisible)
                {
                    m_VisibleMeshProxies.push_back(&proxy);
                }
            }

            m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size());
            m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;
            m_Stats.CullingTimeMs = 0.0f;
        }

        BatchProxies();
        GenerateCommands();
    }

    bool SceneView::FrustumCull(const MeshProxy &proxy, const Math::Matrix4x4 &viewProjection) const
    {
        // 簡易的な視錐台カリング
        // バウンディングスフィアの中心を変換し、範囲内かチェック
        const BoundingSphere &bounds = proxy.WorldBounds;

        const Math::Vector3 center(bounds.CenterX, bounds.CenterY, bounds.CenterZ);
        const Math::ClipSpaceSphereBounds clipBounds =
            Math::MatrixUtils::TransformSphereToClipSpace(viewProjection, center, bounds.Radius);
        return Math::MatrixUtils::IntersectsClipSpace(clipBounds, Math::ClipSpaceDepthRange::ZeroToOne);
    }

    bool SceneView::DistanceCull(const MeshProxy &proxy, const Math::Vector3 &cameraPosition) const
    {
        const BoundingSphere &bounds = proxy.WorldBounds;

        float dx = bounds.CenterX - cameraPosition.x;
        float dy = bounds.CenterY - cameraPosition.y;
        float dz = bounds.CenterZ - cameraPosition.z;
        float distanceSq = dx * dx + dy * dy + dz * dz;

        float maxDistance = m_MaxDrawDistance + bounds.Radius;
        return distanceSq <= (maxDistance * maxDistance);
    }

} // namespace NorvesLib::Core::Rendering

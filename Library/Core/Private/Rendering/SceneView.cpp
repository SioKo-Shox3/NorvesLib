#include "Rendering/SceneView.h"
#include "Rendering/Viewport.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/SkyAtmospherePass.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/VisibilityResolvePass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/VolumetricsPass.h"
#include "Rendering/ForwardPass.h"
#include "Rendering/BloomPass.h"
#include "Rendering/ToneMappingPass.h"
#include "Rendering/VignettePass.h"
#include "Rendering/DebugDrawPass.h"
#include "Rendering/SSAOPass.h"
#include "Rendering/FXAAPass.h"
#include "Rendering/UpscalePass.h"
#include "Rendering/SSRPass.h"
#include "Rendering/AutoExposurePass.h"
#include "Rendering/TemporalAAPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/NeuralMaterialDecodePass.h"
#include "Rendering/MegaGeometryPass.h"
#include "Rendering/CameraViewConstants.h"
#include "Math/MatrixUtils.h"
#include "Debug/Stats.h"
#include "Logging/LogMacros.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // ディファードのLightingPassとパストレーサーが共有する環境マップの設定。
        constexpr const char* DefaultEnvironmentMapPath = "Textures/Atmosphere/grasslands_sunset_4k.hdr";
        constexpr float DefaultEnvironmentIntensity = 1.0f;

        float NormalizeBoardFlipFlag(bool bFlip)
        {
            return bFlip ? 1.0f : 0.0f;
        }

        void FillWorldBoardInstanceData(const BoardProxy &proxy,
                                        GPUSceneInstanceData &outData)
        {
            Math::MatrixUtils::CopyToShaderData(proxy.WorldTransform, outData.World);
            Math::MatrixUtils::CopyToShaderData(proxy.PreviousWorldTransform, outData.PreviousWorld);

            for (float &value : outData.NormalMatrix)
            {
                value = 0.0f;
            }

            outData.NormalMatrix[0] = proxy.SizeWorld.x;
            outData.NormalMatrix[1] = proxy.SizeWorld.y;
            outData.NormalMatrix[2] = proxy.Pivot.x;
            outData.NormalMatrix[3] = proxy.Pivot.y;
            outData.NormalMatrix[4] = NormalizeBoardFlipFlag(proxy.bFlipX);
            outData.NormalMatrix[5] = NormalizeBoardFlipFlag(proxy.bFlipY);
            outData.NormalMatrix[6] = proxy.UVRect.x;
            outData.NormalMatrix[7] = proxy.UVRect.y;
            outData.NormalMatrix[8] = proxy.UVRect.z;
            outData.NormalMatrix[9] = proxy.UVRect.w;
            outData.NormalMatrix[10] = static_cast<float>(proxy.ImpostorAxisCellCountX);
            outData.NormalMatrix[11] = static_cast<float>(proxy.ImpostorAxisCellCountY);

            outData.ObjectColor[0] = proxy.Tint.x;
            outData.ObjectColor[1] = proxy.Tint.y;
            outData.ObjectColor[2] = proxy.Tint.z;
            outData.ObjectColor[3] = proxy.BlendModeProp == BlendMode::Opaque ? 1.0f : proxy.Tint.w;
            outData.CustomData[0] = static_cast<float>(proxy.ImpostorCellResolution);
            outData.CustomData[1] = static_cast<float>(proxy.ImpostorAtlasWidth);
            outData.CustomData[2] = static_cast<float>(proxy.ImpostorAtlasHeight);
            outData.CustomData[3] = proxy.LODSwitchDistance;
        }

        float ComputeDistanceToBounds(const BoundingSphere &bounds,
                                      const Math::Vector3 &cameraPosition)
        {
            const float dx = bounds.CenterX - cameraPosition.x;
            const float dy = bounds.CenterY - cameraPosition.y;
            const float dz = bounds.CenterZ - cameraPosition.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        bool IsImpostorBoardEligibleForDistance(const BoardProxy &proxy,
                                                float cameraDistance)
        {
            return proxy.RenderSubtype != BoardRenderSubtype::Impostor ||
                   proxy.LODSwitchDistance <= 0.0f ||
                   cameraDistance >= proxy.LODSwitchDistance;
        }

        bool ShouldSuppressMeshForImpostor(const BoardProxy &proxy)
        {
            return proxy.RenderSubtype == BoardRenderSubtype::Impostor &&
                   proxy.SourceMeshComponentId != 0;
        }

        AutoExposurePass *FindAutoExposurePass(const PostProcessStack *postProcessStack)
        {
            if (!postProcessStack)
            {
                return nullptr;
            }
            for (const auto &pass : postProcessStack->GetPasses())
            {
                if (auto *autoExposurePass = dynamic_cast<AutoExposurePass *>(pass.get()))
                {
                    return autoExposurePass->IsEnabled() ? autoExposurePass : nullptr;
                }
            }
            return nullptr;
        }

        // 露出の方式が Auto のカメラへ、前のフレームまでに順応させた EV100 の露出を写す。
        // 写した露出はこの View の全パス（ライティング・空・フォグ・測定）で同じ値として使われる。
        bool TryApplyAutoExposure(AutoExposurePass &autoExposurePass,
                                  const CameraProxy &camera,
                                  CameraProxy &outCamera)
        {
            autoExposurePass.SetExposureCompensation(camera.ExposureCompensation);
            autoExposurePass.SetCompensationCurve(camera.AutoExposureCurve);
            float adaptedEV100 = 0.0f;
            if (!autoExposurePass.TryGetAdaptedEV100(adaptedEV100))
            {
                return false;
            }
            const float exposure = AutoExposurePreExposureFromEV100(adaptedEV100);
            if (!(exposure > 0.0f))
            {
                return false;
            }
            outCamera = camera;
            outCamera.EV100 = adaptedEV100;
            outCamera.Exposure = exposure;
            outCamera.PreExposure = exposure;
            outCamera.InvPreExposure = 1.0f / exposure;
            return true;
        }

        TemporalAAPass *FindTemporalAAPass(const PostProcessStack *postProcessStack)
        {
            if (!postProcessStack)
            {
                return nullptr;
            }
            for (const auto &pass : postProcessStack->GetPasses())
            {
                if (auto *temporalAAPass = dynamic_cast<TemporalAAPass *>(pass.get()))
                {
                    return temporalAAPass;
                }
            }
            return nullptr;
        }
    } // namespace

    bool SceneView::TryGetAutoExposureMeasurement(AutoExposureMeasurement &outMeasurement) const
    {
        const AutoExposurePass *autoExposurePass = FindAutoExposurePass(GetPostProcessStack());
        if (!autoExposurePass || !autoExposurePass->GetLatestMeasurement().bValid)
        {
            return false;
        }
        outMeasurement = autoExposurePass->GetLatestMeasurement();
        return true;
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
        m_InstanceData.clear();

        // Batcherをクリア
        m_Batcher.Clear();

        View::Shutdown();
    }

    // ========================================
    // Proxy操作（WorldからSceneViewへ直接渡す）
    // ========================================

    void SceneView::AddMeshProxy(const MeshProxy &proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }

        auto indexIt = m_MeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_MeshProxyIndex.end())
        {
            m_MeshProxies[indexIt->second] = proxy;
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_MeshProxies.size());
        m_MeshProxies.push_back(proxy);
        m_MeshProxyIndex[proxy.ComponentId] = index;
        m_VisibleMeshProxies.clear();
    }

    void SceneView::RemoveMeshProxy(uint64_t componentId)
    {
        auto indexIt = m_MeshProxyIndex.find(componentId);
        if (indexIt == m_MeshProxyIndex.end())
        {
            return;
        }

        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_MeshProxies.size() - 1);
        m_MeshProxyIndex.erase(indexIt);

        if (removeIndex != lastIndex)
        {
            m_MeshProxies[removeIndex] = m_MeshProxies[lastIndex];
            m_MeshProxyIndex[m_MeshProxies[removeIndex].ComponentId] = removeIndex;
        }

        m_MeshProxies.pop_back();
        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();
    }

    void SceneView::RemoveStaleMeshProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds)
    {
        uint32_t index = 0;
        while (index < m_MeshProxies.size())
        {
            const uint64_t componentId = m_MeshProxies[index].ComponentId;
            if (liveComponentIds.find(componentId) != liveComponentIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_MeshProxies.size() - 1);
            m_MeshProxyIndex.erase(componentId);
            if (index != lastIndex)
            {
                m_MeshProxies[index] = m_MeshProxies[lastIndex];
                m_MeshProxyIndex[m_MeshProxies[index].ComponentId] = index;
            }
            m_MeshProxies.pop_back();
            m_VisibleMeshProxies.clear();
        }
    }

    void SceneView::UpdateBoardProxy(const BoardProxy &proxy)
    {
        if (!proxy.IsValid() || proxy.Space != BoardSpace::WorldSpace)
        {
            RemoveBoardProxy(proxy.ComponentId);
            return;
        }

        auto indexIt = m_BoardProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_BoardProxyIndex.end())
        {
            m_BoardProxies[indexIt->second] = proxy;
            m_VisibleBoardProxies.clear();
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_BoardProxies.size());
        m_BoardProxies.push_back(proxy);
        m_BoardProxyIndex[proxy.ComponentId] = index;
        m_VisibleBoardProxies.clear();
    }

    void SceneView::RemoveBoardProxy(uint64_t componentId)
    {
        auto indexIt = m_BoardProxyIndex.find(componentId);
        if (indexIt == m_BoardProxyIndex.end())
        {
            return;
        }

        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_BoardProxies.size() - 1);
        m_BoardProxyIndex.erase(indexIt);

        if (removeIndex != lastIndex)
        {
            m_BoardProxies[removeIndex] = m_BoardProxies[lastIndex];
            m_BoardProxyIndex[m_BoardProxies[removeIndex].ComponentId] = removeIndex;
        }

        m_BoardProxies.pop_back();
        m_VisibleBoardProxies.clear();
    }

    void SceneView::RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds)
    {
        uint32_t index = 0;
        while (index < m_BoardProxies.size())
        {
            const uint64_t componentId = m_BoardProxies[index].ComponentId;
            if (liveComponentIds.find(componentId) != liveComponentIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_BoardProxies.size() - 1);
            m_BoardProxyIndex.erase(componentId);
            if (index != lastIndex)
            {
                m_BoardProxies[index] = m_BoardProxies[lastIndex];
                m_BoardProxyIndex[m_BoardProxies[index].ComponentId] = index;
            }
            m_BoardProxies.pop_back();
            m_VisibleBoardProxies.clear();
        }
    }

    void SceneView::AddSkinnedMeshProxy(const SkinnedMeshProxy& proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }
        auto indexIt = m_SkinnedMeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_SkinnedMeshProxyIndex.end())
        {
            m_SkinnedMeshProxies[indexIt->second] = proxy;
            return;
        }
        const uint32_t index = static_cast<uint32_t>(m_SkinnedMeshProxies.size());
        m_SkinnedMeshProxies.push_back(proxy);
        m_SkinnedMeshProxyIndex[proxy.ComponentId] = index;
    }

    void SceneView::RemoveSkinnedMeshProxy(uint64_t componentId)
    {
        auto indexIt = m_SkinnedMeshProxyIndex.find(componentId);
        if (indexIt == m_SkinnedMeshProxyIndex.end())
        {
            return;
        }
        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_SkinnedMeshProxies.size() - 1);
        m_SkinnedMeshProxyIndex.erase(indexIt);
        if (removeIndex != lastIndex)
        {
            m_SkinnedMeshProxies[removeIndex] = m_SkinnedMeshProxies[lastIndex];
            m_SkinnedMeshProxyIndex[m_SkinnedMeshProxies[removeIndex].ComponentId] = removeIndex;
        }
        m_SkinnedMeshProxies.pop_back();
    }

    void SceneView::RemoveStaleSkinnedMeshProxies(const Container::UnorderedSet<uint64_t>& liveComponentIds)
    {
        uint32_t index = 0;
        while (index < m_SkinnedMeshProxies.size())
        {
            const uint64_t componentId = m_SkinnedMeshProxies[index].ComponentId;
            if (liveComponentIds.find(componentId) != liveComponentIds.end())
            {
                ++index;
                continue;
            }
            const uint32_t lastIndex = static_cast<uint32_t>(m_SkinnedMeshProxies.size() - 1);
            m_SkinnedMeshProxyIndex.erase(componentId);
            if (index != lastIndex)
            {
                m_SkinnedMeshProxies[index] = m_SkinnedMeshProxies[lastIndex];
                m_SkinnedMeshProxyIndex[m_SkinnedMeshProxies[index].ComponentId] = index;
            }
            m_SkinnedMeshProxies.pop_back();
        }
    }

    void SceneView::UpdateSkinnedMeshProxy(const SkinnedMeshProxy& proxy)
    {
        auto indexIt = m_SkinnedMeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_SkinnedMeshProxyIndex.end())
        {
            m_SkinnedMeshProxies[indexIt->second] = proxy;
            return;
        }
        AddSkinnedMeshProxy(proxy);
    }

    void SceneView::AddLightProxy(const LightProxy &proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }

        auto indexIt = m_LightProxyIndex.find(proxy.LightId);
        if (indexIt != m_LightProxyIndex.end())
        {
            m_LightProxies[indexIt->second] = proxy;
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_LightProxies.size());
        m_LightProxies.push_back(proxy);
        m_LightProxyIndex[proxy.LightId] = index;
    }

    void SceneView::AddMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }

        auto indexIt = m_MegaGeometryProxyIndex.find(proxy.ObjectId);
        if (indexIt != m_MegaGeometryProxyIndex.end())
        {
            m_MegaGeometryProxies[indexIt->second] = proxy;
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_MegaGeometryProxies.size());
        m_MegaGeometryProxies.push_back(proxy);
        m_MegaGeometryProxyIndex[proxy.ObjectId] = index;
    }

    void SceneView::RemoveLightProxy(uint64_t objectId)
    {
        auto indexIt = m_LightProxyIndex.find(objectId);
        if (indexIt == m_LightProxyIndex.end())
        {
            return;
        }

        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_LightProxies.size() - 1);
        m_LightProxyIndex.erase(indexIt);

        if (removeIndex != lastIndex)
        {
            m_LightProxies[removeIndex] = m_LightProxies[lastIndex];
            m_LightProxyIndex[m_LightProxies[removeIndex].LightId] = removeIndex;
        }

        m_LightProxies.pop_back();
    }

    void SceneView::RemoveStaleLightProxies(const Container::UnorderedSet<uint64_t> &liveLightIds)
    {
        uint32_t index = 0;
        while (index < m_LightProxies.size())
        {
            const uint64_t lightId = m_LightProxies[index].LightId;
            if (liveLightIds.find(lightId) != liveLightIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_LightProxies.size() - 1);
            m_LightProxyIndex.erase(lightId);
            if (index != lastIndex)
            {
                m_LightProxies[index] = m_LightProxies[lastIndex];
                m_LightProxyIndex[m_LightProxies[index].LightId] = index;
            }
            m_LightProxies.pop_back();
        }
    }

    void SceneView::RemoveMegaGeometryProxy(uint64_t objectId)
    {
        auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
        if (indexIt == m_MegaGeometryProxyIndex.end())
        {
            return;
        }

        const uint32_t removeIndex = indexIt->second;
        const uint32_t lastIndex = static_cast<uint32_t>(m_MegaGeometryProxies.size() - 1);
        m_MegaGeometryProxyIndex.erase(indexIt);

        if (removeIndex != lastIndex)
        {
            m_MegaGeometryProxies[removeIndex] = m_MegaGeometryProxies[lastIndex];
            m_MegaGeometryProxyIndex[m_MegaGeometryProxies[removeIndex].ObjectId] = removeIndex;
        }

        m_MegaGeometryProxies.pop_back();
    }

    void SceneView::RemoveStaleMegaGeometryProxies(const Container::UnorderedSet<uint64_t> &liveObjectIds)
    {
        uint32_t index = 0;
        while (index < m_MegaGeometryProxies.size())
        {
            const uint64_t objectId = m_MegaGeometryProxies[index].ObjectId;
            if (liveObjectIds.find(objectId) != liveObjectIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_MegaGeometryProxies.size() - 1);
            m_MegaGeometryProxyIndex.erase(objectId);
            if (index != lastIndex)
            {
                m_MegaGeometryProxies[index] = m_MegaGeometryProxies[lastIndex];
                m_MegaGeometryProxyIndex[m_MegaGeometryProxies[index].ObjectId] = index;
            }
            m_MegaGeometryProxies.pop_back();
        }
    }

    void SceneView::UpdateMeshProxy(const MeshProxy &proxy)
    {
        auto indexIt = m_MeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_MeshProxyIndex.end())
        {
            m_MeshProxies[indexIt->second] = proxy;
            return;
        }

        // 見つからなければ追加
        AddMeshProxy(proxy);
    }

    void SceneView::UpdateLightProxy(const LightProxy &proxy)
    {
        auto indexIt = m_LightProxyIndex.find(proxy.LightId);
        if (indexIt != m_LightProxyIndex.end())
        {
            m_LightProxies[indexIt->second] = proxy;
            return;
        }

        // 見つからなければ追加
        AddLightProxy(proxy);
    }

    void SceneView::UpdateMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        auto indexIt = m_MegaGeometryProxyIndex.find(proxy.ObjectId);
        if (indexIt != m_MegaGeometryProxyIndex.end())
        {
            m_MegaGeometryProxies[indexIt->second] = proxy;
            return;
        }

        AddMegaGeometryProxy(proxy);
    }

    void SceneView::ClearAllProxies()
    {
        m_MeshProxies.clear();
        m_SkinnedMeshProxies.clear();
        m_BoardProxies.clear();
        m_MegaGeometryProxies.clear();
        m_LightProxies.clear();
        m_MeshProxyIndex.clear();
        m_SkinnedMeshProxyIndex.clear();
        m_BoardProxyIndex.clear();
        m_MegaGeometryProxyIndex.clear();
        m_LightProxyIndex.clear();
        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();
    }

    void SceneView::ClearMegaGeometryProxies()
    {
        m_MegaGeometryProxies.clear();
        m_MegaGeometryProxyIndex.clear();
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
        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size() + m_BoardProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size() + m_BoardProxies.size());

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
        const uint32_t viewId = context.CurrentViewport ? context.CurrentViewport->ViewId : UINT32_MAX;
        const uint32_t viewportId = context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        context.PhysicalLighting.Begin(context.FrameNumber, viewId, viewportId);
        context.PhysicalLighting.ConfigureRTGI(context.RTGICapability,
                                               context.bRTGIEnabled,
                                               context.bRTGITLASAvailable,
                                               context.SceneRevision,
                                               context.LightRevision);
        context.SkyAtmosphere.Reset();
        struct PhysicalLightingScope final
        {
            ViewRenderContext& Context;
            ~PhysicalLightingScope() { Context.PhysicalLighting.Invalidate(); }
        } physicalLightingScope{context};

        if (!m_bEnabled || !m_bInitialized)
        {
            return;
        }

        // 統計を更新
        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        // 決定的な撮影のエポックの最初のフレーム: 時間的な状態（TAA のジッタの列と履歴・自動露出の順応）を捨てて
        // 数え直す。RTGI の乱数の列と履歴は LightingPass が同じ合図で捨てる。
        if (context.bTemporalEpochStart)
        {
            if (TemporalAAPass *temporalAAPass = FindTemporalAAPass(GetPostProcessStack()))
            {
                temporalAAPass->ResetForDeterministicEpoch();
            }
            if (AutoExposurePass *autoExposurePass = FindAutoExposurePass(GetPostProcessStack()))
            {
                autoExposurePass->ResetForDeterministicEpoch();
            }
        }

        // 自動露出のカメラは、この View を描く間だけ露出を写したカメラの複製へ差し替える
        // （FramePacket のカメラは書き換えない）
        CameraProxy autoExposedCamera;
        struct CameraOverrideScope final
        {
            ViewRenderContext &Context;
            const CameraProxy *SavedMainCamera;
            const CameraProxy *SavedCurrentCamera;
            const CameraProxy *SavedPreviousMainCamera;
            uint64_t SavedPreviousCameraFrameNumber;
            ~CameraOverrideScope()
            {
                Context.MainCamera = SavedMainCamera;
                Context.CurrentCamera = SavedCurrentCamera;
                Context.PreviousMainCamera = SavedPreviousMainCamera;
                Context.PreviousCameraFrameNumber = SavedPreviousCameraFrameNumber;
            }
        } cameraOverrideScope{context, context.MainCamera, context.CurrentCamera, context.PreviousMainCamera,
                              context.PreviousCameraFrameNumber};
        if (const CameraProxy *activeCamera = context.GetActiveCamera();
            activeCamera && activeCamera->ExposureMode == CameraExposureMode::Auto)
        {
            AutoExposurePass *autoExposurePass = FindAutoExposurePass(GetPostProcessStack());
            if (autoExposurePass && TryApplyAutoExposure(*autoExposurePass, *activeCamera, autoExposedCamera))
            {
                if (context.CurrentCamera)
                {
                    context.CurrentCamera = &autoExposedCamera;
                }
                else
                {
                    context.MainCamera = &autoExposedCamera;
                }
            }
        }

        // TAA: カメラが TAA を選んだ Viewport では、この View を描く間だけ投影へサブピクセルのジッタを掛ける。
        // 前のカメラにも同じジッタを掛け、velocity と履歴の再投影からジッタを除く。
        CameraProxy jitteredCamera;
        CameraProxy jitteredPreviousCamera;
        bool bApplyTemporalAA = false;
        if (TemporalAAPass *temporalAAPass = FindTemporalAAPass(GetPostProcessStack()))
        {
            const CameraProxy *activeCamera = context.GetActiveCamera();
            TemporalAAJitter jitter;
            bApplyTemporalAA = activeCamera != nullptr &&
                               (m_bTemporalAAForced ||
                                activeCamera->AntiAliasing == CameraAntiAliasingMode::TemporalAA) &&
                               context.GetActiveDebugMode() == DebugViewMode::Normal &&
                               temporalAAPass->BeginFrame(context.FrameNumber,
                                                          viewportId,
                                                          context.GetActiveRenderWidth(),
                                                          context.GetActiveRenderHeight(),
                                                          jitter);
            if (bApplyTemporalAA)
            {
                // 描画がゲームのフレームを飛ばしたときは、前のカメラを TAA の履歴を書いたフレームのカメラにする
                // （物体の前の変換も RenderingCoordinator がそのフレームへ付け替える）。
                const CameraProxy *reprojectionCamera = temporalAAPass->FindReprojectionCamera(
                    viewportId, activeCamera->CameraId, activeCamera->SourceCameraId, context.FrameNumber);
                const CameraProxy *previousCamera =
                    reprojectionCamera ? reprojectionCamera : context.GetPreviousCamera();
                jitteredCamera = *activeCamera;
                ApplyTemporalAAJitter(jitteredCamera, jitter);
                if (context.CurrentCamera)
                {
                    context.CurrentCamera = &jitteredCamera;
                }
                else
                {
                    context.MainCamera = &jitteredCamera;
                }
                if (previousCamera)
                {
                    jitteredPreviousCamera = *previousCamera;
                    ApplyTemporalAAJitter(jitteredPreviousCamera, jitter);
                    context.PreviousMainCamera = &jitteredPreviousCamera;
                }
                if (reprojectionCamera)
                {
                    context.PreviousCameraFrameNumber = temporalAAPass->GetHistoryFrameNumber();
                }
            }
        }
        SetTemporalAAApplied(bApplyTemporalAA, viewportId);

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

    void SceneView::SetTemporalAAApplied(bool bApplied, uint32_t viewportId)
    {
        PostProcessStack *postProcessStack = GetPostProcessStack();
        TemporalAAPass *temporalAAPass = FindTemporalAAPass(postProcessStack);
        if (!temporalAAPass)
        {
            return;
        }
        // 有効・無効は Viewport ごとに切り替わる（同じフレームの2つ目以降の Viewport では無効）。履歴は
        // それを書いた Viewport を TAA 無しで描いたときだけ捨てる。
        temporalAAPass->NotifyViewportRendered(viewportId, bApplied);

        IViewPass *fxaaPass = postProcessStack->GetPass("FXAAPass");
        if (!fxaaPass)
        {
            return;
        }
        if (bApplied && fxaaPass->IsEnabled())
        {
            fxaaPass->SetEnabled(false);
            m_bFXAASuppressedByTemporalAA = true;
        }
        else if (!bApplied && m_bFXAASuppressedByTemporalAA)
        {
            fxaaPass->SetEnabled(true);
            m_bFXAASuppressedByTemporalAA = false;
        }
    }

    // ========================================
    // パイプライン構築ヘルパー
    // ========================================

    void SceneView::SetupDeferredPipeline(SceneRenderer *sceneRenderer,
                                          RasterDirectBrdf directBrdf,
                                          VisibilityBufferMode visibilityBuffer)
    {
        const bool bVisibilityBuffer = IsVisibilityBufferActive(visibilityBuffer);
        // On のときは、ビジビリティバッファの解決が GBuffer を書く（GBufferPass・MegaGeometryPass は GBuffer の描画を止める）。
        // Debug は今の GBuffer の描画を残したまま、ID の検証表示だけを足す
        const bool bVisibilityResolve = visibilityBuffer == VisibilityBufferMode::On;
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
        shadowMapPass->SetRegisterLegacyBridge(false);
        AddPass(std::move(shadowMapPass));

        // NeuralMaterialDecodePass: ニューラルマテリアルの事前デコード（Compute）
        auto neuralDecodePass = MakeUnique<NeuralMaterialDecodePass>();
        neuralDecodePass->SetSceneView(this);
        neuralDecodePass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(neuralDecodePass));

        // SkinningComputePass: スキニングの今・前のフレームの頂点を計算シェーダーで作る。
        // 今の GBuffer の経路は頂点シェーダーのスキニングのままなので、ビジビリティバッファを使うときまで無効にしておく。
        auto skinningComputePass = MakeUnique<SkinningComputePass>();
        skinningComputePass->SetEnabled(bVisibilityBuffer);
        SkinningComputePass *skinningComputePassPtr = skinningComputePass.get();
        AddPass(std::move(skinningComputePass));

        // GBufferPass: ジオメトリ→GBuffer MRT
        GBufferPassSettings gbufferSettings;
        auto gbufferPass = MakeUnique<GBufferPass>(gbufferSettings);
        gbufferPass->SetSceneView(this);
        gbufferPass->SetSceneRenderer(sceneRenderer);
        gbufferPass->SetRegisterLegacyBridge(false);
        gbufferPass->SetVisibilityResolveActive(bVisibilityResolve);
        GBufferPass *gbufferPassPtr = gbufferPass.get();
        AddPass(std::move(gbufferPass));

        // MegaGeometryPass: GPU駆動クラスターカリング + GBufferへのIndirectDraw
        MegaGeometryPassSettings megaGeoSettings;
        auto megaGeometryPass = MakeUnique<MegaGeometryPass>(megaGeoSettings);
        megaGeometryPass->SetSceneView(this);
        megaGeometryPass->SetSceneRenderer(sceneRenderer);
        megaGeometryPass->SetVisibilityDrawPlanEnabled(bVisibilityBuffer);
        megaGeometryPass->SetSkipGBufferDraw(bVisibilityResolve);
        MegaGeometryPass *megaGeometryPassPtr = megaGeometryPass.get();
        AddPass(std::move(megaGeometryPass));

        // VisibilityRasterPass: 不透明の描画のすべて（MegaGeometry のクラスタ・手続きメッシュの塊・スキニングの塊）を、
        // VisBuffer.Id と GBuffer.Depth へ描く（on では GBuffer の描画の代わりに、debug では今の GBuffer の描画に加えて）。
        // --visibility-buffer=on|debug のときだけ足す。
        VisibilityRasterPass *visibilityRasterPassPtr = nullptr;
        if (bVisibilityBuffer)
        {
            auto visibilityRasterPass = MakeUnique<VisibilityRasterPass>();
            visibilityRasterPass->SetMegaGeometryPass(megaGeometryPassPtr);
            visibilityRasterPass->SetSkinningComputePass(skinningComputePassPtr);
            visibilityRasterPassPtr = visibilityRasterPass.get();
            AddPass(std::move(visibilityRasterPass));
        }

        // VisibilityResolvePass: VisBuffer.Id から三角形を引いて、GBuffer の Albedo・Normal・Velocity を書く（--visibility-buffer=on）。
        // 使えないとき（装置の非対応・ID のラスタや解決のパイプラインが無い）は何も宣言せず、GBufferPass・MegaGeometryPass も
        // 描画を止めない。判定はこのパスに問い合わせる（GBufferPass・MegaGeometryPass が持つ）。
        if (visibilityRasterPassPtr && bVisibilityResolve)
        {
            auto visibilityResolvePass = MakeUnique<VisibilityResolvePass>();
            visibilityResolvePass->SetRasterPass(visibilityRasterPassPtr);
            visibilityResolvePass->SetSkinningComputePass(skinningComputePassPtr);
            gbufferPassPtr->SetVisibilityResolvePass(visibilityResolvePass.get());
            megaGeometryPassPtr->SetVisibilityResolvePass(visibilityResolvePass.get());
            AddPass(std::move(visibilityResolvePass));
        }

        // MaterialTileClassifyPass: VisBuffer.Id から、材質ごとのタイルの一覧と間接 dispatch の引数を作る。
        // 材質の解決が使うまで無効にしておく（既定の描画は変えない）。
        if (visibilityRasterPassPtr)
        {
            auto materialTileClassifyPass = MakeUnique<MaterialTileClassifyPass>();
            materialTileClassifyPass->SetRasterPass(visibilityRasterPassPtr);
            AddPass(std::move(materialTileClassifyPass));
        }

        // SSAOPass: GBufferの深度・法線から画面空間AO（GTAO）を計算。半径は世界の長さ（m）で、
        // 球・岩の接地部や軒下（数十cm〜1 m）を拾い、部屋の大きさの壁全体は遮蔽にしない。
        SSAOSettings ssaoSettings;
        ssaoSettings.Radius = 1.0f;
        ssaoSettings.Intensity = 1.0f;
        auto ssaoPass = MakeUnique<SSAOPass>(ssaoSettings);
        AddPass(std::move(ssaoPass));

        // SkyAtmospherePass: 同一空スナップショットからLUTと太陽ディスクを生成
        auto skyAtmospherePass = MakeUnique<SkyAtmospherePass>();
        AddPass(std::move(skyAtmospherePass));

        // LightingPass: GBuffer→HDRシーンカラー
        LightingPassSettings lightingSettings;
        lightingSettings.EnvironmentMapPath = DefaultEnvironmentMapPath;
        lightingSettings.IBLIntensity = DefaultEnvironmentIntensity;
        // 解析BRDFを選んだときはニューラルBRDFの重みを読まず、LightingPassは解析BRDFで直接光を評価する。
        if (directBrdf == RasterDirectBrdf::Neural)
        {
            lightingSettings.NeuralBRDFWeightPath = "Data/disney.ns.bin";
        }
        auto lightingPass = MakeUnique<LightingPass>(lightingSettings);
        lightingPass->SetSceneView(this);
        lightingPass->SetRegisterLegacyBridge(false);
        AddPass(std::move(lightingPass));

        // SSR（スクリーンスペース反射、HDR空間で適用）: Lightingが足した環境光の鏡面反射を画面の反射へ置き換え、
        // "SSR.SceneColor" に書く。フォグ・半透明より前に置き、減衰していない照明の色の上で置き換える
        // （後のパスはSSRの出力があればそれへ重ねる）。
        SSRSettings ssrSettings;
        ssrSettings.MaxDistance = 15.0f;
        ssrSettings.Thickness = 0.3f;
        ssrSettings.MaxSteps = 64.0f;
        ssrSettings.Intensity = 0.8f;
        // 粗さ0.3〜0.7の間でなめらかに弱める（しきい値で急に切れると、粗さの近い面の間で反射の有無が段になる）。
        ssrSettings.RoughnessFadeStart = 0.3f;
        ssrSettings.RoughnessFadeEnd = 0.7f;
        AddPass(MakeUnique<SSRPass>(ssrSettings));

        // VolumetricsPass: SSR後のシーンの色を解析高さフォグで合成
        AddPass(MakeUnique<VolumetricsPass>());

        // ForwardPass(TransparentOnly): フォグ後のシーンの色へ半透明をLoad合成
        auto transparentForwardPass = MakeUnique<ForwardPass>(this, sceneRenderer);
        transparentForwardPass->SetTransparentOnly(true);
        transparentForwardPass->SetRegisterOutputs(false);
        AddPass(std::move(transparentForwardPass));

        // VisibilityDebugPass: ID を色にして最後のシーンの色へ書く（--visibility-buffer=debug の検証表示）
        if (visibilityBuffer == VisibilityBufferMode::Debug && visibilityRasterPassPtr)
        {
            auto visibilityDebugPass = MakeUnique<VisibilityDebugPass>();
            visibilityDebugPass->SetRasterPass(visibilityRasterPassPtr);
            AddPass(std::move(visibilityDebugPass));
        }

#if NORVES_ENABLE_STATS
        // GBufferDebugPass: GBuffer の法線・速度・深度を最後のシーンの色へ書く（環境変数 NORVES_GBUFFER_DEBUG。on・off の比較用）。
        // 統計が有効な構成（Debug・RelWithDebInfo）だけ。Release には検証表示を入れない。
        {
            GBufferDebugView gbufferDebugView = GBufferDebugView::Normal;
            if (GBufferDebugPass::TryGetViewFromEnvironment(gbufferDebugView))
            {
                NORVES_LOG_INFO("SceneView", "GBUFFER_DEBUG NORVES_GBUFFER_DEBUG により GBuffer の検証表示を追加する（表示=%u）",
                                static_cast<uint32_t>(gbufferDebugView));
                AddPass(MakeUnique<GBufferDebugPass>(gbufferDebugView));
            }
        }
#endif

        // PostProcessStack: TemporalAA -> AutoExposure -> Bloom -> ToneMapping -> Vignette -> FXAA -> Upscale -> DebugDraw
        auto postProcessStack = MakeUnique<PostProcessStack>();

        // TemporalAA（ライティング・SSR・半透明の後、ブルームの前。既定は無効で、カメラが TAA を選んだ
        // Viewport でだけ有効にし、そのとき FXAA を外す）
        postProcessStack->AddPass(MakeUnique<TemporalAAPass>());
        m_bTemporalAAForced = IsTemporalAAForcedByEnvironment();
        m_bFXAASuppressedByTemporalAA = false;
        if (m_bTemporalAAForced)
        {
            NORVES_LOG_INFO("SceneView", "NORVES_TEMPORAL_AA=1: すべてのカメラに TAA を掛ける");
        }

        // AutoExposure（ブルーム前のHDRシーンカラーの輝度ヒストグラムから露出を測る。露出の方式が Auto のカメラは、この値で次のフレームの露出を決める）
        postProcessStack->AddPass(MakeUnique<AutoExposurePass>());

        // Bloom（ToneMappingの前にHDR空間でブルーム適用。6段の縮小・拡大を、しきい値なしで元の色へ4%混ぜる）
        BloomSettings bloomSettings;
        bloomSettings.Threshold = 0.0f;
        bloomSettings.Intensity = 0.04f;
        bloomSettings.Radius = 1.0f;
        bloomSettings.MipCount = 6;
        auto bloomPass = MakeUnique<BloomPass>(bloomSettings);
        postProcessStack->AddPass(std::move(bloomPass));

        // ToneMapping（HDR→display-linear Rec.709変換 + Color Grading）
        ToneMappingSettings toneMappingSettings;
        toneMappingSettings.Operator = ToneMappingOperator::ACES;
        // Standalone VignettePass owns default SceneView vignette.
        toneMappingSettings.VignetteIntensity = 0.0f;
        auto toneMappingPass = MakeUnique<ToneMappingPass>(toneMappingSettings);
        postProcessStack->AddPass(std::move(toneMappingPass));

        // Vignette（ToneMapping後のdisplay-linear色へ適用）
        auto vignettePass = MakeUnique<VignettePass>();
        postProcessStack->AddPass(std::move(vignettePass));

        // FXAA（アンチエイリアシング、最終パス）
        FXAASettings fxaaSettings;
        fxaaSettings.EdgeThreshold = 0.0312f;
        fxaaSettings.SubpixelQuality = 0.75f;
        auto fxaaPass = MakeUnique<FXAAPass>(fxaaSettings);
        postProcessStack->AddPass(std::move(fxaaPass));

        // Upscale（内部解像度描画時のみ最終画像をスクリーン解像度へ拡大）
        auto upscalePass = MakeUnique<UpscalePass>();
        postProcessStack->AddPass(std::move(upscalePass));

        // DebugDraw（Upscale後の最終解像度のdisplay-linear色へ、ジッタを外したカメラで描き、SceneDepthで深度遮蔽）
        auto debugDrawPass = MakeUnique<DebugDrawPass>();
        postProcessStack->AddPass(std::move(debugDrawPass));

        SetPostProcessStack(std::move(postProcessStack));

        NORVES_LOG_INFO("SceneView",
                        "Deferred pipeline: ShadowMap -> GBuffer -> SSAO -> Lighting -> SSR -> Volumetrics -> Forward(Transparent) -> TemporalAA(optional) -> AutoExposure -> Bloom -> ToneMapping -> Vignette -> FXAA -> Upscale -> DebugDraw");
    }

    void SceneView::SetupPathTracingPipeline(uint32_t samplesPerFrame,
                                             PathTracingTransportScope transportScope,
                                             PathTracingPixelSampling pixelSampling,
                                             PathTracingDebugOutput debugOutput,
                                             uint32_t sampleBatch)
    {
        if (m_PostProcessStack)
        {
            m_PostProcessStack->Shutdown();
            m_PostProcessStack.reset();
        }
        for (auto& pass : m_Passes)
        {
            if (pass && pass->IsInitialized())
            {
                pass->Shutdown();
            }
        }
        m_Passes.clear();
        AddPass(MakeUnique<SkyAtmospherePass>());
        auto pathTracingPass = MakeUnique<PathTracingPass>();
        PathTracingEnvironmentMapSource environmentMap;
        environmentMap.Path = DefaultEnvironmentMapPath;
        environmentMap.LuminanceScaleNits = LightingPassSettings{}.EnvironmentLuminanceScaleNits;
        environmentMap.Intensity = DefaultEnvironmentIntensity;
        pathTracingPass->SetEnvironmentMapSource(environmentMap);
        pathTracingPass->SetSamplesPerFrame(samplesPerFrame);
        pathTracingPass->SetTransportScope(transportScope);
        pathTracingPass->SetPixelSampling(pixelSampling);
        pathTracingPass->SetDebugOutput(debugOutput);
        pathTracingPass->SetSampleBatch(sampleBatch);
        AddPass(std::move(pathTracingPass));

        auto postProcessStack = MakeUnique<PostProcessStack>();
        ToneMappingSettings toneMappingSettings;
        toneMappingSettings.Operator = ToneMappingOperator::ACES;
        toneMappingSettings.VignetteIntensity = 0.0f;
        postProcessStack->AddPass(MakeUnique<ToneMappingPass>(toneMappingSettings));
        SetPostProcessStack(std::move(postProcessStack));
    }

    void SceneView::CullProxies(Viewport *viewport)
    {
        NORVES_STAT_TIME_START(cullingViewport);

        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();

        if (!viewport)
        {
            return;
        }

        const CameraProxy &camera = viewport->GetCamera();
        const Math::Matrix4x4 viewProjection =
            CameraViewConstants::BuildCullingViewProjectionMatrix(camera, viewport->GetAspectRatio());

        Math::Vector3 cameraPosition(
            camera.PositionX,
            camera.PositionY,
            camera.PositionZ);

        Container::UnorderedSet<uint64_t> suppressedMeshComponentIds;

        for (BoardProxy &proxy : m_BoardProxies)
        {
            bool bVisible = proxy.IsValid() &&
                            proxy.Space == BoardSpace::WorldSpace &&
                            HasFlag(camera.CullingMask, proxy.LayerMask);

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
                proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
                bVisible = IsImpostorBoardEligibleForDistance(proxy, proxy.SortDepth);
            }

            if (bVisible)
            {
                if (ShouldSuppressMeshForImpostor(proxy))
                {
                    suppressedMeshComponentIds.insert(proxy.SourceMeshComponentId);
                }

                m_VisibleBoardProxies.push_back(&proxy);
            }
        }

        for (MeshProxy &proxy : m_MeshProxies)
        {
            if (suppressedMeshComponentIds.find(proxy.ComponentId) != suppressedMeshComponentIds.end())
            {
                continue;
            }

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
                proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size() + m_VisibleBoardProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        NORVES_STAT_TIME_END(cullingViewport, m_Stats.CullingTimeMs);
    }

    void SceneView::CullProxies(const ViewportSnapshot &viewport)
    {
        NORVES_STAT_TIME_START(cullingSnapshot);
        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();

        if (!viewport.bHasCamera)
        {
            m_Stats.VisibleProxies = 0;
            m_Stats.CulledProxies = m_Stats.CollectedProxies;
            NORVES_STAT_TIME_END(cullingSnapshot, m_Stats.CullingTimeMs);
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

        const Math::Matrix4x4 viewProjection =
            CameraViewConstants::BuildCullingViewProjectionMatrix(camera, aspectRatio);

        Container::UnorderedSet<uint64_t> suppressedMeshComponentIds;

        for (BoardProxy &proxy : m_BoardProxies)
        {
            bool bVisible = proxy.IsValid() &&
                            proxy.Space == BoardSpace::WorldSpace &&
                            HasFlag(camera.CullingMask, proxy.LayerMask);

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
                proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
                bVisible = IsImpostorBoardEligibleForDistance(proxy, proxy.SortDepth);
            }

            if (bVisible)
            {
                if (ShouldSuppressMeshForImpostor(proxy))
                {
                    suppressedMeshComponentIds.insert(proxy.SourceMeshComponentId);
                }

                m_VisibleBoardProxies.push_back(&proxy);
            }
        }

        for (MeshProxy &proxy : m_MeshProxies)
        {
            if (suppressedMeshComponentIds.find(proxy.ComponentId) != suppressedMeshComponentIds.end())
            {
                continue;
            }

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
                proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size() + m_VisibleBoardProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        NORVES_STAT_TIME_END(cullingSnapshot, m_Stats.CullingTimeMs);
    }

    void SceneView::BatchProxies()
    {
        NORVES_STAT_TIME_START(batching);
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

        NORVES_STAT_TIME_END(batching, m_Stats.BatchingTimeMs);
    }

    void SceneView::AppendWorldBoardDrawCommands()
    {
        for (const BoardProxy *proxy : m_VisibleBoardProxies)
        {
            if (!proxy || !proxy->IsValid() || proxy->Space != BoardSpace::WorldSpace)
            {
                continue;
            }

            GPUSceneInstanceData instanceData;
            FillWorldBoardInstanceData(*proxy, instanceData);
            const uint32_t instanceIndex = static_cast<uint32_t>(m_InstanceData.size());
            m_InstanceData.push_back(instanceData);

            DrawCommand command = DrawCommand::CreateDraw();
            command.Type = DrawCommandType::DrawInstanced;
            command.Draw.PayloadKind = DrawPayloadKind::Board;
            command.Draw.BoardSubtype = proxy->RenderSubtype;
            command.Draw.VertexOffset = 6;
            command.Draw.InstanceCount = 1;
            command.Draw.FirstInstance = instanceIndex;
            command.Draw.InstanceDataOffset = instanceIndex;
            command.Draw.bInstanced = true;
            command.Draw.MaterialBlendMode = proxy->BlendModeProp;
            command.Draw.Texture = proxy->Texture;
            command.Draw.SortDepth = proxy->SortDepth;
            command.Draw.ObjectId = proxy->ObjectId;
            command.Draw.SourceMeshComponentId = proxy->SourceMeshComponentId;
            command.SortKey = proxy->SortKey;
            m_DrawCommands.push_back(command);
        }
    }

    void SceneView::GenerateCommands()
    {
        m_DrawCommands.clear();
        m_OpaqueCommands.clear();
        m_TransparentCommands.clear();
        m_InstanceData.clear();

        // バッチャーからDrawCommandを生成
        m_Batcher.GenerateDrawCommands(m_DrawCommands,
                                       m_InstanceData,
                                       m_bEnableInstancing,
                                       m_MinInstanceCount);

        for (DrawCommand &cmd : m_DrawCommands)
        {
            cmd.CalculateSortKey(cmd.Draw.SortDepth, cmd.Draw.MaterialBlendMode);
        }

        AppendWorldBoardDrawCommands();

        // 不透明と透明を分離してソート
        DrawCommandSorter::SortAndSeparate(m_DrawCommands, m_OpaqueCommands, m_TransparentCommands);

        // ソート
        DrawCommandSorter::Sort(m_OpaqueCommands, DrawCommandSorter::SortMode::FrontToBack);
        DrawCommandSorter::Sort(m_TransparentCommands, DrawCommandSorter::SortMode::BackToFront);

        m_Stats.DrawCommandCount = static_cast<uint32_t>(m_DrawCommands.size());
        m_Stats.InstancedDrawCalls = m_Batcher.GetStats().InstancedDrawCalls;
        m_Stats.SavedDrawCalls = m_Batcher.GetStats().SavedDrawCalls;
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

        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size());

        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();
        for (MeshProxy &proxy : m_MeshProxies)
        {
            if (proxy.bVisible)
            {
                proxy.SortDepth = 0.0f;
                m_VisibleMeshProxies.push_back(&proxy);
            }
        }

        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        BatchProxies();
        GenerateCommands();
    }

    void SceneView::PrepareDrawCommandsForViewport(const ViewportSnapshot &viewport)
    {
        if (!m_bInitialized)
        {
            return;
        }

        m_Stats.TotalObjects = static_cast<uint32_t>(m_MeshProxies.size() + m_BoardProxies.size());
        m_Stats.CollectedProxies = static_cast<uint32_t>(m_MeshProxies.size() + m_BoardProxies.size());

        if (viewport.bHasCamera)
        {
            CullProxies(viewport);
        }
        else
        {
            m_VisibleMeshProxies.clear();
            m_VisibleBoardProxies.clear();
            for (MeshProxy &proxy : m_MeshProxies)
            {
                if (proxy.bVisible)
                {
                    proxy.SortDepth = 0.0f;
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

    bool SceneView::FrustumCull(const BoardProxy &proxy, const Math::Matrix4x4 &viewProjection) const
    {
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

    bool SceneView::DistanceCull(const BoardProxy &proxy, const Math::Vector3 &cameraPosition) const
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

#include "PathTracingPass.inl"
#include "DepthOfFieldPass.inl"
#include "MotionBlurPass.inl"
#include "AutoExposurePass.inl"
#include "TemporalAAPass.inl"

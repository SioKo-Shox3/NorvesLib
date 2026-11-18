#include "Rendering/SceneView.h"
#include "Rendering/CanvasView.h"
#include "Rendering/Viewport.h"
#include "Rendering/ViewRenderContext.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/ShadowMapPass.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/GBufferProgramOverlayPass.h"
#include "Rendering/DecalPass.h"
#include "Rendering/DistortionPass.h"
#include "Rendering/DepthOfFieldPass.h"
#include "Rendering/MotionBlurPass.h"
#include "Rendering/FoliagePass.h"
#include "Rendering/ImpostorGBufferPass.h"
#include "Rendering/GrassPass.h"
#include "Rendering/WorldFieldPass.h"
#include "Rendering/SkyAtmospherePass.h"
#include "Rendering/SkyOcclusionMapPass.h"
#include "Rendering/OverheadHeightMapPass.h"
#include "Rendering/MaterialTileClassifyPass.h"
#include "Rendering/SkinningComputePass.h"
#include "Rendering/VisibilityRasterPass.h"
#include "Rendering/GBufferDebugPass.h"
#include "Rendering/VisibilityResolvePass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/ShadowProbePass.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "Rendering/PathTracingPass.h"
#include "Rendering/VolumetricsPass.h"
#include "Rendering/RainCurtainPass.h"
#include "Rendering/CloudLayerPass.h"
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
#include "Thread/JobSystem.h"
#include <algorithm>
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
            outData.CustomData[3] = proxy.RenderSubtype == BoardRenderSubtype::Impostor
                                        ? proxy.LODCrossfadeValue
                                        : proxy.LODSwitchDistance;
            for (float &value : outData.ShadingParams)
            {
                value = -1.0f;
            }
        }

        void FillLayerImpostorInstanceData(const MegaGeometryProxy& proxy,
                                           const MegaMeshImpostorDesc& desc,
                                           const Math::Vector3& center,
                                           float halfSize,
                                           float crossfadeValue,
                                           GPUSceneInstanceData& outData)
        {
            Math::Matrix4x4 world = proxy.WorldTransform;
            world.SetTranslationRow(center);
            Math::Matrix4x4 previousWorld = proxy.PreviousWorldTransform;
            previousWorld.SetTranslationRow(Math::MatrixUtils::TransformPointRowVector(previousWorld, desc.BoundsCenter));
            Math::MatrixUtils::CopyToShaderData(world, outData.World);
            Math::MatrixUtils::CopyToShaderData(previousWorld, outData.PreviousWorld);

            for (float& value : outData.NormalMatrix)
            {
                value = 0.0f;
            }
            // 焼きの正射影は境界球の中心から半径の範囲なので、一辺は半径の 2 倍。
            outData.NormalMatrix[0] = 2.0f * halfSize;
            outData.NormalMatrix[1] = 2.0f * halfSize;
            outData.NormalMatrix[2] = 0.5f;
            outData.NormalMatrix[3] = 0.5f;
            outData.NormalMatrix[8] = 1.0f;
            outData.NormalMatrix[9] = 1.0f;
            outData.NormalMatrix[10] = static_cast<float>(desc.AxisCellCountX);
            outData.NormalMatrix[11] = static_cast<float>(desc.AxisCellCountY);

            constexpr float colorScale = 1.0f / 255.0f;
            outData.ObjectColor[0] = static_cast<float>(proxy.Tint & 0xFFu) * colorScale;
            outData.ObjectColor[1] = static_cast<float>((proxy.Tint >> 8) & 0xFFu) * colorScale;
            outData.ObjectColor[2] = static_cast<float>((proxy.Tint >> 16) & 0xFFu) * colorScale;
            outData.ObjectColor[3] = 1.0f;
            outData.CustomData[0] = static_cast<float>(desc.CellResolution);
            outData.CustomData[1] = static_cast<float>(desc.AtlasWidth);
            outData.CustomData[2] = static_cast<float>(desc.AtlasHeight);
            outData.CustomData[3] = crossfadeValue;
            for (float& value : outData.ShadingParams)
            {
                value = -1.0f;
            }
        }

        // BillboardComponent::BuildBoardProxy と同じ式で、板の基点から各頂点を包む境界を作る。
        void BuildTransientBoardBounds(BoardProxy& proxy)
        {
            const Math::Vector3 translation = proxy.WorldTransform.GetTranslationRow();
            proxy.WorldBounds.CenterX = translation.x;
            proxy.WorldBounds.CenterY = translation.y;
            proxy.WorldBounds.CenterZ = translation.z;

            const Math::Vector3 scale = Math::MatrixUtils::ExtractScale(proxy.WorldTransform);
            const float width = std::abs(proxy.SizeWorld.x) * scale.x;
            const float height = std::abs(proxy.SizeWorld.y) * scale.y;
            const float left = -proxy.Pivot.x * width;
            const float right = (1.0f - proxy.Pivot.x) * width;
            const float bottom = -proxy.Pivot.y * height;
            const float top = (1.0f - proxy.Pivot.y) * height;
            const float maxX = std::max(std::abs(left), std::abs(right));
            const float maxY = std::max(std::abs(bottom), std::abs(top));
            proxy.WorldBounds.Radius = std::sqrt(maxX * maxX + maxY * maxY);
        }

        // 非数が同じ欄に残る更新でも索引を作り直し続けない。
        bool SameLayerImpostorBounds(const BoundingSphere& lhs, const BoundingSphere& rhs)
        {
            const auto same = [](float a, float b)
            {
                return a == b || (std::isnan(a) && std::isnan(b));
            };
            return same(lhs.CenterX, rhs.CenterX) && same(lhs.CenterY, rhs.CenterY) &&
                   same(lhs.CenterZ, rhs.CenterZ) && same(lhs.Radius, rhs.Radius);
        }

        float ComputeDistanceToBounds(const BoundingSphere &bounds,
                                      const Math::Vector3 &cameraPosition)
        {
            const float dx = bounds.CenterX - cameraPosition.x;
            const float dy = bounds.CenterY - cameraPosition.y;
            const float dz = bounds.CenterZ - cameraPosition.z;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        SceneViewScreenSizeContext MakeScreenSizeContext(const CameraProxy& camera)
        {
            SceneViewScreenSizeContext context;
            if (camera.Projection == ProjectionType::Orthographic)
            {
                context.bOrthographic = true;
                context.OrthoHeight = camera.OrthoHeight;
            }
            else
            {
                context.TanHalfFovY = std::tan(camera.FieldOfView * (Math::Constants::PI / 180.0f) * 0.5f);
            }
            return context;
        }

        float ComputeBoundsScreenSize(const BoundingSphere& bounds, float distance,
                                      const SceneViewScreenSizeContext& context)
        {
            if (context.bOrthographic)
            {
                return ComputeOrthographicScreenSize(bounds.Radius, context.OrthoHeight);
            }
            return ComputePerspectiveScreenSize(bounds.Radius, distance, context.TanHalfFovY);
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
        m_CullParallelSettings = settings.CullParallel;
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
        m_ImpostorBatches.clear();

        // Batcherをクリア
        m_Batcher.Clear();

        m_RenderExtensions.ShutdownShadowCasters();
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
            const uint8_t previousLODSelectionState = m_MeshProxies[indexIt->second].LODSelectionState;
            const LODCrossfadeState previousLODCrossfade = m_MeshProxies[indexIt->second].LODCrossfade;
            const float previousLODCrossfadeValue = m_MeshProxies[indexIt->second].LODCrossfadeValue;
            m_MeshProxies[indexIt->second] = proxy;
            m_MeshProxies[indexIt->second].LODSelectionState = previousLODSelectionState;
            m_MeshProxies[indexIt->second].LODCrossfade = previousLODCrossfade;
            m_MeshProxies[indexIt->second].LODCrossfadeValue = previousLODCrossfadeValue;
            NoteProxyChanged(m_MeshChangedIndices, indexIt->second, m_MeshProxies.size());
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_MeshProxies.size());
        m_MeshProxies.push_back(proxy);
        m_MeshProxyIndex[proxy.ComponentId] = index;
        NoteProxyChanged(m_MeshChangedIndices, index, m_MeshProxies.size());
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
            NoteProxyChanged(m_MeshChangedIndices, removeIndex, m_MeshProxies.size());
        }

        NoteProxyChanged(m_MeshChangedIndices, lastIndex, m_MeshProxies.size());
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
                NoteProxyChanged(m_MeshChangedIndices, index, m_MeshProxies.size());
            }
            NoteProxyChanged(m_MeshChangedIndices, lastIndex, m_MeshProxies.size());
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
            const uint8_t previousLODSelectionState = m_BoardProxies[indexIt->second].LODSelectionState;
            const LODCrossfadeState previousLODCrossfade = m_BoardProxies[indexIt->second].LODCrossfade;
            const float previousLODCrossfadeValue = m_BoardProxies[indexIt->second].LODCrossfadeValue;
            m_BoardProxies[indexIt->second] = proxy;
            m_BoardProxies[indexIt->second].LODSelectionState = previousLODSelectionState;
            m_BoardProxies[indexIt->second].LODCrossfade = previousLODCrossfade;
            m_BoardProxies[indexIt->second].LODCrossfadeValue = previousLODCrossfadeValue;
            m_VisibleBoardProxies.clear();
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_BoardProxies.size());
        m_BoardProxies.push_back(proxy);
        m_BoardProxyIndex[proxy.ComponentId] = index;
        m_VisibleBoardProxies.clear();
    }

    void SceneView::SetTransientBoardProxies(const Container::VariableArray<BoardProxy>& proxies)
    {
        m_TransientBoardProxies.clear();
        for (const BoardProxy& proxy : proxies)
        {
            if (!proxy.IsValid() || proxy.Space != BoardSpace::WorldSpace)
            {
                continue;
            }

            BoardProxy transientProxy = proxy;
            if (transientProxy.WorldBounds.Radius <= 0.0f)
            {
                BuildTransientBoardBounds(transientProxy);
            }
            m_TransientBoardProxies.push_back(transientProxy);
        }
        m_VisibleBoardProxies.clear();
    }

    void DistributeTransientBoardProxies(const Container::VariableArray<BoardProxy>& frameProxies,
                                         SceneView* sceneView,
                                         CanvasView* canvasView)
    {
        Container::VariableArray<BoardProxy> worldProxies;
        Container::VariableArray<BoardProxy> screenProxies;
        for (const BoardProxy& proxy : frameProxies)
        {
            if (proxy.Space == BoardSpace::WorldSpace)
            {
                worldProxies.push_back(proxy);
            }
            else if (proxy.Space == BoardSpace::ScreenSpace)
            {
                screenProxies.push_back(proxy);
            }
        }

        if (sceneView)
        {
            sceneView->SetTransientBoardProxies(worldProxies);
        }
        if (canvasView)
        {
            canvasView->SetTransientBoardProxies(screenProxies);
        }
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
            NoteProxyChanged(m_SkinnedMeshChangedIndices, indexIt->second, m_SkinnedMeshProxies.size());
            return;
        }
        const uint32_t index = static_cast<uint32_t>(m_SkinnedMeshProxies.size());
        m_SkinnedMeshProxies.push_back(proxy);
        m_SkinnedMeshProxyIndex[proxy.ComponentId] = index;
        NoteProxyChanged(m_SkinnedMeshChangedIndices, index, m_SkinnedMeshProxies.size());
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
            NoteProxyChanged(m_SkinnedMeshChangedIndices, removeIndex, m_SkinnedMeshProxies.size());
        }
        NoteProxyChanged(m_SkinnedMeshChangedIndices, lastIndex, m_SkinnedMeshProxies.size());
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
                NoteProxyChanged(m_SkinnedMeshChangedIndices, index, m_SkinnedMeshProxies.size());
            }
            NoteProxyChanged(m_SkinnedMeshChangedIndices, lastIndex, m_SkinnedMeshProxies.size());
            m_SkinnedMeshProxies.pop_back();
        }
    }

    void SceneView::UpdateSkinnedMeshProxy(const SkinnedMeshProxy& proxy)
    {
        auto indexIt = m_SkinnedMeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_SkinnedMeshProxyIndex.end())
        {
            m_SkinnedMeshProxies[indexIt->second] = proxy;
            NoteProxyChanged(m_SkinnedMeshChangedIndices, indexIt->second, m_SkinnedMeshProxies.size());
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
            NoteProxyChanged(m_LightChangedIndices, indexIt->second, m_LightProxies.size());
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_LightProxies.size());
        m_LightProxies.push_back(proxy);
        m_LightProxyIndex[proxy.LightId] = index;
        NoteProxyChanged(m_LightChangedIndices, index, m_LightProxies.size());
    }

    bool SceneView::SetMegaMeshImpostor(MegaGeometry::MegaMeshHandle mesh, const MegaMeshImpostorDesc& desc)
    {
        if (!mesh.IsValid() || !desc.IsValid())
        {
            return false;
        }
        m_MegaMeshImpostors[mesh.Id] = desc;
        m_bLayerImpostorCellsDirty = true;
        return true;
    }

    void SceneView::ClearMegaMeshImpostor(MegaGeometry::MegaMeshHandle mesh)
    {
        if (m_MegaMeshImpostors.erase(mesh.Id) != 0)
        {
            m_bLayerImpostorCellsDirty = true;
        }
    }

    void SceneView::ClearMegaMeshImpostors()
    {
        if (!m_MegaMeshImpostors.empty())
        {
            m_MegaMeshImpostors.clear();
            m_bLayerImpostorCellsDirty = true;
        }
    }

    const MegaMeshImpostorDesc* SceneView::FindMegaMeshImpostor(MegaGeometry::MegaMeshHandle mesh) const
    {
        const auto it = m_MegaMeshImpostors.find(mesh.Id);
        return it != m_MegaMeshImpostors.end() ? &it->second : nullptr;
    }

    void SceneView::AddMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        if (!proxy.IsValid())
        {
            return;
        }

        if (IsMegaScatterProxyKey(proxy.ObjectId))
        {
            m_bLayerImpostorCellsDirty = true;
        }

        auto indexIt = m_MegaGeometryProxyIndex.find(proxy.ObjectId);
        if (indexIt != m_MegaGeometryProxyIndex.end())
        {
            MegaGeometryProxy& current = m_MegaGeometryProxies[indexIt->second];
            if (!IsMegaScatterProxyKey(proxy.ObjectId) && current.ComponentId != proxy.ComponentId)
            {
                m_bMegaComponentIndexMayBeStale = true;
            }
            const bool bHiddenByImpostor = current.bHiddenByImpostor;
            const float crossfadeValue = current.LODCrossfadeValue;
            current = proxy;
            current.bRayTracing = proxy.bRayTracing && IsMegaGeometryRayTracing(proxy.ComponentId);
            current.bHiddenByImpostor = bHiddenByImpostor;
            current.LODCrossfadeValue = crossfadeValue;
            NoteMegaChanged(indexIt->second);
            return;
        }

        const uint32_t index = static_cast<uint32_t>(m_MegaGeometryProxies.size());
        m_MegaGeometryProxies.push_back(proxy);
        m_MegaGeometryProxies.back().bRayTracing = proxy.bRayTracing && IsMegaGeometryRayTracing(proxy.ComponentId);
        m_MegaGeometryProxyIndex[proxy.ObjectId] = index;
        if (!IsMegaScatterProxyKey(proxy.ObjectId))
        {
            m_bMegaComponentIndexMayBeStale = true;
        }
        NoteMegaChanged(index);
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
            NoteProxyChanged(m_LightChangedIndices, removeIndex, m_LightProxies.size());
        }

        NoteProxyChanged(m_LightChangedIndices, lastIndex, m_LightProxies.size());
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
                NoteProxyChanged(m_LightChangedIndices, index, m_LightProxies.size());
            }
            NoteProxyChanged(m_LightChangedIndices, lastIndex, m_LightProxies.size());
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
        if (IsMegaScatterProxyKey(objectId))
        {
            m_bLayerImpostorCellsDirty = true;
            // 同じ鍵が次のカリング前に再利用されても、以前の個体の状態を継がせない。
            m_LayerImpostorStates.erase(objectId);
            m_SuppressedLayerMegaIds.erase(objectId);
            m_NextSuppressedLayerMegaIds.erase(objectId);
            m_LayerMegaCrossfadeValues.erase(objectId);
            m_NextLayerMegaCrossfadeValues.erase(objectId);
        }
        else
        {
            m_bMegaComponentIndexMayBeStale = true;
        }
        if (removeIndex != lastIndex && IsMegaScatterProxyKey(m_MegaGeometryProxies[lastIndex].ObjectId))
        {
            m_bLayerImpostorCellsDirty = true;
        }
        m_MegaGeometryProxyIndex.erase(indexIt);

        if (removeIndex != lastIndex)
        {
            m_MegaGeometryProxies[removeIndex] = m_MegaGeometryProxies[lastIndex];
            m_MegaGeometryProxyIndex[m_MegaGeometryProxies[removeIndex].ObjectId] = removeIndex;
            NoteMegaChanged(removeIndex);
        }

        m_MegaGeometryProxies.pop_back();
    }

    void SceneView::RemoveStaleMegaGeometryProxies(const Container::UnorderedSet<uint64_t> &liveObjectIds)
    {
        uint32_t index = 0;
        while (index < m_MegaGeometryProxies.size())
        {
            const uint64_t objectId = m_MegaGeometryProxies[index].ObjectId;
            // 散布の軽い行の proxy は散布が公開と取り下げを持つ（World の行の生存集合には載らない）
            if (IsMegaScatterProxyKey(objectId) || liveObjectIds.find(objectId) != liveObjectIds.end())
            {
                ++index;
                continue;
            }

            const uint32_t lastIndex = static_cast<uint32_t>(m_MegaGeometryProxies.size() - 1);
            m_bMegaComponentIndexMayBeStale = true;
            if (index != lastIndex && IsMegaScatterProxyKey(m_MegaGeometryProxies[lastIndex].ObjectId))
            {
                m_bLayerImpostorCellsDirty = true;
            }
            m_MegaGeometryProxyIndex.erase(objectId);
            if (index != lastIndex)
            {
                m_MegaGeometryProxies[index] = m_MegaGeometryProxies[lastIndex];
                m_MegaGeometryProxyIndex[m_MegaGeometryProxies[index].ObjectId] = index;
                NoteMegaChanged(index);
            }
            m_MegaGeometryProxies.pop_back();
        }
    }

    void SceneView::UpdateMeshProxy(const MeshProxy &proxy)
    {
        auto indexIt = m_MeshProxyIndex.find(proxy.ComponentId);
        if (indexIt != m_MeshProxyIndex.end())
        {
            const uint8_t previousLODSelectionState = m_MeshProxies[indexIt->second].LODSelectionState;
            const LODCrossfadeState previousLODCrossfade = m_MeshProxies[indexIt->second].LODCrossfade;
            const float previousLODCrossfadeValue = m_MeshProxies[indexIt->second].LODCrossfadeValue;
            m_MeshProxies[indexIt->second] = proxy;
            m_MeshProxies[indexIt->second].LODSelectionState = previousLODSelectionState;
            m_MeshProxies[indexIt->second].LODCrossfade = previousLODCrossfade;
            m_MeshProxies[indexIt->second].LODCrossfadeValue = previousLODCrossfadeValue;
            NoteProxyChanged(m_MeshChangedIndices, indexIt->second, m_MeshProxies.size());
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
            NoteProxyChanged(m_LightChangedIndices, indexIt->second, m_LightProxies.size());
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
            MegaGeometryProxy& current = m_MegaGeometryProxies[indexIt->second];
            if (IsMegaScatterProxyKey(proxy.ObjectId) &&
                (!SameLayerImpostorBounds(current.WorldBounds, proxy.WorldBounds) ||
                 current.MegaMeshHandle != proxy.MegaMeshHandle || current.bVisible != proxy.bVisible))
            {
                m_bLayerImpostorCellsDirty = true;
            }
            if (!IsMegaScatterProxyKey(proxy.ObjectId) && current.ComponentId != proxy.ComponentId)
            {
                m_bMegaComponentIndexMayBeStale = true;
            }
            const bool bHiddenByImpostor = current.bHiddenByImpostor;
            const float crossfadeValue = current.LODCrossfadeValue;
            current = proxy;
            current.bRayTracing = proxy.bRayTracing && IsMegaGeometryRayTracing(proxy.ComponentId);
            current.bHiddenByImpostor = bHiddenByImpostor;
            current.LODCrossfadeValue = crossfadeValue;
            NoteMegaChanged(indexIt->second);
            return;
        }

        AddMegaGeometryProxy(proxy);
    }

    void SceneView::SetMegaGeometryRayTracing(uint64_t componentId, bool bRayTracing)
    {
        if (bRayTracing)
        {
            m_RayTracingExcludedMegaComponentIds.erase(componentId);
        }
        else
        {
            m_RayTracingExcludedMegaComponentIds.insert(componentId);
        }

        for (uint32_t index = 0; index < static_cast<uint32_t>(m_MegaGeometryProxies.size()); ++index)
        {
            MegaGeometryProxy& proxy = m_MegaGeometryProxies[index];
            if (proxy.ComponentId == componentId && proxy.bRayTracing != bRayTracing)
            {
                proxy.bRayTracing = bRayTracing;
                NoteMegaChanged(index);
            }
        }
    }

    bool SceneView::IsMegaGeometryRayTracing(uint64_t componentId) const
    {
        return m_RayTracingExcludedMegaComponentIds.find(componentId) == m_RayTracingExcludedMegaComponentIds.end();
    }

    void SceneView::NoteMegaChanged(uint32_t index)
    {
        if (m_bMegaFullSync)
        {
            return;
        }
        m_MegaChangedIndices.push_back(index);
        if (m_MegaChangedIndices.size() > m_MegaGeometryProxies.size() * 4u + 4096u)
        {
            std::sort(m_MegaChangedIndices.begin(), m_MegaChangedIndices.end());
            m_MegaChangedIndices.erase(std::unique(m_MegaChangedIndices.begin(), m_MegaChangedIndices.end()),
                                       m_MegaChangedIndices.end());
        }
    }

    const MegaGeometryProxy *SceneView::FindMegaGeometryProxy(uint64_t objectId) const
    {
        const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
        return indexIt != m_MegaGeometryProxyIndex.end() ? &m_MegaGeometryProxies[indexIt->second] : nullptr;
    }

    namespace
    {
        bool SameMegaTransformBits(const Math::Matrix4x4 &lhs, const Math::Matrix4x4 &rhs)
        {
            return std::memcmp(&lhs, &rhs, sizeof(Math::Matrix4x4)) == 0;
        }
    } // namespace

    void SceneView::PublishMegaGeometryProxy(const MegaGeometryProxy &proxy)
    {
        MegaGeometryProxy next = proxy;
        const auto indexIt = m_MegaGeometryProxyIndex.find(proxy.ObjectId);
        next.PreviousWorldTransform = indexIt != m_MegaGeometryProxyIndex.end()
                                          ? m_MegaGeometryProxies[indexIt->second].WorldTransform
                                          : next.WorldTransform;
        UpdateMegaGeometryProxy(next);
        if (!SameMegaTransformBits(next.PreviousWorldTransform, next.WorldTransform))
        {
            m_MegaUnsettledObjects.push_back(next.ObjectId);
        }
    }

    void SceneView::BeginMegaGeometrySync()
    {
        for (const uint64_t objectId : m_MegaUnsettledObjects)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
            if (indexIt == m_MegaGeometryProxyIndex.end())
            {
                continue;
            }
            MegaGeometryProxy &proxy = m_MegaGeometryProxies[indexIt->second];
            if (!SameMegaTransformBits(proxy.PreviousWorldTransform, proxy.WorldTransform))
            {
                proxy.PreviousWorldTransform = proxy.WorldTransform;
                NoteMegaChanged(indexIt->second);
            }
        }
        m_MegaUnsettledObjects.clear();
    }

    void SceneView::TakeMegaGeometryChangedIndices(Container::VariableArray<uint32_t> &outIndices)
    {
        outIndices.clear();
        if (m_MegaChangedIndices.empty())
        {
            return;
        }
        std::sort(m_MegaChangedIndices.begin(), m_MegaChangedIndices.end());
        m_MegaChangedIndices.erase(std::unique(m_MegaChangedIndices.begin(), m_MegaChangedIndices.end()),
                                   m_MegaChangedIndices.end());
        outIndices.assign(m_MegaChangedIndices.begin(), m_MegaChangedIndices.end());
        m_MegaChangedIndices.clear();
    }

    void SceneView::NoteProxyChanged(Container::VariableArray<uint32_t> &changedIndices, uint32_t index, size_t arraySize)
    {
        changedIndices.push_back(index);
        if (changedIndices.size() > arraySize * 4u + 4096u)
        {
            std::sort(changedIndices.begin(), changedIndices.end());
            changedIndices.erase(std::unique(changedIndices.begin(), changedIndices.end()), changedIndices.end());
        }
    }

    void SceneView::TakeProxyChangedIndices(Container::VariableArray<uint32_t> &changedIndices,
                                            Container::VariableArray<uint32_t> &outIndices)
    {
        outIndices.clear();
        if (changedIndices.empty())
        {
            return;
        }
        std::sort(changedIndices.begin(), changedIndices.end());
        changedIndices.erase(std::unique(changedIndices.begin(), changedIndices.end()), changedIndices.end());
        outIndices.assign(changedIndices.begin(), changedIndices.end());
        changedIndices.clear();
    }

    void SceneView::TakeMeshProxyChangedIndices(Container::VariableArray<uint32_t> &outIndices)
    {
        TakeProxyChangedIndices(m_MeshChangedIndices, outIndices);
    }

    void SceneView::TakeSkinnedMeshProxyChangedIndices(Container::VariableArray<uint32_t> &outIndices)
    {
        TakeProxyChangedIndices(m_SkinnedMeshChangedIndices, outIndices);
    }

    void SceneView::TakeLightProxyChangedIndices(Container::VariableArray<uint32_t> &outIndices)
    {
        TakeProxyChangedIndices(m_LightChangedIndices, outIndices);
    }

    void SceneView::ClearAllProxies()
    {
        m_LayerImpostorCells.clear();
        m_ImpostorShadowCasters.clear();
        m_ImpostorShadowAtlases.clear();
        m_LayerImpostorStates.clear();
        m_SuppressedLayerMegaIds.clear();
        m_NextSuppressedLayerMegaIds.clear();
        m_LayerMegaCrossfadeValues.clear();
        m_NextLayerMegaCrossfadeValues.clear();
        m_LayerImpostorInstances.clear();
        m_bLayerImpostorCellsDirty = true;
        m_MeshProxies.clear();
        m_ImpostorMeshCrossfadeValues.clear();
        m_LODCrossfadeProxies.clear();
        m_SkinnedMeshProxies.clear();
        m_BoardProxies.clear();
        m_TransientBoardProxies.clear();
        m_MegaGeometryProxies.clear();
        m_LightProxies.clear();
        m_MeshProxyIndex.clear();
        m_SkinnedMeshProxyIndex.clear();
        m_BoardProxyIndex.clear();
        m_MegaGeometryProxyIndex.clear();
        m_MegaComponentObjectIds.clear();
        m_bMegaComponentIndexMayBeStale = false;
        m_SuppressedMegaBoardIds.clear();
        m_NextSuppressedMegaBoardIds.clear();
        m_ImpostorMegaCrossfadeValues.clear();
        m_NextImpostorMegaCrossfadeValues.clear();
        m_MegaChangedIndices.clear();
        m_MegaUnsettledObjects.clear();
        m_LightProxyIndex.clear();
        m_MeshChangedIndices.clear();
        m_SkinnedMeshChangedIndices.clear();
        m_LightChangedIndices.clear();
        m_VisibleMeshProxies.clear();
        m_VisibleBoardProxies.clear();
        AdvanceSceneGeneration();
    }

    void SceneView::ClearMegaGeometryProxies()
    {
        m_LayerImpostorCells.clear();
        m_ImpostorShadowCasters.clear();
        m_ImpostorShadowAtlases.clear();
        m_LayerImpostorStates.clear();
        m_SuppressedLayerMegaIds.clear();
        m_NextSuppressedLayerMegaIds.clear();
        m_LayerMegaCrossfadeValues.clear();
        m_NextLayerMegaCrossfadeValues.clear();
        m_LayerImpostorInstances.clear();
        m_bLayerImpostorCellsDirty = true;
        m_MegaGeometryProxies.clear();
        m_MegaGeometryProxyIndex.clear();
        m_MegaComponentObjectIds.clear();
        m_bMegaComponentIndexMayBeStale = false;
        m_SuppressedMegaBoardIds.clear();
        m_NextSuppressedMegaBoardIds.clear();
        m_ImpostorMegaCrossfadeValues.clear();
        m_NextImpostorMegaCrossfadeValues.clear();
        m_MegaChangedIndices.clear();
        m_MegaUnsettledObjects.clear();
        AdvanceSceneGeneration();
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
        m_RenderExtensions.ApplyPending();
        m_GBufferPrograms.ApplyPending();

        const uint32_t viewId = context.CurrentViewport ? context.CurrentViewport->ViewId : UINT32_MAX;
        const uint32_t viewportId = context.CurrentViewport ? context.CurrentViewport->ViewportId : UINT32_MAX;
        context.PhysicalLighting.Begin(context.FrameNumber, viewId, viewportId);
        context.PhysicalLighting.ConfigureRTGI(context.RTGICapability,
                                               context.bRTGIEnabled,
                                               context.bRTGITLASAvailable,
                                               context.SceneRevision,
                                               context.LightRevision);
        context.SkyAtmosphere.Reset();
        context.SkyOcclusion.Reset();
        context.CloudShadow.Reset();
        context.bTemporalAAApplied = false;
        context.TemporalAAJitterIndex = 0u;
        context.OverheadHeightMap.Invalidate();
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
                context.TemporalAAJitterIndex = static_cast<uint32_t>(
                    temporalAAPass->GetFrameJitterIndex() % GBufferAlphaHashPhaseCount);
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
        context.bTemporalAAApplied = bApplyTemporalAA;
        SetTemporalAAApplied(bApplyTemporalAA, viewportId);

        // パスチェーンが存在すれば基底クラスのパスベース描画を実行
        if (GetPassCount() > 0)
        {
            // DrawCommandはGameThreadのGenerateDrawCommands()でスナップショット済み
            // context.SnapshotDrawCommands等から各パスが参照する
            m_RenderExtensions.CollectShadowCasters(context);
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
                                          VisibilityBufferMode visibilityBuffer,
                                          SwRasterMode swRaster,
                                          float swRasterMaxPixels)
    {
        const bool bVisibilityBuffer = IsVisibilityBufferActive(visibilityBuffer);
        // On のときは、ビジビリティバッファの解決が GBuffer を書く（GBufferPass・MegaGeometryPass は GBuffer の描画を止める）。
        // Debug は今の GBuffer の描画を残したまま、ID の検証表示だけを足す
        const bool bVisibilityResolve = visibilityBuffer == VisibilityBufferMode::On;
        // ソフトウェアラスタの 64bit のバッファ・埋め・合流は、材質の解決を使う On のときだけ作る（Debug は GBuffer が先に MegaGeometry を描くので、
        // ソフトに回せない）。振り分けの要求は --sw-raster=on なら出し、使えない理由（Debug・64bit アトミックが無いなど）は
        // MegaGeometryPass が SW_RASTER_FALLBACK として 1 回だけログへ出す
        const bool bSwRaster = bVisibilityResolve && swRaster == SwRasterMode::On;
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
        ShadowMapPass* shadowMapPassPtr = shadowMapPass.get();
        shadowMapPass->SetSceneView(this);
        shadowMapPass->SetSceneRenderer(sceneRenderer);
        shadowMapPass->SetRegisterLegacyBridge(false);
        shadowMapPass->SetShadowMethod(m_ShadowMethod);
        AddPass(std::move(shadowMapPass));

        // 真上からの雨の遮蔽を深度画像へ描く。
        AddPass(MakeUnique<OverheadHeightMapPass>());

        // NeuralMaterialDecodePass: ニューラルマテリアルの事前デコード（Compute）
        auto neuralDecodePass = MakeUnique<NeuralMaterialDecodePass>();
        neuralDecodePass->SetSceneView(this);
        neuralDecodePass->SetSceneRenderer(sceneRenderer);
        AddPass(std::move(neuralDecodePass));

        // SkinningComputePass: スキニングの今・前のフレームの頂点を計算シェーダーで作る。
        // 今の GBuffer の経路は頂点シェーダーのスキニングのままなので、ビジビリティバッファを使うときか、
        // 太陽の影を VSM で描く（変形した頂点を影の描画が読む）ときまで無効にしておく。
        auto skinningComputePass = MakeUnique<SkinningComputePass>();
        skinningComputePass->SetEnabled(bVisibilityBuffer || m_ShadowMethod == ShadowMethod::Vsm);
        skinningComputePass->SetShadowCasterOutput(m_ShadowMethod == ShadowMethod::Vsm);
        SkinningComputePass *skinningComputePassPtr = skinningComputePass.get();
        AddPass(std::move(skinningComputePass));

        // カメラに追従する世界フィールドを、ジオメトリの描画より先に更新する。
        AddPass(MakeUnique<WorldFieldPass>());

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
        megaGeometryPass->SetSwRasterBinning(swRaster == SwRasterMode::On, swRasterMaxPixels);
        MegaGeometryPass *megaGeometryPassPtr = megaGeometryPass.get();
        AddPass(std::move(megaGeometryPass));

        // VisibilityRasterPass: MegaGeometry のクラスタと、プログラム指定の無い不透明のメッシュ・スキニングの塊を、
        // VisBuffer.Id と GBuffer.Depth へ描く（on では GBuffer の描画の代わりに、debug では今の GBuffer の描画に加えて）。
        // --visibility-buffer=on|debug のときだけ足す。
        VisibilityRasterPass *visibilityRasterPassPtr = nullptr;
        if (bVisibilityBuffer)
        {
            auto visibilityRasterPass = MakeUnique<VisibilityRasterPass>();
            visibilityRasterPass->SetMegaGeometryPass(megaGeometryPassPtr);
            visibilityRasterPass->SetSkinningComputePass(skinningComputePassPtr);
            visibilityRasterPass->SetSwRasterEnabled(bSwRaster);
            visibilityRasterPassPtr = visibilityRasterPass.get();
            AddPass(std::move(visibilityRasterPass));
        }

        // MaterialTileClassifyPass: VisBuffer.Id から、材質ごとのタイルの一覧と間接 dispatch の引数を作る。
        // 材質の解決（次のパス）が読むので、解決より前に足す。解決を使う --visibility-buffer=on のときだけ有効にする
        // （off は足さず、debug は足しても無効のまま。既定の描画は変えない）。
        MaterialTileClassifyPass *materialTileClassifyPassPtr = nullptr;
        if (visibilityRasterPassPtr)
        {
            auto materialTileClassifyPass = MakeUnique<MaterialTileClassifyPass>();
            materialTileClassifyPass->SetRasterPass(visibilityRasterPassPtr);
            materialTileClassifyPass->SetEnabled(bVisibilityResolve);
            materialTileClassifyPassPtr = materialTileClassifyPass.get();
            AddPass(std::move(materialTileClassifyPass));
        }

        // VisibilityResolvePass: VisBuffer.Id から三角形を引いて、GBuffer の Albedo・Normal・Velocity を書く（--visibility-buffer=on）。
        // 分類のパスの引数・一覧で、材質ごとに 1 回ずつ間接 dispatch する（分類を使えないフレームは画面全体の直接 dispatch）。
        // 使えないとき（装置の非対応・ID のラスタや解決のパイプラインが無い・計算スキニングのパイプラインが無い）は何も宣言せず、
        // GBufferPass・MegaGeometryPass も描画を止めない。判定はこのパスに問い合わせる（上の 2 つへ渡す参照）。
        if (visibilityRasterPassPtr && bVisibilityResolve)
        {
            auto visibilityResolvePass = MakeUnique<VisibilityResolvePass>();
            visibilityResolvePass->SetRasterPass(visibilityRasterPassPtr);
            visibilityResolvePass->SetSkinningComputePass(skinningComputePassPtr);
            visibilityResolvePass->SetClassifyPass(materialTileClassifyPassPtr);
            // 撮影の AOV（--capture-aov）は、三角形の面の幾何の法線も GBufferAovPass へ渡す
            visibilityResolvePass->SetWriteGeometricNormal(m_bCaptureAovEnabled);
            visibilityRasterPassPtr->SetResolvePass(visibilityResolvePass.get());
            skinningComputePassPtr->SetResolvePass(visibilityResolvePass.get());
            gbufferPassPtr->SetVisibilityResolvePass(visibilityResolvePass.get());
            megaGeometryPassPtr->SetVisibilityResolvePass(visibilityResolvePass.get());
            // 2パスの遮蔽の HZB は、GBuffer の描画を止めている間は ID のラスタの深度から作る（記録をラスタの Execute へ移す）
            megaGeometryPassPtr->SetVisibilityRasterPass(visibilityRasterPassPtr);
            AddPass(std::move(visibilityResolvePass));

            // 重ね描きの対象となる材質（プログラム・Masked・両面・頂点アニメ）は草と同じく解決せず、解決の後の GBuffer と深度へ重ねて描く。
            // ID のラスタの深度には載らず、2 パスの遮蔽の HZB の遮蔽物にはならない。
            auto programOverlayPass = MakeUnique<GBufferProgramOverlayPass>();
            programOverlayPass->SetGBufferPass(gbufferPassPtr);
            AddPass(std::move(programOverlayPass));
        }

        // DecalPass: 画面空間の遅延デカールを解決・GBuffer の描画の後、草の前に重ねる。
        // 色と法線の RGB だけを混ぜ、Material と各 A は変えない。既定では草に描かない。
        // 一覧が空のフレームは何も宣言しない。
        AddPass(MakeUnique<DecalPass>());

        // FoliagePass: 草・低木の株を、ビジビリティの解決（または GBuffer の描画）の後の GBuffer と深度へ直接重ねて描く。
        // 頂点シェーダーで風に揺らすので解決の経路には載せず、SSAO・影の印付け・照明より前に置いて影を受ける。
        // 束が無いフレームは何も宣言しない（既定の描画は変えない）
        AddPass(MakeUnique<FoliagePass>());

        // インポスターを、解決・GBuffer の描画の後の GBuffer と深度へ重ねて描く。
        // SSAO・照明の前に置き、照明・影の受け・霧・濡れに参加させる。束が無いフレームは何も宣言しない。
        AddPass(MakeUnique<ImpostorGBufferPass>());

        // GrassPass: 草の場からタイルごとのブレードと間接描画の引数を生成する。
        auto grassPass = MakeUnique<GrassPass>();
        grassPass->SetMegaGeometryPass(megaGeometryPassPtr);
        AddPass(std::move(grassPass));

        // SSAOPass: GBufferの深度・法線から画面空間AO（GTAO）を計算。半径は世界の長さ（m）で、
        // 球・岩の接地部や軒下（数十cm〜1 m）を拾い、部屋の大きさの壁全体は遮蔽にしない。
        SSAOSettings ssaoSettings;
        ssaoSettings.Radius = 1.0f;
        ssaoSettings.Intensity = 1.0f;
        auto ssaoPass = MakeUnique<SSAOPass>(ssaoSettings);
        AddPass(std::move(ssaoPass));

        // 空を遮る形状からクリップマップを更新し、照明へ公開する。
        AddPass(MakeUnique<SkyOcclusionMapPass>());

        // SkyAtmospherePass: 同一空スナップショットからLUTと太陽ディスクを生成
        auto skyAtmospherePass = MakeUnique<SkyAtmospherePass>();
        AddPass(std::move(skyAtmospherePass));
        // 雲の影のマスクを霧と照明より先に更新して公開する。
        AddPass(MakeUnique<CloudShadowMaskPass>());

        // VirtualShadowMapPass: 太陽の VSM の物理ページのプールとページの表などを作り、名前で公開し、影を落とす手続きメッシュと
        // スキニング（SkinningComputePass が変形した頂点）を物理ページへ描く（--shadow-method=vsm のときだけ）。
        // 深度が確定した後（ビジビリティの解決・GBuffer・MegaGeometry の後）・照明の前に置く。照明はまだ CSM のまま。
        // 装置が対応しないとき・プールを取れないときは、パスが資源を作らず VSM_FALLBACK を出して CSM で描く
        if (m_ShadowMethod == ShadowMethod::Vsm)
        {
            auto virtualShadowMapPass = MakeUnique<VirtualShadowMapPass>(m_VsmPoolPages);
            virtualShadowMapPass->SetSkinningComputePass(skinningComputePassPtr);
            virtualShadowMapPass->SetRenderExtensionRegistry(&m_RenderExtensions);
            // MegaGeometry の投影物（bCastShadow のインスタンス）を、展開の前に段ごとにカリングする（主の経路の入力を読み取りだけで使う）
            virtualShadowMapPass->SetMegaGeometryPass(megaGeometryPassPtr);
            virtualShadowMapPass->SetPointShadowMethod(m_PointShadowMethod);
            // 影の測定（--shadow-probe）は CSM と VSM の両方を読んで比べるので、CSM へ MegaGeometry を描き続ける
            if (!m_bShadowProbeEnabled)
            {
                shadowMapPassPtr->SetSunVsmPass(virtualShadowMapPass.get());
            }
            AddPass(std::move(virtualShadowMapPass));
        }
        else if (m_PointShadowMethod == PointShadowMethod::Vsm)
        {
            // 点光源の VSM は太陽の VSM が前提。太陽が CSM のときはキューブのまま描く
            NORVES_LOG_INFO("VirtualShadowMapPass", "VSM_FALLBACK reason=%s", VirtualShadowMap::PointRequiresVsmReasonName);
        }

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

#if NORVES_ENABLE_STATS
        // ShadowProbePass: ワールドに固定した点の太陽の可視度を毎フレーム測り、影の揺れと細かさの数を出す（--shadow-probe）。
        // 照明の後に置く。統計が有効な構成（Debug・RelWithDebInfo）だけで、Release には入れない。
        if (m_bShadowProbeEnabled)
        {
            auto shadowProbePass = MakeUnique<ShadowProbePass>();
            shadowProbePass->SetPointOnly(m_bShadowProbePointOnly);
            AddPass(std::move(shadowProbePass));
        }
#endif

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

        // CloudLayerPass: SSR後のシーンの色の空の画素へ雲の層を重ねる（シーンの雲の層が有効なときだけ。フォグはその上に重なる）
        AddPass(MakeUnique<CloudLayerPass>());

        // VolumetricsPass: SSR後のシーンの色を解析高さフォグで合成
        AddPass(MakeUnique<VolumetricsPass>());
        // RainCurtainPass: 霧の後のシーンの色へ遠景の雨のカーテンを重ねる（雨量 0・環境が無効なフレームは何も記録しない）
        AddPass(MakeUnique<RainCurtainPass>());

        // ForwardPass(TransparentOnly): フォグ後のシーンの色へ半透明をLoad合成
        auto transparentForwardPass = MakeUnique<ForwardPass>(this, sceneRenderer);
        transparentForwardPass->SetTransparentOnly(true);
        transparentForwardPass->SetRegisterOutputs(false);
        AddPass(std::move(transparentForwardPass));

        // 半透明の後・TAA の前にシーンの色をずらす。源が無いフレームは何も宣言しない。
        AddPass(MakeUnique<DistortionPass>());

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
        // GBufferAovPass: 撮影の AOV（GBuffer の生の値を浮動小数の 2 枚へ。--capture-aov）。シーンの色へは書かない
        if (m_bCaptureAovEnabled)
        {
            NORVES_LOG_INFO("SceneView", "CAPTURE_AOV --capture-aov により GBuffer の生の値を書くパスを追加する");
            AddPass(MakeUnique<GBufferAovPass>());
        }
        // AtmosphereProbePass: 撮影の大気の書き出し（決めた視線の高さフォグと空気遠近。Game の --atmosphere-dump が
        // 環境変数 NORVES_ATMOSPHERE_DUMP で渡す）。シーンの色へは書かない
        if (AtmosphereProbePass::IsRequestedByEnvironment())
        {
            NORVES_LOG_INFO("SceneView", "ATMOSPHERE_DUMP --atmosphere-dump により大気の書き出しのパスを追加する");
            AddPass(MakeUnique<AtmosphereProbePass>());
        }
        // CloudProbePass: 撮影の雲の書き出し（決めた画素の視線の雲。Game の --cloud-dump が環境変数 NORVES_CLOUD_DUMP で渡す）。
        // シーンの色へは書かない
        if (CloudProbePass::IsRequestedByEnvironment())
        {
            NORVES_LOG_INFO("SceneView", "CLOUD_DUMP --cloud-dump により雲の書き出しのパスを追加する");
            AddPass(MakeUnique<CloudProbePass>());
        }
#endif

        // 被写界深度は半透明を合成した後のSceneColorへ掛ける。カメラのピント距離が0なら働かない。
        AddPass(Container::MakeUnique<DepthOfFieldPass>());
        // 動きぼけは被写界深度の後のSceneColorへ掛ける。シャッター時間が0（既定）なら働かない。
        AddPass(Container::MakeUnique<MotionBlurPass>());

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
        m_RenderExtensions.ReinsertAll();

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
        m_RenderExtensions.NotePathTracingPipeline();
    }

    const MegaGeometryProxy* SceneView::FindImpostorMegaProxy(const BoardProxy& board, bool& bRebuiltIndex)
    {
        const auto directIt = m_MegaGeometryProxyIndex.find(board.ObjectId);
        if (directIt != m_MegaGeometryProxyIndex.end() && !IsMegaScatterProxyKey(board.ObjectId))
        {
            const MegaGeometryProxy& proxy = m_MegaGeometryProxies[directIt->second];
            if (proxy.ComponentId == board.SourceMeshComponentId)
            {
                return &proxy;
            }
        }

        const auto findCached = [this, &board]() -> const MegaGeometryProxy*
        {
            const auto componentIt = m_MegaComponentObjectIds.find(board.SourceMeshComponentId);
            if (componentIt == m_MegaComponentObjectIds.end())
            {
                return nullptr;
            }
            const auto indexIt = m_MegaGeometryProxyIndex.find(componentIt->second);
            if (indexIt == m_MegaGeometryProxyIndex.end() || IsMegaScatterProxyKey(componentIt->second))
            {
                return nullptr;
            }
            const MegaGeometryProxy& proxy = m_MegaGeometryProxies[indexIt->second];
            return proxy.ComponentId == board.SourceMeshComponentId ? &proxy : nullptr;
        };

        if (const MegaGeometryProxy* proxy = findCached())
        {
            return proxy;
        }

        if (!m_bMegaComponentIndexMayBeStale || bRebuiltIndex)
        {
            return nullptr;
        }

        m_MegaComponentObjectIds.clear();
        for (const MegaGeometryProxy& proxy : m_MegaGeometryProxies)
        {
            if (!IsMegaScatterProxyKey(proxy.ObjectId))
            {
                m_MegaComponentObjectIds[proxy.ComponentId] = proxy.ObjectId;
            }
        }
        m_bMegaComponentIndexMayBeStale = false;
        bRebuiltIndex = true;
        return findCached();
    }

    void SceneView::ApplyImpostorMegaSuppression()
    {
        const auto applyState = [this](uint32_t index, bool bHidden, float value)
        {
            MegaGeometryProxy& proxy = m_MegaGeometryProxies[index];
            if (proxy.bHiddenByImpostor != bHidden || proxy.LODCrossfadeValue != value)
            {
                proxy.bHiddenByImpostor = bHidden;
                proxy.LODCrossfadeValue = value;
                NoteMegaChanged(index);
            }
        };

        for (const auto& [objectId, boardId] : m_SuppressedMegaBoardIds)
        {
            if (m_NextSuppressedMegaBoardIds.find(objectId) != m_NextSuppressedMegaBoardIds.end() ||
                m_NextImpostorMegaCrossfadeValues.find(objectId) != m_NextImpostorMegaCrossfadeValues.end())
            {
                continue;
            }
            const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
            if (indexIt == m_MegaGeometryProxyIndex.end())
            {
                continue;
            }
            if (!m_MegaGeometryProxies[indexIt->second].bHiddenByImpostor)
            {
                continue;
            }
            applyState(indexIt->second, false, 0.0f);

            // 元が戻る変更は次フレームのパケットで届くため、このフレームは板を残す。
            const auto boardIt = m_BoardProxyIndex.find(boardId);
            if (boardIt != m_BoardProxyIndex.end())
            {
                BoardProxy* board = &m_BoardProxies[boardIt->second];
                if (std::find(m_VisibleBoardProxies.begin(), m_VisibleBoardProxies.end(), board) == m_VisibleBoardProxies.end())
                {
                    m_VisibleBoardProxies.push_back(board);
                }
            }
        }

        for (const auto& entry : m_NextSuppressedMegaBoardIds)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(entry.first);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, true, 0.0f);
            }
        }
        for (const auto& entry : m_ImpostorMegaCrossfadeValues)
        {
            if (m_NextImpostorMegaCrossfadeValues.find(entry.first) != m_NextImpostorMegaCrossfadeValues.end() ||
                m_NextSuppressedMegaBoardIds.find(entry.first) != m_NextSuppressedMegaBoardIds.end())
            {
                continue;
            }
            const auto indexIt = m_MegaGeometryProxyIndex.find(entry.first);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, false, 0.0f);
            }
        }
        for (const auto& entry : m_NextImpostorMegaCrossfadeValues)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(entry.first);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, false, entry.second);
            }
        }
        m_SuppressedMegaBoardIds.swap(m_NextSuppressedMegaBoardIds);
        m_NextSuppressedMegaBoardIds.clear();
        m_ImpostorMegaCrossfadeValues.swap(m_NextImpostorMegaCrossfadeValues);
        m_NextImpostorMegaCrossfadeValues.clear();
    }

    void SceneView::RebuildLayerImpostorCells()
    {
        m_LayerImpostorCells.clear();
        if (!m_MegaMeshImpostors.empty())
        {
            Container::UnorderedMap<uint64_t, Container::UnorderedMap<uint64_t, uint32_t>> cellIndices;
            for (uint32_t index = 0; index < static_cast<uint32_t>(m_MegaGeometryProxies.size()); ++index)
            {
                const MegaGeometryProxy& proxy = m_MegaGeometryProxies[index];
                if (!IsMegaScatterProxyKey(proxy.ObjectId) || !proxy.MegaMeshHandle.IsValid() || !proxy.bVisible ||
                    m_MegaMeshImpostors.find(proxy.MegaMeshHandle.Id) == m_MegaMeshImpostors.end())
                {
                    continue;
                }

                const BoundingSphere& bounds = proxy.WorldBounds;
                const uint64_t cellKey = MakeLayerImpostorCellKey(bounds.CenterX, bounds.CenterZ);
                auto& meshCells = cellIndices[proxy.MegaMeshHandle.Id];
                auto cellIt = meshCells.find(cellKey);
                if (cellIt == meshCells.end())
                {
                    const uint32_t cellIndex = static_cast<uint32_t>(m_LayerImpostorCells.size());
                    cellIt = meshCells.emplace(cellKey, cellIndex).first;
                    m_LayerImpostorCells.emplace_back();
                    m_LayerImpostorCells.back().MeshId = proxy.MegaMeshHandle.Id;
                }
                LayerImpostorCell& cell = m_LayerImpostorCells[cellIt->second];
                cell.ProxyIndices.push_back(index);

                const float minX = bounds.CenterX - bounds.Radius;
                const float minY = bounds.CenterY - bounds.Radius;
                const float minZ = bounds.CenterZ - bounds.Radius;
                const float maxX = bounds.CenterX + bounds.Radius;
                const float maxY = bounds.CenterY + bounds.Radius;
                const float maxZ = bounds.CenterZ + bounds.Radius;
                if (!std::isfinite(bounds.Radius) || bounds.Radius < 0.0f ||
                    !std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(minZ) ||
                    !std::isfinite(maxX) || !std::isfinite(maxY) || !std::isfinite(maxZ))
                {
                    // 境界が不正な組は帯を決めず、個体ごとに選ぶ。
                    cell.bValidBounds = false;
                    continue;
                }
                cell.Bounds.Expand(minX, minY, minZ);
                cell.Bounds.Expand(maxX, maxY, maxZ);
                cell.MinRadius = std::min(cell.MinRadius, bounds.Radius);
                cell.MaxRadius = std::max(cell.MaxRadius, bounds.Radius);
            }
        }

        for (auto it = m_LayerImpostorStates.begin(); it != m_LayerImpostorStates.end();)
        {
            if (m_MegaGeometryProxyIndex.find(it->first) == m_MegaGeometryProxyIndex.end())
            {
                it = m_LayerImpostorStates.erase(it);
            }
            else
            {
                ++it;
            }
        }
        m_bLayerImpostorCellsDirty = false;
    }

#if NORVES_ENABLE_LOGGING
    void SceneView::LogLayerImpostorStats(const LayerImpostorLogStats& stats)
    {
        const LayerImpostorLogStats& last = m_LastLayerImpostorLogStats;
        if (stats.Groups != last.Groups || stats.Mixed != last.Mixed || stats.Mesh != last.Mesh ||
            stats.Impostor != last.Impostor || stats.Crossfading != last.Crossfading || stats.Shadow != last.Shadow)
        {
            NORVES_LOG_INFO("SceneView", "LAYER_IMPOSTOR groups=%u mixed=%u mesh=%u impostor=%u crossfading=%u shadow=%u",
                            stats.Groups, stats.Mixed, stats.Mesh, stats.Impostor, stats.Crossfading, stats.Shadow);
            m_LastLayerImpostorLogStats = stats;
        }
    }
#endif

    void SceneView::CullLayerImpostors(const Math::Vector3& cameraPosition,
                                       const SceneViewScreenSizeContext& context,
                                       const Math::Matrix4x4& viewProjection)
    {
        m_LayerImpostorInstances.clear();
        if (m_MegaMeshImpostors.empty() && m_SuppressedLayerMegaIds.empty() && m_LayerMegaCrossfadeValues.empty())
        {
#if NORVES_ENABLE_LOGGING
            LogLayerImpostorStats({});
#endif
            return;
        }
        if (m_bLayerImpostorCellsDirty)
        {
            RebuildLayerImpostorCells();
        }

#if NORVES_ENABLE_LOGGING
        LayerImpostorLogStats stats;
        stats.Groups = static_cast<uint32_t>(m_LayerImpostorCells.size());
#endif
        for (const LayerImpostorCell& cell : m_LayerImpostorCells)
        {
            const auto descIt = m_MegaMeshImpostors.find(cell.MeshId);
            if (descIt == m_MegaMeshImpostors.end())
            {
                continue;
            }
            const MegaMeshImpostorDesc& desc = descIt->second;
            LayerImpostorBand band = LayerImpostorBand::Mixed;
            if (cell.bValidBounds && std::isfinite(cameraPosition.x) &&
                std::isfinite(cameraPosition.y) && std::isfinite(cameraPosition.z))
            {
                const auto nearest = [](float position, float minValue, float maxValue)
                {
                    return std::max({minValue - position, 0.0f, position - maxValue});
                };
                const auto farthest = [](float position, float minValue, float maxValue)
                {
                    return std::max(std::abs(position - minValue), std::abs(position - maxValue));
                };
                const BoundingBox& bounds = cell.Bounds;
                const float minDistance = std::hypot(nearest(cameraPosition.x, bounds.MinX, bounds.MaxX),
                                                     nearest(cameraPosition.y, bounds.MinY, bounds.MaxY),
                                                     nearest(cameraPosition.z, bounds.MinZ, bounds.MaxZ));
                const float maxDistance = std::hypot(farthest(cameraPosition.x, bounds.MinX, bounds.MaxX),
                                                     farthest(cameraPosition.y, bounds.MinY, bounds.MaxY),
                                                     farthest(cameraPosition.z, bounds.MinZ, bounds.MaxZ));
                band = ClassifyLayerImpostorCell(minDistance, maxDistance, cell.MinRadius, cell.MaxRadius,
                                                 desc.SwitchScreenSize, context.bOrthographic,
                                                 context.TanHalfFovY, context.OrthoHeight);
            }
#if NORVES_ENABLE_LOGGING
            if (band == LayerImpostorBand::Mixed)
            {
                ++stats.Mixed;
            }
#endif
            const float thresholds[2] = {desc.SwitchScreenSize, 0.0f};
            for (const uint32_t index : cell.ProxyIndices)
            {
                const MegaGeometryProxy& proxy = m_MegaGeometryProxies[index];
                LayerImpostorSelectionState& state = m_LayerImpostorStates[proxy.ObjectId];
                uint32_t selected = band == LayerImpostorBand::Impostor ? 1u : 0u;
                if (band == LayerImpostorBand::Mixed)
                {
                    const float distance = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
                    const float screenSize = ComputeBoundsScreenSize(proxy.WorldBounds, distance, context);
                    selected = SelectLODByScreenSizeWithHysteresis(screenSize, thresholds, 2, state.Selection);
                }
                AdvanceLODCrossfade(state.Crossfade, selected, m_LODCrossfadeCullDeltaSeconds,
                                    DEFAULT_LOD_CROSSFADE_SECONDS, m_bLODCrossfadeCullSnap);
                state.Selection = state.Crossfade.ToLevel;
#if NORVES_ENABLE_LOGGING
                if (state.Selection == 0)
                {
                    ++stats.Mesh;
                }
                else
                {
                    ++stats.Impostor;
                }
                if (state.Crossfade.IsActive() && state.Crossfade.Progress > 0.0f)
                {
                    ++stats.Crossfading;
                }
#endif
                if (state.Crossfade.IsActive() && state.Crossfade.Progress > 0.0f)
                {
                    const bool bIncomingImpostor = state.Crossfade.ToLevel == 1;
                    const float value = MakeLODCrossfadeValue(state.Crossfade.Progress, !bIncomingImpostor);
                    m_NextLayerMegaCrossfadeValues[proxy.ObjectId] =
                        DecodeLODCrossfadeFromTint(PackLODCrossfadeIntoTint(0u, value));
                }
                else
                {
                    // 遷移を始めた瞬間は、それまで表示していた段だけを保つ。
                    const uint8_t shown = state.Crossfade.IsActive()
                                              ? state.Crossfade.FromLevel
                                              : state.Crossfade.ToLevel;
                    if (shown == 1)
                    {
                        m_NextSuppressedLayerMegaIds.insert(proxy.ObjectId);
                    }
                }
            }
        }

        const auto appendInstance = [&](uint64_t objectId, float crossfadeValue, bool bHidden)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
            if (indexIt == m_MegaGeometryProxyIndex.end())
            {
                return;
            }
            const MegaGeometryProxy& proxy = m_MegaGeometryProxies[indexIt->second];
            const MegaMeshImpostorDesc* desc = FindMegaMeshImpostor(proxy.MegaMeshHandle);
            LayerImpostorPlacement placement;
            if (!desc || !ComputeLayerImpostorPlacement(proxy.WorldTransform, *desc, placement))
            {
                return;
            }
            LayerImpostorInstance instance;
            instance.ObjectId = objectId;
            instance.ProxyIndex = indexIt->second;
            instance.CrossfadeValue = crossfadeValue;
            instance.bVisible = true;
            if (m_bEnableFrustumCulling)
            {
                const Math::ClipSpaceSphereBounds clipBounds =
                    Math::MatrixUtils::TransformSphereToClipSpace(viewProjection, placement.Center, placement.HalfSize);
                instance.bVisible = Math::MatrixUtils::IntersectsClipSpace(clipBounds, Math::ClipSpaceDepthRange::ZeroToOne);
            }
            if (bHidden && desc->bCastShadow && proxy.bCastShadow)
            {
                const float distance = std::hypot(placement.Center.x - cameraPosition.x,
                                                  placement.Center.y - cameraPosition.y,
                                                  placement.Center.z - cameraPosition.z);
                instance.bCastShadow = desc->ShadowDistance == 0.0f ||
                                       distance <= desc->ShadowDistance + placement.HalfSize;
            }
#if NORVES_ENABLE_LOGGING
            if (instance.bCastShadow)
            {
                ++stats.Shadow;
            }
#endif
            m_LayerImpostorInstances.push_back(instance);
        };

        // 今の GPU に届いている前回の Mega の状態に板を合わせる。
        for (const auto& entry : m_LayerMegaCrossfadeValues)
        {
            if (entry.second != 0.0f)
            {
                appendInstance(entry.first, -entry.second, false);
            }
        }
        for (const uint64_t objectId : m_SuppressedLayerMegaIds)
        {
            const auto fadeIt = m_LayerMegaCrossfadeValues.find(objectId);
            if (fadeIt == m_LayerMegaCrossfadeValues.end() || fadeIt->second == 0.0f)
            {
                appendInstance(objectId, 0.0f, true);
            }
        }

        const auto applyState = [this](uint32_t index, bool bHidden, float value)
        {
            MegaGeometryProxy& proxy = m_MegaGeometryProxies[index];
            if (proxy.bHiddenByImpostor != bHidden || proxy.LODCrossfadeValue != value)
            {
                proxy.bHiddenByImpostor = bHidden;
                proxy.LODCrossfadeValue = value;
                NoteMegaChanged(index);
            }
        };
        const auto restoreIfUnused = [&](uint64_t objectId)
        {
            if (m_NextSuppressedLayerMegaIds.find(objectId) != m_NextSuppressedLayerMegaIds.end() ||
                m_NextLayerMegaCrossfadeValues.find(objectId) != m_NextLayerMegaCrossfadeValues.end())
            {
                return;
            }
            const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, false, 0.0f);
            }
        };
        for (const uint64_t objectId : m_SuppressedLayerMegaIds)
        {
            restoreIfUnused(objectId);
        }
        for (const auto& entry : m_LayerMegaCrossfadeValues)
        {
            restoreIfUnused(entry.first);
        }
        for (const uint64_t objectId : m_NextSuppressedLayerMegaIds)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(objectId);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, true, 0.0f);
            }
        }
        for (const auto& entry : m_NextLayerMegaCrossfadeValues)
        {
            const auto indexIt = m_MegaGeometryProxyIndex.find(entry.first);
            if (indexIt != m_MegaGeometryProxyIndex.end())
            {
                applyState(indexIt->second, false, entry.second);
            }
        }
        m_SuppressedLayerMegaIds.swap(m_NextSuppressedLayerMegaIds);
        m_NextSuppressedLayerMegaIds.clear();
        m_LayerMegaCrossfadeValues.swap(m_NextLayerMegaCrossfadeValues);
        m_NextLayerMegaCrossfadeValues.clear();
#if NORVES_ENABLE_LOGGING
        LogLayerImpostorStats(stats);
#endif
        // 複数の Viewport では、最後にカリングした Viewport の判定が残る。
    }

    void SceneView::CullBoardProxy(BoardProxy& proxy,
                                   const CameraProxy& camera,
                                   const Math::Matrix4x4& viewProjection,
                                   const Math::Vector3& cameraPosition,
                                   const SceneViewScreenSizeContext& context,
                                   Container::UnorderedSet<uint64_t>& suppressedMeshComponentIds,
                                   bool& bRebuiltMegaIndex)
    {
        bool bVisible = proxy.IsValid() &&
                        proxy.Space == BoardSpace::WorldSpace &&
                        HasFlag(camera.CullingMask, proxy.LayerMask);

        bool bDrawBoard = bVisible;
        const MegaGeometryProxy* megaSource = nullptr;
        bool bCrossfading = false;
        proxy.LODCrossfadeValue = 0.0f;
        if (bVisible && proxy.RenderSubtype == BoardRenderSubtype::Impostor)
        {
            const bool bHasMeshSource = m_MeshProxyIndex.find(proxy.SourceMeshComponentId) != m_MeshProxyIndex.end();
            if (!bHasMeshSource)
            {
                megaSource = FindImpostorMegaProxy(proxy, bRebuiltMegaIndex);
            }
            uint32_t selected = 0;
            proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
            if (proxy.LODSwitchScreenSize > 0.0f)
            {
                const float thresholds[2] = {proxy.LODSwitchScreenSize, 0.0f};
                const float screenSize = ComputeBoundsScreenSize(proxy.WorldBounds, proxy.SortDepth, context);
                selected = SelectLODByScreenSizeWithHysteresis(
                    screenSize, thresholds, 2, proxy.LODSelectionState);
            }
            else if (proxy.LODSwitchDistance > 0.0f)
            {
                if (context.bOrthographic && proxy.SortDepth <= 0.0f)
                {
                    selected = 0;
                }
                else
                {
                    const float threshold = context.bOrthographic
                                                ? 1.0f
                                                : ComputePerspectiveScreenSize(proxy.WorldBounds.Radius,
                                                                               proxy.LODSwitchDistance,
                                                                               context.TanHalfFovY);
                    const float screenSize = context.bOrthographic
                                                 ? proxy.LODSwitchDistance / proxy.SortDepth
                                                 : ComputeBoundsScreenSize(proxy.WorldBounds, proxy.SortDepth, context);
                    const float thresholds[2] = {threshold, 0.0f};
                    selected = SelectLODByScreenSizeWithHysteresis(
                        screenSize, thresholds, 2, proxy.LODSelectionState);
                }
            }
            else
            {
                selected = 1;
            }
            // 遷移中は選び直さず、入口は元の段だけを値0で描く。
            AdvanceLODCrossfade(proxy.LODCrossfade, selected, m_LODCrossfadeCullDeltaSeconds,
                                DEFAULT_LOD_CROSSFADE_SECONDS, m_bLODCrossfadeCullSnap);
            proxy.LODSelectionState = proxy.LODCrossfade.ToLevel;
            bCrossfading = proxy.LODCrossfade.IsActive() && proxy.LODCrossfade.Progress > 0.0f;
            if (bCrossfading)
            {
                const bool bIncomingBoard = proxy.LODCrossfade.ToLevel == 1;
                proxy.LODCrossfadeValue = MakeLODCrossfadeValue(proxy.LODCrossfade.Progress, bIncomingBoard);
                const float sourceValue = MakeLODCrossfadeValue(proxy.LODCrossfade.Progress, !bIncomingBoard);
                // 板の錐台の結果によらず、元にも相補の値を渡す。
                if (bHasMeshSource)
                {
                    m_ImpostorMeshCrossfadeValues[proxy.SourceMeshComponentId] = sourceValue;
                }
                else if (megaSource)
                {
                    // Mega は色味の A で 1/127 刻みに丸まるため、板にも同じ値を渡す。
                    // 画素ごとの相補を保つため、保存前に同じ丸めを適用する。
                    m_NextImpostorMegaCrossfadeValues[megaSource->ObjectId] =
                        DecodeLODCrossfadeFromTint(PackLODCrossfadeIntoTint(0u, sourceValue));
                }
                bVisible = true;
            }
            else
            {
                const uint8_t shown = proxy.LODCrossfade.IsActive()
                                          ? proxy.LODCrossfade.FromLevel
                                          : proxy.LODCrossfade.ToLevel;
                bVisible = shown == 1;
            }

            bDrawBoard = bVisible;
            if (megaSource)
            {
                // 今回の GPU に届いている前回の元の状態に、板の描画だけを合わせる。
                bDrawBoard = false;
                proxy.LODCrossfadeValue = 0.0f;
                if (!m_ImpostorMegaCrossfadeValues.empty() || !m_SuppressedMegaBoardIds.empty())
                {
                    if (!m_ImpostorMegaCrossfadeValues.empty())
                    {
                        const auto valueIt = m_ImpostorMegaCrossfadeValues.find(megaSource->ObjectId);
                        if (valueIt != m_ImpostorMegaCrossfadeValues.end() && valueIt->second != 0.0f)
                        {
                            bDrawBoard = true;
                            proxy.LODCrossfadeValue = -valueIt->second;
                        }
                    }
                    if (!bDrawBoard && !m_SuppressedMegaBoardIds.empty())
                    {
                        bDrawBoard = m_SuppressedMegaBoardIds.find(megaSource->ObjectId) != m_SuppressedMegaBoardIds.end();
                    }
                }
            }
        }

        // 次回の抑制と今回の描画には、同じ板の錐台・距離の結果を使う。
        bool bPassesCulling = bVisible || bDrawBoard;
        if (m_bEnableFrustumCulling && bPassesCulling)
        {
            bPassesCulling = FrustumCull(proxy, viewProjection);
        }

        if (m_bEnableDistanceCulling && bPassesCulling)
        {
            bPassesCulling = DistanceCull(proxy, cameraPosition);
        }

        if (bVisible && bPassesCulling)
        {
            if (proxy.RenderSubtype != BoardRenderSubtype::Impostor)
            {
                proxy.SortDepth = ComputeDistanceToBounds(proxy.WorldBounds, cameraPosition);
            }
            if (!bCrossfading && ShouldSuppressMeshForImpostor(proxy))
            {
                suppressedMeshComponentIds.insert(proxy.SourceMeshComponentId);
                if (megaSource)
                {
                    m_NextSuppressedMegaBoardIds[megaSource->ObjectId] = proxy.ComponentId;
                }
            }
        }
        if (bDrawBoard && bPassesCulling)
        {
            m_VisibleBoardProxies.push_back(&proxy);
        }
    }

    void SceneView::CullMeshProxyRange(size_t begin, size_t end,
                                      const Math::Matrix4x4& viewProjection,
                                      const Math::Vector3& cameraPosition,
                                      const SceneViewScreenSizeContext& context,
                                      Container::VariableArray<MeshProxy*>& outVisible,
                                      Container::VariableArray<MeshProxy>& outLODCopies)
    {
        outLODCopies.clear();
        const size_t firstVisible = outVisible.size();
        const auto& suppressedMeshComponentIds = m_SuppressedMeshComponentIds;
        const auto& impostorCrossfadeValues = m_ImpostorMeshCrossfadeValues;
        for (size_t index = begin; index < end; ++index)
        {
            MeshProxy& proxy = m_MeshProxies[index];
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
                uint32_t level = 0;
                if (proxy.LODLevelCount > 1)
                {
                    const float screenSize = ComputeBoundsScreenSize(proxy.WorldBounds, proxy.SortDepth, context);
                    level = ResolveMeshLODLevel(screenSize, &proxy.LODScreenSizeThresholds[0],
                                                proxy.LODLevelCount, proxy.ForcedLODLevel,
                                                proxy.LODSelectionState);
                    if (level >= 1 && !proxy.LODChain[level - 1].MeshHandle.IsValid())
                    {
                        level = 0;
                    }
                }
                const bool bInImpostorTransition = impostorCrossfadeValues.find(proxy.ComponentId) != impostorCrossfadeValues.end();
                // インポスターとの遷移と段の遷移を重ねず、遷移中は選び直さない。
                AdvanceLODCrossfade(proxy.LODCrossfade, level, m_LODCrossfadeCullDeltaSeconds,
                                    DEFAULT_LOD_CROSSFADE_SECONDS, m_bLODCrossfadeCullSnap || bInImpostorTransition);
                proxy.LODSelectionState = proxy.LODCrossfade.ToLevel;
                proxy.LODCrossfadeValue = 0.0f;
                outVisible.push_back(&proxy);
            }
        }

        const size_t visibleEnd = outVisible.size();
        size_t copyCount = 0;
        size_t extraVisibleCount = 0;
        for (size_t index = firstVisible; index < visibleEnd; ++index)
        {
            const LODCrossfadeState& state = outVisible[index]->LODCrossfade;
            if (state.IsActive() && state.Progress > 0.0f)
            {
                copyCount += 2;
                ++extraVisibleCount;
            }
            else if ((state.IsActive() ? state.FromLevel : state.ToLevel) != 0)
            {
                ++copyCount;
            }
        }

        // 全ての写しの領域を先に確保し、可視一覧へ渡すポインタの再確保による無効化を防ぐ。
        outLODCopies.reserve(copyCount);
        outVisible.reserve(visibleEnd + extraVisibleCount);
        const auto makeCopy = [&outLODCopies](const MeshProxy& proxy, uint8_t level, float value, bool bCastShadow) -> MeshProxy*
        {
            MeshProxy copy = proxy;
            if (level != 0)
            {
                const MeshLODChainLevel& lod = proxy.LODChain[level - 1];
                copy.MeshHandle = lod.MeshHandle;
                copy.SubMeshes = lod.SubMeshes;
                copy.SubMeshCount = lod.SubMeshCount;
            }
            copy.LODLevel = level;
            copy.LODCrossfadeValue = value;
            copy.bCastShadow = bCastShadow;
            outLODCopies.push_back(copy);
            return &outLODCopies.back();
        };
        for (size_t index = firstVisible; index < visibleEnd; ++index)
        {
            MeshProxy& proxy = *outVisible[index];
            const LODCrossfadeState& state = proxy.LODCrossfade;
            if (state.IsActive() && state.Progress > 0.0f)
            {
                outVisible[index] = makeCopy(proxy, state.FromLevel,
                                            MakeLODCrossfadeValue(state.Progress, false), false);
                outVisible.push_back(makeCopy(proxy, state.ToLevel,
                                             MakeLODCrossfadeValue(state.Progress, true), proxy.bCastShadow));
                continue;
            }

            // 入口は出る側の1段だけを値0で描く。
            const uint8_t level = state.IsActive() ? state.FromLevel : state.ToLevel;
            const auto impostorIt = impostorCrossfadeValues.find(proxy.ComponentId);
            const float value = impostorIt != impostorCrossfadeValues.end() ? impostorIt->second : 0.0f;
            if (level == 0)
            {
                proxy.LODCrossfadeValue = value;
            }
            else
            {
                outVisible[index] = makeCopy(proxy, level, value, proxy.bCastShadow);
            }
        }
    }

    void SceneView::CullMeshProxies(const Math::Matrix4x4& viewProjection,
                                    const Math::Vector3& cameraPosition,
                                    const SceneViewScreenSizeContext& context,
                                    Container::VariableArray<MeshProxy>& outLODCopies)
    {
        const size_t n = m_MeshProxies.size();
        const size_t chunkSize = std::max(m_CullParallelSettings.ChunkSize, size_t{1});
        // 加算による桁あふれを避けて切り上げる
        const size_t chunkCount = n / chunkSize + (n % chunkSize != 0 ? 1 : 0);
        if (n < m_CullParallelSettings.MinProxyCount || chunkCount <= 1)
        {
            CullMeshProxyRange(0, n, viewProjection, cameraPosition, context, m_VisibleMeshProxies, outLODCopies);
            return;
        }

        if (m_CullChunkVisible.size() < chunkCount)
        {
            m_CullChunkVisible.resize(chunkCount);
        }
        if (m_CullChunkLODCopies.size() < chunkCount)
        {
            m_CullChunkLODCopies.resize(chunkCount);
        }

        NorvesLib::Thread::JobSystem::Get().ParallelFor(chunkCount, 1, [&](size_t chunk)
        {
            auto& out = m_CullChunkVisible[chunk];
            out.clear();
            const size_t begin = chunk * chunkSize;
            const size_t end = begin + std::min(chunkSize, n - begin);
            CullMeshProxyRange(begin, end, viewProjection, cameraPosition, context, out, m_CullChunkLODCopies[chunk]);
        });

        size_t visibleCount = 0;
        for (size_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            visibleCount += m_CullChunkVisible[chunk].size();
        }
        m_VisibleMeshProxies.reserve(visibleCount);

        // 完了順によらず、元のプロキシの並びで連結する
        for (size_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            const auto& visible = m_CullChunkVisible[chunk];
            m_VisibleMeshProxies.insert(m_VisibleMeshProxies.end(), visible.begin(), visible.end());
        }
    }

    void SceneView::SetLODCrossfadeClock(uint64_t frameNumber, float deltaSeconds)
    {
        m_bHasLODCrossfadeClock = true;
        m_LODCrossfadeFrameNumber = frameNumber;
        m_LODCrossfadeDeltaSeconds = deltaSeconds;
    }

    void SceneView::PrepareLODCrossfadeCullClock()
    {
        m_LODCrossfadeCullDeltaSeconds = 0.0f;
        // 時計が無ければ即座に切り替える。
        m_bLODCrossfadeCullSnap = !m_bHasLODCrossfadeClock;
        if (!m_bHasLODCrossfadeClock)
        {
            return;
        }
        if (m_bHasAdvancedLODCrossfadeFrame && m_LastLODCrossfadeFrameNumber == m_LODCrossfadeFrameNumber)
        {
            return;
        }
        m_bLODCrossfadeCullSnap = !std::isfinite(m_LODCrossfadeDeltaSeconds) || m_LODCrossfadeDeltaSeconds <= 0.0f;
        if (!m_bLODCrossfadeCullSnap)
        {
            m_LODCrossfadeCullDeltaSeconds = m_LODCrossfadeDeltaSeconds;
        }
        m_LastLODCrossfadeFrameNumber = m_LODCrossfadeFrameNumber;
        m_bHasAdvancedLODCrossfadeFrame = true;
    }

    void SceneView::CullProxies(Viewport *viewport)
    {
        m_LayerImpostorInstances.clear();
        PrepareLODCrossfadeCullClock();
        m_ImpostorMeshCrossfadeValues.clear();
        NORVES_STAT_TIME_START(cullingViewport);

        m_VisibleMeshProxies.clear();
        m_SuppressedMeshComponentIds.clear();
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

        const SceneViewScreenSizeContext screenSizeContext = MakeScreenSizeContext(camera);
        bool bRebuiltMegaIndex = false;
        for (BoardProxy& proxy : m_BoardProxies)
        {
            CullBoardProxy(proxy, camera, viewProjection, cameraPosition, screenSizeContext,
                           m_SuppressedMeshComponentIds, bRebuiltMegaIndex);
        }
        for (BoardProxy& proxy : m_TransientBoardProxies)
        {
            CullBoardProxy(proxy, camera, viewProjection, cameraPosition, screenSizeContext,
                           m_SuppressedMeshComponentIds, bRebuiltMegaIndex);
        }
        ApplyImpostorMegaSuppression();
        CullLayerImpostors(cameraPosition, screenSizeContext, viewProjection);

        CullMeshProxies(viewProjection, cameraPosition, screenSizeContext, m_LODProxyCopies);

        const size_t outgoingCount = static_cast<size_t>(std::count_if(m_VisibleMeshProxies.begin(), m_VisibleMeshProxies.end(),
            [](const MeshProxy* proxy) { return proxy->LODCrossfade.IsActive() && proxy->LODCrossfadeValue < 0.0f; }));
        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size() - outgoingCount + m_VisibleBoardProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        NORVES_STAT_TIME_END(cullingViewport, m_Stats.CullingTimeMs);
    }

    void SceneView::CullProxies(const ViewportSnapshot &viewport)
    {
        m_LayerImpostorInstances.clear();
        PrepareLODCrossfadeCullClock();
        m_ImpostorMeshCrossfadeValues.clear();
        NORVES_STAT_TIME_START(cullingSnapshot);
        m_VisibleMeshProxies.clear();
        m_SuppressedMeshComponentIds.clear();
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

        const SceneViewScreenSizeContext screenSizeContext = MakeScreenSizeContext(camera);
        bool bRebuiltMegaIndex = false;
        for (BoardProxy& proxy : m_BoardProxies)
        {
            CullBoardProxy(proxy, camera, viewProjection, cameraPosition, screenSizeContext,
                           m_SuppressedMeshComponentIds, bRebuiltMegaIndex);
        }
        for (BoardProxy& proxy : m_TransientBoardProxies)
        {
            CullBoardProxy(proxy, camera, viewProjection, cameraPosition, screenSizeContext,
                           m_SuppressedMeshComponentIds, bRebuiltMegaIndex);
        }
        ApplyImpostorMegaSuppression();
        CullLayerImpostors(cameraPosition, screenSizeContext, viewProjection);

        CullMeshProxies(viewProjection, cameraPosition, screenSizeContext, m_LODProxyCopies);

        const size_t outgoingCount = static_cast<size_t>(std::count_if(m_VisibleMeshProxies.begin(), m_VisibleMeshProxies.end(),
            [](const MeshProxy* proxy) { return proxy->LODCrossfade.IsActive() && proxy->LODCrossfadeValue < 0.0f; }));
        m_Stats.VisibleProxies = static_cast<uint32_t>(m_VisibleMeshProxies.size() - outgoingCount + m_VisibleBoardProxies.size());
        m_Stats.CulledProxies = m_Stats.CollectedProxies - m_Stats.VisibleProxies;

        NORVES_STAT_TIME_END(cullingSnapshot, m_Stats.CullingTimeMs);
    }

    void SceneView::BatchProxies()
    {
        NORVES_STAT_TIME_START(batching);
        m_Batcher.BeginBatching();
        m_LODCrossfadeProxies.clear();

        for (MeshProxy *proxy : m_VisibleMeshProxies)
        {
            if (proxy)
            {
                if (proxy->LODCrossfadeValue != 0.0f)
                {
                    m_LODCrossfadeProxies.push_back(proxy);
                }
                else
                {
                    m_Batcher.AddMeshProxy(*proxy);
                }
            }
        }

        m_Batcher.EndBatching();

        m_Stats.BatchCount = m_Batcher.GetStats().TotalBatches;

        NORVES_STAT_TIME_END(batching, m_Stats.BatchingTimeMs);
    }

    void SceneView::AppendWorldBoardDrawCommands()
    {
        Container::VariableArray<const BoardProxy *> impostorProxies;
        for (const BoardProxy *proxy : m_VisibleBoardProxies)
        {
            if (!proxy || !proxy->IsValid() || proxy->Space != BoardSpace::WorldSpace)
            {
                continue;
            }

            if (proxy->RenderSubtype == BoardRenderSubtype::Impostor)
            {
                if (proxy->Texture.IsValid())
                {
                    impostorProxies.push_back(proxy);
                }
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

        // 同じアトラスの組を隣接させ、同じ組の中では可視一覧の順序を保つ。
        std::stable_sort(impostorProxies.begin(), impostorProxies.end(),
                         [](const BoardProxy *lhs, const BoardProxy *rhs)
                         {
                             if (lhs->Texture != rhs->Texture)
                             {
                                 return lhs->Texture < rhs->Texture;
                             }
                             if (lhs->ImpostorNormalTexture != rhs->ImpostorNormalTexture)
                             {
                                 return lhs->ImpostorNormalTexture < rhs->ImpostorNormalTexture;
                             }
                             if (lhs->ImpostorOrmTexture != rhs->ImpostorOrmTexture)
                             {
                                 return lhs->ImpostorOrmTexture < rhs->ImpostorOrmTexture;
                             }
                             if (lhs->ImpostorDepthTexture != rhs->ImpostorDepthTexture)
                             {
                                 return lhs->ImpostorDepthTexture < rhs->ImpostorDepthTexture;
                             }
                             return lhs->ImpostorLayout < rhs->ImpostorLayout;
                         });

        for (const BoardProxy *proxy : impostorProxies)
        {
            if (m_ImpostorBatches.empty() ||
                m_ImpostorBatches.back().AlbedoAtlas != proxy->Texture ||
                m_ImpostorBatches.back().NormalAtlas != proxy->ImpostorNormalTexture ||
                m_ImpostorBatches.back().OrmAtlas != proxy->ImpostorOrmTexture ||
                m_ImpostorBatches.back().DepthAtlas != proxy->ImpostorDepthTexture ||
                m_ImpostorBatches.back().Layout != proxy->ImpostorLayout)
            {
                ImpostorBatch batch;
                batch.AlbedoAtlas = proxy->Texture;
                batch.NormalAtlas = proxy->ImpostorNormalTexture;
                batch.OrmAtlas = proxy->ImpostorOrmTexture;
                batch.DepthAtlas = proxy->ImpostorDepthTexture;
                batch.Layout = proxy->ImpostorLayout;
                batch.InstanceDataOffset = static_cast<uint32_t>(m_InstanceData.size());
                m_ImpostorBatches.push_back(batch);
            }

            GPUSceneInstanceData instanceData;
            FillWorldBoardInstanceData(*proxy, instanceData);
            m_InstanceData.push_back(instanceData);
            ++m_ImpostorBatches.back().InstanceCount;
        }
    }

    // 板の束とは別に、層の可視個体の束と視錐台外も含む影の一覧を作る。
    void SceneView::AppendLayerImpostorBatches()
    {
        if (m_LayerImpostorInstances.empty())
        {
            return;
        }

        struct VisibleLayerImpostor
        {
            const MegaGeometryProxy* Proxy;
            const MegaMeshImpostorDesc* Desc;
            float CenterX;
            float CenterY;
            float CenterZ;
            float HalfSize;
            float CrossfadeValue;
        };
        Container::VariableArray<VisibleLayerImpostor> visible;
        Container::UnorderedMap<uint64_t, uint32_t> shadowAtlasByMesh;
        for (const LayerImpostorInstance& instance : m_LayerImpostorInstances)
        {
            if ((!instance.bVisible && !instance.bCastShadow) || instance.ProxyIndex >= m_MegaGeometryProxies.size())
            {
                continue;
            }
            const MegaGeometryProxy& proxy = m_MegaGeometryProxies[instance.ProxyIndex];
            if (proxy.ObjectId != instance.ObjectId)
            {
                continue;
            }
            const MegaMeshImpostorDesc* desc = FindMegaMeshImpostor(proxy.MegaMeshHandle);
            LayerImpostorPlacement placement;
            if (!desc || !ComputeLayerImpostorPlacement(proxy.WorldTransform, *desc, placement))
            {
                continue;
            }

            if (instance.bVisible)
            {
                visible.push_back({&proxy, desc, placement.Center.x, placement.Center.y, placement.Center.z,
                                   placement.HalfSize, instance.CrossfadeValue});
            }
            if (!instance.bCastShadow)
            {
                continue;
            }

            uint32_t atlasIndex;
            const auto cached = shadowAtlasByMesh.find(proxy.MegaMeshHandle.Id);
            if (cached != shadowAtlasByMesh.end())
            {
                atlasIndex = cached->second;
            }
            else
            {
                atlasIndex = static_cast<uint32_t>(m_ImpostorShadowAtlases.size());
                for (uint32_t index = 0; index < m_ImpostorShadowAtlases.size(); ++index)
                {
                    const ImpostorShadowAtlas& atlas = m_ImpostorShadowAtlases[index];
                    if (atlas.AlbedoAtlas == desc->AlbedoAtlas && atlas.Layout == desc->Layout &&
                        atlas.AxisCellCountX == desc->AxisCellCountX && atlas.AxisCellCountY == desc->AxisCellCountY &&
                        atlas.CellResolution == desc->CellResolution && atlas.AtlasWidth == desc->AtlasWidth &&
                        atlas.AtlasHeight == desc->AtlasHeight)
                    {
                        atlasIndex = index;
                        break;
                    }
                }
                if (atlasIndex == m_ImpostorShadowAtlases.size())
                {
                    ImpostorShadowAtlas atlas;
                    atlas.AlbedoAtlas = desc->AlbedoAtlas;
                    atlas.Layout = desc->Layout;
                    atlas.AxisCellCountX = desc->AxisCellCountX;
                    atlas.AxisCellCountY = desc->AxisCellCountY;
                    atlas.CellResolution = desc->CellResolution;
                    atlas.AtlasWidth = desc->AtlasWidth;
                    atlas.AtlasHeight = desc->AtlasHeight;
                    m_ImpostorShadowAtlases.push_back(atlas);
                }
                shadowAtlasByMesh.emplace(proxy.MegaMeshHandle.Id, atlasIndex);
            }

            ImpostorShadowCaster caster;
            caster.Key = instance.ObjectId;
            caster.Center[0] = placement.Center.x;
            caster.Center[1] = placement.Center.y;
            caster.Center[2] = placement.Center.z;
            caster.HalfSize = placement.HalfSize;
            caster.AxisX[0] = placement.AxisX.x;
            caster.AxisX[1] = placement.AxisX.y;
            caster.AxisX[2] = placement.AxisX.z;
            caster.AxisY[0] = placement.AxisY.x;
            caster.AxisY[1] = placement.AxisY.y;
            caster.AxisY[2] = placement.AxisY.z;
            caster.AxisZ[0] = placement.AxisZ.x;
            caster.AxisZ[1] = placement.AxisZ.y;
            caster.AxisZ[2] = placement.AxisZ.z;
            caster.AtlasIndex = atlasIndex;
            m_ImpostorShadowCasters.push_back(caster);
        }

        // 板と同じ鍵で束ね、同じ組の中では元の可視一覧の順序を保つ。
        std::stable_sort(visible.begin(), visible.end(),
                         [](const VisibleLayerImpostor& lhs, const VisibleLayerImpostor& rhs)
                         {
                             if (lhs.Desc->AlbedoAtlas != rhs.Desc->AlbedoAtlas)
                             {
                                 return lhs.Desc->AlbedoAtlas < rhs.Desc->AlbedoAtlas;
                             }
                             if (lhs.Desc->NormalAtlas != rhs.Desc->NormalAtlas)
                             {
                                 return lhs.Desc->NormalAtlas < rhs.Desc->NormalAtlas;
                             }
                             if (lhs.Desc->OrmAtlas != rhs.Desc->OrmAtlas)
                             {
                                 return lhs.Desc->OrmAtlas < rhs.Desc->OrmAtlas;
                             }
                             if (lhs.Desc->DepthAtlas != rhs.Desc->DepthAtlas)
                             {
                                 return lhs.Desc->DepthAtlas < rhs.Desc->DepthAtlas;
                             }
                             return lhs.Desc->Layout < rhs.Desc->Layout;
                         });

        const size_t firstLayerBatch = m_ImpostorBatches.size();
        for (const VisibleLayerImpostor& item : visible)
        {
            const MegaMeshImpostorDesc& desc = *item.Desc;
            if (m_ImpostorBatches.size() == firstLayerBatch ||
                m_ImpostorBatches.back().AlbedoAtlas != desc.AlbedoAtlas ||
                m_ImpostorBatches.back().NormalAtlas != desc.NormalAtlas ||
                m_ImpostorBatches.back().OrmAtlas != desc.OrmAtlas ||
                m_ImpostorBatches.back().DepthAtlas != desc.DepthAtlas ||
                m_ImpostorBatches.back().Layout != desc.Layout)
            {
                ImpostorBatch batch;
                batch.AlbedoAtlas = desc.AlbedoAtlas;
                batch.NormalAtlas = desc.NormalAtlas;
                batch.OrmAtlas = desc.OrmAtlas;
                batch.DepthAtlas = desc.DepthAtlas;
                batch.Layout = desc.Layout;
                batch.InstanceDataOffset = static_cast<uint32_t>(m_InstanceData.size());
                m_ImpostorBatches.push_back(batch);
            }
            GPUSceneInstanceData instanceData;
            const Math::Vector3 center(item.CenterX, item.CenterY, item.CenterZ);
            FillLayerImpostorInstanceData(*item.Proxy, desc, center, item.HalfSize, item.CrossfadeValue, instanceData);
            m_InstanceData.push_back(instanceData);
            ++m_ImpostorBatches.back().InstanceCount;
        }
    }

    void SceneView::GenerateCommands()
    {
        m_DrawCommands.clear();
        m_OpaqueCommands.clear();
        m_TransparentCommands.clear();
        m_InstanceData.clear();
        m_ImpostorBatches.clear();
        m_ImpostorShadowCasters.clear();
        m_ImpostorShadowAtlases.clear();

        // バッチャーからDrawCommandを生成
        m_Batcher.GenerateDrawCommands(m_DrawCommands,
                                       m_InstanceData,
                                       m_bEnableInstancing,
                                       m_MinInstanceCount);

        for (const MeshProxy* proxy : m_LODCrossfadeProxies)
        {
            m_LODCrossfadeBatcher.Clear();
            m_LODCrossfadeBatcher.BeginBatching();
            m_LODCrossfadeBatcher.AddMeshProxy(*proxy);
            m_LODCrossfadeBatcher.EndBatching();
            m_LODCrossfadeBatcher.GenerateDrawCommands(m_LODCrossfadeCommands, m_LODCrossfadeInstanceData,
                                                       false, m_MinInstanceCount);
            const uint32_t instanceBase = static_cast<uint32_t>(m_InstanceData.size());
            for (GPUSceneInstanceData& instance : m_LODCrossfadeInstanceData)
            {
                instance.NormalMatrix[3] = proxy->LODCrossfadeValue;
                m_InstanceData.push_back(instance);
            }
            for (DrawCommand cmd : m_LODCrossfadeCommands)
            {
                // 非インスタンスの経路でも両方の欄がデータの位置を指す。
                cmd.Draw.FirstInstance += instanceBase;
                cmd.Draw.InstanceDataOffset += instanceBase;
                cmd.Draw.bLodCrossfade = true;
                m_DrawCommands.push_back(cmd);
            }
        }

        for (DrawCommand &cmd : m_DrawCommands)
        {
            cmd.CalculateSortKey(cmd.Draw.SortDepth, cmd.Draw.MaterialBlendMode);
        }

        AppendWorldBoardDrawCommands();
        AppendLayerImpostorBatches();

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
        m_LayerImpostorInstances.clear();
        m_ImpostorShadowCasters.clear();
        m_ImpostorShadowAtlases.clear();
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
                proxy.LODCrossfadeValue = 0.0f;
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
        m_LayerImpostorInstances.clear();
        m_ImpostorShadowCasters.clear();
        m_ImpostorShadowAtlases.clear();
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
                    proxy.LODCrossfadeValue = 0.0f;
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
#include "CloudLayerPass.inl"

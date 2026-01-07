#include "Rendering/SceneView.h"
#include "Rendering/Viewport.h"
#include "Math/VectorUtils.h"
#include <chrono>

namespace NorvesLib::Core::Rendering
{

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

    void SceneView::RemoveLightProxy(uint64_t objectId)
    {
        auto it = std::remove_if(m_LightProxies.begin(), m_LightProxies.end(),
                                 [objectId](const LightProxy &proxy)
                                 { return proxy.LightId == objectId; });
        m_LightProxies.erase(it, m_LightProxies.end());
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

    void SceneView::ClearAllProxies()
    {
        m_MeshProxies.clear();
        m_LightProxies.clear();
        m_VisibleMeshProxies.clear();
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

    void SceneView::CullProxies(Viewport *viewport)
    {
        auto startTime = std::chrono::high_resolution_clock::now();

        m_VisibleMeshProxies.clear();

        if (!viewport)
        {
            return;
        }

        const Math::Matrix4x4 &viewProjection = viewport->GetViewProjectionMatrix();
        const CameraProxy &camera = viewport->GetCamera();

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

    bool SceneView::FrustumCull(const MeshProxy &proxy, const Math::Matrix4x4 &viewProjection) const
    {
        // 簡易的な視錐台カリング
        // バウンディングスフィアの中心を変換し、範囲内かチェック
        const BoundingSphere &bounds = proxy.WorldBounds;

        // クリップ空間に変換
        float x = bounds.CenterX * viewProjection.m00 +
                  bounds.CenterY * viewProjection.m10 +
                  bounds.CenterZ * viewProjection.m20 + viewProjection.m30;
        float y = bounds.CenterX * viewProjection.m01 +
                  bounds.CenterY * viewProjection.m11 +
                  bounds.CenterZ * viewProjection.m21 + viewProjection.m31;
        float z = bounds.CenterX * viewProjection.m02 +
                  bounds.CenterY * viewProjection.m12 +
                  bounds.CenterZ * viewProjection.m22 + viewProjection.m32;
        float w = bounds.CenterX * viewProjection.m03 +
                  bounds.CenterY * viewProjection.m13 +
                  bounds.CenterZ * viewProjection.m23 + viewProjection.m33;

        // wが負の場合は背面
        if (w <= 0.0f)
        {
            return false;
        }

        // 半径をwで調整
        float radius = bounds.Radius;

        // 6平面のうち1つでも外なら不可視
        if (x + radius < -w || x - radius > w)
            return false;
        if (y + radius < -w || y - radius > w)
            return false;
        if (z + radius < 0.0f || z - radius > w)
            return false; // Near/Far

        return true;
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

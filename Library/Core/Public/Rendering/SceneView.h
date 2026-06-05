#pragma once

#include "View.h"
#include "Viewport.h"
#include "ViewportSnapshot.h"
#include "SceneProxy.h"
#include "DrawCommand.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Container/UnorderedSet.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class SceneRenderer;

    /**
     * @brief SceneView設定
     */
    struct SceneViewSettings : public ViewSettings
    {
        bool bEnableFrustumCulling = true;    // 視錐台カリング
        bool bEnableOcclusionCulling = false; // オクルージョンカリング
        bool bEnableDistanceCulling = true;   // 距離カリング
        float MaxDrawDistance = 10000.0f;     // 最大描画距離
        bool bEnableInstancing = true;        // インスタンシング有効
        uint32_t MinInstanceCount = 2;        // インスタンシング最小数

        SceneViewSettings()
        {
            Type = ViewType::Scene;
        }
    };

    /**
     * @brief SceneView
     *
     * 3Dシーンの描画を担当するView。
     * Proxyを収集し、バッチングとDrawCommand発行を行います。
     *
     * 描画フロー:
     * 1. CollectProxies(): 登録されたオブジェクトからMeshProxyを収集
     * 2. CullProxies(): 視錐台カリング等でProxyをフィルタリング
     * 3. BatchProxies(): MeshBatcherでProxyをバッチ化
     * 4. GenerateCommands(): DrawCommandを生成
     * 5. RenderCommands(): Viewportごとに描画実行
     */
    class SceneView : public View
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        SceneView() = default;

        /**
         * @brief デストラクタ
         */
        virtual ~SceneView() = default;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param settings SceneView設定
         * @return 初期化成功時true
         */
        bool Initialize(const SceneViewSettings &settings);

        /**
         * @brief 終了処理
         */
        virtual void Shutdown() override;

        // ========================================
        // Proxy登録（WorldからSceneViewへ直接渡す）
        // ========================================

        /**
         * @brief MeshProxyを追加
         * @param proxy 追加するProxy
         *
         * WorldがMeshを生成した際に呼び出し、SceneViewに直接Proxyを渡します。
         */
        void AddMeshProxy(const MeshProxy &proxy);

        /**
         * @brief MeshProxyを削除
         * @param objectId 削除するProxyのObjectId
         */
        void RemoveMeshProxy(uint64_t objectId);

        /**
         * @brief 生存ObjectIdに含まれないMeshProxyを削除
         * @param liveObjectIds 現在World側で有効なMesh所有ObjectId
         */
        void RemoveStaleMeshProxies(const Container::UnorderedSet<uint64_t> &liveObjectIds);

        /**
         * @brief LightProxyを追加
         * @param proxy 追加するProxy
         */
        void AddLightProxy(const LightProxy &proxy);

        /**
         * @brief MegaGeometryProxyを追加
         * @param proxy 追加するProxy
         */
        void AddMegaGeometryProxy(const MegaGeometryProxy &proxy);

        /**
         * @brief LightProxyを削除
         * @param objectId 削除するProxyのObjectId
         */
        void RemoveLightProxy(uint64_t objectId);

        /**
         * @brief 生存LightIdに含まれないLightProxyを削除
         * @param liveLightIds 現在World側で有効なLightComponentId
         */
        void RemoveStaleLightProxies(const Container::UnorderedSet<uint64_t> &liveLightIds);

        /**
         * @brief MegaGeometryProxyを削除
         * @param objectId 削除対象ObjectId
         */
        void RemoveMegaGeometryProxy(uint64_t objectId);

        /**
         * @brief 生存ObjectIdに含まれないMegaGeometryProxyを削除
         * @param liveObjectIds 現在World側で有効なMegaGeometry所有ObjectId
         */
        void RemoveStaleMegaGeometryProxies(const Container::UnorderedSet<uint64_t> &liveObjectIds);

        /**
         * @brief MeshProxyを更新
         * @param proxy 更新するProxy（ObjectIdで照合）
         */
        void UpdateMeshProxy(const MeshProxy &proxy);

        /**
         * @brief LightProxyを更新
         * @param proxy 更新するProxy（ObjectIdで照合）
         */
        void UpdateLightProxy(const LightProxy &proxy);

        /**
         * @brief MegaGeometryProxyを更新
         * @param proxy 更新するProxy（ComponentIdで照合）
         */
        void UpdateMegaGeometryProxy(const MegaGeometryProxy &proxy);

        /**
         * @brief すべてのProxyをクリア
         */
        void ClearAllProxies();

        /**
         * @brief MegaGeometryProxyのみクリア
         */
        void ClearMegaGeometryProxies();

        // ========================================
        // 描画フロー
        // ========================================

        /**
         * @brief Viewの描画を実行（オーバーライド）
         */
        virtual void Render() override;

        /**
         * @brief パスチェーン対応の描画（オーバーライド）
         * @param context 描画コンテキスト
         *
         * パスチェーンが設定されていれば、GBuffer→Lighting→PostProcess
         * の順にパスを実行します。未設定の場合はレガシーRender()に
         * フォールバックします。
         */
        virtual void Render(ViewRenderContext &context) override;

        // ========================================
        // パイプライン構築ヘルパー
        // ========================================

        /**
         * @brief ディファードレンダリングパイプラインをセットアップ
         * @param sceneRenderer SceneRenderer参照
         *
         * GBufferPass → LightingPass → PostProcessStack(ToneMapping)
         * の順にパスチェーンを構築します。
         */
        void SetupDeferredPipeline(SceneRenderer *sceneRenderer);

        /**
         * @brief フォワードレンダリングパイプラインをセットアップ
         * @param sceneRenderer SceneRenderer参照
         *
         * ForwardPass → PostProcessStack(ToneMapping)
         * の順にパスチェーンを構築します。
         */
        void SetupForwardPipeline(SceneRenderer *sceneRenderer);

        /**
         * @brief Proxyをカリング
         * @param viewport 対象Viewport
         *
         * 視錐台カリング、距離カリングでProxyをフィルタリングします。
         */
        void CullProxies(Viewport *viewport);

        /**
         * @brief Viewport snapshotを使ってProxyをカリング
         * @param viewport 対象Viewport snapshot
         */
        void CullProxies(const ViewportSnapshot &viewport);

        /**
         * @brief Proxyをバッチング
         *
         * MeshBatcherを使用してProxyをバッチ化します。
         */
        void BatchProxies();

        /**
         * @brief DrawCommandを生成
         *
         * バッチからDrawCommandを生成します。
         */
        void GenerateCommands();

        /**
         * @brief DrawCommandを実行
         * @param viewport 対象Viewport
         */
        void RenderCommands(Viewport *viewport);

        /**
         * @brief Viewport不要のDrawCommand準備
         *
         * カメラ依存のカリングをスキップし、
         * 全可視Proxyからバッチング→DrawCommand生成を行います。
         * Viewport/Camera未設定時のフォールバック用。
         */
        void PrepareDrawCommands();

        /**
         * @brief Viewport snapshotに対応するDrawCommand準備
         *
         * GameThread上でViewportごとのCamera/Rectに基づいて
         * カリング、バッチング、DrawCommand生成を行います。
         *
         * @param viewport 対象Viewport snapshot
         */
        void PrepareDrawCommandsForViewport(const ViewportSnapshot &viewport);

        // ========================================
        // Proxy直接アクセス
        // ========================================

        /**
         * @brief 収集されたMeshProxyを取得
         */
        const Container::VariableArray<MeshProxy> &GetMeshProxies() const { return m_MeshProxies; }

        /**
         * @brief 収集されたLightProxyを取得
         */
        const Container::VariableArray<LightProxy> &GetLightProxies() const { return m_LightProxies; }

        /**
         * @brief 収集されたMegaGeometryProxyを取得
         */
        const Container::VariableArray<MegaGeometryProxy> &GetMegaGeometryProxies() const { return m_MegaGeometryProxies; }

        /**
         * @brief 可視MeshProxyを取得
         */
        const Container::VariableArray<MeshProxy *> &GetVisibleMeshProxies() const { return m_VisibleMeshProxies; }

        /**
         * @brief DrawCommandを取得
         */
        const Container::VariableArray<DrawCommand> &GetDrawCommands() const { return m_DrawCommands; }

        /**
         * @brief 不透明DrawCommandを取得
         */
        const Container::VariableArray<DrawCommand> &GetOpaqueCommands() const { return m_OpaqueCommands; }

        /**
         * @brief 半透明DrawCommandを取得
         */
        const Container::VariableArray<DrawCommand> &GetTransparentCommands() const { return m_TransparentCommands; }

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief カリング設定
         */
        void SetFrustumCullingEnabled(bool bEnabled) { m_bEnableFrustumCulling = bEnabled; }
        void SetOcclusionCullingEnabled(bool bEnabled) { m_bEnableOcclusionCulling = bEnabled; }
        void SetDistanceCullingEnabled(bool bEnabled) { m_bEnableDistanceCulling = bEnabled; }
        void SetMaxDrawDistance(float distance) { m_MaxDrawDistance = distance; }

        /**
         * @brief インスタンシング設定
         */
        void SetInstancingEnabled(bool bEnabled) { m_bEnableInstancing = bEnabled; }
        void SetMinInstanceCount(uint32_t count) { m_MinInstanceCount = count; }

        // ========================================
        // 統計
        // ========================================

        struct SceneViewStats
        {
            uint32_t TotalObjects = 0;       // 登録オブジェクト数
            uint32_t CollectedProxies = 0;   // 収集されたProxy数
            uint32_t VisibleProxies = 0;     // 可視Proxy数
            uint32_t CulledProxies = 0;      // カリングされたProxy数
            uint32_t BatchCount = 0;         // バッチ数
            uint32_t DrawCommandCount = 0;   // DrawCommand数
            uint32_t InstancedDrawCalls = 0; // インスタンシング描画数
            float CollectionTimeMs = 0.0f;   // 収集時間
            float CullingTimeMs = 0.0f;      // カリング時間
            float BatchingTimeMs = 0.0f;     // バッチング時間
        };

        const SceneViewStats &GetStats() const { return m_Stats; }

    private:
        /**
         * @brief 視錐台カリングを実行
         * @param proxy チェック対象のProxy
         * @param viewProjection ビュープロジェクション行列
         * @return 可視の場合true
         */
        bool FrustumCull(const MeshProxy &proxy, const Math::Matrix4x4 &viewProjection) const;

        /**
         * @brief 距離カリングを実行
         * @param proxy チェック対象のProxy
         * @param cameraPosition カメラ位置
         * @return 可視の場合true
         */
        bool DistanceCull(const MeshProxy &proxy, const Math::Vector3 &cameraPosition) const;

    private:
        // MeshProxy（WorldからSceneViewに直接渡される）
        Container::VariableArray<MeshProxy> m_MeshProxies;
        Container::VariableArray<MegaGeometryProxy> m_MegaGeometryProxies;
        Container::VariableArray<LightProxy> m_LightProxies;

        // 可視Proxy（カリング後）
        Container::VariableArray<MeshProxy *> m_VisibleMeshProxies;

        // バッチャー
        MeshBatcher m_Batcher;

        // DrawCommand
        Container::VariableArray<DrawCommand> m_DrawCommands;
        Container::VariableArray<DrawCommand> m_OpaqueCommands;
        Container::VariableArray<DrawCommand> m_TransparentCommands;

        // 設定
        bool m_bEnableFrustumCulling = true;
        bool m_bEnableOcclusionCulling = false;
        bool m_bEnableDistanceCulling = true;
        float m_MaxDrawDistance = 10000.0f;
        bool m_bEnableInstancing = true;
        uint32_t m_MinInstanceCount = 2;

        // 統計
        SceneViewStats m_Stats;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
#include "FramePacket.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{

    // 前方宣言
    class MeshComponent;

    // ========================================
    // コレクション対象インターフェース
    // ========================================

    /**
     * @brief シーンコレクション対象インターフェース
     *
     * SceneCollectorによって収集されるオブジェクトが実装するインターフェース
     */
    class ISceneCollectable
    {
    public:
        virtual ~ISceneCollectable() = default;

        /**
         * @brief MeshProxyを収集
         * @param outProxy 出力先のMeshProxy
         * @return 収集に成功した場合true
         */
        virtual bool CollectMeshProxy(MeshProxy &outProxy) const = 0;

        /**
         * @brief 可視状態かどうか
         */
        virtual bool IsVisible() const = 0;

        /**
         * @brief レンダリングレイヤーを取得
         */
        virtual RenderLayer GetRenderLayer() const = 0;
    };

    /**
     * @brief ライトコレクション対象インターフェース
     */
    class ILightCollectable
    {
    public:
        virtual ~ILightCollectable() = default;

        /**
         * @brief LightProxyを収集
         * @param outProxy 出力先のLightProxy
         * @return 収集に成功した場合true
         */
        virtual bool CollectLightProxy(LightProxy &outProxy) const = 0;

        /**
         * @brief 有効状態かどうか
         */
        virtual bool IsEnabled() const = 0;
    };

    // ========================================
    // シーンコレクター
    // ========================================

    /**
     * @brief シーンコレクター
     *
     * GameThread上でシーン内のオブジェクトを収集し、
     * FramePacketに描画データを書き込みます。
     */
    class SceneCollector
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        SceneCollector() = default;

        /**
         * @brief 初期化
         */
        void Initialize();

        /**
         * @brief 終了処理
         */
        void Shutdown();

        // ========================================
        // オブジェクト登録
        // ========================================

        /**
         * @brief メッシュ描画可能オブジェクトを登録
         * @param collectable 登録するオブジェクト
         */
        void RegisterMeshCollectable(ISceneCollectable *collectable);

        /**
         * @brief メッシュ描画可能オブジェクトを登録解除
         * @param collectable 登録解除するオブジェクト
         */
        void UnregisterMeshCollectable(ISceneCollectable *collectable);

        /**
         * @brief ライトオブジェクトを登録
         * @param collectable 登録するオブジェクト
         */
        void RegisterLightCollectable(ILightCollectable *collectable);

        /**
         * @brief ライトオブジェクトを登録解除
         * @param collectable 登録解除するオブジェクト
         */
        void UnregisterLightCollectable(ILightCollectable *collectable);

        // ========================================
        // シーン収集
        // ========================================

        /**
         * @brief シーンを収集してFramePacketに書き込む
         * @param packet 書き込み先のFramePacket
         * @param camera メインカメラデータ
         */
        void CollectScene(FramePacket *packet, const CameraProxy &camera);

        /**
         * @brief シーンを収集（カメラ指定なし）
         * @param packet 書き込み先のFramePacket
         */
        void CollectScene(FramePacket *packet);

        // ========================================
        // カリングとソート
        // ========================================

        /**
         * @brief カリング設定
         */
        struct CullingSettings
        {
            bool bEnableFrustumCulling = true;    // 視錐台カリング
            bool bEnableOcclusionCulling = false; // オクルージョンカリング（未実装）
            bool bEnableDistanceCulling = true;   // 距離カリング
            float MaxDrawDistance = 10000.0f;     // 最大描画距離
            float SmallObjectThreshold = 0.001f;  // 小さすぎるオブジェクトを除外するしきい値
        };

        /**
         * @brief カリング設定を設定
         */
        void SetCullingSettings(const CullingSettings &settings)
        {
            m_CullingSettings = settings;
        }

        /**
         * @brief カリング設定を取得
         */
        const CullingSettings &GetCullingSettings() const
        {
            return m_CullingSettings;
        }

        // ========================================
        // 統計
        // ========================================

        struct CollectionStats
        {
            uint32_t TotalObjects = 0;     // 登録オブジェクト総数
            uint32_t VisibleObjects = 0;   // 可視オブジェクト数
            uint32_t CulledObjects = 0;    // カリングされたオブジェクト数
            uint32_t CollectedMeshes = 0;  // 収集されたメッシュ数
            uint32_t CollectedLights = 0;  // 収集されたライト数
            float CollectionTimeMs = 0.0f; // 収集にかかった時間（ミリ秒）
        };

        /**
         * @brief 最後の収集統計を取得
         */
        const CollectionStats &GetLastCollectionStats() const
        {
            return m_LastStats;
        }

    private:
        // メッシュ描画可能オブジェクトリスト
        Container::VariableArray<ISceneCollectable *> m_MeshCollectables;

        // ライトオブジェクトリスト
        Container::VariableArray<ILightCollectable *> m_LightCollectables;

        // カリング設定
        CullingSettings m_CullingSettings;

        // 最後の収集統計
        CollectionStats m_LastStats;

        // ========================================
        // 内部メソッド
        // ========================================

        /**
         * @brief 視錐台カリングを実行
         * @param sphere オブジェクトのバウンディングスフィア
         * @param camera カメラ情報
         * @return オブジェクトが視錐台内にある場合true
         */
        bool FrustumCull(const BoundingSphere &sphere, const CameraProxy &camera) const;

        /**
         * @brief 距離カリングを実行
         * @param sphere オブジェクトのバウンディングスフィア
         * @param camera カメラ情報
         * @return オブジェクトが描画距離内にある場合true
         */
        bool DistanceCull(const BoundingSphere &sphere, const CameraProxy &camera) const;

        /**
         * @brief MeshProxyリストをソート
         * @param proxies ソート対象のリスト
         * @param camera カメラ情報
         */
        void SortMeshProxies(Container::VariableArray<MeshProxy> &proxies,
                             const CameraProxy &camera) const;
    };

    // ========================================
    // シーンコレクターグローバルアクセス
    // ========================================

    /**
     * @brief グローバルシーンコレクターを取得
     */
    SceneCollector &GetSceneCollector();

} // namespace NorvesLib::Core::Rendering

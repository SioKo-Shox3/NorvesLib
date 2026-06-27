// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Public\Engine\NorvesEngine.h
#pragma once

#include "IEngine.h"
#include "Container/Containers.h"
#include "Thread/Atomic.h"
#include "Object/ResourceRegistry.h"
#include "Engine/AssetRegistry.h"
#include "Engine/ComponentDataRegistry.h"
#include "Rendering/Screen.h"
#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderThread.h"
#include "Rendering/DebugDrawQueue.h"

namespace NorvesLib::Core
{

    /**
     * @brief Norvesゲームエンジンの実装クラス
     *
     * IEngineインターフェースを実装し、Norvesエンジンの中心的な機能を提供します。
     *
     * サブシステム:
     * - ResourceRegistry: リソース管理（参照カウント方式）
     * - Screen: 最終描画出力先
     * - RenderingCoordinator: 描画フロー管理
     * - RenderThread: レンダースレッド管理
     *
     * 描画フロー:
     * 1. SceneViewがProxyを収集・カリング・バッチング
     * 2. DrawCommandを生成
     * 3. ViewportごとにDrawCommandを実行
     * 4. Viewportの結果をViewに合成
     * 5. 複数Viewの結果をScreenに合成
     * 6. Screenをウィンドウにプレゼント
     */
    class NorvesEngine : public IEngine
    {
    public:
        /**
         * @brief コンストラクタ
         */
        NorvesEngine();

        /**
         * @brief デストラクタ
         */
        ~NorvesEngine() override;

        /**
         * @brief エンジンの初期化
         *
         * @return 初期化に成功した場合はtrue、失敗した場合はfalse
         */
        bool Initialize() override;

        /**
         * @brief エンジンの更新
         *
         * @param deltaTime 前フレームからの経過時間（秒）
         */
        void Update(float deltaTime) override;

        /**
         * @brief エンジンのシャットダウン
         */
        void Shutdown() override;

        /**
         * @brief エンジンが実行中かどうか
         *
         * @return 実行中の場合はtrue、そうでない場合はfalse
         */
        bool IsRunning() const override;

        /**
         * @brief エンジンの実行を停止する
         */
        void Stop() override;

        /**
         * @brief エンジンのバージョンを取得
         *
         * @return バージョン文字列
         */
        const NorvesLib::Core::Container::String &GetVersion() const;

        // ========================================
        // サブシステムアクセス
        // ========================================

        /**
         * @brief リソースレジストリを取得
         * @return リソースレジストリへの参照
         */
        ResourceRegistry &GetResourceRegistry() { return m_ResourceRegistry; }

        /**
         * @brief リソースレジストリを取得（const版）
         * @return リソースレジストリへのconst参照
         */
        const ResourceRegistry &GetResourceRegistry() const { return m_ResourceRegistry; }

        /**
         * @brief アセットレジストリを取得
         * @return アセットレジストリへの参照
         */
        AssetRegistry &GetAssetRegistry() { return m_AssetRegistry; }

        /**
         * @brief アセットレジストリを取得（const版）
         * @return アセットレジストリへのconst参照
         */
        const AssetRegistry &GetAssetRegistry() const { return m_AssetRegistry; }

        /**
         * @brief Screenを取得
         * @return Screenへの参照
         */
        Rendering::Screen &GetScreen() { return m_RenderingCoordinator.GetScreen(); }

        /**
         * @brief Screenを取得（const版）
         * @return Screenへのconst参照
         */
        const Rendering::Screen &GetScreen() const { return m_RenderingCoordinator.GetScreen(); }

        /**
         * @brief レンダリングコーディネーターを取得
         * @return レンダリングコーディネーターへの参照
         */
        Rendering::RenderingCoordinator &GetRenderingCoordinator() { return m_RenderingCoordinator; }

        /**
         * @brief レンダリングコーディネーターを取得（const版）
         * @return レンダリングコーディネーターへのconst参照
         */
        const Rendering::RenderingCoordinator &GetRenderingCoordinator() const { return m_RenderingCoordinator; }

        /**
         * @brief レンダースレッドを取得
         * @return レンダースレッドへの参照
         */
        Rendering::RenderThread &GetRenderThread() { return m_RenderThread; }

        /**
         * @brief レンダースレッドを取得（const版）
         * @return レンダースレッドへのconst参照
         */
        const Rendering::RenderThread &GetRenderThread() const { return m_RenderThread; }

        /**
         * @brief デバッグ描画キューを取得
         * @return デバッグ描画キューへの参照
         */
        Rendering::DebugDrawQueue& GetDebugDraw() { return m_DebugDraw; }

        /**
         * @brief デバッグ描画キューを取得（const版）
         * @return デバッグ描画キューへのconst参照
         */
        const Rendering::DebugDrawQueue& GetDebugDraw() const { return m_DebugDraw; }

        /**
         * @brief Component data registryを取得
         * @return Component data registryへの参照
         */
        ComponentDataRegistry &GetComponentDataRegistry() { return m_ComponentDataRegistry; }

        /**
         * @brief Component data registryを取得（const版）
         * @return Component data registryへのconst参照
         */
        const ComponentDataRegistry &GetComponentDataRegistry() const { return m_ComponentDataRegistry; }

    private:
        Thread::Atomic<bool> m_isRunning;             ///< エンジンが実行中かどうか
        NorvesLib::Core::Container::String m_version; ///< エンジンのバージョン
        uint64_t m_FrameCounter = 0;                  ///< AssetRegistry更新用フレーム番号

        // サブシステム（GEngineと寿命が一致）
        ResourceRegistry m_ResourceRegistry;                    ///< リソース管理
        AssetRegistry m_AssetRegistry;                          ///< アセット（ファイル）管理
        ComponentDataRegistry m_ComponentDataRegistry;           ///< Component data scratch registry
        Rendering::RenderingCoordinator m_RenderingCoordinator; ///< 描画フロー管理
        Rendering::RenderThread m_RenderThread;                 ///< レンダースレッド
        Rendering::DebugDrawQueue m_DebugDraw;                  ///< デバッグ描画キュー
    };

    /**
     * @brief グローバルエンジンインスタンス
     *
     * アプリケーション全体で共有されるNorvesEngineのグローバルインスタンス
     */
    extern NorvesEngine GEngine;

} // namespace NorvesLib::Core

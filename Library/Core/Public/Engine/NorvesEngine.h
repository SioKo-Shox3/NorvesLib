// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Public\Engine\NorvesEngine.h
#pragma once

#include "IEngine.h"
#include "Core/Public/Container/Containers.h"
#include "Thread/Atomic.h"
#include "Object/ResourceRegistry.h"
#include "Rendering/RenderWorld.h"

namespace NorvesLib::Core
{

    /**
     * @brief Norvesゲームエンジンの実装クラス
     *
     * IEngineインターフェースを実装し、Norvesエンジンの中心的な機能を提供します。
     * 
     * サブシステム:
     * - ResourceRegistry: リソース管理（参照カウント方式）
     * - RenderWorld: レンダリングシステム
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
        const NorvesLib::Core::Container::String& GetVersion() const;

        // ========================================
        // サブシステムアクセス
        // ========================================

        /**
         * @brief リソースレジストリを取得
         * @return リソースレジストリへの参照
         */
        ResourceRegistry& GetResourceRegistry() { return m_ResourceRegistry; }

        /**
         * @brief リソースレジストリを取得（const版）
         * @return リソースレジストリへのconst参照
         */
        const ResourceRegistry& GetResourceRegistry() const { return m_ResourceRegistry; }

        /**
         * @brief レンダリングワールドを取得
         * @return レンダリングワールドへの参照
         */
        Rendering::RenderWorld& GetRenderWorld() { return m_RenderWorld; }

        /**
         * @brief レンダリングワールドを取得（const版）
         * @return レンダリングワールドへのconst参照
         */
        const Rendering::RenderWorld& GetRenderWorld() const { return m_RenderWorld; }

    private:
        Thread::Atomic<bool> m_isRunning;             ///< エンジンが実行中かどうか
        NorvesLib::Core::Container::String m_version; ///< エンジンのバージョン

        // サブシステム（GEngineと寿命が一致）
        ResourceRegistry m_ResourceRegistry;         ///< リソース管理
        Rendering::RenderWorld m_RenderWorld;        ///< レンダリングシステム
    };

    /**
     * @brief グローバルエンジンインスタンス
     *
     * アプリケーション全体で共有されるNorvesEngineのグローバルインスタンス
     */
    extern NorvesEngine GEngine;

} // namespace NorvesLib::Core
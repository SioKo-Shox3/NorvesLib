// filepath: c:\Users\KINGkawamura\Documents\NorvesLib\Library\Core\Public\Engine\NorvesEngine.h
#pragma once

#include "IEngine.h"
#include <string>
// Threadライブラリへの参照を相対パスに修正
#include "../../../Thread/Public/Atomic.h"

namespace NorvesLib::Core
{

    /**
     * @brief Norvesゲームエンジンの実装クラス
     *
     * IEngineインターフェースを実装し、Norvesエンジンの中心的な機能を提供します。
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
        const std::string &GetVersion() const;

    private:
        Thread::Atomic<bool> m_isRunning; ///< エンジンが実行中かどうか
        std::string m_version;            ///< エンジンのバージョン
    };

    /**
     * @brief グローバルエンジンインスタンス
     *
     * アプリケーション全体で共有されるNorvesEngineのグローバルインスタンス
     */
    extern NorvesEngine GEngine;

} // namespace NorvesLib::Core
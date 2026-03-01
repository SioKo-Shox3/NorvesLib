#pragma once

#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    struct ViewRenderContext;

    /**
     * @brief Viewパスインターフェース
     *
     * Viewが所有する描画パスの基底クラス。
     * 各パス（GBuffer, Lighting, PostProcess等）はこれを継承して実装します。
     *
     * パスのライフサイクル:
     * 1. Initialize() - パス固有のRHIリソース作成
     * 2. 毎フレーム: Setup() → Execute()
     *    - Setup(): 一時リソースの取得・レンダーターゲット準備
     *    - Execute(): コマンドリストへの描画コマンド発行
     * 3. Shutdown() - リソース解放
     *
     * 設計意図:
     * - 各パスが独立したリソース宣言と描画ロジックを持つ
     * - View内でパスチェーンとして順次実行される
     * - TransientResourcePoolと連携し、一時リソースの効率的な管理を行う
     */
    class IViewPass
    {
    public:
        /**
         * @brief デストラクタ
         */
        virtual ~IViewPass() = default;

        // ========================================
        // 識別
        // ========================================

        /**
         * @brief パス名を取得
         * @return パスの識別名
         */
        virtual const char *GetName() const = 0;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief パスを初期化します
         *
         * パイプライン、シェーダー等の永続リソースを作成します。
         * @param context 描画コンテキスト（デバイス参照用）
         * @return 初期化成功時true
         */
        virtual bool Initialize(ViewRenderContext &context) = 0;

        /**
         * @brief パスの終了処理
         *
         * 永続リソースを解放します。
         */
        virtual void Shutdown() = 0;

        // ========================================
        // フレーム実行
        // ========================================

        /**
         * @brief パスのセットアップ（リソース準備フェーズ）
         *
         * TransientResourcePoolからレンダーターゲットを取得し、
         * レンダーパス・フレームバッファを準備します。
         * @param context 描画コンテキスト
         */
        virtual void Setup(ViewRenderContext &context) = 0;

        /**
         * @brief パスの実行（描画コマンド発行フェーズ）
         *
         * コマンドリストに対して実際の描画コマンドを発行します。
         * @param context 描画コンテキスト
         */
        virtual void Execute(ViewRenderContext &context) = 0;

        // ========================================
        // 状態管理
        // ========================================

        /**
         * @brief パスの有効/無効を設定
         * @param bEnabled 有効にする場合true
         */
        void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }

        /**
         * @brief パスが有効かどうか
         * @return 有効な場合true
         */
        bool IsEnabled() const { return m_bEnabled; }

        /**
         * @brief パスが初期化済みかどうか
         * @return 初期化済みの場合true
         */
        bool IsInitialized() const { return m_bInitialized; }

    protected:
        bool m_bEnabled = true;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

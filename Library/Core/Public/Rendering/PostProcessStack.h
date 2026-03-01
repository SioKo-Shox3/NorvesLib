#pragma once

#include "IViewPass.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{

    /**
     * @brief ポストプロセスチェーン
     *
     * 複数のポストプロセスパスを管理し、順次実行するチェーン。
     * Viewが所有し、メインパス（GBuffer+Lighting等）の後に実行されます。
     *
     * 各パスは「入力テクスチャ → フルスクリーン描画 → 出力テクスチャ」のパターンで動作。
     * TransientResourcePoolからピンポンバッファを自動取得します。
     *
     * パスの追加・削除・有効/無効切り替えが可能です。
     * 最終パスは SwapChain フォーマット (SRGB) に変換して出力します。
     *
     * 使用例:
     * ```cpp
     * PostProcessStack stack;
     * stack.AddPass(MakeUnique<ToneMappingPass>());
     * stack.AddPass(MakeUnique<BloomPass>());
     * stack.AddPass(MakeUnique<FXAAPass>());
     *
     * // 毎フレーム
     * stack.Execute(context);
     * ```
     */
    class PostProcessStack
    {
    public:
        /**
         * @brief コンストラクタ
         */
        PostProcessStack() = default;

        /**
         * @brief デストラクタ
         */
        ~PostProcessStack();

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化します
         * @param context 描画コンテキスト（デバイス参照用）
         * @return 初期化成功時true
         */
        bool Initialize(ViewRenderContext &context);

        /**
         * @brief 終了処理を行います
         */
        void Shutdown();

        // ========================================
        // パス管理
        // ========================================

        /**
         * @brief パスを末尾に追加します
         * @param pass 追加するパス
         */
        void AddPass(TUniquePtr<IViewPass> pass);

        /**
         * @brief パスをインデックスに挿入します
         * @param index 挿入位置
         * @param pass 追加するパス
         */
        void InsertPass(uint32_t index, TUniquePtr<IViewPass> pass);

        /**
         * @brief パスを名前で削除します
         * @param name 削除するパス名
         */
        void RemovePass(const char *name);

        /**
         * @brief パスの有効/無効を名前で切り替えます
         * @param name パス名
         * @param bEnabled 有効にする場合true
         */
        void SetPassEnabled(const char *name, bool bEnabled);

        /**
         * @brief パスを名前で取得します
         * @param name パス名
         * @return パスへのポインタ（見つからない場合nullptr）
         */
        IViewPass *GetPass(const char *name) const;

        /**
         * @brief パス数を取得します
         * @return パス数
         */
        uint32_t GetPassCount() const { return static_cast<uint32_t>(m_Passes.size()); }

        /**
         * @brief 有効なパス数を取得します
         * @return 有効なパス数
         */
        uint32_t GetEnabledPassCount() const;

        // ========================================
        // 実行
        // ========================================

        /**
         * @brief 全パスのセットアップを実行します
         * @param context 描画コンテキスト
         */
        void Setup(ViewRenderContext &context);

        /**
         * @brief 全パスを順次実行します
         * @param context 描画コンテキスト
         */
        void Execute(ViewRenderContext &context);

        // ========================================
        // アクセサ
        // ========================================

        /**
         * @brief パスリストを取得します（読み取り専用）
         */
        const VariableArray<TUniquePtr<IViewPass>> &GetPasses() const { return m_Passes; }

        /**
         * @brief 初期化済みかどうかを返します
         */
        bool IsInitialized() const { return m_bInitialized; }

    private:
        VariableArray<TUniquePtr<IViewPass>> m_Passes;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

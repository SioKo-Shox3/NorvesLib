#pragma once

#include "RenderTypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include "Math/Matrix4x4.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IRenderTarget;
    class ITexture2D;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class Viewport;

    /**
     * @brief Viewの種類
     */
    enum class ViewType : uint8_t
    {
        Scene, // 3Dシーン描画
        UI,    // UI描画
        Debug, // デバッグ描画
        Custom // カスタム描画
    };

    /**
     * @brief View設定
     */
    struct ViewSettings
    {
        ViewType Type = ViewType::Scene;
        uint32_t Width = 1280;
        uint32_t Height = 720;
        bool bClearColor = true;
        float ClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool bClearDepth = true;
        float ClearDepth = 1.0f;
    };

    /**
     * @brief View基底クラス
     *
     * 描画を実行する単位。複数のViewportを持ち、それぞれにカメラを設定できます。
     * Viewportの描画結果を合成して出力します。
     */
    class View
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        View() = default;

        /**
         * @brief デストラクタ
         */
        virtual ~View() = default;

        // コピー・ムーブ禁止
        View(const View &) = delete;
        View &operator=(const View &) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param settings View設定
         * @return 初期化成功時true
         */
        virtual bool Initialize(const ViewSettings &settings);

        /**
         * @brief 終了処理
         */
        virtual void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // Viewport管理
        // ========================================

        /**
         * @brief Viewportを追加
         * @param viewport 追加するViewport
         * @return 追加されたViewportのインデックス
         */
        uint32_t AddViewport(Container::TSharedPtr<Viewport> viewport);

        /**
         * @brief Viewportを削除
         * @param index 削除するViewportのインデックス
         */
        void RemoveViewport(uint32_t index);

        /**
         * @brief Viewportを取得
         * @param index Viewportのインデックス
         * @return Viewport
         */
        Container::TSharedPtr<Viewport> GetViewport(uint32_t index) const;

        /**
         * @brief Viewport数を取得
         */
        uint32_t GetViewportCount() const { return static_cast<uint32_t>(m_Viewports.size()); }

        /**
         * @brief メインViewportを取得（インデックス0）
         */
        Container::TSharedPtr<Viewport> GetMainViewport() const;

        // ========================================
        // 描画
        // ========================================

        /**
         * @brief Viewの描画を実行
         *
         * 全Viewportを描画し、結果を合成して出力します。
         */
        virtual void Render();

        /**
         * @brief View出力を合成
         *
         * 全Viewportの描画結果を合成します。
         */
        virtual void CompositeViewports();

        // ========================================
        // 出力
        // ========================================

        /**
         * @brief 出力レンダーターゲットを取得
         */
        Container::TSharedPtr<RHI::IRenderTarget> GetOutputRenderTarget() const { return m_OutputRenderTarget; }

        /**
         * @brief 出力テクスチャを取得
         */
        Container::TSharedPtr<RHI::ITexture2D> GetOutputTexture() const { return m_OutputTexture; }

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief Viewタイプを取得
         */
        ViewType GetViewType() const { return m_ViewType; }

        /**
         * @brief 解像度を変更
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        virtual void Resize(uint32_t width, uint32_t height);

        /**
         * @brief 幅を取得
         */
        uint32_t GetWidth() const { return m_Width; }

        /**
         * @brief 高さを取得
         */
        uint32_t GetHeight() const { return m_Height; }

        /**
         * @brief 有効/無効を設定
         */
        void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }
        bool IsEnabled() const { return m_bEnabled; }

    protected:
        // Viewport管理
        Container::VariableArray<Container::TSharedPtr<Viewport>> m_Viewports;

        // 出力リソース
        Container::TSharedPtr<RHI::IRenderTarget> m_OutputRenderTarget;
        Container::TSharedPtr<RHI::ITexture2D> m_OutputTexture;

        // 設定
        ViewType m_ViewType = ViewType::Scene;
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;
        float m_ClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        float m_ClearDepth = 1.0f;
        bool m_bClearColor = true;
        bool m_bClearDepth = true;
        bool m_bEnabled = true;

        // 状態
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "RenderTypes.h"
#include "SceneProxy.h"
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
    /**
     * @brief Viewport設定
     */
    struct ViewportSettings
    {
        // ビューポート矩形（0.0〜1.0の正規化座標）
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 1.0f;
        float Height = 1.0f;

        // 深度範囲
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    /**
     * @brief Viewport
     *
     * 描画の出力先。一つのViewで複数持つことができ、
     * それぞれ一個のカメラを必要とします。
     */
    class Viewport
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Viewport() = default;

        /**
         * @brief デストラクタ
         */
        ~Viewport() = default;

        // コピー・ムーブ禁止
        Viewport(const Viewport &) = delete;
        Viewport &operator=(const Viewport &) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param settings Viewport設定
         * @return 初期化成功時true
         */
        bool Initialize(const ViewportSettings &settings);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        // ========================================
        // カメラ
        // ========================================

        /**
         * @brief カメラを設定
         * @param camera カメラ情報
         */
        void SetCamera(const CameraProxy &camera);

        /**
         * @brief 共有カメラIDを設定
         */
        void SetCameraId(uint64_t cameraId);

        /**
         * @brief 共有カメラIDを取得
         */
        uint64_t GetCameraId() const { return m_CameraId; }

        /**
         * @brief カメラを取得
         */
        const CameraProxy &GetCamera() const { return m_Camera; }

        /**
         * @brief ビュー行列を取得
         */
        const Math::Matrix4x4 &GetViewMatrix() const { return m_ViewMatrix; }

        /**
         * @brief プロジェクション行列を取得
         */
        const Math::Matrix4x4 &GetProjectionMatrix() const { return m_ProjectionMatrix; }

        /**
         * @brief ビュープロジェクション行列を取得
         */
        const Math::Matrix4x4 &GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

        // ========================================
        // ビューポート矩形
        // ========================================

        /**
         * @brief ビューポート矩形を設定（正規化座標）
         * @param x X座標（0.0〜1.0）
         * @param y Y座標（0.0〜1.0）
         * @param width 幅（0.0〜1.0）
         * @param height 高さ（0.0〜1.0）
         */
        void SetRect(float x, float y, float width, float height);

        /**
         * @brief ビューポート矩形を取得
         */
        void GetRect(float &outX, float &outY, float &outWidth, float &outHeight) const;

        /**
         * @brief 深度範囲を設定
         * @param minDepth 最小深度
         * @param maxDepth 最大深度
         */
        void SetDepthRange(float minDepth, float maxDepth);

        /**
         * @brief 深度範囲を取得
         */
        void GetDepthRange(float &outMinDepth, float &outMaxDepth) const;

        // ========================================
        // ピクセル座標への変換
        // ========================================

        /**
         * @brief ピクセル座標でのビューポート矩形を計算
         * @param screenWidth スクリーン幅
         * @param screenHeight スクリーン高さ
         * @param outX 出力X座標（ピクセル）
         * @param outY 出力Y座標（ピクセル）
         * @param outWidth 出力幅（ピクセル）
         * @param outHeight 出力高さ（ピクセル）
         */
        void GetPixelRect(uint32_t screenWidth, uint32_t screenHeight,
                          uint32_t &outX, uint32_t &outY,
                          uint32_t &outWidth, uint32_t &outHeight) const;

        /**
         * @brief アスペクト比を取得
         */
        float GetAspectRatio() const
        {
            return m_Height > 0.0f ? m_Width / m_Height : 1.0f;
        }

        // ========================================
        // 出力
        // ========================================

        /**
         * @brief 出力レンダーターゲットを取得
         */
        Container::TSharedPtr<RHI::IRenderTarget> GetRenderTarget() const { return m_RenderTarget; }

        /**
         * @brief 出力テクスチャを取得
         */
        Container::TSharedPtr<RHI::ITexture2D> GetOutputTexture() const { return m_OutputTexture; }

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief 有効/無効を設定
         */
        void SetEnabled(bool bEnabled) { m_bEnabled = bEnabled; }
        bool IsEnabled() const { return m_bEnabled; }

        /**
         * @brief デバッグ描画モードを設定
         */
        void SetDebugViewMode(DebugViewMode mode);
        DebugViewMode GetDebugViewMode() const { return m_DebugViewMode; }

    private:
        /**
         * @brief 行列を更新
         */
        void UpdateMatrices();

    private:
        // カメラ
        CameraProxy m_Camera;
        uint64_t m_CameraId = 0;

        // 行列
        Math::Matrix4x4 m_ViewMatrix;
        Math::Matrix4x4 m_ProjectionMatrix;
        Math::Matrix4x4 m_ViewProjectionMatrix;

        // ビューポート矩形（正規化座標）
        float m_X = 0.0f;
        float m_Y = 0.0f;
        float m_Width = 1.0f;
        float m_Height = 1.0f;
        float m_MinDepth = 0.0f;
        float m_MaxDepth = 1.0f;

        // 出力リソース
        Container::TSharedPtr<RHI::IRenderTarget> m_RenderTarget;
        Container::TSharedPtr<RHI::ITexture2D> m_OutputTexture;

        // 設定
        DebugViewMode m_DebugViewMode = DebugViewMode::Normal;
        bool m_bEnabled = true;
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

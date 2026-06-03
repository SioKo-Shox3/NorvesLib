#pragma once

#include "RenderTypes.h"
#include "SharedResourceRegistry.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class IDevice;
    class ISwapChain;
    class ICommandList;
    class IRenderTarget;
    class ITexture2D;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class View;

    /**
     * @brief スクリーン設定
     */
    struct ScreenSettings
    {
        void *WindowHandle = nullptr; // ウィンドウハンドル
        uint32_t Width = 1280;        // 幅
        uint32_t Height = 720;        // 高さ
        bool bVSync = true;           // 垂直同期
        bool bFullscreen = false;     // フルスクリーン
        uint32_t BackBufferCount = 2; // バックバッファ数
    };

    /**
     * @brief スクリーン
     *
     * 最終的な描画の出力先。Windowの解像度と一致したサーフェスを持ちます。
     * 複数のViewの出力を合成し、最終的にPresentします。
     *
     * GEngineが保持する描画出力の最終段階です。
     */
    class Screen
    {
    public:
        /**
         * @brief デフォルトコンストラクタ
         */
        Screen() = default;

        /**
         * @brief デストラクタ
         */
        ~Screen() = default;

        // コピー・ムーブ禁止
        Screen(const Screen &) = delete;
        Screen &operator=(const Screen &) = delete;

        // ========================================
        // ライフサイクル
        // ========================================

        /**
         * @brief 初期化
         * @param device RHIデバイス
         * @param settings スクリーン設定
         * @return 初期化成功時true
         */
        bool Initialize(Container::TSharedPtr<RHI::IDevice> device, const ScreenSettings &settings);

        /**
         * @brief 終了処理
         */
        void Shutdown();

        /**
         * @brief 初期化済みかどうか
         */
        bool IsInitialized() const { return m_bInitialized; }

        // ========================================
        // 解像度
        // ========================================

        /**
         * @brief 解像度を変更
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        void Resize(uint32_t width, uint32_t height);

        /**
         * @brief 幅を取得
         */
        uint32_t GetWidth() const { return m_Width; }

        /**
         * @brief 高さを取得
         */
        uint32_t GetHeight() const { return m_Height; }

        /**
         * @brief アスペクト比を取得
         */
        float GetAspectRatio() const
        {
            return m_Height > 0 ? static_cast<float>(m_Width) / static_cast<float>(m_Height) : 1.0f;
        }

        // ========================================
        // View管理
        // ========================================

        /**
         * @brief Viewを追加
         * @param view 追加するView
         * @param priority 描画優先度（小さいほど先に描画）
         */
        void AddView(Container::TSharedPtr<View> view, int32_t priority = 0);

        /**
         * @brief Viewを削除
         * @param view 削除するView
         */
        void RemoveView(Container::TSharedPtr<View> view);

        /**
         * @brief 全Viewを取得
         */
        const Container::VariableArray<Container::TSharedPtr<View>> &GetViews() const { return m_Views; }

        // ========================================
        // 描画
        // ========================================

        /**
         * @brief フレーム開始
         *
         * 次のバックバッファを取得し、描画準備を行います。
         */
        bool BeginFrame();

        /**
         * @brief 全Viewの出力を合成
         *
         * 各Viewの出力をScreenサーフェスに合成します。
         */
        void CompositeViews();

        /**
         * @brief フレーム終了・Present
         *
         * コマンドリストをサブミットし、合成結果を画面に表示します。
         * @param commandList サブミットするコマンドリスト
         */
        void EndFrame(Container::TSharedPtr<RHI::ICommandList> commandList);

        // ========================================
        // RHIアクセス
        // ========================================

        /**
         * @brief スワップチェーンを取得
         */
        Container::TSharedPtr<RHI::ISwapChain> GetSwapChain() const { return m_SwapChain; }

        /**
         * @brief 現在のバックバッファを取得
         */
        Container::TSharedPtr<RHI::IRenderTarget> GetCurrentBackBuffer() const { return m_CurrentBackBuffer; }

        // ========================================
        // 設定
        // ========================================

        /**
         * @brief VSync設定
         */
        void SetVSync(bool bEnabled);
        bool IsVSyncEnabled() const { return m_bVSyncEnabled; }

        /**
         * @brief フルスクリーン設定
         */
        void SetFullscreen(bool bEnabled);
        bool IsFullscreen() const { return m_bFullscreen; }

        // ========================================
        // 共有リソースレジストリ
        // ========================================

        /**
         * @brief View間共有リソースレジストリを取得
         *
         * View間（例: SceneViewの深度をUIViewで参照等）で
         * リソースを共有するためのレジストリです。
         * 毎フレームBeginFrame()時にクリアされます。
         */
        SharedResourceRegistry &GetSharedResourceRegistry() { return m_SharedResourceRegistry; }
        const SharedResourceRegistry &GetSharedResourceRegistry() const { return m_SharedResourceRegistry; }

    private:
        // RHIリソース
        Container::TSharedPtr<RHI::IDevice> m_Device;
        Container::TSharedPtr<RHI::ISwapChain> m_SwapChain;
        Container::TSharedPtr<RHI::IRenderTarget> m_CurrentBackBuffer;

        // View管理
        Container::VariableArray<Container::TSharedPtr<View>> m_Views;
        Container::VariableArray<int32_t> m_ViewPriorities;

        // View間共有リソース
        SharedResourceRegistry m_SharedResourceRegistry;

        // 設定
        uint32_t m_Width = 1280;
        uint32_t m_Height = 720;
        bool m_bVSyncEnabled = true;
        bool m_bFullscreen = false;

        // 状態
        bool m_bInitialized = false;
    };

} // namespace NorvesLib::Core::Rendering

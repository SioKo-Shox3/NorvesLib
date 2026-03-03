#pragma once

#include "RHI/RHITypes.h"
#include <cstdint>

// 前方宣言
namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
    class TransientResourcePool;
    class IRenderPass;
    class IFramebuffer;
}

namespace NorvesLib::Core::Rendering
{
    // 前方宣言
    class SharedResourceRegistry;
    class RenderResourceManager;
    class ShaderManager;
    struct CameraProxy;

    /**
     * @brief View描画コンテキスト
     *
     * View::Render()に渡される描画実行に必要なリソースの参照をまとめた構造体。
     * 各Viewはこのコンテキストからコマンドリスト・一時リソースプール等にアクセスします。
     *
     * 責務:
     * - RHIコマンドリストへの参照提供
     * - TransientResourcePool（一時レンダーターゲット等）への参照提供
     * - View間共有リソースレジストリへの参照提供
     * - フレーム・タイミング情報の提供
     * - 現在のレンダーパス/フレームバッファ情報（SwapChain直接描画用）
     */
    struct ViewRenderContext
    {
        // ========================================
        // RHIリソース
        // ========================================

        /** @brief 描画コマンド記録先 */
        RHI::ICommandList *CommandList = nullptr;

        /** @brief RHIデバイス */
        RHI::IDevice *Device = nullptr;

        /** @brief フレーム内一時リソースプール（レンダーターゲット等） */
        RHI::TransientResourcePool *TransientPool = nullptr;

        // ========================================
        // 共有リソース
        // ========================================

        /** @brief View間でリソースを共有するためのレジストリ */
        SharedResourceRegistry *SharedResources = nullptr;

        /** @brief メッシュGPUデータ等を管理するリソースマネージャー */
        RenderResourceManager *ResourceManager = nullptr;

        /** @brief シェーダーアセットの読み込み・コンパイル・キャッシュ管理 */
        ShaderManager *ShaderMgr = nullptr;

        /** @brief メインカメラ情報（ビュー/プロジェクション行列計算用） */
        const CameraProxy *MainCamera = nullptr;

        // ========================================
        // 現在のレンダーパスコンテキスト
        // ========================================

        /**
         * @brief 現在アクティブなレンダーパス
         *
         * RenderingCoordinatorが既にBeginRenderPassしている場合、
         * パスはこのレンダーパス内で描画を行います。
         * nullptrの場合、パスが自分でBeginRenderPass/EndRenderPassを呼びます。
         */
        RHI::IRenderPass *CurrentRenderPass = nullptr;

        /**
         * @brief 現在アクティブなフレームバッファ
         *
         * SwapChainフレームバッファへの直接描画時に使用されます。
         */
        RHI::IFramebuffer *CurrentFramebuffer = nullptr;

        /**
         * @brief レンダーパスが外部で管理されているか
         *
         * trueの場合、パスはBeginRenderPass/EndRenderPassを呼びません。
         * RenderingCoordinatorが既にレンダーパスを開始している場合にtrueになります。
         */
        bool bRenderPassActive = false;

        // ========================================
        // フレーム情報
        // ========================================

        /** @brief 現在のフレームインデックス（ダブル/トリプルバッファリング用） */
        uint32_t FrameIndex = 0;

        /** @brief スクリーン幅 */
        uint32_t ScreenWidth = 0;

        /** @brief スクリーン高さ */
        uint32_t ScreenHeight = 0;

        /** @brief 前フレームからの経過時間（秒） */
        float DeltaTime = 0.0f;

        /** @brief アプリケーション開始からの経過時間（秒） */
        double TotalTime = 0.0;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "RHITypes.h"

namespace NorvesLib::RHI
{
    /** @brief swapchain image acquire の結果。Success 以外では image は未取得。 */
    enum class SwapChainBeginFrameStatus : uint8_t
    {
        Success,
        OutOfDate,
        NotReady,
        Fatal
    };

    /** @brief queue submission と presentation を分離したフレーム終了状態 */
    enum class SwapChainEndFrameStatus : uint8_t
    {
        Success,
        InvalidCommandList,
        SubmissionSerialExhausted,
        FenceResetFailed,
        SubmitFailed, // fence reset後の失敗。swapchainを再利用せずfatalとして扱う。
        PresentationFailed
    };

    struct SwapChainEndFrameResult
    {
        uint64_t SubmissionSerial = 0;
        SwapChainEndFrameStatus Status = SwapChainEndFrameStatus::InvalidCommandList;

        [[nodiscard]] bool HasError() const
        {
            return Status != SwapChainEndFrameStatus::Success;
        }
    };

    /**
     * @brief スワップチェーンインターフェース
     * スワップチェーンはウィンドウ表示に使用するバックバッファを管理します。
     */
    class ISwapChain
    {
    public:
        virtual ~ISwapChain() = default;

        /**
         * @brief スワップチェーンの幅を取得
         * @return スワップチェーンの幅
         */
        virtual uint32_t GetWidth() const = 0;

        /**
         * @brief スワップチェーンの高さを取得
         * @return スワップチェーンの高さ
         */
        virtual uint32_t GetHeight() const = 0;

        /**
         * @brief スワップチェーンのフォーマットを取得
         * @return フォーマット
         */
        virtual Format GetFormat() const = 0;
        virtual bool ConsumePresentationDirty() = 0;

        /**
         * @brief バックバッファ数を取得
         * @return バックバッファ数
         */
        virtual uint32_t GetBufferCount() const = 0;

        /**
         * @brief 現在のバックバッファインデックスを取得
         * @return 現在のバックバッファインデックス
         */
        virtual uint32_t GetCurrentBackBufferIndex() const = 0;

        /**
         * @brief バックバッファを取得
         * @param index バッファインデックス
         * @return バックバッファテクスチャ
         */
        virtual TexturePtr GetBackBuffer(uint32_t index) const = 0;

        /**
         * @brief 現在のバックバッファを取得
         * @return 現在のバックバッファテクスチャ
         */
        virtual TexturePtr GetCurrentBackBuffer() const = 0;

        /**
         * @brief スワップチェーンのプレゼント（表示）
         * @param vsync 垂直同期を行うかどうか
         */
        virtual void Present(bool vsync = true) = 0;

        /**
         * @brief フレーム開始（次のバックバッファを取得）
         *
         * フェンス待機 → イメージ取得を行います。
         * @return image acquire の構造化結果。Success 以外では image は未取得。
         */
        virtual SwapChainBeginFrameStatus BeginFrame() = 0;

        /**
         * @brief フレーム終了（コマンドリストをサブミット＆プレゼント）
         *
         * セマフォを使用した同期付きでコマンドを送信し、Presentを実行します。
         * @param commandList 実行するコマンドリスト
         * @return submit成功時のserialとpresentationを含む結果。errorでもserialは失われない。
         *
         * SubmissionSerialExhausted はfence reset/submitより前に検出される。
         * SubmitFailed はreset済みfenceが未signalの可能性があるため、呼び出し側は
         * errorを伝播し、このswapchainをBeginFrameで再利用してはならない。
         */
        virtual SwapChainEndFrameResult EndFrame(CommandListPtr commandList) = 0;

        /**
         * @brief BeginFrameで実際のGPU fence完了を確認済みのsubmission serialを取得
         */
        virtual uint64_t GetCompletedSubmissionSerial() const = 0;

        /**
         * @brief スワップチェーンのリサイズ
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        /**
         * @brief 現在のフレームインデックスを取得（フレーム同期用）
         *
         * ダブル/トリプルバッファリングにおけるフレームスロットのインデックスを返します。
         * コマンドバッファのローテーションに使用します。
         * @return 現在のフレームインデックス（0 ～ GetMaxFramesInFlight()-1）
         */
        virtual uint32_t GetCurrentFrameIndex() const = 0;

        /**
         * @brief 同時に処理可能な最大フレーム数を取得
         * @return 最大フレーム・イン・フライト数
         */
        virtual uint32_t GetMaxFramesInFlight() const = 0;
    };

} // namespace NorvesLib::RHI

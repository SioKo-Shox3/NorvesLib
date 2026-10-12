#pragma once

#include "RHI/ISwapChain.h"
#include "RHI/IDevice.h"

// Windowsプラットフォーム用Vulkan拡張
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::VariableArray;

    class VulkanDevice;
    class VulkanTexture;

    namespace Detail
    {
        inline bool IsAcquireStateReady(uint32_t width,
                                        uint32_t height,
                                        bool bHasSwapChain,
                                        bool bHasFrameSync)
        {
            return width > 0 && height > 0 && bHasSwapChain && bHasFrameSync;
        }

        inline SwapChainBeginFrameStatus ClassifyFenceWaitResult(vk::Result result)
        {
            return result == vk::Result::eSuccess
                       ? SwapChainBeginFrameStatus::Success
                       : SwapChainBeginFrameStatus::Fatal;
        }

        inline SwapChainBeginFrameStatus ClassifyAcquireResult(vk::Result result)
        {
            switch (result)
            {
            case vk::Result::eSuccess:
            case vk::Result::eSuboptimalKHR:
                return SwapChainBeginFrameStatus::Success;
            case vk::Result::eErrorOutOfDateKHR:
                return SwapChainBeginFrameStatus::OutOfDate;
            case vk::Result::eNotReady:
            case vk::Result::eTimeout:
                return SwapChainBeginFrameStatus::NotReady;
            default:
                return SwapChainBeginFrameStatus::Fatal;
            }
        }
    } // namespace Detail

    /**
     * @brief Vulkanスワップチェーン実装クラス
     */
    class VulkanSwapChain : public ISwapChain
    {
    public:
        /**
         * @brief コンストラクタ
         * @param device Vulkanデバイス
         * @param desc スワップチェーン作成情報
         */
        VulkanSwapChain(TSharedPtr<VulkanDevice> device, const SwapChainDesc &desc);

        /**
         * @brief デストラクタ
         */
        ~VulkanSwapChain() override;

        /**
         * @brief 次のバックバッファのインデックスを取得
         * @return バックバッファのインデックス
         */
        uint32_t GetCurrentBackBufferIndex() const override;

        /**
         * @brief バックバッファの数を取得
         * @return バックバッファの数
         */
        uint32_t GetBufferCount() const override { return static_cast<uint32_t>(m_swapChainImages.size()); }

        /**
         * @brief バックバッファのテクスチャを取得
         * @param index バックバッファのインデックス
         * @return テクスチャへのポインタ
         */
        TexturePtr GetBackBuffer(uint32_t index) const override;

        /**
         * @brief 現在のバックバッファテクスチャを取得
         * @return 現在のバックバッファテクスチャ
         */
        TexturePtr GetCurrentBackBuffer() const override;

        /**
         * @brief スワップチェーンをリサイズ
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        void Resize(uint32_t width, uint32_t height) override;

        /**
         * @brief フレーム開始（フェンス待機＋イメージ取得）
         * @return image acquire の構造化結果
         */
        SwapChainBeginFrameStatus BeginFrame() override;

        /**
         * @brief フレーム終了（セマフォ同期付きサブミット＋プレゼント）
         * @param commandList 実行するコマンドリスト
         */
        SwapChainEndFrameResult EndFrame(CommandListPtr commandList) override;
        /**
         * @brief フレーム終了（複数のコマンドリストを 1 回でサブミット＋プレゼント）
         * @param commandLists 提出するリストの並び（並びの順に実行する）
         * @param commandListCount 並びの数
         */
        SwapChainEndFrameResult EndFrame(const CommandListPtr *commandLists, uint32_t commandListCount) override;
        uint64_t GetCompletedSubmissionSerial() const override { return m_completedSubmissionSerial; }

        /**
         * @brief スワップチェーンの幅を取得
         * @return 幅
         */
        uint32_t GetWidth() const override { return m_width; }

        /**
         * @brief スワップチェーンの高さを取得
         * @return 高さ
         */
        uint32_t GetHeight() const override { return m_height; }

        /**
         * @brief スワップチェーンのフォーマットを取得
         * @return フォーマット
         */
        Format GetFormat() const override { return m_format; }
        bool ConsumePresentationDirty() override;

        uint32_t GetCurrentFrameIndex() const override { return m_currentFrame; }
        uint32_t GetMaxFramesInFlight() const override { return MAX_FRAMES_IN_FLIGHT; }

        // Vulkan固有の機能
        vk::SwapchainKHR GetVkSwapchain() const { return m_swapChain; }
        vk::SurfaceKHR GetVkSurface() const { return m_surface; }
        vk::Format GetVkFormat() const { return m_vkFormat; }

    private:
        TSharedPtr<VulkanDevice> m_device;
        SwapChainDesc m_desc;

        uint32_t m_width = 0;
        uint32_t m_height = 0;
        Format m_format = Format::UNKNOWN;
        uint32_t m_currentImageIndex = 0;

        // Vulkan固有のメンバ
        vk::SwapchainKHR m_swapChain;
        vk::SurfaceKHR m_surface;
        vk::Format m_vkFormat = vk::Format::eUndefined;
        vk::ColorSpaceKHR m_colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        vk::PresentModeKHR m_presentMode = vk::PresentModeKHR::eFifo;

        // スワップチェーンイメージ関連
        VariableArray<vk::Image> m_swapChainImages;
        VariableArray<TSharedPtr<VulkanTexture>> m_backBufferTextures;

        // 同期オブジェクト
        // imageAvailable とフェンスは飛行中のスロットごと（m_currentFrame で引く）。
        VariableArray<vk::Semaphore> m_imageAvailableSemaphores;
        // 描画の完了はスワップチェーンの画像ごと（m_currentImageIndex で引く）。画像 i を取得できたことで、
        // 前に画像 i を表示した present がこのセマフォを待ち終えたことが保証されるので、次の提出が signal してよい。
        // スロットごとにすると、前の present がまだ待っているセマフォを次の提出が signal しうる。
        VariableArray<vk::Semaphore> m_renderFinishedSemaphores;
        VariableArray<vk::Fence> m_inFlightFences;
        VariableArray<uint64_t> m_frameSlotSubmissionSerials;
        // slot ごとに、その slot の提出に含めたコマンドリスト（次にその slot のフェンスを待ったとき完了を通知する）
        VariableArray<VariableArray<TWeakPtr<ICommandList>>> m_frameSlotCommandLists;
        uint32_t m_currentFrame = 0;
        uint64_t m_nextSubmissionSerial = 0;
        uint64_t m_completedSubmissionSerial = 0;
        static constexpr int MAX_FRAMES_IN_FLIGHT = 1;
        bool m_bPresentationDirty = false;

        // 初期化メソッド
        void CreateSurface();
        void CreateSwapChain();
        void CreateImageViews();
        void CreateSyncObjects();
        /// @brief スワップチェーンの画像の数だけ描画の完了のセマフォを作る（作成と Resize で呼ぶ）
        void CreateRenderFinishedSemaphores();

        // スワップチェーン関連ヘルパー
        /// @brief バックバッファ・描画の完了のセマフォ・スワップチェーンを破棄する（呼び出し側が GPU のアイドルを待ってから呼ぶ）
        void CleanupSwapChain();
        /// @brief slot の提出に含めたコマンドリストへ、その slot の提出番号までの完了を通知して忘れる
        void NotifyFrameSlotCommandListsCompleted(uint32_t frameSlot);
        vk::SurfaceFormatKHR ChooseSurfaceFormat();
        vk::PresentModeKHR ChoosePresentMode();
        vk::Extent2D ChooseSwapExtent();
        uint32_t GetMinImageCount();

        // フォーマット変換ヘルパー
        Format FromVkFormat(vk::Format format) const;
    };

} // namespace NorvesLib::RHI::Vulkan

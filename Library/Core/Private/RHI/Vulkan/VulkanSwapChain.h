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
         * @brief スワップチェーンを画面に表示
         * @param vsync 垂直同期を行うかどうか
         */
        void Present(bool vsync = true) override;

        /**
         * @brief スワップチェーンをリサイズ
         * @param width 新しい幅
         * @param height 新しい高さ
         */
        void Resize(uint32_t width, uint32_t height) override;

        /**
         * @brief フレーム開始（フェンス待機＋イメージ取得）
         * @return 成功時true
         */
        bool BeginFrame() override;

        /**
         * @brief フレーム終了（セマフォ同期付きサブミット＋プレゼント）
         * @param commandList 実行するコマンドリスト
         */
        void EndFrame(CommandListPtr commandList) override;

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
        VariableArray<vk::Semaphore> m_imageAvailableSemaphores;
        VariableArray<vk::Semaphore> m_renderFinishedSemaphores;
        VariableArray<vk::Fence> m_inFlightFences;
        uint32_t m_currentFrame = 0;
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        // 初期化メソッド
        void CreateSurface();
        void CreateSwapChain();
        void CreateImageViews();
        void CreateSyncObjects();

        // スワップチェーン関連ヘルパー
        void CleanupSwapChain();
        vk::SurfaceFormatKHR ChooseSurfaceFormat();
        vk::PresentModeKHR ChoosePresentMode();
        vk::Extent2D ChooseSwapExtent();
        uint32_t GetMinImageCount();

        // フォーマット変換ヘルパー
        Format FromVkFormat(vk::Format format) const;
    };

} // namespace NorvesLib::RHI::Vulkan

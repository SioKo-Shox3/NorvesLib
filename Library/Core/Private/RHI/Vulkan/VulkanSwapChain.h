#pragma once

#include "RHI/ISwapChain.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

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
        VulkanSwapChain(NorvesLib::Core::Container::TSharedPtr<VulkanDevice> device, const SwapChainDesc &desc);

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

        // Vulkan固有の機能
        vk::SwapchainKHR GetVkSwapchain() const { return m_swapChain; }
        vk::SurfaceKHR GetVkSurface() const { return m_surface; }
        vk::Format GetVkFormat() const { return m_vkFormat; }

    private:
        NorvesLib::Core::Container::TSharedPtr<VulkanDevice> m_device;
        SwapChainDesc m_desc;

        uint32_t m_width = 0;
        uint32_t m_height = 0;
        Format m_format = Format::Unknown;
        uint32_t m_currentImageIndex = 0;

        // Vulkan固有のメンバ
        vk::SwapchainKHR m_swapChain;
        vk::SurfaceKHR m_surface;
        vk::Format m_vkFormat = vk::Format::eUndefined;
        vk::ColorSpaceKHR m_colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
        vk::PresentModeKHR m_presentMode = vk::PresentModeKHR::eFifo;

        // スワップチェーンイメージ関連
        NorvesLib::Core::Container::VariableArray<vk::Image> m_swapChainImages;
        NorvesLib::Core::Container::VariableArray<NorvesLib::Core::Container::TSharedPtr<VulkanTexture>> m_backBufferTextures;

        // 同期オブジェクト
        NorvesLib::Core::Container::VariableArray<vk::Semaphore> m_imageAvailableSemaphores;
        NorvesLib::Core::Container::VariableArray<vk::Semaphore> m_renderFinishedSemaphores;
        NorvesLib::Core::Container::VariableArray<vk::Fence> m_inFlightFences;
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

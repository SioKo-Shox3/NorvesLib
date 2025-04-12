#pragma once

#include "RHI/Public/ISwapChain.h"
#include <vulkan/vulkan.h>
#include <memory>
#include "Core/Public/Container/Containers.h"

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
    VulkanSwapChain(std::shared_ptr<VulkanDevice> device, const SwapChainDesc& desc);
    
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
    TexturePtr GetBackBufferTexture(uint32_t index) override;
    
    /**
     * @brief スワップチェーンを画面に表示
     */
    void Present() override;
    
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
    VkSwapchainKHR GetVkSwapchain() const { return m_swapChain; }
    VkSurfaceKHR GetVkSurface() const { return m_surface; }
    VkFormat GetVkFormat() const { return m_vkFormat; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    SwapChainDesc m_desc;
    
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Format m_format = Format::Unknown;
    uint32_t m_currentImageIndex = 0;
    
    // Vulkan固有のメンバ
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkFormat m_vkFormat = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR m_colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    
    // スワップチェーンイメージ関連
    NorvesLib::Core::Container::VariableArray<VkImage> m_swapChainImages;
    NorvesLib::Core::Container::VariableArray<std::shared_ptr<VulkanTexture>> m_backBufferTextures;
    
    // 同期オブジェクト
    NorvesLib::Core::Container::VariableArray<VkSemaphore> m_imageAvailableSemaphores;
    NorvesLib::Core::Container::VariableArray<VkSemaphore> m_renderFinishedSemaphores;
    NorvesLib::Core::Container::VariableArray<VkFence> m_inFlightFences;
    uint32_t m_currentFrame = 0;
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    
    // 初期化メソッド
    void CreateSurface();
    void CreateSwapChain();
    void CreateImageViews();
    void CreateSyncObjects();
    
    // スワップチェーン関連ヘルパー
    void CleanupSwapChain();
    VkSurfaceFormatKHR ChooseSurfaceFormat();
    VkPresentModeKHR ChoosePresentMode();
    VkExtent2D ChooseSwapExtent();
    uint32_t GetMinImageCount();
    
    // フォーマット変換ヘルパー
    Format FromVkFormat(VkFormat format) const;
};

} // namespace NorvesLib::RHI::Vulkan
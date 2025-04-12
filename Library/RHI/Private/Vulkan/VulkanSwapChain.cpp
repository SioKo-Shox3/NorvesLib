#include "VulkanSwapChain.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include "Core/Public/Container/Containers.h"

// Windowsのインクルード（Windowsプラットフォーム用）
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanSwapChain::VulkanSwapChain(std::shared_ptr<VulkanDevice> device, const SwapChainDesc& desc)
    : m_device(device)
    , m_desc(desc)
    , m_width(desc.width)
    , m_height(desc.height)
{
    CreateSurface();
    CreateSwapChain();
    CreateImageViews();
    CreateSyncObjects();
}

// デストラクタ
VulkanSwapChain::~VulkanSwapChain()
{
    // デバイスの処理が完了するのを待機
    vkDeviceWaitIdle(m_device->GetVkDevice());

    // 同期オブジェクトのクリーンアップ
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_device->GetVkDevice(), m_renderFinishedSemaphores[i], nullptr);
        vkDestroySemaphore(m_device->GetVkDevice(), m_imageAvailableSemaphores[i], nullptr);
        vkDestroyFence(m_device->GetVkDevice(), m_inFlightFences[i], nullptr);
    }

    // スワップチェーンのクリーンアップ
    CleanupSwapChain();

    // サーフェスの破棄
    vkDestroySurfaceKHR(m_device->GetVkInstance(), m_surface, nullptr);
}

// 現在のバックバッファインデックスを取得
uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const
{
    return m_currentImageIndex;
}

// 特定インデックスのバックバッファテクスチャを取得
TexturePtr VulkanSwapChain::GetBackBufferTexture(uint32_t index)
{
    if (index < m_backBufferTextures.size()) {
        return m_backBufferTextures[index];
    }
    return nullptr;
}

// スワップチェーンを画面に表示
void VulkanSwapChain::Present()
{
    // 現在のフレームのフェンスが完了するまで待機
    vkWaitForFences(m_device->GetVkDevice(), 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX);

    // 次の画像を取得
    VkResult result = vkAcquireNextImageKHR(
        m_device->GetVkDevice(),
        m_swapChain,
        UINT64_MAX,
        m_imageAvailableSemaphores[m_currentFrame],
        VK_NULL_HANDLE,
        &m_currentImageIndex);
    
    // スワップチェーンの再作成が必要な場合
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        Resize(m_width, m_height);
        return;
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("スワップチェーンの画像の取得に失敗しました");
    }
    
    // レンダリングが完了したらプレゼント
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    
    // 待機するセマフォ
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
    
    // プレゼントするスワップチェーンと画像
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_currentImageIndex;
    
    // プレゼント実行
    result = vkQueuePresentKHR(m_device->GetPresentQueue(), &presentInfo);
    
    // スワップチェーンの再作成が必要な場合
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        Resize(m_width, m_height);
    }
    else if (result != VK_SUCCESS) {
        throw std::runtime_error("画像のプレゼントに失敗しました");
    }
    
    // 次のフレームに進む
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

// スワップチェーンのリサイズ
void VulkanSwapChain::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        return; // ウィンドウが最小化されている場合は何もしない
    }
    
    // デバイスがアイドル状態になるまで待機
    vkDeviceWaitIdle(m_device->GetVkDevice());
    
    // 新しいサイズを保存
    m_width = width;
    m_height = height;
    
    // スワップチェーンを再作成
    CleanupSwapChain();
    CreateSwapChain();
    CreateImageViews();
}

// サーフェスの作成
void VulkanSwapChain::CreateSurface()
{
#ifdef _WIN32
    // Windowsプラットフォーム用のサーフェス作成
    VkWin32SurfaceCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    createInfo.hwnd = static_cast<HWND>(m_desc.windowHandle);
    createInfo.hinstance = GetModuleHandle(nullptr);
    
    if (vkCreateWin32SurfaceKHR(m_device->GetVkInstance(), &createInfo, nullptr, &m_surface) != VK_SUCCESS) {
        throw std::runtime_error("Windowsサーフェスの作成に失敗しました");
    }
#else
    // 他のプラットフォーム用のサーフェス作成はここに追加
    throw std::runtime_error("このプラットフォームではサーフェスの作成がサポートされていません");
#endif
}

// スワップチェーンの作成
void VulkanSwapChain::CreateSwapChain()
{
    // サーフェスのケイパビリティを取得
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->GetVkPhysicalDevice(), m_surface, &capabilities);
    
    // サーフェスフォーマットを選択
    VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat();
    m_vkFormat = surfaceFormat.format;
    m_colorSpace = surfaceFormat.colorSpace;
    
    // プレゼントモードを選択
    m_presentMode = ChoosePresentMode();
    
    // スワップチェーンの範囲を決定
    VkExtent2D extent = ChooseSwapExtent();
    
    // 画像の最小数（ダブルバッファリング + 1）
    uint32_t imageCount = GetMinImageCount();
    
    // スワップチェーン作成情報の設定
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_vkFormat;
    createInfo.imageColorSpace = m_colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1; // 通常のイメージではレイヤー数は1
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // レンダリング用途
    
    // キューファミリーインデックスの取得
    uint32_t queueFamilyIndices[] = {
        m_device->GetGraphicsQueueFamilyIndex(),
        m_device->GetPresentQueueFamilyIndex()
    };
    
    // キューファミリーの共有モードを設定
    if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
        // グラフィックスキューとプレゼントキューが異なる場合は共有モード
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        // 同じキューファミリーの場合は排他モード
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    // 追加の変換指定（通常は変換なし）
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // アルファ合成なし
    createInfo.presentMode = m_presentMode;
    createInfo.clipped = VK_TRUE; // 隠れたピクセルのクリッピングを許可
    createInfo.oldSwapchain = VK_NULL_HANDLE; // 古いスワップチェーンはない
    
    // スワップチェーンの作成
    if (vkCreateSwapchainKHR(m_device->GetVkDevice(), &createInfo, nullptr, &m_swapChain) != VK_SUCCESS) {
        throw std::runtime_error("スワップチェーンの作成に失敗しました");
    }
    
    // スワップチェーンイメージの取得
    uint32_t actualImageCount;
    vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &actualImageCount, nullptr);
    m_swapChainImages.resize(actualImageCount);
    vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &actualImageCount, m_swapChainImages.data());
    
    // サイズとフォーマットを保存
    m_width = extent.width;
    m_height = extent.height;
    m_format = FromVkFormat(m_vkFormat);
}

// イメージビューの作成
void VulkanSwapChain::CreateImageViews()
{
    // バックバッファテクスチャの配列をリサイズ
    m_backBufferTextures.resize(m_swapChainImages.size());

    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        // テクスチャ記述子の設定
        TextureDesc textureDesc = {};
        textureDesc.width = m_width;
        textureDesc.height = m_height;
        textureDesc.format = m_format;
        textureDesc.usage = TextureUsage::RenderTarget;
        textureDesc.initialState = ResourceState::Present;
        
        // VulkanTextureを作成（既存のVkImageを使用）
        m_backBufferTextures[i] = NorvesLib::Core::Container::MakeShared<VulkanTexture>(m_device, textureDesc, m_swapChainImages[i]);
    }
}

// 同期オブジェクトの作成
void VulkanSwapChain::CreateSyncObjects()
{
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 初期状態はシグナル
    
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(m_device->GetVkDevice(), &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(m_device->GetVkDevice(), &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(m_device->GetVkDevice(), &fenceInfo, nullptr, &m_inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("同期オブジェクトの作成に失敗しました");
        }
    }
}

// スワップチェーンのクリーンアップ
void VulkanSwapChain::CleanupSwapChain()
{
    // バックバッファテクスチャをクリア
    m_backBufferTextures.clear();
    
    // スワップチェーンを破棄
    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device->GetVkDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
}

// サーフェスフォーマットの選択
VkSurfaceFormatKHR VulkanSwapChain::ChooseSurfaceFormat()
{
    // 利用可能なサーフェスフォーマットを取得
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->GetVkPhysicalDevice(), m_surface, &formatCount, nullptr);
    
    NorvesLib::Core::Container::VariableArray<VkSurfaceFormatKHR> availableFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        m_device->GetVkPhysicalDevice(), m_surface, &formatCount, availableFormats.data());
    
    // 優先フォーマットを探す（SRGB + B8G8R8A8）
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    
    // 優先フォーマットが見つからなければ、最初のフォーマットを使用
    return availableFormats[0];
}

// プレゼントモードの選択
VkPresentModeKHR VulkanSwapChain::ChoosePresentMode()
{
    // 利用可能なプレゼントモードを取得
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_device->GetVkPhysicalDevice(), m_surface, &presentModeCount, nullptr);
    
    NorvesLib::Core::Container::VariableArray<VkPresentModeKHR> availablePresentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        m_device->GetVkPhysicalDevice(), m_surface, &presentModeCount, availablePresentModes.data());
    
    // VSync設定に基づいてプレゼントモードを選択
    if (!m_desc.vsync) {
        // VSync無効 - メールボックスモードを優先（ティアリングなし、低遅延）
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        
        // メールボックスモードがなければ即時モード（ティアリングあり、最低遅延）
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                return availablePresentMode;
            }
        }
    }
    
    // VSyncが有効か、他のモードが利用できない場合はFIFOモード（Vブランク同期）
    return VK_PRESENT_MODE_FIFO_KHR;
}

// スワップチェーンの範囲の選択
VkExtent2D VulkanSwapChain::ChooseSwapExtent()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->GetVkPhysicalDevice(), m_surface, &capabilities);
    
    // currentExtentが特殊値でなければ、それを使用
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        // ウィンドウサイズから適切な範囲を設定
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(m_width),
            static_cast<uint32_t>(m_height)
        };
        
        // 最小・最大の制約内に収める
        actualExtent.width = std::clamp(actualExtent.width, 
                                       capabilities.minImageExtent.width, 
                                       capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, 
                                        capabilities.minImageExtent.height, 
                                        capabilities.maxImageExtent.height);
        
        return actualExtent;
    }
}

// 最小イメージ数の取得
uint32_t VulkanSwapChain::GetMinImageCount()
{
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->GetVkPhysicalDevice(), m_surface, &capabilities);
    
    // トリプルバッファリングを推奨するが、サポートされる最大数を超えないようにする
    uint32_t imageCount = capabilities.minImageCount + 1;
    
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    return imageCount;
}

// VkFormatからRHIのFormatに変換
Format VulkanSwapChain::FromVkFormat(VkFormat format) const
{
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_UNORM;
        case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8_SRGB;
        case VK_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8_UNORM;
        case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8_SRGB;
        default: return Format::Unknown;
    }
}

} // namespace NorvesLib::RHI::Vulkan
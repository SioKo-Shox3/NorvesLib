#include "VulkanSwapChain.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include "VulkanCommandList.h"
#include <stdexcept>
#include <algorithm>
#include "Container/Containers.h"

// Windowsのインクルード（ヘッダで定義済みだが追加インクルードが必要）
#ifdef _WIN32
#include <Windows.h>
#include <vulkan/vulkan_win32.h>
#endif

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    // コンストラクタ
    VulkanSwapChain::VulkanSwapChain(TSharedPtr<VulkanDevice> device, const SwapChainDesc &desc)
        : m_device(device), m_desc(desc), m_width(desc.width), m_height(desc.height)
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
        m_device->GetVkDevice().waitIdle();

        // 同期オブジェクトのクリーンアップ
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_device->GetVkDevice().destroySemaphore(m_renderFinishedSemaphores[i]);
            m_device->GetVkDevice().destroySemaphore(m_imageAvailableSemaphores[i]);
            m_device->GetVkDevice().destroyFence(m_inFlightFences[i]);
        }

        // スワップチェーンのクリーンアップ
        CleanupSwapChain();

        // サーフェスの破棄
        m_device->GetVkInstance().destroySurfaceKHR(m_surface);
    }

    // 現在のバックバッファインデックスを取得
    uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const
    {
        return m_currentImageIndex;
    }

    // 特定インデックスのバックバッファテクスチャを取得
    TexturePtr VulkanSwapChain::GetBackBuffer(uint32_t index) const
    {
        if (index < m_backBufferTextures.size())
        {
            return m_backBufferTextures[index];
        }
        return nullptr;
    }

    // 現在のバックバッファテクスチャを取得
    TexturePtr VulkanSwapChain::GetCurrentBackBuffer() const
    {
        if (m_currentImageIndex < m_backBufferTextures.size())
        {
            return m_backBufferTextures[m_currentImageIndex];
        }
        return nullptr;
    }

    // スワップチェーンを画面に表示
    void VulkanSwapChain::Present(bool vsync)
    {
        // 現在のフレームのフェンスが完了するまで待機
        auto waitResult = m_device->GetVkDevice().waitForFences(
            m_inFlightFences[m_currentFrame],
            VK_TRUE,
            UINT64_MAX);

        if (waitResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("フェンスの待機に失敗しました");
        }

        // 次の画像を取得
        auto acquireResult = m_device->GetVkDevice().acquireNextImageKHR(
            m_swapChain,
            UINT64_MAX,
            m_imageAvailableSemaphores[m_currentFrame],
            nullptr);

        // スワップチェーンの再作成が必要な場合
        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            Resize(m_width, m_height);
            return;
        }
        else if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR)
        {
            throw std::runtime_error("次のスワップチェーン画像の取得に失敗しました");
        }

        m_currentImageIndex = acquireResult.value;

        // レンダリングが完了したシグナルを待つ
        m_device->GetVkDevice().resetFences(m_inFlightFences[m_currentFrame]);

        // プレゼント情報の設定
        vk::PresentInfoKHR presentInfo = {};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapChain;
        presentInfo.pImageIndices = &m_currentImageIndex;

        // プレゼント実行
        vk::Result presentResult = m_device->GetPresentQueue().presentKHR(presentInfo);

        // スワップチェーンの再作成が必要な場合
        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            Resize(m_width, m_height);
        }
        else if (presentResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("画像のプレゼントに失敗しました");
        }

        // 次のフレームに進む
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // スワップチェーンのリサイズ
    void VulkanSwapChain::Resize(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return; // ウィンドウが最小化されている場合は何もしない
        }

        // デバイスがアイドル状態になるまで待機
        m_device->GetVkDevice().waitIdle();

        // 新しいサイズを保存
        m_width = width;
        m_height = height;

        // スワップチェーンを再作成
        CleanupSwapChain();
        CreateSwapChain();
        CreateImageViews();
    }

    // フレーム開始（フェンス待機＋イメージ取得）
    bool VulkanSwapChain::BeginFrame()
    {
        // 現在のフレームのフェンスが完了するまで待機
        auto waitResult = m_device->GetVkDevice().waitForFences(
            m_inFlightFences[m_currentFrame],
            VK_TRUE,
            UINT64_MAX);

        if (waitResult != vk::Result::eSuccess)
        {
            return false;
        }

        // 次の画像を取得
        auto acquireResult = m_device->GetVkDevice().acquireNextImageKHR(
            m_swapChain,
            UINT64_MAX,
            m_imageAvailableSemaphores[m_currentFrame],
            nullptr);

        // スワップチェーンの再作成が必要な場合
        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            Resize(m_width, m_height);
            return false;
        }
        else if (acquireResult.result != vk::Result::eSuccess && acquireResult.result != vk::Result::eSuboptimalKHR)
        {
            return false;
        }

        m_currentImageIndex = acquireResult.value;

        // フェンスをリセット（イメージ取得成功後にリセット）
        m_device->GetVkDevice().resetFences(m_inFlightFences[m_currentFrame]);

        return true;
    }

    // フレーム終了（セマフォ同期付きサブミット＋プレゼント）
    void VulkanSwapChain::EndFrame(CommandListPtr commandList)
    {
        auto vulkanCmdList = DynamicPointerCast<VulkanCommandList>(commandList);
        if (!vulkanCmdList)
        {
            return;
        }

        vk::CommandBuffer cmdBuffer = vulkanCmdList->GetVkCommandBuffer();

        // セマフォ同期付きでサブミット
        vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;
        vk::SubmitInfo submitInfo;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &m_imageAvailableSemaphores[m_currentFrame];
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &m_renderFinishedSemaphores[m_currentFrame];

        auto submitResult = m_device->GetGraphicsQueue().submit(1, &submitInfo, m_inFlightFences[m_currentFrame]);
        if (submitResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファのサブミットに失敗しました");
        }

        // プレゼント
        vk::PresentInfoKHR presentInfo = {};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &m_renderFinishedSemaphores[m_currentFrame];
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_swapChain;
        presentInfo.pImageIndices = &m_currentImageIndex;

        vk::Result presentResult = m_device->GetPresentQueue().presentKHR(presentInfo);

        if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
        {
            Resize(m_width, m_height);
        }
        else if (presentResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("画像のプレゼントに失敗しました");
        }

        // 次のフレームに進む
        m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // サーフェスの作成
    void VulkanSwapChain::CreateSurface()
    {
#ifdef _WIN32
        // Windowsプラットフォーム用のサーフェス作成
        vk::Win32SurfaceCreateInfoKHR createInfo = {};
        createInfo.hwnd = static_cast<HWND>(m_desc.windowHandle);
        createInfo.hinstance = GetModuleHandle(nullptr);

        auto surfaceResult = m_device->GetVkInstance().createWin32SurfaceKHR(createInfo);
        if (surfaceResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("Win32サーフェスの作成に失敗しました");
        }
        m_surface = surfaceResult.value;
#else
        // 他のプラットフォーム用のサーフェス作成はここに追加
        throw std::runtime_error("このプラットフォームではサーフェスの作成がサポートされていません");
#endif
    }

    // スワップチェーンの作成
    void VulkanSwapChain::CreateSwapChain()
    {
        // サーフェスのケイパビリティを取得
        auto capabilitiesResult = m_device->GetVkPhysicalDevice().getSurfaceCapabilitiesKHR(m_surface);
        if (capabilitiesResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("サーフェスケーパビリティの取得に失敗しました");
        }
        vk::SurfaceCapabilitiesKHR capabilities = capabilitiesResult.value;

        // サーフェスフォーマットを選択
        vk::SurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat();
        m_vkFormat = surfaceFormat.format;
        m_colorSpace = surfaceFormat.colorSpace;

        // プレゼントモードを選択
        m_presentMode = ChoosePresentMode();

        // スワップチェーンの範囲を決定
        vk::Extent2D extent = ChooseSwapExtent();

        // 画像の最小数（ダブルバッファリング + 1）
        uint32_t imageCount = GetMinImageCount();

        // スワップチェーン作成情報の設定
        vk::SwapchainCreateInfoKHR createInfo = {};
        createInfo.surface = m_surface;
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = m_vkFormat;
        createInfo.imageColorSpace = m_colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;                                  // 通常のイメージではレイヤー数は1
        createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment; // レンダリング用途

        // キューファミリーインデックスの取得
        // 注: GetGraphicsQueueFamilyIndex をグラフィックスとプレゼント両方に使用
        // （通常は同じキューファミリーがプレゼントもサポート）
        uint32_t graphicsQueueFamily = m_device->GetGraphicsQueueFamilyIndex();
        VariableArray<uint32_t> queueFamilyIndices = {graphicsQueueFamily};

        // キューファミリーの共有モードを設定（単一キューファミリーなので排他モード）
        createInfo.imageSharingMode = vk::SharingMode::eExclusive;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;

        // 追加の変換指定（通常は変換なし）
        createInfo.preTransform = capabilities.currentTransform;
        createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque; // アルファ合成なし
        createInfo.presentMode = m_presentMode;
        createInfo.clipped = VK_TRUE;      // 隠れたピクセルのクリッピングを許可
        createInfo.oldSwapchain = nullptr; // 古いスワップチェーンはない

        // スワップチェーンの作成
        auto swapChainResult = m_device->GetVkDevice().createSwapchainKHR(createInfo);
        if (swapChainResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("スワップチェーンの作成に失敗しました");
        }
        m_swapChain = swapChainResult.value;

        // スワップチェーンイメージの取得
        auto imagesResult = m_device->GetVkDevice().getSwapchainImagesKHR(m_swapChain);
        if (imagesResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("スワップチェーンイメージの取得に失敗しました");
        }
        // std::vectorからVariableArrayへコピー
        const auto &images = imagesResult.value;
        m_swapChainImages.clear();
        m_swapChainImages.reserve(images.size());
        for (const auto &image : images)
        {
            m_swapChainImages.push_back(image);
        }

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

        for (size_t i = 0; i < m_swapChainImages.size(); i++)
        {
            // テクスチャ記述子の設定
            TextureDesc textureDesc = {};
            textureDesc.Width = m_width;
            textureDesc.Height = m_height;
            textureDesc.TextureFormat = m_format;
            textureDesc.Usage = ResourceUsage::RenderTarget;

            // VulkanTextureを作成（既存のvk::Imageを使用）
            m_backBufferTextures[i] = MakeShared<VulkanTexture>(m_device, textureDesc, m_swapChainImages[i]);
        }
    }

    // 同期オブジェクトの作成
    void VulkanSwapChain::CreateSyncObjects()
    {
        m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        vk::SemaphoreCreateInfo semaphoreInfo = {};

        vk::FenceCreateInfo fenceInfo = {};
        fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled; // 初期状態はシグナル

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
        {
            m_imageAvailableSemaphores[i] = m_device->GetVkDevice().createSemaphore(semaphoreInfo).value;
            m_renderFinishedSemaphores[i] = m_device->GetVkDevice().createSemaphore(semaphoreInfo).value;
            m_inFlightFences[i] = m_device->GetVkDevice().createFence(fenceInfo).value;
        }
    }

    // スワップチェーンのクリーンアップ
    void VulkanSwapChain::CleanupSwapChain()
    {
        // バックバッファテクスチャをクリア
        m_backBufferTextures.clear();

        // スワップチェーンを破棄
        if (m_swapChain)
        {
            m_device->GetVkDevice().destroySwapchainKHR(m_swapChain);
            m_swapChain = nullptr;
        }
    }

    // サーフェスフォーマットの選択
    vk::SurfaceFormatKHR VulkanSwapChain::ChooseSurfaceFormat()
    {
        // 利用可能なサーフェスフォーマットを取得
        auto formatsResult = m_device->GetVkPhysicalDevice().getSurfaceFormatsKHR(m_surface);
        if (formatsResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("サーフェスフォーマットの取得に失敗しました");
        }
        auto &availableFormats = formatsResult.value;

        // 優先フォーマットを探す（SRGB + B8G8R8A8）
        for (const auto &availableFormat : availableFormats)
        {
            if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
                availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
            {
                return availableFormat;
            }
        }

        // 優先フォーマットが見つからなければ、最初のフォーマットを使用
        return availableFormats[0];
    }

    // プレゼントモードの選択
    vk::PresentModeKHR VulkanSwapChain::ChoosePresentMode()
    {
        // 利用可能なプレゼントモードを取得
        auto presentModesResult = m_device->GetVkPhysicalDevice().getSurfacePresentModesKHR(m_surface);
        if (presentModesResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("プレゼントモードの取得に失敗しました");
        }
        auto &availablePresentModes = presentModesResult.value;

        // VSync設定に基づいてプレゼントモードを選択
        if (!m_desc.vsync)
        {
            // VSync無効 - メールボックスモードを優先（ティアリングなし、低遅延）
            for (const auto &availablePresentMode : availablePresentModes)
            {
                if (availablePresentMode == vk::PresentModeKHR::eMailbox)
                {
                    return availablePresentMode;
                }
            }

            // メールボックスモードがなければ即時モード（ティアリングあり、最低遅延）
            for (const auto &availablePresentMode : availablePresentModes)
            {
                if (availablePresentMode == vk::PresentModeKHR::eImmediate)
                {
                    return availablePresentMode;
                }
            }
        }

        // VSyncが有効か、他のモードが利用できない場合はFIFOモード（Vブランク同期）
        return vk::PresentModeKHR::eFifo;
    }

    // スワップチェーンの範囲の選択
    vk::Extent2D VulkanSwapChain::ChooseSwapExtent()
    {
        auto capabilitiesResult = m_device->GetVkPhysicalDevice().getSurfaceCapabilitiesKHR(m_surface);
        if (capabilitiesResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("サーフェスケーパビリティの取得に失敗しました");
        }
        vk::SurfaceCapabilitiesKHR capabilities = capabilitiesResult.value;

        // currentExtentが特殊値でなければ、それを使用
        if (capabilities.currentExtent.width != UINT32_MAX)
        {
            return capabilities.currentExtent;
        }
        else
        {
            // ウィンドウサイズから適切な範囲を設定
            vk::Extent2D actualExtent = {
                static_cast<uint32_t>(m_width),
                static_cast<uint32_t>(m_height)};

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
        auto capabilitiesResult = m_device->GetVkPhysicalDevice().getSurfaceCapabilitiesKHR(m_surface);
        if (capabilitiesResult.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("サーフェスケーパビリティの取得に失敗しました");
        }
        vk::SurfaceCapabilitiesKHR capabilities = capabilitiesResult.value;

        // トリプルバッファリングを推奨するが、サポートされる最大数を超えないようにする
        uint32_t imageCount = capabilities.minImageCount + 1;

        if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
        {
            imageCount = capabilities.maxImageCount;
        }

        return imageCount;
    }

    // vk::FormatからRHIのFormatに変換
    Format VulkanSwapChain::FromVkFormat(vk::Format format) const
    {
        switch (format)
        {
        case vk::Format::eR8G8B8A8Unorm:
            return Format::R8G8B8A8_UNORM;
        case vk::Format::eR8G8B8A8Srgb:
            return Format::R8G8B8A8_SRGB;
        case vk::Format::eB8G8R8A8Unorm:
            return Format::B8G8R8A8_UNORM;
        case vk::Format::eB8G8R8A8Srgb:
            return Format::B8G8R8A8_SRGB;
        default:
            return Format::UNKNOWN;
        }
    }

} // namespace NorvesLib::RHI::Vulkan

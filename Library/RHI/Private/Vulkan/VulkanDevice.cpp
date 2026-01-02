#include "VulkanDevice.h"
#include <iostream>
#include <algorithm>
#include "Core/Public/Container/Containers.h"

// Dynamic dispatcherの定義
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace NorvesLib::RHI::Vulkan
{

using namespace NorvesLib::Core::Container;

// バリデーションレイヤー名
const VariableArray<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// 必要なデバイス拡張機能
const VariableArray<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ファクトリメソッド
DevicePtr VulkanDevice::Create(const VulkanInitParams& params)
{
    return MakeShared<VulkanDevice>(params.bEnableValidation);
}

// コンストラクタ
VulkanDevice::VulkanDevice(bool bEnableValidation)
    : m_bValidationEnabled(bEnableValidation)
{
    // Dynamic dispatcherの初期化
    static vk::DynamicLoader dl;
    auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
    VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

    CreateInstance();
    
    if (m_bValidationEnabled)
    {
        SetupDebugMessenger();
    }
    
    PickPhysicalDevice();
    CreateLogicalDevice();
    CreateCommandPool();
    InitFormatMaps();
}

// デストラクタ
VulkanDevice::~VulkanDevice()
{
    // コマンドプールを破棄
    if (m_commandPool)
    {
        m_device.destroyCommandPool(m_commandPool);
    }
    
    // デバイスを破棄
    if (m_device)
    {
        m_device.destroy();
    }
    
    // デバッグメッセンジャーを破棄
    if (m_debugMessenger)
    {
        m_instance.destroyDebugUtilsMessengerEXT(m_debugMessenger);
    }
    
    // インスタンスを破棄
    if (m_instance)
    {
        m_instance.destroy();
    }
}

// Vulkanインスタンス作成
void VulkanDevice::CreateInstance()
{
    // バリデーションレイヤーのチェック
    if (m_bValidationEnabled && !CheckValidationLayerSupport())
    {
        throw std::runtime_error("バリデーションレイヤーが利用できません");
    }
    
    // アプリケーション情報
    vk::ApplicationInfo appInfo{};
    appInfo.pApplicationName = "NorvesLib Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "NorvesLib Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    // インスタンス作成情報
    auto extensions = GetRequiredExtensions();
    
    vk::InstanceCreateInfo createInfo{};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    // デバッグメッセンジャー情報
    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    
    if (m_bValidationEnabled)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        
        // デバッグメッセンジャー情報
        debugCreateInfo.messageSeverity = 
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | 
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | 
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        debugCreateInfo.messageType = 
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | 
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | 
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        
        createInfo.pNext = &debugCreateInfo;
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    
    // インスタンス作成
    auto result = vk::createInstance(createInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("Vulkanインスタンスの作成に失敗しました");
    }
    m_instance = result.value;
    
    // インスタンスレベルの関数ポインタを初期化
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_instance);
}

// デバッグメッセンジャーのセットアップ
void VulkanDevice::SetupDebugMessenger()
{
    vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.messageSeverity = 
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | 
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | 
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType = 
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | 
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | 
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = DebugCallback;
    
    auto result = m_instance.createDebugUtilsMessengerEXT(createInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("デバッグメッセンジャーの設定に失敗しました");
    }
    m_debugMessenger = result.value;
}

// 物理デバイスの選択
void VulkanDevice::PickPhysicalDevice()
{
    // デバイス一覧取得
    auto devicesResult = m_instance.enumeratePhysicalDevices();
    if (devicesResult.result != vk::Result::eSuccess || devicesResult.value.empty())
    {
        throw std::runtime_error("Vulkanをサポートするデバイスが見つかりません");
    }
    
    auto devices = devicesResult.value;
    
    // 適切なデバイスを探す
    for (const auto& device : devices)
    {
        if (IsDeviceSuitable(device))
        {
            m_physicalDevice = device;
            
            // デバイス情報を取得
            m_deviceProperties = m_physicalDevice.getProperties();
            m_deviceFeatures = m_physicalDevice.getFeatures();
            m_memoryProperties = m_physicalDevice.getMemoryProperties();
            
            // キューファミリーを取得
            FindQueueFamilies(m_physicalDevice);
            break;
        }
    }
    
    if (!m_physicalDevice)
    {
        throw std::runtime_error("適切なGPUデバイスが見つかりません");
    }
}

// 論理デバイスの作成
void VulkanDevice::CreateLogicalDevice()
{
    // 重複のないキューファミリインデックスのセット
    Set<uint32_t> uniqueQueueFamilies;
    uniqueQueueFamilies.insert(m_graphicsQueueFamilyIndex);
    uniqueQueueFamilies.insert(m_presentQueueFamilyIndex);
    uniqueQueueFamilies.insert(m_computeQueueFamilyIndex);
    
    // 転送キューを追加
    if (m_transferQueueFamilyIndex != UINT32_MAX)
    {
        uniqueQueueFamilies.insert(m_transferQueueFamilyIndex);
    }
    
    // キュー作成情報
    float queuePriority = 1.0f;
    VariableArray<vk::DeviceQueueCreateInfo> queueCreateInfos;
    
    for (uint32_t queueFamily : uniqueQueueFamilies)
    {
        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // デバイス機能の設定
    vk::PhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE; // ワイヤーフレームなどのサポート
    
    // デバイス作成情報
    auto extensions = GetDeviceExtensions();
    
    vk::DeviceCreateInfo createInfo{};
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    // バリデーションレイヤー
    if (m_bValidationEnabled)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else
    {
        createInfo.enabledLayerCount = 0;
    }
    
    // デバイス作成
    auto result = m_physicalDevice.createDevice(createInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("Vulkan論理デバイスの作成に失敗しました");
    }
    m_device = result.value;
    
    // デバイスレベルの関数ポインタを初期化
    VULKAN_HPP_DEFAULT_DISPATCHER.init(m_device);
    
    // キューハンドルを取得
    m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamilyIndex, 0);
    m_presentQueue = m_device.getQueue(m_presentQueueFamilyIndex, 0);
    m_computeQueue = m_device.getQueue(m_computeQueueFamilyIndex, 0);
    
    if (m_transferQueueFamilyIndex != UINT32_MAX)
    {
        m_transferQueue = m_device.getQueue(m_transferQueueFamilyIndex, 0);
    }
    else
    {
        m_transferQueue = m_graphicsQueue;
    }
}

// コマンドプールの作成
void VulkanDevice::CreateCommandPool()
{
    vk::CommandPoolCreateInfo poolInfo{};
    poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
    
    auto result = m_device.createCommandPool(poolInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドプールの作成に失敗しました");
    }
    m_commandPool = result.value;
}

// フォーマット変換マップの初期化
void VulkanDevice::InitFormatMaps()
{
    // RHI Format → vk::Format
    m_formatMap[Format::R8_UNORM] = vk::Format::eR8Unorm;
    m_formatMap[Format::R8G8_UNORM] = vk::Format::eR8G8Unorm;
    m_formatMap[Format::R8G8B8A8_UNORM] = vk::Format::eR8G8B8A8Unorm;
    m_formatMap[Format::R8G8B8A8_SRGB] = vk::Format::eR8G8B8A8Srgb;
    m_formatMap[Format::B8G8R8A8_UNORM] = vk::Format::eB8G8R8A8Unorm;
    m_formatMap[Format::B8G8R8A8_SRGB] = vk::Format::eB8G8R8A8Srgb;
    m_formatMap[Format::R16_FLOAT] = vk::Format::eR16Sfloat;
    m_formatMap[Format::R16G16_FLOAT] = vk::Format::eR16G16Sfloat;
    m_formatMap[Format::R16G16B16A16_FLOAT] = vk::Format::eR16G16B16A16Sfloat;
    m_formatMap[Format::R32_FLOAT] = vk::Format::eR32Sfloat;
    m_formatMap[Format::R32G32_FLOAT] = vk::Format::eR32G32Sfloat;
    m_formatMap[Format::R32G32B32_FLOAT] = vk::Format::eR32G32B32Sfloat;
    m_formatMap[Format::R32G32B32A32_FLOAT] = vk::Format::eR32G32B32A32Sfloat;
    m_formatMap[Format::D16_UNORM] = vk::Format::eD16Unorm;
    m_formatMap[Format::D24_UNORM_S8_UINT] = vk::Format::eD24UnormS8Uint;
    m_formatMap[Format::D32_FLOAT] = vk::Format::eD32Sfloat;
    
    // vk::Format → RHI Format (逆変換マップも作成)
    for (const auto& [rhiFormat, vkFormat] : m_formatMap)
    {
        m_reverseFormatMap[vkFormat] = rhiFormat;
    }
}

// バリデーションレイヤーのサポート確認
bool VulkanDevice::CheckValidationLayerSupport()
{
    // レイヤー一覧の取得
    auto layersResult = vk::enumerateInstanceLayerProperties();
    if (layersResult.result != vk::Result::eSuccess)
    {
        return false;
    }
    
    auto availableLayers = layersResult.value;
    
    // 必要なレイヤーが全て存在するか確認
    for (const char* layerName : validationLayers)
    {
        bool bLayerFound = false;
        
        for (const auto& layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                bLayerFound = true;
                break;
            }
        }
        
        if (!bLayerFound)
        {
            return false;
        }
    }
    
    return true;
}

// デバイスが適切かどうかの判定
bool VulkanDevice::IsDeviceSuitable(vk::PhysicalDevice device)
{
    // 物理デバイスのプロパティと機能
    auto deviceProperties = device.getProperties();
    auto deviceFeatures = device.getFeatures();
    
    // キューファミリーのサポートチェック
    FindQueueFamilies(device);
    bool bHasRequiredQueueFamilies = 
        m_graphicsQueueFamilyIndex != UINT32_MAX && 
        m_computeQueueFamilyIndex != UINT32_MAX;
    
    // 拡張機能のサポートチェック
    auto extensionsResult = device.enumerateDeviceExtensionProperties();
    if (extensionsResult.result != vk::Result::eSuccess)
    {
        return false;
    }
    
    auto availableExtensions = extensionsResult.value;
    
    Set<String> requiredExtensions;
    for (const auto& ext : deviceExtensions)
    {
        requiredExtensions.insert(String(ext));
    }
    
    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(String(extension.extensionName.data()));
    }
    
    bool bExtensionsSupported = requiredExtensions.empty();
    
    // 物理デバイスの選定
    bool bIsDiscrete = deviceProperties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
    bool bHasAnisotropySupport = deviceFeatures.samplerAnisotropy;
    
    return bHasRequiredQueueFamilies && 
           bExtensionsSupported && 
           bHasAnisotropySupport && 
           bIsDiscrete;  // 離散GPUを優先
}

// 必要なインスタンス拡張機能を取得
VariableArray<const char*> VulkanDevice::GetRequiredExtensions()
{
    VariableArray<const char*> extensions;
    
    // ウィンドウシステム連携のための拡張機能
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    
    // プラットフォーム固有の拡張機能
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#endif
    
    // バリデーション関連の拡張機能
    if (m_bValidationEnabled)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    return extensions;
}

// 必要なデバイス拡張機能を取得
VariableArray<const char*> VulkanDevice::GetDeviceExtensions()
{
    return deviceExtensions;
}

// キューファミリーのインデックス取得
void VulkanDevice::FindQueueFamilies(vk::PhysicalDevice device)
{
    // キューファミリーのプロパティ取得
    auto queueFamilies = device.getQueueFamilyProperties();
    
    // グラフィックスキューファミリーを探す
    for (uint32_t i = 0; i < queueFamilies.size(); i++)
    {
        const auto& queueFamily = queueFamilies[i];
        
        // グラフィックスキュー
        if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)
        {
            m_graphicsQueueFamilyIndex = i;
            m_presentQueueFamilyIndex = i;  // 通常はグラフィックスキューでプレゼントも可能
        }
        
        // コンピュートキュー（可能ならグラフィックスとは別のキューを使用）
        if (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)
        {
            if (m_computeQueueFamilyIndex == UINT32_MAX ||
                !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
            {
                m_computeQueueFamilyIndex = i;
            }
        }
        
        // 転送専用キュー（可能ならグラフィックスとは別のキューを使用）
        if (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer)
        {
            if (m_transferQueueFamilyIndex == UINT32_MAX ||
                !(queueFamily.queueFlags & vk::QueueFlagBits::eGraphics))
            {
                m_transferQueueFamilyIndex = i;
            }
        }
    }
    
    // コンピュートキューがない場合はグラフィックスキューで代用
    if (m_computeQueueFamilyIndex == UINT32_MAX)
    {
        m_computeQueueFamilyIndex = m_graphicsQueueFamilyIndex;
    }
}

// メモリタイプのインデックス検索
uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const
{
    for (uint32_t i = 0; i < m_memoryProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) && 
            (m_memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    
    throw std::runtime_error("適切なメモリタイプが見つかりません");
}

// 単発コマンドバッファ開始
vk::CommandBuffer VulkanDevice::BeginSingleTimeCommands()
{
    vk::CommandBufferAllocateInfo allocInfo{};
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;
    
    auto allocResult = m_device.allocateCommandBuffers(allocInfo);
    if (allocResult.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("単発コマンドバッファの割り当てに失敗しました");
    }
    
    vk::CommandBuffer commandBuffer = allocResult.value[0];
    
    vk::CommandBufferBeginInfo beginInfo{};
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
    
    auto beginResult = commandBuffer.begin(beginInfo);
    if (beginResult != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドバッファの開始に失敗しました");
    }
    
    return commandBuffer;
}

// 単発コマンドバッファ終了
void VulkanDevice::EndSingleTimeCommands(vk::CommandBuffer commandBuffer)
{
    commandBuffer.end();
    
    vk::SubmitInfo submitInfo{};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    
    auto submitResult = m_graphicsQueue.submit(1, &submitInfo, nullptr);
    if (submitResult != vk::Result::eSuccess)
    {
        throw std::runtime_error("キューへの送信に失敗しました");
    }
    
    m_graphicsQueue.waitIdle();
    
    m_device.freeCommandBuffers(m_commandPool, 1, &commandBuffer);
}

// サポートするフォーマットを検索
vk::Format VulkanDevice::FindSupportedFormat(
    const VariableArray<vk::Format>& candidates,
    vk::ImageTiling tiling, 
    vk::FormatFeatureFlags features) const
{
    for (vk::Format format : candidates)
    {
        auto props = m_physicalDevice.getFormatProperties(format);
        
        if (tiling == vk::ImageTiling::eLinear && (props.linearTilingFeatures & features) == features)
        {
            return format;
        }
        else if (tiling == vk::ImageTiling::eOptimal && (props.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }
    
    throw std::runtime_error("サポートされているフォーマットが見つかりません");
}

// RHI Format → vk::Format変換
vk::Format VulkanDevice::ToVkFormat(Format format) const
{
    auto it = m_formatMap.find(format);
    if (it != m_formatMap.end())
    {
        return it->second;
    }
    return vk::Format::eUndefined;
}

// vk::Format → RHI Format変換
Format VulkanDevice::FromVkFormat(vk::Format format) const
{
    auto it = m_reverseFormatMap.find(format);
    if (it != m_reverseFormatMap.end())
    {
        return it->second;
    }
    return Format::UNKNOWN;
}

// デバッグコールバック
VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        std::cerr << "Vulkanバリデーション: " << pCallbackData->pMessage << std::endl;
    }
    
    return VK_FALSE;  // メッセージを処理したことを示す
}

#include "VulkanBuffer.h"
#include "VulkanCommandList.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanShader.h"
#include "VulkanDescriptorSet.h"
#include "VulkanSwapChain.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanPipeline.h"

// IDeviceインターフェース実装
BufferPtr VulkanDevice::CreateBuffer(const BufferDesc& desc)
{
    return MakeShared<VulkanBuffer>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

TexturePtr VulkanDevice::CreateTexture(const TextureDesc& desc)
{
    return MakeShared<VulkanTexture>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

SamplerPtr VulkanDevice::CreateSampler(const SamplerDesc& desc)
{
    return MakeShared<VulkanSampler>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

ShaderPtr VulkanDevice::CreateShader(const ShaderDesc& desc)
{
    return MakeShared<VulkanShader>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

CommandListPtr VulkanDevice::CreateCommandList()
{
    return MakeShared<VulkanCommandList>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}));
}

SwapChainPtr VulkanDevice::CreateSwapChain(const SwapChainDesc& desc)
{
    return MakeShared<VulkanSwapChain>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

RenderPassPtr VulkanDevice::CreateRenderPass(const RenderPassDesc& desc)
{
    return MakeShared<VulkanRenderPass>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

FramebufferPtr VulkanDevice::CreateFramebuffer(const FramebufferDesc& desc)
{
    return MakeShared<VulkanFramebuffer>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

PipelinePtr VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    return MakeShared<VulkanGraphicsPipeline>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

PipelinePtr VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    return MakeShared<VulkanComputePipeline>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
}

DescriptorSetPtr VulkanDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
{
    // ディスクリプタセットレイアウトの作成
    auto layout = MakeShared<VulkanDescriptorSetLayout>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), 
        desc.bindings);
    
    // ディスクリプタプールの作成
    auto pool = MakeShared<VulkanDescriptorPool>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}), 
        10);  // 10セット分のプールを作成
    
    // VulkanDescriptorSetの作成
    return MakeShared<VulkanDescriptorSet>(
        TSharedPtr<VulkanDevice>(this, [](VulkanDevice*){}),
        desc,
        layout,
        pool);
}

void VulkanDevice::WaitIdle()
{
    m_device.waitIdle();
}

} // namespace NorvesLib::RHI::Vulkan

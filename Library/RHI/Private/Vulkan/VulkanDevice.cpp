#include "VulkanDevice.h"
#include <iostream>
#include <algorithm>
#include "Core/Public/Container/Containers.h"
#include "VulkanSwapChain.h"

namespace NorvesLib::RHI::Vulkan
{

// バリデーションレイヤー名
const NorvesLib::Core::Container::VariableArray<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

// 必要なデバイス拡張機能
const NorvesLib::Core::Container::VariableArray<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// DebugUtilsMessenger作成用
VkResult CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks* pAllocator,
    VkDebugUtilsMessengerEXT* pDebugMessenger) 
{
    
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkCreateDebugUtilsMessengerEXT");
    
    if (func != nullptr) 
    {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}

// DebugUtilsMessenger破棄用
void DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks* pAllocator) 
{
    
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance, "vkDestroyDebugUtilsMessengerEXT");
    
    if (func != nullptr) 
    {
        func(instance, debugMessenger, pAllocator);
    }
}

// コンストラクタ
VulkanDevice::VulkanDevice(bool enableValidation)
    : m_validationEnabled(enableValidation)
{
    CreateInstance();
    
    if (m_validationEnabled) 
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
    if (m_commandPool != VK_NULL_HANDLE) 
    {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
    
    // デバイスを破棄
    if (m_device != VK_NULL_HANDLE) 
    {
        vkDestroyDevice(m_device, nullptr);
    }
    
    // デバッグメッセンジャーを破棄
    if (m_debugMessenger != VK_NULL_HANDLE) 
    {
        DestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    
    // インスタンスを破棄
    if (m_instance != VK_NULL_HANDLE) 
    {
        vkDestroyInstance(m_instance, nullptr);
    }
}

// Vulkanインスタンス作成
void VulkanDevice::CreateInstance()
{
    // バリデーションレイヤーのチェック
    if (m_validationEnabled && !CheckValidationLayerSupport()) 
    {
        throw std::runtime_error("バリデーションレイヤーが利用できません");
    }
    
    // アプリケーション情報
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "NorvesLib Application";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "NorvesLib Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    // インスタンス作成情報
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // 拡張機能
    auto extensions = GetRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    // バリデーションレイヤー
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (m_validationEnabled) 
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        
        // デバッグメッセンジャー情報
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = DebugCallback;
        createInfo.pNext = &debugCreateInfo;
    }
    else 
    {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    
    // インスタンス作成
    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) 
    {
        throw std::runtime_error("Vulkanインスタンスの作成に失敗しました");
    }
}

// デバッグメッセンジャーのセットアップ
void VulkanDevice::SetupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = 
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | 
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | 
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = DebugCallback;
    
    if (CreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) 
    {
        throw std::runtime_error("デバッグメッセンジャーの設定に失敗しました");
    }
}

// 物理デバイスの選択
void VulkanDevice::PickPhysicalDevice()
{
    // デバイス数の取得
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    
    if (deviceCount == 0) 
    {
        throw std::runtime_error("Vulkanをサポートするデバイスが見つかりません");
    }
    
    // デバイス一覧取得
    NorvesLib::Core::Container::VariableArray<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
    
    // 適切なデバイスを探す
    for (const auto& device : devices) 
    {
        if (IsDeviceSuitable(device)) 
        {
            m_physicalDevice = device;
            
            // デバイス情報を取得
            vkGetPhysicalDeviceProperties(m_physicalDevice, &m_deviceProperties);
            vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_deviceFeatures);
            vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
            
            // キューファミリーを取得
            FindQueueFamilies(m_physicalDevice);
            break;
        }
    }
    
    if (m_physicalDevice == VK_NULL_HANDLE) 
    {
        throw std::runtime_error("適切なGPUデバイスが見つかりません");
    }
}

// 論理デバイスの作成
void VulkanDevice::CreateLogicalDevice()
{
    // 重複のないキューファミリインデックスのセット
    NorvesLib::Core::Container::Set<uint32_t> uniqueQueueFamilies;
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
    NorvesLib::Core::Container::VariableArray<VkDeviceQueueCreateInfo> queueCreateInfos;
    
    for (uint32_t queueFamily : uniqueQueueFamilies) 
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }
    
    // デバイス機能の設定
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;
    deviceFeatures.fillModeNonSolid = VK_TRUE; // ワイヤーフレームなどのサポート
    
    // デバイス作成情報
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    
    // デバイス拡張機能
    auto extensions = GetDeviceExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    
    // バリデーションレイヤー
    if (m_validationEnabled) 
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else 
    {
        createInfo.enabledLayerCount = 0;
    }
    
    // デバイス作成
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) 
    {
        throw std::runtime_error("Vulkan論理デバイスの作成に失敗しました");
    }
    
    // キューハンドルを取得
    vkGetDeviceQueue(m_device, m_graphicsQueueFamilyIndex, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_presentQueueFamilyIndex, 0, &m_presentQueue);
    vkGetDeviceQueue(m_device, m_computeQueueFamilyIndex, 0, &m_computeQueue);
    
    if (m_transferQueueFamilyIndex != UINT32_MAX) 
    {
        vkGetDeviceQueue(m_device, m_transferQueueFamilyIndex, 0, &m_transferQueue);
    } else 
    {
        m_transferQueue = m_graphicsQueue;
    }
}

// コマンドプールの作成
void VulkanDevice::CreateCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) 
    {
        throw std::runtime_error("コマンドプールの作成に失敗しました");
    }
}

// フォーマット変換マップの初期化
void VulkanDevice::InitFormatMaps()
{
    // RHI Format → VkFormat
    m_formatMap[Format::R8_UNORM] = VK_FORMAT_R8_UNORM;
    m_formatMap[Format::R8G8_UNORM] = VK_FORMAT_R8G8_UNORM;
    m_formatMap[Format::R8G8B8A8_UNORM] = VK_FORMAT_R8G8B8A8_UNORM;
    m_formatMap[Format::R8G8B8A8_SRGB] = VK_FORMAT_R8G8B8A8_SRGB;
    m_formatMap[Format::B8G8R8A8_UNORM] = VK_FORMAT_B8G8R8A8_UNORM;
    m_formatMap[Format::B8G8R8A8_SRGB] = VK_FORMAT_B8G8R8A8_SRGB;
    m_formatMap[Format::R16_FLOAT] = VK_FORMAT_R16_SFLOAT;
    m_formatMap[Format::R16G16_FLOAT] = VK_FORMAT_R16G16_SFLOAT;
    m_formatMap[Format::R16G16B16A16_FLOAT] = VK_FORMAT_R16G16B16A16_SFLOAT;
    m_formatMap[Format::R32_FLOAT] = VK_FORMAT_R32_SFLOAT;
    m_formatMap[Format::R32G32_FLOAT] = VK_FORMAT_R32G32_SFLOAT;
    m_formatMap[Format::R32G32B32_FLOAT] = VK_FORMAT_R32G32B32_SFLOAT;
    m_formatMap[Format::R32G32B32A32_FLOAT] = VK_FORMAT_R32G32B32A32_SFLOAT;
    m_formatMap[Format::D16_UNORM] = VK_FORMAT_D16_UNORM;
    m_formatMap[Format::D24_UNORM_S8_UINT] = VK_FORMAT_D24_UNORM_S8_UINT;
    m_formatMap[Format::D32_FLOAT] = VK_FORMAT_D32_SFLOAT;
    
    // VkFormat → RHI Format (逆変換マップも作成)
    for (const auto& [rhiFormat, vkFormat] : m_formatMap) 
    {
        m_reverseFormatMap[vkFormat] = rhiFormat;
    }
}

// バリデーションレイヤーのサポート確認
bool VulkanDevice::CheckValidationLayerSupport()
{
    // レイヤー一覧の取得
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    
    NorvesLib::Core::Container::VariableArray<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    
    // 必要なレイヤーが全て存在するか確認
    for (const char* layerName : validationLayers) 
    {
        bool layerFound = false;
        
        for (const auto& layerProperties : availableLayers) 
        {
            if (strcmp(layerName, layerProperties.layerName) == 0) 
            {
                layerFound = true;
                break;
            }
        }
        
        if (!layerFound) 
        {
            return false;
        }
    }
    
    return true;
}

// デバイスが適切かどうかの判定
bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device)
{
    // 物理デバイスのプロパティと機能
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    
    // キューファミリーのサポートチェック
    FindQueueFamilies(device);
    bool hasRequiredQueueFamilies = 
        m_graphicsQueueFamilyIndex != UINT32_MAX && 
        m_computeQueueFamilyIndex != UINT32_MAX;
    
    // 拡張機能のサポートチェック
    bool extensionsSupported = true;
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    
    NorvesLib::Core::Container::VariableArray<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());
    
    NorvesLib::Core::Container::Set<NorvesLib::Core::Container::String> requiredExtensions;
    for (const auto& ext : deviceExtensions) 
    {
        requiredExtensions.insert(NorvesLib::Core::Container::String(ext));
    }
    
    for (const auto& extension : availableExtensions) 
    {
        requiredExtensions.erase(NorvesLib::Core::Container::String(extension.extensionName));
    }
    
    extensionsSupported = requiredExtensions.empty();
    
    // 物理デバイスの選定
    bool isDiscrete = deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    bool hasAnisotropySupport = deviceFeatures.samplerAnisotropy;
    
    return hasRequiredQueueFamilies && 
           extensionsSupported && 
           hasAnisotropySupport && 
           isDiscrete;  // 離散GPUを優先
}

// 必要なインスタンス拡張機能を取得
NorvesLib::Core::Container::VariableArray<const char*> VulkanDevice::GetRequiredExtensions()
{
    NorvesLib::Core::Container::VariableArray<const char*> extensions;
    
    // ウィンドウシステム連携のための拡張機能
    extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    
    // プラットフォーム固有の拡張機能
#ifdef _WIN32
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(__linux__)
    extensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
    // または extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
    
    // バリデーション関連の拡張機能
    if (m_validationEnabled) 
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    return extensions;
}

// 必要なデバイス拡張機能を取得
NorvesLib::Core::Container::VariableArray<const char*> VulkanDevice::GetDeviceExtensions()
{
    return deviceExtensions;
}

// キューファミリーのインデックス取得
void VulkanDevice::FindQueueFamilies(VkPhysicalDevice device)
{
    // キューファミリーのプロパティ取得
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    
    NorvesLib::Core::Container::VariableArray<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    
    // グラフィックスキューファミリーを探す
    for (uint32_t i = 0; i < queueFamilyCount; i++) 
    {
        const auto& queueFamily = queueFamilies[i];
        
        // グラフィックスキュー
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
        {
            m_graphicsQueueFamilyIndex = i;
            m_presentQueueFamilyIndex = i;  // 通常はグラフィックスキューでプレゼントも可能
        }
        
        // コンピュートキュー（可能ならグラフィックスとは別のキューを使用）
        if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) 
        {
            if (m_computeQueueFamilyIndex == UINT32_MAX ||
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) 
            {
                m_computeQueueFamilyIndex = i;
            }
        }
        
        // 転送専用キュー（可能ならグラフィックスとは別のキューを使用）
        if (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) 
        {
            if (m_transferQueueFamilyIndex == UINT32_MAX ||
                !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)) 
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
uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
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

// サポートするフォーマットを検索
VkFormat VulkanDevice::FindSupportedFormat(
    const NorvesLib::Core::Container::VariableArray<VkFormat>& candidates,
    VkImageTiling tiling, 
    VkFormatFeatureFlags features) const
{
    for (VkFormat format : candidates) 
    {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &props);
        
        if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
        {
            return format;
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) 
        {
            return format;
        }
    }
    
    throw std::runtime_error("サポートされているフォーマットが見つかりません");
}

// RHI Format → VkFormat変換
VkFormat VulkanDevice::ToVkFormat(Format format) const
{
    auto it = m_formatMap.find(format);
    if (it != m_formatMap.end()) 
    {
        return it->second;
    }
    return VK_FORMAT_UNDEFINED;
}

// VkFormat → RHI Format変換
Format VulkanDevice::FromVkFormat(VkFormat format) const
{
    auto it = m_reverseFormatMap.find(format);
    if (it != m_reverseFormatMap.end()) 
    {
        return it->second;
    }
    return Format::UNKNOWN;
}

// デバッグコールバック
VkBool32 VulkanDevice::DebugCallback(
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

// IDeviceインターフェース実装
BufferPtr VulkanDevice::CreateBuffer(const BufferDesc& desc)
{
    // VulkanBufferの作成
    auto buffer = std::make_shared<VulkanBuffer>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return buffer;
}

TexturePtr VulkanDevice::CreateTexture(const TextureDesc& desc)
{
    // VulkanTextureの作成
    auto texture = std::make_shared<VulkanTexture>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return texture;
}

SamplerPtr VulkanDevice::CreateSampler(const SamplerDesc& desc)
{
    // VulkanSamplerの作成
    auto sampler = std::make_shared<VulkanSampler>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return sampler;
}

ShaderPtr VulkanDevice::CreateShader(const ShaderDesc& desc)
{
    // VulkanShaderの作成
    auto shader = std::make_shared<VulkanShader>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return shader;
}

CommandListPtr VulkanDevice::CreateCommandList()
{
    auto commandList = std::make_shared<VulkanCommandList>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}));
    return commandList;
}

SwapChainPtr VulkanDevice::CreateSwapChain(const SwapChainDesc& desc)
{
    // VulkanSwapChainの作成
    auto swapChain = std::make_shared<VulkanSwapChain>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return swapChain;
}

RenderPassPtr VulkanDevice::CreateRenderPass(const RenderPassDesc& desc)
{
    // VulkanRenderPassの作成
    auto renderPass = std::make_shared<VulkanRenderPass>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return renderPass;
}

FramebufferPtr VulkanDevice::CreateFramebuffer(const FramebufferDesc& desc)
{
    // VulkanFramebufferの作成
    auto framebuffer = std::make_shared<VulkanFramebuffer>(std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return framebuffer;
}

PipelinePtr VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc)
{
    // VulkanGraphicsPipelineの作成
    auto pipeline = std::make_shared<VulkanGraphicsPipeline>(
        std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return pipeline;
}

PipelinePtr VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc)
{
    // VulkanComputePipelineの作成
    auto pipeline = std::make_shared<VulkanComputePipeline>(
        std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), desc);
    return pipeline;
}

DescriptorSetPtr VulkanDevice::CreateDescriptorSet(const DescriptorSetDesc& desc)
{
    // ディスクリプタセットレイアウトの作成
    auto layout = std::make_shared<VulkanDescriptorSetLayout>(
        std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), 
        desc.bindings);
    
    // ディスクリプタプールの作成（または既存のプールを取得）
    // 簡易実装として、毎回新しいプールを作成する
    auto pool = std::make_shared<VulkanDescriptorPool>(
        std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}), 
        10);  // 10セット分のプールを作成
    
    // VulkanDescriptorSetの作成
    auto descriptorSet = std::make_shared<VulkanDescriptorSet>(
        std::shared_ptr<VulkanDevice>(this, [](VulkanDevice*){}),
        desc,
        layout,
        pool);
    
    return descriptorSet;
}

void VulkanDevice::WaitIdle()
{
    vkDeviceWaitIdle(m_device);
}

} // namespace NorvesLib::RHI::Vulkan
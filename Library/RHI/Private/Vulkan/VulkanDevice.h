#pragma once

#include "RHI/Public/IDevice.h"
#include <vulkan/vulkan.h>
#include <memory>
#include "Core/Public/Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{

class VulkanBuffer;
class VulkanCommandList;
class VulkanTexture;
class VulkanSampler;
class VulkanRenderPass;
class VulkanFramebuffer;
class VulkanShader;
class VulkanPipeline;
class VulkanSwapChain;
class VulkanDescriptorSet;

/**
 * @brief Vulkanデバイスの実装クラス
 */
class VulkanDevice : public IDevice
{
public:
    /**
     * @brief VulkanDeviceのコンストラクタ
     * @param enableValidation バリデーションレイヤー有効化フラグ
     */
    explicit VulkanDevice(bool enableValidation = true);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanDevice() override;

    // IDeviceインターフェース実装
    BufferPtr CreateBuffer(const BufferDesc& desc) override;
    TexturePtr CreateTexture(const TextureDesc& desc) override;
    SamplerPtr CreateSampler(const SamplerDesc& desc) override;
    ShaderPtr CreateShader(const ShaderDesc& desc) override;
    CommandListPtr CreateCommandList() override;
    SwapChainPtr CreateSwapChain(const SwapChainDesc& desc) override;
    RenderPassPtr CreateRenderPass(const RenderPassDesc& desc) override;
    FramebufferPtr CreateFramebuffer(const FramebufferDesc& desc) override;
    PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelinePtr CreateComputePipeline(const ComputePipelineDesc& desc) override;
    void WaitIdle() override;
    API GetAPI() const override { return API::Vulkan; }

    // Vulkan固有のメソッド
    VkDevice GetVkDevice() const { return m_device; }
    VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
    VkInstance GetVkInstance() const { return m_instance; }
    
    // キュー関連
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue GetPresentQueue() const { return m_presentQueue; }
    VkQueue GetComputeQueue() const { return m_computeQueue; }
    VkQueue GetTransferQueue() const { return m_transferQueue; }
    
    uint32_t GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
    uint32_t GetComputeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }
    uint32_t GetTransferQueueFamilyIndex() const { return m_transferQueueFamilyIndex; }
    
    // メモリ管理
    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;
    
    // コマンドプール
    VkCommandPool GetCommandPool() const { return m_commandPool; }

    // 実装固有の機能
    VkFormat FindSupportedFormat(const NorvesLib::Core::Container::VariableArray<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;
    
    // フォーマット変換
    VkFormat ToVkFormat(Format format) const;
    Format FromVkFormat(VkFormat format) const;

private:
    // Vulkanインスタンスとデバイス
    VkInstance m_instance = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_deviceProperties{};
    VkPhysicalDeviceFeatures m_deviceFeatures{};
    VkPhysicalDeviceMemoryProperties m_memoryProperties{};
    
    // キューファミリー
    uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
    uint32_t m_computeQueueFamilyIndex = UINT32_MAX;
    uint32_t m_transferQueueFamilyIndex = UINT32_MAX;
    uint32_t m_presentQueueFamilyIndex = UINT32_MAX;
    
    // キュー
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
    
    // コマンドプール
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    
    // デバッグ・バリデーション用
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    bool m_validationEnabled = false;

    // フォーマット変換テーブル
    NorvesLib::Core::Container::HashMap<Format, VkFormat> m_formatMap;
    NorvesLib::Core::Container::HashMap<VkFormat, Format> m_reverseFormatMap;
    
    // 初期化メソッド
    void CreateInstance();
    void SetupDebugMessenger();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateCommandPool();
    void InitFormatMaps();
    
    // ヘルパー
    bool IsDeviceSuitable(VkPhysicalDevice device);
    NorvesLib::Core::Container::VariableArray<const char*> GetRequiredExtensions();
    NorvesLib::Core::Container::VariableArray<const char*> GetDeviceExtensions();
    void FindQueueFamilies(VkPhysicalDevice device);

    // バリデーション関連
    bool CheckValidationLayerSupport();
    static VkBool32 DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

} // namespace NorvesLib::RHI::Vulkan
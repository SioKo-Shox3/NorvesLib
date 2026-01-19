#pragma once

#include "RHI/IDevice.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

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
    class VulkanGraphicsPipeline;
    class VulkanComputePipeline;
    class VulkanSwapChain;
    class VulkanDescriptorSet;
    class VulkanDescriptorSetLayout;
    class VulkanDescriptorPool;

    /**
     * @brief Vulkan初期化パラメータ
     */
    struct VulkanInitParams
    {
        bool bEnableValidation = true;
        bool bPreferIntegratedGPU = false;
    };

    /**
     * @brief Vulkanデバイスの実装クラス (vulkan.hpp使用)
     */
    class VulkanDevice : public IDevice
    {
    public:
        /**
         * @brief VulkanDeviceのファクトリメソッド
         * @param params 初期化パラメータ
         * @return 作成されたデバイス
         */
        static DevicePtr Create(const VulkanInitParams &params = {});

        /**
         * @brief VulkanDeviceのコンストラクタ
         * @param bEnableValidation バリデーションレイヤー有効化フラグ
         */
        explicit VulkanDevice(bool bEnableValidation = true);

        /**
         * @brief デストラクタ
         */
        ~VulkanDevice() override;

        // IDeviceインターフェース実装
        BufferPtr CreateBuffer(const BufferDesc &desc) override;
        TexturePtr CreateTexture(const TextureDesc &desc) override;
        SamplerPtr CreateSampler(const SamplerDesc &desc) override;
        ShaderPtr CreateShader(const ShaderDesc &desc) override;
        CommandListPtr CreateCommandList() override;
        SwapChainPtr CreateSwapChain(const SwapChainDesc &desc) override;
        RenderPassPtr CreateRenderPass(const RenderPassDesc &desc) override;
        FramebufferPtr CreateFramebuffer(const FramebufferDesc &desc) override;
        PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
        PipelinePtr CreateComputePipeline(const ComputePipelineDesc &desc) override;
        DescriptorSetPtr CreateDescriptorSet(const DescriptorSetDesc &desc) override;
        void WaitIdle() override;
        API GetAPI() const override { return API::Vulkan; }

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Device GetVkDevice() const { return m_device; }
        vk::PhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
        vk::Instance GetVkInstance() const { return m_instance; }

        // キュー関連
        vk::Queue GetGraphicsQueue() const { return m_graphicsQueue; }
        vk::Queue GetPresentQueue() const { return m_presentQueue; }
        vk::Queue GetComputeQueue() const { return m_computeQueue; }
        vk::Queue GetTransferQueue() const { return m_transferQueue; }

        uint32_t GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
        uint32_t GetComputeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }
        uint32_t GetTransferQueueFamilyIndex() const { return m_transferQueueFamilyIndex; }

        // メモリ管理
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const;

        // コマンドプール
        vk::CommandPool GetCommandPool() const { return m_commandPool; }

        // 単発コマンドバッファ用ユーティリティ
        vk::CommandBuffer BeginSingleTimeCommands();
        void EndSingleTimeCommands(vk::CommandBuffer commandBuffer);

        // 実装固有の機能
        vk::Format FindSupportedFormat(
            const NorvesLib::Core::Container::VariableArray<vk::Format> &candidates,
            vk::ImageTiling tiling,
            vk::FormatFeatureFlags features) const;

        // フォーマット変換
        vk::Format ToVkFormat(Format format) const;
        Format FromVkFormat(vk::Format format) const;

    private:
        // Vulkanインスタンスとデバイス
        vk::Instance m_instance;
        vk::PhysicalDevice m_physicalDevice;
        vk::Device m_device;
        vk::PhysicalDeviceProperties m_deviceProperties{};
        vk::PhysicalDeviceFeatures m_deviceFeatures{};
        vk::PhysicalDeviceMemoryProperties m_memoryProperties{};

        // キューファミリー
        uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
        uint32_t m_computeQueueFamilyIndex = UINT32_MAX;
        uint32_t m_transferQueueFamilyIndex = UINT32_MAX;
        uint32_t m_presentQueueFamilyIndex = UINT32_MAX;

        // キュー
        vk::Queue m_graphicsQueue;
        vk::Queue m_computeQueue;
        vk::Queue m_transferQueue;
        vk::Queue m_presentQueue;

        // コマンドプール
        vk::CommandPool m_commandPool;

        // デバッグ・バリデーション用
        vk::DebugUtilsMessengerEXT m_debugMessenger;
        bool m_bValidationEnabled = false;

        // フォーマット変換テーブル
        NorvesLib::Core::Container::UnorderedMap<Format, vk::Format> m_formatMap;
        NorvesLib::Core::Container::UnorderedMap<vk::Format, Format> m_reverseFormatMap;

        // 初期化メソッド
        void CreateInstance();
        void SetupDebugMessenger();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandPool();
        void InitFormatMaps();

        // ヘルパー
        bool IsDeviceSuitable(vk::PhysicalDevice device);
        NorvesLib::Core::Container::VariableArray<const char *> GetRequiredExtensions();
        NorvesLib::Core::Container::VariableArray<const char *> GetDeviceExtensions();
        void FindQueueFamilies(vk::PhysicalDevice device);

        // バリデーション関連
        bool CheckValidationLayerSupport();
        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            void *pUserData);
    };

} // namespace NorvesLib::RHI::Vulkan

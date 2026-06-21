#pragma once

#include "RHI/IDevice.h"

// Windowsプラットフォーム用Vulkan拡張
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#endif

// Vulkan Dynamic Dispatcherの設定
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"

namespace NorvesLib::RHI::Vulkan
{
    // 明示的なusing宣言（グローバル名前空間から参照）
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TUniquePtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::UnorderedMap;
    using ::NorvesLib::Core::Container::VariableArray;

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
    class VulkanGPUResourceAllocator;
#if defined(NORVES_ENABLE_IMGUI)
    class VulkanImGuiRenderer;
#endif

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
        static DescriptorType ConvertResourceBindType(ResourceBindType type);

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
        ShaderCompilerPtr CreateShaderCompiler() override;
        ShaderCompilerPtr CreateSlangShaderCompiler() override;
        IGPUResourceAllocator* GetResourceAllocator() override;
#if defined(NORVES_ENABLE_IMGUI)
        // ImGui バックエンドの遅延生成ファクトリ（ゲート ON 時のみ override）。
        // 初回呼び出しで VulkanImGuiRenderer を生成し以後同一インスタンスの
        // 借用ポインタを返す。所有は m_imguiRenderer（device 寿命に内包）。
        IImGuiRenderer *CreateImGuiRenderer() override;
#endif
        void WaitIdle() override;
        API GetAPI() const override { return API::Vulkan; }
        const DeviceCapabilities &GetCapabilities() const override { return m_Capabilities; }
        Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4 &projection, bool bApplyYFlip = true) const override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Device GetVkDevice() const { return m_device; }
        vk::PhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
        vk::Instance GetVkInstance() const { return m_instance; }

        // インスタンス生成時に VkApplicationInfo::apiVersion へ渡した値。
        // imgui バックエンド等、インスタンス apiVersion と一致させる必要がある
        // 利用側へ供給する（ハードコードを避けるため）。
        uint32_t GetInstanceApiVersion() const { return m_instanceApiVersion; }

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
            const VariableArray<vk::Format> &candidates,
            vk::ImageTiling tiling,
            vk::FormatFeatureFlags features) const;

        // フォーマット変換
        vk::Format ToVkFormat(Format format) const;
        Format FromVkFormat(vk::Format format) const;

    private:
        // Vulkanインスタンスとデバイス
        vk::Instance m_instance;
        uint32_t m_instanceApiVersion = 0; ///< CreateInstance が設定した VkApplicationInfo::apiVersion
        vk::PhysicalDevice m_physicalDevice;
        vk::Device m_device;
        vk::PhysicalDeviceProperties m_deviceProperties{};
        vk::PhysicalDeviceFeatures m_deviceFeatures{};
        vk::PhysicalDeviceFeatures m_enabledDeviceFeatures{};
        vk::PhysicalDeviceMemoryProperties m_memoryProperties{};

        // デバイス能力情報
        DeviceCapabilities m_Capabilities{};

        // Cooperative Vector 機能構造体（Features2チェーン用）
        vk::PhysicalDeviceCooperativeVectorFeaturesNV m_cooperativeVectorFeatures{};

        // Vulkan 1.2 機能構造体（Features2チェーン用）
        vk::PhysicalDeviceVulkan12Features m_vulkan12Features{};

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

        // GPUリソースアロケーター
        TUniquePtr<VulkanGPUResourceAllocator> m_ResourceAllocator;

#if defined(NORVES_ENABLE_IMGUI)
        // ImGui バックエンド（ゲート ON 時のみ）。CreateImGuiRenderer の初回呼び出しで
        // 生成し device に内包する。VkDevice 破棄前に Shutdown→reset する。
        TUniquePtr<VulkanImGuiRenderer> m_imguiRenderer;
#endif

        // デバッグ・バリデーション用
        vk::DebugUtilsMessengerEXT m_debugMessenger;
        bool m_bValidationEnabled = false;

        // フォーマット変換テーブル
        UnorderedMap<Format, vk::Format> m_formatMap;
        UnorderedMap<vk::Format, Format> m_reverseFormatMap;

        // 初期化メソッド
        void CreateInstance();
        void SetupDebugMessenger();
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateCommandPool();
        void InitFormatMaps();
        void DetectCapabilities();

        // ヘルパー
        bool IsDeviceSuitable(vk::PhysicalDevice device);
        VariableArray<const char *> GetRequiredExtensions();
        VariableArray<const char *> GetDeviceExtensions();
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

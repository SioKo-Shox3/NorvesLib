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
#include "FileStream/FileStream.h"
#include "Thread/Atomic.h"
#include "Thread/Mutex.h"

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
    class VulkanAccelerationStructure;
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
         * @brief 配列bindingを含むset群がデバイスのdescriptor上限に収まるか検査する
         *
         * 全bindingがcount=1のset群は従来どおり検査せずtrueを返す。配列を含む場合は、
         * stageごとの合計（sampler・sampled image・maxPerStageResources対象資源）とset群全体の合計を
         * 物理デバイスのlimitと比べる。maxPerStageResourcesは単独samplerと加速構造を数えず、
         * fragment段ではcolor attachment数を加える。
         * @param sets pipeline layoutまたは単一descriptor setを構成するset群
         * @param fragmentColorAttachmentCount graphics pipelineのcolor attachment数（他は0）
         */
        bool IsWithinDescriptorArrayLimits(const VariableArray<DescriptorSetDesc> &sets,
                                           uint32_t fragmentColorAttachmentCount = 0u) const;

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
        AccelerationStructurePtr CreateAccelerationStructure(const AccelerationStructureDesc &desc) override;
        TexturePtr CreateTexture(const TextureDesc &desc) override;
        SamplerPtr CreateSampler(const SamplerDesc &desc) override;
        ShaderPtr CreateShader(const ShaderDesc &desc) override;
        CommandListPtr CreateCommandList() override;
        SwapChainPtr CreateSwapChain(const SwapChainDesc &desc) override;
        RenderPassPtr CreateRenderPass(const RenderPassDesc &desc) override;
        FramebufferPtr CreateFramebuffer(const FramebufferDesc &desc) override;
        PipelinePtr CreateGraphicsPipeline(const GraphicsPipelineDesc &desc) override;
        PipelinePtr CreateComputePipeline(const ComputePipelineDesc &desc) override;
        PipelinePtr CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) override;
        DescriptorSetPtr CreateDescriptorSet(const DescriptorSetDesc &desc) override;
        ShaderCompilerPtr CreateShaderCompiler() override;
        ShaderCompilerPtr CreateSlangShaderCompiler() override;
        IGPUResourceAllocator* GetResourceAllocator() override;
        SparseMemoryBlockPtr CreateSparseMemoryBlock(uint64_t sizeBytes, const char *debugName = nullptr) override;
        // キューへの外部同期は呼出側で行う（コマンド送信・プレゼントと同じ直列化の下で呼ぶ）。
        bool BindSparse(const SparseBindRequest &request) override;
        // 同一デバイスのコマンド送信・コマンドプール操作と呼出側で直列化する。
        void WaitIdle() override;
        API GetAPI() const override { return API::Vulkan; }
        const DeviceCapabilities &GetCapabilities() const override { return m_Capabilities; }
        // VK_EXT_memory_budget が有効なときだけ DeviceLocal ヒープの予算と使用量を返す。
        VideoMemoryBudget GetVideoMemoryBudget() const override;
        Math::Matrix4x4 AdjustProjectionForClipSpace(
            const Math::Matrix4x4 &projection, bool bApplyYFlip = true) const override;

        // Vulkan固有のメソッド (vulkan.hpp型)
        vk::Device GetVkDevice() const { return m_device; }
        vk::PhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
        vk::Instance GetVkInstance() const { return m_instance; }

        // インスタンス生成時に VkApplicationInfo::apiVersion へ渡した値。
        // 各サブシステムで、インスタンス apiVersion と一致させる必要がある
        // 利用側へ供給する（ハードコードを避けるため）。
        uint32_t GetInstanceApiVersion() const { return m_instanceApiVersion; }

        // キュー関連
        vk::Queue GetGraphicsQueue() const { return m_graphicsQueue; }
        vk::Queue GetPresentQueue() const { return m_presentQueue; }
        vk::Queue GetComputeQueue() const { return m_computeQueue; }
        vk::Queue GetTransferQueue() const { return m_transferQueue; }
        // sparse の結び付け（vkQueueBindSparse）に使うキュー。sparseBinding が無効なら空のハンドル。
        vk::Queue GetSparseBindingQueue() const { return m_sparseBindingQueue; }

        // グラフィックスのキューへ描画を提出する間の窓口（sparse の結び付けとの順序付け）。
        //   ・結び付けは、次にグラフィックスのキューへ送る提出より前に終わらせる。提出側は送信の前に
        //     GetSparseBindWait を呼び、結び付けを出したことがあれば、そのタイムラインセマフォの最新の値を
        //     待ちに加える（待ち段は全コマンド。タイムラインの待ちは消費されないので、送信に失敗しても戻す必要はない）。
        //   ・タイルを外す結び付けは、それより前に出した描画の完了を待つ（外すタイルを読む描画が実行中でも
        //     未定義の読み出しにならない）。提出側は AcquireRenderSignal で描画用のタイムラインセマフォの値を
        //     1つ割り当て、その値を提出で通知する。
        // 値の割り当てと提出を結び付けの提出と同じミューテックスの下で行うので、結び付けが読む「最新の値」は
        // 必ずその値までの提出が済んだ状態になる。提出が失敗したときは Commit せずに破棄すると値が戻る。
        // 窓口を持っている間は BindSparse を呼ばないこと（同じミューテックスで待つ）。
        class GraphicsSubmitScope
        {
        public:
            explicit GraphicsSubmitScope(VulkanDevice &device);
            ~GraphicsSubmitScope();
            GraphicsSubmitScope(const GraphicsSubmitScope &) = delete;
            GraphicsSubmitScope &operator=(const GraphicsSubmitScope &) = delete;

            // 出してある結び付けの最新の値（無ければ false）
            bool GetSparseBindWait(vk::Semaphore &outSemaphore, uint64_t &outValue) const;
            // この提出が通知する描画用の値を割り当てる（描画用のセマフォが無ければ false）。
            // 割り当て済みなら同じ値を返す。
            bool AcquireRenderSignal(vk::Semaphore &outSemaphore, uint64_t &outValue);
            // 提出が成功した。割り当てた値を確定してミューテックスを手放す。
            void Commit();

        private:
            VulkanDevice &m_owner;
            uint64_t m_previousRenderValue = 0;
            bool m_bLocked = false;
            bool m_bAcquired = false;
        };

        uint32_t GetGraphicsQueueFamilyIndex() const { return m_graphicsQueueFamilyIndex; }
        // VK_QUEUE_SPARSE_BINDING_BIT を持つ族（無ければ UINT32_MAX）。グラフィックスの族が持てばそれを使う。
        uint32_t GetSparseBindingQueueFamilyIndex() const { return m_sparseQueueFamilyIndex; }
        uint32_t GetComputeQueueFamilyIndex() const { return m_computeQueueFamilyIndex; }
        uint32_t GetTransferQueueFamilyIndex() const { return m_transferQueueFamilyIndex; }

        // メモリ管理
        // excluded に含まれる属性を持つメモリタイプは選ばない
        uint32_t FindMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties,
                                vk::MemoryPropertyFlags excluded = {}) const;
        vk::MemoryPropertyFlags GetMemoryTypeFlags(uint32_t memoryTypeIndex) const
        {
            return m_memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
        }

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

        // VK_EXT_memory_budget を論理デバイスで有効にしたか（任意拡張）
        bool m_bMemoryBudgetExtensionEnabled = false;

        // Cooperative Vector 機能構造体（Features2チェーン用）
        vk::PhysicalDeviceCooperativeVectorFeaturesNV m_cooperativeVectorFeatures{};

        // Device Fault 機能構造体（Features2チェーン用）
        VkPhysicalDeviceFaultFeaturesEXT m_deviceFaultFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
            nullptr,
            VK_FALSE,
            VK_FALSE};
        Thread::Atomic<bool> m_bDeviceFaultReported{false};

#if defined(VK_EXT_device_address_binding_report)
        // Address Binding Report 機能構造体（Features2チェーン用）
        VkPhysicalDeviceAddressBindingReportFeaturesEXT m_addressBindingReportFeatures{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ADDRESS_BINDING_REPORT_FEATURES_EXT,
            nullptr,
            VK_FALSE};
        FileStream::FileStreamPtr m_addressBindingDiagnosticsSink;
        Thread::Mutex m_addressBindingDiagnosticsMutex;
        uint64_t m_addressBindingDiagnosticsSequence = 0;
#endif

        // Vulkan 1.2 機能構造体（Features2チェーン用）
        vk::PhysicalDeviceVulkan12Features m_vulkan12Features{};

        // RT機能構造体（照会結果と論理デバイス有効化に使用）
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR m_accelerationStructureFeatures{};
        vk::PhysicalDeviceRayQueryFeaturesKHR m_rayQueryFeatures{};
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR m_rayTracingPipelineFeatures{};

        // キューファミリー
        uint32_t m_graphicsQueueFamilyIndex = UINT32_MAX;
        uint32_t m_computeQueueFamilyIndex = UINT32_MAX;
        uint32_t m_transferQueueFamilyIndex = UINT32_MAX;
        uint32_t m_presentQueueFamilyIndex = UINT32_MAX;
        uint32_t m_sparseQueueFamilyIndex = UINT32_MAX;

        // キュー
        vk::Queue m_sparseBindingQueue;

        // sparse の結び付けの順序付け。結び付けごとに値を1つ進めて通知するタイムラインセマフォ。
        // 次の結び付けは前の値を待ち（結び付け同士の順序）、グラフィックスの提出は最新の値を待つ。
        // m_sparseBindSubmittedValue は、これまでに出した結び付けの最新の値（0 は未提出）。
        vk::Semaphore m_sparseBindTimeline;
        uint64_t m_sparseBindSubmittedValue = 0;
        // 描画の完了を通知するタイムラインセマフォ。描画の提出ごとに値を1つ進めて通知し、
        // タイルを外す結び付けが最新の値を待つ。m_renderSubmittedValue は、これまでに提出した描画の最新の値。
        // 両方の値の読み書きと、結び付け・描画の提出は m_sparseBindMutex の下で行う。
        vk::Semaphore m_renderTimeline;
        uint64_t m_renderSubmittedValue = 0;
        Thread::Mutex m_sparseBindMutex;
        // 論理デバイスで sparse イメージを結べるメモリタイプの集合（最初の塊の作成時に、見本のイメージで求める）
        uint32_t m_sparseMemoryTypeBits = 0;
        vk::Queue m_graphicsQueue;
        vk::Queue m_computeQueue;
        vk::Queue m_transferQueue;
        vk::Queue m_presentQueue;

        // コマンドプール
        vk::CommandPool m_commandPool;

        // GPUリソースアロケーター
        TUniquePtr<VulkanGPUResourceAllocator> m_ResourceAllocator;

        // デバッグ・バリデーション用
        vk::DebugUtilsMessengerEXT m_debugMessenger;
#if defined(VK_EXT_device_address_binding_report)
        vk::DebugUtilsMessengerEXT m_addressBindingDebugMessenger;
#endif
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
        void DetectSparseFormatProperties();

        // sparse テクスチャを作れるか確かめる（作れない理由はログに出す）
        bool ValidateSparseTextureDesc(const TextureDesc &desc) const;

        // sparse の塊を切り出す DeviceLocal のメモリタイプを選ぶ（失敗の理由はログに出す）
        bool FindSparseMemoryType(uint32_t &outMemoryTypeIndex);

        // ヘルパー
        bool IsDeviceSuitable(vk::PhysicalDevice device);
        VariableArray<const char *> GetRequiredExtensions();
        VariableArray<const char *> GetDeviceExtensions();
        void FindQueueFamilies(vk::PhysicalDevice device);
        void ReportDeviceFaultOnce();
        VkResult WaitIdleInternal() noexcept;

#if defined(VK_EXT_device_address_binding_report)
        void SetupAddressBindingDebugMessenger();
        void InitializeAddressBindingDiagnostics();
        void ShutdownAddressBindingDiagnostics();
        void RecordAddressBindingDiagnostic(
            const VkDebugUtilsMessengerCallbackDataEXT &callbackData,
            const VkDeviceAddressBindingCallbackDataEXT &addressBindingData);
        static const VkDeviceAddressBindingCallbackDataEXT *FindAddressBindingCallbackData(
            const VkDebugUtilsMessengerCallbackDataEXT &callbackData);
#endif
        void SetDebugObjectName(vk::ObjectType objectType, uint64_t objectHandle, const char *pName);

        // バリデーション関連
        bool CheckValidationLayerSupport();
        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
            VkDebugUtilsMessageTypeFlagsEXT messageType,
            const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
            void *pUserData);

        friend class VulkanBuffer;
        friend class VulkanTexture;
    };

} // namespace NorvesLib::RHI::Vulkan

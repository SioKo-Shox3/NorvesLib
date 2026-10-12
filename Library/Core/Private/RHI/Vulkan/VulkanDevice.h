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
#include "Thread/Thread.h"

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
    class VulkanSharedDescriptorSetLayout;
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
        DescriptorSetLayoutPtr GetOrCreateDescriptorSetLayout(const DescriptorSetDesc &desc) override;
        FrameDescriptorAllocatorPtr CreateFrameDescriptorAllocator(const FrameDescriptorPoolCapacity &capacity) override;
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

        // グラフィックスのキュー（プレゼントも同じキュー）は、提出・プレゼント・待機のどれも外部同期が要る。
        // 提出は GraphicsSubmitScope が同じミューテックスで守る。それ以外にキューを触る操作
        // （プレゼント・キューやデバイスの待機）は、このミューテックスを取ってから行う。
        Thread::Mutex &GetGraphicsQueueMutex() { return m_sparseBindMutex; }

        // 提出の完了までの破棄の遅延。
        // Framebuffer・RenderPass・Pipeline・記述子・Sampler・Buffer・Texture・加速構造のデストラクタは
        // vkDestroy* と vkFreeMemory の代わりにハンドルをここへ移す（どのスレッドからでもよい）。
        // 同じ資源のハンドルは使う側から先に積む（view → image → memory など）。列は積んだ順に破棄する。
        // 積んだ時点の「次に提出する通し番号」を添え、
        // スワップチェーンが渡す完了の通し番号がそれ以上になったら破棄する（記録中のフレームが参照して
        // いても、そのフレームの完了まで生かす。中止したフレームの通し番号は次の提出が使う）。
        // WaitIdle は待ちと同じキューロックの下で提出済みの分（次に提出する通し番号より小さい分）を退避して
        // 破棄する（待ちの後に出た提出や、記録中のフレームの分は通常の完了通知まで残す）。
        // スワップチェーンの破棄（WaitIdle の後）と device の破棄の前は列を全部空ける。提出を追うスワップチェーンが無い間は
        // その場で破棄する（スワップチェーンを通さない提出は自分のフェンスで待つ経路だけ）。
        void DeferDestroy(vk::Framebuffer handle) noexcept;
        void DeferDestroy(vk::RenderPass handle) noexcept;
        void DeferDestroy(vk::Pipeline handle) noexcept;
        void DeferDestroy(vk::PipelineLayout handle) noexcept;
        void DeferDestroy(vk::DescriptorSetLayout handle) noexcept;
        // プールを破棄すると、そこから確保した記述子セットも一緒に解放される
        void DeferDestroy(vk::DescriptorPool handle) noexcept;
        void DeferDestroy(vk::Sampler handle) noexcept;
        void DeferDestroy(vk::Buffer handle) noexcept;
        void DeferDestroy(vk::Image handle) noexcept;
        void DeferDestroy(vk::ImageView handle) noexcept;
        void DeferDestroy(vk::AccelerationStructureKHR handle) noexcept;
        // メモリは unmap してから積む。sizeBytes は列に残るメモリの量の統計に使う
        void DeferFree(vk::DeviceMemory handle, uint64_t sizeBytes) noexcept;

        // スワップチェーンが提出の通し番号を追い始める・やめる。nextSubmissionSerial は次の提出が使う通し番号。
        // やめるのは WaitIdle の後に呼ぶ（列を全部空ける）。
        void BeginDeferredDestroyTracking(uint64_t nextSubmissionSerial);
        void EndDeferredDestroyTracking();
        // 提出した通し番号（以後に積むものは submittedSerial + 1 の完了を待つ）。記録中のフレームもここで終わる
        void NotifyDeferredDestroySubmitted(uint64_t submittedSerial);
        // フレームの記録が始まった（BeginFrame の成功）・提出せずに終わった（EndFrame の失敗）。
        // 記録中は、WaitIdle の後も次に提出する通し番号で積んだ分を残す（その記録が提出で参照しうる）
        void NotifyDeferredDestroyFrameRecordingBegan();
        void NotifyDeferredDestroyFrameRecordingAborted();
        // GPU の完了を確かめた通し番号（それ以下で積んだものを破棄する）
        void NotifyDeferredDestroyCompleted(uint64_t completedSerial);

        struct DeferredDestroyStats
        {
            uint64_t Enqueued = 0;  ///< 列へ積んだ数（累計）
            uint64_t Destroyed = 0; ///< 列から破棄した数（累計）
            uint64_t Pending = 0;   ///< 列の残り
            uint64_t Immediate = 0; ///< 追うスワップチェーンが無く、その場で破棄した数（累計）
            uint64_t PendingMemoryBytes = 0;    ///< 列に残るメモリの量（バイト）
            uint64_t MaxPendingMemoryBytes = 0; ///< 列に残ったメモリの量の最大（バイト）
        };
        DeferredDestroyStats GetDeferredDestroyStats() const;

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
        // プールは外部同期が要る（確保・解放・記録の全部が同じプールへの書き込みになる）ので、
        // スレッドをまたいで共有しない。長寿命のコマンドリストは専用のプールを1つずつ持ち、
        // 単発コマンドは呼んだスレッドごとのプールを使う。
        vk::CommandPool CreateExclusiveCommandPool();
        vk::CommandPool GetSingleTimeCommandPool();

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

        // 単発コマンド用のコマンドプール（スレッドごと。登録の操作だけをミューテックスで守る）
        struct SingleTimePoolEntry
        {
            Thread::Thread::ThreadId Owner;
            vk::CommandPool Pool;
        };
        VariableArray<SingleTimePoolEntry> m_singleTimePools;
        Thread::Mutex m_singleTimePoolMutex;

        // 共有のディスクリプタセットレイアウト（束縛の形の並びのハッシュ → 同じハッシュの形の列）。
        // m_sharedDescriptorLayoutMutex の下で読み書きし、デバイスの破棄の前に手放す。
        // このキャッシュは遅らせる破棄の列より先に宣言しているので、メンバの破棄で中身の layout が列へ触らないよう、
        // デストラクタはどの道でも先に空にする（待機を確かめられない終了ではハンドルを破棄せずに切り離す）
        Thread::Mutex m_sharedDescriptorLayoutMutex;
        UnorderedMap<uint64_t, VariableArray<TSharedPtr<VulkanSharedDescriptorSetLayout>>> m_sharedDescriptorLayouts;
        // bDestroyHandles が true ならハンドルを破棄の列へ渡し、false なら破棄せずに切り離す
        void ReleaseSharedDescriptorLayouts(bool bDestroyHandles) noexcept;

        // 提出の完了まで遅らせる破棄の列（m_deferredDestroyMutex の下で読み書きする）
        enum class DeferredDestroyKind : uint8_t
        {
            Framebuffer,
            RenderPass,
            Pipeline,
            PipelineLayout,
            DescriptorSetLayout,
            DescriptorPool,
            Sampler,
            Buffer,
            Image,
            ImageView,
            AccelerationStructure,
            Memory,
        };
        struct DeferredDestroyEntry
        {
            DeferredDestroyKind Kind = DeferredDestroyKind::Framebuffer;
            uint64_t Handle = 0;
            // この通し番号の提出が完了したら破棄してよい
            uint64_t Serial = 0;
            // Memory のときの大きさ（バイト。統計だけに使う）
            uint64_t MemoryBytes = 0;
        };
        mutable Thread::Mutex m_deferredDestroyMutex;
        VariableArray<DeferredDestroyEntry> m_deferredDestroyQueue;
        // 次に提出する通し番号（スワップチェーンの通し番号）
        uint64_t m_deferredDestroyNextSerial = 0;
        // 提出を追っているスワップチェーンの数（0 ならその場で破棄する）
        uint32_t m_deferredDestroyTrackerCount = 0;
        // BeginFrame から提出（または提出の失敗）までの間
        bool m_bDeferredDestroyFrameRecording = false;
        uint64_t m_deferredDestroyEnqueuedCount = 0;
        uint64_t m_deferredDestroyDestroyedCount = 0;
        uint64_t m_deferredDestroyImmediateCount = 0;
        uint64_t m_deferredDestroyPendingMemoryBytes = 0;
        uint64_t m_deferredDestroyMaxPendingMemoryBytes = 0;
        // 定期の統計のログの間引き用
        uint64_t m_deferredDestroyCompletionCount = 0;

        void EnqueueDeferredDestroy(DeferredDestroyKind kind, uint64_t handle, uint64_t memoryBytes = 0) noexcept;
        void DestroyDeferredHandle(DeferredDestroyKind kind, uint64_t handle) noexcept;
        // 通し番号が lastCompletedSerial 以下の分を列から外して outEntries へ移す（m_deferredDestroyMutex を持って呼ぶ）
        void TakeCompletedDeferredDestroyEntriesLocked(
            uint64_t lastCompletedSerial,
            VariableArray<DeferredDestroyEntry> &outEntries) noexcept;
        // 列を全部破棄する（GPU が列のハンドルを使っていないと分かっているときだけ呼ぶ）
        void DrainDeferredDestroyQueue(const char *reason) noexcept;
        void LogDeferredDestroyStats(const char *reason) const;

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

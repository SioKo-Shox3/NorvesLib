#pragma once

#include "Debug/DebugConfig.h"
#include "RHI/ICommandList.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::RHI::Vulkan
{

    // グローバル名前空間から絶対パスで指定
    using ::NorvesLib::Core::Container::DynamicPointerCast;
    using ::NorvesLib::Core::Container::FixedArray;
    using ::NorvesLib::Core::Container::MakeShared;
    using ::NorvesLib::Core::Container::StaticPointerCast;
    using ::NorvesLib::Core::Container::String;
    using ::NorvesLib::Core::Container::TSharedPtr;
    using ::NorvesLib::Core::Container::TWeakPtr;
    using ::NorvesLib::Core::Container::UnorderedMap;
    using ::NorvesLib::Core::Container::VariableArray;

    namespace Detail
    {
        enum class GPUTimestampFrameState : uint8_t
        {
            Empty,
            Recording,
            Recorded,
            Submitted,
            Completed
        };

        struct GPUTimestampScopeRecord
        {
            String Name;
            bool bClosed = false;
            bool bLegacy = false;
        };

        struct GPUTimestampFrameBatch
        {
            GPUTimestampFrameState State = GPUTimestampFrameState::Empty;
            uint64_t FrameNumber = 0u;
            uint64_t SubmissionSerial = 0u;
            uint32_t FrameSlotIndex = UINT32_MAX;
            uint32_t ScopeCount = 0u;
            uint32_t ClosedScopeCount = 0u;
            bool bQueriesReset = false;
            FixedArray<GPUTimestampScopeRecord, MaximumGPUTimestampScopesPerFrame> Scopes;
        };

        inline void ClearGPUTimestampFrameBatch(GPUTimestampFrameBatch& batch) noexcept
        {
            batch.State = GPUTimestampFrameState::Empty;
            batch.FrameNumber = 0u;
            batch.SubmissionSerial = 0u;
            batch.FrameSlotIndex = UINT32_MAX;
            batch.ScopeCount = 0u;
            batch.ClosedScopeCount = 0u;
            batch.bQueriesReset = false;
            for (GPUTimestampScopeRecord& scope : batch.Scopes)
            {
                scope.Name.clear();
                scope.bClosed = false;
                scope.bLegacy = false;
            }
        }

        inline bool TryBeginGPUTimestampFrame(GPUTimestampFrameBatch& batch,
                                              uint64_t frameNumber,
                                              bool bQueriesReset)
        {
            if (batch.State != GPUTimestampFrameState::Empty || !bQueriesReset)
            {
                return false;
            }
            batch.FrameNumber = frameNumber;
            batch.SubmissionSerial = 0u;
            batch.ScopeCount = 0u;
            batch.ClosedScopeCount = 0u;
            batch.FrameSlotIndex = UINT32_MAX;
            batch.bQueriesReset = true;
            batch.State = GPUTimestampFrameState::Recording;
            return true;
        }

        inline bool TryBeginGPUTimestampScope(GPUTimestampFrameBatch& batch,
                                              uint32_t frameSlotIndex,
                                              const char* scopeName,
                                              GPUTimestampScopeHandle& outHandle)
        {
            outHandle = {};
            if (batch.State != GPUTimestampFrameState::Recording ||
                !batch.bQueriesReset ||
                batch.ScopeCount >= MaximumGPUTimestampScopesPerFrame)
            {
                return false;
            }

            const uint32_t scopeIndex = batch.ScopeCount++;
            if (batch.FrameSlotIndex == UINT32_MAX)
            {
                batch.FrameSlotIndex = frameSlotIndex;
            }
            if (batch.FrameSlotIndex != frameSlotIndex)
            {
                --batch.ScopeCount;
                return false;
            }
            GPUTimestampScopeRecord& scope = batch.Scopes[scopeIndex];
            scope.Name = scopeName ? scopeName : "GPU Scope";
            scope.bClosed = false;
            scope.bLegacy = false;
            outHandle.FrameSlotIndex = frameSlotIndex;
            outHandle.ScopeIndex = scopeIndex;
            outHandle.FrameNumber = batch.FrameNumber;
            return true;
        }

        inline bool TryEndGPUTimestampScope(GPUTimestampFrameBatch& batch,
                                            GPUTimestampScopeHandle handle)
        {
            if (batch.State != GPUTimestampFrameState::Recording ||
                !handle.IsValid() ||
                handle.FrameSlotIndex != batch.FrameSlotIndex ||
                handle.FrameNumber != batch.FrameNumber ||
                handle.ScopeIndex >= batch.ScopeCount)
            {
                return false;
            }

            GPUTimestampScopeRecord& scope = batch.Scopes[handle.ScopeIndex];
            if (scope.bClosed)
            {
                return false;
            }
            scope.bClosed = true;
            ++batch.ClosedScopeCount;
            return true;
        }

        inline bool TryEndGPUTimestampFrame(GPUTimestampFrameBatch& batch)
        {
            if (batch.State != GPUTimestampFrameState::Recording ||
                batch.ClosedScopeCount != batch.ScopeCount)
            {
                return false;
            }
            batch.State = GPUTimestampFrameState::Recorded;
            return true;
        }

        inline bool TryCommitGPUTimestampSubmission(GPUTimestampFrameBatch& batch,
                                                    uint64_t submissionSerial)
        {
            if (batch.State != GPUTimestampFrameState::Recorded || submissionSerial == 0u)
            {
                return false;
            }
            batch.SubmissionSerial = submissionSerial;
            batch.State = GPUTimestampFrameState::Submitted;
            return true;
        }

        inline bool TryNotifyGPUTimestampFrameCompleted(GPUTimestampFrameBatch& batch,
                                                        uint64_t completedSubmissionSerial)
        {
            if (batch.State != GPUTimestampFrameState::Submitted ||
                batch.SubmissionSerial == 0u ||
                completedSubmissionSerial < batch.SubmissionSerial)
            {
                return false;
            }
            batch.State = GPUTimestampFrameState::Completed;
            return true;
        }

        inline void AbortGPUTimestampFrameBatch(GPUTimestampFrameBatch& batch) noexcept
        {
            if (batch.State == GPUTimestampFrameState::Recording ||
                batch.State == GPUTimestampFrameState::Recorded)
            {
                ClearGPUTimestampFrameBatch(batch);
            }
        }

        inline uint64_t CalculateGPUTimestampTicks(uint64_t begin,
                                                   uint64_t end,
                                                   uint32_t validBits)
        {
            if (validBits == 0u || validBits > 64u)
            {
                return 0u;
            }
            const uint64_t mask = validBits == 64u
                                      ? UINT64_MAX
                                      : ((uint64_t{1} << validBits) - 1u);
            return ((end & mask) - (begin & mask)) & mask;
        }

        inline bool ShouldCarryGPUTimestampBatch(vk::Result result,
                                                 bool bAllQueriesAvailable)
        {
            return result == vk::Result::eNotReady ||
                   (result == vk::Result::eSuccess && !bAllQueriesAvailable);
        }

        class GPUTimestampSubmissionGuard final
        {
        public:
            GPUTimestampSubmissionGuard(ICommandList* commandList,
                                        uint32_t frameSlotIndex) noexcept
                : m_CommandList(commandList), m_FrameSlotIndex(frameSlotIndex)
            {
            }

            ~GPUTimestampSubmissionGuard() noexcept
            {
                if (m_CommandList && m_bArmed)
                {
                    m_CommandList->AbortGPUTimestampFrame(m_FrameSlotIndex);
                }
            }

            void Commit(uint64_t submissionSerial)
            {
                if (m_CommandList)
                {
                    m_CommandList->CommitGPUTimestampSubmission(
                        m_FrameSlotIndex,
                        submissionSerial);
                }
                m_bArmed = false;
            }

            GPUTimestampSubmissionGuard(const GPUTimestampSubmissionGuard&) = delete;
            GPUTimestampSubmissionGuard& operator=(const GPUTimestampSubmissionGuard&) = delete;
            GPUTimestampSubmissionGuard(GPUTimestampSubmissionGuard&&) = delete;
            GPUTimestampSubmissionGuard& operator=(GPUTimestampSubmissionGuard&&) = delete;

        private:
            ICommandList* m_CommandList = nullptr;
            uint32_t m_FrameSlotIndex = 0u;
            bool m_bArmed = true;
        };

        enum class GPUTimestampSubmissionSequenceStatus : uint8_t
        {
            Success,
            SerialAllocationFailed,
            FenceResetFailed,
            QueueSubmitFailed
        };

        template <typename AllocateSubmissionSerialCallable,
                  typename ResetFenceCallable,
                  typename QueueSubmitCallable>
        GPUTimestampSubmissionSequenceStatus ExecuteGPUTimestampSubmissionSequence(
            ICommandList* commandList,
            uint32_t frameSlotIndex,
            bool bResetFence,
            AllocateSubmissionSerialCallable&& allocateSubmissionSerial,
            ResetFenceCallable&& resetFence,
            QueueSubmitCallable&& queueSubmit,
            uint64_t& outSubmittedSerial)
        {
            outSubmittedSerial = 0u;
            GPUTimestampSubmissionGuard guard(commandList, frameSlotIndex);

            uint64_t submittedSerial = 0u;
            if (!allocateSubmissionSerial(submittedSerial) || submittedSerial == 0u)
            {
                return GPUTimestampSubmissionSequenceStatus::SerialAllocationFailed;
            }
            if (bResetFence && !resetFence())
            {
                return GPUTimestampSubmissionSequenceStatus::FenceResetFailed;
            }
            if (!queueSubmit())
            {
                return GPUTimestampSubmissionSequenceStatus::QueueSubmitFailed;
            }

            guard.Commit(submittedSerial);
            outSubmittedSerial = submittedSerial;
            return GPUTimestampSubmissionSequenceStatus::Success;
        }
    } // namespace Detail

    // Vulkanハンドル用カスタムハッシュ
    struct VkBufferHash
    {
        std::size_t operator()(const vk::Buffer &buffer) const noexcept
        {
            return std::hash<VkBuffer>()(static_cast<VkBuffer>(buffer));
        }
    };

    struct VkImageHash
    {
        std::size_t operator()(const vk::Image &image) const noexcept
        {
            return std::hash<VkImage>()(static_cast<VkImage>(image));
        }
    };

    class VulkanDevice;
    class VulkanBuffer;
    class VulkanTexture;
    class VulkanSampler;
    class VulkanPipeline;
    class VulkanRenderPass;
    class VulkanFramebuffer;
    class VulkanDescriptorSet;

    /**
     * @brief メモリバリア追跡情報
     */
    struct ResourceBarrierTracker
    {
        struct BufferState
        {
            vk::AccessFlags accessFlags = {};
            vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
            uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        };

        struct ImageState
        {
            vk::AccessFlags accessFlags = {};
            vk::ImageLayout layout = vk::ImageLayout::eUndefined;
            vk::PipelineStageFlags stageFlags = vk::PipelineStageFlagBits::eTopOfPipe;
            uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;
        };

        // リソースの状態を追跡
        UnorderedMap<vk::Buffer, BufferState, VkBufferHash> bufferStates;
        UnorderedMap<vk::Image, ImageState, VkImageHash> imageStates;

        // リソース状態からアクセスフラグに変換
        vk::AccessFlags ResourceStateToAccessFlags(ResourceState state) const;

        // リソース状態からパイプラインステージフラグに変換
        vk::PipelineStageFlags ResourceStateToPipelineStageFlags(ResourceState state) const;

        // リソース状態からイメージレイアウトに変換
        vk::ImageLayout ResourceStateToImageLayout(ResourceState state) const;
    };

    /**
     * @brief パイプラインステートキャッシュ
     */
    struct PipelineStateCache
    {
        // グラフィックスパイプラインのキーと生成済みパイプラインのマッピング
        struct GraphicsPipelineCacheKey
        {
            vk::RenderPass renderPass;
            VariableArray<vk::ShaderModule> shaderModules;
            VariableArray<vk::VertexInputBindingDescription> vertexBindings;
            VariableArray<vk::VertexInputAttributeDescription> vertexAttributes;
            vk::PrimitiveTopology topology;
            vk::CullModeFlags cullMode;
            vk::FrontFace frontFace;
            vk::PolygonMode polygonMode;
            bool bDepthTestEnable;
            bool bDepthWriteEnable;
            vk::CompareOp depthCompareOp;
            bool bBlendEnable;
            vk::BlendFactor srcColorBlendFactor;
            vk::BlendFactor dstColorBlendFactor;
            vk::BlendOp colorBlendOp;
            vk::BlendFactor srcAlphaBlendFactor;
            vk::BlendFactor dstAlphaBlendFactor;
            vk::BlendOp alphaBlendOp;

            // ハッシュ計算用関数
            bool operator==(const GraphicsPipelineCacheKey &other) const;
        };

        struct GraphicsPipelineCacheKeyHash
        {
            std::size_t operator()(const GraphicsPipelineCacheKey &key) const;
        };

        // コンピュートパイプラインのキーと生成済みパイプラインのマッピング
        struct ComputePipelineCacheKey
        {
            vk::ShaderModule computeShader;

            // ハッシュ計算用関数
            bool operator==(const ComputePipelineCacheKey &other) const;
        };

        struct ComputePipelineCacheKeyHash
        {
            std::size_t operator()(const ComputePipelineCacheKey &key) const;
        };

        // パイプラインキャッシュ
        UnorderedMap<GraphicsPipelineCacheKey, vk::Pipeline, GraphicsPipelineCacheKeyHash> graphicsPipelines;
        UnorderedMap<ComputePipelineCacheKey, vk::Pipeline, ComputePipelineCacheKeyHash> computePipelines;

        // Vulkanパイプラインキャッシュオブジェクト
        vk::PipelineCache pipelineCache;
    };

    /**
     * @brief シェーダーバインディングキー（ディスクリプタリソース管理用）
     */
    struct ShaderBindingKey
    {
        uint32_t set;      // ディスクリプタセット番号
        uint32_t binding;  // バインディング番号
        ShaderStage stage; // シェーダーステージ

        bool operator==(const ShaderBindingKey &other) const
        {
            return set == other.set && binding == other.binding && stage == other.stage;
        }
    };

    /**
     * @brief ShaderBindingKeyのハッシュ関数
     */
    struct ShaderBindingKeyHash
    {
        size_t operator()(const ShaderBindingKey &key) const
        {
            return std::hash<uint32_t>()(key.set) ^
                   (std::hash<uint32_t>()(key.binding) << 1) ^
                   (std::hash<uint32_t>()(static_cast<uint32_t>(key.stage)) << 2);
        }
    };

    /**
     * @brief バインディングリソース情報
     */
    struct BindingResourceInfo
    {
        enum class Type
        {
            Buffer,
            Texture,
            Sampler
        };

        Type type;
        TSharedPtr<void> resource;      // リソースへの参照
        uint64_t offset = 0;            // バッファのオフセット
        uint64_t range = VK_WHOLE_SIZE; // バッファのサイズ
    };

    /**
     * @brief Vulkanコマンドリスト実装クラス
     */
    class VulkanCommandList : public ICommandList
    {
    public:
        /**
         * @brief VulkanCommandListのコンストラクタ
         * @param device Vulkanデバイス
         */
        explicit VulkanCommandList(TSharedPtr<VulkanDevice> device);

        /**
         * @brief デストラクタ
         */
        ~VulkanCommandList() override;

        // ICommandListインターフェース実装
        void Begin() override;
        void BeginRecording() override;
        void SetFrameIndex(uint32_t frameIndex) override;
        void End() override;
        bool SupportsGPUTimestamps() const override;
        uint32_t GetMaximumGPUTimestampScopesPerFrame() const override;
        void BeginGPUTimestampFrame(uint64_t frameNumber) override;
        GPUTimestampScopeHandle BeginGPUTimestampScope(const char* scopeName) override;
        void EndGPUTimestampScope(GPUTimestampScopeHandle handle) override;
        void EndGPUTimestampFrame() override;
        void CommitGPUTimestampSubmission(uint32_t frameSlotIndex,
                                          uint64_t submissionSerial) override;
        void AbortGPUTimestampFrame(uint32_t frameSlotIndex) noexcept override;
        void NotifyGPUTimestampFrameSlotCompleted(
            uint32_t frameSlotIndex,
            uint64_t completedSubmissionSerial) override;
        void ConsumeCompletedGPUTimestampResults(
            VariableArray<GPUTimestampResult>& outResults) override;
        void BeginGPUTimestamp(const char* markerName = nullptr) override;
        void EndGPUTimestamp() override;
        float GetLastGPUTimestampDurationMs() const override;
        void BeginDebugMarker(const char* name) override;
        void EndDebugMarker() override;
        void Submit(bool bWaitForCompletion = false) override;

        void BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer) override;
        void EndRenderPass() override;

        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const ScissorRect &scissor) override;
        void SetPipeline(PipelinePtr pipeline) override;

        void SetVertexBuffer(BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override;
        void SetIndexBuffer(BufferPtr buffer, uint64_t offset = 0, IndexType type = IndexType::Uint32) override;
        void SetConstantBuffer(BufferPtr buffer, uint32_t slot, ShaderStage stage) override;
        void SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage) override;
        void SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage) override;
        void SetDescriptorSet(DescriptorSetPtr descriptorSet, uint32_t slot = 0) override;

        void DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0) override;
        void Draw(uint32_t vertexCount, uint32_t startVertexLocation = 0) override;
        void DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount,
                                  uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0, uint32_t startInstanceLocation = 0) override;
        void DrawInstanced(uint32_t vertexCount, uint32_t instanceCount,
                           uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) override;
        void DrawIndexedIndirect(BufferPtr indirectBuffer, uint64_t offset,
                                 uint32_t drawCount, uint32_t stride) override;
        void DrawIndexedIndirectCount(BufferPtr indirectBuffer, uint64_t indirectOffset,
                                      BufferPtr countBuffer, uint64_t countOffset,
                                      uint32_t maxDrawCount, uint32_t stride) override;
        void FillBuffer(BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value) override;
        void Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ) override;

        void CopyBuffer(BufferPtr src, BufferPtr dst, uint64_t size = 0,
                        uint64_t srcOffset = 0, uint64_t dstOffset = 0) override;
        void CopyBufferToTexture(BufferPtr src, TexturePtr dst,
                                 uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;
        void CopyTextureToBuffer(TexturePtr src, BufferPtr dst,
                                 uint32_t width, uint32_t height, uint64_t bufferOffset = 0,
                                 uint32_t mipLevel = 0, uint32_t arrayIndex = 0) override;
        void CopyTexture(TexturePtr src, TexturePtr dst,
                         uint32_t width, uint32_t height,
                         uint32_t srcMipLevel = 0, uint32_t srcArrayIndex = 0,
                         uint32_t dstMipLevel = 0, uint32_t dstArrayIndex = 0) override;
        void GenerateMipmaps(TexturePtr texture) override;

        void BufferBarrier(BufferPtr buffer, ResourceState beforeState, ResourceState afterState,
                           uint64_t offset = 0, uint64_t size = 0) override;
        void TextureBarrier(TexturePtr texture, ResourceState beforeState, ResourceState afterState,
                            uint32_t mipLevel = 0, uint32_t arrayIndex = 0, uint32_t mipCount = 0, uint32_t arrayCount = 0) override;

        // Vulkan固有のメソッド
        vk::CommandBuffer GetVkCommandBuffer() const { return m_commandBuffer; }
        bool IsInRenderPass() const { return m_bInRenderPass; }

        /**
         * @brief 最適化されたバッファバリア
         * @param buffer Vulkanバッファ
         * @param newState 新しいリソース状態
         * @param offset オフセット
         * @param size サイズ
         */
        void OptimizedBufferBarrier(VulkanBuffer *buffer, ResourceState newState, uint64_t offset = 0, uint64_t size = 0);

        /**
         * @brief 最適化されたテクスチャバリア
         * @param texture Vulkanテクスチャ
         * @param newState 新しいリソース状態
         * @param subresourceRange サブリソース範囲
         */
        void OptimizedTextureBarrier(VulkanTexture *texture, ResourceState newState, const vk::ImageSubresourceRange &subresourceRange);

        /**
         * @brief グラフィックスパイプラインのキャッシュ取得/作成
         * @param key パイプラインキャッシュキー
         * @return キャッシュされたパイプライン、または新しく作成されたパイプライン
         */
        vk::Pipeline GetOrCreateGraphicsPipeline(const PipelineStateCache::GraphicsPipelineCacheKey &key);

        /**
         * @brief コンピュートパイプラインのキャッシュ取得/作成
         * @param key パイプラインキャッシュキー
         * @return キャッシュされたパイプライン、または新しく作成されたパイプライン
         */
        vk::Pipeline GetOrCreateComputePipeline(const PipelineStateCache::ComputePipelineCacheKey &key);

        /**
         * @brief パイプラインキャッシュの保存
         * @param filePath 保存先ファイルパス
         */
        void SavePipelineCache(const String &filePath);

        /**
         * @brief パイプラインキャッシュの読み込み
         * @param filePath 読み込み元ファイルパス
         */
        void LoadPipelineCache(const String &filePath);

        /**
         * @brief リソースバリア追跡のリセット
         */
        void ResetResourceBarriers();

    private:
        TSharedPtr<VulkanDevice> m_device;
        vk::CommandBuffer m_commandBuffer;                 ///< 現在のフレームのコマンドバッファ（m_commandBuffersの要素を参照）
        VariableArray<vk::CommandBuffer> m_commandBuffers; ///< フレームごとのコマンドバッファ
        uint32_t m_currentFrameIndex = 0;
        static constexpr uint32_t MAX_COMMAND_BUFFERS = 3; ///< トリプルバッファリングまで対応
        vk::Fence m_fence;

        // ディスクリプタプール関連
        VariableArray<vk::DescriptorPool> m_descriptorPools;
        static constexpr uint32_t MAX_DESCRIPTOR_SETS = 100;
        static constexpr uint32_t MAX_DESCRIPTORS_PER_TYPE = 1000;

        bool m_bIsRecording = false;
        bool m_bInRenderPass = false;

        // レンダーパス終了時のレイアウト追跡用
        TSharedPtr<VulkanRenderPass> m_activeRenderPass;
        TSharedPtr<VulkanFramebuffer> m_activeFramebuffer;

        /// @brief レンダーパス終了後にアタッチメントテクスチャのレイアウトを更新
        void UpdateAttachmentLayoutsAfterRenderPass();

        // 現在バインドされているリソース
        PipelinePtr m_currentPipeline;
        VariableArray<BufferPtr> m_currentVertexBuffers;
        VariableArray<uint64_t> m_currentVertexBufferOffsets;
        BufferPtr m_currentIndexBuffer;
        uint64_t m_currentIndexBufferOffset = 0;

        // リソースバリア追跡
        ResourceBarrierTracker m_barrierTracker;

        // パイプラインステートキャッシュ
        PipelineStateCache m_pipelineStateCache;

        // 一時リソース保存用（リソース解放を防ぐため）
        VariableArray<TSharedPtr<void>> m_temporaryResources;

        // ディスクリプタセット管理
        struct DescriptorSetInfo
        {
            vk::DescriptorSet descriptorSet;
            UnorderedMap<ShaderBindingKey, BindingResourceInfo, ShaderBindingKeyHash> resources;
            bool bIsDirty = false;
        };

        UnorderedMap<uint32_t, DescriptorSetInfo> m_descriptorSetCache;

        // バインディングリソース情報
        UnorderedMap<ShaderBindingKey, BindingResourceInfo, ShaderBindingKeyHash> m_bindingResources;

        // プライベートメソッド
        void Reset();
        void CreateDescriptorPool();
        void DestroyDescriptorPool();
        bool UpdateDescriptorSet(uint32_t setIndex);
        vk::DescriptorSet GetOrCreateDescriptorSet(uint32_t setIndex, vk::DescriptorSetLayout layout);
        void BindDescriptorSets();

#if NORVES_ENABLE_STATS
        void CreateTimestampQueryPool();
        void DestroyTimestampQueryPool();
        void ResolveGPUTimestampResultsForCurrentSlot();
        void PrepareGPUTimestampSlotForRecording();
        uint32_t GetTimestampQueryBaseIndex(uint32_t frameSlotIndex,
                                            uint32_t scopeIndex = 0u) const;
#endif

        // リソース参照の追加（リソース解放防止用）
        template <typename T>
        void AddTemporaryResource(TSharedPtr<T> resource)
        {
            m_temporaryResources.push_back(StaticPointerCast<void>(resource));
        }

        // シェーダーステージをVkPipelineStageに変換
        vk::PipelineStageFlags ToVkPipelineStage(ShaderStage stage) const;
        // シェーダーステージをVkシェーダーステージに変換
        vk::ShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) const;

        PFN_vkCmdBeginDebugUtilsLabelEXT m_pfnBeginDebugUtilsLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT m_pfnEndDebugUtilsLabel = nullptr;

#if NORVES_ENABLE_STATS
        vk::QueryPool m_timestampQueryPool;
        bool m_bTimestampSupported = false;
        FixedArray<Detail::GPUTimestampFrameBatch, MAX_COMMAND_BUFFERS> m_TimestampFrameBatches;
        VariableArray<GPUTimestampResult> m_CompletedGPUTimestampResults;
        GPUTimestampScopeHandle m_LegacyGPUTimestampScope;
        FixedArray<uint64_t, MAX_COMMAND_BUFFERS> m_DirectFrameSlotSubmissionSerials = {};
        uint64_t m_DirectNextSubmissionSerial = 0u;
        uint64_t m_InvalidGPUTimestampOperationCount = 0u;
        uint64_t m_GPUTimestampResolveErrorCount = 0u;
        bool m_bTimestampFrameActive = false;
        bool m_bLegacyPrivateTimestampFrame = false;
        float m_lastGPUTimestampDurationMs = 0.0f;
        float m_timestampPeriodNs = 0.0f;
        uint32_t m_TimestampValidBits = 0u;
#endif
    };

} // namespace NorvesLib::RHI::Vulkan

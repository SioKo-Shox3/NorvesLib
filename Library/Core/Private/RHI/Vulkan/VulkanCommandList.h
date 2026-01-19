#pragma once

#include "RHI/ICommandList.h"
#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include "Container/Containers.h"
#include "Container/PointerTypes.h"

namespace NorvesLib::RHI::Vulkan
{

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
        NorvesLib::Core::Container::UnorderedMap<vk::Buffer, BufferState> bufferStates;
        NorvesLib::Core::Container::UnorderedMap<vk::Image, ImageState> imageStates;

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
            NorvesLib::Core::Container::VariableArray<vk::ShaderModule> shaderModules;
            NorvesLib::Core::Container::VariableArray<vk::VertexInputBindingDescription> vertexBindings;
            NorvesLib::Core::Container::VariableArray<vk::VertexInputAttributeDescription> vertexAttributes;
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
        NorvesLib::Core::Container::UnorderedMap<GraphicsPipelineCacheKey, vk::Pipeline, GraphicsPipelineCacheKeyHash> graphicsPipelines;
        NorvesLib::Core::Container::UnorderedMap<ComputePipelineCacheKey, vk::Pipeline, ComputePipelineCacheKeyHash> computePipelines;

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
        void End() override;
        void Submit(bool bWaitForCompletion = false) override;

        void BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer) override;
        void EndRenderPass() override;

        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const ScissorRect &scissor) override;
        void SetPipeline(PipelinePtr pipeline) override;

        void SetVertexBuffer(BufferPtr buffer, uint64_t offset = 0, uint32_t slot = 0) override;
        void SetIndexBuffer(BufferPtr buffer, uint64_t offset = 0) override;
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
        void SavePipelineCache(const NorvesLib::Core::Container::String &filePath);

        /**
         * @brief パイプラインキャッシュの読み込み
         * @param filePath 読み込み元ファイルパス
         */
        void LoadPipelineCache(const NorvesLib::Core::Container::String &filePath);

        /**
         * @brief リソースバリア追跡のリセット
         */
        void ResetResourceBarriers();

    private:
        TSharedPtr<VulkanDevice> m_device;
        vk::CommandBuffer m_commandBuffer;
        vk::Fence m_fence;

        // ディスクリプタプール関連
        NorvesLib::Core::Container::VariableArray<vk::DescriptorPool> m_descriptorPools;
        static constexpr uint32_t MAX_DESCRIPTOR_SETS = 100;
        static constexpr uint32_t MAX_DESCRIPTORS_PER_TYPE = 1000;

        bool m_bIsRecording = false;
        bool m_bInRenderPass = false;

        // 現在バインドされているリソース
        PipelinePtr m_currentPipeline;
        NorvesLib::Core::Container::VariableArray<BufferPtr> m_currentVertexBuffers;
        NorvesLib::Core::Container::VariableArray<uint64_t> m_currentVertexBufferOffsets;
        BufferPtr m_currentIndexBuffer;
        uint64_t m_currentIndexBufferOffset = 0;

        // リソースバリア追跡
        ResourceBarrierTracker m_barrierTracker;

        // パイプラインステートキャッシュ
        PipelineStateCache m_pipelineStateCache;

        // 一時リソース保存用（リソース解放を防ぐため）
        NorvesLib::Core::Container::VariableArray<TSharedPtr<void>> m_temporaryResources;

        // ディスクリプタセット管理
        struct DescriptorSetInfo
        {
            vk::DescriptorSet descriptorSet;
            NorvesLib::Core::Container::UnorderedMap<ShaderBindingKey, BindingResourceInfo, ShaderBindingKeyHash> resources;
            bool bIsDirty = false;
        };

        NorvesLib::Core::Container::UnorderedMap<uint32_t, DescriptorSetInfo> m_descriptorSetCache;

        // バインディングリソース情報
        NorvesLib::Core::Container::UnorderedMap<ShaderBindingKey, BindingResourceInfo, ShaderBindingKeyHash> m_bindingResources;

        // プライベートメソッド
        void Reset();
        void CreateDescriptorPool();
        void DestroyDescriptorPool();
        bool UpdateDescriptorSet(uint32_t setIndex);
        vk::DescriptorSet GetOrCreateDescriptorSet(uint32_t setIndex, vk::DescriptorSetLayout layout);
        void BindDescriptorSets();

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
    };

} // namespace NorvesLib::RHI::Vulkan

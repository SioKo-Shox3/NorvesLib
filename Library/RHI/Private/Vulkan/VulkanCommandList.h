#pragma once

#include "RHI/Public/ICommandList.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <set>

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
    struct BufferState {
        VkAccessFlags accessFlags = 0;
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    };
    
    struct ImageState {
        VkAccessFlags accessFlags = 0;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags stageFlags = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        uint32_t queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    };
    
    // リソースの状態を追跡
    std::unordered_map<VkBuffer, BufferState> bufferStates;
    std::unordered_map<VkImage, ImageState> imageStates;
    
    // リソース状態からアクセスフラグに変換
    VkAccessFlags ResourceStateToAccessFlags(ResourceState state) const;
    
    // リソース状態からパイプラインステージフラグに変換
    VkPipelineStageFlags ResourceStateToPipelineStageFlags(ResourceState state) const;
    
    // リソース状態からイメージレイアウトに変換
    VkImageLayout ResourceStateToImageLayout(ResourceState state) const;
};

/**
 * @brief パイプラインステートキャッシュ
 */
struct PipelineStateCache
{
    // グラフィックスパイプラインのキーと生成済みパイプラインのマッピング
    struct GraphicsPipelineCacheKey 
    {
        VkRenderPass renderPass;
        std::vector<VkShaderModule> shaderModules;
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkPrimitiveTopology topology;
        VkCullModeFlags cullMode;
        VkFrontFace frontFace;
        VkPolygonMode polygonMode;
        bool depthTestEnable;
        bool depthWriteEnable;
        VkCompareOp depthCompareOp;
        bool blendEnable;
        VkBlendFactor srcColorBlendFactor;
        VkBlendFactor dstColorBlendFactor;
        VkBlendOp colorBlendOp;
        VkBlendFactor srcAlphaBlendFactor;
        VkBlendFactor dstAlphaBlendFactor;
        VkBlendOp alphaBlendOp;
        
        // ハッシュ計算用関数
        bool operator==(const GraphicsPipelineCacheKey& other) const;
    };
    
    struct GraphicsPipelineCacheKeyHash
    {
        std::size_t operator()(const GraphicsPipelineCacheKey& key) const;
    };
    
    // コンピュートパイプラインのキーと生成済みパイプラインのマッピング
    struct ComputePipelineCacheKey
    {
        VkShaderModule computeShader;
        
        // ハッシュ計算用関数
        bool operator==(const ComputePipelineCacheKey& other) const;
    };
    
    struct ComputePipelineCacheKeyHash
    {
        std::size_t operator()(const ComputePipelineCacheKey& key) const;
    };
    
    // パイプラインキャッシュ
    std::unordered_map<GraphicsPipelineCacheKey, VkPipeline, GraphicsPipelineCacheKeyHash> graphicsPipelines;
    std::unordered_map<ComputePipelineCacheKey, VkPipeline, ComputePipelineCacheKeyHash> computePipelines;
    
    // Vulkanパイプラインキャッシュオブジェクト
    VkPipelineCache vkPipelineCache = VK_NULL_HANDLE;
};

/**
 * @brief シェーダーバインディングキー（ディスクリプタリソース管理用）
 */
struct ShaderBindingKey {
    uint32_t set;          // ディスクリプタセット番号
    uint32_t binding;      // バインディング番号
    ShaderStage stage;     // シェーダーステージ
    
    bool operator==(const ShaderBindingKey& other) const {
        return set == other.set && binding == other.binding && stage == other.stage;
    }
};

/**
 * @brief ShaderBindingKeyのハッシュ関数
 */
struct ShaderBindingKeyHash {
    size_t operator()(const ShaderBindingKey& key) const {
        return std::hash<uint32_t>()(key.set) ^
               (std::hash<uint32_t>()(key.binding) << 1) ^
               (std::hash<uint32_t>()((uint32_t)key.stage) << 2);
    }
};

/**
 * @brief バインディングリソース情報
 */
struct BindingResourceInfo {
    enum class Type {
        Buffer,
        Texture,
        Sampler
    };
    
    Type type;
    std::shared_ptr<void> resource; // リソースへの参照
    uint64_t offset = 0;           // バッファのオフセット
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
    explicit VulkanCommandList(std::shared_ptr<VulkanDevice> device);
    
    /**
     * @brief デストラクタ
     */
    ~VulkanCommandList() override;

    // ICommandListインターフェース実装
    void Begin() override;
    void End() override;
    void Submit(bool waitForCompletion = false) override;
    
    void BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer) override;
    void EndRenderPass() override;
    
    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const ScissorRect& scissor) override;
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
    VkCommandBuffer GetVkCommandBuffer() const { return m_commandBuffer; }
    bool IsInRenderPass() const { return m_inRenderPass; }

    /**
     * @brief 最適化されたバッファバリア
     * @param buffer Vulkanバッファ
     * @param newState 新しいリソース状態
     * @param offset オフセット
     * @param size サイズ
     */
    void OptimizedBufferBarrier(VulkanBuffer* buffer, ResourceState newState, uint64_t offset = 0, uint64_t size = 0);
    
    /**
     * @brief 最適化されたテクスチャバリア
     * @param texture Vulkanテクスチャ
     * @param newState 新しいリソース状態
     * @param subresourceRange サブリソース範囲
     */
    void OptimizedTextureBarrier(VulkanTexture* texture, ResourceState newState, const VkImageSubresourceRange& subresourceRange);
    
    /**
     * @brief グラフィックスパイプラインのキャッシュ取得/作成
     * @param key パイプラインキャッシュキー
     * @return キャッシュされたパイプライン、または新しく作成されたパイプライン
     */
    VkPipeline GetOrCreateGraphicsPipeline(const PipelineStateCache::GraphicsPipelineCacheKey& key);
    
    /**
     * @brief コンピュートパイプラインのキャッシュ取得/作成
     * @param key パイプラインキャッシュキー
     * @return キャッシュされたパイプライン、または新しく作成されたパイプライン
     */
    VkPipeline GetOrCreateComputePipeline(const PipelineStateCache::ComputePipelineCacheKey& key);
    
    /**
     * @brief パイプラインキャッシュの保存
     * @param filePath 保存先ファイルパス
     */
    void SavePipelineCache(const std::string& filePath);
    
    /**
     * @brief パイプラインキャッシュの読み込み
     * @param filePath 読み込み元ファイルパス
     */
    void LoadPipelineCache(const std::string& filePath);
    
    /**
     * @brief リソースバリア追跡のリセット
     */
    void ResetResourceBarriers();

private:
    std::shared_ptr<VulkanDevice> m_device;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    
    // ディスクリプタプール関連
    std::vector<VkDescriptorPool> m_descriptorPools;
    static constexpr uint32_t MAX_DESCRIPTOR_SETS = 100;
    static constexpr uint32_t MAX_DESCRIPTORS_PER_TYPE = 1000;
    
    bool m_isRecording = false;
    bool m_inRenderPass = false;
    
    // 現在バインドされているリソース
    PipelinePtr m_currentPipeline;
    std::vector<BufferPtr> m_currentVertexBuffers;
    std::vector<uint64_t> m_currentVertexBufferOffsets;
    BufferPtr m_currentIndexBuffer;
    uint64_t m_currentIndexBufferOffset = 0;
    
    // リソースバリア追跡
    ResourceBarrierTracker m_barrierTracker;
    
    // パイプラインステートキャッシュ
    PipelineStateCache m_pipelineStateCache;
    
    // 一時リソース保存用（リソース解放を防ぐため）
    std::vector<std::shared_ptr<void>> m_temporaryResources;
    
    // ディスクリプタセット管理
    struct DescriptorSetInfo {
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        std::unordered_map<ShaderBindingKey, BindingResourceInfo, ShaderBindingKeyHash> resources;
        bool isDirty = false;
    };
    
    std::unordered_map<uint32_t, DescriptorSetInfo> m_descriptorSetCache;
    
    // プライベートメソッド
    void Reset();
    void CreateDescriptorPool();
    void DestroyDescriptorPool();
    bool UpdateDescriptorSet(uint32_t setIndex);
    VkDescriptorSet GetOrCreateDescriptorSet(uint32_t setIndex, VkDescriptorSetLayout layout);
    void BindDescriptorSets();
    
    // リソース参照の追加（リソース解放防止用）
    template<typename T>
    void AddTemporaryResource(std::shared_ptr<T> resource) {
        m_temporaryResources.push_back(std::static_pointer_cast<void>(resource));
    }
    
    // シェーダーステージをVkPipelineStageに変換
    VkPipelineStageFlags ToVkPipelineStage(ShaderStage stage) const;
    // シェーダーステージをVkシェーダーステージに変換
    VkShaderStageFlags ToVkShaderStageFlags(ShaderStage stage) const;
};

} // namespace NorvesLib::RHI::Vulkan
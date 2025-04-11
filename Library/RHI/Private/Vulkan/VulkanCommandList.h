#pragma once

#include "RHI/Public/ICommandList.h"
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

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

    // Vulkan固有のメソッド
    VkCommandBuffer GetVkCommandBuffer() const { return m_commandBuffer; }
    bool IsInRenderPass() const { return m_inRenderPass; }

private:
    std::shared_ptr<VulkanDevice> m_device;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    
    bool m_isRecording = false;
    bool m_inRenderPass = false;
    
    // 現在バインドされているリソース
    PipelinePtr m_currentPipeline;
    std::vector<BufferPtr> m_currentVertexBuffers;
    std::vector<uint64_t> m_currentVertexBufferOffsets;
    BufferPtr m_currentIndexBuffer;
    uint64_t m_currentIndexBufferOffset = 0;
    
    // 一時リソース保存用（リソース解放を防ぐため）
    std::vector<std::shared_ptr<void>> m_temporaryResources;
    
    // コマンドバッファの再利用処理
    void Reset();
    
    // リソース参照の追加（リソース解放防止用）
    template<typename T>
    void AddTemporaryResource(std::shared_ptr<T> resource) {
        m_temporaryResources.push_back(std::static_pointer_cast<void>(resource));
    }
    
    // シェーダーステージをVkPipelineStageに変換
    VkPipelineStageFlags ToVkPipelineStage(ShaderStage stage) const;
};

} // namespace NorvesLib::RHI::Vulkan
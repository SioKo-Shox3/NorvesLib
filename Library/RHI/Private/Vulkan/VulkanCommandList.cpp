#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include <stdexcept>

namespace NorvesLib::RHI::Vulkan
{

// コンストラクタ
VulkanCommandList::VulkanCommandList(std::shared_ptr<VulkanDevice> device)
    : m_device(device)
{
    // コマンドバッファの割り当て
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = device->GetCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    
    if (vkAllocateCommandBuffers(device->GetVkDevice(), &allocInfo, &m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("コマンドバッファの割り当てに失敗しました");
    }
    
    // フェンスの作成
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // 初期状態はシグナル状態
    
    if (vkCreateFence(device->GetVkDevice(), &fenceInfo, nullptr, &m_fence) != VK_SUCCESS) {
        throw std::runtime_error("フェンスの作成に失敗しました");
    }
}

// デストラクタ
VulkanCommandList::~VulkanCommandList()
{
    // 一時リソースをクリア
    m_temporaryResources.clear();
    
    // フェンスの破棄
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device->GetVkDevice(), m_fence, nullptr);
    }
    
    // コマンドバッファの解放（コマンドプールごと破棄される場合があるので省略可能）
    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &m_commandBuffer);
    }
}

// コマンドバッファのリセット
void VulkanCommandList::Reset()
{
    // リソース参照をクリア
    m_temporaryResources.clear();
    
    // フェンスをリセット
    vkResetFences(m_device->GetVkDevice(), 1, &m_fence);
    
    // コマンドバッファをリセット
    vkResetCommandBuffer(m_commandBuffer, 0);
    
    // 状態のリセット
    m_isRecording = false;
    m_inRenderPass = false;
}

// コマンドリストの記録開始
void VulkanCommandList::Begin()
{
    if (m_isRecording) {
        throw std::runtime_error("コマンドリストは既に記録中です");
    }
    
    Reset();
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("コマンドバッファの記録開始に失敗しました");
    }
    
    m_isRecording = true;
}

// コマンドリストの記録終了
void VulkanCommandList::End()
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    if (m_inRenderPass) {
        EndRenderPass();
    }
    
    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("コマンドバッファの記録終了に失敗しました");
    }
    
    m_isRecording = false;
}

// コマンドリストの実行
void VulkanCommandList::Submit(bool waitForCompletion)
{
    if (m_isRecording) {
        End();
    }
    
    // 提出情報の設定
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    
    // コマンドバッファの提出
    if (vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, m_fence) != VK_SUCCESS) {
        throw std::runtime_error("コマンドバッファの提出に失敗しました");
    }
    
    // 完了を待つ場合
    if (waitForCompletion) {
        if (vkWaitForFences(m_device->GetVkDevice(), 1, &m_fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            throw std::runtime_error("フェンスの待機に失敗しました");
        }
    }
}

// レンダーパスの開始
void VulkanCommandList::BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer)
{
    // Vulkan実装クラスへのキャストはまだ実装されていません
    // 実際の実装では、renderPassとframebufferをダウンキャストして使用します
    // このコードは実際のVulkan RenderPass/Framebufferが実装されるまでのプレースホルダーです
    
    /*
    auto vkRenderPass = std::dynamic_pointer_cast<VulkanRenderPass>(renderPass);
    auto vkFramebuffer = std::dynamic_pointer_cast<VulkanFramebuffer>(framebuffer);
    
    if (!vkRenderPass || !vkFramebuffer) {
        throw std::runtime_error("無効なレンダーパスまたはフレームバッファです");
    }
    
    // レンダーパス情報の設定
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = vkRenderPass->GetVkRenderPass();
    renderPassInfo.framebuffer = vkFramebuffer->GetVkFramebuffer();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = {vkFramebuffer->GetWidth(), vkFramebuffer->GetHeight()};
    
    // クリア値の設定
    std::vector<VkClearValue> clearValues;
    for (uint32_t i = 0; i < vkFramebuffer->GetColorAttachmentCount(); i++) {
        VkClearValue clearValue{};
        clearValue.color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // デフォルトのクリアカラー
        clearValues.push_back(clearValue);
    }
    
    if (vkFramebuffer->HasDepthStencilAttachment()) {
        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};
        clearValues.push_back(clearValue);
    }
    
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    
    // レンダーパスの開始
    vkCmdBeginRenderPass(m_commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    */
    
    // 仮の実装（実際のVulkanオブジェクトが実装されるまで）
    m_inRenderPass = true;
    
    // 一時的なリソースへの参照を保持
    AddTemporaryResource(renderPass);
    AddTemporaryResource(framebuffer);
}

// レンダーパスの終了
void VulkanCommandList::EndRenderPass()
{
    if (!m_inRenderPass) {
        throw std::runtime_error("レンダーパス外でEndRenderPassが呼ばれました");
    }
    
    // vkCmdEndRenderPass(m_commandBuffer);
    m_inRenderPass = false;
}

// ビューポートの設定
void VulkanCommandList::SetViewport(const Viewport& viewport)
{
    VkViewport vkViewport{};
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;
    
    vkCmdSetViewport(m_commandBuffer, 0, 1, &vkViewport);
}

// シザー矩形の設定
void VulkanCommandList::SetScissor(const ScissorRect& scissor)
{
    VkRect2D vkScissor{};
    vkScissor.offset = {scissor.left, scissor.top};
    vkScissor.extent = {
        static_cast<uint32_t>(scissor.right - scissor.left),
        static_cast<uint32_t>(scissor.bottom - scissor.top)
    };
    
    vkCmdSetScissor(m_commandBuffer, 0, 1, &vkScissor);
}

// パイプラインの設定
void VulkanCommandList::SetPipeline(PipelinePtr pipeline)
{
    /*
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
    if (!vkPipeline) {
        throw std::runtime_error("無効なパイプラインです");
    }
    
    if (vkPipeline->IsComputePipeline()) {
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline->GetVkPipeline());
    } else {
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetVkPipeline());
    }
    */
    
    m_currentPipeline = pipeline;
    AddTemporaryResource(pipeline);
}

// 頂点バッファの設定
void VulkanCommandList::SetVertexBuffer(BufferPtr buffer, uint64_t offset, uint32_t slot)
{
    auto vkBuffer = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!vkBuffer) {
        throw std::runtime_error("無効なバッファです");
    }
    
    // スロットの確保
    while (m_currentVertexBuffers.size() <= slot) {
        m_currentVertexBuffers.push_back(nullptr);
        m_currentVertexBufferOffsets.push_back(0);
    }
    
    // バッファの設定
    m_currentVertexBuffers[slot] = buffer;
    m_currentVertexBufferOffsets[slot] = offset;
    
    // Vulkanコマンドの発行
    VkBuffer vkBufferHandle = vkBuffer->GetVkBuffer();
    vkCmdBindVertexBuffers(m_commandBuffer, slot, 1, &vkBufferHandle, &offset);
    
    AddTemporaryResource(buffer);
}

// インデックスバッファの設定
void VulkanCommandList::SetIndexBuffer(BufferPtr buffer, uint64_t offset)
{
    auto vkBuffer = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!vkBuffer) {
        throw std::runtime_error("無効なバッファです");
    }
    
    m_currentIndexBuffer = buffer;
    m_currentIndexBufferOffset = offset;
    
    // インデックスタイプは32bitを仮定
    vkCmdBindIndexBuffer(m_commandBuffer, vkBuffer->GetVkBuffer(), offset, VK_INDEX_TYPE_UINT32);
    
    AddTemporaryResource(buffer);
}

// 定数バッファの設定
void VulkanCommandList::SetConstantBuffer(BufferPtr buffer, uint32_t slot, ShaderStage stage)
{
    // ディスクリプタセットを使用した実装が必要
    // このメソッドは、VulkanDescriptorSetが実装された後に完成します
    
    AddTemporaryResource(buffer);
}

// テクスチャの設定
void VulkanCommandList::SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage)
{
    // ディスクリプタセットを使用した実装が必要
    // このメソッドは、VulkanDescriptorSetが実装された後に完成します
    
    AddTemporaryResource(texture);
}

// サンプラーの設定
void VulkanCommandList::SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage)
{
    // ディスクリプタセットを使用した実装が必要
    // このメソッドは、VulkanDescriptorSetが実装された後に完成します
    
    AddTemporaryResource(sampler);
}

// ディスクリプタセットの設定
void VulkanCommandList::SetDescriptorSet(DescriptorSetPtr descriptorSet, uint32_t slot)
{
    if (!m_isRecording)
    {
        // エラー：コマンドバッファが記録中でない
        return;
    }
    
    // VulkanDescriptorSetにキャスト
    auto vulkanDescriptorSet = std::static_pointer_cast<VulkanDescriptorSet>(descriptorSet);
    if (!vulkanDescriptorSet)
    {
        // エラー：無効なディスクリプタセット
        return;
    }
    
    // 現在のパイプラインがない場合は設定できない
    if (!m_currentPipeline)
    {
        // エラー：パイプラインがバインドされていない
        return;
    }
    
    auto vulkanPipeline = std::static_pointer_cast<VulkanPipeline>(m_currentPipeline);
    
    // ディスクリプタセットをバインド
    vkCmdBindDescriptorSets(
        m_commandBuffer,
        vulkanPipeline->IsCompute() ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkanPipeline->GetPipelineLayout(),
        slot, // セットインデックス
        1,    // セット数
        vulkanDescriptorSet->GetVkDescriptorSet(),
        0,    // 動的オフセットの数
        nullptr // 動的オフセット配列
    );
    
    // ディスクリプタセットを一時的なリソースリストに追加して解放を防ぐ
    AddTemporaryResource(vulkanDescriptorSet);
}

// インデックス付き描画
void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
{
    if (!m_inRenderPass) {
        throw std::runtime_error("レンダーパス外でDrawIndexedが呼ばれました");
    }
    
    if (!m_currentIndexBuffer) {
        throw std::runtime_error("インデックスバッファが設定されていません");
    }
    
    vkCmdDrawIndexed(m_commandBuffer, indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}

// インデックスなし描画
void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
{
    if (!m_inRenderPass) {
        throw std::runtime_error("レンダーパス外でDrawが呼ばれました");
    }
    
    vkCmdDraw(m_commandBuffer, vertexCount, 1, startVertexLocation, 0);
}

// インスタンシング描画（インデックス付き）
void VulkanCommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, 
    uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation)
{
    if (!m_inRenderPass) {
        throw std::runtime_error("レンダーパス外でDrawIndexedInstancedが呼ばれました");
    }
    
    if (!m_currentIndexBuffer) {
        throw std::runtime_error("インデックスバッファが設定されていません");
    }
    
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, 
        startIndexLocation, baseVertexLocation, startInstanceLocation);
}

// インスタンシング描画（インデックスなし）
void VulkanCommandList::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, 
    uint32_t startVertexLocation, uint32_t startInstanceLocation)
{
    if (!m_inRenderPass) {
        throw std::runtime_error("レンダーパス外でDrawInstancedが呼ばれました");
    }
    
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, startVertexLocation, startInstanceLocation);
}

// コンピュートシェーダーディスパッチ
void VulkanCommandList::Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
    if (m_inRenderPass) {
        throw std::runtime_error("レンダーパス内でDispatchが呼ばれました");
    }
    
    if (!m_currentPipeline) {
        throw std::runtime_error("パイプラインが設定されていません");
    }
    
    /*
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(m_currentPipeline);
    if (!vkPipeline->IsComputePipeline()) {
        throw std::runtime_error("コンピュートパイプラインが設定されていません");
    }
    */
    
    vkCmdDispatch(m_commandBuffer, threadGroupCountX, threadGroupCountY, threadGroupCountZ);
}

// バッファコピー
void VulkanCommandList::CopyBuffer(BufferPtr src, BufferPtr dst, uint64_t size, 
    uint64_t srcOffset, uint64_t dstOffset)
{
    auto vkSrc = std::dynamic_pointer_cast<VulkanBuffer>(src);
    auto vkDst = std::dynamic_pointer_cast<VulkanBuffer>(dst);
    
    if (!vkSrc || !vkDst) {
        throw std::runtime_error("無効なバッファです");
    }
    
    // サイズの決定
    uint64_t copySize = size;
    if (copySize == 0) {
        copySize = vkSrc->GetSize() - srcOffset;
    }
    
    // コピー領域の設定
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = copySize;
    
    // コピーコマンドを発行
    vkCmdCopyBuffer(m_commandBuffer, vkSrc->GetVkBuffer(), vkDst->GetVkBuffer(), 1, &copyRegion);
    
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// バッファからテクスチャへのコピー
void VulkanCommandList::CopyBufferToTexture(BufferPtr src, TexturePtr dst, 
    uint32_t width, uint32_t height, uint64_t bufferOffset, 
    uint32_t mipLevel, uint32_t arrayIndex)
{
    // テクスチャ実装後に実装
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// テクスチャからバッファへのコピー
void VulkanCommandList::CopyTextureToBuffer(TexturePtr src, BufferPtr dst, 
    uint32_t width, uint32_t height, uint64_t bufferOffset, 
    uint32_t mipLevel, uint32_t arrayIndex)
{
    // テクスチャ実装後に実装
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// テクスチャからテクスチャへのコピー
void VulkanCommandList::CopyTexture(TexturePtr src, TexturePtr dst, 
    uint32_t width, uint32_t height, 
    uint32_t srcMipLevel, uint32_t srcArrayIndex,
    uint32_t dstMipLevel, uint32_t dstArrayIndex)
{
    // テクスチャ実装後に実装
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// シェーダーステージをVkPipelineStageに変換
VkPipelineStageFlags VulkanCommandList::ToVkPipelineStage(ShaderStage stage) const
{
    VkPipelineStageFlags result = 0;
    
    if ((stage & ShaderStage::Vertex) == ShaderStage::Vertex) {
        result |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    }
    
    if ((stage & ShaderStage::Hull) == ShaderStage::Hull) {
        result |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    }
    
    if ((stage & ShaderStage::Domain) == ShaderStage::Domain) {
        result |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    }
    
    if ((stage & ShaderStage::Geometry) == ShaderStage::Geometry) {
        result |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    }
    
    if ((stage & ShaderStage::Pixel) == ShaderStage::Pixel) {
        result |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    
    if ((stage & ShaderStage::Compute) == ShaderStage::Compute) {
        result |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    }
    
    return result;
}

} // namespace NorvesLib::RHI::Vulkan
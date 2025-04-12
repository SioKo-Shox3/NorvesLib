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
    
    // フェンスの破棋
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
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    if (m_inRenderPass) {
        throw std::runtime_error("既にレンダーパス内です");
    }
    
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
    
    vkCmdEndRenderPass(m_commandBuffer);
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
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(pipeline);
    if (!vkPipeline) {
        throw std::runtime_error("無効なパイプラインです");
    }
    
    if (vkPipeline->IsCompute()) {
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vkPipeline->GetVkPipeline());
    } else {
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetVkPipeline());
    }
    
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
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkBuffer = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!vkBuffer) {
        throw std::runtime_error("無効なバッファです");
    }
    
    // パイプラインがバインドされていない場合はエラー
    if (!m_currentPipeline) {
        throw std::runtime_error("パイプラインがバインドされていません");
    }
    
    // バインディングポイント用のキー
    // Vulkanの場合、セット番号とバインディング番号の組み合わせでリソースを特定する
    // 簡単のために、スロット番号をバインディング番号として使用
    uint32_t set = 0;  // セット0を使用
    uint32_t binding = slot;
    
    // パイプラインレイアウトとバインドポイントの取得
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(m_currentPipeline);
    VkPipelineLayout layout = vkPipeline->GetVkPipelineLayout();
    VkPipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // 動的オフセットを使用する場合はここで設定
    uint32_t dynamicOffset = 0;
    
    // バッファを直接バインドすることはできず、ディスクリプタセットを通じて行う必要がある
    // プッシュコンスタントを使用するか、一時的なディスクリプタセットを作成する必要がある
    // この実装では簡略化のため、コメントのみとする
    
    // 将来的な実装：一時的なディスクリプタセットを作成してバインド
    
    AddTemporaryResource(buffer);
}

// テクスチャの設定
void VulkanCommandList::SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkTexture = std::dynamic_pointer_cast<VulkanTexture>(texture);
    if (!vkTexture) {
        throw std::runtime_error("無効なテクスチャです");
    }
    
    // パイプラインがバインドされていない場合はエラー
    if (!m_currentPipeline) {
        throw std::runtime_error("パイプラインがバインドされていません");
    }
    
    // バインディングポイント用のキー
    uint32_t set = 0;  // セット0を使用
    uint32_t binding = slot;
    
    // パイプラインレイアウトとバインドポイントの取得
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(m_currentPipeline);
    VkPipelineLayout layout = vkPipeline->GetVkPipelineLayout();
    VkPipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // テクスチャを直接バインドすることはできず、ディスクリプタセットを通じて行う必要がある
    // この実装では簡略化のため、コメントのみとする
    
    // 将来的な実装：一時的なディスクリプタセットを作成してバインド
    
    AddTemporaryResource(texture);
}

// サンプラーの設定
void VulkanCommandList::SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkSampler = std::dynamic_pointer_cast<VulkanSampler>(sampler);
    if (!vkSampler) {
        throw std::runtime_error("無効なサンプラーです");
    }
    
    // パイプラインがバインドされていない場合はエラー
    if (!m_currentPipeline) {
        throw std::runtime_error("パイプラインがバインドされていません");
    }
    
    // バインディングポイント用のキー
    uint32_t set = 0;  // セット0を使用
    uint32_t binding = slot;
    
    // パイプラインレイアウトとバインドポイントの取得
    auto vkPipeline = std::dynamic_pointer_cast<VulkanPipeline>(m_currentPipeline);
    VkPipelineLayout layout = vkPipeline->GetVkPipelineLayout();
    VkPipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // サンプラーを直接バインドすることはできず、ディスクリプタセットを通じて行う必要がある
    // この実装では簡略化のため、コメントのみとする
    
    // 将来的な実装：一時的なディスクリプタセットを作成してバインド
    
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
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkSrc = std::dynamic_pointer_cast<VulkanBuffer>(src);
    auto vkDst = std::dynamic_pointer_cast<VulkanTexture>(dst);
    
    if (!vkSrc || !vkDst) {
        throw std::runtime_error("無効なバッファまたはテクスチャです");
    }
    
    // テクスチャの次元を考慮
    uint32_t effectiveWidth = (width > 0) ? width : vkDst->GetWidth();
    uint32_t effectiveHeight = (height > 0) ? height : vkDst->GetHeight();
    uint32_t depth = 1; // 3Dテクスチャの場合は調整必要
    
    // テクスチャのアスペクトマスクを取得
    VkImageAspectFlags aspectMask;
    if ((vkDst->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkDst->GetFormat() == Format::D24_UNORM_S8_UINT) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // サブリソース範囲を定義
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = mipLevel;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = arrayIndex;
    subresourceRange.layerCount = 1;
    
    // テクスチャレイアウトを転送先に変更
    vkDst->TransitionLayout(m_commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
    
    // バッファからテクスチャへのコピー
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;  // 密にパックされたデータ
    region.bufferImageHeight = 0;  // 密にパックされたデータ
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {effectiveWidth, effectiveHeight, depth};
    
    vkCmdCopyBufferToImage(
        m_commandBuffer,
        vkSrc->GetVkBuffer(),
        vkDst->GetVkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    
    // テクスチャを適切なレイアウトに戻す
    VkImageLayout finalLayout;
    if ((vkDst->GetUsage() & ResourceUsage::ShaderResource) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::RenderTarget) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::UnorderedAccess) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    
    vkDst->TransitionLayout(m_commandBuffer, finalLayout, subresourceRange);
    
    // 一時的なリソースへの参照を保持
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// テクスチャからバッファへのコピー
void VulkanCommandList::CopyTextureToBuffer(TexturePtr src, BufferPtr dst, 
    uint32_t width, uint32_t height, uint64_t bufferOffset, 
    uint32_t mipLevel, uint32_t arrayIndex)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkSrc = std::dynamic_pointer_cast<VulkanTexture>(src);
    auto vkDst = std::dynamic_pointer_cast<VulkanBuffer>(dst);
    
    if (!vkSrc || !vkDst) {
        throw std::runtime_error("無効なテクスチャまたはバッファです");
    }
    
    // テクスチャの次元を考慮
    uint32_t effectiveWidth = (width > 0) ? width : vkSrc->GetWidth();
    uint32_t effectiveHeight = (height > 0) ? height : vkSrc->GetHeight();
    uint32_t depth = 1; // 3Dテクスチャの場合は調整必要
    
    // テクスチャのアスペクトマスクを取得
    VkImageAspectFlags aspectMask;
    if ((vkSrc->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkSrc->GetFormat() == Format::D24_UNORM_S8_UINT) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // サブリソース範囲を定義
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = mipLevel;
    subresourceRange.levelCount = 1;
    subresourceRange.baseArrayLayer = arrayIndex;
    subresourceRange.layerCount = 1;
    
    // テクスチャレイアウトを転送元に変更
    vkSrc->TransitionLayout(m_commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);
    
    // テクスチャからバッファへのコピー
    VkBufferImageCopy region{};
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;  // 密にパックされたデータ
    region.bufferImageHeight = 0;  // 密にパックされたデータ
    region.imageSubresource.aspectMask = aspectMask;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {effectiveWidth, effectiveHeight, depth};
    
    vkCmdCopyImageToBuffer(
        m_commandBuffer,
        vkSrc->GetVkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkDst->GetVkBuffer(),
        1,
        &region
    );
    
    // テクスチャを適切なレイアウトに戻す
    VkImageLayout finalLayout;
    if ((vkSrc->GetUsage() & ResourceUsage::ShaderResource) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::RenderTarget) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::UnorderedAccess) != ResourceUsage::None) {
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        finalLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    
    vkSrc->TransitionLayout(m_commandBuffer, finalLayout, subresourceRange);
    
    // 一時的なリソースへの参照を保持
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// テクスチャからテクスチャへのコピー
void VulkanCommandList::CopyTexture(TexturePtr src, TexturePtr dst, 
    uint32_t width, uint32_t height, 
    uint32_t srcMipLevel, uint32_t srcArrayIndex,
    uint32_t dstMipLevel, uint32_t dstArrayIndex)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkSrc = std::dynamic_pointer_cast<VulkanTexture>(src);
    auto vkDst = std::dynamic_pointer_cast<VulkanTexture>(dst);
    
    if (!vkSrc || !vkDst) {
        throw std::runtime_error("無効なテクスチャです");
    }
    
    // コピー領域の寸法を決定
    uint32_t effectiveWidth = (width > 0) ? width : std::min(vkSrc->GetWidth() >> srcMipLevel, vkDst->GetWidth() >> dstMipLevel);
    uint32_t effectiveHeight = (height > 0) ? height : std::min(vkSrc->GetHeight() >> srcMipLevel, vkDst->GetHeight() >> dstMipLevel);
    uint32_t depth = 1; // 2Dテクスチャの場合
    
    // ソーステクスチャのアスペクトマスクを設定
    VkImageAspectFlags srcAspectMask;
    if ((vkSrc->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        srcAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkSrc->GetFormat() == Format::D24_UNORM_S8_UINT) {
            srcAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        srcAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // デスティネーションテクスチャのアスペクトマスクを設定
    VkImageAspectFlags dstAspectMask;
    if ((vkDst->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        dstAspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkDst->GetFormat() == Format::D24_UNORM_S8_UINT) {
            dstAspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        dstAspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // ソースのサブリソース範囲を定義
    VkImageSubresourceRange srcSubresourceRange{};
    srcSubresourceRange.aspectMask = srcAspectMask;
    srcSubresourceRange.baseMipLevel = srcMipLevel;
    srcSubresourceRange.levelCount = 1;
    srcSubresourceRange.baseArrayLayer = srcArrayIndex;
    srcSubresourceRange.layerCount = 1;
    
    // デスティネーションのサブリソース範囲を定義
    VkImageSubresourceRange dstSubresourceRange{};
    dstSubresourceRange.aspectMask = dstAspectMask;
    dstSubresourceRange.baseMipLevel = dstMipLevel;
    dstSubresourceRange.levelCount = 1;
    dstSubresourceRange.baseArrayLayer = dstArrayIndex;
    dstSubresourceRange.layerCount = 1;
    
    // ソースイメージをコピー元レイアウトに変更
    vkSrc->TransitionLayout(m_commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcSubresourceRange);
    
    // デスティネーションイメージをコピー先レイアウトに変更
    vkDst->TransitionLayout(m_commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, dstSubresourceRange);
    
    // コピーリージョンの設定
    VkImageCopy region{};
    region.srcSubresource.aspectMask = srcAspectMask;
    region.srcSubresource.mipLevel = srcMipLevel;
    region.srcSubresource.baseArrayLayer = srcArrayIndex;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = {0, 0, 0};
    
    region.dstSubresource.aspectMask = dstAspectMask;
    region.dstSubresource.mipLevel = dstMipLevel;
    region.dstSubresource.baseArrayLayer = dstArrayIndex;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = {0, 0, 0};
    
    region.extent = {effectiveWidth, effectiveHeight, depth};
    
    // コピーコマンドを実行
    vkCmdCopyImage(
        m_commandBuffer,
        vkSrc->GetVkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        vkDst->GetVkImage(),
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    
    // ソースとデスティネーションのイメージを元のレイアウトに戻す
    VkImageLayout srcFinalLayout;
    if ((vkSrc->GetUsage() & ResourceUsage::ShaderResource) != ResourceUsage::None) {
        srcFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::RenderTarget) != ResourceUsage::None) {
        srcFinalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        srcFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((vkSrc->GetUsage() & ResourceUsage::UnorderedAccess) != ResourceUsage::None) {
        srcFinalLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        srcFinalLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    
    VkImageLayout dstFinalLayout;
    if ((vkDst->GetUsage() & ResourceUsage::ShaderResource) != ResourceUsage::None) {
        dstFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::RenderTarget) != ResourceUsage::None) {
        dstFinalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        dstFinalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((vkDst->GetUsage() & ResourceUsage::UnorderedAccess) != ResourceUsage::None) {
        dstFinalLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else {
        dstFinalLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    
    vkSrc->TransitionLayout(m_commandBuffer, srcFinalLayout, srcSubresourceRange);
    vkDst->TransitionLayout(m_commandBuffer, dstFinalLayout, dstSubresourceRange);
    
    // リソース参照を保持
    AddTemporaryResource(src);
    AddTemporaryResource(dst);
}

// バッファバリアの設定
void VulkanCommandList::BufferBarrier(BufferPtr buffer, ResourceState beforeState, ResourceState afterState, 
                                     uint64_t offset, uint64_t size)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkBuffer = std::dynamic_pointer_cast<VulkanBuffer>(buffer);
    if (!vkBuffer) {
        throw std::runtime_error("無効なバッファです");
    }
    
    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.buffer = vkBuffer->GetVkBuffer();
    barrier.offset = offset;
    barrier.size = (size == 0) ? VK_WHOLE_SIZE : size;
    
    // ソースアクセスマスク
    if ((beforeState & ResourceState::VertexBuffer) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if ((beforeState & ResourceState::IndexBuffer) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_INDEX_READ_BIT;
    }
    if ((beforeState & ResourceState::ConstantBuffer) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_UNIFORM_READ_BIT;
    }
    if ((beforeState & ResourceState::UnorderedAccess) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if ((beforeState & ResourceState::IndirectArgument) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    if ((beforeState & ResourceState::CopyDest) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if ((beforeState & ResourceState::CopySource) != ResourceState::None) {
        barrier.srcAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    
    // デスティネーションアクセスマスク
    if ((afterState & ResourceState::VertexBuffer) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    }
    if ((afterState & ResourceState::IndexBuffer) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_INDEX_READ_BIT;
    }
    if ((afterState & ResourceState::ConstantBuffer) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_UNIFORM_READ_BIT;
    }
    if ((afterState & ResourceState::UnorderedAccess) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    if ((afterState & ResourceState::IndirectArgument) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    }
    if ((afterState & ResourceState::CopyDest) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if ((afterState & ResourceState::CopySource) != ResourceState::None) {
        barrier.dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    
    // キューファミリーインデックス - 同じキューを使用する場合は無視される
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    
    // パイプラインステージ - ワーストケースシナリオを使用
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    
    // パイプラインバリアコマンドを発行
    vkCmdPipelineBarrier(
        m_commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        1, &barrier,
        0, nullptr
    );
    
    AddTemporaryResource(buffer);
}

// テクスチャバリアの設定
void VulkanCommandList::TextureBarrier(TexturePtr texture, ResourceState beforeState, ResourceState afterState,
                                      uint32_t mipLevel, uint32_t arrayIndex, uint32_t mipCount, uint32_t arrayCount)
{
    if (!m_isRecording) {
        throw std::runtime_error("コマンドリストは記録中ではありません");
    }
    
    auto vkTexture = std::dynamic_pointer_cast<VulkanTexture>(texture);
    if (!vkTexture) {
        throw std::runtime_error("無効なテクスチャです");
    }
    
    // アスペクトマスクの決定
    VkImageAspectFlags aspectMask;
    if ((vkTexture->GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (vkTexture->GetFormat() == Format::D24_UNORM_S8_UINT) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    } else {
        aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }
    
    // サブリソース範囲の設定
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.baseMipLevel = mipLevel;
    subresourceRange.levelCount = (mipCount == 0) ? vkTexture->GetMipLevels() - mipLevel : mipCount;
    subresourceRange.baseArrayLayer = arrayIndex;
    subresourceRange.layerCount = (arrayCount == 0) ? vkTexture->GetArraySize() - arrayIndex : arrayCount;
    
    // イメージレイアウトの決定
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    // BeforeStateに基づくレイアウト設定
    if ((beforeState & ResourceState::RenderTarget) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((beforeState & ResourceState::DepthWrite) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((beforeState & ResourceState::DepthRead) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    } else if ((beforeState & ResourceState::UnorderedAccess) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else if ((beforeState & ResourceState::ShaderResource) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((beforeState & ResourceState::CopyDest) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    } else if ((beforeState & ResourceState::CopySource) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else if ((beforeState & ResourceState::Present) != ResourceState::None) {
        oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    
    // AfterStateに基づくレイアウト設定
    if ((afterState & ResourceState::RenderTarget) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    } else if ((afterState & ResourceState::DepthWrite) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    } else if ((afterState & ResourceState::DepthRead) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    } else if ((afterState & ResourceState::UnorderedAccess) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_GENERAL;
    } else if ((afterState & ResourceState::ShaderResource) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    } else if ((afterState & ResourceState::CopyDest) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    } else if ((afterState & ResourceState::CopySource) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else if ((afterState & ResourceState::Present) != ResourceState::None) {
        newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    
    // 現在と同じレイアウトの場合は何もしない
    if (oldLayout == newLayout) {
        return;
    }

    // アクセスマスクの設定
    VkAccessFlags srcAccessMask = 0;
    VkAccessFlags dstAccessMask = 0;
    
    // ソースアクセスマスク
    if ((beforeState & ResourceState::RenderTarget) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if ((beforeState & ResourceState::DepthWrite) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if ((beforeState & ResourceState::DepthRead) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if ((beforeState & ResourceState::UnorderedAccess) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    if ((beforeState & ResourceState::ShaderResource) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ((beforeState & ResourceState::CopyDest) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if ((beforeState & ResourceState::CopySource) != ResourceState::None) {
        srcAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    
    // デスティネーションアクセスマスク
    if ((afterState & ResourceState::RenderTarget) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    }
    if ((afterState & ResourceState::DepthWrite) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }
    if ((afterState & ResourceState::DepthRead) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }
    if ((afterState & ResourceState::UnorderedAccess) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_SHADER_WRITE_BIT;
    }
    if ((afterState & ResourceState::ShaderResource) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_SHADER_READ_BIT;
    }
    if ((afterState & ResourceState::CopyDest) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    if ((afterState & ResourceState::CopySource) != ResourceState::None) {
        dstAccessMask |= VK_ACCESS_TRANSFER_READ_BIT;
    }
    
    // パイプラインステージの決定
    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    
    // より最適化されたステージマスクを設定することも可能
    if ((beforeState & ResourceState::RenderTarget) != ResourceState::None) {
        srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (((beforeState & ResourceState::DepthWrite) != ResourceState::None) || 
               ((beforeState & ResourceState::DepthRead) != ResourceState::None)) {
        srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    }
    
    if ((afterState & ResourceState::RenderTarget) != ResourceState::None) {
        dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (((afterState & ResourceState::DepthWrite) != ResourceState::None) || 
               ((afterState & ResourceState::DepthRead) != ResourceState::None)) {
        dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    
    // イメージメモリバリア
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vkTexture->GetVkImage();
    barrier.subresourceRange = subresourceRange;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    
    // パイプラインバリアの発行
    vkCmdPipelineBarrier(
        m_commandBuffer,
        srcStageMask,
        dstStageMask,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    
    // テクスチャの現在のレイアウトを更新
    vkTexture->SetVkImageLayout(newLayout);
    
    AddTemporaryResource(texture);
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
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
    
    // ディスクリプタプールの作成
    CreateDescriptorPool();
}

// デストラクタ
VulkanCommandList::~VulkanCommandList()
{
    // 一時リソースをクリア
    m_temporaryResources.clear();
    
    // ディスクリプタプールの破棄
    DestroyDescriptorPool();
    
    // フェンスの破棋
    if (m_fence != VK_NULL_HANDLE) {
        vkDestroyFence(m_device->GetVkDevice(), m_fence, nullptr);
    }
    
    // コマンドバッファの解放（コマンドプールごと破棄される場合があるので省略可能）
    if (m_commandBuffer != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(m_device->GetVkDevice(), m_device->GetCommandPool(), 1, &m_commandBuffer);
    }
    
    // ディスクリプタプールが作成されていれば破棄
    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device->GetVkDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
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
    
    // ディスクリプタセットキャッシュをリセット
    // プールは再利用するが、各セットは再作成する
    m_descriptorSetCache.clear();
    
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

// ディスクリプタプールの作成
void VulkanCommandList::CreateDescriptorPool()
{
    // 各タイプのディスクリプタの数を定義
    std::array<VkDescriptorPoolSize, 4> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[3].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // ディスクリプタプールの作成情報を設定
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_DESCRIPTOR_SETS;
    
    // ディスクリプタプールを作成
    if (vkCreateDescriptorPool(m_device->GetVkDevice(), &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("ディスクリプタプールの作成に失敗しました");
    }
}

// ディスクリプタプールの破棄
void VulkanCommandList::DestroyDescriptorPool()
{
    if (m_descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(m_device->GetVkDevice(), m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

// シェーダーステージをVkシェーダーステージフラグに変換
VkShaderStageFlags VulkanCommandList::ToVkShaderStageFlags(ShaderStage stage) const
{
    VkShaderStageFlags result = 0;
    
    if ((stage & ShaderStage::Vertex) == ShaderStage::Vertex) {
        result |= VK_SHADER_STAGE_VERTEX_BIT;
    }
    
    if ((stage & ShaderStage::Hull) == ShaderStage::Hull) {
        result |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    }
    
    if ((stage & ShaderStage::Domain) == ShaderStage::Domain) {
        result |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    }
    
    if ((stage & ShaderStage::Geometry) == ShaderStage::Geometry) {
        result |= VK_SHADER_STAGE_GEOMETRY_BIT;
    }
    
    if ((stage & ShaderStage::Pixel) == ShaderStage::Pixel) {
        result |= VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    
    if ((stage & ShaderStage::Compute) == ShaderStage::Compute) {
        result |= VK_SHADER_STAGE_COMPUTE_BIT;
    }
    
    return result;
}

// ディスクリプタセットの取得または作成
VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(uint32_t setIndex, VkDescriptorSetLayout layout)
{
    auto& setInfo = m_descriptorSetCache[setIndex];
    
    // すでに同じレイアウトのディスクリプタセットが存在する場合は再利用
    if (setInfo.descriptorSet != VK_NULL_HANDLE && setInfo.layout == layout)
    {
        // ダーティフラグが立っている場合は更新
        if (setInfo.isDirty)
        {
            UpdateDescriptorSet(setIndex);
        }
        return setInfo.descriptorSet;
    }
    
    // 以前のディスクリプタセットがある場合は解放
    if (setInfo.descriptorSet != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(m_device->GetVkDevice(), m_descriptorPool, 1, &setInfo.descriptorSet);
        setInfo.descriptorSet = VK_NULL_HANDLE;
    }
    
    // 新しいディスクリプタセットを割り当て
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    if (vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &setInfo.descriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("ディスクリプタセットの割り当てに失敗しました");
    }
    
    setInfo.layout = layout;
    
    // 新しいディスクリプタセットに登録されたリソースを更新
    if (!setInfo.resources.empty())
    {
        UpdateDescriptorSet(setIndex);
    }
    
    return setInfo.descriptorSet;
}

// ディスクリプタセットの更新
void VulkanCommandList::UpdateDescriptorSet(uint32_t setIndex)
{
    auto& setInfo = m_descriptorSetCache[setIndex];
    if (!setInfo.isDirty || setInfo.descriptorSet == VK_NULL_HANDLE)
    {
        return;
    }
    
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    // リソースごとに適切な書き込み情報を作成
    for (const auto& [key, resourceInfo] : setInfo.resources)
    {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = setInfo.descriptorSet;
        write.dstBinding = key.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        
        switch (resourceInfo.type)
        {
            case BindingResourceInfo::Type::Buffer:
            {
                auto buffer = std::static_pointer_cast<VulkanBuffer>(resourceInfo.resource);
                
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = buffer->GetVkBuffer();
                bufferInfo.offset = resourceInfo.offset;
                bufferInfo.range = resourceInfo.range;
                
                bufferInfos.push_back(bufferInfo);
                
                write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                write.pBufferInfo = &bufferInfos.back();
                break;
            }
            case BindingResourceInfo::Type::Texture:
            {
                auto texture = std::static_pointer_cast<VulkanTexture>(resourceInfo.resource);
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = texture->GetVkImageView();
                imageInfo.sampler = VK_NULL_HANDLE; // テクスチャのみの場合はNULL
                
                imageInfos.push_back(imageInfo);
                
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                write.pImageInfo = &imageInfos.back();
                break;
            }
            case BindingResourceInfo::Type::Sampler:
            {
                auto sampler = std::static_pointer_cast<VulkanSampler>(resourceInfo.resource);
                
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.imageView = VK_NULL_HANDLE;
                imageInfo.sampler = sampler->GetVkSampler();
                
                imageInfos.push_back(imageInfo);
                
                write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                write.pImageInfo = &imageInfos.back();
                break;
            }
        }
        
        descriptorWrites.push_back(write);
    }
    
    // ディスクリプタセットを更新
    if (!descriptorWrites.empty())
    {
        vkUpdateDescriptorSets(
            m_device->GetVkDevice(),
            static_cast<uint32_t>(descriptorWrites.size()),
            descriptorWrites.data(),
            0, nullptr);
    }
    
    // 更新完了したのでダーティフラグをクリア
    setInfo.isDirty = false;
}

// 全てのディスクリプタセットをバインド
void VulkanCommandList::BindDescriptorSets()
{
    if (!m_currentPipeline) {
        return; // パイプラインがバインドされていない
    }
    
    auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(m_currentPipeline);
    VkPipelineLayout layout = vkPipeline->GetVkPipelineLayout();
    VkPipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // 各ディスクリプタセットについて更新してバインド
    for (auto& [setIndex, setInfo] : m_descriptorSetCache) {
        // 必要に応じてディスクリプタセットを更新
        UpdateDescriptorSet(setIndex);
        
        // ディスクリプタセットをバインド
        if (setInfo.descriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(
                m_commandBuffer,
                bindPoint,
                layout,
                setIndex,  // セットインデックス
                1,         // セット数
                &setInfo.descriptorSet,
                0,         // 動的オフセット数
                nullptr    // 動的オフセット
            );
        }
    }
}

// コマンドリスト初期化時にディスクリプタプールを作成
bool VulkanCommandList::Init()
{
    // コマンドバッファの作成
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(m_device->GetVkDevice(), &allocInfo, &m_commandBuffer) != VK_SUCCESS)
    {
        return false;
    }

    // ディスクリプタプールを作成
    CreateDescriptorPool();

    return true;
}

// 定数バッファをセットする
void VulkanCommandList::SetConstantBuffer(uint32_t setIndex, uint32_t binding, std::shared_ptr<IBuffer> buffer, uint64_t offset, uint64_t range)
{
    if (!buffer)
    {
        return;
    }

    auto vkBuffer = std::static_pointer_cast<VulkanBuffer>(buffer);
    BindingKey key{ setIndex, binding };
    
    auto& setInfo = m_descriptorSetCache[setIndex];
    auto& resourceInfo = setInfo.resources[key];
    
    resourceInfo.type = BindingResourceInfo::Type::Buffer;
    resourceInfo.resource = buffer;
    resourceInfo.offset = offset;
    resourceInfo.range = (range == 0) ? vkBuffer->GetSize() - offset : range;
    
    // リソースが変更されたのでディスクリプタセットは更新が必要
    setInfo.isDirty = true;

    // シェーダーリソースをバインドするには、ディスクリプタセットが必要なので、
    // 現在のパイプラインからディスクリプタセットレイアウトを取得
    if (m_currentPipeline)
    {
        auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(m_currentPipeline);
        VkDescriptorSetLayout layout = vkPipeline->GetDescriptorSetLayout(setIndex);
        if (layout != VK_NULL_HANDLE)
        {
            // ディスクリプタセットを取得または作成
            GetOrCreateDescriptorSet(setIndex, layout);
        }
    }
}

// テクスチャをセットする
void VulkanCommandList::SetTexture(uint32_t setIndex, uint32_t binding, std::shared_ptr<ITexture> texture)
{
    if (!texture)
    {
        return;
    }

    auto vkTexture = std::static_pointer_cast<VulkanTexture>(texture);
    BindingKey key{ setIndex, binding };
    
    auto& setInfo = m_descriptorSetCache[setIndex];
    auto& resourceInfo = setInfo.resources[key];
    
    resourceInfo.type = BindingResourceInfo::Type::Texture;
    resourceInfo.resource = texture;
    resourceInfo.offset = 0;
    resourceInfo.range = 0;
    
    // リソースが変更されたのでディスクリプタセットは更新が必要
    setInfo.isDirty = true;

    // 現在のパイプラインからディスクリプタセットレイアウトを取得
    if (m_currentPipeline)
    {
        auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(m_currentPipeline);
        VkDescriptorSetLayout layout = vkPipeline->GetDescriptorSetLayout(setIndex);
        if (layout != VK_NULL_HANDLE)
        {
            // ディスクリプタセットを取得または作成
            GetOrCreateDescriptorSet(setIndex, layout);
        }
    }
}

// サンプラーをセットする
void VulkanCommandList::SetSampler(uint32_t setIndex, uint32_t binding, std::shared_ptr<ISampler> sampler)
{
    if (!sampler)
    {
        return;
    }

    auto vkSampler = std::static_pointer_cast<VulkanSampler>(sampler);
    BindingKey key{ setIndex, binding };
    
    auto& setInfo = m_descriptorSetCache[setIndex];
    auto& resourceInfo = setInfo.resources[key];
    
    resourceInfo.type = BindingResourceInfo::Type::Sampler;
    resourceInfo.resource = sampler;
    resourceInfo.offset = 0;
    resourceInfo.range = 0;
    
    // リソースが変更されたのでディスクリプタセットは更新が必要
    setInfo.isDirty = true;

    // 現在のパイプラインからディスクリプタセットレイアウトを取得
    if (m_currentPipeline)
    {
        auto vkPipeline = std::static_pointer_cast<VulkanPipeline>(m_currentPipeline);
        VkDescriptorSetLayout layout = vkPipeline->GetDescriptorSetLayout(setIndex);
        if (layout != VK_NULL_HANDLE)
        {
            // ディスクリプタセットを取得または作成
            GetOrCreateDescriptorSet(setIndex, layout);
        }
    }
}

// パイプラインレイアウトのバインドポイントに対応するディスクリプタセットを取得または作成
VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(uint32_t bindPoint)
{
    // 既にこのバインドポイント用のディスクリプタセットが存在するか確認
    auto it = m_descriptorSets.find(bindPoint);
    if (it != m_descriptorSets.end()) {
        return it->second;
    }

    // 現在のパイプラインからディスクリプタセットレイアウトを取得
    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    if (!vulkanPipeline) {
        throw std::runtime_error("パイプラインがバインドされていません");
    }

    VkDescriptorSetLayout layout = vulkanPipeline->GetDescriptorSetLayout(bindPoint);
    if (layout == VK_NULL_HANDLE) {
        throw std::runtime_error("指定されたバインドポイントのディスクリプタセットレイアウトが見つかりません");
    }

    // 新しいディスクリプタセットの割り当て
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("ディスクリプタセットの割り当てに失敗しました");
    }

    // マップに保存
    m_descriptorSets[bindPoint] = descriptorSet;
    return descriptorSet;
}

// ディスクリプタセットを更新
void VulkanCommandList::UpdateDescriptorSet()
{
    if (m_pendingWrites.empty()) {
        return;
    }

    // 保留中の書き込みを処理
    vkUpdateDescriptorSets(
        m_device->GetVkDevice(),
        static_cast<uint32_t>(m_pendingWrites.size()),
        m_pendingWrites.data(),
        0,
        nullptr
    );

    // 保留中の書き込みをクリア
    m_pendingWrites.clear();
}

// ディスクリプタセットをパイプラインにバインド
void VulkanCommandList::BindDescriptorSets()
{
    if (m_descriptorSets.empty() || !m_currentPipeline) {
        return;
    }

    // 保留中の更新を適用
    UpdateDescriptorSet();

    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    VkPipelineLayout pipelineLayout = vulkanPipeline->GetVkPipelineLayout();

    // バインドポイントに基づいてディスクリプタセットをバインド
    for (const auto& [bindPoint, descriptorSet] : m_descriptorSets) {
        vkCmdBindDescriptorSets(
            m_commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS, // または COMPUTE
            pipelineLayout,
            bindPoint,
            1,
            &descriptorSet,
            0,
            nullptr
        );
    }
}

void VulkanCommandList::SetConstantBuffer(uint32_t bindPoint, uint32_t binding, const std::shared_ptr<IBuffer>& buffer)
{
    if (!buffer) {
        return;
    }

    VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(buffer.get());
    VkBuffer vkBuffer = vulkanBuffer->GetVkBuffer();
    VkDeviceSize offset = 0;
    VkDeviceSize range = vulkanBuffer->GetSize();

    // 対応するバインドポイントのディスクリプタセットを取得または作成
    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(bindPoint);

    // バッファの記述子更新情報を作成
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = vkBuffer;
    bufferInfo.offset = offset;
    bufferInfo.range = range;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // または STORAGE_BUFFER
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfo;

    // 後で一括して更新するために記述子更新情報を保存
    m_pendingWrites.push_back(write);
    
    // バッファへの参照を保持（ディスクリプタが有効である間）
    m_boundResources.push_back(buffer);
}

void VulkanCommandList::SetTexture(uint32_t bindPoint, uint32_t binding, const std::shared_ptr<ITexture>& texture)
{
    if (!texture) {
        return;
    }
    
    VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(texture.get());
    VkImageView imageView = vulkanTexture->GetVkImageView();
    
    // 対応するバインドポイントのディスクリプタセットを取得または作成
    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(bindPoint);
    
    // テクスチャの記述子更新情報を作成
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // テクスチャのレイアウト
    imageInfo.imageView = imageView;
    imageInfo.sampler = VK_NULL_HANDLE; // テクスチャとサンプラーは別々に設定
    
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    
    // 後で一括して更新するために記述子更新情報を保存
    m_pendingWrites.push_back(write);
    
    // テクスチャへの参照を保持
    m_boundResources.push_back(texture);
}

void VulkanCommandList::SetSampler(uint32_t bindPoint, uint32_t binding, const std::shared_ptr<ISampler>& sampler)
{
    if (!sampler) {
        return;
    }
    
    VulkanSampler* vulkanSampler = static_cast<VulkanSampler*>(sampler.get());
    VkSampler vkSampler = vulkanSampler->GetVkSampler();
    
    // 対応するバインドポイントのディスクリプタセットを取得または作成
    VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(bindPoint);
    
    // サンプラーの記述子更新情報を作成
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = vkSampler;
    samplerInfo.imageView = VK_NULL_HANDLE; // サンプラーのみの設定
    samplerInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &samplerInfo;
    
    // 後で一括して更新するために記述子更新情報を保存
    m_pendingWrites.push_back(write);
    
    // サンプラーへの参照を保持
    m_boundResources.push_back(sampler);
}

VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(uint32_t bindPoint)
{
    // 指定されたバインドポイントのディスクリプタセット情報を検索
    auto it = m_descriptorSetInfos.find(bindPoint);
    
    if (it != m_descriptorSetInfos.end()) {
        // 既にディスクリプタセットが存在する場合はそれを返す
        return it->second.descriptorSet;
    }
    
    // 新しいディスクリプタセット情報を作成
    DescriptorSetInfo setInfo{};
    
    // 現在バインドされているパイプラインからレイアウトを取得
    if (!m_currentPipeline) {
        // エラー処理: パイプラインがバインドされていない
        return VK_NULL_HANDLE;
    }
    
    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    VkDescriptorSetLayout layout = vulkanPipeline->GetDescriptorSetLayout(bindPoint);
    
    if (layout == VK_NULL_HANDLE) {
        // 指定されたバインドポイントに対応するレイアウトが存在しない
        return VK_NULL_HANDLE;
    }
    
    // ディスクリプタセットの割り当て
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_device->GetDescriptorPool();
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    
    VkDescriptorSet descriptorSet;
    VkResult result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
    
    if (result != VK_SUCCESS) {
        // ディスクリプタセットの割り当てに失敗
        return VK_NULL_HANDLE;
    }
    
    // 作成したディスクリプタセット情報を保存
    setInfo.descriptorSet = descriptorSet;
    setInfo.layout = layout;
    m_descriptorSetInfos[bindPoint] = setInfo;
    
    return descriptorSet;
}

void VulkanCommandList::UpdateDescriptorSets()
{
    if (m_pendingWrites.empty()) {
        return;
    }
    
    // 蓄積された記述子更新情報を一括で適用
    vkUpdateDescriptorSets(m_device->GetVkDevice(), 
                          static_cast<uint32_t>(m_pendingWrites.size()), 
                          m_pendingWrites.data(), 
                          0, nullptr);
    
    // 更新後はリストをクリア
    m_pendingWrites.clear();
}

void VulkanCommandList::BindDescriptorSets()
{
    // 記述子セットの更新を確実に適用
    UpdateDescriptorSets();
    
    if (m_descriptorSetInfos.empty() || !m_currentPipeline) {
        return;
    }
    
    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    VkPipelineBindPoint bindPoint = vulkanPipeline->GetVkPipelineBindPoint();
    VkPipelineLayout pipelineLayout = vulkanPipeline->GetVkPipelineLayout();
    
    // 各バインドポイントのディスクリプタセットをバインド
    for (const auto& [setIndex, setInfo] : m_descriptorSetInfos) {
        vkCmdBindDescriptorSets(
            m_commandBuffer,
                               bindPoint,
                               pipelineLayout,
                               setIndex,
                               1,
                               &setInfo.descriptorSet,
                               0,
                               nullptr);
    }
}

// ディスクリプタセットの取得または作成
VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(const ShaderBindingKey& key, VulkanPipeline* pipeline)
{
    // すでに作成済みのディスクリプタセットを検索
    auto it = m_descriptorSetCache.find(key);
    if (it != m_descriptorSetCache.end()) {
        return it->second.descriptorSet;
    }

    // 新しいディスクリプタセットを作成
    VkDescriptorSetLayout layout = pipeline->GetDescriptorSetLayout(key.setIndex);
    if (layout == VK_NULL_HANDLE) {
        // このセットインデックスに対するレイアウトが存在しない場合
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
    
    if (result != VK_SUCCESS) {
        // 既存のプールがいっぱいの場合は、新しいプールを作成して再試行
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            CreateDescriptorPool();
            allocInfo.descriptorPool = m_descriptorPool;
            result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
            
            if (result != VK_SUCCESS) {
                // それでも失敗する場合はエラー
                return VK_NULL_HANDLE;
            }
        }
        else {
            // その他のエラー
            return VK_NULL_HANDLE;
        }
    }

    // キャッシュに保存
    DescriptorSetInfo setInfo;
    setInfo.descriptorSet = descriptorSet;
    setInfo.layout = layout;
    m_descriptorSetCache[key] = setInfo;

    // 作成したディスクリプタセットを更新
    UpdateDescriptorSet(key, descriptorSet);

    return descriptorSet;
}

// ディスクリプタセットの更新
void VulkanCommandList::UpdateDescriptorSet(const ShaderBindingKey& key, VkDescriptorSet descriptorSet, const std::unordered_map<uint32_t, BindingResourceInfo>& resources)
{
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    // 各リソースについて記述子の書き込みを準備
    for (const auto& [binding, resourceInfo] : resources) {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = binding;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorCount = 1;
        
        switch (resourceInfo.type) {
            case BindingResourceType::UniformBuffer:
            case BindingResourceType::StorageBuffer: {
                VkDescriptorBufferInfo bufferInfo{};
                VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(resourceInfo.resource);
                bufferInfo.buffer = vulkanBuffer->GetVkBuffer();
                bufferInfo.offset = resourceInfo.offset;
                bufferInfo.range = resourceInfo.size;
                
                bufferInfos.push_back(bufferInfo);
                
                writeDescriptorSet.descriptorType = 
                    resourceInfo.type == BindingResourceType::UniformBuffer ? 
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : 
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writeDescriptorSet.pBufferInfo = &bufferInfos.back();
                break;
            }
            case BindingResourceType::Texture: {
                VkDescriptorImageInfo imageInfo{};
                VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(resourceInfo.resource);
                imageInfo.imageView = vulkanTexture->GetImageView();
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.sampler = VK_NULL_HANDLE; // サンプラーは別に設定
                
                imageInfos.push_back(imageInfo);
                
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writeDescriptorSet.pImageInfo = &imageInfos.back();
                break;
            }
            case BindingResourceType::Sampler: {
                VkDescriptorImageInfo imageInfo{};
                VulkanSampler* vulkanSampler = static_cast<VulkanSampler*>(resourceInfo.resource);
                imageInfo.sampler = vulkanSampler->GetVkSampler();
                imageInfo.imageView = VK_NULL_HANDLE;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                
                imageInfos.push_back(imageInfo);
                
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                writeDescriptorSet.pImageInfo = &imageInfos.back();
                break;
            }
        }
        
        descriptorWrites.push_back(writeDescriptorSet);
    }
    
    // ディスクリプタセットを更新
    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(
            m_device->GetVkDevice(), 
            static_cast<uint32_t>(descriptorWrites.size()), 
            descriptorWrites.data(), 
            0, nullptr
        );
    }
}

// ディスクリプタセットの更新
void VulkanCommandList::UpdateDescriptorSet(const ShaderBindingKey& key, VkDescriptorSet descriptorSet)
{
    auto resourcesIt = m_bindingResources.find(key);
    if (resourcesIt == m_bindingResources.end()) {
        // このキーに対するリソースがない場合は何もしない
        return;
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;

    // バインドされたリソースを使ってディスクリプタの書き込み情報を準備する
    for (const auto& [bindingIndex, resourceInfo] : resourcesIt->second) {
        VkWriteDescriptorSet writeInfo{};
        writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeInfo.dstSet = descriptorSet;
        writeInfo.dstBinding = bindingIndex;
        writeInfo.dstArrayElement = 0;
        writeInfo.descriptorCount = 1;

        switch (resourceInfo.type) {
        case BindingResourceInfo::ResourceType::UniformBuffer:
        case BindingResourceInfo::ResourceType::StorageBuffer:
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = reinterpret_cast<VulkanBuffer*>(resourceInfo.resource)->GetVkBuffer();
                bufferInfo.offset = resourceInfo.offset;
                bufferInfo.range = resourceInfo.size == 0 ? VK_WHOLE_SIZE : resourceInfo.size;

                bufferInfos.push_back(bufferInfo);
                writeInfo.pBufferInfo = &bufferInfos.back();
                writeInfo.descriptorType = resourceInfo.type == BindingResourceInfo::ResourceType::UniformBuffer ?
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        case BindingResourceInfo::ResourceType::Texture:
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = reinterpret_cast<VulkanTexture*>(resourceInfo.resource)->GetVkImageView();
                imageInfo.sampler = VK_NULL_HANDLE;

                imageInfos.push_back(imageInfo);
                writeInfo.pImageInfo = &imageInfos.back();
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        case BindingResourceInfo::ResourceType::Sampler:
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.imageView = VK_NULL_HANDLE;
                imageInfo.sampler = reinterpret_cast<VulkanSampler*>(resourceInfo.resource)->GetVkSampler();

                imageInfos.push_back(imageInfo);
                writeInfo.pImageInfo = &imageInfos.back();
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        default:
            // サポートされていないリソースタイプ
            break;
        }
    }

    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(m_device->GetVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void VulkanCommandList::BindDescriptorSets(VulkanPipeline* pipeline)
{
    if (!pipeline) {
        return;
    }

    VkPipelineBindPoint bindPoint = pipeline->GetPipelineType() == EPipelineType::Graphics 
        ? VK_PIPELINE_BIND_POINT_GRAPHICS 
        : VK_PIPELINE_BIND_POINT_COMPUTE;

    VkPipelineLayout pipelineLayout = pipeline->GetVkPipelineLayout();
    
    // ディスクリプタセットが存在するセットインデックスを収集
    std::map<uint32_t, VkDescriptorSet> sortedDescriptorSets;
    for (const auto& entry : m_boundResources) {
        const ShaderBindingKey& key = entry.first;
        auto it = m_descriptorSetCache.find(key);
        if (it != m_descriptorSetCache.end()) {
            sortedDescriptorSets[key.setIndex] = it->second.descriptorSet;
        }
    }

    if (sortedDescriptorSets.empty()) {
        return;
    }

    // バインドするディスクリプタセットを準備
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<uint32_t> setIndices;

    for (const auto& entry : sortedDescriptorSets) {
        setIndices.push_back(entry.first);
        descriptorSets.push_back(entry.second);
    }

    // 動的オフセット情報を収集（バッファ用）
    std::vector<uint32_t> dynamicOffsets;
    for (const auto& setIndex : setIndices) {
        for (const auto& entry : m_boundResources) {
            if (entry.first.setIndex == setIndex && entry.second.type == EBindingResourceType::Buffer) {
                if (entry.second.buffer.dynamicOffset != 0) {
                    dynamicOffsets.push_back(entry.second.buffer.dynamicOffset);
                }
            }
        }
    }

    // ディスクリプタセットをコマンドバッファにバインド
    if (!descriptorSets.empty()) {
        vkCmdBindDescriptorSets(
            m_commandBuffer,
            bindPoint,
            pipelineLayout,
            setIndices[0], // 最初のセットインデックス
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            static_cast<uint32_t>(dynamicOffsets.size()),
            dynamicOffsets.empty() ? nullptr : dynamicOffsets.data()
        );
    }
}

VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(const ShaderBindingKey& key)
{
    // キャッシュから既存のディスクリプタセットを検索
    auto it = m_descriptorSetCache.find(key);
    if (it != m_descriptorSetCache.end()) {
        return it->second.descriptorSet;
    }

    // リソースバインディング情報を取得
    VulkanShader* shader = static_cast<VulkanShader*>(key.shader);
    if (!shader) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSetLayout& layout = shader->GetDescriptorSetLayout(key.setIndex);
    if (layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // ディスクリプタプールを作成または取得
    VkDescriptorPool descriptorPool = GetOrCreateDescriptorPool();
    if (descriptorPool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // 新しいディスクリプタセットの割り当て
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);

    if (result != VK_SUCCESS) {
        // プールが満杯の場合、新しいプールを作成して再試行
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            descriptorPool = CreateDescriptorPool();
            allocInfo.descriptorPool = descriptorPool;
            result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
            if (result != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }
        else {
            return VK_NULL_HANDLE;
        }
    }

    // キャッシュに新しいディスクリプタセット情報を保存
    DescriptorSetInfo setInfo;
    setInfo.descriptorSet = descriptorSet;
    setInfo.descriptorPool = descriptorPool;
    setInfo.layout = layout;
    
    m_descriptorSetCache[key] = setInfo;
    
    return descriptorSet;
}

void VulkanCommandList::UpdateDescriptorSets()
{
    if (m_pendingWrites.empty()) {
        return;
    }
    
    // 蓄積された記述子更新情報を一括で適用
    vkUpdateDescriptorSets(m_device->GetVkDevice(), 
                          static_cast<uint32_t>(m_pendingWrites.size()), 
                          m_pendingWrites.data(), 
                          0, nullptr);
    
    // 更新後はリストをクリア
    m_pendingWrites.clear();
}

void VulkanCommandList::BindDescriptorSets()
{
    // 記述子セットの更新を確実に適用
    UpdateDescriptorSets();
    
    if (m_descriptorSetInfos.empty() || !m_currentPipeline) {
        return;
    }
    
    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    VkPipelineBindPoint bindPoint = vulkanPipeline->GetVkPipelineBindPoint();
    VkPipelineLayout pipelineLayout = vulkanPipeline->GetVkPipelineLayout();
    
    // 各バインドポイントのディスクリプタセットをバインド
    for (const auto& [setIndex, setInfo] : m_descriptorSetInfos) {
        vkCmdBindDescriptorSets(
            m_commandBuffer,
                               bindPoint,
                               pipelineLayout,
                               setIndex,
                               1,
                               &setInfo.descriptorSet,
                               0,
                               nullptr);
    }
}

// ディスクリプタセットの取得または作成
VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(const ShaderBindingKey& key, VulkanPipeline* pipeline)
{
    // すでに作成済みのディスクリプタセットを検索
    auto it = m_descriptorSetCache.find(key);
    if (it != m_descriptorSetCache.end()) {
        return it->second.descriptorSet;
    }

    // 新しいディスクリプタセットを作成
    VkDescriptorSetLayout layout = pipeline->GetDescriptorSetLayout(key.setIndex);
    if (layout == VK_NULL_HANDLE) {
        // このセットインデックスに対するレイアウトが存在しない場合
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
    
    if (result != VK_SUCCESS) {
        // 既存のプールがいっぱいの場合は、新しいプールを作成して再試行
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            CreateDescriptorPool();
            allocInfo.descriptorPool = m_descriptorPool;
            result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
            
            if (result != VK_SUCCESS) {
                // それでも失敗する場合はエラー
                return VK_NULL_HANDLE;
            }
        }
        else {
            // その他のエラー
            return VK_NULL_HANDLE;
        }
    }

    // キャッシュに保存
    DescriptorSetInfo setInfo;
    setInfo.descriptorSet = descriptorSet;
    setInfo.layout = layout;
    m_descriptorSetCache[key] = setInfo;

    // 作成したディスクリプタセットを更新
    UpdateDescriptorSet(key, descriptorSet);

    return descriptorSet;
}

// ディスクリプタセットの更新
void VulkanCommandList::UpdateDescriptorSet(const ShaderBindingKey& key, VkDescriptorSet descriptorSet, const std::unordered_map<uint32_t, BindingResourceInfo>& resources)
{
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    
    // 各リソースについて記述子の書き込みを準備
    for (const auto& [binding, resourceInfo] : resources) {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = descriptorSet;
        writeDescriptorSet.dstBinding = binding;
        writeDescriptorSet.dstArrayElement = 0;
        writeDescriptorSet.descriptorCount = 1;
        
        switch (resourceInfo.type) {
            case BindingResourceType::UniformBuffer:
            case BindingResourceType::StorageBuffer: {
                VkDescriptorBufferInfo bufferInfo{};
                VulkanBuffer* vulkanBuffer = static_cast<VulkanBuffer*>(resourceInfo.resource);
                bufferInfo.buffer = vulkanBuffer->GetVkBuffer();
                bufferInfo.offset = resourceInfo.offset;
                bufferInfo.range = resourceInfo.size;
                
                bufferInfos.push_back(bufferInfo);
                
                writeDescriptorSet.descriptorType = 
                    resourceInfo.type == BindingResourceType::UniformBuffer ? 
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : 
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writeDescriptorSet.pBufferInfo = &bufferInfos.back();
                break;
            }
            case BindingResourceType::Texture: {
                VkDescriptorImageInfo imageInfo{};
                VulkanTexture* vulkanTexture = static_cast<VulkanTexture*>(resourceInfo.resource);
                imageInfo.imageView = vulkanTexture->GetImageView();
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.sampler = VK_NULL_HANDLE; // サンプラーは別に設定
                
                imageInfos.push_back(imageInfo);
                
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                writeDescriptorSet.pImageInfo = &imageInfos.back();
                break;
            }
            case BindingResourceType::Sampler: {
                VkDescriptorImageInfo imageInfo{};
                VulkanSampler* vulkanSampler = static_cast<VulkanSampler*>(resourceInfo.resource);
                imageInfo.sampler = vulkanSampler->GetVkSampler();
                imageInfo.imageView = VK_NULL_HANDLE;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                
                imageInfos.push_back(imageInfo);
                
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                writeDescriptorSet.pImageInfo = &imageInfos.back();
                break;
            }
        }
        
        descriptorWrites.push_back(writeDescriptorSet);
    }
    
    // ディスクリプタセットを更新
    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(
            m_device->GetVkDevice(), 
            static_cast<uint32_t>(descriptorWrites.size()), 
            descriptorWrites.data(), 
            0, nullptr
        );
    }
}

// ディスクリプタセットの更新
void VulkanCommandList::UpdateDescriptorSet(const ShaderBindingKey& key, VkDescriptorSet descriptorSet)
{
    auto resourcesIt = m_bindingResources.find(key);
    if (resourcesIt == m_bindingResources.end()) {
        // このキーに対するリソースがない場合は何もしない
        return;
    }

    std::vector<VkWriteDescriptorSet> descriptorWrites;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;

    // バインドされたリソースを使ってディスクリプタの書き込み情報を準備する
    for (const auto& [bindingIndex, resourceInfo] : resourcesIt->second) {
        VkWriteDescriptorSet writeInfo{};
        writeInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeInfo.dstSet = descriptorSet;
        writeInfo.dstBinding = bindingIndex;
        writeInfo.dstArrayElement = 0;
        writeInfo.descriptorCount = 1;

        switch (resourceInfo.type) {
        case BindingResourceInfo::ResourceType::UniformBuffer:
        case BindingResourceInfo::ResourceType::StorageBuffer:
            {
                VkDescriptorBufferInfo bufferInfo{};
                bufferInfo.buffer = reinterpret_cast<VulkanBuffer*>(resourceInfo.resource)->GetVkBuffer();
                bufferInfo.offset = resourceInfo.offset;
                bufferInfo.range = resourceInfo.size == 0 ? VK_WHOLE_SIZE : resourceInfo.size;

                bufferInfos.push_back(bufferInfo);
                writeInfo.pBufferInfo = &bufferInfos.back();
                writeInfo.descriptorType = resourceInfo.type == BindingResourceInfo::ResourceType::UniformBuffer ?
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        case BindingResourceInfo::ResourceType::Texture:
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = reinterpret_cast<VulkanTexture*>(resourceInfo.resource)->GetVkImageView();
                imageInfo.sampler = VK_NULL_HANDLE;

                imageInfos.push_back(imageInfo);
                writeInfo.pImageInfo = &imageInfos.back();
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        case BindingResourceInfo::ResourceType::Sampler:
            {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                imageInfo.imageView = VK_NULL_HANDLE;
                imageInfo.sampler = reinterpret_cast<VulkanSampler*>(resourceInfo.resource)->GetVkSampler();

                imageInfos.push_back(imageInfo);
                writeInfo.pImageInfo = &imageInfos.back();
                writeInfo.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                descriptorWrites.push_back(writeInfo);
            }
            break;

        default:
            // サポートされていないリソースタイプ
            break;
        }
    }

    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(m_device->GetVkDevice(), static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void VulkanCommandList::BindDescriptorSets(VulkanPipeline* pipeline)
{
    if (!pipeline) {
        return;
    }

    VkPipelineBindPoint bindPoint = pipeline->GetPipelineType() == EPipelineType::Graphics 
        ? VK_PIPELINE_BIND_POINT_GRAPHICS 
        : VK_PIPELINE_BIND_POINT_COMPUTE;

    VkPipelineLayout pipelineLayout = pipeline->GetVkPipelineLayout();
    
    // ディスクリプタセットが存在するセットインデックスを収集
    std::map<uint32_t, VkDescriptorSet> sortedDescriptorSets;
    for (const auto& entry : m_boundResources) {
        const ShaderBindingKey& key = entry.first;
        auto it = m_descriptorSetCache.find(key);
        if (it != m_descriptorSetCache.end()) {
            sortedDescriptorSets[key.setIndex] = it->second.descriptorSet;
        }
    }

    if (sortedDescriptorSets.empty()) {
        return;
    }

    // バインドするディスクリプタセットを準備
    std::vector<VkDescriptorSet> descriptorSets;
    std::vector<uint32_t> setIndices;

    for (const auto& entry : sortedDescriptorSets) {
        setIndices.push_back(entry.first);
        descriptorSets.push_back(entry.second);
    }

    // 動的オフセット情報を収集（バッファ用）
    std::vector<uint32_t> dynamicOffsets;
    for (const auto& setIndex : setIndices) {
        for (const auto& entry : m_boundResources) {
            if (entry.first.setIndex == setIndex && entry.second.type == EBindingResourceType::Buffer) {
                if (entry.second.buffer.dynamicOffset != 0) {
                    dynamicOffsets.push_back(entry.second.buffer.dynamicOffset);
                }
            }
        }
    }

    // ディスクリプタセットをコマンドバッファにバインド
    if (!descriptorSets.empty()) {
        vkCmdBindDescriptorSets(
            m_commandBuffer,
            bindPoint,
            pipelineLayout,
            setIndices[0], // 最初のセットインデックス
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            static_cast<uint32_t>(dynamicOffsets.size()),
            dynamicOffsets.empty() ? nullptr : dynamicOffsets.data()
        );
    }
}

VkDescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(const ShaderBindingKey& key)
{
    // キャッシュから既存のディスクリプタセットを検索
    auto it = m_descriptorSetCache.find(key);
    if (it != m_descriptorSetCache.end()) {
        return it->second.descriptorSet;
    }

    // リソースバインディング情報を取得
    VulkanShader* shader = static_cast<VulkanShader*>(key.shader);
    if (!shader) {
        return VK_NULL_HANDLE;
    }

    const VkDescriptorSetLayout& layout = shader->GetDescriptorSetLayout(key.setIndex);
    if (layout == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // ディスクリプタプールを作成または取得
    VkDescriptorPool descriptorPool = GetOrCreateDescriptorPool();
    if (descriptorPool == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    // 新しいディスクリプタセットの割り当て
    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkResult result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);

    if (result != VK_SUCCESS) {
        // プールが満杯の場合、新しいプールを作成して再試行
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            descriptorPool = CreateDescriptorPool();
            allocInfo.descriptorPool = descriptorPool;
            result = vkAllocateDescriptorSets(m_device->GetVkDevice(), &allocInfo, &descriptorSet);
            if (result != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
        }
        else {
            return VK_NULL_HANDLE;
        }
    }

    // キャッシュに新しいディスクリプタセット情報を保存
    DescriptorSetInfo setInfo;
    setInfo.descriptorSet = descriptorSet;
    setInfo.descriptorPool = descriptorPool;
    setInfo.layout = layout;
    
    m_descriptorSetCache[key] = setInfo;
    
    return descriptorSet;
}

setInfo.descriptorPool = descriptorPool;
    setInfo.layout = layout;
    
    m_descriptorSetCache[key] = setInfo;
    
    return descriptorSet;
}

// ディスクリプタプールの取得または作成
VkDescriptorPool VulkanCommandList::GetOrCreateDescriptorPool()
{
    // 使用可能なプールがあるか確認
    if (!m_descriptorPools.empty())
    {
        return m_descriptorPools.back();
    }
    
    // 新しいプールを作成
    return CreateDescriptorPool();
}

// 新しいディスクリプタプールを作成
VkDescriptorPool VulkanCommandList::CreateDescriptorPool()
{
    // 各タイプのディスクリプタの数を定義
    std::array<VkDescriptorPoolSize, 6> poolSizes{};
    
    // ユニフォームバッファ用
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // ストレージバッファ用
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // サンプル済みイメージ用
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[2].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // サンプラー用
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[3].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // 結合イメージサンプラー用
    poolSizes[4].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[4].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // ストレージイメージ用
    poolSizes[5].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[5].descriptorCount = MAX_DESCRIPTORS_PER_TYPE;
    
    // プール作成情報の設定
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // 個別の解放をサポート
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_DESCRIPTOR_SETS;  // プールごとの最大セット数
    
    // 新しいプールを作成
    VkDescriptorPool newPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(m_device->GetVkDevice(), &poolInfo, nullptr, &newPool) != VK_SUCCESS)
    {
        throw std::runtime_error("ディスクリプタプールの作成に失敗しました");
    }
    
    // 作成したプールをリストに追加
    m_descriptorPools.push_back(newPool);
    
    return newPool;
}

// 指定されたリソースをコマンドリストの実行中に維持するために追加
void VulkanCommandList::AddTemporaryResource(std::shared_ptr<IResource> resource)
{
    if (resource)
    {
        // リソースが既に存在するかチェック
        auto it = std::find(m_temporaryResources.begin(), m_temporaryResources.end(), resource);
        if (it == m_temporaryResources.end())
        {
            // 存在しない場合は追加
            m_temporaryResources.push_back(resource);
        }
    }
}

// シェーダーリソースのバインド（共通処理）
void VulkanCommandList::BindShaderResource(uint32_t setIndex, uint32_t binding, 
                                          BindingResourceInfo::ResourceType type, 
                                          void* resource, uint64_t offset, uint64_t size)
{
    if (!resource || !m_currentPipeline)
    {
        return;
    }
    
    // バインディングキーを作成
    ShaderBindingKey key;
    key.setIndex = setIndex;
    key.shader = m_currentPipeline->GetShader();
    
    // バインディング情報を更新または追加
    BindingResourceInfo resourceInfo;
    resourceInfo.type = type;
    resourceInfo.resource = resource;
    resourceInfo.offset = offset;
    resourceInfo.size = size;
    
    m_bindingResources[key][binding] = resourceInfo;
    
    // ディスクリプタセットが既に作成されている場合は更新マークを付ける
    auto it = m_descriptorSetCache.find(key);
    if (it != m_descriptorSetCache.end())
    {
        it->second.needsUpdate = true;
    }
}

// コマンドリストのクリーンアップ処理
void VulkanCommandList::Cleanup()
{
    // 一時的なリソース参照をクリア
    m_temporaryResources.clear();
    
    // バインディングリソース情報をクリア
    m_bindingResources.clear();
    
    // カレントパイプラインをクリア
    m_currentPipeline = nullptr;
    
    // 頂点バッファとインデックスバッファの参照をクリア
    m_currentVertexBuffers.clear();
    m_currentVertexBufferOffsets.clear();
    m_currentIndexBuffer = nullptr;
    m_currentIndexBufferOffset = 0;
}

// すべてのディスクリプタセットのバインド処理
void VulkanCommandList::BindAllDescriptorSets()
{
    if (!m_currentPipeline)
    {
        return;
    }
    
    VulkanPipeline* vulkanPipeline = static_cast<VulkanPipeline*>(m_currentPipeline.get());
    VkPipelineBindPoint bindPoint = vulkanPipeline->IsCompute() ? 
        VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    
    // セットごとにディスクリプタセットを作成・更新・バインド
    for (const auto& [key, resourceMap] : m_bindingResources)
    {
        if (key.shader != m_currentPipeline->GetShader())
        {
            continue;  // 現在のパイプラインに関連しないリソースはスキップ
        }
        
        // ディスクリプタセットを取得または作成
        VkDescriptorSet descriptorSet = GetOrCreateDescriptorSet(key);
        if (descriptorSet == VK_NULL_HANDLE)
        {
            continue;
        }
        
        // 必要に応じてディスクリプタセットを更新
        auto it = m_descriptorSetCache.find(key);
        if (it != m_descriptorSetCache.end() && it->second.needsUpdate)
        {
            UpdateDescriptorSet(key, descriptorSet);
            it->second.needsUpdate = false;
        }
        
        // ディスクリプタセットをバインド
        vkCmdBindDescriptorSets(
            m_commandBuffer,
            bindPoint,
            vulkanPipeline->GetVkPipelineLayout(),
            key.setIndex,
            1,
            &descriptorSet,
            0,
            nullptr
        );
    }
}

} // namespace NorvesLib::RHI::Vulkan
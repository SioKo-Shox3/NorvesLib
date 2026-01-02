#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanDescriptorSet.h"
#include <stdexcept>
#include <cstring>

namespace NorvesLib::RHI::Vulkan
{

using namespace NorvesLib::Core::Container;

//===========================================================================================
// ResourceBarrierTrackerの実装
//===========================================================================================

vk::AccessFlags ResourceBarrierTracker::ResourceStateToAccessFlags(ResourceState state) const
{
    switch (state)
    {
        case ResourceState::Common:
            return {};
        case ResourceState::VertexBuffer:
            return vk::AccessFlagBits::eVertexAttributeRead;
        case ResourceState::IndexBuffer:
            return vk::AccessFlagBits::eIndexRead;
        case ResourceState::ConstantBuffer:
            return vk::AccessFlagBits::eUniformRead;
        case ResourceState::RenderTarget:
            return vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eColorAttachmentRead;
        case ResourceState::DepthWrite:
            return vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead;
        case ResourceState::DepthRead:
            return vk::AccessFlagBits::eDepthStencilAttachmentRead;
        case ResourceState::ShaderResource:
            return vk::AccessFlagBits::eShaderRead;
        case ResourceState::UnorderedAccess:
            return vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;
        case ResourceState::CopySource:
            return vk::AccessFlagBits::eTransferRead;
        case ResourceState::CopyDest:
            return vk::AccessFlagBits::eTransferWrite;
        case ResourceState::Present:
            return {};
        default:
            return {};
    }
}

vk::PipelineStageFlags ResourceBarrierTracker::ResourceStateToPipelineStageFlags(ResourceState state) const
{
    switch (state)
    {
        case ResourceState::Common:
            return vk::PipelineStageFlagBits::eTopOfPipe;
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:
            return vk::PipelineStageFlagBits::eVertexInput;
        case ResourceState::ConstantBuffer:
            return vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        case ResourceState::RenderTarget:
            return vk::PipelineStageFlagBits::eColorAttachmentOutput;
        case ResourceState::DepthWrite:
        case ResourceState::DepthRead:
            return vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        case ResourceState::ShaderResource:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case ResourceState::UnorderedAccess:
            return vk::PipelineStageFlagBits::eComputeShader;
        case ResourceState::CopySource:
        case ResourceState::CopyDest:
            return vk::PipelineStageFlagBits::eTransfer;
        case ResourceState::Present:
            return vk::PipelineStageFlagBits::eBottomOfPipe;
        default:
            return vk::PipelineStageFlagBits::eTopOfPipe;
    }
}

vk::ImageLayout ResourceBarrierTracker::ResourceStateToImageLayout(ResourceState state) const
{
    switch (state)
    {
        case ResourceState::Common:
            return vk::ImageLayout::eGeneral;
        case ResourceState::RenderTarget:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ResourceState::DepthWrite:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ResourceState::DepthRead:
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ResourceState::ShaderResource:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ResourceState::UnorderedAccess:
            return vk::ImageLayout::eGeneral;
        case ResourceState::CopySource:
            return vk::ImageLayout::eTransferSrcOptimal;
        case ResourceState::CopyDest:
            return vk::ImageLayout::eTransferDstOptimal;
        case ResourceState::Present:
            return vk::ImageLayout::ePresentSrcKHR;
        default:
            return vk::ImageLayout::eUndefined;
    }
}

//===========================================================================================
// PipelineStateCacheの実装
//===========================================================================================

bool PipelineStateCache::GraphicsPipelineCacheKey::operator==(const GraphicsPipelineCacheKey& other) const
{
    return renderPass == other.renderPass &&
           topology == other.topology &&
           cullMode == other.cullMode &&
           frontFace == other.frontFace &&
           polygonMode == other.polygonMode &&
           bDepthTestEnable == other.bDepthTestEnable &&
           bDepthWriteEnable == other.bDepthWriteEnable &&
           depthCompareOp == other.depthCompareOp &&
           bBlendEnable == other.bBlendEnable;
}

std::size_t PipelineStateCache::GraphicsPipelineCacheKeyHash::operator()(const GraphicsPipelineCacheKey& key) const
{
    std::size_t h = 0;
    h ^= std::hash<VkRenderPass>()(static_cast<VkRenderPass>(key.renderPass)) << 1;
    h ^= std::hash<int>()(static_cast<int>(key.topology)) << 2;
    h ^= std::hash<int>()(static_cast<int>(key.cullMode)) << 3;
    h ^= std::hash<bool>()(key.bDepthTestEnable) << 4;
    h ^= std::hash<bool>()(key.bBlendEnable) << 5;
    return h;
}

bool PipelineStateCache::ComputePipelineCacheKey::operator==(const ComputePipelineCacheKey& other) const
{
    return computeShader == other.computeShader;
}

std::size_t PipelineStateCache::ComputePipelineCacheKeyHash::operator()(const ComputePipelineCacheKey& key) const
{
    return std::hash<VkShaderModule>()(static_cast<VkShaderModule>(key.computeShader));
}

//===========================================================================================
// VulkanCommandListの実装
//===========================================================================================

VulkanCommandList::VulkanCommandList(TSharedPtr<VulkanDevice> device)
    : m_device(device)
{
    // コマンドバッファの割り当て
    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = m_device->GetVkCommandPool();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto result = m_device->GetVkDevice().allocateCommandBuffers(allocInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドバッファの割り当てに失敗しました");
    }
    m_commandBuffer = result.value[0];

    // フェンスの作成
    vk::FenceCreateInfo fenceInfo;
    fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
    
    auto fenceResult = m_device->GetVkDevice().createFence(fenceInfo);
    if (fenceResult.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("フェンスの作成に失敗しました");
    }
    m_fence = fenceResult.value;

    // ディスクリプタプールの作成
    CreateDescriptorPool();
}

VulkanCommandList::~VulkanCommandList()
{
    m_device->GetVkDevice().waitIdle();

    DestroyDescriptorPool();

    if (m_fence)
    {
        m_device->GetVkDevice().destroyFence(m_fence);
    }

    if (m_commandBuffer)
    {
        m_device->GetVkDevice().freeCommandBuffers(
            m_device->GetVkCommandPool(), 1, &m_commandBuffer);
    }
}

void VulkanCommandList::Begin()
{
    // フェンスを待機
    (void)m_device->GetVkDevice().waitForFences(1, &m_fence, vk::True, UINT64_MAX);
    (void)m_device->GetVkDevice().resetFences(1, &m_fence);

    Reset();

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    auto result = m_commandBuffer.begin(beginInfo);
    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドバッファの開始に失敗しました");
    }

    m_bIsRecording = true;
}

void VulkanCommandList::End()
{
    if (!m_bIsRecording)
    {
        return;
    }

    if (m_bInRenderPass)
    {
        EndRenderPass();
    }

    auto result = m_commandBuffer.end();
    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドバッファの終了に失敗しました");
    }

    m_bIsRecording = false;
}

void VulkanCommandList::Submit(bool bWaitForCompletion)
{
    vk::SubmitInfo submitInfo;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vk::Queue queue = m_device->GetVkGraphicsQueue();
    auto result = queue.submit(1, &submitInfo, m_fence);
    if (result != vk::Result::eSuccess)
    {
        throw std::runtime_error("コマンドの送信に失敗しました");
    }

    if (bWaitForCompletion)
    {
        (void)m_device->GetVkDevice().waitForFences(1, &m_fence, vk::True, UINT64_MAX);
    }
}

void VulkanCommandList::BeginRenderPass(RenderPassPtr renderPass, FramebufferPtr framebuffer)
{
    auto vkRenderPass = DynamicPointerCast<VulkanRenderPass>(renderPass);
    auto vkFramebuffer = DynamicPointerCast<VulkanFramebuffer>(framebuffer);

    if (!vkRenderPass || !vkFramebuffer)
    {
        throw std::runtime_error("無効なレンダーパスまたはフレームバッファです");
    }

    VariableArray<vk::ClearValue> clearValues;
    const auto& desc = vkRenderPass->GetDesc();
    
    for (const auto& attachment : desc.colorAttachments)
    {
        vk::ClearValue clearValue;
        clearValue.color = vk::ClearColorValue{std::array<float, 4>{
            attachment.clearColor.r,
            attachment.clearColor.g,
            attachment.clearColor.b,
            attachment.clearColor.a
        }};
        clearValues.push_back(clearValue);
    }

    if (desc.depthStencilAttachment.has_value())
    {
        vk::ClearValue clearValue;
        clearValue.depthStencil = vk::ClearDepthStencilValue{
            desc.depthStencilAttachment->clearDepth,
            desc.depthStencilAttachment->clearStencil
        };
        clearValues.push_back(clearValue);
    }

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = vkRenderPass->GetVkRenderPass();
    renderPassInfo.framebuffer = vkFramebuffer->GetVkFramebuffer();
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = vk::Extent2D{
        vkFramebuffer->GetWidth(), 
        vkFramebuffer->GetHeight()
    };
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
    m_bInRenderPass = true;
}

void VulkanCommandList::EndRenderPass()
{
    if (m_bInRenderPass)
    {
        m_commandBuffer.endRenderPass();
        m_bInRenderPass = false;
    }
}

void VulkanCommandList::SetViewport(const Viewport& viewport)
{
    vk::Viewport vkViewport;
    vkViewport.x = viewport.x;
    vkViewport.y = viewport.y;
    vkViewport.width = viewport.width;
    vkViewport.height = viewport.height;
    vkViewport.minDepth = viewport.minDepth;
    vkViewport.maxDepth = viewport.maxDepth;

    m_commandBuffer.setViewport(0, 1, &vkViewport);
}

void VulkanCommandList::SetScissor(const ScissorRect& scissor)
{
    vk::Rect2D vkScissor;
    vkScissor.offset = vk::Offset2D{static_cast<int32_t>(scissor.x), static_cast<int32_t>(scissor.y)};
    vkScissor.extent = vk::Extent2D{scissor.width, scissor.height};

    m_commandBuffer.setScissor(0, 1, &vkScissor);
}

void VulkanCommandList::SetPipeline(PipelinePtr pipeline)
{
    auto vkPipeline = DynamicPointerCast<VulkanPipeline>(pipeline);
    if (!vkPipeline)
    {
        throw std::runtime_error("無効なパイプラインです");
    }

    vk::PipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    
    m_commandBuffer.bindPipeline(bindPoint, vkPipeline->GetVkPipeline());
    m_currentPipeline = pipeline;
}

void VulkanCommandList::SetVertexBuffer(BufferPtr buffer, uint64_t offset, uint32_t slot)
{
    auto vkBuffer = DynamicPointerCast<VulkanBuffer>(buffer);
    if (!vkBuffer)
    {
        throw std::runtime_error("無効なバッファです");
    }

    if (slot >= m_currentVertexBuffers.size())
    {
        m_currentVertexBuffers.resize(slot + 1);
        m_currentVertexBufferOffsets.resize(slot + 1);
    }

    m_currentVertexBuffers[slot] = buffer;
    m_currentVertexBufferOffsets[slot] = offset;

    vk::Buffer vkBuf = vkBuffer->GetVkBuffer();
    m_commandBuffer.bindVertexBuffers(slot, 1, &vkBuf, &offset);
}

void VulkanCommandList::SetIndexBuffer(BufferPtr buffer, uint64_t offset)
{
    auto vkBuffer = DynamicPointerCast<VulkanBuffer>(buffer);
    if (!vkBuffer)
    {
        throw std::runtime_error("無効なバッファです");
    }

    m_currentIndexBuffer = buffer;
    m_currentIndexBufferOffset = offset;

    m_commandBuffer.bindIndexBuffer(vkBuffer->GetVkBuffer(), offset, vk::IndexType::eUint32);
}

void VulkanCommandList::SetConstantBuffer(BufferPtr buffer, uint32_t slot, ShaderStage stage)
{
    ShaderBindingKey key{0, slot, stage};
    BindingResourceInfo info;
    info.type = BindingResourceInfo::Type::Buffer;
    info.resource = StaticPointerCast<void>(buffer);
    info.offset = 0;
    info.range = VK_WHOLE_SIZE;
    m_bindingResources[key] = info;
}

void VulkanCommandList::SetTexture(TexturePtr texture, uint32_t slot, ShaderStage stage)
{
    ShaderBindingKey key{0, slot, stage};
    BindingResourceInfo info;
    info.type = BindingResourceInfo::Type::Texture;
    info.resource = StaticPointerCast<void>(texture);
    m_bindingResources[key] = info;
}

void VulkanCommandList::SetSampler(SamplerPtr sampler, uint32_t slot, ShaderStage stage)
{
    ShaderBindingKey key{0, slot, stage};
    BindingResourceInfo info;
    info.type = BindingResourceInfo::Type::Sampler;
    info.resource = StaticPointerCast<void>(sampler);
    m_bindingResources[key] = info;
}

void VulkanCommandList::SetDescriptorSet(DescriptorSetPtr descriptorSet, uint32_t slot)
{
    auto vkDescSet = DynamicPointerCast<VulkanDescriptorSet>(descriptorSet);
    if (!vkDescSet)
    {
        throw std::runtime_error("無効なディスクリプタセットです");
    }

    auto vkPipeline = DynamicPointerCast<VulkanPipeline>(m_currentPipeline);
    if (!vkPipeline)
    {
        throw std::runtime_error("パイプラインが設定されていません");
    }

    vk::PipelineBindPoint bindPoint = vkPipeline->IsCompute() ? 
        vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;

    vk::DescriptorSet descSet = vkDescSet->GetVkDescriptorSet();
    m_commandBuffer.bindDescriptorSets(
        bindPoint,
        vkDescSet->GetVkPipelineLayout(),
        slot,
        1,
        &descSet,
        0,
        nullptr
    );
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
{
    m_commandBuffer.drawIndexed(indexCount, 1, startIndexLocation, baseVertexLocation, 0);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t startVertexLocation)
{
    m_commandBuffer.draw(vertexCount, 1, startVertexLocation, 0);
}

void VulkanCommandList::DrawIndexedInstanced(uint32_t indexCount, uint32_t instanceCount, 
    uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation)
{
    m_commandBuffer.drawIndexed(indexCount, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
}

void VulkanCommandList::DrawInstanced(uint32_t vertexCount, uint32_t instanceCount, 
    uint32_t startVertexLocation, uint32_t startInstanceLocation)
{
    m_commandBuffer.draw(vertexCount, instanceCount, startVertexLocation, startInstanceLocation);
}

void VulkanCommandList::Dispatch(uint32_t threadGroupCountX, uint32_t threadGroupCountY, uint32_t threadGroupCountZ)
{
    m_commandBuffer.dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
}

void VulkanCommandList::CopyBuffer(BufferPtr src, BufferPtr dst, uint64_t size, 
    uint64_t srcOffset, uint64_t dstOffset)
{
    auto vkSrc = DynamicPointerCast<VulkanBuffer>(src);
    auto vkDst = DynamicPointerCast<VulkanBuffer>(dst);

    if (!vkSrc || !vkDst)
    {
        throw std::runtime_error("無効なバッファです");
    }

    uint64_t copySize = size == 0 ? vkSrc->GetSize() : size;

    vk::BufferCopy region;
    region.srcOffset = srcOffset;
    region.dstOffset = dstOffset;
    region.size = copySize;

    m_commandBuffer.copyBuffer(vkSrc->GetVkBuffer(), vkDst->GetVkBuffer(), 1, &region);
}

void VulkanCommandList::CopyBufferToTexture(BufferPtr src, TexturePtr dst, 
    uint32_t width, uint32_t height, uint64_t bufferOffset, 
    uint32_t mipLevel, uint32_t arrayIndex)
{
    auto vkSrc = DynamicPointerCast<VulkanBuffer>(src);
    auto vkDst = DynamicPointerCast<VulkanTexture>(dst);

    if (!vkSrc || !vkDst)
    {
        throw std::runtime_error("無効なバッファまたはテクスチャです");
    }

    vk::BufferImageCopy region;
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    m_commandBuffer.copyBufferToImage(
        vkSrc->GetVkBuffer(),
        vkDst->GetVkImage(),
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &region
    );
}

void VulkanCommandList::CopyTextureToBuffer(TexturePtr src, BufferPtr dst, 
    uint32_t width, uint32_t height, uint64_t bufferOffset, 
    uint32_t mipLevel, uint32_t arrayIndex)
{
    auto vkSrc = DynamicPointerCast<VulkanTexture>(src);
    auto vkDst = DynamicPointerCast<VulkanBuffer>(dst);

    if (!vkSrc || !vkDst)
    {
        throw std::runtime_error("無効なテクスチャまたはバッファです");
    }

    vk::BufferImageCopy region;
    region.bufferOffset = bufferOffset;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.imageSubresource.mipLevel = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayIndex;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = vk::Offset3D{0, 0, 0};
    region.imageExtent = vk::Extent3D{width, height, 1};

    m_commandBuffer.copyImageToBuffer(
        vkSrc->GetVkImage(),
        vk::ImageLayout::eTransferSrcOptimal,
        vkDst->GetVkBuffer(),
        1,
        &region
    );
}

void VulkanCommandList::CopyTexture(TexturePtr src, TexturePtr dst, 
    uint32_t width, uint32_t height, 
    uint32_t srcMipLevel, uint32_t srcArrayIndex,
    uint32_t dstMipLevel, uint32_t dstArrayIndex)
{
    auto vkSrc = DynamicPointerCast<VulkanTexture>(src);
    auto vkDst = DynamicPointerCast<VulkanTexture>(dst);

    if (!vkSrc || !vkDst)
    {
        throw std::runtime_error("無効なテクスチャです");
    }

    vk::ImageCopy region;
    region.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.srcSubresource.mipLevel = srcMipLevel;
    region.srcSubresource.baseArrayLayer = srcArrayIndex;
    region.srcSubresource.layerCount = 1;
    region.srcOffset = vk::Offset3D{0, 0, 0};
    region.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
    region.dstSubresource.mipLevel = dstMipLevel;
    region.dstSubresource.baseArrayLayer = dstArrayIndex;
    region.dstSubresource.layerCount = 1;
    region.dstOffset = vk::Offset3D{0, 0, 0};
    region.extent = vk::Extent3D{width, height, 1};

    m_commandBuffer.copyImage(
        vkSrc->GetVkImage(),
        vk::ImageLayout::eTransferSrcOptimal,
        vkDst->GetVkImage(),
        vk::ImageLayout::eTransferDstOptimal,
        1,
        &region
    );
}

void VulkanCommandList::BufferBarrier(BufferPtr buffer, ResourceState beforeState, ResourceState afterState, 
                                      uint64_t offset, uint64_t size)
{
    auto vkBuffer = DynamicPointerCast<VulkanBuffer>(buffer);
    if (!vkBuffer)
    {
        throw std::runtime_error("無効なバッファです");
    }

    vk::BufferMemoryBarrier barrier;
    barrier.srcAccessMask = m_barrierTracker.ResourceStateToAccessFlags(beforeState);
    barrier.dstAccessMask = m_barrierTracker.ResourceStateToAccessFlags(afterState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = vkBuffer->GetVkBuffer();
    barrier.offset = offset;
    barrier.size = size == 0 ? VK_WHOLE_SIZE : size;

    m_commandBuffer.pipelineBarrier(
        m_barrierTracker.ResourceStateToPipelineStageFlags(beforeState),
        m_barrierTracker.ResourceStateToPipelineStageFlags(afterState),
        {},
        0, nullptr,
        1, &barrier,
        0, nullptr
    );
}

void VulkanCommandList::TextureBarrier(TexturePtr texture, ResourceState beforeState, ResourceState afterState,
                                       uint32_t mipLevel, uint32_t arrayIndex, uint32_t mipCount, uint32_t arrayCount)
{
    auto vkTexture = DynamicPointerCast<VulkanTexture>(texture);
    if (!vkTexture)
    {
        throw std::runtime_error("無効なテクスチャです");
    }

    vk::ImageMemoryBarrier barrier;
    barrier.srcAccessMask = m_barrierTracker.ResourceStateToAccessFlags(beforeState);
    barrier.dstAccessMask = m_barrierTracker.ResourceStateToAccessFlags(afterState);
    barrier.oldLayout = m_barrierTracker.ResourceStateToImageLayout(beforeState);
    barrier.newLayout = m_barrierTracker.ResourceStateToImageLayout(afterState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = vkTexture->GetVkImage();
    barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    barrier.subresourceRange.baseMipLevel = mipLevel;
    barrier.subresourceRange.levelCount = mipCount == 0 ? VK_REMAINING_MIP_LEVELS : mipCount;
    barrier.subresourceRange.baseArrayLayer = arrayIndex;
    barrier.subresourceRange.layerCount = arrayCount == 0 ? VK_REMAINING_ARRAY_LAYERS : arrayCount;

    m_commandBuffer.pipelineBarrier(
        m_barrierTracker.ResourceStateToPipelineStageFlags(beforeState),
        m_barrierTracker.ResourceStateToPipelineStageFlags(afterState),
        {},
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void VulkanCommandList::OptimizedBufferBarrier(VulkanBuffer* buffer, ResourceState newState, uint64_t offset, uint64_t size)
{
    // 簡略化された実装
    vk::BufferMemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
    barrier.dstAccessMask = m_barrierTracker.ResourceStateToAccessFlags(newState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = buffer->GetVkBuffer();
    barrier.offset = offset;
    barrier.size = size == 0 ? VK_WHOLE_SIZE : size;

    m_commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands,
        m_barrierTracker.ResourceStateToPipelineStageFlags(newState),
        {},
        0, nullptr,
        1, &barrier,
        0, nullptr
    );
}

void VulkanCommandList::OptimizedTextureBarrier(VulkanTexture* texture, ResourceState newState, const vk::ImageSubresourceRange& subresourceRange)
{
    vk::ImageMemoryBarrier barrier;
    barrier.srcAccessMask = vk::AccessFlagBits::eMemoryWrite;
    barrier.dstAccessMask = m_barrierTracker.ResourceStateToAccessFlags(newState);
    barrier.oldLayout = texture->GetVkImageLayout();
    barrier.newLayout = m_barrierTracker.ResourceStateToImageLayout(newState);
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->GetVkImage();
    barrier.subresourceRange = subresourceRange;

    m_commandBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands,
        m_barrierTracker.ResourceStateToPipelineStageFlags(newState),
        {},
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    texture->SetVkImageLayout(barrier.newLayout);
}

vk::Pipeline VulkanCommandList::GetOrCreateGraphicsPipeline(const PipelineStateCache::GraphicsPipelineCacheKey& key)
{
    auto it = m_pipelineStateCache.graphicsPipelines.find(key);
    if (it != m_pipelineStateCache.graphicsPipelines.end())
    {
        return it->second;
    }
    
    // パイプラインキャッシュにない場合は作成が必要
    // 現時点では未実装
    return nullptr;
}

vk::Pipeline VulkanCommandList::GetOrCreateComputePipeline(const PipelineStateCache::ComputePipelineCacheKey& key)
{
    auto it = m_pipelineStateCache.computePipelines.find(key);
    if (it != m_pipelineStateCache.computePipelines.end())
    {
        return it->second;
    }
    
    // パイプラインキャッシュにない場合は作成が必要
    // 現時点では未実装
    return nullptr;
}

void VulkanCommandList::SavePipelineCache(const NorvesLib::Core::Container::String& /*filePath*/)
{
    // 実装予定
}

void VulkanCommandList::LoadPipelineCache(const NorvesLib::Core::Container::String& /*filePath*/)
{
    // 実装予定
}

void VulkanCommandList::ResetResourceBarriers()
{
    m_barrierTracker.bufferStates.clear();
    m_barrierTracker.imageStates.clear();
}

void VulkanCommandList::Reset()
{
    m_currentPipeline = nullptr;
    m_currentVertexBuffers.clear();
    m_currentVertexBufferOffsets.clear();
    m_currentIndexBuffer = nullptr;
    m_currentIndexBufferOffset = 0;
    m_temporaryResources.clear();
    m_bindingResources.clear();
    m_descriptorSetCache.clear();
    
    m_commandBuffer.reset({});
}

void VulkanCommandList::CreateDescriptorPool()
{
    FixedArray<vk::DescriptorPoolSize, 5> poolSizes = {{
        { vk::DescriptorType::eUniformBuffer, MAX_DESCRIPTORS_PER_TYPE },
        { vk::DescriptorType::eSampledImage, MAX_DESCRIPTORS_PER_TYPE },
        { vk::DescriptorType::eSampler, MAX_DESCRIPTORS_PER_TYPE },
        { vk::DescriptorType::eStorageBuffer, MAX_DESCRIPTORS_PER_TYPE },
        { vk::DescriptorType::eStorageImage, MAX_DESCRIPTORS_PER_TYPE }
    }};

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = MAX_DESCRIPTOR_SETS;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;

    auto result = m_device->GetVkDevice().createDescriptorPool(poolInfo);
    if (result.result != vk::Result::eSuccess)
    {
        throw std::runtime_error("ディスクリプタプールの作成に失敗しました");
    }
    m_descriptorPools.push_back(result.value);
}

void VulkanCommandList::DestroyDescriptorPool()
{
    for (auto& pool : m_descriptorPools)
    {
        m_device->GetVkDevice().destroyDescriptorPool(pool);
    }
    m_descriptorPools.clear();
}

bool VulkanCommandList::UpdateDescriptorSet(uint32_t /*setIndex*/)
{
    // 実装予定
    return true;
}

vk::DescriptorSet VulkanCommandList::GetOrCreateDescriptorSet(uint32_t /*setIndex*/, vk::DescriptorSetLayout /*layout*/)
{
    // 実装予定
    return nullptr;
}

void VulkanCommandList::BindDescriptorSets()
{
    // 実装予定
}

vk::PipelineStageFlags VulkanCommandList::ToVkPipelineStage(ShaderStage stage) const
{
    switch (stage)
    {
        case ShaderStage::Vertex:
            return vk::PipelineStageFlagBits::eVertexShader;
        case ShaderStage::Pixel:
            return vk::PipelineStageFlagBits::eFragmentShader;
        case ShaderStage::Compute:
            return vk::PipelineStageFlagBits::eComputeShader;
        case ShaderStage::Geometry:
            return vk::PipelineStageFlagBits::eGeometryShader;
        case ShaderStage::Hull:
            return vk::PipelineStageFlagBits::eTessellationControlShader;
        case ShaderStage::Domain:
            return vk::PipelineStageFlagBits::eTessellationEvaluationShader;
        default:
            return vk::PipelineStageFlagBits::eAllGraphics;
    }
}

vk::ShaderStageFlags VulkanCommandList::ToVkShaderStageFlags(ShaderStage stage) const
{
    vk::ShaderStageFlags flags;
    
    if ((stage & ShaderStage::Vertex) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eVertex;
    }
    if ((stage & ShaderStage::Pixel) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eFragment;
    }
    if ((stage & ShaderStage::Compute) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eCompute;
    }
    if ((stage & ShaderStage::Geometry) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eGeometry;
    }
    if ((stage & ShaderStage::Hull) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eTessellationControl;
    }
    if ((stage & ShaderStage::Domain) != ShaderStage::None)
    {
        flags |= vk::ShaderStageFlagBits::eTessellationEvaluation;
    }
    
    return flags;
}

} // namespace NorvesLib::RHI::Vulkan

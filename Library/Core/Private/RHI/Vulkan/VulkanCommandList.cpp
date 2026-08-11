#include "VulkanCommandList.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanSampler.h"
#include "VulkanPipeline.h"
#include "VulkanRenderPass.h"
#include "VulkanFramebuffer.h"
#include "VulkanDescriptorSet.h"
#include "RHI/SubmissionSerialAllocator.h"
#include "Logging/LogMacros.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <cstring>

namespace NorvesLib::RHI::Vulkan
{

    using namespace NorvesLib::Core::Container;

    namespace
    {
        vk::ImageAspectFlags GetBarrierAspectMask(const VulkanTexture& texture)
        {
            if ((texture.GetUsage() & ResourceUsage::DepthStencil) != ResourceUsage::None)
            {
                vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eDepth;
                if (texture.GetFormat() == Format::D24_UNORM_S8_UINT)
                {
                    aspectMask |= vk::ImageAspectFlagBits::eStencil;
                }
                return aspectMask;
            }

            return vk::ImageAspectFlagBits::eColor;
        }
    } // namespace

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
        case ResourceState::IndirectArgument:
            return vk::AccessFlagBits::eIndirectCommandRead;
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
            return vk::PipelineStageFlagBits::eFragmentShader |
                   vk::PipelineStageFlagBits::eComputeShader;
        case ResourceState::UnorderedAccess:
            return vk::PipelineStageFlagBits::eComputeShader;
        case ResourceState::IndirectArgument:
            return vk::PipelineStageFlagBits::eDrawIndirect;
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

    bool PipelineStateCache::GraphicsPipelineCacheKey::operator==(const GraphicsPipelineCacheKey &other) const
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

    std::size_t PipelineStateCache::GraphicsPipelineCacheKeyHash::operator()(const GraphicsPipelineCacheKey &key) const
    {
        std::size_t h = 0;
        h ^= std::hash<VkRenderPass>()(static_cast<VkRenderPass>(key.renderPass)) << 1;
        h ^= std::hash<int>()(static_cast<int>(key.topology)) << 2;
        h ^= std::hash<VkCullModeFlags>()(static_cast<VkCullModeFlags>(key.cullMode)) << 3;
        h ^= std::hash<bool>()(key.bDepthTestEnable) << 4;
        h ^= std::hash<bool>()(key.bBlendEnable) << 5;
        return h;
    }

    bool PipelineStateCache::ComputePipelineCacheKey::operator==(const ComputePipelineCacheKey &other) const
    {
        return computeShader == other.computeShader;
    }

    std::size_t PipelineStateCache::ComputePipelineCacheKeyHash::operator()(const ComputePipelineCacheKey &key) const
    {
        return std::hash<VkShaderModule>()(static_cast<VkShaderModule>(key.computeShader));
    }

    //===========================================================================================
    // VulkanCommandListの実装
    //===========================================================================================

    VulkanCommandList::VulkanCommandList(TSharedPtr<VulkanDevice> device)
        : m_device(device)
    {
        // フレームごとのコマンドバッファを割り当て
        vk::CommandBufferAllocateInfo allocInfo;
        allocInfo.commandPool = m_device->GetCommandPool();
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandBufferCount = MAX_COMMAND_BUFFERS;

        auto result = m_device->GetVkDevice().allocateCommandBuffers(allocInfo);
        if (result.result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファの割り当てに失敗しました");
        }

        m_commandBuffers.reserve(MAX_COMMAND_BUFFERS);
        for (uint32_t i = 0; i < MAX_COMMAND_BUFFERS; ++i)
        {
            m_commandBuffers.push_back(result.value[i]);
        }
        m_commandBuffer = m_commandBuffers[0];

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

        const VkDevice vkDevice = static_cast<VkDevice>(m_device->GetVkDevice());
        m_pfnBeginDebugUtilsLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(vkDevice, "vkCmdBeginDebugUtilsLabelEXT"));
        m_pfnEndDebugUtilsLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetDeviceProcAddr(vkDevice, "vkCmdEndDebugUtilsLabelEXT"));

#if NORVES_ENABLE_STATS
        CreateTimestampQueryPool();
#endif
    }

    VulkanCommandList::~VulkanCommandList()
    {
        m_device->WaitIdle();

#if NORVES_ENABLE_STATS
        DestroyTimestampQueryPool();
#endif

        DestroyDescriptorPool();

        if (m_fence)
        {
            m_device->GetVkDevice().destroyFence(m_fence);
        }

        if (m_commandBuffers.size() > 0)
        {
            m_device->GetVkDevice().freeCommandBuffers(
                m_device->GetCommandPool(),
                static_cast<uint32_t>(m_commandBuffers.size()),
                m_commandBuffers.data());
        }
    }

    void VulkanCommandList::Begin()
    {
        // フェンスを待機
        const vk::Result waitResult = m_device->GetVkDevice().waitForFences(
            1, &m_fence, vk::True, UINT64_MAX);
        if (waitResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドフェンスの待機に失敗しました");
        }

#if NORVES_ENABLE_STATS
        NotifyGPUTimestampFrameSlotCompleted(
            m_currentFrameIndex,
            m_DirectFrameSlotSubmissionSerials[m_currentFrameIndex]);
        ResolveGPUTimestampResultsForCurrentSlot();
#endif

        const vk::Result resetFenceResult = m_device->GetVkDevice().resetFences(1, &m_fence);
        if (resetFenceResult != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドフェンスのリセットに失敗しました");
        }

        Reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        auto result = m_commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファの開始に失敗しました");
        }

        m_bIsRecording = true;
#if NORVES_ENABLE_STATS
        PrepareGPUTimestampSlotForRecording();
#endif
    }

    void VulkanCommandList::BeginRecording()
    {
        // 現在のフレームのコマンドバッファを使用（SetFrameIndexで設定済み）
#if NORVES_ENABLE_STATS
        ResolveGPUTimestampResultsForCurrentSlot();
#endif

        Reset();

        vk::CommandBufferBeginInfo beginInfo;
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        auto result = m_commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファの開始に失敗しました");
        }

        m_bIsRecording = true;
#if NORVES_ENABLE_STATS
        PrepareGPUTimestampSlotForRecording();
#endif
    }

    void VulkanCommandList::SetFrameIndex(uint32_t frameIndex)
    {
        m_currentFrameIndex = frameIndex % MAX_COMMAND_BUFFERS;
        m_commandBuffer = m_commandBuffers[m_currentFrameIndex];
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

#if NORVES_ENABLE_STATS
        if (m_LegacyGPUTimestampScope.IsValid())
        {
            EndGPUTimestamp();
        }
#endif

        auto result = m_commandBuffer.end();
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドバッファの終了に失敗しました");
        }

        m_bIsRecording = false;
    }

    bool VulkanCommandList::SupportsGPUTimestamps() const
    {
#if NORVES_ENABLE_STATS
        return m_bTimestampSupported;
#else
        return false;
#endif
    }

    uint32_t VulkanCommandList::GetMaximumGPUTimestampScopesPerFrame() const
    {
        return SupportsGPUTimestamps() ? MaximumGPUTimestampScopesPerFrame : 0u;
    }

    void VulkanCommandList::BeginGPUTimestampFrame(uint64_t frameNumber)
    {
#if NORVES_ENABLE_STATS
        if (!m_bTimestampSupported || !m_bIsRecording || m_bTimestampFrameActive)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }

        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[m_currentFrameIndex];
        if (!Detail::TryBeginGPUTimestampFrame(batch, frameNumber, batch.bQueriesReset))
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }
        m_bTimestampFrameActive = true;
#else
        (void)frameNumber;
#endif
    }

    GPUTimestampScopeHandle VulkanCommandList::BeginGPUTimestampScope(const char* scopeName)
    {
#if NORVES_ENABLE_STATS
        GPUTimestampScopeHandle handle;
        if (!m_bTimestampSupported || !m_bIsRecording || !m_bTimestampFrameActive)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return handle;
        }

        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[m_currentFrameIndex];
        if (!Detail::TryBeginGPUTimestampScope(
                batch,
                m_currentFrameIndex,
                scopeName,
                handle))
        {
            ++m_InvalidGPUTimestampOperationCount;
            return {};
        }

        m_commandBuffer.writeTimestamp(
            vk::PipelineStageFlagBits::eTopOfPipe,
            m_timestampQueryPool,
            GetTimestampQueryBaseIndex(handle.FrameSlotIndex, handle.ScopeIndex));
        return handle;
#else
        (void)scopeName;
        return {};
#endif
    }

    void VulkanCommandList::EndGPUTimestampScope(GPUTimestampScopeHandle handle)
    {
#if NORVES_ENABLE_STATS
        if (!m_bTimestampSupported || !m_bIsRecording || !m_bTimestampFrameActive ||
            handle.FrameSlotIndex != m_currentFrameIndex)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }

        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[m_currentFrameIndex];
        if (!Detail::TryEndGPUTimestampScope(batch, handle))
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }

        m_commandBuffer.writeTimestamp(
            vk::PipelineStageFlagBits::eBottomOfPipe,
            m_timestampQueryPool,
            GetTimestampQueryBaseIndex(handle.FrameSlotIndex, handle.ScopeIndex) + 1u);
#else
        (void)handle;
#endif
    }

    void VulkanCommandList::EndGPUTimestampFrame()
    {
#if NORVES_ENABLE_STATS
        if (!m_bTimestampSupported || !m_bTimestampFrameActive)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }
        if (!Detail::TryEndGPUTimestampFrame(m_TimestampFrameBatches[m_currentFrameIndex]))
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }
        m_bTimestampFrameActive = false;
        m_bLegacyPrivateTimestampFrame = false;
#endif
    }

    void VulkanCommandList::CommitGPUTimestampSubmission(uint32_t frameSlotIndex,
                                                         uint64_t submissionSerial)
    {
#if NORVES_ENABLE_STATS
        if (frameSlotIndex >= MAX_COMMAND_BUFFERS)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }

        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[frameSlotIndex];
        if (batch.State == Detail::GPUTimestampFrameState::Empty)
        {
            return;
        }
        if (!Detail::TryCommitGPUTimestampSubmission(batch, submissionSerial))
        {
            ++m_InvalidGPUTimestampOperationCount;
            AbortGPUTimestampFrame(frameSlotIndex);
        }
#else
        (void)frameSlotIndex;
        (void)submissionSerial;
#endif
    }

    void VulkanCommandList::AbortGPUTimestampFrame(uint32_t frameSlotIndex) noexcept
    {
#if NORVES_ENABLE_STATS
        if (frameSlotIndex >= MAX_COMMAND_BUFFERS)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }
        Detail::AbortGPUTimestampFrameBatch(m_TimestampFrameBatches[frameSlotIndex]);
        if (frameSlotIndex == m_currentFrameIndex)
        {
            m_bTimestampFrameActive = false;
            m_bLegacyPrivateTimestampFrame = false;
            m_LegacyGPUTimestampScope = {};
        }
#else
        (void)frameSlotIndex;
#endif
    }

    void VulkanCommandList::NotifyGPUTimestampFrameSlotCompleted(
        uint32_t frameSlotIndex,
        uint64_t completedSubmissionSerial)
    {
#if NORVES_ENABLE_STATS
        if (frameSlotIndex >= MAX_COMMAND_BUFFERS)
        {
            ++m_InvalidGPUTimestampOperationCount;
            return;
        }
        (void)Detail::TryNotifyGPUTimestampFrameCompleted(
            m_TimestampFrameBatches[frameSlotIndex],
            completedSubmissionSerial);
#else
        (void)frameSlotIndex;
        (void)completedSubmissionSerial;
#endif
    }

    void VulkanCommandList::ConsumeCompletedGPUTimestampResults(
        VariableArray<GPUTimestampResult>& outResults)
    {
#if NORVES_ENABLE_STATS
        outResults = m_CompletedGPUTimestampResults;
        m_CompletedGPUTimestampResults.clear();
#else
        outResults.clear();
#endif
    }

    void VulkanCommandList::BeginGPUTimestamp(const char* markerName)
    {
#if NORVES_ENABLE_STATS
        if (!m_bTimestampSupported || !m_bIsRecording || m_LegacyGPUTimestampScope.IsValid())
        {
            return;
        }
        if (!m_bTimestampFrameActive)
        {
            BeginGPUTimestampFrame(0u);
            m_bLegacyPrivateTimestampFrame = m_bTimestampFrameActive;
        }
        m_LegacyGPUTimestampScope = BeginGPUTimestampScope(
            markerName ? markerName : "FrameGPU");
        if (m_LegacyGPUTimestampScope.IsValid())
        {
            m_TimestampFrameBatches[m_currentFrameIndex]
                .Scopes[m_LegacyGPUTimestampScope.ScopeIndex]
                .bLegacy = true;
        }
#else
        (void)markerName;
#endif
    }

    void VulkanCommandList::EndGPUTimestamp()
    {
#if NORVES_ENABLE_STATS
        if (!m_bTimestampSupported || !m_LegacyGPUTimestampScope.IsValid())
        {
            return;
        }
        const bool bFinalizePrivateFrame = m_bLegacyPrivateTimestampFrame;
        EndGPUTimestampScope(m_LegacyGPUTimestampScope);
        m_LegacyGPUTimestampScope = {};
        if (bFinalizePrivateFrame)
        {
            EndGPUTimestampFrame();
        }
#endif
    }

    float VulkanCommandList::GetLastGPUTimestampDurationMs() const
    {
#if NORVES_ENABLE_STATS
        return m_lastGPUTimestampDurationMs;
#else
        return 0.0f;
#endif
    }

    void VulkanCommandList::BeginDebugMarker(const char* name)
    {
        if (!m_bIsRecording ||
            name == nullptr ||
            name[0] == '\0' ||
            m_pfnBeginDebugUtilsLabel == nullptr)
        {
            return;
        }

        VkDebugUtilsLabelEXT labelInfo = {};
        labelInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        labelInfo.pLabelName = name;
        labelInfo.color[0] = 0.25f;
        labelInfo.color[1] = 0.65f;
        labelInfo.color[2] = 1.0f;
        labelInfo.color[3] = 1.0f;

        m_pfnBeginDebugUtilsLabel(static_cast<VkCommandBuffer>(m_commandBuffer), &labelInfo);
    }

    void VulkanCommandList::EndDebugMarker()
    {
        if (!m_bIsRecording ||
            m_pfnEndDebugUtilsLabel == nullptr)
        {
            return;
        }

        m_pfnEndDebugUtilsLabel(static_cast<VkCommandBuffer>(m_commandBuffer));
    }

    void VulkanCommandList::Submit(bool bWaitForCompletion)
    {
        vk::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_commandBuffer;

        vk::Queue queue = m_device->GetGraphicsQueue();
#if NORVES_ENABLE_STATS
        uint64_t submittedSerial = 0u;
        const Detail::GPUTimestampSubmissionSequenceStatus submissionStatus =
            Detail::ExecuteGPUTimestampSubmissionSequence(
                this,
                m_currentFrameIndex,
                false,
                [&](uint64_t& outSerial)
                {
                    return RHI::Detail::TryAllocateSubmissionSerial(
                        m_DirectNextSubmissionSerial,
                        outSerial);
                },
                []() noexcept
                {
                    return true;
                },
                [&]()
                {
                    return queue.submit(1, &submitInfo, m_fence) == vk::Result::eSuccess;
                },
                submittedSerial);
        if (submissionStatus == Detail::GPUTimestampSubmissionSequenceStatus::SerialAllocationFailed)
        {
            throw std::runtime_error("コマンド送信serialが枯渇しました");
        }
        if (submissionStatus != Detail::GPUTimestampSubmissionSequenceStatus::Success)
        {
            throw std::runtime_error("コマンドの送信に失敗しました");
        }

        m_DirectNextSubmissionSerial = submittedSerial;
        m_DirectFrameSlotSubmissionSerials[m_currentFrameIndex] = submittedSerial;
#else
        auto result = queue.submit(1, &submitInfo, m_fence);
        if (result != vk::Result::eSuccess)
        {
            throw std::runtime_error("コマンドの送信に失敗しました");
        }
#endif

        if (bWaitForCompletion)
        {
            const vk::Result waitResult = m_device->GetVkDevice().waitForFences(
                1, &m_fence, vk::True, UINT64_MAX);
            if (waitResult != vk::Result::eSuccess)
            {
                throw std::runtime_error("コマンド送信完了の待機に失敗しました");
            }
#if NORVES_ENABLE_STATS
            NotifyGPUTimestampFrameSlotCompleted(m_currentFrameIndex, submittedSerial);
#endif
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
        const auto &desc = vkRenderPass->GetDesc();

        for (const auto &attachment : desc.colorAttachments)
        {
            VkClearValue clearValue = {};
            clearValue.color.float32[0] = attachment.clearColor[0];
            clearValue.color.float32[1] = attachment.clearColor[1];
            clearValue.color.float32[2] = attachment.clearColor[2];
            clearValue.color.float32[3] = attachment.clearColor[3];
            clearValues.push_back(*reinterpret_cast<vk::ClearValue *>(&clearValue));
        }

        if (desc.hasDepthStencil)
        {
            VkClearValue clearValue = {};
            clearValue.depthStencil.depth = desc.depthStencilAttachment.clearDepth;
            clearValue.depthStencil.stencil = desc.depthStencilAttachment.clearStencil;
            clearValues.push_back(*reinterpret_cast<vk::ClearValue *>(&clearValue));
        }

        vk::RenderPassBeginInfo renderPassInfo;
        renderPassInfo.renderPass = vkRenderPass->GetVkRenderPass();
        renderPassInfo.framebuffer = vkFramebuffer->GetVkFramebuffer();
        renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
        renderPassInfo.renderArea.extent = vk::Extent2D{
            vkFramebuffer->GetWidth(),
            vkFramebuffer->GetHeight()};
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // レンダーパス終了時のレイアウト追跡用に参照を保持
        m_activeRenderPass = vkRenderPass;
        m_activeFramebuffer = vkFramebuffer;

        m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);
        m_bInRenderPass = true;
    }

    void VulkanCommandList::EndRenderPass()
    {
        if (m_bInRenderPass)
        {
            m_commandBuffer.endRenderPass();
            m_bInRenderPass = false;

            // レンダーパスのfinalLayout情報に基づいてテクスチャの追跡レイアウトを更新
            UpdateAttachmentLayoutsAfterRenderPass();
        }
    }

    void VulkanCommandList::UpdateAttachmentLayoutsAfterRenderPass()
    {
        if (!m_activeRenderPass || !m_activeFramebuffer)
        {
            return;
        }

        const auto &rpDesc = m_activeRenderPass->GetDesc();

        // カラーアタッチメントのレイアウトを更新
        uint32_t colorCount = m_activeFramebuffer->GetColorAttachmentCount();
        for (uint32_t i = 0; i < rpDesc.colorAttachments.size() && i < colorCount; ++i)
        {
            auto colorTarget = m_activeFramebuffer->GetColorAttachment(i);
            if (colorTarget)
            {
                auto vkTexture = DynamicPointerCast<VulkanTexture>(colorTarget);
                if (vkTexture)
                {
                    vk::ImageLayout finalLayout = m_barrierTracker.ResourceStateToImageLayout(
                        rpDesc.colorAttachments[i].finalState);
                    vkTexture->SetVkImageLayout(finalLayout);
                }
            }
        }

        // デプスアタッチメントのレイアウトを更新
        if (rpDesc.hasDepthStencil && m_activeFramebuffer->HasDepthStencilAttachment())
        {
            auto depthTarget = m_activeFramebuffer->GetDepthStencilAttachment();
            if (depthTarget)
            {
                auto vkTexture = DynamicPointerCast<VulkanTexture>(depthTarget);
                if (vkTexture)
                {
                    vk::ImageLayout finalLayout = m_barrierTracker.ResourceStateToImageLayout(
                        rpDesc.depthStencilAttachment.finalState);
                    vkTexture->SetVkImageLayout(finalLayout);
                }
            }
        }

        m_activeRenderPass.reset();
        m_activeFramebuffer.reset();
    }

    void VulkanCommandList::SetViewport(const Viewport &viewport)
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

    void VulkanCommandList::SetScissor(const ScissorRect &scissor)
    {
        vk::Rect2D vkScissor;
        vkScissor.offset = vk::Offset2D{scissor.left, scissor.top};
        vkScissor.extent = vk::Extent2D{
            static_cast<uint32_t>(scissor.right - scissor.left),
            static_cast<uint32_t>(scissor.bottom - scissor.top)};

        m_commandBuffer.setScissor(0, 1, &vkScissor);
    }

    void VulkanCommandList::SetPipeline(PipelinePtr pipeline)
    {
        auto vkPipeline = DynamicPointerCast<VulkanPipeline>(pipeline);
        if (!vkPipeline)
        {
            throw std::runtime_error("無効なパイプラインです");
        }

        vk::PipelineBindPoint bindPoint = vkPipeline->IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;

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

    void VulkanCommandList::SetIndexBuffer(BufferPtr buffer, uint64_t offset, IndexType type)
    {
        auto vkBuffer = DynamicPointerCast<VulkanBuffer>(buffer);
        if (!vkBuffer)
        {
            throw std::runtime_error("無効なバッファです");
        }

        m_currentIndexBuffer = buffer;
        m_currentIndexBufferOffset = offset;

        const vk::IndexType vkIndexType = type == IndexType::Uint16 ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
        m_commandBuffer.bindIndexBuffer(vkBuffer->GetVkBuffer(), offset, vkIndexType);
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

        vk::PipelineBindPoint bindPoint = vkPipeline->IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;

        vk::DescriptorSet descSet = vkDescSet->GetVkDescriptorSet();
        m_commandBuffer.bindDescriptorSets(
            bindPoint,
            vkPipeline->GetVkPipelineLayout(),
            slot,
            1,
            &descSet,
            0,
            nullptr);
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

    void VulkanCommandList::DrawIndexedIndirect(BufferPtr indirectBuffer, uint64_t offset,
                                                uint32_t drawCount, uint32_t stride)
    {
        auto vkBuffer = DynamicPointerCast<VulkanBuffer>(indirectBuffer);
        if (!vkBuffer)
        {
            throw std::runtime_error("DrawIndexedIndirect: 無効な間接バッファです");
        }

        m_commandBuffer.drawIndexedIndirect(vkBuffer->GetVkBuffer(), offset, drawCount, stride);
    }

    void VulkanCommandList::DrawIndexedIndirectCount(BufferPtr indirectBuffer, uint64_t indirectOffset,
                                                     BufferPtr countBuffer, uint64_t countOffset,
                                                     uint32_t maxDrawCount, uint32_t stride)
    {
        auto vkIndirect = DynamicPointerCast<VulkanBuffer>(indirectBuffer);
        if (!vkIndirect)
        {
            throw std::runtime_error("DrawIndexedIndirectCount: 無効な間接バッファです");
        }
        auto vkCount = DynamicPointerCast<VulkanBuffer>(countBuffer);
        if (!vkCount)
        {
            throw std::runtime_error("DrawIndexedIndirectCount: 無効なカウントバッファです");
        }

        m_commandBuffer.drawIndexedIndirectCount(
            vkIndirect->GetVkBuffer(), indirectOffset,
            vkCount->GetVkBuffer(), countOffset,
            maxDrawCount, stride);
    }

    void VulkanCommandList::FillBuffer(BufferPtr buffer, uint64_t offset, uint64_t size, uint32_t value)
    {
        auto vkBuffer = DynamicPointerCast<VulkanBuffer>(buffer);
        if (!vkBuffer)
        {
            throw std::runtime_error("FillBuffer: 無効なバッファです");
        }

        m_commandBuffer.fillBuffer(vkBuffer->GetVkBuffer(), offset, size, value);
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
            &region);
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
            &region);
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
            &region);
    }

    void VulkanCommandList::GenerateMipmaps(TexturePtr texture)
    {
        auto vkTexture = DynamicPointerCast<VulkanTexture>(texture);
        if (!vkTexture)
        {
            throw std::runtime_error("無効なテクスチャです");
        }

        uint32_t mipLevels = texture->GetMipLevels();
        if (mipLevels <= 1)
        {
            return;
        }

        vk::Format vkFormat = m_device->ToVkFormat(texture->GetFormat());
        vk::FormatProperties formatProperties = m_device->GetVkPhysicalDevice().getFormatProperties(vkFormat);
        if ((formatProperties.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear) !=
            vk::FormatFeatureFlagBits::eSampledImageFilterLinear)
        {
            throw std::runtime_error("このテクスチャフォーマットは線形ブリットによるミップ生成をサポートしていません");
        }

        uint32_t layerCount = texture->GetArraySize() * (texture->IsCubemap() ? 6u : 1u);
        int32_t mipWidth = static_cast<int32_t>(texture->GetWidth());
        int32_t mipHeight = static_cast<int32_t>(texture->GetHeight());

        vk::Image image = vkTexture->GetVkImage();

        for (uint32_t mipLevel = 1; mipLevel < mipLevels; ++mipLevel)
        {
            vk::ImageMemoryBarrier dstBarrier;
            dstBarrier.srcAccessMask = {};
            dstBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
            dstBarrier.oldLayout = vk::ImageLayout::eUndefined;
            dstBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
            dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            dstBarrier.image = image;
            dstBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            dstBarrier.subresourceRange.baseMipLevel = mipLevel;
            dstBarrier.subresourceRange.levelCount = 1;
            dstBarrier.subresourceRange.baseArrayLayer = 0;
            dstBarrier.subresourceRange.layerCount = layerCount;

            m_commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe,
                vk::PipelineStageFlagBits::eTransfer,
                {},
                0, nullptr,
                0, nullptr,
                1, &dstBarrier);

            vk::ImageMemoryBarrier srcBarrier;
            srcBarrier.srcAccessMask = vk::AccessFlagBits::eMemoryRead;
            srcBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            srcBarrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            srcBarrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            srcBarrier.image = image;
            srcBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            srcBarrier.subresourceRange.baseMipLevel = mipLevel - 1;
            srcBarrier.subresourceRange.levelCount = 1;
            srcBarrier.subresourceRange.baseArrayLayer = 0;
            srcBarrier.subresourceRange.layerCount = layerCount;

            m_commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eAllCommands,
                vk::PipelineStageFlagBits::eTransfer,
                {},
                0, nullptr,
                0, nullptr,
                1, &srcBarrier);

            int32_t nextMipWidth = std::max(1, mipWidth / 2);
            int32_t nextMipHeight = std::max(1, mipHeight / 2);

            vk::ImageBlit blit;
            blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.srcSubresource.mipLevel = mipLevel - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = layerCount;
            blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
            blit.dstOffsets[1] = vk::Offset3D{nextMipWidth, nextMipHeight, 1};
            blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            blit.dstSubresource.mipLevel = mipLevel;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = layerCount;

            m_commandBuffer.blitImage(
                image, vk::ImageLayout::eTransferSrcOptimal,
                image, vk::ImageLayout::eTransferDstOptimal,
                1, &blit,
                vk::Filter::eLinear);

            vk::ImageMemoryBarrier restoreSrcBarrier;
            restoreSrcBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
            restoreSrcBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            restoreSrcBarrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
            restoreSrcBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            restoreSrcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            restoreSrcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            restoreSrcBarrier.image = image;
            restoreSrcBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            restoreSrcBarrier.subresourceRange.baseMipLevel = mipLevel - 1;
            restoreSrcBarrier.subresourceRange.levelCount = 1;
            restoreSrcBarrier.subresourceRange.baseArrayLayer = 0;
            restoreSrcBarrier.subresourceRange.layerCount = layerCount;

            vk::ImageMemoryBarrier promoteDstBarrier;
            promoteDstBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            promoteDstBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            promoteDstBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            promoteDstBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            promoteDstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            promoteDstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            promoteDstBarrier.image = image;
            promoteDstBarrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            promoteDstBarrier.subresourceRange.baseMipLevel = mipLevel;
            promoteDstBarrier.subresourceRange.levelCount = 1;
            promoteDstBarrier.subresourceRange.baseArrayLayer = 0;
            promoteDstBarrier.subresourceRange.layerCount = layerCount;

            std::array<vk::ImageMemoryBarrier, 2> restoreBarriers = {restoreSrcBarrier, promoteDstBarrier};

            m_commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eFragmentShader,
                {},
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(restoreBarriers.size()), restoreBarriers.data());

            mipWidth = nextMipWidth;
            mipHeight = nextMipHeight;
        }

        vkTexture->SetVkImageLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
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
            0, nullptr);
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

        const uint32_t totalMipLevels = vkTexture->GetMipLevels();
        const uint32_t totalArrayLayers =
            vkTexture->GetArraySize() * (vkTexture->IsCubemap() ? 6u : 1u);
        const uint32_t resolvedMipCount =
            mipCount == 0 && mipLevel <= totalMipLevels
                ? totalMipLevels - mipLevel
                : mipCount;
        const uint32_t resolvedArrayCount =
            arrayCount == 0 && arrayIndex <= totalArrayLayers
                ? totalArrayLayers - arrayIndex
                : arrayCount;
        const bool bCoversWholeTexture =
            mipLevel == 0u &&
            arrayIndex == 0u &&
            resolvedMipCount == totalMipLevels &&
            resolvedArrayCount == totalArrayLayers;
        const bool bPreserveGeneralLayout =
            !bCoversWholeTexture &&
            beforeState == ResourceState::UnorderedAccess &&
            afterState == ResourceState::ShaderResource &&
            (vkTexture->GetUsage() & ResourceUsage::UnorderedAccess) != ResourceUsage::None &&
            vkTexture->GetVkImageLayout() == vk::ImageLayout::eGeneral;
        if (bPreserveGeneralLayout)
        {
            barrier.oldLayout = vk::ImageLayout::eGeneral;
            barrier.newLayout = vk::ImageLayout::eGeneral;
        }

        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = vkTexture->GetVkImage();
        barrier.subresourceRange.aspectMask = GetBarrierAspectMask(*vkTexture);
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
            1, &barrier);

        if (bCoversWholeTexture)
        {
            vkTexture->SetVkImageLayout(barrier.newLayout);
        }
    }

    void VulkanCommandList::OptimizedBufferBarrier(VulkanBuffer *buffer, ResourceState newState, uint64_t offset, uint64_t size)
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
            0, nullptr);
    }

    void VulkanCommandList::OptimizedTextureBarrier(VulkanTexture *texture, ResourceState newState, const vk::ImageSubresourceRange &subresourceRange)
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
            1, &barrier);

        texture->SetVkImageLayout(barrier.newLayout);
    }

    vk::Pipeline VulkanCommandList::GetOrCreateGraphicsPipeline(const PipelineStateCache::GraphicsPipelineCacheKey &key)
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

    vk::Pipeline VulkanCommandList::GetOrCreateComputePipeline(const PipelineStateCache::ComputePipelineCacheKey &key)
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

    void VulkanCommandList::SavePipelineCache(const NorvesLib::Core::Container::String & /*filePath*/)
    {
        // 実装予定
    }

    void VulkanCommandList::LoadPipelineCache(const NorvesLib::Core::Container::String & /*filePath*/)
    {
        // 実装予定
    }

    void VulkanCommandList::ResetResourceBarriers()
    {
        m_barrierTracker.bufferStates.clear();
        m_barrierTracker.imageStates.clear();
    }

#if NORVES_ENABLE_STATS
    void VulkanCommandList::CreateTimestampQueryPool()
    {
        auto queueFamilies = m_device->GetVkPhysicalDevice().getQueueFamilyProperties();
        const uint32_t graphicsQueueFamilyIndex = m_device->GetGraphicsQueueFamilyIndex();
        if (graphicsQueueFamilyIndex >= queueFamilies.size())
        {
            return;
        }

        auto deviceProperties = m_device->GetVkPhysicalDevice().getProperties();
        m_timestampPeriodNs = deviceProperties.limits.timestampPeriod;
        m_TimestampValidBits = queueFamilies[graphicsQueueFamilyIndex].timestampValidBits;
        m_bTimestampSupported =
            m_TimestampValidBits > 0u &&
            m_TimestampValidBits <= 64u &&
            m_timestampPeriodNs > 0.0f &&
            std::isfinite(m_timestampPeriodNs);

        if (!m_bTimestampSupported)
        {
            return;
        }

        vk::QueryPoolCreateInfo queryPoolInfo;
        queryPoolInfo.queryType = vk::QueryType::eTimestamp;
        queryPoolInfo.queryCount =
            MAX_COMMAND_BUFFERS * MaximumGPUTimestampScopesPerFrame * 2u;

        auto result = m_device->GetVkDevice().createQueryPool(queryPoolInfo);
        if (result.result != vk::Result::eSuccess)
        {
            m_bTimestampSupported = false;
            return;
        }

        m_timestampQueryPool = result.value;
    }

    void VulkanCommandList::DestroyTimestampQueryPool()
    {
        if (m_timestampQueryPool)
        {
            m_device->GetVkDevice().destroyQueryPool(m_timestampQueryPool);
            m_timestampQueryPool = nullptr;
        }

        m_bTimestampSupported = false;
        m_bTimestampFrameActive = false;
        m_bLegacyPrivateTimestampFrame = false;
        m_LegacyGPUTimestampScope = {};
        m_CompletedGPUTimestampResults.clear();
        for (Detail::GPUTimestampFrameBatch& batch : m_TimestampFrameBatches)
        {
            Detail::ClearGPUTimestampFrameBatch(batch);
        }
    }

    void VulkanCommandList::ResolveGPUTimestampResultsForCurrentSlot()
    {
        if (!m_bTimestampSupported)
        {
            return;
        }

        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[m_currentFrameIndex];
        if (batch.State != Detail::GPUTimestampFrameState::Completed)
        {
            return;
        }

        if (batch.ScopeCount == 0u)
        {
            Detail::ClearGPUTimestampFrameBatch(batch);
            return;
        }

        struct TimestampQueryReadback
        {
            uint64_t Value = 0u;
            uint64_t Availability = 0u;
        };
        FixedArray<TimestampQueryReadback, MaximumGPUTimestampScopesPerFrame * 2u> timestamps = {};
        const uint32_t queryCount = batch.ScopeCount * 2u;
        const vk::Result result = m_device->GetVkDevice().getQueryPoolResults(
            m_timestampQueryPool,
            GetTimestampQueryBaseIndex(m_currentFrameIndex),
            queryCount,
            sizeof(TimestampQueryReadback) * queryCount,
            timestamps.data(),
            sizeof(TimestampQueryReadback),
            vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);

        bool bAllQueriesAvailable = result == vk::Result::eSuccess;
        if (bAllQueriesAvailable)
        {
            for (uint32_t queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
            {
                if (timestamps[queryIndex].Availability == 0u)
                {
                    bAllQueriesAvailable = false;
                    break;
                }
            }
        }

        if (Detail::ShouldCarryGPUTimestampBatch(result, bAllQueriesAvailable))
        {
            return;
        }

        if (result != vk::Result::eSuccess)
        {
            ++m_GPUTimestampResolveErrorCount;
            NORVES_LOG_WARNING("Vulkan", "GPU timestamp query resolve failed (%d)", static_cast<int>(result));
        }

        for (uint32_t scopeIndex = 0u; scopeIndex < batch.ScopeCount; ++scopeIndex)
        {
            const Detail::GPUTimestampScopeRecord& scope = batch.Scopes[scopeIndex];
            const TimestampQueryReadback& begin = timestamps[scopeIndex * 2u];
            const TimestampQueryReadback& end = timestamps[scopeIndex * 2u + 1u];
            GPUTimestampResult completed;
            completed.FrameNumber = batch.FrameNumber;
            completed.ScopeName = scope.Name;

            const bool bAvailable = result == vk::Result::eSuccess &&
                                    begin.Availability != 0u &&
                                    end.Availability != 0u;
            if (scope.bClosed && bAvailable)
            {
                const uint64_t ticks = Detail::CalculateGPUTimestampTicks(
                    begin.Value,
                    end.Value,
                    m_TimestampValidBits);
                const double durationMs =
                    static_cast<double>(ticks) * static_cast<double>(m_timestampPeriodNs) /
                    1000000.0;
                completed.DurationMs = static_cast<float>(durationMs);
                completed.bValid = std::isfinite(durationMs) &&
                                   std::isfinite(completed.DurationMs);
            }
            if (scope.bLegacy && completed.bValid)
            {
                m_lastGPUTimestampDurationMs = completed.DurationMs;
            }
            m_CompletedGPUTimestampResults.push_back(completed);
        }

        Detail::ClearGPUTimestampFrameBatch(batch);
    }

    void VulkanCommandList::PrepareGPUTimestampSlotForRecording()
    {
        if (!m_bTimestampSupported)
        {
            return;
        }
        Detail::GPUTimestampFrameBatch& batch = m_TimestampFrameBatches[m_currentFrameIndex];
        if (batch.State != Detail::GPUTimestampFrameState::Empty)
        {
            return;
        }
        m_commandBuffer.resetQueryPool(
            m_timestampQueryPool,
            GetTimestampQueryBaseIndex(m_currentFrameIndex),
            MaximumGPUTimestampScopesPerFrame * 2u);
        batch.bQueriesReset = true;
    }

    uint32_t VulkanCommandList::GetTimestampQueryBaseIndex(uint32_t frameSlotIndex,
                                                           uint32_t scopeIndex) const
    {
        return frameSlotIndex * MaximumGPUTimestampScopesPerFrame * 2u +
               scopeIndex * 2u;
    }
#endif

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
        m_activeRenderPass.reset();
        m_activeFramebuffer.reset();

        m_commandBuffer.reset({});
    }

    void VulkanCommandList::CreateDescriptorPool()
    {
        FixedArray<vk::DescriptorPoolSize, 5> poolSizes = {{{vk::DescriptorType::eUniformBuffer, MAX_DESCRIPTORS_PER_TYPE},
                                                            {vk::DescriptorType::eSampledImage, MAX_DESCRIPTORS_PER_TYPE},
                                                            {vk::DescriptorType::eSampler, MAX_DESCRIPTORS_PER_TYPE},
                                                            {vk::DescriptorType::eStorageBuffer, MAX_DESCRIPTORS_PER_TYPE},
                                                            {vk::DescriptorType::eStorageImage, MAX_DESCRIPTORS_PER_TYPE}}};

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
        for (auto &pool : m_descriptorPools)
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

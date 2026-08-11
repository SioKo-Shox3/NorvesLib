#include "Rendering/RenderingCoordinator.h"
#include "Rendering/RenderWorld.h"
#include "RHI/GPUTimestamp.h"
#include "RHI/ICommandList.h"
#include "RHI/Vulkan/VulkanCommandList.h"

#include <cassert>
#include <initializer_list>
#include <iostream>
#include <type_traits>
#include <utility>

namespace
{
    namespace Container = NorvesLib::Core::Container;
    namespace Rendering = NorvesLib::Core::Rendering;
    namespace RHI = NorvesLib::RHI;

    class FakeUnsupportedCommandList : public RHI::ICommandList
    {
    public:
        void Begin() override {}
        void End() override {}
        void Submit(bool = false) override {}
        void BeginRenderPass(RHI::RenderPassPtr, RHI::FramebufferPtr) override {}
        void EndRenderPass() override {}
        void SetViewport(const RHI::Viewport&) override {}
        void SetScissor(const RHI::ScissorRect&) override {}
        void SetPipeline(RHI::PipelinePtr) override {}
        void SetVertexBuffer(RHI::BufferPtr, uint64_t = 0, uint32_t = 0) override {}
        void SetIndexBuffer(RHI::BufferPtr, uint64_t = 0, RHI::IndexType = RHI::IndexType::Uint32) override {}
        void SetConstantBuffer(RHI::BufferPtr, uint32_t, RHI::ShaderStage) override {}
        void SetTexture(RHI::TexturePtr, uint32_t, RHI::ShaderStage) override {}
        void SetSampler(RHI::SamplerPtr, uint32_t, RHI::ShaderStage) override {}
        void SetDescriptorSet(RHI::DescriptorSetPtr, uint32_t = 0) override {}
        void DrawIndexed(uint32_t, uint32_t = 0, int32_t = 0) override {}
        void Draw(uint32_t, uint32_t = 0) override {}
        void DrawIndexedInstanced(uint32_t, uint32_t, uint32_t = 0, int32_t = 0, uint32_t = 0) override {}
        void DrawInstanced(uint32_t, uint32_t, uint32_t = 0, uint32_t = 0) override {}
        void DrawIndexedIndirect(RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferPtr, uint64_t, RHI::BufferPtr, uint64_t, uint32_t, uint32_t) override {}
        void FillBuffer(RHI::BufferPtr, uint64_t, uint64_t, uint32_t) override {}
        void Dispatch(uint32_t, uint32_t, uint32_t) override {}
        void CopyBuffer(RHI::BufferPtr, RHI::BufferPtr, uint64_t = 0, uint64_t = 0, uint64_t = 0) override {}
        void CopyBufferToTexture(RHI::BufferPtr, RHI::TexturePtr, uint32_t, uint32_t, uint64_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void CopyTextureToBuffer(RHI::TexturePtr, RHI::BufferPtr, uint32_t, uint32_t, uint64_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void CopyTexture(RHI::TexturePtr, RHI::TexturePtr, uint32_t, uint32_t, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override {}
        void GenerateMipmaps(RHI::TexturePtr) override {}
        void BufferBarrier(RHI::BufferPtr, RHI::ResourceState, RHI::ResourceState, uint64_t = 0, uint64_t = 0) override {}
        void TextureBarrier(RHI::TexturePtr, RHI::ResourceState, RHI::ResourceState, uint32_t = 0, uint32_t = 0, uint32_t = 0, uint32_t = 0) override {}
    };

    class FakeSubmissionCommandList final : public FakeUnsupportedCommandList
    {
    public:
        explicit FakeSubmissionCommandList(
            Container::VariableArray<Container::String>* events = nullptr)
            : Events(events)
        {
        }

        void CommitGPUTimestampSubmission(uint32_t frameSlotIndex,
                                          uint64_t submissionSerial) override
        {
            if (Events)
            {
                Events->push_back("commit");
            }
            ++CommitCount;
            LastFrameSlotIndex = frameSlotIndex;
            LastSubmissionSerial = submissionSerial;
        }

        void AbortGPUTimestampFrame(uint32_t frameSlotIndex) noexcept override
        {
            if (Events)
            {
                Events->push_back("abort");
            }
            ++AbortCount;
            LastFrameSlotIndex = frameSlotIndex;
        }

        Container::VariableArray<Container::String>* Events = nullptr;
        uint32_t CommitCount = 0u;
        uint32_t AbortCount = 0u;
        uint32_t LastFrameSlotIndex = UINT32_MAX;
        uint64_t LastSubmissionSerial = 0u;
    };

    void AssertUnsupportedDefaultsClearStaleOutput()
    {
        FakeUnsupportedCommandList commandList;
        assert(commandList.GetMaximumGPUTimestampScopesPerFrame() == 0u);
        assert(!commandList.BeginGPUTimestampScope("Pass").IsValid());
        commandList.EndGPUTimestampFrame();
        commandList.CommitGPUTimestampSubmission(0u, 1u);
        commandList.NotifyGPUTimestampFrameSlotCompleted(0u, 1u);
        commandList.AbortGPUTimestampFrame(0u);

        Container::VariableArray<RHI::GPUTimestampResult> results;
        results.push_back({});
        commandList.ConsumeCompletedGPUTimestampResults(results);
        assert(results.empty());
    }

    void AssertSupportObservationIsFalseBeforeInitialization()
    {
        Rendering::RenderingCoordinator coordinator;
        Rendering::RenderWorld renderWorld;
        assert(!coordinator.SupportsGPUTimings());
        assert(!renderWorld.SupportsGPUTimings());

        Container::VariableArray<Rendering::RenderPassGPUTiming> timings;
        timings.push_back({});
        uint64_t droppedFrameCount = 99u;
        assert(!renderWorld.TryConsumeCompletedGPUTimings(timings, droppedFrameCount));
        assert(timings.empty());
        assert(droppedFrameCount == 0u);
    }

    void AssertTimestampBatchStateMachine()
    {
        RHI::Vulkan::Detail::GPUTimestampFrameBatch batch;
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Empty);
        assert(RHI::Vulkan::Detail::TryBeginGPUTimestampFrame(batch, 77u, true));
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Recording);

        RHI::GPUTimestampScopeHandle handle;
        Container::String callerOwnedName("CopiedPass");
        assert(RHI::Vulkan::Detail::TryBeginGPUTimestampScope(
            batch, 0u, callerOwnedName.c_str(), handle));
        callerOwnedName = "Changed";
        assert(batch.Scopes[0].Name == "CopiedPass");
        assert(!RHI::Vulkan::Detail::TryEndGPUTimestampScope(
            batch, {1u, handle.ScopeIndex, handle.FrameNumber}));
        assert(RHI::Vulkan::Detail::TryEndGPUTimestampScope(batch, handle));
        assert(!RHI::Vulkan::Detail::TryEndGPUTimestampScope(batch, handle));
        assert(RHI::Vulkan::Detail::TryEndGPUTimestampFrame(batch));
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Recorded);
        assert(!RHI::Vulkan::Detail::TryCommitGPUTimestampSubmission(batch, 0u));
        assert(RHI::Vulkan::Detail::TryCommitGPUTimestampSubmission(batch, 9u));
        assert(!RHI::Vulkan::Detail::TryCommitGPUTimestampSubmission(batch, 10u));
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Submitted);
        assert(!RHI::Vulkan::Detail::TryNotifyGPUTimestampFrameCompleted(batch, 8u));
        assert(RHI::Vulkan::Detail::TryNotifyGPUTimestampFrameCompleted(batch, 9u));
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Completed);
        RHI::Vulkan::Detail::AbortGPUTimestampFrameBatch(batch);
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Completed);
        RHI::Vulkan::Detail::ClearGPUTimestampFrameBatch(batch);
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Empty);
    }

    void AssertTimestampBatchClearRetainsStorageAndIsNoexcept()
    {
        using Batch = RHI::Vulkan::Detail::GPUTimestampFrameBatch;
        using Guard = RHI::Vulkan::Detail::GPUTimestampSubmissionGuard;
        static_assert(noexcept(RHI::Vulkan::Detail::ClearGPUTimestampFrameBatch(
            std::declval<Batch&>())));
        static_assert(noexcept(RHI::Vulkan::Detail::AbortGPUTimestampFrameBatch(
            std::declval<Batch&>())));
        static_assert(std::is_nothrow_destructible_v<Guard>);

        Batch batch;
        auto* clearStorage = batch.Scopes.data();
        batch.State = RHI::Vulkan::Detail::GPUTimestampFrameState::Completed;
        batch.FrameNumber = 73u;
        batch.SubmissionSerial = 19u;
        batch.FrameSlotIndex = 2u;
        batch.ScopeCount = RHI::MaximumGPUTimestampScopesPerFrame;
        batch.ClosedScopeCount = RHI::MaximumGPUTimestampScopesPerFrame;
        batch.bQueriesReset = true;
        for (auto& scope : batch.Scopes)
        {
            scope.Name = "AllocatedScopeName";
            scope.bClosed = true;
            scope.bLegacy = true;
        }

        RHI::Vulkan::Detail::ClearGPUTimestampFrameBatch(batch);
        assert(batch.Scopes.data() == clearStorage);
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Empty);
        assert(batch.FrameNumber == 0u);
        assert(batch.SubmissionSerial == 0u);
        assert(batch.FrameSlotIndex == UINT32_MAX);
        assert(batch.ScopeCount == 0u);
        assert(batch.ClosedScopeCount == 0u);
        assert(!batch.bQueriesReset);
        for (const auto& scope : batch.Scopes)
        {
            assert(scope.Name.empty());
            assert(!scope.bClosed);
            assert(!scope.bLegacy);
        }

        assert(RHI::Vulkan::Detail::TryBeginGPUTimestampFrame(batch, 74u, true));
        for (uint32_t index = 0u; index < RHI::MaximumGPUTimestampScopesPerFrame; ++index)
        {
            RHI::GPUTimestampScopeHandle handle;
            assert(RHI::Vulkan::Detail::TryBeginGPUTimestampScope(
                batch,
                1u,
                "AbandonedScopeName",
                handle));
        }
        auto* abortStorage = batch.Scopes.data();
        RHI::Vulkan::Detail::AbortGPUTimestampFrameBatch(batch);
        assert(batch.Scopes.data() == abortStorage);
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Empty);
        assert(batch.ScopeCount == 0u);
        for (const auto& scope : batch.Scopes)
        {
            assert(scope.Name.empty());
            assert(!scope.bClosed);
            assert(!scope.bLegacy);
        }
    }

    void AssertTimestampBatchOverflowAndAbandonment()
    {
        RHI::Vulkan::Detail::GPUTimestampFrameBatch batch;
        assert(RHI::Vulkan::Detail::TryBeginGPUTimestampFrame(batch, 3u, true));
        for (uint32_t index = 0u; index < RHI::MaximumGPUTimestampScopesPerFrame; ++index)
        {
            RHI::GPUTimestampScopeHandle handle;
            assert(RHI::Vulkan::Detail::TryBeginGPUTimestampScope(batch, 2u, "Pass", handle));
            assert(handle.ScopeIndex == index);
            assert(RHI::Vulkan::Detail::TryEndGPUTimestampScope(batch, handle));
        }
        RHI::GPUTimestampScopeHandle overflow;
        assert(!RHI::Vulkan::Detail::TryBeginGPUTimestampScope(batch, 2u, "Overflow", overflow));
        assert(!overflow.IsValid());
        assert(RHI::Vulkan::Detail::TryEndGPUTimestampFrame(batch));
        RHI::Vulkan::Detail::AbortGPUTimestampFrameBatch(batch);
        assert(batch.State == RHI::Vulkan::Detail::GPUTimestampFrameState::Empty);
    }

    void AssertTimestampWrapAndCarryClassification()
    {
        assert(RHI::Vulkan::Detail::CalculateGPUTimestampTicks(250u, 5u, 8u) == 11u);
        assert(RHI::Vulkan::Detail::CalculateGPUTimestampTicks(1u, 0u, 1u) == 1u);
        assert(RHI::Vulkan::Detail::CalculateGPUTimestampTicks(0xFFFFFFFFu, 1u, 32u) == 2u);
        assert(RHI::Vulkan::Detail::CalculateGPUTimestampTicks(
                   0x7FFFFFFFFFFFFFFEu, 1u, 63u) == 3u);
        assert(RHI::Vulkan::Detail::CalculateGPUTimestampTicks(UINT64_MAX, 1u, 64u) == 2u);
        assert(RHI::Vulkan::Detail::ShouldCarryGPUTimestampBatch(vk::Result::eNotReady, true));
        assert(RHI::Vulkan::Detail::ShouldCarryGPUTimestampBatch(vk::Result::eSuccess, false));
        assert(!RHI::Vulkan::Detail::ShouldCarryGPUTimestampBatch(vk::Result::eSuccess, true));
        assert(!RHI::Vulkan::Detail::ShouldCarryGPUTimestampBatch(vk::Result::eErrorDeviceLost, true));
    }

    enum class SubmissionFailureStage : uint8_t
    {
        None,
        SerialAllocation,
        FenceReset,
        QueueSubmit
    };

    bool EventsEqual(const Container::VariableArray<Container::String>& actual,
                     std::initializer_list<const char*> expected)
    {
        if (actual.size() != expected.size())
        {
            return false;
        }
        uint32_t index = 0u;
        for (const char* value : expected)
        {
            if (actual[index++] != value)
            {
                return false;
            }
        }
        return true;
    }

    RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus RunSubmissionSequence(
        bool bResetFence,
        SubmissionFailureStage failureStage,
        FakeSubmissionCommandList& commandList,
        Container::VariableArray<Container::String>& events,
        uint64_t& submittedSerial)
    {
        return RHI::Vulkan::Detail::ExecuteGPUTimestampSubmissionSequence(
            &commandList,
            2u,
            bResetFence,
            [&](uint64_t& outSerial)
            {
                events.push_back("serial");
                if (failureStage == SubmissionFailureStage::SerialAllocation)
                {
                    return false;
                }
                outSerial = 41u;
                return true;
            },
            [&]()
            {
                events.push_back("fence-reset");
                return failureStage != SubmissionFailureStage::FenceReset;
            },
            [&]()
            {
                events.push_back("queue-submit");
                return failureStage != SubmissionFailureStage::QueueSubmit;
            },
            submittedSerial);
    }

    void AssertSwapChainSubmissionSequenceUsesProductionHelper()
    {
        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 99u;
            const auto status = RunSubmissionSequence(
                true,
                SubmissionFailureStage::SerialAllocation,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::SerialAllocationFailed);
            assert(submittedSerial == 0u);
            assert(EventsEqual(events, {"serial", "abort"}));
            assert(commandList.CommitCount == 0u);
            assert(commandList.AbortCount == 1u);
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 0u;
            const auto status = RunSubmissionSequence(
                true,
                SubmissionFailureStage::FenceReset,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::FenceResetFailed);
            assert(EventsEqual(events, {"serial", "fence-reset", "abort"}));
            assert(commandList.CommitCount == 0u);
            assert(commandList.AbortCount == 1u);
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 0u;
            const auto status = RunSubmissionSequence(
                true,
                SubmissionFailureStage::QueueSubmit,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::QueueSubmitFailed);
            assert(EventsEqual(events, {"serial", "fence-reset", "queue-submit", "abort"}));
            assert(commandList.CommitCount == 0u);
            assert(commandList.AbortCount == 1u);
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 0u;
            const auto status = RunSubmissionSequence(
                true,
                SubmissionFailureStage::None,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::Success);
            assert(submittedSerial == 41u);
            assert(EventsEqual(events, {"serial", "fence-reset", "queue-submit", "commit"}));
            assert(commandList.CommitCount == 1u);
            assert(commandList.AbortCount == 0u);

            events.push_back("presentation-failed");
            assert(EventsEqual(events, {
                "serial", "fence-reset", "queue-submit", "commit", "presentation-failed"}));
            assert(commandList.AbortCount == 0u);
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            {
                RHI::Vulkan::Detail::GPUTimestampSubmissionGuard guard(
                    &commandList,
                    2u);
            }
            assert(EventsEqual(events, {"abort"}));
            assert(commandList.CommitCount == 0u);
            assert(commandList.AbortCount == 1u);
        }
    }

    void AssertDirectSubmissionSequenceUsesProductionHelper()
    {
        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 99u;
            const auto status = RunSubmissionSequence(
                false,
                SubmissionFailureStage::SerialAllocation,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::SerialAllocationFailed);
            assert(submittedSerial == 0u);
            assert(EventsEqual(events, {"serial", "abort"}));
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 0u;
            const auto status = RunSubmissionSequence(
                false,
                SubmissionFailureStage::QueueSubmit,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::QueueSubmitFailed);
            assert(EventsEqual(events, {"serial", "queue-submit", "abort"}));
        }

        {
            Container::VariableArray<Container::String> events;
            FakeSubmissionCommandList commandList(&events);
            uint64_t submittedSerial = 0u;
            const auto status = RunSubmissionSequence(
                false,
                SubmissionFailureStage::None,
                commandList,
                events,
                submittedSerial);
            assert(status == RHI::Vulkan::Detail::GPUTimestampSubmissionSequenceStatus::Success);
            assert(submittedSerial == 41u);
            assert(EventsEqual(events, {"serial", "queue-submit", "commit"}));
            assert(commandList.CommitCount == 1u);
            assert(commandList.AbortCount == 0u);
        }
    }

    Rendering::RenderPassGPUTiming MakeTiming(uint64_t frameNumber, const char* name)
    {
        Rendering::RenderPassGPUTiming timing;
        timing.FrameNumber = frameNumber;
        timing.PassName = name;
        timing.DurationMs = 1.0f;
        timing.bValid = true;
        return timing;
    }

    void AssertMailboxAppendConsumeAndWholeFrameDrop()
    {
        Rendering::Detail::GPUTimingMailbox mailbox;
        Container::VariableArray<Rendering::RenderPassGPUTiming> firstBatch;
        firstBatch.push_back(MakeTiming(10u, "FrameGPU"));
        firstBatch.push_back(MakeTiming(10u, "GBufferPass"));
        mailbox.Append(firstBatch);

        Container::VariableArray<Rendering::RenderPassGPUTiming> secondBatch;
        secondBatch.push_back(MakeTiming(11u, "LightingPass"));
        mailbox.Append(secondBatch);

        Container::VariableArray<Rendering::RenderPassGPUTiming> consumed;
        uint64_t droppedFrameCount = 99u;
        assert(mailbox.Consume(consumed, droppedFrameCount));
        assert(consumed.size() == 3u);
        assert(droppedFrameCount == 0u);
        assert(!mailbox.Consume(consumed, droppedFrameCount));
        assert(consumed.empty());
        assert(droppedFrameCount == 0u);

        Container::VariableArray<Rendering::RenderPassGPUTiming> overflowing;
        overflowing.push_back(MakeTiming(20u, "OldA"));
        overflowing.push_back(MakeTiming(20u, "OldB"));
        for (uint32_t index = 0u; index < Rendering::MaximumPublishedGPUTimingSamples - 1u; ++index)
        {
            overflowing.push_back(MakeTiming(21u, "New"));
        }
        mailbox.Append(overflowing);
        assert(mailbox.Consume(consumed, droppedFrameCount));
        assert(consumed.size() == Rendering::MaximumPublishedGPUTimingSamples - 1u);
        assert(consumed.front().FrameNumber == 21u);
        assert(droppedFrameCount == 1u);
    }

    void AssertMailboxDropsNumericallyOldestWholeFrameAfterReverseArrival()
    {
        Rendering::Detail::GPUTimingMailbox mailbox;

        Container::VariableArray<Rendering::RenderPassGPUTiming> newerFrame;
        for (uint32_t index = 0u;
             index < Rendering::MaximumPublishedGPUTimingSamples - 2u;
             ++index)
        {
            newerFrame.push_back(MakeTiming(21u, "Newer"));
        }
        mailbox.Append(newerFrame);

        Container::VariableArray<Rendering::RenderPassGPUTiming> carriedOldSample;
        carriedOldSample.push_back(MakeTiming(20u, "OldA"));
        mailbox.Append(carriedOldSample);

        Container::VariableArray<Rendering::RenderPassGPUTiming> newestFrame;
        newestFrame.push_back(MakeTiming(22u, "Newest"));
        mailbox.Append(newestFrame);

        carriedOldSample.clear();
        carriedOldSample.push_back(MakeTiming(20u, "OldB"));
        mailbox.Append(carriedOldSample);

        Container::VariableArray<Rendering::RenderPassGPUTiming> consumed;
        uint64_t droppedFrameCount = 0u;
        assert(mailbox.Consume(consumed, droppedFrameCount));
        assert(droppedFrameCount == 1u);
        assert(consumed.size() == Rendering::MaximumPublishedGPUTimingSamples - 1u);
        for (const Rendering::RenderPassGPUTiming& timing : consumed)
        {
            assert(timing.FrameNumber != 20u);
        }
    }
}

int main()
{
    AssertUnsupportedDefaultsClearStaleOutput();
    AssertSupportObservationIsFalseBeforeInitialization();
    AssertTimestampBatchStateMachine();
    AssertTimestampBatchClearRetainsStorageAndIsNoexcept();
    AssertTimestampBatchOverflowAndAbandonment();
    AssertTimestampWrapAndCarryClassification();
    AssertSwapChainSubmissionSequenceUsesProductionHelper();
    AssertDirectSubmissionSequenceUsesProductionHelper();
    AssertMailboxAppendConsumeAndWholeFrameDrop();
    AssertMailboxDropsNumericallyOldestWholeFrameAfterReverseArrival();
    std::cout << "RenderingCoordinatorGPUTimestampPublicationTest passed\n";
    return 0;
}

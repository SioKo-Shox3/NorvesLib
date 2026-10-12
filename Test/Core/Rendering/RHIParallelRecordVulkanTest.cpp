// 複数のコマンドリストを 1 回で提出する経路（ICommandList::SubmitBatchAndWait。スワップチェーンの EndFrame の複数リストの版と
// 同じ提出の手順と状態）を、実際の Vulkan デバイスで確かめる。
//   - 2 つのスレッドで別々のリストへ「書き込み（render pass のクリア）」と「バリア → 読み出し（コピー）」を記録し、
//     2 本を 1 回で提出すると、読み出した値が書き込んだ色になる。2 本に同じ提出番号が配られ、完了が通知される。
//   - 1 本を中止すると、その束は提出されず（GPU は何も実行しない）、束の残りのリストも中止される。中止したリストは
//     記録し直すまで単独でも提出できない。記録し直した後の提出は、新しく記録した中身だけを実行する。
//   - 直接の提出（Begin/Submit）でも、中止したリストの提出は拒否され、中止の後の Begin は待ち続けずに記録し直せる。
//   - 2 本のそれぞれで取った GPU の時間をパスの名前で合わせると、2 本にまたがる FrameGPU は先頭のリストの開始から
//     末尾のリストの終了までになり、各区間の時間以上で、開始と終了の順が正しい。順の壊れた区間は合わせても無効になる。
//   - どのケースも Vulkan の検証レイヤーのエラーが 0 件。
#include "CoreTypes.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"
#include "RHI/IRenderPass.h"
#include "RHI/IFramebuffer.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <thread>

namespace NorvesLib::RHI::Vulkan
{
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
} // namespace NorvesLib::RHI::Vulkan

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "RHIParallelRecordVulkanTest";
    constexpr uint32_t TextureWidth = 4;
    constexpr uint32_t TextureHeight = 4;
    constexpr uint32_t BytesPerPixel = 4;
    constexpr uint64_t ReadbackBytes = TextureWidth * TextureHeight * BytesPerPixel;
    constexpr uint8_t PrefillByte = 0xCD;
    constexpr int GpuTestSkipReturnCode = 125;

    int g_failures = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_failures;
            std::cerr << "失敗: " << message << '\n';
        }
    }

    bool IsGpuTestSkipForced()
    {
        char* forceSkip = nullptr;
        size_t forceSkipLength = 0;
        if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
        {
            return false;
        }
        const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
        free(forceSkip);
        return bForceSkip;
    }

    int SkipGpuTest(const char* reason)
    {
        std::cout << TestName << " スキップ: " << reason << std::endl;
        return GpuTestSkipReturnCode;
    }

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    struct ClearColor
    {
        uint8_t R = 0;
        uint8_t G = 0;
        uint8_t B = 0;
    };

    // 書き込み側: render pass のクリアで色を書く。ロードの前の layout は Undefined、終わりは ColorAttachment
    struct WriteTarget
    {
        RenderPassPtr RenderPass;
        FramebufferPtr Framebuffer;
    };

    WriteTarget MakeWriteTarget(IDevice& device, const TexturePtr& texture, const ClearColor& color)
    {
        AttachmentDesc attachment;
        attachment.format = Format::R8G8B8A8_UNORM;
        attachment.clearColor[0] = static_cast<float>(color.R) / 255.0f;
        attachment.clearColor[1] = static_cast<float>(color.G) / 255.0f;
        attachment.clearColor[2] = static_cast<float>(color.B) / 255.0f;
        attachment.clearColor[3] = 1.0f;
        attachment.loadOp = AttachmentLoadOp::Clear;
        attachment.storeOp = AttachmentStoreOp::Store;
        attachment.initialState = ResourceState::Undefined;
        attachment.finalState = ResourceState::RenderTarget;

        RenderPassDesc renderPassDesc;
        renderPassDesc.colorAttachments.push_back(attachment);
        renderPassDesc.hasDepthStencil = false;

        WriteTarget target;
        target.RenderPass = device.CreateRenderPass(renderPassDesc);

        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = target.RenderPass;
        framebufferDesc.colorTargets.push_back(texture);
        framebufferDesc.width = TextureWidth;
        framebufferDesc.height = TextureHeight;
        target.Framebuffer = device.CreateFramebuffer(framebufferDesc);
        return target;
    }

    // 共有の layout を書かずに記録する（別のスレッドが同じテクスチャを記録しても、記録の結果が順序に左右されない）
    void RecordWrite(ICommandList& commandList, const WriteTarget& target)
    {
        commandList.SetFrameIndex(0);
        commandList.BeginRecording();
        commandList.SetSharedTextureLayoutWrites(false);
        commandList.BeginRenderPass(target.RenderPass, target.Framebuffer);
        commandList.EndRenderPass();
        commandList.End();
    }

    // 読み出し側: 書き込み側のリストの後に実行される前提で、明示の old/new のバリアを張ってからバッファへコピーする
    void RecordRead(ICommandList& commandList, const TexturePtr& texture, const BufferPtr& readback)
    {
        commandList.SetFrameIndex(0);
        commandList.BeginRecording();
        commandList.SetSharedTextureLayoutWrites(false);
        ExplicitTextureBarrierDesc barrier;
        barrier.BeforeState = ResourceState::RenderTarget;
        barrier.AfterState = ResourceState::CopySource;
        barrier.OldLayout = TextureLayout::ColorAttachment;
        barrier.NewLayout = TextureLayout::TransferSource;
        commandList.TextureBarrierExplicit(texture, barrier);
        commandList.CopyTextureToBuffer(texture, readback, TextureWidth, TextureHeight, 0);
        commandList.End();
    }

    // 2 本を別々のスレッドで同時に記録する
    bool RecordOnTwoThreads(ICommandList& writeList,
                            const WriteTarget& target,
                            ICommandList& readList,
                            const TexturePtr& texture,
                            const BufferPtr& readback)
    {
        bool bWriteFailed = false;
        bool bReadFailed = false;
        std::thread writeThread([&]()
        {
            try
            {
                RecordWrite(writeList, target);
            }
            catch (const std::exception& exception)
            {
                std::cerr << "書き込み側の記録で例外: " << exception.what() << '\n';
                bWriteFailed = true;
            }
        });
        std::thread readThread([&]()
        {
            try
            {
                RecordRead(readList, texture, readback);
            }
            catch (const std::exception& exception)
            {
                std::cerr << "読み出し側の記録で例外: " << exception.what() << '\n';
                bReadFailed = true;
            }
        });
        writeThread.join();
        readThread.join();
        return !bWriteFailed && !bReadFailed;
    }

#if NORVES_ENABLE_STATS
    // GPU の時間の計測は NORVES_ENABLE_STATS の構成（Debug・RelWithDebInfo）だけで有効なので、計測ケースもそこに限る
    constexpr const char* FrameGPUScopeName = "FrameGPU";
    constexpr uint64_t TimestampFrameNumber = 500u;

    // FrameGPU の区間の中に、パスの区間（segmentName）を開いて record を記録する
    template <typename RecordFunction>
    void RecordWithTimestamps(ICommandList& commandList, const char* segmentName, RecordFunction record)
    {
        commandList.SetFrameIndex(0);
        commandList.BeginRecording();
        commandList.SetSharedTextureLayoutWrites(false);
        commandList.BeginGPUTimestampFrame(TimestampFrameNumber);
        const GPUTimestampScopeHandle frameScope = commandList.BeginGPUTimestampScope(FrameGPUScopeName);
        const GPUTimestampScopeHandle segmentScope = commandList.BeginGPUTimestampScope(segmentName);
        record();
        commandList.EndGPUTimestampScope(segmentScope);
        commandList.EndGPUTimestampScope(frameScope);
        commandList.EndGPUTimestampFrame();
        commandList.End();
    }

    const GPUTimestampResult* FindTimestampResult(const Core::Container::VariableArray<GPUTimestampResult>& results,
                                                  const char* scopeName)
    {
        const GPUTimestampResult* found = nullptr;
        for (const GPUTimestampResult& result : results)
        {
            if (result.FrameNumber == TimestampFrameNumber && result.ScopeName == scopeName)
            {
                if (found != nullptr)
                {
                    return nullptr;
                }
                found = &result;
            }
        }
        return found;
    }

    uint64_t TimestampTicks(const GPUTimestampResult& result)
    {
        return CalculateGPUTimestampTickDelta(result.BeginTimestamp, result.EndTimestamp, result.TimestampValidBits);
    }

    // 書き込みのリストと読み出しのリストのそれぞれに FrameGPU を開いて 1 回で提出し、合わせた FrameGPU を確かめる
    void RunCrossListFrameGPUCase(IDevice& device,
                                  const CommandListPtr& writeList,
                                  const WriteTarget& target,
                                  const CommandListPtr& readList,
                                  const TexturePtr& texture,
                                  const BufferPtr& readback)
    {
        Expect(writeList->SupportsGPUTimestamps() && readList->SupportsGPUTimestamps(),
               "Debug の Vulkan のリストは GPU の時間を取れなければならない");
        if (!writeList->SupportsGPUTimestamps() || !readList->SupportsGPUTimestamps())
        {
            return;
        }
        device.WaitIdle();
        // 前のケースの結果を捨てる
        Core::Container::VariableArray<GPUTimestampResult> discarded;
        writeList->ConsumeCompletedGPUTimestampResults(discarded);
        readList->ConsumeCompletedGPUTimestampResults(discarded);

        RecordWithTimestamps(*writeList, "WriteSegment", [&]()
        {
            writeList->BeginRenderPass(target.RenderPass, target.Framebuffer);
            writeList->EndRenderPass();
        });
        RecordWithTimestamps(*readList, "ReadSegment", [&]()
        {
            ExplicitTextureBarrierDesc barrier;
            barrier.BeforeState = ResourceState::RenderTarget;
            barrier.AfterState = ResourceState::CopySource;
            barrier.OldLayout = TextureLayout::ColorAttachment;
            barrier.NewLayout = TextureLayout::TransferSource;
            readList->TextureBarrierExplicit(texture, barrier);
            readList->CopyTextureToBuffer(texture, readback, TextureWidth, TextureHeight, 0);
        });
        const CommandListPtr batch[2] = {writeList, readList};
        const uint64_t serial = writeList->SubmitBatchAndWait(batch, 2u);
        Expect(serial != 0u, "時間を取る 2 本の束の提出は成功しなければならない");

        // 完了を待った束の結果は、記録し直す前に読める
        Core::Container::VariableArray<GPUTimestampResult> perListResults[2];
        writeList->ConsumeCompletedGPUTimestampResults(perListResults[0]);
        readList->ConsumeCompletedGPUTimestampResults(perListResults[1]);
        const GPUTimestampResult* firstFrame = FindTimestampResult(perListResults[0], FrameGPUScopeName);
        const GPUTimestampResult* firstSegment = FindTimestampResult(perListResults[0], "WriteSegment");
        const GPUTimestampResult* lastFrame = FindTimestampResult(perListResults[1], FrameGPUScopeName);
        const GPUTimestampResult* lastSegment = FindTimestampResult(perListResults[1], "ReadSegment");
        const bool bPartsValid = firstFrame && firstSegment && lastFrame && lastSegment &&
                                 firstFrame->bValid && firstSegment->bValid && lastFrame->bValid && lastSegment->bValid &&
                                 firstFrame->TimestampValidBits > 0u && firstFrame->TimestampPeriodNs > 0.0f;
        Expect(bPartsValid, "2 本のそれぞれに FrameGPU とパスの区間が 1 つずつ、有効な時刻で残らなければならない");
        if (!bPartsValid)
        {
            return;
        }

        Core::Container::VariableArray<GPUTimestampResult> merged;
        MergeGPUTimestampResultsAcrossCommandLists(perListResults, 2u, merged);
        Expect(merged.size() == 3u, "合わせた結果は FrameGPU 1 つとパスの区間 2 つでなければならない");
        const GPUTimestampResult* frame = FindTimestampResult(merged, FrameGPUScopeName);
        const GPUTimestampResult* writeSegment = FindTimestampResult(merged, "WriteSegment");
        const GPUTimestampResult* readSegment = FindTimestampResult(merged, "ReadSegment");
        Expect(frame && frame->bValid, "2 本にまたがる FrameGPU は有効でなければならない");
        Expect(writeSegment && readSegment &&
                   writeSegment->DurationMs == firstSegment->DurationMs &&
                   readSegment->DurationMs == lastSegment->DurationMs,
               "パスの区間は記録したリストの値のまま残らなければならない");
        if (!frame || !frame->bValid)
        {
            return;
        }

        const uint32_t validBits = frame->TimestampValidBits;
        const uint64_t halfRange = uint64_t{1} << (validBits - 1u);
        const uint64_t frameTicks = TimestampTicks(*frame);
        // 先頭のリストの開始からの位置で、開始と終了の順を見る
        const uint64_t firstEnd = CalculateGPUTimestampTickDelta(firstFrame->BeginTimestamp, firstFrame->EndTimestamp, validBits);
        const uint64_t lastBegin = CalculateGPUTimestampTickDelta(firstFrame->BeginTimestamp, lastFrame->BeginTimestamp, validBits);
        const uint64_t lastEnd = CalculateGPUTimestampTickDelta(firstFrame->BeginTimestamp, lastFrame->EndTimestamp, validBits);
        Expect(frame->BeginTimestamp == firstFrame->BeginTimestamp,
               "FrameGPU の開始は先頭のリストの開始でなければならない");
        Expect(frame->EndTimestamp == lastFrame->EndTimestamp,
               "FrameGPU の終了は末尾のリストの終了でなければならない（読み出しは書き込みの完了を待つ）");
        Expect(frameTicks < halfRange && lastBegin <= lastEnd && firstEnd <= lastEnd,
               "先頭のリストの開始 <= 末尾のリストの開始 <= 末尾のリストの終了、かつ先頭のリストの終了 <= 末尾のリストの終了でなければならない");
        const GPUTimestampResult* parts[4] = {firstFrame, firstSegment, lastFrame, lastSegment};
        bool bCoversParts = true;
        for (const GPUTimestampResult* part : parts)
        {
            bCoversParts = bCoversParts && TimestampTicks(*part) <= frameTicks && part->DurationMs <= frame->DurationMs;
        }
        Expect(bCoversParts, "2 本にまたがる FrameGPU は各区間の時間以上でなければならない");
        Expect(frame->DurationMs > 0.0f, "2 本にまたがる FrameGPU は 0 より長くなければならない");

        // 順の壊れた区間（後のリストが先頭のリストより前に始まる）は合わせても無効になる
        Core::Container::VariableArray<GPUTimestampResult> swapped[2] = {perListResults[1], perListResults[0]};
        swapped[0][0].BeginTimestamp = firstFrame->BeginTimestamp + 1u;
        swapped[1][0].BeginTimestamp = firstFrame->BeginTimestamp;
        Core::Container::VariableArray<GPUTimestampResult> brokenMerged;
        MergeGPUTimestampResultsAcrossCommandLists(swapped, 2u, brokenMerged);
        const GPUTimestampResult* brokenFrame = FindTimestampResult(brokenMerged, FrameGPUScopeName);
        Expect(brokenFrame && !brokenFrame->bValid,
               "後のリストの区間が先頭のリストの開始より前に始まると、合わせた FrameGPU は無効でなければならない");

        std::cout << "CASE cross_list_frame_gpu frame_ms=" << frame->DurationMs
                  << " first_ms=" << firstFrame->DurationMs
                  << " last_ms=" << lastFrame->DurationMs
                  << " write_ms=" << firstSegment->DurationMs
                  << " read_ms=" << lastSegment->DurationMs << '\n';
    }
#endif

    void FillReadback(IBuffer& readback, uint8_t value)
    {
        void* mapped = readback.Map(0, ReadbackBytes);
        if (mapped != nullptr)
        {
            std::memset(mapped, value, static_cast<size_t>(ReadbackBytes));
            readback.Unmap();
        }
    }

    // 全画素が期待の色（RGBA）か。bExpectPrefill のときは全バイトが PrefillByte のままか
    bool ReadbackMatches(IBuffer& readback, const ClearColor& color, bool bExpectPrefill)
    {
        const void* mapped = readback.Map(0, ReadbackBytes);
        if (mapped == nullptr)
        {
            return false;
        }
        const auto* bytes = static_cast<const uint8_t*>(mapped);
        bool bMatches = true;
        for (uint32_t pixel = 0; pixel < TextureWidth * TextureHeight && bMatches; ++pixel)
        {
            const uint8_t* texel = bytes + pixel * BytesPerPixel;
            const uint8_t expected[4] = {
                bExpectPrefill ? PrefillByte : color.R,
                bExpectPrefill ? PrefillByte : color.G,
                bExpectPrefill ? PrefillByte : color.B,
                bExpectPrefill ? PrefillByte : uint8_t{255}};
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                if (texel[channel] != expected[channel])
                {
                    std::cerr << "画素 " << pixel << " のチャネル " << channel << " が "
                              << static_cast<uint32_t>(texel[channel]) << "（期待 "
                              << static_cast<uint32_t>(expected[channel]) << "）\n";
                    bMatches = false;
                    break;
                }
            }
        }
        readback.Unmap();
        return bMatches;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された");
        }

        VulkanValidationErrorCapture validationCapture;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return SkipGpuTest("Vulkanデバイスを利用できません");
        }

        {
            TextureDesc textureDesc;
            textureDesc.Width = TextureWidth;
            textureDesc.Height = TextureHeight;
            textureDesc.TextureFormat = Format::R8G8B8A8_UNORM;
            textureDesc.Usage = ResourceUsage::RenderTarget | ResourceUsage::TransferSrc;
            textureDesc.DebugName = "RHIParallelRecordColor";
            TexturePtr texture = device->CreateTexture(textureDesc);

            BufferDesc bufferDesc;
            bufferDesc.Size = ReadbackBytes;
            bufferDesc.Usage = ResourceUsage::TransferDst;
            bufferDesc.CPUAccessible = true;
            bufferDesc.DebugName = "RHIParallelRecordReadback";
            BufferPtr readback = device->CreateBuffer(bufferDesc);

            const ClearColor red{255, 0, 0};
            const ClearColor green{0, 255, 0};
            const ClearColor blue{0, 0, 255};
            const WriteTarget redTarget = MakeWriteTarget(*device, texture, red);
            const WriteTarget greenTarget = MakeWriteTarget(*device, texture, green);
            const WriteTarget blueTarget = MakeWriteTarget(*device, texture, blue);

            CommandListPtr writeList = device->CreateCommandList();
            CommandListPtr readList = device->CreateCommandList();
            if (!texture || !readback || !redTarget.Framebuffer || !greenTarget.Framebuffer ||
                !blueTarget.Framebuffer || !writeList || !readList)
            {
                std::cerr << TestName << ": 資源を作れませんでした\n";
                return 1;
            }
            const CommandListPtr batch[2] = {writeList, readList};

            // 1. 2 つのスレッドで記録した 2 本を 1 回で提出する
            FillReadback(*readback, PrefillByte);
            const bool bRecorded = RecordOnTwoThreads(*writeList, redTarget, *readList, texture, readback);
            Expect(bRecorded, "2 つのスレッドでの記録が例外なく終わらなければならない");
            Expect(writeList->GetSubmissionState() == CommandListSubmissionState::Recorded &&
                       readList->GetSubmissionState() == CommandListSubmissionState::Recorded,
                   "記録を終えた 2 本は Recorded でなければならない");
            const uint64_t firstSerial = writeList->SubmitBatchAndWait(batch, 2u);
            Expect(firstSerial != 0u, "2 本の束の提出は成功しなければならない");
            Expect(writeList->GetLastSubmissionSerial() == firstSerial &&
                       readList->GetLastSubmissionSerial() == firstSerial,
                   "束の 2 本に同じ提出番号が配られなければならない");
            Expect(writeList->GetSubmissionState() == CommandListSubmissionState::Idle &&
                       readList->GetSubmissionState() == CommandListSubmissionState::Idle,
                   "完了を待った束の 2 本に完了が通知されなければならない");
            Expect(ReadbackMatches(*readback, red, false),
                   "書き込み → バリア → 読み出しの結果が書き込んだ色（赤）でなければならない");
            std::cout << "CASE two_threads_one_submit serial=" << firstSerial << '\n';

            // 2. 中止の後に古いリストを再利用しない
            FillReadback(*readback, PrefillByte);
            Expect(RecordOnTwoThreads(*writeList, greenTarget, *readList, texture, readback),
                   "中止の前の記録が例外なく終わらなければならない");
            writeList->AbortSubmission();
            Expect(writeList->GetSubmissionState() == CommandListSubmissionState::Aborted,
                   "中止したリストは Aborted でなければならない");
            const uint64_t abortedSerial = writeList->SubmitBatchAndWait(batch, 2u);
            Expect(abortedSerial == 0u, "中止したリストを含む束は提出してはならない");
            Expect(readList->GetSubmissionState() == CommandListSubmissionState::Aborted,
                   "拒否した束の残りのリストも中止されなければならない");
            const CommandListPtr readOnly[1] = {readList};
            Expect(writeList->SubmitBatchAndWait(readOnly, 1u) == 0u,
                   "中止したリストは単独でも提出してはならない");
            device->WaitIdle();
            Expect(ReadbackMatches(*readback, green, true),
                   "中止した束の中身（緑のクリアとコピー）は GPU で実行されてはならない");

            Expect(RecordOnTwoThreads(*writeList, blueTarget, *readList, texture, readback),
                   "中止の後の記録が例外なく終わらなければならない");
            const uint64_t secondSerial = writeList->SubmitBatchAndWait(batch, 2u);
            Expect(secondSerial > firstSerial, "記録し直した束は新しい提出番号で提出されなければならない");
            Expect(writeList->GetLastSubmissionSerial() == secondSerial &&
                       readList->GetLastSubmissionSerial() == secondSerial,
                   "記録し直した束の 2 本に同じ提出番号が配られなければならない");
            Expect(ReadbackMatches(*readback, blue, false),
                   "記録し直した後の提出は新しい中身（青）だけを実行しなければならない");
            std::cout << "CASE abort_then_rerecord serial=" << secondSerial << '\n';

            // 3. 直接の提出（Begin/Submit）でも、中止したリストは提出せず、中止の後の Begin は待ち続けない
            {
                device->WaitIdle();
                FillReadback(*readback, PrefillByte);
                CommandListPtr directList = device->CreateCommandList();
                Expect(directList != nullptr, "直接の提出のリストを作れなければならない");
                if (directList)
                {
                    const ClearColor white{255, 255, 255};
                    const WriteTarget whiteTarget = MakeWriteTarget(*device, texture, white);
                    auto recordDirect = [&](const WriteTarget& target)
                    {
                        directList->Begin();
                        directList->SetSharedTextureLayoutWrites(false);
                        directList->BeginRenderPass(target.RenderPass, target.Framebuffer);
                        directList->EndRenderPass();
                        ExplicitTextureBarrierDesc barrier;
                        barrier.BeforeState = ResourceState::RenderTarget;
                        barrier.AfterState = ResourceState::CopySource;
                        barrier.OldLayout = TextureLayout::ColorAttachment;
                        barrier.NewLayout = TextureLayout::TransferSource;
                        directList->TextureBarrierExplicit(texture, barrier);
                        directList->CopyTextureToBuffer(texture, readback, TextureWidth, TextureHeight, 0);
                        directList->End();
                    };

                    recordDirect(greenTarget);
                    directList->AbortSubmission();
                    bool bSubmitRejected = false;
                    try
                    {
                        directList->Submit(true);
                    }
                    catch (const std::exception&)
                    {
                        bSubmitRejected = true;
                    }
                    Expect(bSubmitRejected, "中止したリストの直接の提出は拒否されなければならない");
                    device->WaitIdle();
                    Expect(ReadbackMatches(*readback, green, true),
                           "中止したリストの中身（緑のクリアとコピー）は GPU で実行されてはならない");

                    // 中止の後の Begin がフェンスの待機で止まらず、記録し直した中身だけが実行される
                    recordDirect(whiteTarget);
                    directList->Submit(true);
                    Expect(ReadbackMatches(*readback, white, false),
                           "中止の後に記録し直した直接の提出は新しい中身（白）を実行しなければならない");
                    std::cout << "CASE direct_abort_then_rerecord rejected=" << (bSubmitRejected ? 1 : 0) << '\n';
                }
            }

#if NORVES_ENABLE_STATS
            // 4. 2 本のそれぞれで取った GPU の時間を合わせ、2 本にまたがる FrameGPU を求める
            RunCrossListFrameGPUCase(*device, writeList, redTarget, readList, texture, readback);
#endif

            device->WaitIdle();
        }

        const uint32_t validationErrorCount = validationCapture.GetHitCount();
        std::cout << "VUID_COUNT=" << validationErrorCount << '\n';
        Expect(validationErrorCount == 0u, "Vulkan の検証エラーが出てはならない");

        std::cout << (g_failures == 0 ? "RESULT=PASS" : "RESULT=FAIL") << '\n';
        return g_failures == 0 ? 0 : 1;
    }
} // namespace

int main()
{
    try
    {
        return RunTest();
    }
    catch (const std::exception& exception)
    {
        std::cerr << TestName << "で例外が出ました: " << exception.what() << '\n';
        return 1;
    }
}

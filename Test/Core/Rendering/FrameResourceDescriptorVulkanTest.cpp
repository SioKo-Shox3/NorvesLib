// フレームの持ち主から記述子セットを借りる仕組み（IDevice::GetOrCreateDescriptorSetLayout・IFrameDescriptorAllocator・
// FrameResourceOwner::AllocateDescriptorSet）を、実際の Vulkan デバイスで確かめる。
//   - 共有の layout: 同じ束縛の形（並べる順が違うだけ・RWBuffer と StructuredBuffer の違いだけを含む）は同じ layout を返し、
//     ステージや要素数が違えば別の layout を返す。
//   - pool を足す道: 小さな容量の pool から容量を超えて借りると pool を足し、借りたセットは全部別のハンドルで書き込める。
//     pool の容量を超える型を持つ layout も、その layout が入る pool を足して借りられる。統計（借りた数・書き込んだ数・
//     pool を足した回数）が合う。
//   - 回収で戻ること: 提出の完了でスロットの pool がまとめて戻り、次のフレームは pool を足さずに同じ数を借りられる。
//     完了していない提出のスロットは戻らず、そのセットは使えるまま。
//   - 世代の拒否: 前のフレームで借りたセットは世代の検査で拒まれ、IsExpired になり、書き込みも拒まれる。
//     記録中でないときは借りられない。
//   - どのケースも Vulkan の検証レイヤーのエラーが 0 件。
// Vulkan デバイスが無い環境では 125（スキップ）を返す。
#include "CoreTypes.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/IBuffer.h"
#include "RHI/IDescriptorSet.h"
#include "Rendering/FrameResourceOwner.h"
#include "RHI/Vulkan/VulkanDescriptorSet.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

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
    using NorvesLib::Core::Container::VariableArray;
    using NorvesLib::Core::Rendering::FrameDescriptorAllocation;
    using NorvesLib::Core::Rendering::FrameResourceOwner;
    using NorvesLib::Core::Rendering::FrameResourceOwners;

    constexpr const char* TestName = "FrameResourceDescriptorVulkanTest";
    constexpr int GpuTestSkipReturnCode = 125;

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

    class VulkanValidationErrorCapture
    {
    public:
        VulkanValidationErrorCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~VulkanValidationErrorCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << std::endl;
        }
        return condition;
    }

    DescriptorSetDesc MakeDesc(std::initializer_list<DescriptorBinding> bindings)
    {
        DescriptorSetDesc desc;
        for (const DescriptorBinding& binding : bindings)
        {
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    uint64_t GetVkHandle(const DescriptorSetPtr& set)
    {
        auto vkSet = DynamicPointerCast<RHI::Vulkan::VulkanDescriptorSet>(set);
        return vkSet ? reinterpret_cast<uint64_t>(static_cast<VkDescriptorSet>(vkSet->GetVkDescriptorSet())) : 0;
    }

    // 共有の layout: 同じ形は同じ layout、違う形は別の layout
    bool RunSharedLayoutCase(IDevice& device)
    {
        const DescriptorSetLayoutPtr layoutA = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::ConstantBuffer, ShaderStage::Pixel, 1},
            {1, ResourceBindType::CombinedImageSampler, ShaderStage::Pixel, 1},
        }));
        const DescriptorSetLayoutPtr layoutReordered = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {1, ResourceBindType::CombinedImageSampler, ShaderStage::Pixel, 1},
            {0, ResourceBindType::ConstantBuffer, ShaderStage::Pixel, 1},
        }));
        const DescriptorSetLayoutPtr layoutOtherStage = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::ConstantBuffer, ShaderStage::All, 1},
            {1, ResourceBindType::CombinedImageSampler, ShaderStage::Pixel, 1},
        }));
        const DescriptorSetLayoutPtr layoutArray = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::ConstantBuffer, ShaderStage::Pixel, 1},
            {1, ResourceBindType::CombinedImageSampler, ShaderStage::Pixel, 4},
        }));
        const DescriptorSetLayoutPtr layoutRWBuffer = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::RWBuffer, ShaderStage::Compute, 1},
        }));
        const DescriptorSetLayoutPtr layoutStructured = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::StructuredBuffer, ShaderStage::Compute, 1},
        }));

        bool bOk = true;
        bOk &= Check(layoutA && layoutReordered && layoutOtherStage && layoutArray && layoutRWBuffer && layoutStructured,
                     "共有の layout を作れない");
        if (!bOk)
        {
            return false;
        }
        bOk &= Check(layoutA.get() == layoutReordered.get(), "並べる順だけが違う形が別の layout になった");
        bOk &= Check(layoutA.get() != layoutOtherStage.get(), "ステージが違う形が同じ layout になった");
        bOk &= Check(layoutA.get() != layoutArray.get(), "要素数が違う形が同じ layout になった");
        bOk &= Check(layoutRWBuffer.get() == layoutStructured.get(),
                     "同じストレージバッファの形（RWBuffer と StructuredBuffer）が別の layout になった");
        bOk &= Check(layoutA->GetDesc().bindings.size() == 2, "layout が作成情報を持っていない");
        std::cout << TestName << " 共有の layout: 合格" << std::endl;
        return bOk;
    }

    struct BorrowFixture
    {
        DescriptorSetLayoutPtr UniformLayout;
        DescriptorSetLayoutPtr OversizedLayout;
        BufferPtr Constants;
    };

    // 借りたセットに定数バッファを書き込む
    void WriteConstants(const BorrowFixture& fixture, const FrameDescriptorAllocation& allocation, uint32_t index)
    {
        allocation.Set->BindConstantBuffer(0, fixture.Constants, (index % 4u) * 256u, 256u);
        allocation.Set->Update();
    }

    // pool を足す道・回収で戻ること・世代の拒否
    bool RunBorrowCases(IDevice& device)
    {
        BorrowFixture fixture;
        fixture.UniformLayout = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::ConstantBuffer, ShaderStage::Pixel, 1},
        }));
        // pool の容量（結合の 4）を超える 8 要素の配列を持つ layout
        fixture.OversizedLayout = device.GetOrCreateDescriptorSetLayout(MakeDesc({
            {0, ResourceBindType::CombinedImageSampler, ShaderStage::Pixel, 8},
        }));
        BufferDesc bufferDesc;
        bufferDesc.Size = 1024;
        bufferDesc.Usage = ResourceUsage::ConstantBuffer;
        bufferDesc.CPUAccessible = true;
        bufferDesc.DebugName = "FrameResourceDescriptorConstants";
        fixture.Constants = device.CreateBuffer(bufferDesc);
        if (!Check(fixture.UniformLayout && fixture.OversizedLayout && fixture.Constants, "試験の資源を作れない"))
        {
            return false;
        }

        // 1 つの pool に 4 セット・定数 4・結合 4 だけ入る小さな容量
        FrameDescriptorPoolCapacity capacity;
        capacity.MaxSets = 4;
        capacity.UniformBuffers = 4;
        capacity.SampledImages = 4;
        capacity.Samplers = 4;
        capacity.StorageBuffers = 4;
        capacity.StorageImages = 4;
        capacity.CombinedImageSamplers = 4;
        capacity.AccelerationStructures = 4;

        FrameResourceOwners owners;
        if (!Check(owners.ConfigureDescriptors(&device, capacity), "フレームの持ち主に記述子の pool を作れない"))
        {
            return false;
        }

        bool bOk = true;
        constexpr uint32_t BorrowCount = 10;

        // --- pool を足す道 ---
        FrameResourceOwner* frame0 = owners.BeginFrame(0, 0);
        bOk &= Check(frame0 != nullptr, "スロット 0 を開始できない");
        if (!bOk)
        {
            return false;
        }
        const IFrameDescriptorAllocator* allocator0 = frame0->GetDescriptorAllocator();
        VariableArray<FrameDescriptorAllocation> frame0Sets;
        VariableArray<uint64_t> handles;
        for (uint32_t index = 0; index < BorrowCount; ++index)
        {
            FrameDescriptorAllocation allocation = frame0->AllocateDescriptorSet(fixture.UniformLayout);
            if (!Check(allocation.IsValid(), "小さな pool を超えて借りられない"))
            {
                return false;
            }
            WriteConstants(fixture, allocation, index);
            const uint64_t handle = GetVkHandle(allocation.Set);
            for (uint64_t previous : handles)
            {
                bOk &= Check(previous != handle, "同じフレームで借りたセットのハンドルが重なった");
            }
            handles.push_back(handle);
            frame0Sets.push_back(allocation);
        }
        const FrameDescriptorAllocation oversized = frame0->AllocateDescriptorSet(fixture.OversizedLayout);
        bOk &= Check(oversized.IsValid(), "pool の容量を超える型を持つ layout を借りられない");

        const FrameDescriptorStats frame0Stats = allocator0->GetFrameStats();
        // 定数の 10 セットは 4 セットずつで 3 個、8 要素の配列は既定の pool に入らないので 1 個足す
        bOk &= Check(frame0Stats.Allocations == BorrowCount + 1, "借りた数の統計が合わない");
        bOk &= Check(frame0Stats.Writes == BorrowCount, "書き込んだ数の統計が合わない");
        bOk &= Check(frame0Stats.PoolsAdded == 4, "pool を足した回数の統計が合わない");
        bOk &= Check(allocator0->GetPoolCount() == 4, "pool の数が合わない");
        bOk &= Check(frame0Stats.Failures == 0, "借りるのに失敗した");
        for (const FrameDescriptorAllocation& allocation : frame0Sets)
        {
            bOk &= Check(frame0->ValidateBorrowed(allocation, "frame0"), "記録中のフレームで借りたセットが拒まれた");
            bOk &= Check(!allocation.Set->IsExpired(), "記録中のフレームで借りたセットが使えない");
        }
        std::cout << TestName << " pool を足す道: allocations=" << frame0Stats.Allocations
                  << " writes=" << frame0Stats.Writes << " pools_added=" << frame0Stats.PoolsAdded << std::endl;

        bOk &= Check(owners.CommitSubmitted(1), "スロット 0 を提出できない");
        bOk &= Check(owners.GetLastFrameDescriptorStats().Allocations == BorrowCount + 1,
                     "提出の時点のフレームの統計が残っていない");

        // --- 完了していない提出のスロットは戻らない ---
        FrameResourceOwner* frame1 = owners.BeginFrame(1, 0);
        bOk &= Check(frame1 != nullptr, "スロット 1 を開始できない");
        if (!bOk)
        {
            return false;
        }
        bOk &= Check(!frame0Sets[0].Set->IsExpired(), "完了していない提出のセットが戻された");
        bOk &= Check(allocator0->GetFrameStats().Allocations == BorrowCount + 1, "完了していない提出の pool が戻された");
        const FrameDescriptorAllocation frame1Set = frame1->AllocateDescriptorSet(fixture.UniformLayout);
        bOk &= Check(frame1Set.IsValid(), "スロット 1 で借りられない");
        bOk &= Check(frame1->GetDescriptorAllocator() != allocator0, "スロットごとに別の pool になっていない");
        bOk &= Check(owners.CommitSubmitted(2), "スロット 1 を提出できない");

        // --- 回収で戻ること ---
        // スロット 0 の提出（通し番号 1）が完了したので、次の開始で pool がまとめて戻る
        FrameResourceOwner* frame0Again = owners.BeginFrame(0, 1);
        bOk &= Check(frame0Again == frame0, "スロット 0 の持ち主が変わった");
        if (!bOk)
        {
            return false;
        }
        bOk &= Check(allocator0->GetFrameStats().Allocations == 0, "回収で借りた数が戻っていない");
        bOk &= Check(allocator0->GetPoolCount() == 4, "回収で pool を手放した（使い回していない）");
        bOk &= Check(!frame1Set.Set->IsExpired(), "完了していないスロット 1 のセットが戻された");
        for (uint32_t index = 0; index < BorrowCount; ++index)
        {
            FrameDescriptorAllocation allocation = frame0Again->AllocateDescriptorSet(fixture.UniformLayout);
            if (!Check(allocation.IsValid(), "回収の後に借りられない"))
            {
                return false;
            }
            WriteConstants(fixture, allocation, index);
        }
        const FrameDescriptorStats reusedStats = allocator0->GetFrameStats();
        bOk &= Check(reusedStats.PoolsAdded == 0, "回収の後に同じ数を借りて pool を足した");
        bOk &= Check(allocator0->GetTotalPoolsAdded() == 4, "pool を足した回数の合計が合わない");
        bOk &= Check(reusedStats.Writes == BorrowCount, "回収の後の書き込んだ数が合わない");
        std::cout << TestName << " 回収で戻ること: pools=" << allocator0->GetPoolCount()
                  << " pools_added=" << reusedStats.PoolsAdded << std::endl;

        // --- 世代の拒否 ---
        const FrameDescriptorAllocation& stale = frame0Sets[0];
        bOk &= Check(stale.Set->IsExpired(), "回収の前に借りたセットが IsExpired にならない");
#if NORVES_BUILD_DEVELOPMENT
        bOk &= Check(!frame0Again->ValidateBorrowed(stale, "stale_descriptor"), "前のフレームで借りたセットが世代の検査を通った");
        bOk &= Check(frame0Again->GetStaleRejectCount() >= 1, "世代の拒否が数えられていない");
#endif
        WriteConstants(fixture, stale, 0);
        bOk &= Check(allocator0->GetFrameStats().ExpiredRejects == 1, "回収の後のセットへの書き込みが拒まれていない");
        bOk &= Check(allocator0->GetFrameStats().Writes == BorrowCount, "回収の後のセットへの書き込みが数えられた");

        owners.AbortFrame();
        const FrameDescriptorAllocation outside = frame0Again->AllocateDescriptorSet(fixture.UniformLayout);
        bOk &= Check(!outside.IsValid(), "記録中でないのに借りられた");
        std::cout << TestName << " 世代の拒否: expired=" << stale.Set->IsExpired()
                  << " rejects=" << allocator0->GetFrameStats().ExpiredRejects << std::endl;

        owners.Reset();
        owners.ReleaseDescriptorPools();
        bOk &= Check(!owners.IsDescriptorsConfigured(), "pool を手放した後も設定が残っている");
        bOk &= Check(frame1Set.Set->IsExpired(), "pool を手放した後のセットが使える");
        return bOk;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            std::cout << TestName << " スキップ: NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定された" << std::endl;
            return GpuTestSkipReturnCode;
        }

        VulkanValidationErrorCapture validationCapture;
        bool bOk = true;
        {
            RHIDeviceDesc deviceDesc;
            deviceDesc.Api = GraphicsAPI::Vulkan;
            deviceDesc.bEnableValidation = true;
            DevicePtr device = CreateRHIDevice(deviceDesc);
            if (!IsValid(device))
            {
                std::cout << TestName << " スキップ: Vulkan デバイスが無い" << std::endl;
                return GpuTestSkipReturnCode;
            }

            bOk &= RunSharedLayoutCase(*device);
            bOk &= RunBorrowCases(*device);
            device->WaitIdle();
        }

        const uint32_t validationErrors = validationCapture.GetHitCount();
        bOk &= Check(validationErrors == 0, "Vulkan の検証レイヤーのエラーが出た");
        std::cout << TestName << " validation_errors=" << validationErrors << std::endl;
        if (!bOk)
        {
            return 1;
        }
        std::cout << TestName << " 合格" << std::endl;
        return 0;
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
        std::cerr << TestName << " 失敗: 例外 " << exception.what() << std::endl;
        return 1;
    }
}

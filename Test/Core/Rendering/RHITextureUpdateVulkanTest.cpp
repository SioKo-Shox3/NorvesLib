#include "CoreTypes.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/IDevice.h"
#include "RHI/ICommandList.h"
#include "RHI/IBuffer.h"
#include "RHI/ITexture.h"

#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>

namespace NorvesLib::RHI::Vulkan
{
void InjectVulkanSingleTimeCommandEndFailureForTesting(IDevice *device) noexcept;
void InjectVulkanSingleTimeCommandSubmitFailureForTesting(IDevice *device) noexcept;
void InjectVulkanSingleTimeCommandWaitFailureForTesting(IDevice *device) noexcept;
void InjectVulkanSingleTimeCommandWaitAndFallbackFailureForTesting(IDevice *device) noexcept;
void ClearVulkanSingleTimeCommandFailureForTesting(IDevice *device) noexcept;
uint32_t GetVulkanSingleTimeCommandFailureHitCountForTesting(IDevice *device) noexcept;
void InjectVulkanDeviceWaitIdleFailuresForTesting(IDevice *device, uint32_t failureCount) noexcept;
uint32_t GetVulkanDeviceWaitIdleFailureHitCountForTesting(IDevice *device) noexcept;
uint32_t GetVulkanSafeDeviceTeardownLeakCountForTesting() noexcept;
bool TriggerVulkanDeviceTeardownWaitFailureForTesting(DevicePtr &device) noexcept;
void GetVulkanTextureUpdateDeferredTextureResourceCountForTesting(uint32_t &resourceCount) noexcept;
void GetVulkanTextureUpdateStagingResourceCountsForTesting(
    uint32_t &bufferCount,
    uint32_t &memoryCount) noexcept;
void BeginVulkanValidationErrorCaptureForTesting() noexcept;
void EndVulkanValidationErrorCaptureForTesting() noexcept;
void ResetVulkanValidationErrorCaptureForTesting() noexcept;
uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
bool SubmitVulkanValidationErrorForTesting(IDevice *device) noexcept;
}

namespace NorvesLib
{
namespace
{

constexpr uint32_t TextureWidth = 2;
constexpr uint32_t TextureHeight = 2;
constexpr uint32_t BytesPerPixel = 4;
constexpr uint32_t TextureByteCount = TextureWidth * TextureHeight * BytesPerPixel;
constexpr uint32_t GpuTestSkipReturnCode = 125;
using FailureInjector = void (*)(RHI::IDevice *) noexcept;

class VulkanValidationErrorCapture
{
public:
    VulkanValidationErrorCapture()
    {
        RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting();
    }

    ~VulkanValidationErrorCapture()
    {
        RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting();
    }

    bool VerifyCallbackPath(const RHI::DevicePtr &device)
    {
        if (!RHI::Vulkan::SubmitVulkanValidationErrorForTesting(device.get()))
        {
            std::cerr << "Vulkanの検証エラーコールバックを呼び出せません" << std::endl;
            return false;
        }

        const uint32_t hitCount = RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
        if (hitCount != 1)
        {
            std::cerr << "Vulkanの検証エラーコールバック検出数が不正です: " << hitCount << std::endl;
            return false;
        }

        RHI::Vulkan::ResetVulkanValidationErrorCaptureForTesting();
        std::cout << "Vulkanの検証エラーコールバック検出経路を確認しました" << std::endl;
        return true;
    }

    bool VerifyNoErrors() const
    {
        const uint32_t hitCount = RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting();
        if (hitCount != 0)
        {
            std::cerr << "Vulkanの検証エラーを検出しました: " << hitCount << std::endl;
            return false;
        }
        return true;
    }
};

bool IsGpuTestSkipForced()
{
    char *forceSkip = nullptr;
    size_t forceSkipLength = 0;
    if (_dupenv_s(&forceSkip, &forceSkipLength, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || forceSkip == nullptr)
    {
        return false;
    }

    const bool bForceSkip = std::strcmp(forceSkip, "1") == 0;
    free(forceSkip);
    return bForceSkip;
}

int SkipGpuTest(const char *reason)
{
    std::cout << "RHITextureUpdateVulkanTest をスキップします: " << reason << std::endl;
    return static_cast<int>(GpuTestSkipReturnCode);
}

RHI::TextureDesc MakeTextureDesc()
{
    RHI::TextureDesc desc;
    desc.Width = TextureWidth;
    desc.Height = TextureHeight;
    desc.Depth = 1;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.TextureFormat = RHI::Format::R8G8B8A8_UNORM;
    desc.Dimension = RHI::TextureDimension::Texture2D;
    desc.Usage = RHI::ResourceUsage::ShaderResource | RHI::ResourceUsage::TransferSrc;
    desc.DebugName = "RHITextureUpdateVulkanTest";
    return desc;
}

RHI::BufferDesc MakeReadbackBufferDesc()
{
    RHI::BufferDesc desc;
    desc.Size = TextureByteCount;
    desc.Usage = RHI::ResourceUsage::TransferDst;
    desc.CPUAccessible = true;
    desc.DebugName = "RHITextureUpdateVulkanTestReadback";
    return desc;
}

void GetStagingResourceCounts(uint32_t &bufferCount, uint32_t &memoryCount)
{
    RHI::Vulkan::GetVulkanTextureUpdateStagingResourceCountsForTesting(bufferCount, memoryCount);
}

bool VerifyTextureReadback(
    const RHI::DevicePtr &device,
    const RHI::TexturePtr &texture,
    const uint8_t *pixels)
{
    RHI::BufferPtr readbackBuffer = device->CreateBuffer(MakeReadbackBufferDesc());
    if (!IsValid(texture) || !IsValid(readbackBuffer))
    {
        std::cerr << "正常更新の検証リソースを作成できません" << std::endl;
        return false;
    }

    RHI::CommandListPtr commandList = device->CreateCommandList();
    if (!IsValid(commandList))
    {
        std::cerr << "読み戻し用コマンドリストを作成できません" << std::endl;
        return false;
    }

    commandList->Begin();
    commandList->TextureBarrier(
        texture,
        RHI::ResourceState::ShaderResource,
        RHI::ResourceState::CopySource);
    commandList->CopyTextureToBuffer(texture, readbackBuffer, TextureWidth, TextureHeight, 0);
    commandList->End();
    commandList->Submit(true);
    device->WaitIdle();

    void *mappedData = readbackBuffer->Map(0, TextureByteCount);
    if (mappedData == nullptr)
    {
        std::cerr << "更新結果の読み戻しバッファをマップできません" << std::endl;
        return false;
    }

    const bool bPixelsMatch = std::memcmp(mappedData, pixels, TextureByteCount) == 0;
    readbackBuffer->Unmap();
    if (!bPixelsMatch)
    {
        std::cerr << "正常更新後の読み戻し値が入力データと一致しません" << std::endl;
        return false;
    }

    uint32_t bufferCount = 0;
    uint32_t memoryCount = 0;
    GetStagingResourceCounts(bufferCount, memoryCount);
    if (bufferCount != 0 || memoryCount != 0)
    {
        std::cerr << "正常更新後にステージング資源が残っています: buffer=" << bufferCount
                  << " memory=" << memoryCount << std::endl;
        return false;
    }

    return true;
}

bool VerifyFailedUpdateReleasesStagingResources(
    const RHI::DevicePtr &device,
    FailureInjector injectFailure,
    const char *failureName,
    const uint8_t *pixels,
    bool bExpectDeferredCleanup)
{
    RHI::TexturePtr texture = device->CreateTexture(MakeTextureDesc());
    if (!IsValid(texture))
    {
        std::cerr << "失敗経路用テクスチャを作成できません: " << failureName << std::endl;
        return false;
    }

    uint32_t bufferCountBefore = 0;
    uint32_t memoryCountBefore = 0;
    GetStagingResourceCounts(bufferCountBefore, memoryCountBefore);
    const uint32_t failureHitCountBefore =
        RHI::Vulkan::GetVulkanSingleTimeCommandFailureHitCountForTesting(device.get());

    bool bUpdateThrew = false;
    injectFailure(device.get());
    try
    {
        texture->Update(pixels, TextureWidth * BytesPerPixel, TextureByteCount, 0, 0);
    }
    catch (const std::exception &)
    {
        bUpdateThrew = true;
    }
    RHI::Vulkan::ClearVulkanSingleTimeCommandFailureForTesting(device.get());

    uint32_t bufferCountAfter = 0;
    uint32_t memoryCountAfter = 0;
    GetStagingResourceCounts(bufferCountAfter, memoryCountAfter);
    const uint32_t failureHitCountAfter =
        RHI::Vulkan::GetVulkanSingleTimeCommandFailureHitCountForTesting(device.get());
    if (!bUpdateThrew)
    {
        std::cerr << "同期失敗を再現できませんでした: " << failureName << std::endl;
        return false;
    }

    if (failureHitCountAfter != failureHitCountBefore + 1)
    {
        std::cerr << "同期失敗の注入地点を通過していません: " << failureName << std::endl;
        return false;
    }

    const uint32_t expectedBufferCountAfter = bufferCountBefore + (bExpectDeferredCleanup ? 1u : 0u);
    const uint32_t expectedMemoryCountAfter = memoryCountBefore + (bExpectDeferredCleanup ? 1u : 0u);
    if (bufferCountAfter != expectedBufferCountAfter || memoryCountAfter != expectedMemoryCountAfter)
    {
        std::cerr << "同期失敗後のステージング資源数が不正です: " << failureName
                  << " buffer=" << bufferCountAfter << " memory=" << memoryCountAfter << std::endl;
        return false;
    }

    device->WaitIdle();
    GetStagingResourceCounts(bufferCountAfter, memoryCountAfter);
    if (bufferCountAfter != bufferCountBefore || memoryCountAfter != memoryCountBefore)
    {
        std::cerr << "デバイス待機後もステージング資源が残っています: " << failureName
                  << " buffer=" << bufferCountAfter << " memory=" << memoryCountAfter << std::endl;
        return false;
    }

    texture->Update(pixels, TextureWidth * BytesPerPixel, TextureByteCount, 0, 0);
    if (!VerifyTextureReadback(device, texture, pixels))
    {
        std::cerr << "失敗後の再更新または読み戻しに失敗しました: " << failureName << std::endl;
        return false;
    }

    std::cout << "同期失敗後の再更新・資源解放・読み戻しを確認: " << failureName << std::endl;
    return true;
}

bool VerifySuccessfulUpdate(const RHI::DevicePtr &device, const uint8_t *pixels)
{
    RHI::TexturePtr texture = device->CreateTexture(MakeTextureDesc());
    if (!IsValid(texture))
    {
        std::cerr << "正常更新の検証テクスチャを作成できません" << std::endl;
        return false;
    }

    texture->Update(pixels, TextureWidth * BytesPerPixel, TextureByteCount, 0, 0);
    if (!VerifyTextureReadback(device, texture, pixels))
    {
        return false;
    }

    std::cout << "正常なテクスチャ更新と読み戻しを確認しました" << std::endl;
    return true;
}

bool VerifyDeferredTextureCleanupKeepsDeviceAlive(
    RHI::DevicePtr &device,
    const uint8_t *pixels)
{
    RHI::TexturePtr texture = device->CreateTexture(MakeTextureDesc());
    if (!IsValid(texture))
    {
        std::cerr << "遅延解放検証用テクスチャを作成できません" << std::endl;
        return false;
    }

    uint32_t bufferCountBefore = 0;
    uint32_t memoryCountBefore = 0;
    uint32_t deferredTextureCountBefore = 0;
    GetStagingResourceCounts(bufferCountBefore, memoryCountBefore);
    RHI::Vulkan::GetVulkanTextureUpdateDeferredTextureResourceCountForTesting(deferredTextureCountBefore);
    const uint32_t failureHitCountBefore =
        RHI::Vulkan::GetVulkanSingleTimeCommandFailureHitCountForTesting(device.get());

    bool bUpdateThrew = false;
    RHI::Vulkan::InjectVulkanSingleTimeCommandWaitAndFallbackFailureForTesting(device.get());
    try
    {
        texture->Update(pixels, TextureWidth * BytesPerPixel, TextureByteCount, 0, 0);
    }
    catch (const std::exception &)
    {
        bUpdateThrew = true;
    }
    RHI::Vulkan::ClearVulkanSingleTimeCommandFailureForTesting(device.get());

    uint32_t bufferCountAfter = 0;
    uint32_t memoryCountAfter = 0;
    GetStagingResourceCounts(bufferCountAfter, memoryCountAfter);
    const uint32_t failureHitCountAfter =
        RHI::Vulkan::GetVulkanSingleTimeCommandFailureHitCountForTesting(device.get());
    if (!bUpdateThrew || failureHitCountAfter != failureHitCountBefore + 1 ||
        bufferCountAfter != bufferCountBefore + 1 || memoryCountAfter != memoryCountBefore + 1)
    {
        std::cerr << "待機失敗後の遅延解放状態を作れませんでした" << std::endl;
        return false;
    }

    const uint32_t idleFailureHitCountBefore =
        RHI::Vulkan::GetVulkanDeviceWaitIdleFailureHitCountForTesting(device.get());
    RHI::Vulkan::InjectVulkanDeviceWaitIdleFailuresForTesting(device.get(), 1);
    TWeakPtr<RHI::IDevice> weakDevice = device;
    texture = nullptr;

    uint32_t deferredTextureCountAfter = 0;
    RHI::Vulkan::GetVulkanTextureUpdateDeferredTextureResourceCountForTesting(deferredTextureCountAfter);
    const uint32_t idleFailureHitCountAfter =
        RHI::Vulkan::GetVulkanDeviceWaitIdleFailureHitCountForTesting(device.get());
    GetStagingResourceCounts(bufferCountAfter, memoryCountAfter);
    if (idleFailureHitCountAfter != idleFailureHitCountBefore + 1 ||
        bufferCountAfter != bufferCountBefore + 1 ||
        memoryCountAfter != memoryCountBefore + 1 ||
        deferredTextureCountAfter != deferredTextureCountBefore + 1)
    {
        std::cerr << "デバイス待機失敗時にテクスチャとstaging資源を保持できませんでした" << std::endl;
        return false;
    }

    device = nullptr;
    RHI::DevicePtr retainedDevice = weakDevice.lock();
    if (!IsValid(retainedDevice))
    {
        std::cerr << "遅延資源がデバイスの所有権を保持していません" << std::endl;
        return false;
    }
    device = retainedDevice;

    device->WaitIdle();
    GetStagingResourceCounts(bufferCountAfter, memoryCountAfter);
    RHI::Vulkan::GetVulkanTextureUpdateDeferredTextureResourceCountForTesting(deferredTextureCountAfter);
    if (bufferCountAfter != bufferCountBefore ||
        memoryCountAfter != memoryCountBefore ||
        deferredTextureCountAfter != deferredTextureCountBefore)
    {
        std::cerr << "待機成功後に遅延texture資源を解放できませんでした" << std::endl;
        return false;
    }

    std::cout << "待機失敗中のテクスチャ破棄後もデバイス所有と遅延資源解放を確認しました" << std::endl;
    return true;
}

bool VerifyDeviceTeardownWaitFailureIsSafe(const RHI::RHIDeviceDesc &desc)
{
    RHI::DevicePtr teardownDevice = RHI::CreateRHIDevice(desc);
    if (!IsValid(teardownDevice))
    {
        std::cerr << "device終了検証用のVulkan deviceを作成できません" << std::endl;
        return false;
    }

    const uint32_t protectedTeardownCountBefore =
        RHI::Vulkan::GetVulkanSafeDeviceTeardownLeakCountForTesting();
    if (!RHI::Vulkan::TriggerVulkanDeviceTeardownWaitFailureForTesting(teardownDevice) ||
        IsValid(teardownDevice))
    {
        std::cerr << "待機失敗時のdevice teardown検証を実行できません" << std::endl;
        return false;
    }

    const uint32_t protectedTeardownCountAfter =
        RHI::Vulkan::GetVulkanSafeDeviceTeardownLeakCountForTesting();
    if (protectedTeardownCountAfter != protectedTeardownCountBefore + 1)
    {
        std::cerr << "待機失敗時の安全なdevice保持経路を通過していません" << std::endl;
        return false;
    }

    std::cout << "待機失敗時にVulkan device資源を保持する終了経路を確認しました" << std::endl;
    return true;
}

int RunTest()
{
    if (IsGpuTestSkipForced())
    {
        return SkipGpuTest("NORVESLIB_FORCE_GPU_TEST_SKIP=1 が指定されています。");
    }

    RHI::RHIDeviceDesc desc;
    desc.Api = RHI::GraphicsAPI::Vulkan;
    desc.bEnableValidation = true;

    VulkanValidationErrorCapture validationErrorCapture;
    RHI::DevicePtr device = RHI::CreateRHIDevice(desc);
    if (!IsValid(device))
    {
        return SkipGpuTest("Vulkanデバイスが利用できません。");
    }

    if (!validationErrorCapture.VerifyCallbackPath(device))
    {
        return 1;
    }

    const uint8_t pixels[TextureByteCount] = {
        255, 16, 32, 255, 64, 128, 192, 255,
        12, 34, 56, 255, 210, 180, 90, 255};

    if (!VerifyFailedUpdateReleasesStagingResources(
            device,
            RHI::Vulkan::InjectVulkanSingleTimeCommandEndFailureForTesting,
            "終了",
            pixels,
            false) ||
        !VerifyFailedUpdateReleasesStagingResources(
            device,
            RHI::Vulkan::InjectVulkanSingleTimeCommandSubmitFailureForTesting,
            "送信",
            pixels,
            false) ||
        !VerifyFailedUpdateReleasesStagingResources(
            device,
            RHI::Vulkan::InjectVulkanSingleTimeCommandWaitFailureForTesting,
            "完了待機",
            pixels,
            false) ||
        !VerifyFailedUpdateReleasesStagingResources(
            device,
            RHI::Vulkan::InjectVulkanSingleTimeCommandWaitAndFallbackFailureForTesting,
            "完了待機と遅延解放",
            pixels,
            true) ||
        !VerifyDeferredTextureCleanupKeepsDeviceAlive(device, pixels) ||
        !VerifySuccessfulUpdate(device, pixels) ||
        !VerifyDeviceTeardownWaitFailureIsSafe(desc) ||
        !validationErrorCapture.VerifyNoErrors())
    {
        return 1;
    }

    std::cout << "テクスチャ更新テストが成功しました" << std::endl;
    return 0;
}

} // 匿名名前空間
} // NorvesLib名前空間

int main()
{
    return NorvesLib::RunTest();
}

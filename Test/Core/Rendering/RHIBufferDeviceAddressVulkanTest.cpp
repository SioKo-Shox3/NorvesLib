// Buffer Device Addressの要求別にRHI契約を検証します。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IBuffer.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr uint64_t BufferSize = 256;
    constexpr const char* TestName = "RHIBufferDeviceAddressVulkanTest";

    RHI::BufferDesc MakeBufferDesc(RHI::ResourceUsage extraUsage, const char* debugName)
    {
        RHI::BufferDesc desc;
        desc.Size = BufferSize;
        desc.Usage = RHI::ResourceUsage::StorageBuffer | extraUsage;
        desc.DebugName = debugName;
        return desc;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }

        RHI::RHIDeviceDesc deviceDesc;
        deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = false;
        RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
        if (!device)
        {
            return ReportGpuTestSkip(TestName, "Vulkanデバイスを利用できません");
        }
        if (device->GetAPI() != RHI::API::Vulkan)
        {
            std::cerr << "Vulkanデバイスを作成できませんでした\n";
            return 1;
        }

        RHI::BufferPtr unrequestedBuffer = device->CreateBuffer(
            MakeBufferDesc(RHI::ResourceUsage::None, "RHIBufferDeviceAddress.Unrequested"));
        RHI::BufferPtr requestedBuffer = device->CreateBuffer(
            MakeBufferDesc(RHI::ResourceUsage::BufferDeviceAddress, "RHIBufferDeviceAddress.Requested"));
        if (!unrequestedBuffer || !requestedBuffer)
        {
            std::cerr << "検証用バッファを作成できませんでした\n";
            return 1;
        }

        const bool bBufferDeviceAddressSupported =
            device->GetCapabilities().bBufferDeviceAddress;
        const uint64_t unrequestedAddress = unrequestedBuffer->GetDeviceAddress();
        const uint64_t requestedAddress = requestedBuffer->GetDeviceAddress();

        if (unrequestedAddress != 0)
        {
            std::cerr << "要求していないバッファがdevice addressを返しました\n";
            return 1;
        }
        if (bBufferDeviceAddressSupported && requestedAddress == 0)
        {
            std::cerr << "対応デバイスが要求済みバッファのdevice addressを返しませんでした\n";
            return 1;
        }
        if (!bBufferDeviceAddressSupported && requestedAddress != 0)
        {
            std::cerr << "非対応デバイスがdevice addressを返しました\n";
            return 1;
        }

        std::cout << "buffer_device_address_supported="
                  << (bBufferDeviceAddressSupported ? "true" : "false") << '\n';
        std::cout << "requested_buffer_device_address=" << requestedAddress << '\n';
        std::cout << "unrequested_buffer_device_address=" << unrequestedAddress << '\n';
        std::cout << "Buffer Device Addressの検証に成功しました\n";
        return 0;
    }
}

int main()
{
    return RunTest();
}
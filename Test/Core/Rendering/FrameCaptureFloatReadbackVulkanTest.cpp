#include "RenderingValidation/GpuTestEnvironment.h"
#include "RenderingValidation/RenderingFloatImage.h"

#include "CoreTypes.h"
#include "Rendering/FrameCaptureReadbackHelper.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IGPUResourceAllocator.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cstdint>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr uint32_t Rgba16BytesPerPixel = 8u;
    constexpr uint32_t RgbaChannelCount = 4u;

    bool Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FrameCaptureFloatReadbackVulkanTest failed: " << message << '\n';
            return false;
        }
        return true;
    }

    bool CaptureFloatTexture(
        RHI::IDevice* device,
        Core::Rendering::FrameCaptureReadbackHelper& helper,
        const uint8_t* pixels,
        uint32_t width,
        uint64_t frameNumber,
        RgbaFloatImage& outImage)
    {
        RHI::TextureDesc textureDesc;
        textureDesc.Width = width;
        textureDesc.Height = 1u;
        textureDesc.TextureFormat = RHI::Format::R16G16B16A16_FLOAT;
        textureDesc.Usage = RHI::ResourceUsage::TransferDst |
            RHI::ResourceUsage::TransferSrc |
            RHI::ResourceUsage::ShaderRead;
        textureDesc.DebugName = "FrameCaptureFloatReadbackSource";

        RHI::TexturePtr texture = device->CreateTexture(textureDesc);
        if (!Require(IsValid(texture), "RGBA16F texture creation must succeed"))
        {
            return false;
        }
        const uint32_t rowPitch = width * Rgba16BytesPerPixel;
        texture->Update(pixels, rowPitch, rowPitch);

        const Core::Rendering::FrameCaptureRequestResult request = helper.RequestFrameCapture();
        if (!Require(request.IsAccepted(), "capture request must be accepted"))
        {
            return false;
        }

        RHI::CommandListPtr commandList = device->CreateCommandList();
        if (!Require(IsValid(commandList), "command list creation must succeed"))
        {
            return false;
        }

        Core::Rendering::FrameCaptureSource source;
        source.Texture = texture;
        source.CurrentState = RHI::ResourceState::ShaderResource;
        source.RestoreState = RHI::ResourceState::ShaderResource;
        source.FrameNumber = frameNumber;

        commandList->Begin();
        const Core::Rendering::FrameCaptureRecordStatus recordStatus =
            helper.TryRecordCopy(0u, commandList.get(), source);
        commandList->End();
        if (!Require(recordStatus == Core::Rendering::FrameCaptureRecordStatus::Recorded,
                     "RGBA16F copy must record"))
        {
            return false;
        }

        commandList->Submit(true);
        device->WaitIdle();
        helper.PublishCompletedFrameSlot(0u);

        Core::Rendering::CapturedFrame frame;
        if (!Require(helper.TryConsumeCapturedFrame(frame), "completed capture must be consumable") ||
            !Require(frame.IsSuccess(), "completed capture must report success") ||
            !Require(frame.RequestId == request.RequestId, "capture request id must round-trip") ||
            !Require(frame.FrameNumber == frameNumber, "capture frame number must round-trip") ||
            !Require(frame.Width == width && frame.Height == 1u,
                     "capture dimensions must round-trip") ||
            !Require(frame.Format == RHI::Format::R16G16B16A16_FLOAT,
                     "capture format must remain RGBA16F") ||
            !Require(frame.BytesPerPixel == Rgba16BytesPerPixel && frame.RowPitchBytes == rowPitch,
                     "capture layout must be tightly packed RGBA16F"))
        {
            return false;
        }

        return Require(DecodeCapturedRgba16Float(frame, outImage) == FloatImageStatus::Success,
                       "captured RGBA16F bytes must decode");
    }

    bool VerifyKnownValues(const RgbaFloatImage& image)
    {
        constexpr float Expected[] = {
            0.0f, 1.0f, -2.0f, 65504.0f,
            0.5f, -0.5f, 2.0f, 4.0f};
        if (!Require(image.Width == 2u && image.Height == 1u && image.Values.size() == 8u,
                     "known-value image dimensions must match"))
        {
            return false;
        }
        for (size_t index = 0; index < image.Values.size(); ++index)
        {
            if (!Require(image.Values[index] == Expected[index],
                         "known RGBA16F value must match exactly"))
            {
                return false;
            }
        }
        return Require(IsFiniteImage(image), "known-value image must be finite");
    }

    bool VerifyNonFiniteAt(
        const RgbaFloatImage& sourceImage,
        uint32_t expectedX,
        NonFiniteKind expectedKind)
    {
        RgbaFloatImage image = sourceImage;
        for (uint32_t x = 0; x < expectedX; ++x)
        {
            image.Values[static_cast<size_t>(x) * RgbaChannelCount] = 1.0f;
        }
        const NonFiniteLocation location = FindFirstNonFinite(image);
        return Require(location.Kind == expectedKind && location.X == expectedX &&
                           location.Y == 0u && location.Channel == 0u,
                       "non-finite R channel must be classified at the expected pixel");
    }

    bool VerifyNonFiniteValues(const RgbaFloatImage& image)
    {
        if (!Require(image.Width == 3u && image.Height == 1u && image.Values.size() == 12u,
                     "non-finite image dimensions must match") ||
            !Require(!IsFiniteImage(image), "non-finite image must be rejected"))
        {
            return false;
        }
        return VerifyNonFiniteAt(image, 0u, NonFiniteKind::NaN) &&
            VerifyNonFiniteAt(image, 1u, NonFiniteKind::PositiveInfinity) &&
            VerifyNonFiniteAt(image, 2u, NonFiniteKind::NegativeInfinity);
    }
}

int main()
{
    if (IsForcedGpuTestSkipRequested())
    {
        return ReportGpuTestSkip("FrameCaptureFloatReadbackVulkanTest", "forced by environment");
    }

    Core::Container::String reason;
    if (!CanCreateVulkanDeviceForGpuTest(reason))
    {
        return ReportGpuTestSkip("FrameCaptureFloatReadbackVulkanTest", "no Vulkan device is available");
    }

    RHI::RHIDeviceDesc deviceDesc;
    deviceDesc.Api = RHI::GraphicsAPI::Vulkan;
    deviceDesc.bEnableValidation = false;
    RHI::DevicePtr device = RHI::CreateRHIDevice(deviceDesc);
    if (!IsValid(device))
    {
        return ReportGpuTestSkip("FrameCaptureFloatReadbackVulkanTest", "Vulkan device creation failed");
    }

    Core::Rendering::FrameCaptureReadbackHelper helper;
    if (!Require(helper.Initialize(device.get(), 1u), "readback helper initialization must succeed"))
    {
        return 1;
    }

    constexpr uint8_t KnownPixels[] = {
        0x00u, 0x00u, 0x00u, 0x3Cu, 0x00u, 0xC0u, 0xFFu, 0x7Bu,
        0x00u, 0x38u, 0x00u, 0xB8u, 0x00u, 0x40u, 0x00u, 0x44u};
    RgbaFloatImage knownImage;
    if (!CaptureFloatTexture(device.get(), helper, KnownPixels, 2u, 101u, knownImage) ||
        !VerifyKnownValues(knownImage))
    {
        helper.Shutdown();
        return 1;
    }

    constexpr uint8_t NonFinitePixels[] = {
        0x00u, 0x7Eu, 0x00u, 0x3Cu, 0x00u, 0x3Cu, 0x00u, 0x3Cu,
        0x00u, 0x7Cu, 0x00u, 0x3Cu, 0x00u, 0x3Cu, 0x00u, 0x3Cu,
        0x00u, 0xFCu, 0x00u, 0x3Cu, 0x00u, 0x3Cu, 0x00u, 0x3Cu};
    RgbaFloatImage nonFiniteImage;
    if (!CaptureFloatTexture(device.get(), helper, NonFinitePixels, 3u, 102u, nonFiniteImage) ||
        !VerifyNonFiniteValues(nonFiniteImage))
    {
        helper.Shutdown();
        return 1;
    }

    helper.Shutdown();
    device->WaitIdle();
    std::cout << "known_values={0,1,-2,65504},{0.5,-0.5,2,4} "
                 "non_finite=NaN@(0,0,R),+Inf@(1,0,R),-Inf@(2,0,R)\n";
    return 0;
}

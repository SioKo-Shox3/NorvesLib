// 箱のデカールが GBuffer の指定された RGB だけを変え、草の受信設定を守ることを実 GPU で確かめる。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/DecalPass.h"
#include "Rendering/DecalTypes.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/ViewRenderContext.h"
#include "RenderingValidation/GpuTestEnvironment.h"

#include "Math/Quaternion.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
#include "RHI/Vulkan/VulkanBuffer.h"
#include "RHI/Vulkan/VulkanCommandList.h"
#include "RHI/Vulkan/VulkanTexture.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>

namespace NorvesLib::RHI::Vulkan
{
    void BeginVulkanValidationErrorCaptureForTesting() noexcept;
    void EndVulkanValidationErrorCaptureForTesting() noexcept;
    uint32_t GetVulkanValidationErrorCaptureHitCountForTesting() noexcept;
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "DecalGBufferVulkanTest";
    constexpr uint32_t Width = 64;
    constexpr uint32_t Height = 64;
    constexpr uint32_t PixelCount = Width * Height;
    constexpr float ClearDepth = 0.5f;
    constexpr uint32_t PixelBytes[] = {4, 8, 4, 4};
    constexpr uint64_t Offsets[] = {0, PixelCount * 4ull, PixelCount * 12ull, PixelCount * 16ull};
    constexpr uint64_t SnapshotBytes = PixelCount * 20ull;

    // RGBA8 の丸めを 1 段階、RGBA16F は 1 付近の 1 ULP として扱う。
    constexpr float UnormTolerance = 1.01f / 255.0f;
    constexpr float HalfTolerance = 1.0f / 1024.0f;

    class ValidationCapture
    {
    public:
        ValidationCapture() { RHI::Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~ValidationCapture() { RHI::Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t GetHitCount() const { return RHI::Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    float HalfToFloat(uint16_t bits)
    {
        const float sign = (bits & 0x8000u) ? -1.0f : 1.0f;
        const uint32_t exponent = (bits >> 10u) & 0x1fu;
        const uint32_t mantissa = bits & 0x3ffu;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 0x1fu)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                 static_cast<int>(exponent) - 15);
    }

    float ReadHalf(const uint8_t* bytes)
    {
        uint16_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return HalfToFloat(value);
    }

    float ReadFloat(const uint8_t* bytes)
    {
        float value = 0.0f;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    Math::Vector3 WorldAtPixel(const CameraViewConstants& camera, float x, float y)
    {
        const float ndcX = (x + 0.5f) * (2.0f / static_cast<float>(Width)) - 1.0f;
        const float ndcY = (y + 0.5f) * (2.0f / static_cast<float>(Height)) - 1.0f;
        const Math::Vector4 world = camera.InverseViewProjectionMatrix * Math::Vector4(ndcX, ndcY, ClearDepth, 1.0f);
        return Math::Vector3(world.x / world.w, world.y / world.w, world.z / world.w);
    }

    bool CheckPixels(const uint8_t* bytes, const CameraViewConstants& camera, const DecalInstance& decal,
                     float materialAlpha, bool bShouldDraw, const char* label)
    {
        const Math::Quaternion rotation(decal.Rotation[0], decal.Rotation[1], decal.Rotation[2], decal.Rotation[3]);
        const Math::Quaternion inverse(-rotation.x, -rotation.y, -rotation.z, rotation.w);
        const Math::Vector3 position(decal.Position[0], decal.Position[1], decal.Position[2]);
        const Math::Vector3 expectedNormal = rotation * Math::Vector3(decal.LocalNormal[0], decal.LocalNormal[1], decal.LocalNormal[2]);
        const Math::Vector3 center = WorldAtPixel(camera, 31.5f, 31.5f);
        const Math::Vector3 stepX = inverse * (WorldAtPixel(camera, 32.5f, 31.5f) - center);
        const Math::Vector3 stepY = inverse * (WorldAtPixel(camera, 31.5f, 32.5f) - center);
        // 両軸の 1 画素ぶんを足し、箱の面に近い画素は内外どちらの判定にも使わない。
        const float margin[] = {std::abs(stepX.x) + std::abs(stepY.x),
                                std::abs(stepX.y) + std::abs(stepY.y),
                                std::abs(stepX.z) + std::abs(stepY.z)};
        const float normal[] = {expectedNormal.x, expectedNormal.y, expectedNormal.z};
        const float clearAlbedo[] = {0.2f, 0.3f, 0.4f, 0.6f};
        const float clearNormal[] = {0.0f, 1.0f, 0.0f, 0.7f};
        const float clearMaterial[] = {0.1f, 0.5f, 0.9f, materialAlpha};
        uint32_t errors = 0;
        uint32_t insideCount = 0;
        uint32_t outsideCount = 0;
        auto check = [&](bool bOk, uint32_t x, uint32_t y, const char* message)
        {
            if (!bOk)
            {
                if (errors < 12u)
                {
                    std::cerr << TestName << " " << label << " (" << x << ", " << y << "): " << message << '\n';
                }
                ++errors;
            }
        };

        for (uint32_t y = 0; y < Height; ++y)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const uint32_t pixel = y * Width + x;
                const uint8_t* before[4];
                const uint8_t* after[4];
                for (uint32_t image = 0; image < 4u; ++image)
                {
                    before[image] = bytes + Offsets[image] + pixel * PixelBytes[image];
                    after[image] = before[image] + SnapshotBytes;
                }

                for (uint32_t channel = 0; channel < 4u; ++channel)
                {
                    check(std::abs(static_cast<float>(before[0][channel]) / 255.0f - clearAlbedo[channel]) <= UnormTolerance,
                          x, y, "Albedo のクリア値が違います");
                    check(std::abs(ReadHalf(before[1] + channel * 2u) - clearNormal[channel]) <= HalfTolerance,
                          x, y, "Normal のクリア値が違います");
                    check(std::abs(static_cast<float>(before[2][channel]) / 255.0f - clearMaterial[channel]) <= UnormTolerance,
                          x, y, "Material のクリア値が違います");
                }
                check(ReadFloat(before[3]) == ClearDepth, x, y, "深度のクリア値が違います");
                check(before[0][3] == after[0][3], x, y, "Albedo.A が変わりました");
                check(std::memcmp(before[1] + 6u, after[1] + 6u, 2u) == 0, x, y, "Normal.A が変わりました");
                check(std::memcmp(before[2], after[2], 4u) == 0, x, y, "Material が変わりました");
                check(std::memcmp(before[3], after[3], 4u) == 0, x, y, "深度が変わりました");

                const Math::Vector3 local = inverse * (WorldAtPixel(camera, static_cast<float>(x), static_cast<float>(y)) - position);
                const float absolute[] = {std::abs(local.x), std::abs(local.y), std::abs(local.z)};
                bool bInside = true;
                bool bOutside = false;
                for (uint32_t axis = 0; axis < 3u; ++axis)
                {
                    bInside = bInside && absolute[axis] <= decal.HalfExtents[axis] - margin[axis];
                    bOutside = bOutside || absolute[axis] >= decal.HalfExtents[axis] + margin[axis];
                }
                insideCount += bInside ? 1u : 0u;
                outsideCount += bOutside ? 1u : 0u;

                if (!bShouldDraw || bOutside)
                {
                    check(std::memcmp(before[0], after[0], 4u) == 0, x, y, "描かない画素の Albedo が変わりました");
                    check(std::memcmp(before[1], after[1], 8u) == 0, x, y, "描かない画素の Normal が変わりました");
                }
                else if (bInside)
                {
                    for (uint32_t channel = 0; channel < 3u; ++channel)
                    {
                        const float original = static_cast<float>(before[0][channel]) / 255.0f;
                        const float expected = 0.5f * decal.Color[channel] + 0.5f * original;
                        const float actual = static_cast<float>(after[0][channel]) / 255.0f;
                        check(std::abs(actual - expected) <= UnormTolerance, x, y, "箱の内側の Albedo.RGB が違います");
                        check(std::abs(ReadHalf(after[1] + channel * 2u) - normal[channel]) <= HalfTolerance,
                              x, y, "箱の内側の Normal.RGB が違います");
                    }
                }
            }
        }
        std::cout << label << " 内側=" << insideCount << " 外側=" << outsideCount << " 不一致=" << errors << '\n';
        if (insideCount < 100u || outsideCount < 100u)
        {
            std::cerr << TestName << " " << label << ": 箱の内外の確認画素が不足しています\n";
            ++errors;
        }
        return errors == 0u;
    }

    bool CopyDepth(ICommandList& commandList, const TexturePtr& texture, const BufferPtr& buffer, uint64_t offset)
    {
        auto* vkCommandList = dynamic_cast<RHI::Vulkan::VulkanCommandList*>(&commandList);
        auto vkTexture = DynamicPointerCast<RHI::Vulkan::VulkanTexture>(texture);
        auto vkBuffer = DynamicPointerCast<RHI::Vulkan::VulkanBuffer>(buffer);
        if (!vkCommandList || !vkTexture || !vkBuffer)
        {
            std::cerr << TestName << " 深度読み戻しの Vulkan 資源を取得できません\n";
            return false;
        }
        // RHI の CopyTextureToBuffer は色アスペクト固定のため、D32 は深度アスペクトを指定する。
        vk::BufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eDepth;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = vk::Extent3D{Width, Height, 1};
        vkCommandList->GetVkCommandBuffer().copyImageToBuffer(vkTexture->GetVkImage(),
            vk::ImageLayout::eTransferSrcOptimal, vkBuffer->GetVkBuffer(), 1, &region);
        return true;
    }

    bool RecordSnapshot(ICommandList& commandList, const TexturePtr (&textures)[4], const BufferPtr& readback,
                        const ResourceState (&states)[4], uint64_t baseOffset)
    {
        for (uint32_t image = 0; image < 4u; ++image)
        {
            if (baseOffset != 0u && image < 2u)
            {
                // パスの最終レイアウトは読み取り用だが、直前の色添付への書き込みを転送まで待つ。
                ExplicitTextureBarrierDesc barrier;
                barrier.BeforeState = ResourceState::RenderTarget;
                barrier.AfterState = ResourceState::CopySource;
                barrier.OldLayout = TextureLayout::ShaderReadOnly;
                barrier.NewLayout = TextureLayout::TransferSource;
                commandList.TextureBarrierExplicit(textures[image], barrier);
                commandList.CommitTextureLayout(textures[image].get(), TextureLayout::TransferSource);
            }
            else
            {
                commandList.TextureBarrier(textures[image], states[image], ResourceState::CopySource);
            }
            if (image < 3u)
            {
                commandList.CopyTextureToBuffer(textures[image], readback, Width, Height, baseOffset + Offsets[image]);
            }
            else if (!CopyDepth(commandList, textures[image], readback, baseOffset + Offsets[image]))
            {
                return false;
            }
        }
        return true;
    }

    bool RunCase(const DevicePtr& device, DecalPass& pass, FrameResourceOwners& owners,
                 const CameraViewConstants& camera, DecalInstance decal, float materialAlpha,
                 bool bShouldDraw, uint64_t caseIndex, const char* label)
    {
        const Format formats[] = {Format::R8G8B8A8_UNORM, Format::R16G16B16A16_FLOAT,
                                  Format::R8G8B8A8_UNORM, Format::D32_FLOAT};
        const float clearColors[][4] = {{0.2f, 0.3f, 0.4f, 0.6f}, {0.0f, 1.0f, 0.0f, 0.7f},
                                         {0.1f, 0.5f, 0.9f, materialAlpha}};
        TexturePtr textures[4];
        for (uint32_t image = 0; image < 4u; ++image)
        {
            TextureDesc desc = image == 3u ? TextureDesc::DepthStencil(Width, Height, formats[image], "DecalTestDepth")
                                           : TextureDesc::RenderTarget(Width, Height, formats[image], "DecalTestColor");
            desc.Usage = desc.Usage | ResourceUsage::TransferSrc;
            textures[image] = device->CreateTexture(desc);
            if (!textures[image])
            {
                std::cerr << TestName << " " << label << ": テクスチャを作れません\n";
                return false;
            }
        }

        RenderPassDesc clearDesc;
        for (uint32_t image = 0; image < 3u; ++image)
        {
            AttachmentDesc attachment;
            attachment.format = formats[image];
            attachment.initialState = ResourceState::Undefined;
            attachment.finalState = ResourceState::RenderTarget;
            for (uint32_t channel = 0; channel < 4u; ++channel)
            {
                attachment.clearColor[channel] = clearColors[image][channel];
            }
            clearDesc.colorAttachments.push_back(attachment);
        }
        clearDesc.hasDepthStencil = true;
        clearDesc.depthStencilAttachment.format = formats[3];
        clearDesc.depthStencilAttachment.isDepthStencil = true;
        clearDesc.depthStencilAttachment.clearDepth = ClearDepth;
        clearDesc.depthStencilAttachment.initialState = ResourceState::Undefined;
        clearDesc.depthStencilAttachment.finalState = ResourceState::DepthWrite;
        RenderPassPtr clearPass = device->CreateRenderPass(clearDesc);
        FramebufferDesc framebufferDesc;
        framebufferDesc.renderPass = clearPass;
        framebufferDesc.width = Width;
        framebufferDesc.height = Height;
        for (uint32_t image = 0; image < 3u; ++image)
        {
            framebufferDesc.colorTargets.push_back(textures[image]);
        }
        framebufferDesc.depthStencilTarget = textures[3];
        FramebufferPtr framebuffer = clearPass ? device->CreateFramebuffer(framebufferDesc) : nullptr;
        BufferPtr readback = device->CreateBuffer(BufferDesc(SnapshotBytes * 2u, ResourceUsage::TransferDst, true, "DecalTestReadback"));
        CommandListPtr commandList = framebuffer && readback ? device->CreateCommandList() : nullptr;
        FrameResourceOwner* frame = commandList ? owners.BeginFrame(0, caseIndex) : nullptr;
        if (!frame)
        {
            std::cerr << TestName << " " << label << ": 描画かフレームの資源を作れません\n";
            return false;
        }

        auto instances = MakeShared<VariableArray<DecalInstance>>();
        instances->push_back(decal);
        DecalScene scene;
        scene.Decals = instances;
        commandList->Begin();
        commandList->BeginRenderPass(clearPass, framebuffer);
        commandList->EndRenderPass();
        const ResourceState clearStates[] = {ResourceState::RenderTarget, ResourceState::RenderTarget,
                                             ResourceState::RenderTarget, ResourceState::DepthWrite};
        bool bRecorded = RecordSnapshot(*commandList, textures, readback, clearStates, 0);
        if (bRecorded)
        {
            const ResourceState drawStates[] = {ResourceState::RenderTarget, ResourceState::RenderTarget,
                                                ResourceState::ShaderResource, ResourceState::ShaderResource};
            for (uint32_t image = 0; image < 4u; ++image)
            {
                commandList->TextureBarrier(textures[image], ResourceState::CopySource, drawStates[image]);
            }
            bRecorded = pass.RecordDecals(textures[0], textures[1], textures[2], textures[3],
                                          *commandList, *frame, scene, camera);
        }
        if (bRecorded)
        {
            const ResourceState finalStates[] = {ResourceState::ShaderResource, ResourceState::ShaderResource,
                                                 ResourceState::ShaderResource, ResourceState::ShaderResource};
            bRecorded = RecordSnapshot(*commandList, textures, readback, finalStates, SnapshotBytes);
        }
        if (!bRecorded)
        {
            std::cerr << TestName << " " << label << ": デカールか読み戻しを記録できません\n";
            commandList->End();
            owners.AbortFrame();
            return false;
        }
        commandList->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead, 0u, SnapshotBytes * 2u);
        commandList->End();
        commandList->Submit(true);
        const bool bCommitted = owners.CommitSubmitted(caseIndex + 1u);
        device->WaitIdle();
        if (!bCommitted)
        {
            std::cerr << TestName << " " << label << ": フレームの提出を記録できません\n";
            return false;
        }
        const uint8_t* bytes = static_cast<const uint8_t*>(readback->Map(0u, SnapshotBytes * 2u));
        if (!bytes)
        {
            std::cerr << TestName << " " << label << ": 読み戻しを写像できません\n";
            return false;
        }
        const bool bPassed = CheckPixels(bytes, camera, decal, materialAlpha, bShouldDraw, label);
        readback->Unmap();
        return bPassed;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPU 試験のスキップが指定されました");
        }
        ValidationCapture validation;
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device || device->GetAPI() != API::Vulkan)
        {
            return ReportGpuTestSkip(TestName, "Vulkan デバイスを利用できません");
        }

        String shaderDirectory(NORVES_SOURCE_ROOT);
        shaderDirectory += "/Assets/Shaders";
        ShaderManager shaderManager;
        if (!shaderManager.Initialize(device.get(), shaderDirectory))
        {
            std::cerr << TestName << " シェーダー管理を初期化できません\n";
            return 1;
        }
        DeviceFrameUploadPageSource pageSource;
        pageSource.SetDevice(device);
        FrameResourceOwners owners;
        owners.ConfigureUpload(&pageSource, FrameUploadArena::DefaultInitialPageBytes);
        if (!owners.ConfigureDescriptors(device.get()))
        {
            std::cerr << TestName << " フレームの記述子を準備できません\n";
            return 1;
        }
        ViewRenderContext context;
        context.Device = device.get();
        context.ShaderMgr = &shaderManager;
        DecalPass pass;
        if (!pass.Initialize(context))
        {
            std::cerr << TestName << " デカールパスを初期化できません\n";
            return 1;
        }

        CameraProxy proxy;
        proxy.Projection = ProjectionType::Orthographic;
        proxy.OrthoWidth = 8.0f;
        proxy.OrthoHeight = 8.0f;
        proxy.NearPlane = 0.1f;
        proxy.FarPlane = 10.0f;
        const CameraViewConstants camera = CameraViewConstants::BuildForDevice(proxy, 1.0f, device.get());
        DecalInstance decal;
        decal.Position[0] = 0.375f;
        decal.Position[1] = 0.5f;
        decal.Position[2] = WorldAtPixel(camera, 31.5f, 31.5f).z;
        decal.Rotation[1] = 0.2588190451f;
        decal.Rotation[3] = 0.9659258263f;
        decal.HalfExtents[0] = 2.0f;
        decal.HalfExtents[1] = 2.0f;
        decal.HalfExtents[2] = 1.5f;
        decal.Color[0] = 1.0f;
        decal.Color[1] = 0.0f;
        decal.Color[2] = 0.0f;
        decal.Color[3] = 0.5f;
        decal.NormalOpacity = 1.0f;
        decal.EdgeFade = 0.0f;

        bool bPassed = RunCase(device, pass, owners, camera, decal, 0.0f, true, 0u, "場合 A");
        bPassed = RunCase(device, pass, owners, camera, decal, 0.25f, false, 1u, "場合 B") && bPassed;
        decal.Flags = DecalFlags::ReceiveFoliage;
        bPassed = RunCase(device, pass, owners, camera, decal, 0.25f, true, 2u, "場合 C") && bPassed;

        device->WaitIdle();
        pass.Shutdown();
        owners.Reset();
        owners.ReleaseUploadPages();
        owners.ReleaseDescriptorPools();
        shaderManager.Shutdown();
        const uint32_t validationErrors = validation.GetHitCount();
        if (validationErrors != 0u)
        {
            std::cerr << TestName << " Vulkan 検証エラー: " << validationErrors << " 件\n";
            bPassed = false;
        }
        return bPassed ? 0 : 1;
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
        std::cerr << TestName << " 例外が発生しました: " << exception.what() << '\n';
        return 1;
    }
}

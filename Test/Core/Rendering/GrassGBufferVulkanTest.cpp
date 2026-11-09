// 草を実際に GBuffer へ描き、法線・AO・現在と前の位置による速度を読み戻す。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/GBufferPass.h"
#include "Rendering/GrassBladeMesh.h"
#include "Rendering/GrassPass.h"
#include "Rendering/ShaderManager.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/IFramebuffer.h"
#include "RHI/IRenderPass.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
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
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "GrassGBufferVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t Width = 128;
    constexpr uint32_t Height = 128;
    constexpr uint32_t PixelCount = Width * Height;
    constexpr uint64_t NormalOffset = PixelCount * 4ull;
    constexpr uint64_t MaterialOffset = PixelCount * 12ull;
    constexpr uint64_t VelocityOffset = PixelCount * 16ull;
    constexpr uint64_t ReadbackBytes = PixelCount * 20ull;
    constexpr float NormalTolerance = 0.01f;
    constexpr float VelocityTolerance = 0.002f;
    constexpr float StaticVelocityTolerance = 0.00001f;

    bool Check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << TestName << " 失敗: " << message << '\n';
        }
        return condition;
    }

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || value == nullptr)
        {
            return false;
        }
        const bool forced = std::strcmp(value, "1") == 0;
        free(value);
        return forced;
    }

    class ValidationCapture
    {
    public:
        ValidationCapture() { Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~ValidationCapture() { Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t Hits() const { return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    float ReadHalf(const uint8_t* bytes)
    {
        uint16_t bits = 0;
        std::memcpy(&bits, bytes, sizeof(bits));
        const float sign = (bits & 0x8000u) ? -1.0f : 1.0f;
        const uint32_t exponent = (bits >> 10u) & 31u;
        const uint32_t mantissa = bits & 1023u;
        if (exponent == 0u)
        {
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        }
        if (exponent == 31u)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
                                 static_cast<int>(exponent) - 15);
    }

    struct Point2
    {
        float X = 0.0f;
        float Y = 0.0f;
    };

    Point2 ProjectPixel(const Math::Vector4& clip)
    {
        return {(clip.x / clip.w * 0.5f + 0.5f) * Width,
                (clip.y / clip.w * 0.5f + 0.5f) * Height};
    }

    Point2 VertexVelocity(const GrassDrawConstants& constants, const GrassSpeciesGpuData& species,
                          const GrassBladeRecord& blade, uint32_t index)
    {
        const Math::Vector4 current = EvaluateGrassBladeVertexClip(constants, species, blade, 0, index, false);
        const Math::Vector4 previous = EvaluateGrassBladeVertexClip(constants, species, blade, 0, index, true);
        return {(current.x / current.w - previous.x / previous.w) * 0.5f,
                (current.y / current.w - previous.y / previous.w) * 0.5f};
    }

    GrassGenerateBufferRange ToRange(const FrameUploadAllocation& allocation)
    {
        return {allocation.Buffer, allocation.GetOffset32(), allocation.GetSize32()};
    }

    struct Fixture
    {
        DevicePtr Device;
        DeviceFrameUploadPageSource PageSource;
        FrameResourceOwners Owners;
        ShaderManager Shaders;
        GrassDrawGpu Drawer;
        TexturePtr LayerA;
        SamplerPtr Sampler;
        BufferPtr Records;
        BufferPtr Args;
        uint64_t LastSerial = 0;

        ~Fixture()
        {
            if (Device)
            {
                Device->WaitIdle();
            }
            Owners.Reset();
            Drawer.Shutdown();
            Shaders.Shutdown();
            LayerA.reset();
            Sampler.reset();
            Records.reset();
            Args.reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            PageSource.SetDevice({});
        }

        bool Initialize(const GrassBladeRecord& blade, const GrassGenerateSettings& settings)
        {
            PageSource.SetDevice(Device);
            Owners.ConfigureUpload(&PageSource, 1u << 20);
            String shaderDirectory(NORVES_SOURCE_ROOT);
            shaderDirectory += "/Assets/Shaders";
            if (!Check(Owners.ConfigureDescriptors(Device.get()), "記述子のプールを作れません") ||
                !Check(Shaders.Initialize(Device.get(), shaderDirectory), "シェーダー管理を初期化できません") ||
                !Check(Drawer.Initialize(Device.get(), &Shaders, settings), "草描画を初期化できません"))
            {
                return false;
            }

            Records = Device->CreateBuffer(BufferDesc(sizeof(blade), ResourceUsage::StorageBuffer |
                ResourceUsage::ShaderRead, true, "GrassGBufferTest.Records"));
            Args = Device->CreateBuffer(BufferDesc(kGrassArgsBufferSize, ResourceUsage::IndirectBuffer,
                true, "GrassGBufferTest.Args"));
            TextureDesc layerDesc;
            layerDesc.Width = 1;
            layerDesc.Height = 1;
            layerDesc.ArraySize = 2;
            layerDesc.TextureFormat = Format::R8G8B8A8_UNORM;
            layerDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            layerDesc.DebugName = "GrassGBufferTest.LayerA";
            LayerA = Device->CreateTexture(layerDesc);
            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            Sampler = Device->CreateSampler(samplerDesc);
            BufferPtr layerUpload = Device->CreateBuffer(BufferDesc(8, ResourceUsage::TransferSrc, true,
                                                                      "GrassGBufferTest.LayerUpload"));
            CommandListPtr commands = Device->CreateCommandList();
            if (!Check(Records && Args && LayerA && Sampler && layerUpload && commands,
                       "草の入力資源を作れません"))
            {
                return false;
            }
            Records->Update(&blade, sizeof(blade));
            GrassGenerateArgsBuffer args{};
            args.Args[0].IndexCount = settings.LodSegments[0] * 6u;
            args.Args[0].InstanceCount = 1;
            Args->Update(&args, sizeof(args));
            const uint8_t layerPixels[8] = {255, 255, 0, 0, 255, 255, 0, 0};
            layerUpload->Update(layerPixels, sizeof(layerPixels));
            commands->Begin();
            commands->TextureBarrier(LayerA, ResourceState::Undefined, ResourceState::CopyDest);
            commands->CopyBufferToTexture(layerUpload, LayerA, 1, 1, 0, 0, 0);
            commands->CopyBufferToTexture(layerUpload, LayerA, 1, 1, 4, 0, 1);
            commands->TextureBarrier(LayerA, ResourceState::CopyDest, ResourceState::ShaderResource);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            return true;
        }

        bool Draw(const GrassDrawConstants& constants, const GrassSpeciesGpuData& species,
                  Container::VariableArray<uint8_t>& pixels)
        {
            const GBufferPassSettings formats;
            const Format colorFormats[] = {formats.AlbedoFormat, formats.NormalFormat, formats.MaterialFormat,
                                           formats.EmissiveFormat, formats.VelocityFormat};
            TexturePtr colors[GBufferOverlayTargets::kColorTargetCount];
            RenderPassDesc clearDesc;
            FramebufferDesc framebufferDesc;
            framebufferDesc.width = Width;
            framebufferDesc.height = Height;
            for (uint32_t image = 0; image < GBufferOverlayTargets::kColorTargetCount; ++image)
            {
                TextureDesc desc = TextureDesc::RenderTarget(Width, Height, colorFormats[image], "GrassGBufferTest.Color");
                desc.Usage = desc.Usage | ResourceUsage::TransferSrc;
                colors[image] = Device->CreateTexture(desc);
                if (!Check(static_cast<bool>(colors[image]), "GBuffer のテクスチャを作れません"))
                {
                    return false;
                }
                AttachmentDesc attachment;
                attachment.format = colorFormats[image];
                attachment.clearColor[3] = 0.0f;
                attachment.initialState = ResourceState::Undefined;
                attachment.finalState = ResourceState::RenderTarget;
                clearDesc.colorAttachments.push_back(attachment);
                framebufferDesc.colorTargets.push_back(colors[image]);
            }
            TexturePtr depth = Device->CreateTexture(TextureDesc::DepthStencil(
                Width, Height, formats.DepthFormat, "GrassGBufferTest.Depth"));
            clearDesc.hasDepthStencil = true;
            clearDesc.depthStencilAttachment.format = formats.DepthFormat;
            clearDesc.depthStencilAttachment.isDepthStencil = true;
            clearDesc.depthStencilAttachment.clearDepth = 1.0f;
            clearDesc.depthStencilAttachment.initialState = ResourceState::Undefined;
            clearDesc.depthStencilAttachment.finalState = ResourceState::DepthWrite;
            RenderPassPtr clearPass = Device->CreateRenderPass(clearDesc);
            framebufferDesc.renderPass = clearPass;
            framebufferDesc.depthStencilTarget = depth;
            FramebufferPtr framebuffer = clearPass && depth ? Device->CreateFramebuffer(framebufferDesc) : nullptr;
            BufferPtr readback = Device->CreateBuffer(BufferDesc(ReadbackBytes, ResourceUsage::TransferDst, true,
                                                                  "GrassGBufferTest.Readback"));
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = framebuffer && readback && commands ? Owners.BeginFrame(0, LastSerial) : nullptr;
            if (!Check(frame != nullptr, "描画かフレームの資源を作れません"))
            {
                Owners.AbortFrame();
                return false;
            }
            const FrameUploadAllocation constantAllocation = frame->AllocateConstants(constants);
            const FrameUploadAllocation speciesAllocation = frame->AllocateUploadData(
                &species, sizeof(species), FrameUploadUsage::Storage);
            GrassDrawRecordInputs inputs;
            inputs.Constants = ToRange(constantAllocation);
            inputs.Species = ToRange(speciesAllocation);
            for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
            {
                inputs.DescriptorSets[lod] = frame->AllocateDescriptorSet(Drawer.GetDescriptorSetLayout()).Set;
                inputs.LodCapacity[lod] = 1;
            }
            if (!Check(constantAllocation.IsValid() && speciesAllocation.IsValid() &&
                       inputs.DescriptorSets[0] && inputs.DescriptorSets[1] && inputs.DescriptorSets[2],
                       "フレームの入力を借りられません"))
            {
                Owners.AbortFrame();
                return false;
            }
            for (uint32_t image = 0; image < GBufferOverlayTargets::kColorTargetCount; ++image)
            {
                inputs.ColorTargets[image] = colors[image];
            }
            inputs.DepthTarget = depth;
            inputs.Viewport.width = static_cast<float>(Width);
            inputs.Viewport.height = static_cast<float>(Height);
            inputs.RecordBuffer = Records;
            inputs.ArgsBuffer = Args;
            inputs.LayerA = LayerA;
            inputs.Sampler = Sampler;
            commands->Begin();
            commands->BeginRenderPass(clearPass, framebuffer);
            commands->EndRenderPass();
            if (!Check(Drawer.Record(commands.get(), inputs), "草の描画を記録できません"))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            commands->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest);
            const uint32_t images[] = {0, 1, 2, 4};
            const uint64_t offsets[] = {0, NormalOffset, MaterialOffset, VelocityOffset};
            for (uint32_t index = 0; index < 4; ++index)
            {
                commands->TextureBarrier(colors[images[index]], ResourceState::ShaderResource, ResourceState::CopySource);
                commands->CopyTextureToBuffer(colors[images[index]], readback, Width, Height, offsets[index]);
            }
            commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            if (!Check(Owners.CommitSubmitted(++LastSerial), "描画の提出を記録できません"))
            {
                return false;
            }
            const void* mapped = readback->Map(0, ReadbackBytes);
            if (!Check(mapped != nullptr, "GBuffer を読み戻せません"))
            {
                return false;
            }
            pixels.resize(static_cast<size_t>(ReadbackBytes));
            std::memcpy(pixels.data(), mapped, static_cast<size_t>(ReadbackBytes));
            readback->Unmap();
            return true;
        }
    };

    bool CheckPixels(const Container::VariableArray<uint8_t>& bytes, const GrassDrawConstants& constants,
                     const GrassSpeciesGpuData& species, const GrassBladeRecord& blade, bool bWind)
    {
        const char* label = bWind ? "風あり" : "風なし";
        const uint32_t tipRow = constants.LodSegments[0];
        Point2 vertexVelocity[16];
        Point2 tipPixels[2];
        float minimum[2] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        float maximum[2] = {-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
        for (uint32_t index = 0; index < (tipRow + 1u) * 2u; ++index)
        {
            vertexVelocity[index] = VertexVelocity(constants, species, blade, index);
            const float velocity[] = {vertexVelocity[index].X, vertexVelocity[index].Y};
            for (uint32_t axis = 0; axis < 2; ++axis)
            {
                minimum[axis] = std::min(minimum[axis], velocity[axis]);
                maximum[axis] = std::max(maximum[axis], velocity[axis]);
            }
        }
        for (uint32_t side = 0; side < 2; ++side)
        {
            tipPixels[side] = ProjectPixel(EvaluateGrassBladeVertexClip(constants, species, blade, 0,
                                                                        tipRow * 2u + side, false));
        }
        const Point2 rootLeft = ProjectPixel(EvaluateGrassBladeVertexClip(constants, species, blade, 0, 0, false));
        const Point2 rootRight = ProjectPixel(EvaluateGrassBladeVertexClip(constants, species, blade, 0, 1, false));
        const Point2 root = {(rootLeft.X + rootRight.X) * 0.5f, (rootLeft.Y + rootRight.Y) * 0.5f};
        const Point2 tip = {(tipPixels[0].X + tipPixels[1].X) * 0.5f, (tipPixels[0].Y + tipPixels[1].Y) * 0.5f};
        const float dx = tip.X - root.X;
        const float dy = tip.Y - root.Y;
        const float bladeLengthSquared = dx * dx + dy * dy;
        if (!Check(bladeLengthSquared > 100.0f, "投影された草が短すぎます"))
        {
            return false;
        }

        uint32_t count = 0;
        uint32_t badAlpha = 0;
        uint32_t maxAlphaError = 0;
        uint32_t badFinite = 0;
        uint32_t rootCount = 0;
        uint32_t tipCount = 0;
        float rootAo = 0.0f;
        float tipAo = 0.0f;
        float maxNormalError = 0.0f;
        float maxStaticVelocity = 0.0f;
        float maxRangeError = 0.0f;
        float closestDistanceSquared[2] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
        Point2 closestVelocity[2];
        for (uint32_t y = 0; y < Height; ++y)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                const uint32_t pixel = y * Width + x;
                const uint8_t* albedo = bytes.data() + pixel * 4u;
                if (albedo[3] <= 127u)
                {
                    continue;
                }
                ++count;
                badAlpha += albedo[3] != 255u ? 1u : 0u;
                maxAlphaError = std::max(maxAlphaError, 255u - static_cast<uint32_t>(albedo[3]));
                const uint8_t* normal = bytes.data() + NormalOffset + pixel * 8u;
                const float nx = ReadHalf(normal);
                const float ny = ReadHalf(normal + 2);
                const float nz = ReadHalf(normal + 4);
                const uint8_t* velocityBytes = bytes.data() + VelocityOffset + pixel * 4u;
                const Point2 velocity = {ReadHalf(velocityBytes), ReadHalf(velocityBytes + 2)};
                if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nz) ||
                    !std::isfinite(velocity.X) || !std::isfinite(velocity.Y))
                {
                    ++badFinite;
                    continue;
                }
                maxNormalError = std::max(maxNormalError, std::abs(std::sqrt(nx * nx + ny * ny + nz * nz) - 1.0f));
                maxStaticVelocity = std::max(maxStaticVelocity, std::max(std::abs(velocity.X), std::abs(velocity.Y)));
                const float values[] = {velocity.X, velocity.Y};
                for (uint32_t axis = 0; axis < 2; ++axis)
                {
                    maxRangeError = std::max(maxRangeError, std::max(minimum[axis] - values[axis], values[axis] - maximum[axis]));
                }
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float position = ((px - root.X) * dx + (py - root.Y) * dy) / bladeLengthSquared;
                const float ao = static_cast<float>(bytes[MaterialOffset + pixel * 4u + 2u]) / 255.0f;
                if (position >= 0.0f && position <= 0.25f)
                {
                    rootAo += ao;
                    ++rootCount;
                }
                if (position >= 0.75f && position <= 1.0f)
                {
                    tipAo += ao;
                    ++tipCount;
                }
                for (uint32_t side = 0; side < 2; ++side)
                {
                    const float sx = px - tipPixels[side].X;
                    const float sy = py - tipPixels[side].Y;
                    const float distanceSquared = sx * sx + sy * sy;
                    if (distanceSquared < closestDistanceSquared[side])
                    {
                        closestDistanceSquared[side] = distanceSquared;
                        closestVelocity[side] = velocity;
                    }
                }
            }
        }
        const float rootAverage = rootCount ? rootAo / rootCount : 0.0f;
        const float tipAverage = tipCount ? tipAo / tipCount : 0.0f;
        std::cout << TestName << ' ' << label << " 画素=" << count << " alpha不一致=" << badAlpha
                  << " alpha最大誤差=" << static_cast<float>(maxAlphaError) / 255.0f << " 法線の最大誤差=" << maxNormalError << " 非有限=" << badFinite
                  << " AO根元=" << rootAverage << " (" << rootCount << "画素) AO先端=" << tipAverage
                  << " (" << tipCount << "画素)\n";
        bool ok = Check(count >= 100u, "草の画素が 100 未満です");
        ok &= Check(badAlpha == 0u, "草の alpha が 1 ではありません");
        ok &= Check(badFinite == 0u && maxNormalError <= NormalTolerance, "法線または速度が非有限、あるいは法線が単位長ではありません");
        ok &= Check(rootCount >= 3u && tipCount >= 3u && rootAverage < tipAverage, "根元の AO が先端より暗くありません");
        if (!bWind)
        {
            std::cout << TestName << " 風なし 速度の最大絶対値=" << maxStaticVelocity << '\n';
            ok &= Check(maxStaticVelocity <= StaticVelocityTolerance, "静止時の速度が 0 ではありません");
        }
        else
        {
            float maxTipError = 0.0f;
            float maxTipDistance = 0.0f;
            float tipMovement = 0.0f;
            for (uint32_t side = 0; side < 2; ++side)
            {
                const Point2 expected = vertexVelocity[tipRow * 2u + side];
                const float error = std::max(std::abs(closestVelocity[side].X - expected.X),
                                             std::abs(closestVelocity[side].Y - expected.Y));
                maxTipError = std::max(maxTipError, error);
                maxTipDistance = std::max(maxTipDistance, std::sqrt(closestDistanceSquared[side]));
                tipMovement = std::max(tipMovement, std::max(std::abs(expected.X), std::abs(expected.Y)));
                std::cout << TestName << " 先端" << side << " CPU速度=(" << expected.X << ", " << expected.Y
                          << ") GPU速度=(" << closestVelocity[side].X << ", " << closestVelocity[side].Y
                          << ") 距離=" << std::sqrt(closestDistanceSquared[side]) << "画素 誤差=" << error << '\n';
            }
            std::cout << TestName << " 風あり CPU速度範囲 X=[" << minimum[0] << ", " << maximum[0]
                      << "] Y=[" << minimum[1] << ", " << maximum[1] << "] 範囲外の最大差=" << maxRangeError
                      << " 先端の最大誤差=" << maxTipError << '\n';
            ok &= Check(tipMovement > VelocityTolerance * 1.5f, "先端の動きが小さく速度の誤りを検出できません");
            ok &= Check(maxRangeError <= VelocityTolerance, "画素の速度が CPU 頂点の範囲から外れています");
            ok &= Check(maxTipDistance <= 3.0f && maxTipError <= VelocityTolerance,
                        "先端近傍の画素がないか速度が CPU 頂点と一致しません");
        }
        return ok;
    }

    bool RunCases(const DevicePtr& device)
    {
        GrassSpeciesGpuData species{};
        species.RootColor[0] = 0.08f;
        species.RootColor[1] = 0.25f;
        species.RootColor[2] = 0.04f;
        species.TipColor[0] = 0.5f;
        species.TipColor[1] = 0.8f;
        species.TipColor[2] = 0.15f;
        species.WetColor[0] = species.WetColor[1] = species.WetColor[2] = 0.7f;
        species.Roughness = 0.8f;
        species.WetRoughness = 0.25f;
        species.Bend = 0.08f;
        species.Stiffness = 0.5f;
        species.WindResponse = 1.0f;
        const GrassBladeRecord blade = {0.0f, 0.0f, 3.5f, PackGrassBlade(1.6f, 0.25f, 0, 1.0f)};
        const GrassGenerateSettings settings;
        CameraProxy camera;
        camera.PositionY = 0.8f;
        camera.ForwardZ = 1.0f;
        camera.FieldOfView = 45.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 20.0f;
        const CameraViewConstants view = CameraViewConstants::BuildForDevice(camera, 1.0f, device.get());
        GrassDrawConstants constants;
        view.CopyShaderView(&constants.View[0][0]);
        view.CopyShaderProjection(&constants.Projection[0][0]);
        view.CopyShaderView(&constants.PreviousView[0][0]);
        view.CopyShaderProjection(&constants.PreviousProjection[0][0]);
        constants.CameraPosition[1] = constants.PreviousCameraPosition[1] = camera.PositionY;
        constants.CameraPosition[3] = constants.PreviousCameraPosition[3] = 2.0f * std::tan(45.0f * 0.5f * 0.01745329252f) / Height;
        constants.LayerWindow[0] = 32.0f;
        constants.LayerWindow[1] = 1.0f;
        constants.LayerWindow[2] = 1.0f;
        constants.Wind[0] = 1.0f;
        constants.Wind[2] = 2.5f;
        constants.Wind[3] = 2.2f;
        constants.WindParams[0] = 2.5f;
        constants.WindParams[1] = 0.9f;
        constants.WindParams[2] = 0.8f;
        constants.WindParams[3] = 15.0f;
        constants.WindExtra[0] = 0.2f;
        constants.Flags[0] = kGrassDrawFlagPreviousCamera;
        Fixture fixture;
        fixture.Device = device;
        if (!fixture.Initialize(blade, settings))
        {
            return false;
        }
        Container::VariableArray<uint8_t> pixels;
        if (!fixture.Draw(constants, species, pixels))
        {
            return false;
        }
        bool ok = CheckPixels(pixels, constants, species, blade, false);
        constants.Flags[0] |= kGrassDrawFlagWind;
        if (!fixture.Draw(constants, species, pixels))
        {
            return false;
        }
        ok &= CheckPixels(pixels, constants, species, blade, true);
        return ok;
    }

    int RunTest()
    {
        if (IsGpuTestSkipForced())
        {
            std::cout << TestName << " スキップ: 環境変数による指定\n";
            return SkipReturnCode;
        }
        ValidationCapture capture;
        RHIDeviceDesc desc;
        desc.Api = GraphicsAPI::Vulkan;
        desc.bEnableValidation = true;
        DevicePtr device;
        try
        {
            device = CreateRHIDevice(desc);
        }
        catch (const std::exception&)
        {
            std::cout << TestName << " スキップ: Vulkan デバイスを利用できません\n";
            return SkipReturnCode;
        }
        if (!device || device->GetAPI() != API::Vulkan)
        {
            std::cout << TestName << " スキップ: Vulkan デバイスを利用できません\n";
            return SkipReturnCode;
        }
        bool ok = RunCases(device);
        device->WaitIdle();
        device.reset();
        const uint32_t errors = capture.Hits();
        std::cout << TestName << " Vulkan 検証エラー=" << errors << '\n';
        ok &= Check(errors == 0, "Vulkan の検証エラーが出ました");
        std::cout << TestName << (ok ? " 合格\n" : " 不合格\n");
        return ok ? 0 : 1;
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

// カプセル状の世界フィールドによる草の被覆の減少と、今・前の座標から再評価した速度を確かめる。
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

    constexpr const char* TestName = "GrassTrampleVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t Width = 256;
    constexpr uint32_t Height = 256;
    constexpr uint32_t PixelCount = Width * Height;
    constexpr uint64_t VelocityOffset = PixelCount * 4ull;
    constexpr uint64_t ReadbackBytes = PixelCount * 8ull;
    constexpr float VelocityTolerance = 0.002f;
    constexpr uint32_t BladeCount = 5;
    constexpr uint32_t FieldSize = 64;
    constexpr float FieldExtent = 8.0f;

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

    struct FieldImage
    {
        Container::VariableArray<uint16_t> Texels;
        float OriginXZ[2] = {};
    };

    FieldImage MakeField(float originX, float originZ, bool bPrevious)
    {
        FieldImage image;
        image.OriginXZ[0] = originX;
        image.OriginXZ[1] = originZ;
        image.Texels.resize(FieldSize * FieldSize * 4u, 0u);
        for (uint32_t z = 0; z < FieldSize; ++z)
        {
            for (uint32_t x = 0; x < FieldSize; ++x)
            {
                const float worldX = originX + (x + 0.5f) * FieldExtent / FieldSize;
                const float worldZ = originZ + (z + 0.5f) * FieldExtent / FieldSize;
                const float dz = worldZ - std::clamp(worldZ, 2.5f, 4.5f);
                // 通り道は XZ 上の P0=(0,2.5)、P1=(0,4.5)、半径 0.3 m のカプセル。
                // 既知の値を直接書くことで更新パスの試験から独立させ、線形補間も解析できる。
                if (worldX * worldX + dz * dz <= 0.3f * 0.3f)
                {
                    const uint32_t offset = (z * FieldSize + x) * 4u;
                    image.Texels[offset + 1u] = bPrevious ? 0xb000u : 0x3400u; // -0.125 / 0.25。
                    image.Texels[offset + 2u] = bPrevious ? 0x3400u : 0x3c00u; // 0.25 / 1。
                }
            }
        }
        return image;
    }

    void SampleField(const FieldImage& image, const float origin[2], float invExtent,
                     const GrassBladeRecord& blade, float (&value)[4])
    {
        const float u = (blade.PositionX - origin[0]) * invExtent;
        const float v = (blade.PositionZ - origin[1]) * invExtent;
        if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f || invExtent <= 0.0f)
        {
            return;
        }
        const float px = u * FieldSize - 0.5f;
        const float py = v * FieldSize - 0.5f;
        const int left = static_cast<int>(std::floor(px));
        const int top = static_cast<int>(std::floor(py));
        const float tx = px - left;
        const float ty = py - top;
        for (uint32_t channel = 0; channel < 4; ++channel)
        {
            for (int y = 0; y < 2; ++y)
            {
                for (int x = 0; x < 2; ++x)
                {
                    const uint32_t ix = static_cast<uint32_t>(std::clamp(left + x, 0, static_cast<int>(FieldSize) - 1));
                    const uint32_t iy = static_cast<uint32_t>(std::clamp(top + y, 0, static_cast<int>(FieldSize) - 1));
                    const uint16_t* texel = image.Texels.data() + (iy * FieldSize + ix) * 4u + channel;
                    value[channel] += ReadHalf(reinterpret_cast<const uint8_t*>(texel)) *
                        (x == 0 ? 1.0f - tx : tx) * (y == 0 ? 1.0f - ty : ty);
                }
            }
        }
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
        TexturePtr Fields[2];
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
            Fields[0].reset();
            Fields[1].reset();
            Sampler.reset();
            Records.reset();
            Args.reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            PageSource.SetDevice({});
        }

        bool Initialize(const GrassBladeRecord (&blades)[BladeCount], const GrassGenerateSettings& settings,
                        const FieldImage& current, const FieldImage& previous)
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

            Records = Device->CreateBuffer(BufferDesc(sizeof(blades), ResourceUsage::StorageBuffer |
                ResourceUsage::ShaderRead, true, "GrassTrampleTest.Records"));
            Args = Device->CreateBuffer(BufferDesc(kGrassArgsBufferSize, ResourceUsage::IndirectBuffer,
                true, "GrassTrampleTest.Args"));
            TextureDesc layerDesc;
            layerDesc.Width = 1;
            layerDesc.Height = 1;
            layerDesc.ArraySize = 2;
            layerDesc.TextureFormat = Format::R8G8B8A8_UNORM;
            layerDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            layerDesc.DebugName = "GrassTrampleTest.LayerA";
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
                                                                      "GrassTrampleTest.LayerUpload"));
            CommandListPtr commands = Device->CreateCommandList();
            if (!Check(Records && Args && LayerA && Sampler && layerUpload && commands,
                       "草の入力資源を作れません"))
            {
                return false;
            }
            Records->Update(blades, sizeof(blades));
            GrassGenerateArgsBuffer args{};
            args.Args[0].IndexCount = settings.LodSegments[0] * 6u;
            args.Args[0].InstanceCount = BladeCount;
            Args->Update(&args, sizeof(args));
            const uint8_t layerPixels[8] = {255, 255, 0, 0, 255, 255, 0, 0};
            layerUpload->Update(layerPixels, sizeof(layerPixels));
            BufferPtr fieldUploads[2];
            const FieldImage* images[] = {&current, &previous};
            for (uint32_t index = 0; index < 2; ++index)
            {
                TextureDesc fieldDesc;
                fieldDesc.Width = fieldDesc.Height = FieldSize;
                fieldDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
                fieldDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
                fieldDesc.DebugName = "GrassTrampleTest.Field";
                Fields[index] = Device->CreateTexture(fieldDesc);
                const uint32_t bytes = FieldSize * FieldSize * 8u;
                fieldUploads[index] = Device->CreateBuffer(BufferDesc(bytes, ResourceUsage::TransferSrc,
                    true, "GrassTrampleTest.FieldUpload"));
                if (!Check(Fields[index] && fieldUploads[index], "世界フィールドの資源を作れません"))
                {
                    return false;
                }
                fieldUploads[index]->Update(images[index]->Texels.data(), bytes);
            }
            commands->Begin();
            for (uint32_t index = 0; index < 2; ++index)
            {
                commands->TextureBarrier(Fields[index], ResourceState::Undefined, ResourceState::CopyDest);
                commands->CopyBufferToTexture(fieldUploads[index], Fields[index], FieldSize, FieldSize);
                commands->TextureBarrier(Fields[index], ResourceState::CopyDest, ResourceState::ShaderResource);
            }
            commands->TextureBarrier(LayerA, ResourceState::Undefined, ResourceState::CopyDest);
            commands->CopyBufferToTexture(layerUpload, LayerA, 1, 1, 0, 0, 0);
            commands->CopyBufferToTexture(layerUpload, LayerA, 1, 1, 4, 0, 1);
            commands->TextureBarrier(LayerA, ResourceState::CopyDest, ResourceState::ShaderResource);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            return true;
        }

        bool Draw(const GrassDrawWorldFieldConstants& constants, const GrassSpeciesGpuData& species,
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
                TextureDesc desc = TextureDesc::RenderTarget(Width, Height, colorFormats[image], "GrassTrampleTest.Color");
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
                Width, Height, formats.DepthFormat, "GrassTrampleTest.Depth"));
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
                                                                  "GrassTrampleTest.Readback"));
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
                inputs.LodCapacity[lod] = BladeCount;
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
            inputs.WorldFieldCurrent = Fields[0];
            inputs.WorldFieldPrevious = Fields[1];
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
            const uint32_t images[] = {0, 4};
            const uint64_t offsets[] = {0, VelocityOffset};
            for (uint32_t index = 0; index < 2; ++index)
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

    bool CheckCoverage(const Container::VariableArray<uint8_t>& baseline,
                       const Container::VariableArray<uint8_t>& trampled)
    {
        uint32_t before[2] = {};
        uint32_t after[2] = {};
        uint32_t outsideChanged = 0;
        for (uint32_t y = 0; y < Height; ++y)
        {
            for (uint32_t x = 0; x < Width; ++x)
            {
                // 中央の草は幅 0.25 m、投影した半幅は 12 画素未満。
                // 隣の根元は X=±0.5 m（中心から約44画素）なので幅40画素の帯で分離できる。
                const uint32_t region = x >= Width / 2u - 20u && x < Width / 2u + 20u ? 0u : 1u;
                const uint32_t pixel = y * Width + x;
                const bool bBefore = baseline[pixel * 4u + 3u] > 127u;
                const bool bAfter = trampled[pixel * 4u + 3u] > 127u;
                before[region] += bBefore ? 1u : 0u;
                after[region] += bAfter ? 1u : 0u;
                outsideChanged += region == 1u && bBefore != bAfter ? 1u : 0u;
            }
        }
        std::cout << TestName << " 通り道=" << before[0] << " -> " << after[0]
                  << " 外側=" << before[1] << " -> " << after[1] << " 外側の変化=" << outsideChanged << '\n';
        bool bOk = Check(before[0] >= 100u && before[1] >= 100u && after[0] >= 20u,
                         "通り道か外側の草が不足しています");
        // B=1、FlattenScale=0.7 で丈は30%。奥向きの曲げで投影面積はさらに小さくなる。
        // 量子化・三角形の先細り・ラスタ境界の余裕を取り、被覆55%以下（45%以上減少）を要求する。
        bOk &= Check(after[0] <= before[0] * 0.55f, "通り道の草の被覆が45%以上減っていません");
        // 外の4本はカプセルと補間範囲から離れている。2%または4画素を端の丸めの余裕とする。
        bOk &= Check(outsideChanged <= std::max(4u, before[1] / 50u), "通り道の外の被覆が大きく変わりました");
        return bOk;
    }

    Math::Vector4 Transform(const float (&matrix)[4][4], const Math::Vector4& value)
    {
        float result[4];
        for (uint32_t row = 0; row < 4; ++row)
        {
            result[row] = ((matrix[0][row] * value.x + matrix[1][row] * value.y) +
                matrix[2][row] * value.z) + matrix[3][row] * value.w;
        }
        return Math::Vector4(result[0], result[1], result[2], result[3]);
    }

    Math::Vector4 VertexClip(const GrassDrawWorldFieldConstants& constants, const GrassSpeciesGpuData& species,
                            const GrassBladeRecord& blade, uint32_t index, const FieldImage& image, bool bPrevious)
    {
        const GrassDrawConstants& draw = constants.Draw;
        const WorldFieldSampleConstants& field = constants.WorldField;
        float sample[4] = {};
        SampleField(image, bPrevious ? field.PreviousOriginXZ : field.CurrentOriginXZ,
                    bPrevious ? field.PreviousInvExtent : field.CurrentInvExtent, blade, sample);
        const float h = (static_cast<float>(blade.Packed & 1023u) / 1023.0f) * draw.Quant[0];
        const float t = static_cast<float>(GrassBladeIndexRow(index)) / draw.LodSegments[0];
        float displacement[3];
        WorldFieldDisplacement(sample, h, t, displacement, field.BendScale, field.FlattenScale);
        // 中央の根元 X=0、RG は Z 方向のみ、風・自然な傾き・最小画素幅は無効。
        // カメラへの水平向きが変わらないので、既存の無変形頂点へ共通式のずれを加えられる。
        // 根元は近接フェードの外、fade=1 に固定しているため h は復号した丈そのもの。
        const Math::Vector3 base = EvaluateGrassBladeVertex(draw, species, blade, 0, index, bPrevious);
        const Math::Vector4 world(base.x + displacement[0], base.y + displacement[1], base.z + displacement[2], 1.0f);
        return Transform(bPrevious ? draw.PreviousProjection : draw.Projection,
                         Transform(bPrevious ? draw.PreviousView : draw.View, world));
    }

    bool Barycentric(const Point2 (&triangle)[3], float x, float y, float (&weights)[3])
    {
        const float determinant = (triangle[1].Y - triangle[2].Y) * (triangle[0].X - triangle[2].X) +
            (triangle[2].X - triangle[1].X) * (triangle[0].Y - triangle[2].Y);
        if (std::abs(determinant) < 1.0e-6f)
        {
            return false;
        }
        weights[0] = ((triangle[1].Y - triangle[2].Y) * (x - triangle[2].X) +
            (triangle[2].X - triangle[1].X) * (y - triangle[2].Y)) / determinant;
        weights[1] = ((triangle[2].Y - triangle[0].Y) * (x - triangle[2].X) +
            (triangle[0].X - triangle[2].X) * (y - triangle[2].Y)) / determinant;
        weights[2] = 1.0f - weights[0] - weights[1];
        return true;
    }

    bool CheckVelocity(const Container::VariableArray<uint8_t>& pixels, const GrassDrawWorldFieldConstants& constants,
                       const GrassSpeciesGpuData& species, const GrassBladeRecord& blade,
                       const FieldImage& current, const FieldImage& previous)
    {
        const uint32_t row = constants.Draw.LodSegments[0] - 1u;
        const uint32_t indices[2][3] = {{row * 2u, row * 2u + 1u, row * 2u + 2u},
                                      {row * 2u + 1u, row * 2u + 3u, row * 2u + 2u}};
        float bestMargin = 0.08f;
        uint32_t selectedPixel = PixelCount;
        Point2 expected;
        for (uint32_t triangle = 0; triangle < 2; ++triangle)
        {
            Math::Vector4 now[3];
            Math::Vector4 before[3];
            Point2 projected[3];
            for (uint32_t vertex = 0; vertex < 3; ++vertex)
            {
                now[vertex] = VertexClip(constants, species, blade, indices[triangle][vertex], current, false);
                before[vertex] = VertexClip(constants, species, blade, indices[triangle][vertex], previous, true);
                projected[vertex] = ProjectPixel(now[vertex]);
            }
            // GPU の画素を探さず、先端の最後の帯の内側にある画素中心を CPU 投影だけで選ぶ。
            for (uint32_t y = 0; y < Height; ++y)
            {
                for (uint32_t x = 0; x < Width; ++x)
                {
                    float weights[3];
                    if (!Barycentric(projected, x + 0.5f, y + 0.5f, weights))
                    {
                        continue;
                    }
                    const float margin = std::min(weights[0], std::min(weights[1], weights[2]));
                    if (margin <= bestMargin)
                    {
                        continue;
                    }
                    bestMargin = margin;
                    selectedPixel = y * Width + x;
                    // fragCurrentClip / fragPreviousClip は透視補正補間される。
                    // 共通の正規化分母は clip.xy / clip.w で消えるため、1/current.w の重みだけで求める。
                    Math::Vector4 currentClip(0.0f, 0.0f, 0.0f, 0.0f);
                    Math::Vector4 previousClip(0.0f, 0.0f, 0.0f, 0.0f);
                    for (uint32_t vertex = 0; vertex < 3; ++vertex)
                    {
                        const float weight = weights[vertex] / now[vertex].w;
                        currentClip = currentClip + now[vertex] * weight;
                        previousClip = previousClip + before[vertex] * weight;
                    }
                    expected = {(currentClip.x / currentClip.w - previousClip.x / previousClip.w) * 0.5f,
                                (currentClip.y / currentClip.w - previousClip.y / previousClip.w) * 0.5f};
                }
            }
        }
        if (!Check(selectedPixel < PixelCount, "先端の帯の内部に解析用の画素がありません"))
        {
            return false;
        }
        const uint8_t* velocity = pixels.data() + VelocityOffset + selectedPixel * 4u;
        const Point2 actual = {ReadHalf(velocity), ReadHalf(velocity + 2u)};
        std::cout << TestName << " 先端画素=(" << selectedPixel % Width << ", " << selectedPixel / Width
                  << ") CPU速度=(" << expected.X << ", " << expected.Y << ") GPU速度=("
                  << actual.X << ", " << actual.Y << ")\n";
        bool bOk = Check(pixels[selectedPixel * 4u + 3u] == 255u, "解析した先端画素に草がありません");
        bOk &= Check(std::max(std::abs(expected.X), std::abs(expected.Y)) > VelocityTolerance * 4.0f,
                     "今と前の差が小さく、履歴の取り違えを検出できません");
        bOk &= Check(std::isfinite(actual.X) && std::isfinite(actual.Y) &&
                     std::abs(actual.X - expected.X) <= VelocityTolerance &&
                     std::abs(actual.Y - expected.Y) <= VelocityTolerance,
                     "先端の速度が今・前の画像と原点による解析値から0.002を超えて外れました");
        return bOk;
    }

    bool RunCases(const DevicePtr& device)
    {
        GrassSpeciesGpuData species{};
        species.RootColor[0] = species.TipColor[0] = 0.1f;
        species.RootColor[1] = species.TipColor[1] = 0.8f;
        species.RootColor[2] = species.TipColor[2] = 0.1f;
        species.WetColor[0] = species.WetColor[1] = species.WetColor[2] = 1.0f;
        species.Roughness = 0.8f;
        species.Bend = species.WindResponse = species.WetDroop = 0.0f;
        GrassBladeRecord blades[BladeCount];
        for (uint32_t index = 0; index < BladeCount; ++index)
        {
            blades[index] = {(static_cast<float>(index) - 2.0f) * 0.5f, 0.0f, 3.5f,
                             PackGrassBlade(1.6f, 0.25f, 0, 1.0f)};
        }
        const GrassGenerateSettings settings;
        const FieldImage current = MakeField(-4.0f, 0.0f, false);
        const FieldImage previous = MakeField(-3.0f, -1.0f, true);
        CameraProxy camera;
        camera.PositionY = 0.8f;
        camera.ForwardZ = 1.0f;
        camera.FieldOfView = 45.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 20.0f;
        const CameraViewConstants view = CameraViewConstants::BuildForDevice(camera, 1.0f, device.get());
        GrassDrawWorldFieldConstants constants;
        GrassDrawConstants& draw = constants.Draw;
        view.CopyShaderView(&draw.View[0][0]);
        view.CopyShaderProjection(&draw.Projection[0][0]);
        view.CopyShaderView(&draw.PreviousView[0][0]);
        view.CopyShaderProjection(&draw.PreviousProjection[0][0]);
        draw.CameraPosition[1] = draw.PreviousCameraPosition[1] = camera.PositionY;
        draw.CameraPosition[3] = draw.PreviousCameraPosition[3] =
            2.0f * std::tan(45.0f * 0.5f * 0.01745329252f) / Height;
        draw.Surface[1] = 0.0f;
        draw.LayerWindow[0] = 32.0f;
        draw.LayerWindow[1] = draw.LayerWindow[2] = 1.0f;
        draw.Flags[0] = kGrassDrawFlagPreviousCamera | kGrassDrawFlagWorldField;
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            constants.WorldField.CurrentOriginXZ[axis] = current.OriginXZ[axis];
            constants.WorldField.PreviousOriginXZ[axis] = previous.OriginXZ[axis];
        }
        constants.WorldField.CurrentInvExtent = constants.WorldField.PreviousInvExtent = 1.0f / FieldExtent;
        constants.WorldField.BendScale = WorldFieldDefaultBendScale;
        constants.WorldField.FlattenScale = WorldFieldDefaultFlattenScale;
        Fixture fixture;
        fixture.Device = device;
        if (!fixture.Initialize(blades, settings, current, previous))
        {
            return false;
        }
        Container::VariableArray<uint8_t> baseline;
        Container::VariableArray<uint8_t> trampled;
        if (!fixture.Draw(constants, species, baseline))
        {
            return false;
        }
        constants.WorldField.bEnabled = 1u;
        if (!fixture.Draw(constants, species, trampled))
        {
            return false;
        }
        const bool bCoverage = CheckCoverage(baseline, trampled);
        const bool bVelocity = CheckVelocity(trampled, constants, species, blades[2], current, previous);
        return bCoverage && bVelocity;
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
} // 無名名前空間

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

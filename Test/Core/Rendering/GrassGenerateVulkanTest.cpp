// 草の生成数と容量を超えたときの間接引数を、CPU の写しと GPU の読み戻しで確かめる。
#include "Rendering/CameraViewConstants.h"
#include "Rendering/FrameResourceOwner.h"
#include "Rendering/GrassPass.h"
#include "Rendering/ShaderManager.h"
#include "Math/MatrixUtils.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
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
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "GrassGenerateVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t WindowSize = 8;
    constexpr uint32_t Resolution = 4;

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

    GrassFieldRenderData MakeField()
    {
        GrassFieldRenderData field;
        field.ChunkSizeMeters = 32.0f;
        field.LayerAResolution = Resolution;
        field.LayerBResolution = Resolution;
        field.ContentRevision = 1;
        // 前方の 3 距離帯と後方を覆い、(0, 1) だけ密度を 0 にする。
        for (int32_t z = -1; z <= 3; ++z)
        {
            for (int32_t x = -1; x <= 0; ++x)
            {
                GrassFieldChunkRenderData chunk;
                chunk.ChunkX = x;
                chunk.ChunkZ = z;
                chunk.LayerAByteOffset = static_cast<uint32_t>(field.LayerAPixels.size());
                chunk.LayerBByteOffset = static_cast<uint32_t>(field.LayerBPixels.size());
                field.Chunks.push_back(chunk);
                for (uint32_t pixel = 0; pixel < Resolution * Resolution; ++pixel)
                {
                    field.LayerAPixels.push_back(x == 0 && z == 1 ? 0 : 255);
                    field.LayerAPixels.push_back(255);
                    field.LayerAPixels.push_back(128);
                    field.LayerAPixels.push_back(0);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(128);
                    field.LayerBPixels.push_back(0);
                }
            }
        }
        GrassSpeciesGpuData species{};
        species.HeightMinMeters = 0.5f;
        species.HeightMaxMeters = 1.0f;
        species.BladeWidthMeters = 0.01f;
        species.BladesPerSquareMeter = 128.0f;
        field.Species.push_back(species);
        return field;
    }

    GrassTileInfo MakeTile(int32_t x, int32_t z, uint32_t band, const GrassGenerateSettings& settings)
    {
        GrassTileInfo tile;
        tile.TileX = x;
        tile.TileZ = z;
        tile.OriginX = static_cast<float>(x) * settings.TileSizeMeters;
        tile.OriginZ = static_cast<float>(z) * settings.TileSizeMeters;
        tile.GroundY = settings.GroundHeightMeters;
        tile.Band = band;
        tile.LayerIndex = GrassWindowLayerIndex(GrassFloorDiv(x, 4), GrassFloorDiv(z, 4), WindowSize);
        return tile;
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
        GrassFieldGpuWindow Window{GrassFieldGpuWindow::Settings{WindowSize, 32}};
        GrassGenerateGpu Generator;
        TexturePtr DummyHiZ;
        SamplerPtr Sampler;
        uint64_t LastSerial = 0;

        ~Fixture()
        {
            if (Device)
            {
                Device->WaitIdle();
            }
            Owners.Reset();
            Window.Reset();
            Generator.Shutdown();
            DummyHiZ.reset();
            Sampler.reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            PageSource.SetDevice({});
        }

        bool Initialize(const GrassFieldRenderData& field)
        {
            PageSource.SetDevice(Device);
            Owners.ConfigureUpload(&PageSource, 1u << 20);
            if (!Check(Owners.ConfigureDescriptors(Device.get()), "記述子のプールを作れません") ||
                !Check(Generator.Initialize(Device.get()), "草生成パイプラインを作れません"))
            {
                return false;
            }

            TextureDesc desc;
            desc.Width = 1;
            desc.Height = 1;
            desc.TextureFormat = Format::R32_FLOAT;
            desc.Usage = ResourceUsage::ShaderRead | ResourceUsage::TransferDst;
            desc.DebugName = "GrassTest.DummyHiZ";
            DummyHiZ = Device->CreateTexture(desc);
            SamplerDesc samplerDesc;
            samplerDesc.filterMin = FilterMode::Point;
            samplerDesc.filterMag = FilterMode::Point;
            samplerDesc.filterMip = FilterMode::Point;
            samplerDesc.addressU = TextureAddressMode::Clamp;
            samplerDesc.addressV = TextureAddressMode::Clamp;
            samplerDesc.addressW = TextureAddressMode::Clamp;
            Sampler = Device->CreateSampler(samplerDesc);
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = Owners.BeginFrame(0, LastSerial);
            if (!Check(DummyHiZ && Sampler && commands && frame, "窓の更新に必要な資源を作れません"))
            {
                Owners.AbortFrame();
                return false;
            }
            const float depth = 1.0f;
            DummyHiZ->Update(&depth, sizeof(depth), sizeof(depth));
            commands->Begin();
            if (!Check(Window.Update(Device.get(), commands.get(), frame, field, 0.0f, 0.0f),
                       "草の場を窓へ転送できません"))
            {
                Owners.AbortFrame();
                return false;
            }
            for (const GrassFieldChunkRenderData& chunk : field.Chunks)
            {
                if (!Check(Window.IsResident(chunk.ChunkX, chunk.ChunkZ), "チャンクが窓に載っていません"))
                {
                    Owners.AbortFrame();
                    return false;
                }
            }
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            return Check(Owners.CommitSubmitted(++LastSerial), "窓の更新を提出できません");
        }

        bool Run(const GrassFieldRenderData& field, const GrassGenerateSettings& settings,
                 const GrassGenerateConstants& constants, const GrassTileInfo* tiles, uint32_t tileCount,
                 GrassGenerateArgsBuffer& output)
        {
            if (!Check(tileCount != 0 && Generator.EnsureBuffers(settings), "生成バッファを用意できません"))
            {
                return false;
            }
            BufferPtr readback = Device->CreateBuffer(BufferDesc(kGrassArgsBufferSize,
                ResourceUsage::TransferDst, true, "GrassTest.Readback"));
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = Owners.BeginFrame(0, LastSerial);
            if (!Check(readback && commands && frame, "生成に必要な資源を作れません"))
            {
                Owners.AbortFrame();
                return false;
            }
            const FrameUploadAllocation constantAllocation = frame->AllocateConstants(constants);
            const FrameUploadAllocation tileAllocation = frame->AllocateUploadData(
                tiles, static_cast<uint64_t>(tileCount) * sizeof(GrassTileInfo), FrameUploadUsage::Storage);
            const FrameUploadAllocation speciesAllocation = frame->AllocateUploadData(
                field.Species.data(), field.Species.size() * sizeof(GrassSpeciesGpuData), FrameUploadUsage::Storage);
            const FrameDescriptorAllocation descriptors = frame->AllocateDescriptorSet(Generator.GetDescriptorSetLayout());
            if (!Check(constantAllocation.IsValid() && tileAllocation.IsValid() && speciesAllocation.IsValid() &&
                       descriptors.IsValid(), "フレームの入力を借りられません"))
            {
                Owners.AbortFrame();
                return false;
            }
            GrassGenerateRecordInputs inputs;
            inputs.DescriptorSet = descriptors.Set;
            inputs.Constants = ToRange(constantAllocation);
            inputs.Tiles = ToRange(tileAllocation);
            inputs.Species = ToRange(speciesAllocation);
            inputs.LayerA = Window.GetLayerATexture();
            inputs.LayerB = Window.GetLayerBTexture();
            inputs.FallbackHiZ = DummyHiZ;
            inputs.Sampler = Sampler;
            inputs.TileCount = tileCount;
            commands->Begin();
            if (!Check(Generator.Record(commands.get(), inputs), "草の生成を記録できません"))
            {
                Owners.AbortFrame();
                return false;
            }
            commands->BufferBarrier(Generator.GetArgsBuffer(), ResourceState::IndirectArgument, ResourceState::CopySource);
            commands->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest);
            commands->CopyBuffer(Generator.GetArgsBuffer(), readback, kGrassArgsBufferSize);
            commands->BufferBarrier(Generator.GetArgsBuffer(), ResourceState::CopySource, ResourceState::IndirectArgument);
            commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            if (!Check(Owners.CommitSubmitted(++LastSerial), "生成を提出できません"))
            {
                return false;
            }
            const void* mapped = readback->Map(0, kGrassArgsBufferSize);
            if (!Check(mapped != nullptr, "生成結果を読み戻せません"))
            {
                return false;
            }
            std::memcpy(&output, mapped, sizeof(output));
            readback->Unmap();
            return true;
        }
    };

    bool CheckCounts(const char* name, const GrassGenerateCounts& expected, const GrassGenerateArgsBuffer& actual,
                     const GrassGenerateSettings& settings)
    {
        bool ok = true;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            const uint32_t expectedInstances = std::min(expected.PerLod[lod], settings.LodCapacity[lod]);
            std::cout << TestName << ' ' << name << " LOD=" << lod << " 写し=" << expected.PerLod[lod]
                      << " GPU=" << actual.Counter[lod] << " 描画数=" << actual.Args[lod].InstanceCount
                      << " 容量=" << settings.LodCapacity[lod] << '\n';
            ok &= Check(actual.Counter[lod] == expected.PerLod[lod], "生成数が CPU の写しと一致しません");
            ok &= Check(actual.Args[lod].InstanceCount == expectedInstances &&
                        actual.Args[lod].InstanceCount <= settings.LodCapacity[lod], "間接描画の数が容量と一致しません");
        }
        return ok;
    }

    bool RunCases(const DevicePtr& device)
    {
        const GrassFieldRenderData field = MakeField();
        const GrassGenerateSettings settings;
        const Math::Vector3 cameraPosition(0.0f, 1.7f, 0.0f);
        CameraProxy camera;
        camera.PositionX = cameraPosition.x;
        camera.PositionY = cameraPosition.y;
        camera.PositionZ = cameraPosition.z;
        camera.ForwardX = 0.0f;
        camera.ForwardY = 0.0f;
        camera.ForwardZ = 1.0f;
        camera.UpX = 0.0f;
        camera.UpY = 1.0f;
        camera.UpZ = 0.0f;
        camera.FieldOfView = 90.0f;
        camera.NearPlane = 0.1f;
        camera.FarPlane = 200.0f;
        const auto view = CameraViewConstants::BuildForDevice(camera, 1.0f, nullptr);
        const auto planes = Math::MatrixUtils::ExtractClipSpaceFrustumPlanes(
            view.CullingViewProjectionMatrix, Math::ClipSpaceDepthRange::ZeroToOne);
        GrassTileSelectionInput selection;
        selection.CameraPosition = cameraPosition;
        selection.WindowSize = WindowSize;
        planes.CopyToShaderData(selection.FrustumPlanes);
        GrassTileSelector selector;
        Container::VariableArray<GrassTileInfo> tiles;
        const uint32_t selected = selector.Select(field, settings, selection, tiles);
        if (!Check(selected != 0 && selected == tiles.size(), "選別したタイルがありません"))
        {
            return false;
        }
        const GrassGenerateConstants constants = BuildGrassGenerateConstants(
            settings, cameraPosition, planes.Planes, Resolution, Resolution, field.ChunkSizeMeters);
        if (!Check((constants.Flags[0] & kGrassGenerateFlagHiZ) == 0, "HZB が無効ではありません"))
        {
            return false;
        }
        const GrassGenerateCounts expected = RunGrassGenerateReference(field, constants, tiles.data(), selected);
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            if (!Check(expected.PerLod[lod] > 1, "各 LOD の生成数が容量超過を試せる数ではありません"))
            {
                return false;
            }
        }
        Fixture fixture;
        fixture.Device = device;
        if (!fixture.Initialize(field))
        {
            return false;
        }
        GrassGenerateArgsBuffer actual{};
        if (!fixture.Run(field, settings, constants, tiles.data(), selected, actual))
        {
            return false;
        }
        bool ok = CheckCounts("通常", expected, actual, settings);

        // 密度 0 のタイルは視錐台の中、後方のタイルは視錐台の外に置く。
        const GrassTileInfo excluded[] = {MakeTile(0, -4, 0, settings), MakeTile(1, 5, 1, settings)};
        const char* names[] = {"後方", "密度ゼロ"};
        GrassGenerateConstants withoutFrustum = constants;
        for (auto& plane : withoutFrustum.FrustumPlanes)
        {
            plane[0] = 0.0f;
            plane[1] = 0.0f;
            plane[2] = 0.0f;
            plane[3] = 1000.0f;
        }
        const GrassGenerateCounts behindWithoutFrustum =
            RunGrassGenerateReference(field, withoutFrustum, &excluded[0], 1);
        ok &= Check(behindWithoutFrustum.PerLod[0] > 0, "後方タイルは視錐台判定を外すと生成される必要があります");
        const Math::Vector3 zeroCenter(12.0f, 0.75f, 44.0f);
        for (const auto& plane : planes.Planes)
        {
            ok &= Check(plane.x * zeroCenter.x + plane.y * zeroCenter.y + plane.z * zeroCenter.z + plane.w > 2.0f,
                        "密度ゼロのタイルが視錐台の内側にありません");
        }
        for (uint32_t index = 0; index < 2; ++index)
        {
            const GrassGenerateCounts zero = RunGrassGenerateReference(field, constants, &excluded[index], 1);
            for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
            {
                ok &= Check(zero.PerLod[lod] == 0, "除外するタイルの写しがゼロではありません");
            }
            actual = {};
            if (!fixture.Run(field, settings, constants, &excluded[index], 1, actual))
            {
                return false;
            }
            ok &= CheckCounts(names[index], zero, actual, settings);
        }

        GrassGenerateSettings limited = settings;
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            limited.LodCapacity[lod] = expected.PerLod[lod] / 2;
        }
        const GrassGenerateConstants limitedConstants = BuildGrassGenerateConstants(
            limited, cameraPosition, planes.Planes, Resolution, Resolution, field.ChunkSizeMeters);
        const GrassGenerateCounts limitedExpected = RunGrassGenerateReference(field, limitedConstants, tiles.data(), selected);
        for (uint32_t lod = 0; lod < kGrassLodCount; ++lod)
        {
            ok &= Check(limitedExpected.PerLod[lod] == expected.PerLod[lod] &&
                        limitedExpected.PerLod[lod] > limited.LodCapacity[lod], "容量を超える入力になっていません");
        }
        actual = {};
        if (!fixture.Run(field, limited, limitedConstants, tiles.data(), selected, actual))
        {
            return false;
        }
        ok &= CheckCounts("容量超過", limitedExpected, actual, limited);
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

// 空の遮蔽の GPU 出力を CPU の積分と、移動・遮蔽物変更後の全更新に照らす。
// 各段で遮蔽物の外接矩形を最大積分距離だけ広げた領域は全画素、それ以外は各軸 32 画素おきに照合する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/RenderGraph/RenderGraph.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/SkyOcclusionMapPass.h"
#include "Rendering/SkyOcclusionReference.h"
#include "Rendering/ViewRenderContext.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
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

namespace NorvesLib::Core::Rendering
{
    struct SkyOcclusionMapVulkanTestAccess
    {
        static RHI::TexturePtr Occluders(const SkyOcclusionMapPass& pass) { return pass.m_OccludersTexture; }
        static RHI::TexturePtr Visibility(const SkyOcclusionMapPass& pass) { return pass.m_VisibilityTexture; }
        static RHI::TexturePtr Aux(const SkyOcclusionMapPass& pass) { return pass.m_AuxTexture; }
        static uint32_t SplatTiles(const SkyOcclusionMapPass& pass)
        {
            uint32_t count = 0;
            for (const auto& dispatch : pass.m_SplatDispatches)
            {
                count += dispatch.TileCount;
            }
            return count;
        }
    };
}

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core;
    using namespace NorvesLib::Core::Rendering;
    using namespace NorvesLib::RHI;

    constexpr const char* TestName = "SkyOcclusionMapVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t Resolution = 256u;
    constexpr uint32_t Channels = 4u;
    constexpr uint64_t PixelsPerLayer = static_cast<uint64_t>(Resolution) * Resolution;
    constexpr uint64_t FloatLayerBytes = PixelsPerLayer * Channels * sizeof(float);
    constexpr uint64_t HalfLayerBytes = PixelsPerLayer * Channels * sizeof(uint16_t);

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
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || !value)
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

    float DecodeHalf(uint16_t bits)
    {
        const uint32_t exponent = (bits >> 10u) & 31u;
        const uint32_t mantissa = bits & 1023u;
        const float sign = (bits & 0x8000u) ? -1.0f : 1.0f;
        if (exponent == 31u)
        {
            return mantissa == 0u ? sign * std::numeric_limits<float>::infinity()
                                  : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * (exponent == 0u ? std::ldexp(static_cast<float>(mantissa), -24)
            : std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15));
    }

    struct Images
    {
        Container::VariableArray<float> Occluders;
        Container::VariableArray<uint16_t> Visibility;
        Container::VariableArray<uint16_t> Aux;
        TexturePtr Textures[3];
        uint32_t SplatTiles = 0u;
    };

    struct Fixture
    {
        DevicePtr Device;
        ShaderManager Shaders;
        DeviceFrameUploadPageSource PageSource;
        FrameResourceOwners Owners;
        RenderGraph Graph;
        uint64_t Serial = 0u;

        ~Fixture()
        {
            if (Device)
            {
                Device->WaitIdle();
            }
            Graph.Shutdown();
            Owners.Reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            Shaders.Shutdown();
            PageSource.SetDevice({});
        }

        bool Initialize()
        {
            PageSource.SetDevice(Device);
            Owners.ConfigureUpload(&PageSource, 1u << 20u);
            return Check(Owners.ConfigureDescriptors(Device.get()), "記述子のプールを作れません") &&
                Check(Shaders.Initialize(Device.get(), NORVES_SOURCE_ROOT "/Assets/Shaders"),
                      "シェーダーの読み込みを初期化できません") &&
                Check(Graph.Initialize(nullptr), "レンダーグラフを初期化できません");
        }

        bool InitializePass(SkyOcclusionMapPass& pass)
        {
            ViewRenderContext context;
            context.Device = Device.get();
            context.ShaderMgr = &Shaders;
            return Check(pass.Initialize(context), "空の遮蔽パスを初期化できません");
        }

        bool Run(SkyOcclusionMapPass& pass, const SceneProxy& scene, Images& output)
        {
            const uint32_t levels = scene.SkyOcclusion.LevelCount;
            const uint64_t sizes[3] = {FloatLayerBytes * levels, HalfLayerBytes * levels, HalfLayerBytes * levels};
            BufferPtr readbacks[3];
            for (uint32_t index = 0; index < 3u; ++index)
            {
                readbacks[index] = Device->CreateBuffer(BufferDesc(sizes[index], ResourceUsage::TransferDst,
                    true, "SkyOcclusionTest.Readback"));
                if (!Check(static_cast<bool>(readbacks[index]), "読み戻しバッファを作れません"))
                {
                    return false;
                }
            }
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = Owners.BeginFrame(0u, Serial);
            if (!Check(commands && frame, "フレーム資源を用意できません"))
            {
                Owners.AbortFrame();
                return false;
            }
            CameraProxy camera;
            ViewRenderContext context;
            context.Device = Device.get();
            context.ShaderMgr = &Shaders;
            context.Capabilities = &Device->GetCapabilities();
            context.CommandList = commands.get();
            context.MainCamera = &camera;
            context.SnapshotScene = &scene;
            context.FrameResources = frame;
            context.FrameNumber = Serial + 1u;
            commands->Begin();
            Graph.BeginFrame(Serial + 1u);
            Graph.AddPass(&pass);
            if (!Check(Graph.Compile(context), "レンダーグラフをコンパイルできません"))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            output.SplatTiles = SkyOcclusionMapVulkanTestAccess::SplatTiles(pass);
            const RenderGraphExecutionResult result = Graph.ExecuteWithResult(context);
            output.Textures[0] = SkyOcclusionMapVulkanTestAccess::Occluders(pass);
            output.Textures[1] = SkyOcclusionMapVulkanTestAccess::Visibility(pass);
            output.Textures[2] = SkyOcclusionMapVulkanTestAccess::Aux(pass);
            if (!Check(result.bSuccess && result.ExecutedPassCount == 1u && context.SkyOcclusion.bValid &&
                       output.Textures[0] && output.Textures[1] && output.Textures[2],
                       "空の遮蔽パスの出力を取得できません"))
            {
                commands->End();
                Owners.AbortFrame();
                return false;
            }
            for (uint32_t image = 0; image < 3u; ++image)
            {
                commands->TextureBarrier(output.Textures[image], ResourceState::ShaderResource, ResourceState::CopySource);
                commands->BufferBarrier(readbacks[image], ResourceState::Undefined, ResourceState::CopyDest);
                const uint64_t layerBytes = image == 0u ? FloatLayerBytes : HalfLayerBytes;
                for (uint32_t level = 0; level < levels; ++level)
                {
                    commands->CopyTextureToBuffer(output.Textures[image], readbacks[image], Resolution, Resolution,
                                                  layerBytes * level, 0u, level);
                }
                commands->BufferBarrier(readbacks[image], ResourceState::CopyDest, ResourceState::HostRead);
                commands->TextureBarrier(output.Textures[image], ResourceState::CopySource, ResourceState::ShaderResource);
            }
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            if (!Check(Owners.CommitSubmitted(++Serial), "フレームを提出できません"))
            {
                return false;
            }
            output.Occluders.resize(static_cast<size_t>(PixelsPerLayer * Channels * levels));
            output.Visibility.resize(static_cast<size_t>(PixelsPerLayer * Channels * levels));
            output.Aux.resize(static_cast<size_t>(PixelsPerLayer * Channels * levels));
            void* destinations[3] = {output.Occluders.data(), output.Visibility.data(), output.Aux.data()};
            for (uint32_t image = 0; image < 3u; ++image)
            {
                const void* mapped = readbacks[image]->Map(0u, sizes[image]);
                if (!Check(mapped != nullptr, "画像を読み戻せません"))
                {
                    return false;
                }
                std::memcpy(destinations[image], mapped, static_cast<size_t>(sizes[image]));
                readbacks[image]->Unmap();
            }
            return true;
        }
    };

    SkyOccluderProxy MakeOccluder(float x, float y, float z, float rx, float ry, float rz,
                                  bool box, bool canopy, float opacity = 1.0f)
    {
        SkyOccluderProxy proxy;
        proxy.CenterX = x;
        proxy.CenterY = y;
        proxy.CenterZ = z;
        proxy.HalfExtentX = rx;
        proxy.HalfExtentY = ry;
        proxy.HalfExtentZ = rz;
        proxy.Opacity = opacity;
        SetSkyOccluderShape(proxy, box ? SkyOccluderShape::Box : SkyOccluderShape::Ellipsoid);
        SetSkyOccluderClass(proxy, canopy ? SkyOccluderClass::Canopy : SkyOccluderClass::Solid);
        return proxy;
    }

    SceneProxy MakeScene(uint32_t sceneIndex)
    {
        SceneProxy scene;
        scene.SkyOcclusion.bEnabled = true;
        scene.SkyOcclusion.bUseCenterOverride = true;
        scene.SkyOcclusionRevision = 1u;
        if (sceneIndex == 0u)
        {
            scene.SkyOccluders.push_back(MakeOccluder(0.37f, 6.0f, -0.21f, 4.0f, 2.5f, 4.0f, false, true, 0.7f));
        }
        else if (sceneIndex == 1u)
        {
            scene.SkyOccluders.push_back(MakeOccluder(0.31f, 6.0f, 0.37f, 20.0f, 6.0f, 1.25f, true, false));
        }
        else
        {
            scene.SkyOccluders.push_back(MakeOccluder(-4.0f, 0.75f, -1.0f, 1.5f, 0.75f, 1.3f, true, false));
            scene.SkyOccluders.push_back(MakeOccluder(0.5f, 1.1f, 1.5f, 1.7f, 1.1f, 1.6f, true, false));
            scene.SkyOccluders.push_back(MakeOccluder(4.0f, 1.5f, -1.5f, 1.2f, 1.5f, 1.4f, true, false));
        }
        return scene;
    }

    struct Maximum
    {
        float Difference = 0.0f;
        float Actual = 0.0f;
        float Expected = 0.0f;
        uint32_t Level = 0u;
        int32_t X = 0;
        int32_t Z = 0;
        uint32_t Channel = 0u;

        void Consider(float difference, float actual, float expected, uint32_t level,
                      int32_t x, int32_t z, uint32_t channel = 0u)
        {
            if (!std::isfinite(difference))
            {
                difference = std::numeric_limits<float>::infinity();
            }
            if (difference > Difference)
            {
                Difference = difference;
                Actual = actual;
                Expected = expected;
                Level = level;
                X = x;
                Z = z;
                Channel = channel;
            }
        }

        void Print(const char* name) const
        {
            std::cerr << name << " 最大差=" << Difference << " 段=" << Level << " 座標=(" << X << ',' << Z
                      << ") 成分=" << Channel << " GPU=" << Actual << " 比較先=" << Expected << '\n';
        }
    };

    bool CompareReference(const char* name, const SceneProxy& scene, const Images& images)
    {
        const SkyOcclusionParameters& parameters = scene.SkyOcclusion;
        Maximum maxB;
        Maximum maxC;
        Container::VariableArray<SkyOcclusionReferenceGrid> grids;
        grids.reserve(parameters.LevelCount);
        const SkyOcclusionReferenceGrid* levelPointers[SkyOcclusionMaxLevelCount] = {};
        int32_t origins[SkyOcclusionMaxLevelCount][2] = {};
        for (uint32_t level = 0u; level < parameters.LevelCount; ++level)
        {
            const float scale = GetSkyOcclusionLevelTexelMeters(parameters, level);
            ComputeSkyOcclusionWindowOrigin(parameters, level, parameters.CenterOverrideX, parameters.CenterOverrideZ,
                                            origins[level][0], origins[level][1]);
            grids.push_back(BuildSkyOcclusionReferenceGrid(parameters,
                Container::Span<const SkyOccluderProxy>(scene.SkyOccluders.data(), scene.SkyOccluders.size()),
                origins[level][0] * scale, origins[level][1] * scale, scale, Resolution));
        }
        for (uint32_t level = 0u; level < parameters.LevelCount; ++level)
        {
            levelPointers[level] = &grids[level];
        }
        const Container::Span<const SkyOcclusionReferenceGrid* const> levels(levelPointers, parameters.LevelCount);
        for (uint32_t level = 0u; level < parameters.LevelCount; ++level)
        {
            const float scale = grids[level].TexelMeters;
            const int32_t ox = origins[level][0];
            const int32_t oz = origins[level][1];
            for (uint32_t z = 0u; z < Resolution; ++z)
            {
                for (uint32_t x = 0u; x < Resolution; ++x)
                {
                    const int32_t wx = ox + static_cast<int32_t>(x);
                    const int32_t wz = oz + static_cast<int32_t>(z);
                    const float worldX = (static_cast<float>(wx) + 0.5f) * scale;
                    const float worldZ = (static_cast<float>(wz) + 0.5f) * scale;
                    bool inInfluence = false;
                    for (const SkyOccluderProxy& proxy : scene.SkyOccluders)
                    {
                        const float margin = parameters.MaxHorizonMeters;
                        inInfluence |= std::abs(worldX - proxy.CenterX) <= proxy.HalfExtentX + margin &&
                                std::abs(worldZ - proxy.CenterZ) <= proxy.HalfExtentZ + margin;
                    }
                    if (!inInfluence && (x % 32u != 0u || z % 32u != 0u))
                    {
                        continue;
                    }
                    const SkyOcclusionL1 expected = IntegrateSkyOcclusionClipmap(levels, level, parameters, worldX, worldZ);
                    const size_t offset = static_cast<size_t>((level * PixelsPerLayer +
                        WrapSkyOcclusionTexel(wz, Resolution) * Resolution + WrapSkyOcclusionTexel(wx, Resolution)) * Channels);
                    const float b = DecodeHalf(images.Visibility[offset]);
                    const float cx = DecodeHalf(images.Visibility[offset + 1u]);
                    const float cy = DecodeHalf(images.Visibility[offset + 2u]);
                    const float cz = DecodeHalf(images.Visibility[offset + 3u]);
                    const float dx = cx - expected.Cx;
                    const float dy = cy - expected.Cy;
                    const float dz = cz - expected.Cz;
                    maxB.Consider(std::abs(b - expected.B0), b, expected.B0, level, wx, wz);
                    maxC.Consider(std::sqrt(dx * dx + dy * dy + dz * dz),
                                  std::sqrt(cx * cx + cy * cy + cz * cz),
                                  std::sqrt(expected.Cx * expected.Cx + expected.Cy * expected.Cy + expected.Cz * expected.Cz),
                                  level, wx, wz);
                }
            }
        }
        std::cout << TestName << ' ' << name << " b0 最大差=" << maxB.Difference
                  << " |c| 最大差=" << maxC.Difference << std::endl;
        if (maxB.Difference > 0.03f || maxC.Difference > 0.03f)
        {
            maxB.Print("b0");
            maxC.Print("|c|");
            return false;
        }
        return true;
    }

    bool CompareImages(const char* name, const Images& incremental, const Images& rebuilt, uint32_t levels)
    {
        Maximum maxima[3];
        const char* names[3] = {"Occluders", "Visibility", "Aux"};
        for (uint32_t level = 0; level < levels; ++level)
        {
            for (uint32_t z = 0; z < Resolution; ++z)
            {
                for (uint32_t x = 0; x < Resolution; ++x)
                {
                    const size_t offset = static_cast<size_t>((level * PixelsPerLayer + z * Resolution + x) * Channels);
                    for (uint32_t channel = 0; channel < Channels; ++channel)
                    {
                        const size_t i = offset + channel;
                        const float actual[3] = {incremental.Occluders[i], DecodeHalf(incremental.Visibility[i]), DecodeHalf(incremental.Aux[i])};
                        const float expected[3] = {rebuilt.Occluders[i], DecodeHalf(rebuilt.Visibility[i]), DecodeHalf(rebuilt.Aux[i])};
                        for (uint32_t image = 0; image < 3u; ++image)
                        {
                            maxima[image].Consider(std::abs(actual[image] - expected[image]), actual[image], expected[image],
                                                   level, static_cast<int32_t>(x), static_cast<int32_t>(z), channel);
                        }
                    }
                }
            }
        }
        std::cout << TestName << ' ' << name << " 最大差: Occluders=" << maxima[0].Difference
                  << " Visibility=" << maxima[1].Difference << " Aux=" << maxima[2].Difference << std::endl;
        bool passed = true;
        for (uint32_t image = 0; image < 3u; ++image)
        {
            if (maxima[image].Difference > 1.0e-3f)
            {
                maxima[image].Print(names[image]);
                passed = false;
            }
        }
        return passed;
    }

    bool RunReferenceCases(Fixture& fixture)
    {
        const char* names[3] = {"樹冠", "林縁", "岩"};
        bool passed = true;
        for (uint32_t index = 0; index < 3u; ++index)
        {
            SceneProxy scene = MakeScene(index);
            SkyOcclusionMapPass pass;
            Images images;
            if (!fixture.InitializePass(pass) || !fixture.Run(pass, scene, images))
            {
                return false;
            }
            passed = CompareReference(names[index], scene, images) && passed;
        }
        // 同じ樹冠で 1 段を使い、配列ビューと未使用の 2 層目の確保を確かめる。
        SceneProxy single = MakeScene(0u);
        single.SkyOcclusion.LevelCount = 1u;
        SkyOcclusionMapPass singlePass;
        Images singleImages;
        if (!fixture.InitializePass(singlePass) || !fixture.Run(singlePass, single, singleImages))
        {
            return false;
        }
        passed = Check(singleImages.Textures[0]->GetArraySize() == 2u &&
                       singleImages.Textures[1]->GetArraySize() == 2u &&
                       singleImages.Textures[2]->GetArraySize() == 2u, "単段用の配列ビューを作れません") && passed;
        return CompareReference("樹冠・単段", single, singleImages) && passed;
    }

    bool RunIncrementalCases(Fixture& fixture)
    {
        SceneProxy scene = MakeScene(0u);
        // 増分更新は前と同じ樹冠で検査する。
        scene.SkyOccluders[0].CenterZ = 0.0f;
        scene.SkyOccluders[0].HalfExtentX = 3.0f;
        scene.SkyOccluders[0].HalfExtentY = 1.25f;
        scene.SkyOccluders[0].HalfExtentZ = 3.0f;
        scene.SkyOcclusion.CenterOverrideX = 63.5f;
        scene.SkyOccluders[0].CenterX = 63.5f;
        SkyOcclusionMapPass incrementalPass;
        Images initial;
        if (!fixture.InitializePass(incrementalPass) || !fixture.Run(incrementalPass, scene, initial))
        {
            return false;
        }
        int32_t oldX = 0;
        int32_t oldZ = 0;
        ComputeSkyOcclusionWindowOrigin(scene.SkyOcclusion, 0u, scene.SkyOcclusion.CenterOverrideX,
                                        scene.SkyOcclusion.CenterOverrideZ, oldX, oldZ);
        bool passed = true;
        const char* names[4] = {"移動 1", "移動 7 斜め", "移動 64 斜め", "遮蔽物変更"};
        for (uint32_t step = 0; step < 4u; ++step)
        {
            if (step == 0u)
            {
                scene.SkyOcclusion.CenterOverrideX += 0.5f;
                int32_t newX = 0;
                int32_t newZ = 0;
                ComputeSkyOcclusionWindowOrigin(scene.SkyOcclusion, 0u, scene.SkyOcclusion.CenterOverrideX,
                                                scene.SkyOcclusion.CenterOverrideZ, newX, newZ);
                passed = Check(WrapSkyOcclusionTexel(oldX, Resolution) == 255u &&
                               WrapSkyOcclusionTexel(newX, Resolution) == 0u, "折り返しをまたいでいません") && passed;
            }
            else if (step == 1u)
            {
                scene.SkyOcclusion.CenterOverrideX += 3.5f;
                scene.SkyOcclusion.CenterOverrideZ += 3.5f;
            }
            else if (step == 2u)
            {
                scene.SkyOcclusion.CenterOverrideX += 32.0f;
                scene.SkyOcclusion.CenterOverrideZ -= 32.0f;
            }
            else
            {
                scene.SkyOccluders[0].CenterX += 3.0f;
                scene.SkyOcclusionRevision++;
            }
            Images actual;
            Images expected;
            SkyOcclusionMapPass freshPass;
            if (!fixture.Run(incrementalPass, scene, actual) || !fixture.InitializePass(freshPass) ||
                !fixture.Run(freshPass, scene, expected))
            {
                return false;
            }
            passed = Check(actual.Textures[0] == initial.Textures[0] &&
                           actual.Textures[1] == initial.Textures[1] && actual.Textures[2] == initial.Textures[2],
                           "増分更新でテクスチャが作り直されました") && passed;
            passed = Check(actual.SplatTiles > 0u && actual.SplatTiles < 3u * 32u * 32u,
                           "増分更新で部分的な Splat が実行されません") && passed;
            passed = CompareImages(names[step], actual, expected, scene.SkyOcclusion.LevelCount) && passed;
        }
        return passed;
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
        if (!device || device->GetAPI() != API::Vulkan ||
            !device->IsFormatSupported(Format::R32G32B32A32_FLOAT, FormatUsage::Storage) ||
            !device->IsFormatSupported(Format::R16G16B16A16_FLOAT, FormatUsage::Storage))
        {
            std::cout << TestName << " スキップ: 必要な Vulkan のストレージ画像を利用できません\n";
            return SkipReturnCode;
        }
        bool passed = false;
        {
            Fixture fixture;
            fixture.Device = device;
            if (fixture.Initialize())
            {
                passed = RunReferenceCases(fixture);
                passed = RunIncrementalCases(fixture) && passed;
            }
        }
        device->WaitIdle();
        device.reset();
        const uint32_t errors = capture.Hits();
        std::cout << TestName << " Vulkan 検証エラー=" << errors << '\n';
        passed = Check(errors == 0u, "Vulkan の検証エラーが出ました") && passed;
        std::cout << TestName << (passed ? " 合格\n" : " 不合格\n");
        return passed ? 0 : 1;
    }
}

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

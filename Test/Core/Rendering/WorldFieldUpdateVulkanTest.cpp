// 世界フィールドの曲げ・時間減衰・整数画素のスクロールを、計算シェーダーの読み戻しで確かめる。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "Rendering/FrameResourceOwner.h"
#include "Rendering/ShaderManager.h"
#include "Rendering/WorldFieldPass.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDevice.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"
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
    using HalfPixels = Container::VariableArray<uint16_t>;

    constexpr const char* TestName = "WorldFieldUpdateVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t Resolution = 256;
    constexpr uint32_t Channels = 4;
    constexpr uint32_t ComponentCount = Resolution * Resolution * Channels;
    constexpr uint32_t ImageBytes = ComponentCount * sizeof(uint16_t);
    static_assert(WorldFieldResolution == Resolution);

    bool Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::cerr << TestName << " 失敗: " << message << '\n';
        }
        return bCondition;
    }

    bool IsGpuTestSkipForced()
    {
        char* value = nullptr;
        size_t length = 0;
        if (_dupenv_s(&value, &length, "NORVESLIB_FORCE_GPU_TEST_SKIP") != 0 || value == nullptr)
        {
            return false;
        }
        const bool bForced = std::strcmp(value, "1") == 0;
        free(value);
        return bForced;
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
        const uint32_t exponent = (bits >> 10) & 31u;
        const uint32_t mantissa = bits & 1023u;
        const float sign = (bits & 0x8000u) != 0 ? -1.0f : 1.0f;
        if (exponent == 31u)
        {
            return mantissa == 0 ? sign * std::numeric_limits<float>::infinity()
                                 : std::numeric_limits<float>::quiet_NaN();
        }
        return sign * (exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
            : std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f, static_cast<int>(exponent) - 15));
    }

    WorldFieldUpdateConstants MakeConstants()
    {
        WorldFieldUpdateConstants constants;
        constants.OriginXZ[0] = -32.0f;
        constants.OriginXZ[1] = -32.0f;
        constants.TexelSize = 0.25f;
        constants.bResetHistory = 0;
        return constants;
    }

    DescriptorSetDesc MakeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        const ResourceBindType types[] = {ResourceBindType::RWTexture, ResourceBindType::RWTexture,
            ResourceBindType::StructuredBuffer, ResourceBindType::ConstantBuffer};
        for (uint32_t index = 0; index < 4; ++index)
        {
            DescriptorBinding binding;
            binding.binding = index;
            binding.type = types[index];
            binding.stages = ShaderStage::Compute;
            desc.bindings.push_back(binding);
        }
        return desc;
    }

    struct Fixture
    {
        DevicePtr Device;
        ShaderManager Shaders;
        PipelinePtr Pipeline;
        DescriptorSetLayoutPtr Layout;
        TexturePtr Textures[2];
        ResourceState States[2] = {ResourceState::Undefined, ResourceState::Undefined};
        DeviceFrameUploadPageSource PageSource;
        FrameResourceOwners Owners;
        uint32_t PreviousIndex = 0;
        uint64_t LastSerial = 0;

        ~Fixture()
        {
            if (Device)
            {
                Device->WaitIdle();
            }
            Owners.Reset();
            Pipeline.reset();
            Textures[0].reset();
            Textures[1].reset();
            Owners.ReleaseDescriptorPools();
            Owners.ReleaseUploadPages();
            Layout.reset();
            Shaders.Shutdown();
            PageSource.SetDevice({});
        }

        bool Initialize(const HalfPixels& initial)
        {
            PageSource.SetDevice(Device);
            Owners.ConfigureUpload(&PageSource, 1u << 20);
            if (!Check(Owners.ConfigureDescriptors(Device.get()), "記述子のプールを作れません") ||
                !Check(Shaders.Initialize(Device.get(), NORVES_SOURCE_ROOT "/Assets/Shaders"),
                       "シェーダーの読み込みを初期化できません"))
            {
                return false;
            }
            const DescriptorSetDesc layoutDesc = MakeDescriptorSetDesc();
            Layout = Device->GetOrCreateDescriptorSetLayout(layoutDesc);
            ComputePipelineDesc pipelineDesc;
            pipelineDesc.computeShader = Shaders.LoadShader("WorldField/WorldFieldUpdate.comp", ShaderStage::Compute);
            pipelineDesc.descriptorSetLayouts.push_back(layoutDesc);
            if (!Check(Layout && pipelineDesc.computeShader, "更新シェーダーかレイアウトを作れません"))
            {
                return false;
            }
            Pipeline = Device->CreateComputePipeline(pipelineDesc);
            TextureDesc textureDesc;
            textureDesc.Width = Resolution;
            textureDesc.Height = Resolution;
            textureDesc.TextureFormat = Format::R16G16B16A16_FLOAT;
            textureDesc.Usage = ResourceUsage::ShaderRead | ResourceUsage::ShaderWrite |
                                ResourceUsage::TransferSrc | ResourceUsage::TransferDst;
            textureDesc.DebugName = "WorldFieldTest.Previous";
            Textures[0] = Device->CreateTexture(textureDesc);
            textureDesc.DebugName = "WorldFieldTest.Current";
            Textures[1] = Device->CreateTexture(textureDesc);
            if (!Check(Pipeline && Textures[0] && Textures[1], "パイプラインか RGBA16F の画像を作れません") ||
                !Check(initial.size() == ComponentCount, "初期画像の大きさが違います"))
            {
                return false;
            }
            Textures[0]->Update(initial.data(), Resolution * Channels * sizeof(uint16_t), ImageBytes);
            States[0] = ResourceState::ShaderResource;
            return true;
        }

        bool Dispatch(const WorldFieldUpdateConstants& constants, const WorldFieldInteractor* interactor,
                      HalfPixels& output)
        {
            BufferPtr readback = Device->CreateBuffer(BufferDesc(ImageBytes, ResourceUsage::TransferDst,
                true, "WorldFieldTest.Readback"));
            CommandListPtr commands = Device->CreateCommandList();
            FrameResourceOwner* frame = Owners.BeginFrame(0, LastSerial);
            if (!Check(readback && commands && frame, "更新に必要な資源を用意できません"))
            {
                Owners.AbortFrame();
                return false;
            }
            const WorldFieldInteractor emptyInteractor{};
            const FrameUploadAllocation inputs = frame->AllocateUploadData(interactor ? interactor : &emptyInteractor,
                sizeof(WorldFieldInteractor), FrameUploadUsage::Storage);
            const FrameUploadAllocation params = frame->AllocateConstants(constants);
            const FrameDescriptorAllocation descriptors = frame->AllocateDescriptorSet(Layout);
            if (!Check(inputs.IsValid() && params.IsValid() && descriptors.IsValid(), "フレームの入力を借りられません"))
            {
                Owners.AbortFrame();
                return false;
            }
            const uint32_t currentIndex = 1u - PreviousIndex;
            descriptors.Set->BindStorageTexture(0, Textures[PreviousIndex]);
            descriptors.Set->BindStorageTexture(1, Textures[currentIndex]);
            descriptors.Set->BindStorageBuffer(2, inputs.Buffer, inputs.GetOffset32(), inputs.GetSize32());
            descriptors.Set->BindConstantBuffer(3, params.Buffer, params.GetOffset32(), params.GetSize32());
            descriptors.Set->Update();
            commands->Begin();
            for (uint32_t index = 0; index < 2; ++index)
            {
                commands->TextureBarrier(Textures[index], States[index], ResourceState::UnorderedAccess);
                States[index] = ResourceState::UnorderedAccess;
            }
            commands->SetPipeline(Pipeline);
            commands->SetDescriptorSet(descriptors.Set, 0);
            commands->Dispatch(Resolution / 8, Resolution / 8, 1);
            commands->TextureBarrier(Textures[currentIndex], ResourceState::UnorderedAccess, ResourceState::CopySource);
            States[currentIndex] = ResourceState::CopySource;
            commands->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest);
            commands->CopyTextureToBuffer(Textures[currentIndex], readback, Resolution, Resolution);
            commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
            commands->End();
            commands->Submit(true);
            Device->WaitIdle();
            if (!Check(Owners.CommitSubmitted(++LastSerial), "更新を提出できません"))
            {
                return false;
            }
            const uint16_t* mapped = static_cast<const uint16_t*>(readback->Map(0, ImageBytes));
            if (!Check(mapped != nullptr, "更新した画像を読み戻せません"))
            {
                return false;
            }
            output.assign(mapped, mapped + ComponentCount);
            readback->Unmap();
            PreviousIndex = currentIndex;
            return true;
        }
    };

    bool CheckBend(const DevicePtr& device)
    {
        Fixture fixture;
        fixture.Device = device;
        const HalfPixels initial(ComponentCount, 0);
        if (!fixture.Initialize(initial))
        {
            return false;
        }
        WorldFieldUpdateConstants constants = MakeConstants();
        constants.InteractorCount = 1;
        WorldFieldInteractor interactor;
        interactor.P0[0] = interactor.P1[0] = 2.0f;
        interactor.PrevP0[0] = interactor.PrevP1[0] = -2.0f;
        interactor.P1[1] = interactor.PrevP1[1] = 2.0f;
        // 128 行目の画素中心を通る軸に合わせ、半径方向の成分を消す。
        interactor.P0[2] = interactor.P1[2] = interactor.PrevP0[2] = interactor.PrevP1[2] = 0.125f;
        interactor.Radius = interactor.PrevRadius = 1.0f;
        HalfPixels output;
        if (!fixture.Dispatch(constants, &interactor, output))
        {
            return false;
        }
        constexpr float cosineTenDegrees = 0.984807753f;
        // 現在のカプセルから半径より遠い後方だけを検査し、移動の軌跡が残ることも要求する。
        for (uint32_t x = 120; x < 128; ++x)
        {
            const uint32_t offset = (128 * Resolution + x) * Channels;
            const float bendX = DecodeHalf(output[offset]);
            const float bendZ = DecodeHalf(output[offset + 1]);
            const float length = std::sqrt(bendX * bendX + bendZ * bendZ);
            const float flatten = DecodeHalf(output[offset + 2]);
            if (!Check(std::isfinite(length) && length > 0.0f && bendX / length >= cosineTenDegrees &&
                       std::isfinite(flatten) && flatten > 0.0f, "後方の軸上で曲げが +X の 10 度以内に向きません"))
            {
                std::cerr << "画素 X=" << x << " 曲げ=(" << bendX << ',' << bendZ << ")\n";
                return false;
            }
        }
        std::cout << TestName << " 曲げ: 後方の軸上 8 画素が +X の 10 度以内です\n";
        return true;
    }

    bool CheckDecay(const DevicePtr& device)
    {
        Fixture fixture;
        fixture.Device = device;
        HalfPixels initial(ComponentCount, 0);
        for (uint32_t pixel = 0; pixel < Resolution * Resolution; ++pixel)
        {
            initial[pixel * Channels + 2] = 0x3A00u; // 半精度で正確に表せる 0.75。
        }
        if (!fixture.Initialize(initial))
        {
            return false;
        }
        constexpr double timeStep = 1.0 / 60.0;
        constexpr double timeConstant = 1.5;
        constexpr uint32_t iterations = 12;
        WorldFieldUpdateConstants constants = MakeConstants();
        constants.Decay = static_cast<float>(std::exp(-timeStep / timeConstant));
        HalfPixels output;
        for (uint32_t iteration = 0; iteration < iterations; ++iteration)
        {
            if (!fixture.Dispatch(constants, nullptr, output))
            {
                return false;
            }
        }
        // 途中の半精度の丸めを写さず、連続時間の指数式から期待値を求める。
        const double expected = 0.75 * std::exp(-static_cast<double>(iterations) * timeStep / timeConstant);
        double maximumRelativeError = 0.0;
        for (uint32_t pixel = 0; pixel < Resolution * Resolution; ++pixel)
        {
            const float actual = DecodeHalf(output[pixel * Channels + 2]);
            const double relativeError = std::abs(actual - expected) / expected;
            if (!Check(std::isfinite(actual) && relativeError <= 0.05, "押し倒しの減衰が指数式の 5% 以内に収まりません"))
            {
                std::cerr << "画素=" << pixel << " 期待値=" << expected << " 読み戻し=" << actual << '\n';
                return false;
            }
            if (relativeError > maximumRelativeError)
            {
                maximumRelativeError = relativeError;
            }
        }
        std::cout << TestName << " 減衰: 期待値=" << expected << " 最大相対誤差=" << maximumRelativeError << '\n';
        return true;
    }

    bool CheckScroll(const DevicePtr& device)
    {
        Fixture fixture;
        fixture.Device = device;
        HalfPixels initial(ComponentCount, 0);
        for (uint32_t z = 0; z < Resolution; ++z)
        {
            for (uint32_t x = 0; x < Resolution; ++x)
            {
                const uint32_t offset = (z * Resolution + x) * Channels;
                // X と Z を区別できる非ゼロの有限値。全成分を半精度のビット列で直接用意する。
                initial[offset] = static_cast<uint16_t>(0x3800u + x);
                initial[offset + 1] = static_cast<uint16_t>(0xB800u + z);
                initial[offset + 2] = static_cast<uint16_t>(0x3400u + (x + 3 * z) % 1024u);
                initial[offset + 3] = static_cast<uint16_t>(0x3000u + (5 * x + z) % 1024u);
            }
        }
        if (!fixture.Initialize(initial))
        {
            return false;
        }
        constexpr uint32_t shift = 3;
        WorldFieldUpdateConstants constants = MakeConstants();
        constants.Shift[0] = shift;
        constants.OriginXZ[0] += shift * constants.TexelSize;
        HalfPixels output;
        if (!fixture.Dispatch(constants, nullptr, output))
        {
            return false;
        }
        for (uint32_t z = 0; z < Resolution; ++z)
        {
            for (uint32_t x = 0; x < Resolution; ++x)
            {
                for (uint32_t channel = 0; channel < Channels; ++channel)
                {
                    // 原点が +3 画素動くと同じ世界位置は左へ 3 画素動き、右端の 3 列が空く。
                    const uint16_t expected = x < Resolution - shift
                        ? initial[(z * Resolution + x + shift) * Channels + channel] : 0;
                    if (!Check(output[(z * Resolution + x) * Channels + channel] == expected,
                               "3 画素の移動か、入ってきた端のゼロ埋めが一致しません"))
                    {
                        std::cerr << "画素=(" << x << ',' << z << ") 成分=" << channel << '\n';
                        return false;
                    }
                }
            }
        }
        std::cout << TestName << " スクロール: 全画素が左へ 3 画素移り、右端の 3 列がゼロです\n";
        return true;
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
        bool bPassed = CheckBend(device);
        bPassed = CheckDecay(device) && bPassed;
        bPassed = CheckScroll(device) && bPassed;
        device->WaitIdle();
        device.reset();
        const uint32_t errors = capture.Hits();
        std::cout << TestName << " Vulkan 検証エラー=" << errors << '\n';
        bPassed = Check(errors == 0, "Vulkan の検証エラーが出ました") && bPassed;
        std::cout << TestName << (bPassed ? " 合格\n" : " 不合格\n");
        return bPassed ? 0 : 1;
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

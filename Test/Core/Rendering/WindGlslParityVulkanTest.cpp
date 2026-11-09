// 共通の風の速度を 1024 点で GPU から読み戻し、CPU の式と照合する。
#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

#include "FileStream/FileStream.h"
#include "Rendering/Wind.h"
#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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
    using Core::Container::String;
    using Core::Container::VariableArray;

    constexpr const char* TestName = "WindGlslParityVulkanTest";
    constexpr int SkipReturnCode = 125;
    constexpr uint32_t SampleCount = 1024;
    constexpr uint32_t BoundaryCount = 256;
    constexpr float Tolerance = 1.0e-4f;

    // ShaderManager と同じシェーダールートを基準に include を展開する。
    bool ExpandShader(const String& root, const String& relativePath, String& expanded, unsigned depth = 0)
    {
        if (depth >= 16)
        {
            std::cerr << "[失敗] include の再帰が深すぎます: " << relativePath.c_str() << std::endl;
            return false;
        }
        NorvesLib::FileStream::FileStream file;
        if (!file.Open(root + relativePath, NorvesLib::FileStream::FileMode::Read,
                       NorvesLib::FileStream::FileAccess::Read))
        {
            std::cerr << "[失敗] シェーダーを開けません: " << (root + relativePath).c_str() << std::endl;
            return false;
        }
        const auto length = file.GetSize();
        if (length < 0)
        {
            std::cerr << "[失敗] シェーダーのサイズを読めません。" << std::endl;
            return false;
        }
        VariableArray<char> bytes(static_cast<std::size_t>(length) + 1, '\0');
        if (file.Read(bytes.data(), static_cast<std::size_t>(length)) != static_cast<std::size_t>(length))
        {
            std::cerr << "[失敗] シェーダーを最後まで読めません。" << std::endl;
            return false;
        }
        const bool bHasBom = length >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
                             static_cast<unsigned char>(bytes[1]) == 0xBB &&
                             static_cast<unsigned char>(bytes[2]) == 0xBF;
        const String source(bytes.data() + (bHasBom ? 3 : 0));
        std::size_t start = 0;
        while (start < source.size())
        {
            const auto newline = source.find('\n', start);
            const auto end = newline == String::npos ? source.size() : newline;
            const String line = source.substr(start, end - start);
            std::size_t cursor = 0;
            while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
            {
                ++cursor;
            }
            if (line.find("#include", cursor) == cursor)
            {
                cursor += 8;
                while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t'))
                {
                    ++cursor;
                }
                const auto closing = line.find('"', cursor + 1);
                if (cursor >= line.size() || line[cursor] != '"' || closing == String::npos)
                {
                    std::cerr << "[失敗] 引用符付き include を解釈できません: " << line.c_str() << std::endl;
                    return false;
                }
                const String include = line.substr(cursor + 1, closing - cursor - 1);
                if (include.empty() || include[0] == '/' || include.find("..") != String::npos ||
                    include.find(':') != String::npos || include.find('\\') != String::npos)
                {
                    std::cerr << "[失敗] include はシェーダールート内の相対パスで指定してください。" << std::endl;
                    return false;
                }
                if (!ExpandShader(root, include, expanded, depth + 1))
                {
                    return false;
                }
            }
            else
            {
                expanded += line;
            }
            expanded += "\n";
            start = end + 1;
        }
        return true;
    }


    // 試験内の include も、実ファイルと同じルートから展開する。
    bool ExpandProbe(const String& root, const String& source, String& expanded)
    {
        constexpr const char* include = "#include \"Common/Wind.glsl\"";
        const auto start = source.find(include);
        if (start == String::npos || source.find(include, start + 1) != String::npos)
        {
            return false;
        }
        expanded += source.substr(0, start);
        if (!ExpandShader(root, "Common/Wind.glsl", expanded))
        {
            return false;
        }
        expanded += source.substr(start + std::strlen(include));
        return true;
    }

    class ValidationCapture
    {
    public:
        ValidationCapture() { Vulkan::BeginVulkanValidationErrorCaptureForTesting(); }
        ~ValidationCapture() { Vulkan::EndVulkanValidationErrorCaptureForTesting(); }
        uint32_t Hits() const { return Vulkan::GetVulkanValidationErrorCaptureHitCountForTesting(); }
    };

    struct ProbeInput
    {
        float PositionTime[4];
        float DirectionSpeedStrength[4];
        float FrequencyScaleTurbulence[4];
    };
    struct ProbeOutput
    {
        float Velocity[4];
    };
    static_assert(sizeof(ProbeInput) == 48);
    static_assert(sizeof(ProbeOutput) == 16);

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

    DescriptorSetDesc MakeDescriptorSetDesc()
    {
        DescriptorSetDesc desc;
        DescriptorBinding input;
        input.binding = 0;
        input.type = ResourceBindType::StructuredBuffer;
        input.stages = ShaderStage::Compute;
        desc.bindings.push_back(input);
        DescriptorBinding output;
        output.binding = 1;
        output.type = ResourceBindType::RWBuffer;
        output.stages = ShaderStage::Compute;
        desc.bindings.push_back(output);
        return desc;
    }

    PipelinePtr CreatePipeline(IDevice& device, const DescriptorSetDesc& layout)
    {
        const String probe = R"glsl(#version 450
#include "Common/Wind.glsl"
layout(local_size_x = 64) in;
struct ProbeInput
{
    vec4 positionTime;
    vec4 directionSpeedStrength;
    vec4 frequencyScaleTurbulence;
};
layout(std430, set = 0, binding = 0) readonly buffer Inputs { ProbeInput values[]; } inputs;
layout(std430, set = 0, binding = 1) writeonly buffer Outputs { vec4 values[]; } outputs;
void main()
{
    uint index = gl_GlobalInvocationID.x;
    if (index >= 1024u) return;
    ProbeInput p = inputs.values[index];
    vec3 velocity = WindVelocity(p.positionTime.xyz, p.positionTime.w,
        p.directionSpeedStrength.xy, p.directionSpeedStrength.z, p.directionSpeedStrength.w,
        p.frequencyScaleTurbulence.x, p.frequencyScaleTurbulence.y, p.frequencyScaleTurbulence.z);
    outputs.values[index] = vec4(velocity, 0.0);
}
)glsl";
        String source;
        if (!ExpandProbe(NORVES_SOURCE_ROOT "/Assets/Shaders/", probe, source))
        {
            return {};
        }
        ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!compiler)
        {
            std::cerr << TestName << " シェーダーコンパイラを作れません\n";
            return {};
        }
        const ShaderCompileResult compiled = compiler->CompileFromSource(source, ShaderStage::Compute, TestName);
        if (!compiled.bSuccess || compiled.ByteCode.empty())
        {
            std::cerr << TestName << " シェーダーのコンパイルに失敗: " << compiled.ErrorMessage.c_str() << '\n';
            return {};
        }
        ShaderDesc shaderDesc;
        shaderDesc.stage = ShaderStage::Compute;
        shaderDesc.byteCode = compiled.ByteCode;
        ShaderPtr shader = device.CreateShader(shaderDesc);
        if (!shader)
        {
            std::cerr << TestName << " シェーダーを作れません\n";
            return {};
        }
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = shader;
        pipelineDesc.descriptorSetLayouts.push_back(layout);
        return device.CreateComputePipeline(pipelineDesc);
    }

    void MakeSamples(ProbeInput (&inputs)[SampleCount], Math::Vector3 (&expected)[SampleCount])
    {
        for (uint32_t i = 0; i < SampleCount; ++i)
        {
            WindParameters raw = MakeDefaultWindParameters();
            raw.bEnabled = true;
            raw.DirectionYawDegrees = static_cast<float>((i * 37u) % 360u);
            raw.BaseSpeed = 0.5f + static_cast<float>(i % 30u) * 0.125f;
            raw.GustStrength = static_cast<float>(i % 10u) * 0.1f;
            raw.GustFrequencyHz = static_cast<float>(1u + i % 4u) / 4096.0f;
            raw.GustScaleMeters = static_cast<float>(16u << (i % 3u));
            raw.Turbulence = static_cast<float>((i * 3u) % 10u) * 0.1f;
            if (i < BoundaryCount)
            {
                raw.DirectionYawDegrees = 0.0f;
                raw.GustFrequencyHz = 0.125f;
                raw.GustScaleMeters = 16.0f;
            }
            const WindParameters wind = SanitizeWindParameters(raw);
            float dirX = 0.0f;
            float dirZ = 0.0f;
            WindDirectionXZ(wind, dirX, dirZ);

            const float time = WindClockSeconds(static_cast<double>((i * 1013u) % 65536u) / 16.0);
            float x = static_cast<float>(static_cast<int32_t>((i * 73u) % 16001u) - 8000) / 16.0f;
            const float y = static_cast<float>(static_cast<int32_t>((i * 29u) % 3201u) - 1600) / 16.0f;
            const float z = static_cast<float>(static_cast<int32_t>((i * 137u) % 16001u) - 8000) / 16.0f;
            if (i < BoundaryCount)
            {
                // 突風と乱流のセル境界の両側、および境界上を含める。
                float cell = -std::floor(time * wind.GustFrequencyHz) +
                             static_cast<float>(static_cast<int32_t>((i * 7u) % 33u) - 16);
                if (i >= BoundaryCount / 2u)
                {
                    cell = std::round(cell * 3.1f) / 3.1f;
                }
                const float offset = static_cast<float>(static_cast<int32_t>(i % 3u) - 1) / 1024.0f;
                x = (cell + time * wind.GustFrequencyHz + offset) * wind.GustScaleMeters;
            }
            const Math::Vector3 position(x, y, z);
            expected[i] = EvaluateWind(wind, position, time);
            inputs[i] = {{x, y, z, time},
                         {dirX, dirZ, wind.BaseSpeed, wind.GustStrength},
                         {wind.GustFrequencyHz, wind.GustScaleMeters, wind.Turbulence, 0.0f}};
        }
    }

    bool RunCase(const DevicePtr& device)
    {
        ProbeInput inputs[SampleCount] = {};
        Math::Vector3 expected[SampleCount];
        MakeSamples(inputs, expected);
        ProbeOutput missing[SampleCount];
        for (ProbeOutput& sample : missing)
        {
            for (float& component : sample.Velocity)
            {
                component = std::numeric_limits<float>::quiet_NaN();
            }
        }
        constexpr uint32_t inputBytes = sizeof(inputs);
        constexpr uint32_t outputBytes = SampleCount * sizeof(ProbeOutput);
        const DescriptorSetDesc layout = MakeDescriptorSetDesc();
        PipelinePtr pipeline = CreatePipeline(*device, layout);
        BufferPtr staging = device->CreateBuffer(BufferDesc(inputBytes, ResourceUsage::TransferSrc, true, "WindProbe.Staging"));
        BufferPtr input = device->CreateBuffer(BufferDesc(inputBytes,
            ResourceUsage::StorageBuffer | ResourceUsage::ShaderRead | ResourceUsage::TransferDst, false, "WindProbe.Input"));
        BufferPtr outputStaging = device->CreateBuffer(BufferDesc(outputBytes,
            ResourceUsage::TransferSrc, true, "WindProbe.OutputStaging"));
        BufferPtr output = device->CreateBuffer(BufferDesc(outputBytes,
            ResourceUsage::StorageBuffer | ResourceUsage::TransferSrc | ResourceUsage::TransferDst, false, "WindProbe.Output"));
        BufferPtr readback = device->CreateBuffer(BufferDesc(outputBytes, ResourceUsage::TransferDst, true, "WindProbe.Readback"));
        DescriptorSetPtr descriptors = device->CreateDescriptorSet(layout);
        CommandListPtr commands = device->CreateCommandList();
        if (!pipeline || !staging || !input || !outputStaging || !output || !readback || !descriptors || !commands)
        {
            std::cerr << TestName << " 計算に必要な資源を作れません\n";
            return false;
        }
        staging->Update(inputs, inputBytes);
        outputStaging->Update(missing, outputBytes);
        descriptors->BindStorageBuffer(0, input, 0, inputBytes);
        descriptors->BindStorageBuffer(1, output, 0, outputBytes);
        descriptors->Update();

        commands->Begin();
        commands->BufferBarrier(staging, ResourceState::Undefined, ResourceState::CopySource);
        commands->BufferBarrier(input, ResourceState::Undefined, ResourceState::CopyDest);
        commands->CopyBuffer(staging, input, inputBytes);
        commands->BufferBarrier(input, ResourceState::CopyDest, ResourceState::ShaderResource);
        commands->BufferBarrier(outputStaging, ResourceState::Undefined, ResourceState::CopySource);
        commands->BufferBarrier(output, ResourceState::Undefined, ResourceState::CopyDest);
        commands->CopyBuffer(outputStaging, output, outputBytes);
        commands->BufferBarrier(output, ResourceState::CopyDest, ResourceState::UnorderedAccess);
        commands->SetPipeline(pipeline);
        commands->SetDescriptorSet(descriptors);
        commands->Dispatch(SampleCount / 64u, 1u, 1u);
        commands->BufferBarrier(output, ResourceState::UnorderedAccess, ResourceState::CopySource);
        commands->BufferBarrier(readback, ResourceState::Undefined, ResourceState::CopyDest);
        commands->CopyBuffer(output, readback, outputBytes);
        commands->BufferBarrier(readback, ResourceState::CopyDest, ResourceState::HostRead);
        commands->End();
        commands->Submit(true);
        device->WaitIdle();

        const ProbeOutput* results = static_cast<const ProbeOutput*>(readback->Map(0, outputBytes));
        if (!results)
        {
            std::cerr << TestName << " 結果を読み戻せません\n";
            return false;
        }
        float maxDifference = 0.0f;
        uint32_t maxIndex = 0;
        uint32_t maxComponent = 0;
        bool bFinite = true;
        for (uint32_t i = 0; i < SampleCount; ++i)
        {
            const float cpu[] = {expected[i].x, expected[i].y, expected[i].z};
            for (uint32_t component = 0; component < 3; ++component)
            {
                const float gpu = results[i].Velocity[component];
                if (!std::isfinite(gpu) || !std::isfinite(cpu[component]))
                {
                    bFinite = false;
                    continue;
                }
                const float difference = std::fabs(gpu - cpu[component]);
                if (difference > maxDifference)
                {
                    maxDifference = difference;
                    maxIndex = i;
                    maxComponent = component;
                }
            }
        }
        readback->Unmap();
        std::cout << TestName << " 最大絶対差=" << maxDifference << " 点=" << maxIndex
                  << " 成分=" << maxComponent << '\n';
        if (!bFinite || maxDifference > Tolerance)
        {
            std::cerr << TestName << " 速度が許容差を超えたか、非有限値を検出しました\n";
            return false;
        }
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
        bool bPassed = RunCase(device);
        device->WaitIdle();
        device.reset();
        const uint32_t errors = capture.Hits();
        std::cout << TestName << " Vulkan 検証エラー=" << errors << '\n';
        bPassed = bPassed && errors == 0;
        std::cout << TestName << (bPassed ? " 合格\n" : " 不合格\n");
        return bPassed ? 0 : 1;
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

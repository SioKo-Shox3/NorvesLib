// 配列combined image samplerの要素bindと非一様添字の標本化をGPUで確認する。
#include "RenderingValidation/GpuTestEnvironment.h"

#include "RHI/IBuffer.h"
#include "RHI/ICommandList.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/IDevice.h"
#include "RHI/IShaderCompiler.h"
#include "RHI/ITexture.h"
#include "RHI/RHIDeviceDesc.h"
#include "RHI/RHIDeviceFactory.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace
{
    using namespace NorvesLib;
    using namespace NorvesLib::Core::Container;
    using namespace NorvesLib::RHI;
    using namespace NorvesLib::Test::RenderingValidation;

    constexpr const char* TestName = "RHIDescriptorArrayVulkanTest";
    constexpr uint32_t ArrayCount = 4u;
    constexpr uint32_t InvocationCount = 16u;
    constexpr uint64_t ResultBytes = InvocationCount * 4u * sizeof(float);

    // 各要素の既知色（RGBA8）。要素ごとに全チャネルが異なる値にする。
    constexpr uint8_t ElementColors[ArrayCount][4] = {
        {255u, 0u, 0u, 255u},
        {0u, 255u, 0u, 128u},
        {0u, 0u, 255u, 64u},
        {51u, 102u, 153u, 204u}};

    constexpr const char* ShaderSource = R"glsl(
#version 450
#extension GL_EXT_nonuniform_qualifier : require
layout(local_size_x = 16, local_size_y = 1, local_size_z = 1) in;
layout(set = 0, binding = 0) uniform sampler2D elementTextures[4];
layout(set = 0, binding = 1, std430) buffer Result
{
    vec4 values[];
} result;
void main()
{
    uint invocation = gl_GlobalInvocationID.x;
    // 同じsubgroup内で添字が揃わないよう、呼び出し番号から要素を散らす。
    uint element = (invocation * 3u + 1u) % 4u;
    result.values[invocation] =
        textureLod(elementTextures[nonuniformEXT(element)], vec2(0.5), 0.0);
}
)glsl";

    DescriptorBinding MakeBinding(uint32_t binding, ResourceBindType type, uint32_t count)
    {
        DescriptorBinding result;
        result.binding = binding;
        result.type = type;
        result.stages = ShaderStage::Compute;
        result.count = count;
        return result;
    }

    PipelinePtr CreatePipeline(IDevice& device, const DescriptorSetDesc& setDesc)
    {
        ShaderCompilerPtr compiler = device.CreateShaderCompiler();
        if (!compiler)
        {
            std::cerr << "shader compilerを作成できませんでした\n";
            return {};
        }
        const ShaderCompileResult compileResult = compiler->CompileFromSource(
            String(ShaderSource), ShaderStage::Compute, String(TestName));
        if (!compileResult.bSuccess)
        {
            std::cerr << "shaderのcompileに失敗しました: " << compileResult.ErrorMessage << '\n';
            return {};
        }
        ShaderDesc shaderDesc;
        shaderDesc.stage = ShaderStage::Compute;
        shaderDesc.byteCode = compileResult.ByteCode;
        ShaderPtr shader = device.CreateShader(shaderDesc);
        if (!shader)
        {
            return {};
        }
        ComputePipelineDesc pipelineDesc;
        pipelineDesc.computeShader = shader;
        pipelineDesc.descriptorSetLayouts.push_back(setDesc);
        return device.CreateComputePipeline(pipelineDesc);
    }

    TexturePtr CreateElementTexture(IDevice& device, uint32_t element)
    {
        TextureDesc desc;
        desc.Width = 1u;
        desc.Height = 1u;
        desc.MipLevels = 1u;
        desc.TextureFormat = Format::R8G8B8A8_UNORM;
        desc.Usage = ResourceUsage::ShaderRead;
        desc.DebugName = "DescriptorArrayElement";
        TexturePtr texture = device.CreateTexture(desc);
        if (texture)
        {
            texture->Update(ElementColors[element], 4u, 4u);
        }
        return texture;
    }

    SamplerPtr CreatePointSampler(IDevice& device)
    {
        SamplerDesc desc;
        desc.filterMin = FilterMode::Point;
        desc.filterMag = FilterMode::Point;
        desc.filterMip = FilterMode::Point;
        desc.addressU = TextureAddressMode::Clamp;
        desc.addressV = TextureAddressMode::Clamp;
        desc.addressW = TextureAddressMode::Clamp;
        return device.CreateSampler(desc);
    }

    bool CheckRejections(IDevice& device,
                         const DescriptorSetPtr& descriptorSet,
                         const TexturePtr& texture,
                         const SamplerPtr& sampler)
    {
        bool bPassed = true;
        if (descriptorSet->BindTextureArrayElement(0u, ArrayCount, texture, sampler))
        {
            std::cerr << "範囲外の配列要素へのbindを受理しました\n";
            bPassed = false;
        }
        if (descriptorSet->BindTextureArrayElement(1u, 0u, texture, sampler))
        {
            std::cerr << "storage buffer bindingへの配列要素bindを受理しました\n";
            bPassed = false;
        }
        if (descriptorSet->BindTextureArrayElement(0u, 0u, TexturePtr{}, sampler) ||
            descriptorSet->BindTextureArrayElement(0u, 0u, texture, SamplerPtr{}))
        {
            std::cerr << "nullのtextureまたはsamplerを受理しました\n";
            bPassed = false;
        }

        DescriptorSetDesc singleDesc;
        singleDesc.bindings.push_back(MakeBinding(0u, ResourceBindType::CombinedImageSampler, 1u));
        DescriptorSetPtr singleSet = device.CreateDescriptorSet(singleDesc);
        if (!singleSet || singleSet->BindTextureArrayElement(0u, 0u, texture, sampler))
        {
            std::cerr << "非配列bindingへの配列要素bindを受理しました\n";
            bPassed = false;
        }

        DescriptorSetDesc invalidDesc;
        invalidDesc.bindings.push_back(MakeBinding(0u, ResourceBindType::StructuredBuffer, 2u));
        if (device.CreateDescriptorSet(invalidDesc))
        {
            std::cerr << "combined image sampler以外の配列bindingを受理しました\n";
            bPassed = false;
        }
        std::cout << "descriptor_array_rejections=" << (bPassed ? "PASS" : "FAIL") << '\n';
        return bPassed;
    }

    int RunTest()
    {
        if (IsForcedGpuTestSkipRequested())
        {
            return ReportGpuTestSkip(TestName, "GPUテストが環境変数でスキップされました");
        }
        String unavailableReason;
        if (!CanCreateVulkanDeviceForGpuTest(unavailableReason))
        {
            return ReportGpuTestSkip(TestName, unavailableReason.c_str());
        }
        RHIDeviceDesc deviceDesc;
        deviceDesc.Api = GraphicsAPI::Vulkan;
        deviceDesc.bEnableValidation = true;
        DevicePtr device = CreateRHIDevice(deviceDesc);
        if (!device)
        {
            std::cerr << "Vulkan deviceを作成できませんでした\n";
            return 1;
        }
        if (!device->GetCapabilities().bSampledImageArrayNonUniformIndexing)
        {
            return ReportGpuTestSkip(TestName, "配列sampled imageの非一様添字が非対応です");
        }

        DescriptorSetDesc setDesc;
        setDesc.bindings.push_back(
            MakeBinding(0u, ResourceBindType::CombinedImageSampler, ArrayCount));
        setDesc.bindings.push_back(MakeBinding(1u, ResourceBindType::RWBuffer, 1u));
        PipelinePtr pipeline = CreatePipeline(*device, setDesc);
        DescriptorSetPtr descriptorSet = device->CreateDescriptorSet(setDesc);
        SamplerPtr sampler = CreatePointSampler(*device);
        BufferDesc resultDesc(ResultBytes, ResourceUsage::StorageBuffer, true,
                              "DescriptorArrayResult");
        BufferPtr resultBuffer = device->CreateBuffer(resultDesc);
        CommandListPtr commandList = device->CreateCommandList();
        TexturePtr textures[ArrayCount];
        for (uint32_t element = 0u; element < ArrayCount; ++element)
        {
            textures[element] = CreateElementTexture(*device, element);
        }
        if (!pipeline || !descriptorSet || !sampler || !resultBuffer || !commandList ||
            !textures[0] || !textures[1] || !textures[2] || !textures[3])
        {
            std::cerr << "GPU資源を作成できませんでした\n";
            return 1;
        }

        bool bPassed = CheckRejections(*device, descriptorSet, textures[0], sampler);
        const float sentinel[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
        {
            VariableArray<float> initial(InvocationCount * 4u);
            for (uint32_t index = 0u; index < InvocationCount; ++index)
            {
                std::memcpy(&initial[index * 4u], sentinel, sizeof(sentinel));
            }
            resultBuffer->Update(initial.data(), ResultBytes);
        }

        commandList->Begin();
        for (uint32_t element = 0u; element < ArrayCount; ++element)
        {
            if (!descriptorSet->BindTextureArrayElement(0u, element, textures[element], sampler))
            {
                std::cerr << "配列要素" << element << "をbindできませんでした\n";
                bPassed = false;
            }
        }
        descriptorSet->BindStorageBuffer(1u, resultBuffer, 0u, static_cast<uint32_t>(ResultBytes));
        descriptorSet->Update();
        commandList->SetPipeline(pipeline);
        commandList->SetDescriptorSet(descriptorSet, 0u);
        commandList->Dispatch(1u, 1u, 1u);
        commandList->End();
        commandList->Submit(true);
        device->WaitIdle();

        const auto* mapped = static_cast<const float*>(resultBuffer->Map(0u, ResultBytes));
        if (!mapped)
        {
            std::cerr << "結果bufferをmapできませんでした\n";
            return 1;
        }
        double maximumError = 0.0;
        uint32_t elementHits[ArrayCount] = {};
        for (uint32_t invocation = 0u; invocation < InvocationCount; ++invocation)
        {
            const uint32_t element = (invocation * 3u + 1u) % ArrayCount;
            ++elementHits[element];
            for (uint32_t channel = 0u; channel < 4u; ++channel)
            {
                const double expected = ElementColors[element][channel] / 255.0;
                const double actual = mapped[invocation * 4u + channel];
                const double error = std::isfinite(actual) ? std::abs(actual - expected) : 1.0e9;
                maximumError = error > maximumError ? error : maximumError;
            }
        }
        resultBuffer->Unmap();
        const bool bAllElementsUsed = elementHits[0] > 0u && elementHits[1] > 0u &&
                                      elementHits[2] > 0u && elementHits[3] > 0u;
        std::cout << "descriptor_array_nonuniform_max_error=" << maximumError
                  << " invocations=" << InvocationCount
                  << " elements=" << ArrayCount
                  << " all_elements_used=" << (bAllElementsUsed ? "true" : "false") << '\n';
        // RGBA8 UNORMの復号は1/255刻みで一致するため、量子化の半分を上限にする。
        if (maximumError > 0.5 / 255.0 || !bAllElementsUsed)
        {
            std::cerr << "非一様添字の標本化結果が既知色と一致しません\n";
            bPassed = false;
        }
        return bPassed ? 0 : 1;
    }
}

int main()
{
    return RunTest();
}

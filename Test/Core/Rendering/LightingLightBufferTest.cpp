#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"

#if __has_include("Rendering/LightingPassLightPacking.h")
#include "Rendering/LightingPassLightPacking.h"
#else
#error Rendering/LightingPassLightPacking.h is required for LightingLightBufferTest.
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <string>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;
namespace CoreContainer = NorvesLib::Core::Container;

namespace
{
    void ConfigureAssertOutput()
    {
#ifdef _MSC_VER
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    }

    bool ContainsText(const std::string& source, const std::string& expected)
    {
        return source.find(expected) != std::string::npos;
    }

    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());
        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    std::size_t FindText(const std::string& source, const std::string& expected)
    {
        const std::size_t position = source.find(expected);
        assert(position != std::string::npos);
        return position;
    }

    std::string SliceBetween(const std::string& source,
                             const std::string& beginText,
                             const std::string& endText)
    {
        const std::size_t begin = FindText(source, beginText);
        const std::size_t end = source.find(endText, begin);
        assert(end != std::string::npos);
        return source.substr(begin, end - begin);
    }

    LightProxy MakePointLight(uint32_t index)
    {
        LightProxy proxy;
        proxy.LightId = index + 1;
        proxy.Type = LightType::Point;
        proxy.PositionX = static_cast<float>(index) + 0.25f;
        proxy.PositionY = static_cast<float>(index) + 0.5f;
        proxy.PositionZ = static_cast<float>(index) + 0.75f;
        proxy.DirectionX = -0.25f;
        proxy.DirectionY = -0.5f;
        proxy.DirectionZ = -0.75f;
        proxy.InnerConeAngle = 0.91f;
        proxy.OuterConeAngle = 0.73f;
        proxy.ColorR = 0.1f + static_cast<float>(index);
        proxy.ColorG = 0.2f + static_cast<float>(index);
        proxy.ColorB = 0.3f + static_cast<float>(index);
        proxy.Intensity = 1.0f + static_cast<float>(index);
        proxy.Range = 10.0f + static_cast<float>(index);
        proxy.bVisible = true;
        return proxy;
    }

    void AssertGPULightDataLayout()
    {
        assert(sizeof(GPULightData) == 64);
        assert(offsetof(GPULightData, position) == 0);
        assert(offsetof(GPULightData, direction) == 16);
        assert(offsetof(GPULightData, color) == 32);
        assert(offsetof(GPULightData, attenuation) == 48);
    }

    void TestPackTwentyValidPointLights()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        for (uint32_t i = 0; i < 20; ++i)
        {
            proxies.push_back(MakePointLight(i));
        }

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 20);
        assert(packedLights.size() == 20);

        const GPULightData& first = packedLights[0];
        assert(first.position[0] == 0.25f);
        assert(first.position[1] == 0.5f);
        assert(first.position[2] == 0.75f);
        assert(first.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(first.direction[3] == 0.91f);
        assert(first.color[0] == 0.1f);
        assert(first.color[1] == 0.2f);
        assert(first.color[2] == 0.3f);
        assert(first.color[3] == 1.0f);
        assert(first.attenuation[0] == 10.0f);
        assert(first.attenuation[1] == 0.73f);

        const GPULightData& last = packedLights[19];
        assert(last.position[0] == 19.25f);
        assert(last.position[1] == 19.5f);
        assert(last.position[2] == 19.75f);
        assert(last.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(last.color[0] == 19.1f);
        assert(last.color[1] == 19.2f);
        assert(last.color[2] == 19.3f);
        assert(last.color[3] == 20.0f);
        assert(last.attenuation[0] == 29.0f);
        assert(last.attenuation[1] == 0.73f);
    }

    void TestInvalidLightsAreSkipped()
    {
        CoreContainer::VariableArray<LightProxy> proxies;

        LightProxy invisible = MakePointLight(0);
        invisible.bVisible = false;
        proxies.push_back(invisible);

        LightProxy zeroIntensity = MakePointLight(1);
        zeroIntensity.Intensity = 0.0f;
        proxies.push_back(zeroIntensity);

        LightProxy valid = MakePointLight(2);
        proxies.push_back(valid);

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 1);
        assert(packedLights.size() == 1);
        assert(packedLights[0].position[0] == valid.PositionX);
        assert(packedLights[0].color[3] == valid.Intensity);
    }

    void AssertDefaultDirectionalLight(const GPULightData& light)
    {
        assert(light.position[0] == 0.0f);
        assert(light.position[1] == 0.0f);
        assert(light.position[2] == 0.0f);
        assert(light.position[3] == static_cast<float>(static_cast<int>(LightType::Directional)));
        assert(light.direction[0] == -0.577f);
        assert(light.direction[1] == -0.577f);
        assert(light.direction[2] == -0.577f);
        assert(light.direction[3] == 0.0f);
        assert(light.color[0] == 1.0f);
        assert(light.color[1] == 1.0f);
        assert(light.color[2] == 1.0f);
        assert(light.color[3] == 1.0f);
        assert(light.attenuation[0] == 100.0f);
        assert(light.attenuation[1] == 0.0f);
        assert(light.attenuation[2] == 0.0f);
        assert(light.attenuation[3] == 0.0f);
    }

    void TestEmptyInputProducesOneDefaultLight()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        CoreContainer::VariableArray<GPULightData> packedLights;

        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 1);
        assert(packedLights.size() == 1);
        AssertDefaultDirectionalLight(packedLights[0]);
    }

    void TestAllInvalidInputProducesOneDefaultLight()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        LightProxy invisible = MakePointLight(0);
        invisible.bVisible = false;
        proxies.push_back(invisible);

        LightProxy zeroIntensity = MakePointLight(1);
        zeroIntensity.Intensity = 0.0f;
        proxies.push_back(zeroIntensity);

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 1);
        assert(packedLights.size() == 1);
        AssertDefaultDirectionalLight(packedLights[0]);
    }

    void AssertLightingPassSourceContract(const std::string& sourceRoot)
    {
        const std::string lightingPassSource =
            ReadTextFile(sourceRoot + "/Library/Core/Private/Rendering/LightingPass.cpp");

        const std::string bindingBlock =
            SliceBetween(lightingPassSource,
                         "lightBinding.binding = 5;",
                         "dsDesc.bindings.push_back(lightBinding);");
        assert(ContainsText(bindingBlock, "lightBinding.type = RHI::ResourceBindType::StructuredBuffer;"));

        assert(ContainsText(lightingPassSource, "BindStorageBuffer(5,"));
        assert(!ContainsText(lightingPassSource, "BindConstantBuffer(5,"));
        assert(ContainsText(lightingPassSource, "RHI::ResourceUsage::StorageBuffer | RHI::ResourceUsage::ShaderRead"));
        assert(ContainsText(lightingPassSource, "\"LightArraySSBO\""));
        assert(!ContainsText(lightingPassSource, "MAX_LIGHTS"));
        assert(!ContainsText(lightingPassSource, "LIGHT_BUFFER_SIZE"));
        assert(!std::regex_search(lightingPassSource, std::regex("GPULightData\\s+\\w+\\s*\\[")));
        assert(!ContainsText(lightingPassSource, "lightCount >= MAX_LIGHTS"));
    }

    void AssertLightingShaderSourceContract(const std::string& sourceRoot)
    {
        const std::string shaderSource = ReadTextFile(sourceRoot + "/Assets/Shaders/lighting.frag");

        assert(ContainsText(shaderSource,
                            "layout(std430, set = 0, binding = 5) readonly buffer LightBuffer"));
        assert(ContainsText(shaderSource, "LightData lights[];"));
        assert(!std::regex_search(shaderSource, std::regex("lights\\s*\\[\\s*[0-9]+u?\\s*\\]")));
        assert(!ContainsText(shaderSource, "min(params.lightCount"));
        assert(ContainsText(shaderSource, "for (uint i = 0u; i < params.lightCount; i++)"));
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "LightingLightBufferTest start\n";

    AssertGPULightDataLayout();
    TestPackTwentyValidPointLights();
    TestInvalidLightsAreSkipped();
    TestEmptyInputProducesOneDefaultLight();
    TestAllInvalidInputProducesOneDefaultLight();

#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for LightingLightBufferTest.
#endif

    AssertLightingPassSourceContract(NORVES_SOURCE_DIR);
    AssertLightingShaderSourceContract(NORVES_SOURCE_DIR);

    std::cout << "LightingLightBufferTest passed\n";
    return 0;
}

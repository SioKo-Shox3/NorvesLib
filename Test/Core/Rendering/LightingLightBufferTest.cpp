#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/SceneProxy.h"

#if __has_include("Rendering/LightingPassLightPacking.h")
#include "Rendering/LightingPassLightPacking.h"
#else
#error Rendering/LightingPassLightPacking.h is required for LightingLightBufferTest.
#endif

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

    std::size_t CountText(const std::string& source, const std::string& expected)
    {
        std::size_t count = 0;
        std::size_t searchPosition = 0;
        while (true)
        {
            const std::size_t position = source.find(expected, searchPosition);
            if (position == std::string::npos)
            {
                return count;
            }

            ++count;
            searchPosition = position + expected.size();
        }
    }

    bool NearlyEqual(float actual, float expected, float epsilon = 1.0e-5f)
    {
        return std::fabs(actual - expected) <= epsilon;
    }

    float ReadPackedLightFloat(const GPULightData& light, std::size_t byteOffset)
    {
        float value = 0.0f;
        const auto* bytes = reinterpret_cast<const uint8_t*>(&light);
        std::memcpy(&value, bytes + byteOffset, sizeof(value));
        return value;
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
        proxy.ColorR = 1.0f;
        proxy.ColorG = 1.0f;
        proxy.ColorB = 1.0f;
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
        assert(offsetof(GPULightData, direction) + sizeof(float[4]) == 32);
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
        assert(ReadPackedLightFloat(first, 32) == 1.0f);
        assert(ReadPackedLightFloat(first, 36) == 1.0f);
        assert(ReadPackedLightFloat(first, 40) == 1.0f);
        assert(ReadPackedLightFloat(first, 44) == 1.0f);
        assert(ReadPackedLightFloat(first, 48) == 10.0f);
        assert(ReadPackedLightFloat(first, 52) == 0.73f);

        const GPULightData& last = packedLights[19];
        assert(last.position[0] == 19.25f);
        assert(last.position[1] == 19.5f);
        assert(last.position[2] == 19.75f);
        assert(last.position[3] == static_cast<float>(static_cast<int>(LightType::Point)));
        assert(ReadPackedLightFloat(last, 32) == 1.0f);
        assert(ReadPackedLightFloat(last, 36) == 1.0f);
        assert(ReadPackedLightFloat(last, 40) == 1.0f);
        assert(ReadPackedLightFloat(last, 44) == 20.0f);
        assert(ReadPackedLightFloat(last, 48) == 29.0f);
        assert(ReadPackedLightFloat(last, 52) == 0.73f);
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
        assert(ReadPackedLightFloat(packedLights[0], 44) == valid.Intensity);
    }

    void TestPackWhiteAndColoredLightsUsesYOneChromaticityAndCanonicalIntensity()
    {
        struct ChromaticityCase
        {
            float ColorR;
            float ColorG;
            float ColorB;
            float CanonicalIntensity;
            float ExpectedR;
            float ExpectedG;
            float ExpectedB;
            float ExpectedIntensity;
        };

        // Y=0.2126R+0.7152G+0.0722B. Expected values are hand-derived literals.
        const ChromaticityCase cases[] = {
            {1.0f, 1.0f, 1.0f, 100.0f, 1.0f, 1.0f, 1.0f, 100.0f},
            {1.0f, 0.25f, 0.1f, 50.0f, 2.5086549f, 0.6271637f, 0.2508655f, 50.0f},
        };

        CoreContainer::VariableArray<LightProxy> proxies;
        for (const ChromaticityCase& testCase : cases)
        {
            LightProxy proxy;
            proxy.LightId = static_cast<uint64_t>(proxies.size()) + 1;
            proxy.Type = LightType::Point;
            proxy.ColorR = testCase.ColorR;
            proxy.ColorG = testCase.ColorG;
            proxy.ColorB = testCase.ColorB;
            proxy.CanonicalIntensity = testCase.CanonicalIntensity;
            proxy.bVisible = true;
            proxies.push_back(proxy);
        }

        CoreContainer::VariableArray<GPULightData> packedLights;
        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 2);
        assert(packedLights.size() == 2);
        for (uint32_t index = 0; index < 2; ++index)
        {
            const ChromaticityCase& testCase = cases[index];
            const GPULightData& light = packedLights[index];
            const float normalizedR = ReadPackedLightFloat(light, 32);
            const float normalizedG = ReadPackedLightFloat(light, 36);
            const float normalizedB = ReadPackedLightFloat(light, 40);
            assert(NearlyEqual(normalizedR, testCase.ExpectedR));
            assert(NearlyEqual(normalizedG, testCase.ExpectedG));
            assert(NearlyEqual(normalizedB, testCase.ExpectedB));
            assert(NearlyEqual(ReadPackedLightFloat(light, 44), testCase.ExpectedIntensity));
            assert(NearlyEqual(0.2126f * normalizedR +
                                   0.7152f * normalizedG +
                                   0.0722f * normalizedB,
                               1.0f));
        }
    }

    void TestEmptyInputProducesNoLights()
    {
        CoreContainer::VariableArray<LightProxy> proxies;
        CoreContainer::VariableArray<GPULightData> packedLights;

        const uint32_t count = PackLightingPassLights(
            CoreContainer::Span<const LightProxy>(proxies.data(), proxies.size()),
            packedLights);

        assert(count == 0);
        assert(packedLights.empty());
    }

    void TestAllInvalidInputProducesNoLights()
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

        assert(count == 0);
        assert(packedLights.empty());
    }

    void TestLocalAttenuationBoundaryLiteralTable()
    {
        struct AttenuationCase
        {
            double Distance;
            double ExpectedInverseSquare;
        };

        // The near clamp is 0.01 m; these are fixed, hand-derived contract rows.
        const AttenuationCase cases[] = {
            {0.005, 10000.0},
            {0.010, 10000.0},
            {0.020, 2500.0},
        };

        for (const AttenuationCase& testCase : cases)
        {
            const double actualInverseSquare = ComputeLocalInverseSquare(testCase.Distance);
            assert(std::fabs(actualInverseSquare - testCase.ExpectedInverseSquare) <= 1.0e-9);
        }

        struct RangeWindowCase
        {
            double Distance;
            double Range;
            double ExpectedWindow;
        };

        // The range window is a separate literal term from inverse-square attenuation.
        const RangeWindowCase rangeCases[] = {
            {2.0, 1000.0, 0.999999999968},
            {1000.0, 1000.0, 0.0},
            {1001.0, 1000.0, 0.0},
            {0.0001, 0.00005, 0.0},
        };

        for (const RangeWindowCase& testCase : rangeCases)
        {
            const double actualWindow = ComputeRangeWindow(testCase.Distance, testCase.Range);
            assert(std::fabs(actualWindow - testCase.ExpectedWindow) <= 1.0e-12);
        }
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

        // Static source guard only: behavioral evidence comes from the literal table above
        // and the RenderGraph execution test. Keep inverse-square and range terms separate.
        const std::string inverseSquareFunction =
            SliceBetween(shaderSource,
                         "float CalculateInverseSquareAttenuation(",
                         "float CalculateRangeWindow(");
        assert(std::regex_search(
            inverseSquareFunction,
            std::regex("1\\.0\\s*/\\s*max\\(distance\\s*\\*\\s*distance\\s*,\\s*0\\.01\\s*\\*\\s*0\\.01\\)")));

        const std::string rangeWindowFunction =
            SliceBetween(shaderSource, "float CalculateRangeWindow(", "// PCSS");
        assert(std::regex_search(
            rangeWindowFunction,
            std::regex("max\\(range\\s*,\\s*0\\.0001\\)")));
        assert(std::regex_search(
            rangeWindowFunction,
            std::regex("factor\\s*=\\s*max\\(1\\.0\\s*-\\s*pow\\(distance\\s*/")));
        assert(ContainsText(rangeWindowFunction, "return factor * factor;"));

        const std::string attenuationProduct =
            "CalculateInverseSquareAttenuation(distance) * CalculateRangeWindow(distance, light.attenuation.x)";
        assert(CountText(shaderSource, attenuationProduct) == 2);
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "LightingLightBufferTest start\n";

    AssertGPULightDataLayout();
    TestPackTwentyValidPointLights();
    TestInvalidLightsAreSkipped();
    TestEmptyInputProducesNoLights();
    TestAllInvalidInputProducesNoLights();
    TestPackWhiteAndColoredLightsUsesYOneChromaticityAndCanonicalIntensity();
    TestLocalAttenuationBoundaryLiteralTable();

#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for LightingLightBufferTest.
#endif

    AssertLightingPassSourceContract(NORVES_SOURCE_DIR);
    AssertLightingShaderSourceContract(NORVES_SOURCE_DIR);

    std::cout << "LightingLightBufferTest passed\n";
    return 0;
}

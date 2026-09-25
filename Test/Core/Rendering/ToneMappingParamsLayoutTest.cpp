#include "Rendering/ToneMappingPassGpuTypes.h"
#include "Rendering/ToneMappingPass.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cmath>
#include <string>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

using namespace NorvesLib::Core::Rendering;

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

    std::string ReadTextFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        assert(file.is_open());

        return std::string((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    }

    std::size_t RequirePosition(const std::string& source, const std::string& text)
    {
        const std::size_t position = source.find(text);
        assert(position != std::string::npos);
        return position;
    }

    std::size_t RequirePositionAfter(const std::string& source,
                                     const std::string& text,
                                     std::size_t offset)
    {
        const std::size_t position = source.find(text, offset);
        assert(position != std::string::npos);
        return position;
    }

    void AssertNotContains(const std::string& source, const std::string& text)
    {
        assert(source.find(text) == std::string::npos);
    }
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "ToneMappingParamsLayoutTest start\n";

    assert(sizeof(GPUToneMappingParams) == 64);
    assert(sizeof(GPUToneMappingParams) % 16 == 0);

    assert(offsetof(GPUToneMappingParams, operatorType) == 0);
    assert(offsetof(GPUToneMappingParams, bBypass) == 4);
    assert(offsetof(GPUToneMappingParams, _pad0) == 8);
    assert(offsetof(GPUToneMappingParams, vignetteIntensity) == 12);
    assert(offsetof(GPUToneMappingParams, vignetteRadius) == 16);
    assert(offsetof(GPUToneMappingParams, vignetteSoftness) == 20);
    assert(offsetof(GPUToneMappingParams, _pad1) == 24);
    assert(offsetof(GPUToneMappingParams, _pad2) == 28);
    assert(offsetof(GPUToneMappingParams, colorFilter) == 32);
    assert(offsetof(GPUToneMappingParams, contrast) == 48);
    assert(offsetof(GPUToneMappingParams, saturation) == 52);
    assert(offsetof(GPUToneMappingParams, brightness) == 56);
    assert(offsetof(GPUToneMappingParams, temperature) == 60);

#ifndef NORVES_SHADER_DIR
#error NORVES_SHADER_DIR must be defined for ToneMappingParamsLayoutTest.
#endif

    const std::string shaderPath = std::string(NORVES_SHADER_DIR) + "/tonemapping.frag";
    const std::string shaderSource = ReadTextFile(shaderPath);

    RequirePosition(shaderSource, "layout(std140, set = 0, binding = 1) uniform ToneMappingParams");
    const std::size_t operatorTypePosition = RequirePosition(shaderSource, "uint operatorType;");
    const std::size_t bypassFieldPosition = RequirePosition(shaderSource, "uint bBypass;");
    const std::size_t vignettePosition = RequirePosition(shaderSource, "float vignetteIntensity;");
    const std::size_t pad2Position = RequirePosition(shaderSource, "float _pad2;");
    const std::size_t colorFilterPosition = RequirePosition(shaderSource, "vec4 colorFilter;");
    assert(operatorTypePosition < bypassFieldPosition);
    assert(bypassFieldPosition < vignettePosition);
    assert(vignettePosition < pad2Position);
    assert(pad2Position < colorFilterPosition);

    const std::size_t bypassIfPosition = RequirePosition(shaderSource, "if (params.bBypass != 0u)");
    const std::size_t bypassOutputPosition =
        RequirePositionAfter(shaderSource, "outColor = texture(sceneColor, fragUV);", bypassIfPosition);
    const std::size_t bypassReturnPosition =
        RequirePositionAfter(shaderSource, "return;", bypassOutputPosition);
    const std::size_t hdrSamplePosition =
        RequirePosition(shaderSource, "vec3 hdrColor = texture(sceneColor, fragUV).rgb;");
    assert(bypassIfPosition < bypassOutputPosition);
    assert(bypassOutputPosition < bypassReturnPosition);
    assert(bypassReturnPosition < hdrSamplePosition);

    const std::string cameraToken = std::string("Camera") + "Exposure";
    const std::string paramsCameraToken = std::string("params.") + cameraToken;
    const std::string hdrMultiplyToken = std::string("hdrColor *= ") + paramsCameraToken + ";";
    AssertNotContains(shaderSource, cameraToken);
    AssertNotContains(shaderSource, hdrMultiplyToken);
    assert(shaderSource.find("float gamma;") == std::string::npos);
    assert(shaderSource.find("float exposure;") == std::string::npos);

    const std::string gpuTypesPath = std::string(NORVES_SHADER_DIR) +
                                     "/../../Library/Core/Private/Rendering/ToneMappingPassGpuTypes.h";
    const std::string gpuTypesSource = ReadTextFile(gpuTypesPath);
    AssertNotContains(gpuTypesSource, cameraToken);

    const std::string toneMappingPassPath = std::string(NORVES_SHADER_DIR) +
                                             "/../../Library/Core/Private/Rendering/ToneMappingPass.cpp";
    const std::string toneMappingPassSource = ReadTextFile(toneMappingPassPath);
    AssertNotContains(toneMappingPassSource, paramsCameraToken);
    assert(toneMappingPassSource.find("params._pad2 = 0.0f;") != std::string::npos);
    assert(shaderSource.find("TonemapExposure(vec3 color)") != std::string::npos);
    assert(shaderSource.find("TonemapExposure(hdrColor, params") == std::string::npos);
    assert(shaderSource.find("pow(mapped") == std::string::npos);

    const float physicalInput = 4.0f;
    const float sceneColorPreExposure = 0.25f;
    const float commonEntryInput = physicalInput * sceneColorPreExposure;
    assert(std::abs(commonEntryInput - 1.0f) < 0.0001f);
    const float exposureCurve = 1.0f - std::exp(-commonEntryInput);
    assert(std::abs(exposureCurve - (1.0f - std::exp(-1.0f))) < 0.0001f);

    // ACES 2.0 SDR LUT: enum の並びと operatorType 4、binding 2 の3D LUT、既定のグレーディングを外す分岐
    assert(static_cast<unsigned>(ToneMappingOperator::Aces20Lut) == 4u);
    RequirePosition(shaderSource, "layout(set = 0, binding = 2) uniform sampler3D colorLut;");
    RequirePosition(shaderSource, "const uint ACES20_LUT_OPERATOR = 4u;");
    RequirePosition(shaderSource, "const float ACES20_LUT_SHAPER_OFFSET = 0.00390625;");
    RequirePosition(shaderSource, "const float ACES20_LUT_SHAPER_MAX = 256.0;");
    const std::size_t lutBranchPosition =
        RequirePosition(shaderSource, "else if (params.operatorType == ACES20_LUT_OPERATOR)");
    const std::size_t gradingGuardPosition =
        RequirePositionAfter(shaderSource, "if (params.operatorType != ACES20_LUT_OPERATOR)", lutBranchPosition);
    const std::size_t contrastPosition =
        RequirePositionAfter(shaderSource, "result = ApplyContrast(result, params.contrast);", gradingGuardPosition);
    const std::size_t saturationPosition =
        RequirePositionAfter(shaderSource, "result = ApplySaturation(result, params.saturation);", contrastPosition);
    assert(lutBranchPosition < gradingGuardPosition);
    assert(gradingGuardPosition < saturationPosition);
    assert(toneMappingPassSource.find("colorLutBinding.binding = 2;") != std::string::npos);
    assert(toneMappingPassSource.find("constexpr uint32_t Aces20LutOperatorType = 4u;") != std::string::npos);

    const std::size_t vignetteFalloffPosition =
        RequirePosition(shaderSource, "float vignette = smoothstep(radius - softness, radius, dist);");
    const std::size_t vignetteMixPosition =
        RequirePositionAfter(shaderSource, "return mix(1.0, 1.0 - vignette, intensity);", vignetteFalloffPosition);
    assert(vignetteFalloffPosition < vignetteMixPosition);

    std::cout << "ToneMappingParamsLayoutTest passed\n";
    return 0;
}

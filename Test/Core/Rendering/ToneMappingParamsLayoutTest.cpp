#include "Rendering/ToneMappingPassGpuTypes.h"

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
} // namespace

int main()
{
    ConfigureAssertOutput();

    std::cout << "ToneMappingParamsLayoutTest start\n";

    assert(sizeof(GPUToneMappingParams) == 64);
    assert(sizeof(GPUToneMappingParams) % 16 == 0);

    assert(offsetof(GPUToneMappingParams, CameraExposure) == 0);
    assert(offsetof(GPUToneMappingParams, operatorType) == 4);
    assert(offsetof(GPUToneMappingParams, bBypass) == 8);
    assert(offsetof(GPUToneMappingParams, _pad0) == 12);
    assert(offsetof(GPUToneMappingParams, vignetteIntensity) == 16);
    assert(offsetof(GPUToneMappingParams, vignetteRadius) == 20);
    assert(offsetof(GPUToneMappingParams, vignetteSoftness) == 24);
    assert(offsetof(GPUToneMappingParams, _pad1) == 28);
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
    assert(operatorTypePosition < bypassFieldPosition);
    assert(bypassFieldPosition < vignettePosition);

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

    assert(shaderSource.find("float CameraExposure;") != std::string::npos);
    assert(shaderSource.find("float gamma;") == std::string::npos);
    assert(shaderSource.find("float exposure;") == std::string::npos);
    assert(shaderSource.find("hdrColor *= params.CameraExposure;") != std::string::npos);
    assert(shaderSource.find("TonemapExposure(vec3 color)") != std::string::npos);
    assert(shaderSource.find("TonemapExposure(hdrColor, params") == std::string::npos);
    assert(shaderSource.find("pow(mapped") == std::string::npos);

    const float physicalInput = 4.0f;
    const float cameraExposure = 0.25f;
    const float commonEntryInput = physicalInput * cameraExposure;
    assert(std::abs(commonEntryInput - 1.0f) < 0.0001f);
    const float exposureCurve = 1.0f - std::exp(-commonEntryInput);
    assert(std::abs(exposureCurve - (1.0f - std::exp(-1.0f))) < 0.0001f);

    const std::size_t vignetteFalloffPosition =
        RequirePosition(shaderSource, "float vignette = smoothstep(radius - softness, radius, dist);");
    const std::size_t vignetteMixPosition =
        RequirePositionAfter(shaderSource, "return mix(1.0, 1.0 - vignette, intensity);", vignetteFalloffPosition);
    assert(vignetteFalloffPosition < vignetteMixPosition);

    std::cout << "ToneMappingParamsLayoutTest passed\n";
    return 0;
}

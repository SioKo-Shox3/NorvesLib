#include "Rendering/ToneMappingPassGpuTypes.h"

#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;

namespace
{
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
    std::cout << "ToneMappingParamsLayoutTest start\n";

    assert(sizeof(GPUToneMappingParams) == 64);
    assert(sizeof(GPUToneMappingParams) % 16 == 0);

    assert(offsetof(GPUToneMappingParams, exposure) == 0);
    assert(offsetof(GPUToneMappingParams, gamma) == 4);
    assert(offsetof(GPUToneMappingParams, operatorType) == 8);
    assert(offsetof(GPUToneMappingParams, bBypass) == 12);
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

    std::cout << "ToneMappingParamsLayoutTest passed\n";
    return 0;
}


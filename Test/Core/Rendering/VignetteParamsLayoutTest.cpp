#if __has_include("Rendering/VignettePassGpuTypes.h")
#include "Rendering/VignettePassGpuTypes.h"
#define NORVES_HAS_VIGNETTE_GPU_TYPES 1
#else
#define NORVES_HAS_VIGNETTE_GPU_TYPES 0
#endif

#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

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

    std::cout << "VignetteParamsLayoutTest start\n";

    assert(NORVES_HAS_VIGNETTE_GPU_TYPES == 1);

#if NORVES_HAS_VIGNETTE_GPU_TYPES
    using NorvesLib::Core::Rendering::GPUVignetteParams;

    assert(sizeof(GPUVignetteParams) == 16);
    assert(sizeof(GPUVignetteParams) % 16 == 0);

    assert(offsetof(GPUVignetteParams, intensity) == 0);
    assert(offsetof(GPUVignetteParams, radius) == 4);
    assert(offsetof(GPUVignetteParams, softness) == 8);
    assert(offsetof(GPUVignetteParams, bEnabled) == 12);
#endif

#ifndef NORVES_SHADER_DIR
#error NORVES_SHADER_DIR must be defined for VignetteParamsLayoutTest.
#endif

    const std::string shaderPath = std::string(NORVES_SHADER_DIR) + "/vignette.frag";
    const std::string shaderSource = ReadTextFile(shaderPath);

    RequirePosition(shaderSource, "layout(std140, set = 0, binding = 1) uniform VignetteParams");
    const std::size_t intensityPosition = RequirePosition(shaderSource, "float intensity;");
    const std::size_t radiusPosition = RequirePositionAfter(shaderSource, "float radius;", intensityPosition);
    const std::size_t softnessPosition = RequirePositionAfter(shaderSource, "float softness;", radiusPosition);
    const std::size_t enabledPosition = RequirePositionAfter(shaderSource, "uint bEnabled;", softnessPosition);
    assert(intensityPosition < radiusPosition);
    assert(radiusPosition < softnessPosition);
    assert(softnessPosition < enabledPosition);

    const std::size_t bypassIfPosition = RequirePosition(shaderSource, "if (params.bEnabled == 0u)");
    const std::size_t bypassOutputPosition =
        RequirePositionAfter(shaderSource, "outColor = texture(inputTexture, fragUV);", bypassIfPosition);
    const std::size_t bypassReturnPosition =
        RequirePositionAfter(shaderSource, "return;", bypassOutputPosition);
    assert(bypassIfPosition < bypassOutputPosition);
    assert(bypassOutputPosition < bypassReturnPosition);

    const std::size_t vignetteFalloffPosition =
        RequirePosition(shaderSource, "float vignette = smoothstep(radius - softness, radius, dist);");
    const std::size_t vignetteMixPosition =
        RequirePositionAfter(shaderSource, "return mix(1.0, 1.0 - vignette, intensity);", vignetteFalloffPosition);
    assert(vignetteFalloffPosition < vignetteMixPosition);

    std::cout << "VignetteParamsLayoutTest passed\n";
    return 0;
}

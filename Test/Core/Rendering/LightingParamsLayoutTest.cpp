#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/RenderTypes.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using namespace NorvesLib::Core::Rendering;

namespace
{
    bool ContainsText(const std::string& source, const std::string& expected)
    {
        return source.find(expected) != std::string::npos;
    }

    void AssertShaderDebugModeConstant(const std::string& shaderSource,
                                       const std::string& constantName,
                                       uint32_t expectedValue)
    {
        const std::string expectedText =
            "const uint " + constantName + " = " + std::to_string(expectedValue) + "u;";
        assert(ContainsText(shaderSource, expectedText));
    }
} // namespace

int main()
{
    std::cout << "LightingParamsLayoutTest start\n";

    assert(sizeof(GPULightingParams) == 256);
    assert(sizeof(GPULightingParams) % 16 == 0);

    assert(offsetof(GPULightingParams, invViewProjection) == 0);
    assert(offsetof(GPULightingParams, cameraPosition) == 64);
    assert(offsetof(GPULightingParams, ambientColor) == 80);
    assert(offsetof(GPULightingParams, lightView) == 96);
    assert(offsetof(GPULightingParams, lightProjection) == 160);
    assert(offsetof(GPULightingParams, lightCount) == 224);
    assert(offsetof(GPULightingParams, bShadowEnabled) == 228);
    assert(offsetof(GPULightingParams, envMapMipLevels) == 232);
    assert(offsetof(GPULightingParams, bIBLEnabled) == 236);
    assert(offsetof(GPULightingParams, bSSAOEnabled) == 240);
    assert(offsetof(GPULightingParams, bNeuralBRDFEnabled) == 244);
    assert(offsetof(GPULightingParams, debugViewMode) == 248);
    assert(offsetof(GPULightingParams, _pad2) == 252);

    assert(static_cast<uint8_t>(DebugViewMode::Normal) == 0);
    assert(static_cast<uint8_t>(DebugViewMode::Unlit) == 1);
    assert(static_cast<uint8_t>(DebugViewMode::Wireframe) == 2);
    assert(static_cast<uint8_t>(DebugViewMode::MegaGeometryClusters) == 3);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferAlbedo) == 4);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferNormal) == 5);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferMaterial) == 6);
    assert(static_cast<uint8_t>(DebugViewMode::GBufferDepth) == 7);
    assert(static_cast<uint8_t>(DebugViewMode::LODLevel) == 8);
    assert(static_cast<uint8_t>(DebugViewMode::Count) == 9);

#ifndef NORVES_SHADER_DIR
#error NORVES_SHADER_DIR must be defined for LightingParamsLayoutTest.
#endif

    const std::string shaderPath = std::string(NORVES_SHADER_DIR) + "/lighting.frag";
    std::ifstream shaderFile(shaderPath, std::ios::binary);
    assert(shaderFile.is_open());

    const std::string shaderSource((std::istreambuf_iterator<char>(shaderFile)),
                                   std::istreambuf_iterator<char>());
    assert(ContainsText(shaderSource, "layout(std140, set = 0, binding = 4) uniform LightingParams"));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_NORMAL",
                                  static_cast<uint32_t>(DebugViewMode::Normal));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_UNLIT",
                                  static_cast<uint32_t>(DebugViewMode::Unlit));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_WIREFRAME",
                                  static_cast<uint32_t>(DebugViewMode::Wireframe));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS",
                                  static_cast<uint32_t>(DebugViewMode::MegaGeometryClusters));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_ALBEDO",
                                  static_cast<uint32_t>(DebugViewMode::GBufferAlbedo));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_NORMAL",
                                  static_cast<uint32_t>(DebugViewMode::GBufferNormal));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_MATERIAL",
                                  static_cast<uint32_t>(DebugViewMode::GBufferMaterial));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_GBUFFER_DEPTH",
                                  static_cast<uint32_t>(DebugViewMode::GBufferDepth));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_LOD_LEVEL",
                                  static_cast<uint32_t>(DebugViewMode::LODLevel));
    AssertShaderDebugModeConstant(shaderSource,
                                  "DEBUG_VIEW_MODE_COUNT",
                                  static_cast<uint32_t>(DebugViewMode::Count));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS"));

    std::cout << "LightingParamsLayoutTest passed\n";
    return 0;
}

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

    std::size_t FindText(const std::string& source, const std::string& expected)
    {
        const std::size_t position = source.find(expected);
        assert(position != std::string::npos);
        return position;
    }

    std::size_t FindTextAfter(const std::string& source,
                              const std::string& expected,
                              std::size_t startPosition)
    {
        const std::size_t position = source.find(expected, startPosition);
        assert(position != std::string::npos);
        return position;
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

    assert(ContainsText(shaderSource, "float ComputeDebugDepth01(vec2 uv, float depth)"));
    assert(ContainsText(shaderSource, "vec3 worldPos = ReconstructWorldPosition(uv, depth);"));
    assert(ContainsText(shaderSource, "float cameraDistance = distance(params.cameraPosition.xyz, worldPos);"));
    assert(ContainsText(shaderSource, "float depth01 = cameraDistance / (cameraDistance + 25.0);"));
    assert(ContainsText(shaderSource, "return clamp(depth01, 0.0, 1.0);"));

    const std::size_t gbufferSamplePosition =
        FindText(shaderSource, "float depthSample = texture(gbufferDepth, fragUV).r;");
    const std::size_t skyBranchPosition =
        FindText(shaderSource, "if (albedoSample.a < 0.01)");
    const std::size_t rawAlbedoPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_ALBEDO", gbufferSamplePosition);
    const std::size_t rawNormalPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_NORMAL", gbufferSamplePosition);
    const std::size_t rawMaterialPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_MATERIAL", gbufferSamplePosition);
    const std::size_t rawDepthPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_GBUFFER_DEPTH", gbufferSamplePosition);

    assert(rawAlbedoPosition < skyBranchPosition);
    assert(rawNormalPosition < skyBranchPosition);
    assert(rawMaterialPosition < skyBranchPosition);
    assert(rawDepthPosition < skyBranchPosition);

    assert(ContainsText(shaderSource, "vec3 debugNormal = normalize(normalSample.xyz) * 0.5 + 0.5;"));
    assert(ContainsText(shaderSource, "float depth01 = ComputeDebugDepth01(fragUV, depthSample);"));

    const std::size_t rawAlbedoOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(albedoSample.rgb, 1.0);", rawAlbedoPosition);
    const std::size_t rawNormalOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(debugNormal, 1.0);", rawNormalPosition);
    const std::size_t rawMaterialOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(materialSample.rgb, 1.0);", rawMaterialPosition);
    const std::size_t rawDepthOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(vec3(depth01), 1.0);", rawDepthPosition);

    assert(rawAlbedoOutputPosition < rawNormalPosition);
    assert(rawNormalOutputPosition < rawMaterialPosition);
    assert(rawMaterialOutputPosition < rawDepthPosition);
    assert(rawDepthOutputPosition < skyBranchPosition);

    const std::size_t albedoPassthroughPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||", skyBranchPosition);
    FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||", albedoPassthroughPosition);
    FindTextAfter(shaderSource,
                  "params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS ||",
                  albedoPassthroughPosition);
    const std::size_t lodPassthroughPosition =
        FindTextAfter(shaderSource, "params.debugViewMode == DEBUG_VIEW_MODE_LOD_LEVEL", albedoPassthroughPosition);
    const std::size_t passthroughOutputPosition =
        FindTextAfter(shaderSource, "outColor = vec4(albedoSample.rgb, 1.0);", albedoPassthroughPosition);

    assert(lodPassthroughPosition < passthroughOutputPosition);

    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_UNLIT ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_WIREFRAME ||"));
    assert(ContainsText(shaderSource,
                        "params.debugViewMode == DEBUG_VIEW_MODE_MEGA_GEOMETRY_CLUSTERS"));

    std::cout << "LightingParamsLayoutTest passed\n";
    return 0;
}

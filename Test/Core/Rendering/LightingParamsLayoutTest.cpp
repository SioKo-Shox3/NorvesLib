#include "Rendering/LightingPassGpuTypes.h"
#include "Rendering/RenderTypes.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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

    template <typename T>
    std::size_t PreExposureOffset()
    {
        if constexpr (requires(T value) { value.preExposure; })
        {
            return offsetof(T, preExposure);
        }

        return static_cast<std::size_t>(-1);
    }

    std::string FindShaderUintConstantName(const std::string& source, uint32_t value)
    {
        const std::string valueText = " = " + std::to_string(value) + "u;";
        const std::size_t valuePosition = FindText(source, valueText);
        const std::size_t declarationPosition = source.rfind("const uint ", valuePosition);
        assert(declarationPosition != std::string::npos);

        const std::size_t nameStart = declarationPosition + std::string("const uint ").size();
        const std::size_t nameEnd = source.find(" = ", nameStart);
        assert(nameEnd != std::string::npos);
        assert(declarationPosition < nameStart);
        assert(nameEnd == valuePosition);
        return source.substr(nameStart, nameEnd - nameStart);
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
    ConfigureAssertOutput();

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
    assert(PreExposureOffset<GPULightingParams>() == 252);

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

    const std::size_t debugViewModeFieldPosition = FindText(shaderSource, "uint debugViewMode;");
    const std::size_t preExposureFieldPosition = FindText(shaderSource, "float preExposure;");
    assert(debugViewModeFieldPosition < preExposureFieldPosition);

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

    const std::string validationLambertMode = FindShaderUintConstantName(shaderSource, 253);
    const std::string validationPbrMode = FindShaderUintConstantName(shaderSource, 254);
    assert(CountText(shaderSource, " = 253u;") == 1);
    assert(CountText(shaderSource, " = 254u;") == 1);

    const std::string validationLambertCheck =
        "params.debugViewMode == " + validationLambertMode;
    const std::string validationPbrCheck = "params.debugViewMode == " + validationPbrMode;
    const std::size_t validationLambertCheckPosition =
        FindText(shaderSource, validationLambertCheck);
    const std::size_t validationPbrCheckPosition = FindText(shaderSource, validationPbrCheck);
    assert(validationLambertCheckPosition != validationPbrCheckPosition);

    assert(static_cast<uint8_t>(DebugViewMode::Normal) == 0);
    assert(static_cast<uint8_t>(DebugViewMode::Count) == 9);
    assert(CountText(shaderSource, "params.preExposure") == 1);

    const std::size_t lightDataPosition = FindText(shaderSource, "struct LightData");
    const std::size_t chromaticityPosition =
        FindTextAfter(shaderSource, "vec4 chromaticityAndIntensity;", lightDataPosition);
    const std::size_t lightDataEndPosition = FindTextAfter(shaderSource, "};", lightDataPosition);
    assert(chromaticityPosition < lightDataEndPosition);
    assert(!ContainsText(shaderSource, "vec4 color;"));
    assert(ContainsText(shaderSource,
                        "light.chromaticityAndIntensity.rgb * light.chromaticityAndIntensity.w"));
    assert(ContainsText(shaderSource,
                        "vec4 chromaticityAndIntensity; // xyz=Y=1 chromaticity, w=canonical lux/cd"));
    assert(ContainsText(shaderSource, "float iblExposure = 0.15;"));

    assert(ContainsText(shaderSource,
                        "1.0 / max(distance * distance, 0.01 * 0.01)"));
    assert(!ContainsText(shaderSource, "1.0 / (distance * distance + 1.0)"));
    assert(ContainsText(shaderSource, "float CalculateRangeWindow(float distance, float range)"));
    assert(ContainsText(shaderSource, "max(range, 0.0001)"));
    assert(ContainsText(shaderSource, "return factor * factor;"));
    assert(CountText(shaderSource,
                     "CalculateInverseSquareAttenuation(distance) * CalculateRangeWindow(distance, " +
                         std::string("light.attenuation.x)")) == 2);

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

    const std::size_t skyOutputPosition = shaderSource.find("outColor = vec4(skyColor, 1.0);", skyBranchPosition);
    assert(skyOutputPosition == std::string::npos);

    assert(ContainsText(shaderSource, "return sceneColor * params.preExposure;"));
    assert(CountText(shaderSource, "vec3 ApplySceneColorPreExposure(vec3 sceneColor)") == 1);
    assert(CountText(shaderSource, "ApplySceneColorPreExposure(skyColor)") == 1);
    assert(CountText(shaderSource, "ApplySceneColorPreExposure(color)") == 1);
    assert(ContainsText(shaderSource, "if (bValidationLambert)"));
    assert(ContainsText(shaderSource, "Lo_diffuse += (albedo / PI) * radiance;"));
    assert(ContainsText(shaderSource,
                        "else if (bValidationPBR || params.bNeuralBRDFEnabled == 0u)"));
    assert(ContainsText(shaderSource,
                        "if (!bValidationLambert && !bValidationPBR)"));
    assert(ContainsText(shaderSource,
                        "if (!bValidationLambert && lightType < 0.5 && params.bShadowEnabled != 0u)"));
    assert(ContainsText(shaderSource, "color = Lo_diffuse;"));

    std::cout << "LightingParamsLayoutTest passed\n";
    return 0;
}

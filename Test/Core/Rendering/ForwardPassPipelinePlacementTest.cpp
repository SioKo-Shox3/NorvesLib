#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/SSRPass.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/SkyAtmospherePass.h"
#include "Rendering/VolumetricsPass.h"
#include "Rendering/ToneMappingPass.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <cstdint>
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

    std::string SourceRoot()
    {
        const std::string sourceFilePath = __FILE__;
        const std::string windowsMarker = "\\Test\\Core\\Rendering\\";
        const std::size_t windowsPosition = sourceFilePath.find(windowsMarker);
        if (windowsPosition != std::string::npos)
        {
            return sourceFilePath.substr(0, windowsPosition);
        }

        const std::string unixMarker = "/Test/Core/Rendering/";
        const std::size_t unixPosition = sourceFilePath.find(unixMarker);
        if (unixPosition != std::string::npos)
        {
            return sourceFilePath.substr(0, unixPosition);
        }

        return ".";
    }

    std::string ReadRepositoryFile(const std::string& relativePath)
    {
        const std::string roots[] = {
            SourceRoot(),
            ".",
            "..",
            "../..",
            "../../..",
            "../../../..",
        };

        for (const std::string& root : roots)
        {
            const std::string path = root + "/" + relativePath;
            std::ifstream file(path, std::ios::binary);
            if (file.is_open())
            {
                return std::string((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
            }
        }

        assert(false);
        return {};
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

    std::string ExtractBetween(const std::string& source,
                               const std::string& beginText,
                               const std::string& endText)
    {
        const std::size_t begin = RequirePosition(source, beginText);
        const std::size_t end = RequirePositionAfter(source, endText, begin);
        return source.substr(begin, end + endText.size() - begin);
    }

    std::size_t CountText(const std::string& source, const std::string& text)
    {
        std::size_t count = 0;
        std::size_t offset = 0;
        while (true)
        {
            const std::size_t position = source.find(text, offset);
            if (position == std::string::npos)
            {
                return count;
            }

            ++count;
            offset = position + text.size();
        }
    }

    void AssertFieldsInOrder(const std::string& source,
                             const std::string expectedFields[],
                             std::size_t expectedFieldCount)
    {
        std::size_t offset = 0;
        for (std::size_t index = 0; index < expectedFieldCount; ++index)
        {
            const std::size_t position = RequirePositionAfter(source, expectedFields[index], offset);
            offset = position + expectedFields[index].size();
        }
    }

    void AssertWriterSourceLayoutContract(const std::string& fragmentSource,
                                          const std::string& exposureExpression,
                                          const std::string& alphaExpression)
    {
        assert(CountText(fragmentSource, "sceneColorParams.x") == 1);

        const std::size_t outputPosition = RequirePosition(fragmentSource, "outColor = vec4(");
        const std::size_t outputEnd = RequirePositionAfter(fragmentSource, ";", outputPosition);
        const std::string outputAssignment = fragmentSource.substr(outputPosition,
                                                                   outputEnd - outputPosition);
        const std::size_t exposurePosition = RequirePosition(outputAssignment, exposureExpression);
        const std::size_t alphaPosition = RequirePosition(outputAssignment, alphaExpression);
        assert(exposurePosition < alphaPosition);
    }

    void AssertSceneColorPreExposureDebugModeContract(const std::string& forwardPassSource)
    {
        assert(CountText(forwardPassSource, "context.GetActiveDebugMode()") == 1);
        assert(forwardPassSource.find(
                   "const DebugViewMode activeDebugMode = context.GetActiveDebugMode();") !=
               std::string::npos);
        assert(forwardPassSource.find(
                   "const uint32_t activeDebugModeValue = static_cast<uint32_t>(activeDebugMode);") !=
               std::string::npos);

        const std::size_t whitelistPosition =
            RequirePosition(forwardPassSource, "const bool bApplySceneColorPreExposure =");
        const std::size_t preExposurePosition =
            RequirePositionAfter(forwardPassSource, "const float sceneColorPreExposure =", whitelistPosition);
        const std::string whitelist =
            forwardPassSource.substr(whitelistPosition, preExposurePosition - whitelistPosition);
        assert(CountText(whitelist, "activeDebugMode == DebugViewMode::Normal") == 1);
        assert(CountText(whitelist, "activeDebugModeValue == 252u") == 1);
        assert(CountText(whitelist, "activeDebugModeValue == 253u") == 1);
        assert(CountText(whitelist, "activeDebugModeValue == 254u") == 1);
        assert(CountText(whitelist, "activeDebugModeValue ==") == 3);

        const std::size_t preExposureEnd =
            RequirePositionAfter(forwardPassSource, ";", preExposurePosition);
        const std::string preExposureAssignment =
            forwardPassSource.substr(preExposurePosition, preExposureEnd + 1 - preExposurePosition);
        assert(preExposureAssignment.find("activeCamera && bApplySceneColorPreExposure") !=
               std::string::npos);
        assert(preExposureAssignment.find("activeCamera->PreExposure : 1.0f") != std::string::npos);

        assert(CountText(forwardPassSource,
                         "worldBoardFrameUBO.sceneColorParams[0] = sceneColorPreExposure;") == 1);
        assert(CountText(forwardPassSource,
                         "transparentFrameTemplate.sceneColorParams[0] = sceneColorPreExposure;") == 1);
        assert(CountText(forwardPassSource,
                         "sceneColorParams[0] = activeCamera ? activeCamera->PreExposure : 1.0f;") == 0);
    }

    void AssertForwardLayoutContracts(const std::string& forwardPassSource,
                                      const std::string& transparentVertexSource,
                                      const std::string& transparentFragmentSource,
                                      const std::string& worldBoardVertexSource,
                                      const std::string& worldBoardFragmentSource,
                                      const std::string& impostorVertexSource,
                                      const std::string& impostorFragmentSource)
    {
        const std::string sharedPbrSource =
            ReadRepositoryFile("Assets/Shaders/Common/PbrMaterialEvaluation.glsl");
        const std::string gbufferFragmentSource =
            ReadRepositoryFile("Assets/Shaders/gbuffer.frag");
        struct alignas(16) ExpectedTransparentForwardUBO
        {
            float view[16];
            float projection[16];
            float cameraPosition[4];
            float emissiveColor[4];
            float pomParams[4];
            float sceneColorParams[4];
            float lightView[4][16];
            float lightProjection[4][16];
            float shadowSplitDistances[8];
            uint32_t cascadeCount;
            uint32_t lightCount;
            uint32_t bShadowEnabled;
            uint32_t bIBLEnabled;
            uint32_t prefilteredSpecularMipLevels;
            float iblIntensity;
            uint32_t padding0;
            uint32_t padding1;
            uint32_t padding2;
            alignas(16) float cameraForward[4];
        };

        struct ExpectedWorldBoardForwardUBO
        {
            float view[16];
            float projection[16];
            float cameraPosition[4];
            float cameraRight[4];
            float cameraUp[4];
            float sceneColorParams[4];
        };

        assert(alignof(ExpectedTransparentForwardUBO) == 16);
        assert(offsetof(ExpectedTransparentForwardUBO, view) == 0);
        assert(offsetof(ExpectedTransparentForwardUBO, projection) == 64);
        assert(offsetof(ExpectedTransparentForwardUBO, cameraPosition) == 128);
        assert(offsetof(ExpectedTransparentForwardUBO, emissiveColor) == 144);
        assert(offsetof(ExpectedTransparentForwardUBO, pomParams) == 160);
        assert(offsetof(ExpectedTransparentForwardUBO, sceneColorParams) == 176);
        assert(offsetof(ExpectedTransparentForwardUBO, lightView) == 192);
        assert(offsetof(ExpectedTransparentForwardUBO, lightProjection) == 448);
        assert(offsetof(ExpectedTransparentForwardUBO, shadowSplitDistances) == 704);
        assert(offsetof(ExpectedTransparentForwardUBO, cascadeCount) == 736);
        assert(offsetof(ExpectedTransparentForwardUBO, lightCount) == 740);
        assert(offsetof(ExpectedTransparentForwardUBO, bShadowEnabled) == 744);
        assert(offsetof(ExpectedTransparentForwardUBO, bIBLEnabled) == 748);
        assert(offsetof(ExpectedTransparentForwardUBO, prefilteredSpecularMipLevels) == 752);
        assert(offsetof(ExpectedTransparentForwardUBO, iblIntensity) == 756);
        assert(offsetof(ExpectedTransparentForwardUBO, padding0) == 760);
        assert(offsetof(ExpectedTransparentForwardUBO, padding1) == 764);
        assert(offsetof(ExpectedTransparentForwardUBO, padding2) == 768);
        assert(offsetof(ExpectedTransparentForwardUBO, cameraForward) == 784);
        assert(sizeof(ExpectedTransparentForwardUBO) == 800);
        assert(sizeof(ExpectedWorldBoardForwardUBO) == 192);

        const std::string transparentCpuBlock =
            ExtractBetween(forwardPassSource, "struct alignas(16) TransparentForwardUBO", "};");
        const std::string transparentCpuFields[] = {
            "float view[16];",
            "float projection[16];",
            "float cameraPosition[4];",
            "float emissiveColor[4];",
            "float pomParams[4];",
            "float sceneColorParams[4];",
            "float lightView[4][16];",
            "float lightProjection[4][16];",
            "float shadowSplitDistances[8];",
            "uint32_t cascadeCount;",
            "uint32_t lightCount;",
            "uint32_t bShadowEnabled;",
            "uint32_t bIBLEnabled;",
            "uint32_t prefilteredSpecularMipLevels;",
            "float iblIntensity;",
            "uint32_t padding0;",
            "uint32_t padding1;",
            "uint32_t padding2;",
            "float cameraForward[4];",
        };
        AssertFieldsInOrder(transparentCpuBlock,
                            transparentCpuFields,
                            sizeof(transparentCpuFields) / sizeof(transparentCpuFields[0]));

        const std::string worldBoardCpuBlock =
            ExtractBetween(forwardPassSource, "struct WorldBoardForwardUBO", "};");
        const std::string worldBoardCpuFields[] = {
            "float view[16];",
            "float projection[16];",
            "float cameraPosition[4];",
            "float cameraRight[4];",
            "float cameraUp[4];",
            "float sceneColorParams[4];",
        };
        AssertFieldsInOrder(worldBoardCpuBlock,
                            worldBoardCpuFields,
                            sizeof(worldBoardCpuFields) / sizeof(worldBoardCpuFields[0]));

        const std::string transparentShaderFields[] = {
            "mat4 view;",
            "mat4 projection;",
            "vec4 cameraPosition;",
            "vec4 emissiveColor;",
            "vec4 pomParams;",
            "vec4 sceneColorParams;",
            "mat4 lightView[4];",
            "mat4 lightProjection[4];",
            "vec4 shadowSplitDistances[2];",
            "uint cascadeCount;",
            "uint lightCount;",
            "uint bShadowEnabled;",
            "uint bIBLEnabled;",
            "uint prefilteredSpecularMipLevels;",
            "float iblIntensity;",
            "uint padding0;",
            "uint padding1;",
            "uint padding2;",
            "vec4 cameraForward;",
        };
        const std::string transparentVertexBlock =
            ExtractBetween(transparentVertexSource,
                           "layout(set = 0, binding = 0) uniform MVPData",
                           "} mvp;");
        const std::string transparentFragmentBlock =
            ExtractBetween(transparentFragmentSource,
                           "layout(set = 0, binding = 0) uniform MVPData",
                           "} mvp;");
        AssertFieldsInOrder(transparentVertexBlock,
                            transparentShaderFields,
                            sizeof(transparentShaderFields) / sizeof(transparentShaderFields[0]));
        AssertFieldsInOrder(transparentFragmentBlock,
                            transparentShaderFields,
                            sizeof(transparentShaderFields) / sizeof(transparentShaderFields[0]));

        const std::string worldBoardShaderFields[] = {
            "mat4 view;",
            "mat4 projection;",
            "vec4 cameraPosition;",
            "vec4 cameraRight;",
            "vec4 cameraUp;",
            "vec4 sceneColorParams;",
        };
        const std::string worldBoardVertexBlock =
            ExtractBetween(worldBoardVertexSource,
                           "layout(set = 0, binding = 0) uniform WorldBoardForwardData",
                           "} worldBoard;");
        const std::string worldBoardFragmentBlock =
            ExtractBetween(worldBoardFragmentSource,
                           "layout(set = 0, binding = 0) uniform WorldBoardForwardData",
                           "} worldBoard;");
        const std::string impostorVertexBlock =
            ExtractBetween(impostorVertexSource,
                           "layout(set = 0, binding = 0) uniform WorldBoardForwardData",
                           "} worldBoard;");
        const std::string impostorFragmentBlock =
            ExtractBetween(impostorFragmentSource,
                           "layout(set = 0, binding = 0) uniform WorldBoardForwardData",
                           "} worldBoard;");
        AssertFieldsInOrder(worldBoardVertexBlock,
                            worldBoardShaderFields,
                            sizeof(worldBoardShaderFields) / sizeof(worldBoardShaderFields[0]));
        AssertFieldsInOrder(worldBoardFragmentBlock,
                            worldBoardShaderFields,
                            sizeof(worldBoardShaderFields) / sizeof(worldBoardShaderFields[0]));
        AssertFieldsInOrder(impostorVertexBlock,
                            worldBoardShaderFields,
                            sizeof(worldBoardShaderFields) / sizeof(worldBoardShaderFields[0]));
        AssertFieldsInOrder(impostorFragmentBlock,
                            worldBoardShaderFields,
                            sizeof(worldBoardShaderFields) / sizeof(worldBoardShaderFields[0]));

        assert(forwardPassSource.find("PreExposure") != std::string::npos);
        assert(forwardPassSource.find("worldBoardFrameUBO.sceneColorParams") != std::string::npos);
        assert(forwardPassSource.find("transparentFrameTemplate.sceneColorParams") != std::string::npos);
        assert(forwardPassSource.find(
                   "std::memcpy(transparentFrameTemplate.cameraForward, cameraForward, sizeof(cameraForward));") !=
               std::string::npos);
        AssertSceneColorPreExposureDebugModeContract(forwardPassSource);

        AssertWriterSourceLayoutContract(transparentFragmentSource,
                                         "mvp.sceneColorParams.x",
                                         ", alpha");
        AssertWriterSourceLayoutContract(worldBoardFragmentSource,
                                         "worldBoard.sceneColorParams.x",
                                         ", color.a");
        AssertWriterSourceLayoutContract(impostorFragmentSource,
                                         "worldBoard.sceneColorParams.x",
                                         ", color.a");

        const char* const physicalBindings[] = {
            "layout(std430, set = 0, binding = 8) readonly buffer LightBuffer",
            "layout(set = 0, binding = 9) uniform sampler2DArray shadowMap",
            "layout(set = 0, binding = 10) uniform sampler2D environmentRadiance",
            "layout(set = 0, binding = 11) uniform sampler2D diffuseIrradiance",
            "layout(set = 0, binding = 12) uniform sampler2D prefilteredSpecular",
            "layout(set = 0, binding = 13) uniform sampler2D dfgLut",
        };
        for (const char* binding : physicalBindings)
        {
            assert(transparentFragmentSource.find(binding) != std::string::npos);
        }
        assert(forwardPassSource.find("lightBufferBinding.binding = 8") != std::string::npos);
        assert(forwardPassSource.find("for (uint32_t binding = 9; binding <= 13; ++binding") !=
               std::string::npos);
        assert(forwardPassSource.find("m_DefaultShadowMapArrayTexture") != std::string::npos);
        assert(forwardPassSource.find("GetArraySize() ==") != std::string::npos);
        assert(forwardPassSource.find("BindStorageBuffer(8") != std::string::npos);
        assert(forwardPassSource.find("BindTexture(13, physicalLighting.DfgLutTexture)") !=
               std::string::npos);
        const std::size_t viewportGuardPosition =
            RequirePosition(forwardPassSource, "const bool bHasValidViewport =");
        assert(forwardPassSource.find("context.CurrentViewport != nullptr") != std::string::npos);
        assert(forwardPassSource.find("context.CurrentViewport->HasDrawableExtent()") !=
               std::string::npos);
        assert(forwardPassSource.find("context.CurrentViewport->ViewId != UINT32_MAX") !=
               std::string::npos);
        assert(forwardPassSource.find("context.CurrentViewport->ViewportId != UINT32_MAX") !=
               std::string::npos);
        const std::size_t physicalLightingReadyPosition =
            RequirePosition(forwardPassSource, "const bool bPhysicalLightingReady =");
        assert(viewportGuardPosition < physicalLightingReadyPosition);
        RequirePositionAfter(forwardPassSource,
                             "bHasValidViewport && published.Matches(",
                             physicalLightingReadyPosition);
        assert(transparentFragmentSource.find(
                   "#include \"Common/PbrMaterialEvaluation.glsl\"") != std::string::npos);
        assert(transparentFragmentSource.find("SamplePbrMaterialTextures") != std::string::npos);
        assert(transparentFragmentSource.find("EvaluateAnalyticalDirectEndpointBRDF") !=
               std::string::npos);
        assert(gbufferFragmentSource.find(
                   "#include \"Common/PbrMaterialEvaluation.glsl\"") != std::string::npos);
        assert(gbufferFragmentSource.find("SamplePbrMaterialTextures") != std::string::npos);
        assert(sharedPbrSource.find("vec3 FresnelSchlick") != std::string::npos);
        assert(sharedPbrSource.find("float DistributionGGX") != std::string::npos);
        assert(sharedPbrSource.find("float GeometrySmithDirect") != std::string::npos);
        assert(sharedPbrSource.find("SamplePbrMaterialTextures") != std::string::npos);
        assert(transparentFragmentSource.find("compensationD") != std::string::npos);
        assert(transparentFragmentSource.find("compensationC") != std::string::npos);
        assert(transparentFragmentSource.find("lightBuffer.lights[index]") != std::string::npos);
        assert(transparentFragmentSource.find("mvp.bShadowEnabled") != std::string::npos);
        assert(transparentFragmentSource.find("HasValidCascadedShadowData") != std::string::npos);
        assert(transparentFragmentSource.find("smoothstep(blendStart, boundary, receiverDistance)") !=
               std::string::npos);
        assert(transparentFragmentSource.find("vec3 viewForward = mvp.cameraForward.xyz;") !=
               std::string::npos);
        assert(transparentFragmentSource.find(
                   "dot(worldPos - mvp.cameraPosition.xyz, viewForward)") !=
               std::string::npos);
        assert(transparentFragmentSource.find("mvp.bIBLEnabled") != std::string::npos);
        assert(transparentFragmentSource.find("vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));") ==
               std::string::npos);
        assert(transparentFragmentSource.find("vec3 ambient = 0.12 * baseColor;") == std::string::npos);
        assert(transparentFragmentSource.find("pow(max(dot(normal, halfDir), 0.0), 32.0)") ==
               std::string::npos);
    }

    class InspectableSceneView : public SceneView
    {
    public:
        IViewPass* GetPassAt(uint32_t index) const
        {
            if (index >= m_Passes.size())
            {
                return nullptr;
            }

            return m_Passes[index].get();
        }
    };

    uint32_t FindPostProcessPassIndex(const PostProcessStack* postProcessStack,
                                      const char* expectedName)
    {
        assert(postProcessStack != nullptr);

        const auto& postPasses = postProcessStack->GetPasses();
        const uint32_t postPassCount = static_cast<uint32_t>(postPasses.size());
        for (uint32_t index = 0; index < postPassCount; ++index)
        {
            if (std::strcmp(postPasses[index]->GetName(), expectedName) == 0)
            {
                return index;
            }
        }

        return postPassCount;
    }

    const ToneMappingPass* RequireToneMappingPass(const PostProcessStack* postProcessStack)
    {
        assert(postProcessStack != nullptr);

        IViewPass* toneMappingPassBase = postProcessStack->GetPass("ToneMappingPass");
        assert(toneMappingPassBase != nullptr);

        const auto* toneMappingPass = dynamic_cast<const ToneMappingPass*>(toneMappingPassBase);
        assert(toneMappingPass != nullptr);
        return toneMappingPass;
    }
}

int main()
{
    ConfigureAssertOutput();

    std::cout << "ForwardPassPipelinePlacementTest start\n";

    InspectableSceneView sceneView;
    SceneRenderer sceneRenderer;

    sceneView.SetupDeferredPipeline(&sceneRenderer);

    assert(sceneView.GetPassCount() == 9);

    IViewPass* skyAtmospherePass = sceneView.GetPassAt(5);
    assert(skyAtmospherePass != nullptr);
    assert(std::strcmp(skyAtmospherePass->GetName(), "SkyAtmospherePass") == 0);
    assert(dynamic_cast<SkyAtmospherePass*>(skyAtmospherePass) != nullptr);

    IViewPass* lightingPass = sceneView.GetPassAt(6);
    assert(lightingPass != nullptr);
    assert(std::strcmp(lightingPass->GetName(), "LightingPass") == 0);
    assert(dynamic_cast<LightingPass*>(lightingPass) != nullptr);

    IViewPass* volumetricsPassBase = sceneView.GetPassAt(7);
    assert(volumetricsPassBase != nullptr);
    assert(std::strcmp(volumetricsPassBase->GetName(), "VolumetricsPass") == 0);
    assert(dynamic_cast<VolumetricsPass*>(volumetricsPassBase) != nullptr);

    IViewPass* forwardPassBase = sceneView.GetPassAt(8);
    assert(forwardPassBase != nullptr);
    assert(std::strcmp(forwardPassBase->GetName(), "ForwardPass") == 0);

    auto* forwardPass = dynamic_cast<ForwardPass*>(forwardPassBase);
    assert(forwardPass != nullptr);
    assert(forwardPass->IsTransparentOnly());

    PostProcessStack* postProcessStack = sceneView.GetPostProcessStack();
    assert(postProcessStack != nullptr);
    assert(postProcessStack->GetPassCount() == 7);
    assert(postProcessStack->GetPass("SSRPass") != nullptr);
    assert(dynamic_cast<SSRPass*>(postProcessStack->GetPass("SSRPass")) != nullptr);
    assert(postProcessStack->GetPass("VignettePass") != nullptr);

    // VignettePass は ToneMapping 後の LDR 色へ適用され、DebugDrawPass はその後に描画される。
    const ToneMappingPass* toneMappingPass = RequireToneMappingPass(postProcessStack);
    assert(toneMappingPass->GetSettings().VignetteIntensity == 0.0f);
    assert(postProcessStack->GetPass("DebugDrawPass") != nullptr);

    const auto& postPasses = postProcessStack->GetPasses();
    const uint32_t postPassCount = static_cast<uint32_t>(postPasses.size());
    const uint32_t toneMappingIndex = FindPostProcessPassIndex(postProcessStack, "ToneMappingPass");
    const uint32_t vignetteIndex = FindPostProcessPassIndex(postProcessStack, "VignettePass");
    const uint32_t debugDrawIndex = FindPostProcessPassIndex(postProcessStack, "DebugDrawPass");
    const uint32_t fxaaIndex = FindPostProcessPassIndex(postProcessStack, "FXAAPass");
    const uint32_t upscaleIndex = FindPostProcessPassIndex(postProcessStack, "UpscalePass");
    assert(toneMappingIndex < postPassCount);
    assert(vignetteIndex == toneMappingIndex + 1);
    assert(debugDrawIndex == vignetteIndex + 1);
    assert(fxaaIndex == debugDrawIndex + 1);
    assert(upscaleIndex == fxaaIndex + 1);

    const std::string forwardPassSource =
        ReadRepositoryFile("Library/Core/Private/Rendering/ForwardPass.cpp");
    const std::string transparentVertexSource =
        ReadRepositoryFile("Assets/Shaders/forward_transparent.vert");
    const std::string transparentFragmentSource =
        ReadRepositoryFile("Assets/Shaders/forward_transparent.frag");
    const std::string worldBoardVertexSource =
        ReadRepositoryFile("Assets/Shaders/world_board.vert");
    const std::string worldBoardFragmentSource =
        ReadRepositoryFile("Assets/Shaders/world_board.frag");
    const std::string impostorVertexSource =
        ReadRepositoryFile("Assets/Shaders/impostor.vert");
    const std::string impostorFragmentSource =
        ReadRepositoryFile("Assets/Shaders/impostor.frag");
    AssertForwardLayoutContracts(forwardPassSource,
                                 transparentVertexSource,
                                 transparentFragmentSource,
                                 worldBoardVertexSource,
                                 worldBoardFragmentSource,
                                 impostorVertexSource,
                                 impostorFragmentSource);

    std::cout << "ForwardPassPipelinePlacementTest passed\n";
    return 0;
}

#include "Rendering/RenderTypes.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
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

    bool ContainsText(const std::string& source, const std::string& text)
    {
        return source.find(text) != std::string::npos;
    }

    void AssertContains(const std::string& source, const std::string& text)
    {
        assert(ContainsText(source, text));
    }

    void AssertNotContains(const std::string& source, const std::string& text)
    {
        assert(!ContainsText(source, text));
    }

    void AssertUsesLocalBypassPredicate(const std::string& source)
    {
        AssertContains(source, "const bool bDebugPostProcessBypass");
        AssertContains(source, "IsDebugPostProcessBypassMode(context.GetActiveDebugMode())");
    }

    void AssertDoesNotAssignSettings(const std::string& source)
    {
        const std::regex settingsAssignment(R"(m_Settings\s*\.\s*[A-Za-z_][A-Za-z0-9_]*\s*=)");
        assert(!std::regex_search(source, settingsAssignment));
    }

    void AssertPostProcessSourceContract(const std::string& source)
    {
        AssertUsesLocalBypassPredicate(source);
        AssertDoesNotAssignSettings(source);
        AssertNotContains(source, "SetPassEnabled");
    }
} // namespace

int main()
{
    std::cout << "DebugPostProcessPassThroughContractTest start\n";

    assert(!IsDebugPostProcessBypassMode(DebugViewMode::Normal));
    for (uint8_t mode = static_cast<uint8_t>(DebugViewMode::Unlit);
         mode < static_cast<uint8_t>(DebugViewMode::Count);
         ++mode)
    {
        assert(IsDebugPostProcessBypassMode(static_cast<DebugViewMode>(mode)));
    }
    assert(IsDebugPostProcessBypassMode(DebugViewMode::Count));
    assert(IsDebugPostProcessBypassMode(static_cast<DebugViewMode>(255)));

#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for DebugPostProcessPassThroughContractTest.
#endif

    const std::string sourceDir = NORVES_SOURCE_DIR;
    const std::string shaderDir = sourceDir + "/Assets/Shaders";
    const std::string renderingDir = sourceDir + "/Library/Core/Private/Rendering";

    const std::string ssrPass = ReadTextFile(renderingDir + "/SSRPass.cpp");
    const std::string bloomPass = ReadTextFile(renderingDir + "/BloomPass.cpp");
    const std::string toneMappingPass = ReadTextFile(renderingDir + "/ToneMappingPass.cpp");
    const std::string fxaaPass = ReadTextFile(renderingDir + "/FXAAPass.cpp");

    AssertPostProcessSourceContract(ssrPass);
    AssertPostProcessSourceContract(bloomPass);
    AssertPostProcessSourceContract(toneMappingPass);
    AssertPostProcessSourceContract(fxaaPass);

    AssertContains(ssrPass,
                   "params.bEnabled = bDebugPostProcessBypass ? 0u : (m_Settings.bEnabled ? 1u : 0u);");
    AssertContains(bloomPass,
                   "params.intensity = bDebugPostProcessBypass ? 0.0f : m_Settings.Intensity;");
    AssertContains(toneMappingPass,
                   "params.bBypass = bDebugPostProcessBypass ? 1u : 0u;");
    AssertContains(fxaaPass,
                   "params.bEnabled = bDebugPostProcessBypass ? 0u : (m_Settings.bEnabled ? 1u : 0u);");

    AssertContains(ssrPass, "RenderGraphResourceNames::SSRSceneColor");
    AssertContains(bloomPass, "RenderGraphResourceNames::BloomSceneColor");
    AssertContains(toneMappingPass, "RenderGraphResourceNames::ToneMappedColor");
    AssertContains(fxaaPass, "RenderGraphResourceNames::ToneMappedColor");
    AssertContains(toneMappingPass, "builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor");
    AssertContains(fxaaPass, "builder.ExportTexture(RenderGraphResourceNames::ToneMappedColor");

    const std::string ssrShader = ReadTextFile(shaderDir + "/ssr.frag");
    const std::string bloomShader = ReadTextFile(shaderDir + "/bloom.frag");
    const std::string toneMappingShader = ReadTextFile(shaderDir + "/tonemapping.frag");
    const std::string fxaaShader = ReadTextFile(shaderDir + "/fxaa.frag");

    AssertContains(ssrShader, "if (params.bEnabled == 0u)");
    AssertContains(ssrShader, "outColor = sceneColorSample;");

    AssertContains(bloomShader, "float intensity;");
    AssertContains(bloomShader, "vec3 originalColor = texture(sceneColor, fragUV).rgb;");
    AssertContains(bloomShader, "vec3 result = originalColor + bloom * intensity;");

    AssertContains(toneMappingShader, "uint bBypass;");
    AssertContains(toneMappingShader, "if (params.bBypass != 0u)");
    AssertContains(toneMappingShader, "outColor = texture(sceneColor, fragUV);");

    AssertContains(fxaaShader, "if (params.bEnabled == 0u)");
    AssertContains(fxaaShader, "outColor = texture(inputTexture, fragUV);");

    std::cout << "DebugPostProcessPassThroughContractTest passed\n";
    return 0;
}


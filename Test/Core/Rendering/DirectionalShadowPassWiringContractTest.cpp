#include <cassert>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
    int g_FailureCount = 0;

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

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "FAILED: " << message << "\n";
        }
    }

    void ExpectContains(const std::string& source, const std::string& text, const char* message)
    {
        Expect(ContainsText(source, text), message);
    }

    void ExpectNotContains(const std::string& source, const std::string& text, const char* message)
    {
        Expect(!ContainsText(source, text), message);
    }

    std::size_t FindText(const std::string& source, const std::string& text)
    {
        return source.find(text);
    }

    void ExpectTextBefore(const std::string& source,
                          const std::string& first,
                          const std::string& second,
                          const char* message)
    {
        const std::size_t firstPosition = FindText(source, first);
        const std::size_t secondPosition = FindText(source, second);
        Expect(firstPosition != std::string::npos, "first ordering marker exists");
        Expect(secondPosition != std::string::npos, "second ordering marker exists");
        if (firstPosition != std::string::npos && secondPosition != std::string::npos)
        {
            Expect(firstPosition < secondPosition, message);
        }
    }

    std::string ExtractBlock(const std::string& source,
                             const std::string& beginMarker,
                             const std::string& fallbackBeginMarker,
                             const std::string& endMarker,
                             const std::string& fallbackEndMarker)
    {
        std::size_t beginPosition = source.find(beginMarker);
        if (beginPosition == std::string::npos)
        {
            beginPosition = source.find(fallbackBeginMarker);
        }
        Expect(beginPosition != std::string::npos, "source block begin marker exists");
        if (beginPosition == std::string::npos)
        {
            return std::string{};
        }

        std::size_t endPosition = source.find(endMarker, beginPosition);
        if (endPosition == std::string::npos)
        {
            endPosition = source.find(fallbackEndMarker, beginPosition);
        }
        Expect(endPosition != std::string::npos, "source block end marker exists");
        if (endPosition == std::string::npos)
        {
            return source.substr(beginPosition);
        }

        return source.substr(beginPosition, endPosition - beginPosition);
    }

    void AssertNoLiveSceneViewProxyReads(const std::string& source)
    {
        ExpectNotContains(source,
                          "m_SceneView->GetMeshProxies",
                          "pass does not read live mesh proxies from SceneView");
        ExpectNotContains(source,
                          "m_SceneView->GetLightProxies",
                          "pass does not read live light proxies from SceneView");
        ExpectNotContains(source,
                          "m_SceneView->GetMegaGeometryProxies",
                          "pass does not read live mega geometry proxies from SceneView");
        ExpectNotContains(source,
                          "m_SceneView->Get",
                          "pass does not read live SceneView getters");
        ExpectNotContains(source,
                          "SceneView::GetMeshProxies",
                          "pass does not call SceneView mesh proxy getters");
        ExpectNotContains(source,
                          "SceneView::GetLightProxies",
                          "pass does not call SceneView light proxy getters");
        ExpectNotContains(source,
                          "SceneView::GetMegaGeometryProxies",
                          "pass does not call SceneView mega geometry proxy getters");
    }

    void AssertCascadedShadowMatrixCall(const std::string& source, const char* message)
    {
        const std::string callBlock =
            ExtractBlock(source,
                         "BuildCascadedShadowLightMatrices",
                         "BuildCascadedShadowLightMatrices",
                         ");",
                         ");");
        ExpectContains(callBlock,
                       "BuildCascadedShadowLightMatrices",
                       message);
        ExpectContains(callBlock,
                       "context.SnapshotLightProxies",
                       "cascaded shadow call uses snapshot light proxies");
        ExpectContains(callBlock,
                       "context.GetActiveCamera()",
                       "cascaded shadow call uses the active camera snapshot");
    }

    void AssertClipSpaceShadowCopyContract(const std::string& source)
    {
        ExpectContains(source,
                       "context.Device->AdjustProjectionForClipSpace(",
                       "pass adjusts shadow projection for clip space");
        ExpectContains(source,
                       "cascadedShadowMatrices.Cascades[cascadeIndex].Projection",
                       "pass adjusts every cascade projection for clip space");
        ExpectContains(source,
                       "CopyShadowMatrixToShaderData(lightProjMat",
                       "pass copies adjusted shadow projection to shader data");
    }

    void AssertViewRenderContextContract(const std::string& source)
    {
        ExpectContains(source,
                       "struct ShadowMapPassSettings;",
                       "ViewRenderContext forward declares ShadowMapPassSettings");
        ExpectContains(source,
                       "SnapshotMeshProxies",
                       "ViewRenderContext exposes snapshot mesh proxies");
        ExpectContains(source,
                       "ActiveShadowMapSettings",
                       "ViewRenderContext tracks active ShadowMapPass settings");
        ExpectContains(source,
                       "struct PhysicalLightingResources",
                       "ViewRenderContext owns the physical-lighting publication aggregate");
        ExpectContains(source,
                       "PhysicalLightingResources PhysicalLighting;",
                       "ViewRenderContext exposes physical-lighting resources");
        ExpectContains(source,
                       "CascadedDirectionalShadowShaderValues",
                       "ViewRenderContext exposes cascaded shadow shader values");
        ExpectContains(source,
                       "PhysicalLightingShadowCascadeCount = 4u",
                       "ViewRenderContext fixes the four-cascade publication count");
        ExpectContains(source,
                       "void PublishCascadedShadow",
                       "physical-lighting aggregate publishes cascaded shadow data");
        ExpectContains(source,
                       "void PublishDirectionalShadow",
                       "physical-lighting aggregate publishes directional shadow data");
        ExpectContains(source,
                       "void PublishLighting",
                       "physical-lighting aggregate publishes light and IBL data");
        ExpectContains(source,
                       "bool Matches(uint64_t frameNumber, uint32_t viewId, uint32_t viewportId)",
                       "physical-lighting aggregate validates frame and viewport identity");
    }

    void AssertRenderingCoordinatorContract(const std::string& source)
    {
        ExpectContains(source,
                       "viewContext.SnapshotMeshProxies = &packet->Scene.MeshProxies;",
                       "RenderingCoordinator assigns snapshot mesh proxies from FramePacket");
    }

    void AssertRenderFrameExecutorContract(const std::string& source)
    {
        ExpectContains(source,
                       "context.ActiveShadowMapSettings = nullptr;",
                       "ApplyViewportRenderPlan resets active ShadowMapPass settings");
        ExpectTextBefore(source,
                         "context.ActiveShadowMapSettings = nullptr;",
                         "if (!viewportPlan)",
                         "active ShadowMapPass settings reset precedes null viewport early return");
        ExpectTextBefore(source,
                         "context.ActiveShadowMapSettings = nullptr;",
                         "if (!context.SnapshotDrawCommandSource)",
                         "active ShadowMapPass settings reset precedes missing draw source early return");
    }

    void AssertShadowMapPassContract(const std::string& source)
    {
        ExpectContains(source,
                       "#include \"Rendering/CascadedShadowLightMatrices.h\"",
                       "ShadowMapPass includes the cascaded shadow helper");
        ExpectContains(source,
                       "context.ActiveShadowMapSettings = &m_Settings;",
                       "ShadowMapPass records active settings in ViewRenderContext");
        ExpectContains(source,
                       "MakeDirectionalShadowMatrixSettings(m_Settings)",
                       "ShadowMapPass converts its settings through helper");
        ExpectContains(source,
                       "MakeDefaultCascadedShadowMatrixSettings()",
                       "ShadowMapPass initializes the cascaded settings");
        AssertCascadedShadowMatrixCall(source,
                                       "ShadowMapPass builds four cascaded matrices from snapshots and camera");
        ExpectContains(source,
                       "CSM_CASCADE_COUNT",
                       "ShadowMapPass keeps the fixed four-cascade count");
        ExpectContains(source,
                       "m_ShadowFramebuffers[cascadeIndex]",
                       "ShadowMapPass selects the framebuffer for each cascade");
        ExpectContains(source,
                       "for (uint32_t cascadeIndex",
                       "ShadowMapPass records each cascade independently");
        ExpectContains(source,
                       "CopyShadowMatrixToShaderData",
                       "ShadowMapPass copies cascaded matrices through helper");
        ExpectContains(source,
                       "context.PhysicalLighting.PublishCascadedShadow",
                       "ShadowMapPass publishes all cascaded matrices");
        ExpectContains(source,
                       "context.PhysicalLighting.PublishDirectionalShadow",
                       "ShadowMapPass preserves the single-shadow compatibility publication");
        ExpectContains(source,
                       "m_ShadowSampler",
                       "ShadowMapPass publishes a shadow sampler");
        AssertClipSpaceShadowCopyContract(source);
        ExpectContains(source,
                       "RegisterTexturePtr(\"ShadowMap\", m_ShadowMapTexture)",
                       "ShadowMapPass preserves legacy ShadowMap bridge registration");
        ExpectContains(source,
                       "const bool bCanBuildShadowCommands =",
                       "ShadowMapPass limits shadow command population to enabled shadows");
        ExpectContains(source,
                       "cascadedShadowMatrices.bEnabled &&",
                       "ShadowMapPass gates command population on cascaded matrices");
        ExpectContains(source,
                       "context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass",
                       "ShadowMapPass enqueues geometry passes to establish every layer layout");
        ExpectTextBefore(source,
                         "const bool bCanBuildShadowCommands =",
                         "context.EnqueueFrameCommand(FrameCommand::CreateGeometryPass",
                         "ShadowMapPass populates shadow commands before unconditionally enqueueing the pass");
        ExpectNotContains(source,
                          "if (!cascadedShadowMatrices.bEnabled)",
                          "ShadowMapPass does not return before enqueueing the empty depth pass");
        ExpectContains(source,
                       "if (!cmd.Draw.bCastShadow)",
                       "ShadowMapPass keeps DrawCommand shadow-caster filtering");
        ExpectNotContains(source,
                          "-0.577f",
                          "ShadowMapPass no longer contains hardcoded shadow direction");
        AssertNoLiveSceneViewProxyReads(source);
    }

    void AssertLightingPassContract(const std::string& source)
    {
        ExpectNotContains(source,
                          "#include \"Rendering/DirectionalShadowLightMatrices.h\"",
                          "LightingPass does not recompute directional shadow matrices");
        ExpectNotContains(source,
                          "MakeDirectionalShadowMatrixSettings",
                          "LightingPass does not call the directional shadow helper");
        ExpectContains(source,
                       "context.PhysicalLighting.DirectionalShadow.View",
                       "LightingPass consumes the published shadow view matrix");
        ExpectContains(source,
                       "context.PhysicalLighting.DirectionalShadow.Projection",
                       "LightingPass consumes the published shadow projection matrix");
        ExpectContains(source,
                       "context.PhysicalLighting.PublishLighting(",
                       "LightingPass publishes the light SSBO and IBL resources");
        ExpectContains(source,
                       "m_LightArrayBuffer",
                       "LightingPass publishes the physical light SSBO");
        ExpectContains(source,
                       "m_EnvironmentTexture",
                       "LightingPass publishes environment radiance");
        ExpectContains(source,
                       "m_DiffuseIrradianceTexture",
                       "LightingPass publishes diffuse irradiance");
        ExpectContains(source,
                       "m_PrefilteredSpecularTexture",
                       "LightingPass publishes prefiltered specular radiance");
        ExpectContains(source,
                       "m_BrdfLutTexture",
                       "LightingPass publishes the DFG LUT");
        ExpectContains(source,
                       "descriptorSet->BindStorageBuffer(5",
                       "LightingPass binds the physical light SSBO at binding five");
        ExpectTextBefore(source,
                         "UpdateLightBuffer(context, false, false)",
                         "CreateLightingDescriptorSet(initialDescriptorSet)",
                         "initial light publication precedes the mandatory descriptor binding");
        AssertNoLiveSceneViewProxyReads(source);
    }
} // namespace

int main()
{
    std::cout << "DirectionalShadowPassWiringContractTest start\n";

#ifndef NORVES_SOURCE_DIR
#error NORVES_SOURCE_DIR must be defined for DirectionalShadowPassWiringContractTest.
#endif

    const std::string sourceDir = NORVES_SOURCE_DIR;
    const std::string publicRenderingDir = sourceDir + "/Library/Core/Public/Rendering";
    const std::string privateRenderingDir = sourceDir + "/Library/Core/Private/Rendering";

    const std::string viewRenderContext = ReadTextFile(publicRenderingDir + "/ViewRenderContext.h");
    const std::string renderingCoordinator = ReadTextFile(privateRenderingDir + "/RenderingCoordinator.cpp");
    const std::string renderFrameExecutor = ReadTextFile(privateRenderingDir + "/RenderFrameExecutor.cpp");
    const std::string shadowMapPass = ReadTextFile(privateRenderingDir + "/ShadowMapPass.cpp");
    const std::string lightingPass = ReadTextFile(privateRenderingDir + "/LightingPass.cpp");

    AssertViewRenderContextContract(viewRenderContext);
    AssertRenderingCoordinatorContract(renderingCoordinator);
    AssertRenderFrameExecutorContract(renderFrameExecutor);
    AssertShadowMapPassContract(shadowMapPass);
    AssertLightingPassContract(lightingPass);

    if (g_FailureCount != 0)
    {
        std::cout << "DirectionalShadowPassWiringContractTest failed with "
                  << g_FailureCount << " failure(s)\n";
        return 1;
    }

    std::cout << "DirectionalShadowPassWiringContractTest passed\n";
    return 0;
}

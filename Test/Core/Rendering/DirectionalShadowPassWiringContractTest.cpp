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

    void AssertFittedShadowMatrixCall(const std::string& source, const char* message)
    {
        const std::string callBlock =
            ExtractBlock(source,
                         "BuildFittedDirectionalShadowLightMatrices",
                         "BuildFittedDirectionalShadowLightMatrices",
                         ");",
                         ");");
        ExpectContains(callBlock,
                       "BuildFittedDirectionalShadowLightMatrices",
                       message);
        ExpectContains(callBlock,
                       "context.SnapshotLightProxies",
                       "fitted shadow call uses snapshot light proxies");
        ExpectContains(callBlock,
                       "context.SnapshotMeshProxies",
                       "fitted shadow call uses snapshot mesh proxies");
        ExpectContains(callBlock,
                       "context.SnapshotMegaGeometryProxies",
                       "fitted shadow call uses snapshot mega geometry proxies");
    }

    void AssertClipSpaceShadowCopyContract(const std::string& source)
    {
        ExpectContains(source,
                       "context.Device->AdjustProjectionForClipSpace(shadowMatrices.Projection, false)",
                       "pass adjusts shadow projection for clip space with non-reversed depth");
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
                       "#include \"Rendering/DirectionalShadowLightMatrices.h\"",
                       "ShadowMapPass includes DirectionalShadowLightMatrices helper");
        ExpectContains(source,
                       "context.ActiveShadowMapSettings = &m_Settings;",
                       "ShadowMapPass records active settings in ViewRenderContext");
        ExpectContains(source,
                       "MakeDirectionalShadowMatrixSettings(m_Settings)",
                       "ShadowMapPass converts its settings through helper");
        AssertFittedShadowMatrixCall(source,
                                     "ShadowMapPass builds fitted matrices from snapshot proxies and settings");
        ExpectContains(source,
                       "CopyShadowMatrixToShaderData",
                       "ShadowMapPass copies shadow matrices through helper");
        AssertClipSpaceShadowCopyContract(source);
        ExpectContains(source,
                       "RegisterTexturePtr(\"ShadowMap\", m_ShadowMapTexture)",
                       "ShadowMapPass preserves legacy ShadowMap bridge registration");
        ExpectContains(source,
                       "if (!shadowMatrices.bEnabled)",
                       "ShadowMapPass has disabled-shadow early return marker");
        ExpectTextBefore(source,
                         "RegisterTexturePtr(\"ShadowMap\", m_ShadowMapTexture)",
                         "if (!shadowMatrices.bEnabled)",
                         "ShadowMapPass registers legacy ShadowMap before disabled return");
        ExpectTextBefore(source,
                         "if (!shadowMatrices.bEnabled)",
                         "FrameCommand::CreateGeometryPass",
                         "ShadowMapPass disabled return precedes geometry enqueue");
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
        ExpectContains(source,
                       "#include \"Rendering/DirectionalShadowLightMatrices.h\"",
                       "LightingPass includes DirectionalShadowLightMatrices helper");
        ExpectContains(source,
                       "context.ActiveShadowMapSettings",
                       "LightingPass reads active ShadowMapPass settings");
        ExpectContains(source,
                       "MakeDirectionalShadowMatrixSettings(*context.ActiveShadowMapSettings)",
                       "LightingPass converts active ShadowMapPass settings through helper");
        AssertFittedShadowMatrixCall(source,
                                     "LightingPass builds fitted matrices from snapshot proxies and active/default settings");
        ExpectContains(source,
                       "CopyIdentityShadowMatricesToShaderData(params.lightView, params.lightProjection)",
                       "LightingPass writes identity matrices through helper on disabled path");
        ExpectContains(source,
                       "CopyShadowMatrixToShaderData(shadowMatrices.View, params.lightView)",
                       "LightingPass copies enabled shadow view through helper");
        ExpectContains(source,
                       "CopyShadowMatrixToShaderData(lightProjMat, params.lightProjection)",
                       "LightingPass copies enabled shadow projection through helper");
        AssertClipSpaceShadowCopyContract(source);
        ExpectContains(source,
                       "params.bShadowEnabled = 1",
                       "LightingPass enables shadow flag on enabled helper path");
        ExpectContains(source,
                       "params.bShadowEnabled = 0",
                       "LightingPass disables shadow flag on disabled helper path");

        const std::string shadowMatrixBlock =
            ExtractBlock(source,
                         "// シャドウマップ用ライトビュー・プロジェクション行列",
                         "if (bShadowAvailable)",
                         "// SceneViewのLightProxyからライト配列を構築",
                         "uint32_t lightCount = 0;");
        ExpectNotContains(shadowMatrixBlock,
                          "-0.577f",
                          "LightingPass shadow-matrix block no longer contains hardcoded direction");
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

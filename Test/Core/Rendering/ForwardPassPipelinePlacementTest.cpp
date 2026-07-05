#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/SSRPass.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include "Rendering/ToneMappingPass.h"
#include <cassert>
#include <cstring>
#include <iostream>
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

    assert(sceneView.GetPassCount() == 7);

    IViewPass* lightingPass = sceneView.GetPassAt(5);
    assert(lightingPass != nullptr);
    assert(std::strcmp(lightingPass->GetName(), "LightingPass") == 0);
    assert(dynamic_cast<LightingPass*>(lightingPass) != nullptr);

    IViewPass* forwardPassBase = sceneView.GetPassAt(6);
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

    InspectableSceneView forwardSceneView;
    forwardSceneView.SetupForwardPipeline(&sceneRenderer);

    PostProcessStack* forwardPostProcessStack = forwardSceneView.GetPostProcessStack();
    assert(forwardPostProcessStack != nullptr);
    assert(forwardPostProcessStack->GetPassCount() == 3);
    assert(forwardPostProcessStack->GetPass("VignettePass") != nullptr);

    const ToneMappingPass* forwardToneMappingPass = RequireToneMappingPass(forwardPostProcessStack);
    assert(forwardToneMappingPass->GetSettings().VignetteIntensity == 0.0f);

    const auto& forwardPostPasses = forwardPostProcessStack->GetPasses();
    const uint32_t forwardPostPassCount = static_cast<uint32_t>(forwardPostPasses.size());
    const uint32_t forwardToneMappingIndex =
        FindPostProcessPassIndex(forwardPostProcessStack, "ToneMappingPass");
    const uint32_t forwardVignetteIndex =
        FindPostProcessPassIndex(forwardPostProcessStack, "VignettePass");
    const uint32_t forwardUpscaleIndex =
        FindPostProcessPassIndex(forwardPostProcessStack, "UpscalePass");
    assert(forwardToneMappingIndex < forwardPostPassCount);
    assert(forwardVignetteIndex == forwardToneMappingIndex + 1);
    assert(forwardUpscaleIndex == forwardVignetteIndex + 1);

    std::cout << "ForwardPassPipelinePlacementTest passed\n";
    return 0;
}

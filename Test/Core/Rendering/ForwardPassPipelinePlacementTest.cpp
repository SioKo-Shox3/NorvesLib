#include "Rendering/ForwardPass.h"
#include "Rendering/LightingPass.h"
#include "Rendering/PostProcessStack.h"
#include "Rendering/SSRPass.h"
#include "Rendering/SceneRenderer.h"
#include "Rendering/SceneView.h"
#include <cassert>
#include <cstring>
#include <iostream>

using namespace NorvesLib::Core::Rendering;

namespace
{
    class InspectableSceneView : public SceneView
    {
    public:
        IViewPass *GetPassAt(uint32_t index) const
        {
            if (index >= m_Passes.size())
            {
                return nullptr;
            }

            return m_Passes[index].get();
        }
    };
}

int main()
{
    std::cout << "ForwardPassPipelinePlacementTest start\n";

    InspectableSceneView sceneView;
    SceneRenderer sceneRenderer;

    sceneView.SetupDeferredPipeline(&sceneRenderer);

    assert(sceneView.GetPassCount() == 7);

    IViewPass *lightingPass = sceneView.GetPassAt(5);
    assert(lightingPass != nullptr);
    assert(std::strcmp(lightingPass->GetName(), "LightingPass") == 0);
    assert(dynamic_cast<LightingPass *>(lightingPass) != nullptr);

    IViewPass *forwardPassBase = sceneView.GetPassAt(6);
    assert(forwardPassBase != nullptr);
    assert(std::strcmp(forwardPassBase->GetName(), "ForwardPass") == 0);

    auto *forwardPass = dynamic_cast<ForwardPass *>(forwardPassBase);
    assert(forwardPass != nullptr);
    assert(forwardPass->IsTransparentOnly());

    PostProcessStack *postProcessStack = sceneView.GetPostProcessStack();
    assert(postProcessStack != nullptr);
    assert(postProcessStack->GetPassCount() == 5);
    assert(postProcessStack->GetPass("SSRPass") != nullptr);
    assert(dynamic_cast<SSRPass *>(postProcessStack->GetPass("SSRPass")) != nullptr);

    std::cout << "ForwardPassPipelinePlacementTest passed\n";
    return 0;
}

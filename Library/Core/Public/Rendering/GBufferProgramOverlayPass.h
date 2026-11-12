// 解決後の GBuffer に標準でない材質（プログラム・Masked・両面）を重ねるパス。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderExtension/RenderExtensionHelpers.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"

namespace NorvesLib::Core::Rendering
{
    class GBufferPass;

    class GBufferProgramOverlayPass final : public IViewPass, public IRenderGraphPass
    {
    public:
        const char* GetName() const override { return "GBufferProgramOverlayPass"; }
        void SetGBufferPass(GBufferPass* pass) { m_GBufferPass = pass; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        GBufferPass* m_GBufferPass = nullptr;
        GBufferTargetBinder m_Targets;
        bool m_bDeclared = false;
        bool m_bLoggedEmpty = false;
    };
} // namespace NorvesLib::Core::Rendering

#pragma once

#include "Rendering/GBufferOverlayTargets.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include <cstddef>
#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    /** @brief インポスターの頂点・断片シェーダーに渡す std140 定数。 */
    struct alignas(16) ImpostorGBufferUniform
    {
        float view[16];
        float projection[16];
        float previousView[16];
        float previousProjection[16];
        float cameraPosition[4];
        float cameraRight[4];
        float cameraUp[4];
        float frameParams[4];
        float atlasParams[4];
    };
    static_assert(offsetof(ImpostorGBufferUniform, view) == 0u);
    static_assert(offsetof(ImpostorGBufferUniform, projection) == 64u);
    static_assert(offsetof(ImpostorGBufferUniform, previousView) == 128u);
    static_assert(offsetof(ImpostorGBufferUniform, previousProjection) == 192u);
    static_assert(offsetof(ImpostorGBufferUniform, cameraPosition) == 256u);
    static_assert(offsetof(ImpostorGBufferUniform, cameraRight) == 272u);
    static_assert(offsetof(ImpostorGBufferUniform, cameraUp) == 288u);
    static_assert(offsetof(ImpostorGBufferUniform, frameParams) == 304u);
    static_assert(offsetof(ImpostorGBufferUniform, atlasParams) == 320u);
    static_assert(sizeof(ImpostorGBufferUniform) == 336u);

    /** @brief インポスターの束を GBuffer と深度へ重ねて描く。 */
    class ImpostorGBufferPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ImpostorGBufferPass() = default;
        ~ImpostorGBufferPass() override;

        const char *GetName() const override { return "ImpostorGBufferPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

    private:
        void LogDrawCounts(uint32_t batches, uint64_t instances);

        bool m_bInitializeResources = false;
        RHI::IDevice *m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::TexturePtr m_DefaultTexture;
        RHI::SamplerPtr m_AtlasSampler;
        GBufferOverlayTargets m_OverlayTargets;
        GBufferOverlayPipelineDesc m_OverlayPipelineDesc;
        RGResourceHandle m_ColorHandles[GBufferOverlayTargets::kColorTargetCount];
        RGResourceHandle m_DepthHandle;
        uint32_t m_LoggedBatchCount = 0;
        uint64_t m_LoggedInstanceCount = 0;
    };
} // namespace NorvesLib::Core::Rendering

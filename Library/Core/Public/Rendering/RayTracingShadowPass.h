// レイトレーシングによる方向光ハードシャドウの生成を担当する。
#pragma once

#include "Container/Containers.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    class RayTracingShadowPass
    {
    public:
        bool Execute(ViewRenderContext& context,
                     const RHI::TexturePtr& depthTexture,
                     const RHI::TexturePtr& normalTexture,
                     bool bEnabled,
                     RHI::TexturePtr& outVisibilityTexture);
        void Shutdown();

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint32_t Width = 0;
            uint32_t Height = 0;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::TexturePtr VisibilityTexture;
            RHI::ResourceState VisibilityState = RHI::ResourceState::Undefined;
        };

        bool EnsurePipeline(ViewRenderContext& context);
        FrameResources* FindOrCreateFrameResources(ViewRenderContext& context,
                                                   uint32_t width,
                                                   uint32_t height);

        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_RayGenerationShader;
        RHI::ShaderPtr m_MissShader;
        RHI::ShaderPtr m_ClosestHitShader;
        RHI::SamplerPtr m_Sampler;
        Container::VariableArray<FrameResources> m_FrameResources;
        bool m_bPipelineAttempted = false;
        bool m_bPipelineUnavailable = false;
        bool m_bFailureReported = false;
    };
}

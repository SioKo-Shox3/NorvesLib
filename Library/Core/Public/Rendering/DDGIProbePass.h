#pragma once

#include "Container/Containers.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    /**
     * @brief DDGIプローブレイのレイトレーシング結果
     *
     * Compute shaderとの共有用に、各項目を32 bit値で保持する。
     */
    struct DDGIProbeRayQueryResult
    {
        float Distance = -1.0f;
        uint32_t bHit = 0u;
        uint32_t InstanceCustomIndex = UINT32_MAX;
        uint32_t PrimitiveIndex = UINT32_MAX;
    };

    static_assert(sizeof(DDGIProbeRayQueryResult) == 16u);

    /**
     * @brief FramePacketのTLASに対するDDGIプローブレイ問い合わせを記録する
     */
    class DDGIProbePass
    {
    public:
        bool Execute(ViewRenderContext& context);
        void Shutdown();

        RHI::BufferPtr GetResultBuffer(uint32_t frameIndex,
                                       uint32_t viewId = 0u,
                                       uint32_t viewportId = 0u) const;
        uint32_t GetResultCount(uint32_t frameIndex,
                                uint32_t viewId = 0u,
                                uint32_t viewportId = 0u) const;

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint32_t ResultCount = 0u;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::BufferPtr ResultBuffer;
            RHI::ResourceState ResultState = RHI::ResourceState::Undefined;
        };

        bool EnsurePipeline(ViewRenderContext& context);
        FrameResources* FindOrCreateFrameResources(ViewRenderContext& context,
                                                   uint32_t resultCount);
        void DisableAfterResourceFailure(const char* reason);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ComputeShader;
        RHI::PipelinePtr m_Pipeline;
        Container::VariableArray<FrameResources> m_FrameResources;
        bool m_bPipelineAttempted = false;
        bool m_bUnavailable = false;
        bool m_bFailureReported = false;
    };
} // namespace NorvesLib::Core::Rendering

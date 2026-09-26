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
        float Radiance[4] = {};
    };

    static_assert(sizeof(DDGIProbeRayQueryResult) == 32u);

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
        RHI::TexturePtr GetIrradianceAtlas(uint32_t frameIndex,
                                           uint32_t viewId = 0u,
                                           uint32_t viewportId = 0u) const;
        RHI::TexturePtr GetDistanceAtlas(uint32_t frameIndex,
                                         uint32_t viewId = 0u,
                                         uint32_t viewportId = 0u) const;
        uint32_t GetAtlasProbeCount(uint32_t frameIndex,
                                    uint32_t viewId = 0u,
                                    uint32_t viewportId = 0u) const;

    private:
        struct FrameResources
        {
            uint32_t FrameIndex = UINT32_MAX;
            uint32_t ViewId = UINT32_MAX;
            uint32_t ViewportId = UINT32_MAX;
            uint64_t FrameNumber = UINT64_MAX;
            uint32_t ResultCount = 0u;
            uint32_t AtlasProbeCount = 0u;
            bool bAtlasValid = false;
            RHI::DescriptorSetPtr DescriptorSet;
            RHI::DescriptorSetPtr BounceDescriptorSet;
            RHI::DescriptorSetPtr UpdateDescriptorSet;
            RHI::BufferPtr ParametersBuffer;
            RHI::BufferPtr BounceParametersBuffer;
            RHI::BufferPtr UpdateParametersBuffer;
            RHI::BufferPtr InstanceDataBuffer;
            RHI::BufferPtr ResultBuffer;
            RHI::TexturePtr IrradianceAtlas;
            RHI::TexturePtr DistanceAtlas;
            RHI::TexturePtr HistoryIrradianceAtlas;
            RHI::TexturePtr HistoryDistanceAtlas;
            float VolumeOrigin[3] = {};
            float ProbeSpacing[3] = {};
            uint32_t ProbeCounts[3] = {};
            // device address参照はdescriptorに保持されないため、frame slotのGPU完了まで保持する。
            Container::VariableArray<RHI::BufferPtr> GeometryBuffers;
            RHI::ResourceState ResultState = RHI::ResourceState::Undefined;
            RHI::ResourceState IrradianceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState DistanceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState HistoryIrradianceAtlasState = RHI::ResourceState::Undefined;
            RHI::ResourceState HistoryDistanceAtlasState = RHI::ResourceState::Undefined;
        };

        bool EnsurePipeline(ViewRenderContext& context);
        FrameResources* FindOrCreateFrameResources(ViewRenderContext& context,
                                                   uint32_t resultCount,
                                                   uint32_t instanceDataCount);
        void DisableAfterResourceFailure(const char* reason);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ComputeShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_IrradianceUpdateShader;
        RHI::PipelinePtr m_IrradianceUpdatePipeline;
        RHI::SamplerPtr m_AtlasSampler;
        RHI::TexturePtr m_DefaultIrradianceAtlas;
        RHI::TexturePtr m_DefaultDistanceAtlas;
        RHI::ResourceState m_DefaultIrradianceAtlasState = RHI::ResourceState::Undefined;
        RHI::ResourceState m_DefaultDistanceAtlasState = RHI::ResourceState::Undefined;
        Container::VariableArray<FrameResources> m_FrameResources;
        bool m_bPipelineAttempted = false;
        bool m_bUnavailable = false;
        bool m_bFailureReported = false;
    };
} // namespace NorvesLib::Core::Rendering

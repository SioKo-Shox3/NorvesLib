#pragma once

#include "Container/UnorderedMap.h"
#include "Rendering/IBoardProxySink.h"
#include "Rendering/View.h"
#include "Rendering/ViewRenderPlan.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 2D canvas view rendered into a transparent frame texture.
     *
     * Canvas owns exactly one full-rect viewport. RenderingCoordinator assigns
     * the shared orthographic camera; CanvasView does not access camera tables.
     */
    class CanvasView : public View, public IBoardProxySink
    {
    public:
        CanvasView();
        ~CanvasView() override;

        static BlendMode NormalizeBoardBlendMode(BlendMode blendMode);
        static RHI::BlendAttachmentDesc CreateBoardBlendAttachmentDesc(BlendMode blendMode);

        bool Initialize(const ViewSettings& settings) override;
        void Shutdown() override;
        void Render(ViewRenderContext& context) override;

        void UpdateBoardProxy(uint64_t componentId, const BoardProxy &proxy) override;
        void RemoveBoardProxy(uint64_t componentId) override;
        void RemoveStaleBoardProxies(const Container::UnorderedSet<uint64_t> &liveComponentIds) override;

        void SetLayerCompositeMode(uint32_t layerPriority, CanvasLayerCompositeMode mode);
        CanvasLayerCompositeMode GetLayerCompositeMode(uint32_t layerPriority) const;
        void SetLayerOpacity(uint32_t layerPriority, float opacity);
        float GetLayerOpacity(uint32_t layerPriority) const;

        void PrepareBoardDrawCommands(ViewportRenderPlan &viewportPlan, uint32_t packetCommandBase);
        void ReleaseRetainedBoardFrameResources();
        void SetBoardInstanceBatchingEnabled(bool bEnabled) { m_bBoardInstanceBatchingEnabled = bEnabled; }
        bool IsBoardInstanceBatchingEnabled() const { return m_bBoardInstanceBatchingEnabled; }

        const Container::VariableArray<BoardProxy> &GetBoardProxies() const { return m_BoardProxies; }
        const Container::VariableArray<DrawCommand> &GetBoardDrawCommands() const { return m_BoardDrawCommands; }
        const Container::VariableArray<GPUSceneInstanceData> &GetBoardInstanceData() const { return m_BoardInstanceData; }
        uint32_t GetRetainedBoardFrameResourceCount() const
        {
            return static_cast<uint32_t>(m_RetainedBoardFrameResources.size());
        }

    private:
        struct RetainedBoardFrameResources
        {
            RHI::TexturePtr OutputTexture;
            Container::VariableArray<RHI::TexturePtr> Textures;
            Container::VariableArray<RHI::RenderPassPtr> RenderPasses;
            Container::VariableArray<RHI::FramebufferPtr> Framebuffers;
            Container::VariableArray<RHI::TexturePtr> BoundTextures;
            Container::VariableArray<RHI::DescriptorSetPtr> DescriptorSets;
            Container::VariableArray<RHI::PipelinePtr> Pipelines;
            Container::VariableArray<RHI::BufferPtr> OpacityBuffers;
            Container::VariableArray<RHI::SamplerPtr> Samplers;
        };

        struct CanvasLayerCompositeConfig
        {
            CanvasLayerCompositeMode Mode = CanvasLayerCompositeMode::Inline;
            float Opacity = 1.0f;
        };

        void ClearBoardDrawCommands();
        void RetainBoardFrameResources(const RetainedBoardFrameResources &resources);
        bool EnsureBoardSharedResources(RHI::IDevice *device);
        CanvasLayerCompositeConfig GetLayerCompositeConfig(uint32_t layerPriority) const;
        static void AddRetainedTexture(RetainedBoardFrameResources &resources, RHI::TexturePtr texture);
        static void AddRetainedRenderPass(RetainedBoardFrameResources &resources, RHI::RenderPassPtr renderPass);
        static void AddRetainedFramebuffer(RetainedBoardFrameResources &resources, RHI::FramebufferPtr framebuffer);
        static void AddRetainedSampler(RetainedBoardFrameResources &resources, RHI::SamplerPtr sampler);
        static void AddRetainedOpacityBuffer(RetainedBoardFrameResources &resources, RHI::BufferPtr buffer);

        Container::VariableArray<BoardProxy> m_BoardProxies;
        Container::UnorderedMap<uint64_t, uint32_t> m_BoardProxyIndex;
        Container::UnorderedMap<uint64_t, uint64_t> m_BoardInsertionSequenceByComponentId;
        Container::UnorderedMap<uint32_t, CanvasLayerCompositeConfig> m_LayerCompositeConfigs;
        Container::VariableArray<DrawCommand> m_BoardDrawCommands;
        Container::VariableArray<GPUSceneInstanceData> m_BoardInstanceData;
        Container::VariableArray<RetainedBoardFrameResources> m_RetainedBoardFrameResources;
        uint64_t m_NextBoardInsertionSequence = 0;
        RHI::TexturePtr m_BoardFallbackWhiteTexture;
        RHI::SamplerPtr m_BoardPointClampSampler;
        bool m_bBoardInstanceBatchingEnabled = true;

        friend class CanvasBoardPass;
        friend class CanvasClearPass;
        friend class CanvasLayerBoardPass;
        friend class CanvasLayerOwnRTPass;
        friend class CanvasLayerCompositePass;
    };

} // namespace NorvesLib::Core::Rendering

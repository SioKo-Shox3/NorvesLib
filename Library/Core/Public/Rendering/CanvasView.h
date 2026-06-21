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

        void PrepareBoardDrawCommands(const ViewportRenderPlan &viewportPlan);
        void ReleaseRetainedBoardFrameResources();

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
            RHI::RenderPassPtr RenderPass;
            RHI::FramebufferPtr Framebuffer;
            RHI::DescriptorSetPtr DescriptorSet;
            Container::VariableArray<RHI::PipelinePtr> Pipelines;
        };

        void ClearBoardDrawCommands();
        void RetainBoardFrameResources(const RetainedBoardFrameResources &resources);

        Container::VariableArray<BoardProxy> m_BoardProxies;
        Container::UnorderedMap<uint64_t, uint32_t> m_BoardProxyIndex;
        Container::UnorderedMap<uint64_t, uint64_t> m_BoardInsertionSequenceByComponentId;
        Container::VariableArray<DrawCommand> m_BoardDrawCommands;
        Container::VariableArray<GPUSceneInstanceData> m_BoardInstanceData;
        Container::VariableArray<RetainedBoardFrameResources> m_RetainedBoardFrameResources;
        uint64_t m_NextBoardInsertionSequence = 0;

        friend class CanvasBoardPass;
    };

} // namespace NorvesLib::Core::Rendering

#pragma once

#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::RHI
{
    class IDevice;
    class ITexture;
} // namespace NorvesLib::RHI

namespace NorvesLib::Core::Rendering
{
    /// GBuffer へ重ねるパイプラインで、描く対象ごとに異なる設定
    struct GBufferOverlayPipelineDesc
    {
        RHI::ShaderPtr vertexShader;
        RHI::ShaderPtr pixelShader;
        // 空の配列なら頂点入力を持たない
        Container::VariableArray<RHI::VertexBindingDesc> vertexBindings;
        Container::VariableArray<RHI::VertexAttributeDesc> vertexAttributes;
        Container::VariableArray<RHI::DescriptorSetDesc> descriptorSetLayouts;
        RHI::CompareOp depthCompareOp = RHI::CompareOp::LessOrEqual;
        RHI::CullMode cullMode = RHI::CullMode::None;
        const char* debugName = "GBufferOverlayTargets";
        const char* failureLogPrefix = "GBUFFER_OVERLAY";
    };

    /// GBuffer の内容を保持しながら重ねて描くための描き先とパイプライン
    class GBufferOverlayTargets
    {
    public:
        /// gbuffer.frag の出力順に並んだ GBuffer の枚数
        static constexpr uint32_t kColorTargetCount = 5;

        // 同じ形式なら render pass とパイプラインを再利用する。device と desc は Reset まで同じ設定を使う
        bool Ensure(RHI::IDevice* device, const RHI::TexturePtr (&colors)[kColorTargetCount],
                    const RHI::TexturePtr& depth, const GBufferOverlayPipelineDesc& desc);
        const RHI::RenderPassPtr& GetRenderPass() const { return m_RenderPass; }
        const RHI::FramebufferPtr& GetFramebuffer() const { return m_Framebuffer; }
        const RHI::PipelinePtr& GetPipeline() const { return m_Pipeline; }
        void Reset();

    private:
        bool CreatePipeline(RHI::IDevice* device, const RHI::TexturePtr (&colors)[kColorTargetCount],
                            const RHI::TexturePtr& depth, const GBufferOverlayPipelineDesc& desc);

        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        // 今の framebuffer の描き先（作り直しの判定用。所有しない）
        const RHI::ITexture* m_FramebufferTargets[kColorTargetCount + 1] = {};
        // 今の render pass・パイプラインの描き先の形式
        RHI::Format m_TargetFormats[kColorTargetCount + 1] = {};
    };

} // namespace NorvesLib::Core::Rendering

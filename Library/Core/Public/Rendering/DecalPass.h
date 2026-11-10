#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    struct CameraViewConstants;
    struct DecalScene;
    class FrameResourceLane;

    /**
     * @brief 箱の裏面を画面へ投影し、深度から復元した位置にデカールを重ねる遅延描画パス
     *
     * GBuffer の Albedo と Normal の RGB だけを混ぜ、各 A と Material を保つ。箱の局所 -Z 方向は位置の
     * 範囲判定に使う。受け手の印は法線のコピーから読むが、法線の向きによる投影の制限はできない。
     * 定数・インスタンス・記述子セットはフレームの持ち主から借りる。
     */
    class DecalPass : public IViewPass, public IRenderGraphPass
    {
    public:
        DecalPass() = default;
        ~DecalPass() override;

        const char *GetName() const override { return "DecalPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        /**
         * @brief デカールの render pass の開始から終了までを記録する
         *
         * Initialize 後に呼ぶ。3 枚は同じ大きさで、それぞれ別のテクスチャとする。呼び出す前に Albedo と Normal を
         * RenderTarget、Depth を ShaderResource にする。Normal は TransferSrc の用途も持つ。終了後の色添付は ShaderResource になる。
         * cameraConstants は描き先の深度に使ったカメラの値とし、frameResources は記録中の口を渡す。
         * 一覧が空なら何も記録せず true、準備に失敗したら false を返す。
         */
        bool RecordDecals(const RHI::TexturePtr &albedoTarget, const RHI::TexturePtr &normalTarget,
                          const RHI::TexturePtr &depthTexture,
                          RHI::ICommandList &commandList, FrameResourceLane &frameResources, const DecalScene &scene,
                          const CameraViewConstants &cameraConstants);

    private:
        static constexpr uint32_t kColorTargetCount = 2;

        bool EnsureTargets(const RHI::TexturePtr (&colorTargets)[kColorTargetCount]);
        bool CreatePipeline(const RHI::TexturePtr (&colorTargets)[kColorTargetCount]);

        RHI::IDevice *m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_PointSampler;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::TexturePtr m_NormalCopy;
        RHI::ResourceState m_NormalCopyState = RHI::ResourceState::Undefined;
        RGResourceHandle m_ColorHandles[kColorTargetCount];
        RGResourceHandle m_DepthHandle;
        const RHI::ITexture *m_FramebufferTargets[kColorTargetCount] = {};
        uint32_t m_LoggedDecalCount = 0;
    };
} // namespace NorvesLib::Core::Rendering

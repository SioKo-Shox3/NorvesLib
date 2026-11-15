#pragma once

#include "Rendering/DynamicUniformAllocator.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::Core::Rendering
{
    class RenderWorld;

    /** @brief 隠れた主人公に重ねる色と不透明度。色は線形 RGB。 */
    struct HeroSilhouetteSettings
    {
        float Color[3] = {0.16f, 0.21f, 0.25f};
        float Opacity = 0.3f;
    };

    /**
     * @brief 照明後のシーンに、遮蔽された主人公のスキンを重ねる。
     * @note 自分の手足が胴の手前にある画素にも色が載る。
     * 可視バッファの事前スキンとの丸めの差を吸収するため深度を手前に寄せる。
     * その量より主人公に近い遮蔽物ではシルエットが出ない。
     */
    class HeroSilhouettePass final : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit HeroSilhouettePass(const HeroSilhouetteSettings& settings = {});
        ~HeroSilhouettePass() override;

        const char* GetName() const override;
        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;
        bool DeclaresAllLayoutChanges() const override { return true; }

    private:
        bool PrepareTargets(RenderGraphResources& resources);

        HeroSilhouetteSettings m_Settings;
        RHI::IDevice* m_Device = nullptr;
        DynamicUniformAllocator m_UniformAllocator;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        const RHI::ITexture* m_FramebufferColor = nullptr;
        const RHI::ITexture* m_FramebufferDepth = nullptr;
        RHI::Format m_ColorFormat = RHI::Format::UNKNOWN;
        RHI::Format m_DepthFormat = RHI::Format::UNKNOWN;
        RGTextureHandle m_ColorHandle;
        RGTextureHandle m_DepthHandle;
        bool m_bDeclared = false;
    };

    /** @brief 主の SceneView の照明直後にシルエットを登録する。 */
    bool RegisterHeroSilhouetteExtension(RenderWorld& world);
    /** @brief シルエットの拡張を登録解除する。 */
    bool UnregisterHeroSilhouetteExtension(RenderWorld& world);
}

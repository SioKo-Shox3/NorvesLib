#pragma once

#include "IViewPass.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/VolumetricFog.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class VolumetricsPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~VolumetricsPass() override;

        const char* GetName() const override { return "VolumetricsPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        bool PrepareResources(const RHI::TexturePtr& sceneColorTexture);
        void EnqueueEmptyNativePass(ViewRenderContext& context,
                                    const RHI::TexturePtr& sceneColorTexture) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_SceneDepthSampler;
        RHI::SamplerPtr m_SkyRadianceSampler;
        // 空の透過率 LUT（横が天頂角の余弦）を引く線形・端で止めるサンプラー
        RHI::SamplerPtr m_SkyTransmittanceSampler;
        // 共有の記述子セットのレイアウト（定数と記述子セットは Execute ごとにフレームの持ち主から借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）で作った使い回さない定数と記述子セット。
        // 提出の完了を知る手段が無いので、パスを手放すまで持つ
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        RGResourceHandle m_SkyAtmosphereTransmittanceHandle;
        RGResourceHandle m_CascadedShadowMapHandle;
        VolumetricFogParameters m_FogParameters;
        // このフレームの空気遠近（空が無効なら bEnabled を偽にしてある）と空のパラメータ
        AerialPerspectiveParameters m_AerialPerspective;
        SkyAtmosphereParameters m_SkyParameters;

        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // namespace NorvesLib::Core::Rendering

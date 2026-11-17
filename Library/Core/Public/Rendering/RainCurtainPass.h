// 遠景の雨のカーテンを全画面の三角形で HDR のシーンの色へ重ねるパスを定義する。
#pragma once

#include "IViewPass.h"
#include "Rendering/EnvironmentParameters.h"
#include "Rendering/RainModel.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class RainCurtainPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~RainCurtainPass() override;

        const char* GetName() const override { return "RainCurtainPass"; }

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
        // レイアウトは共有し、定数と記述子セットは Execute ごとに借りる。
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        // 持ち主の無い文脈では提出済みの資源を使い回さず、パスを手放すまで保持する。
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        // Declare で確定した、このフレームのスナップショットの値。
        EnvironmentParameters m_Environment;
        RainTuning m_RainTuning;
        RainCurtainEvaluation m_Curtain;
        WindParameters m_Wind;

        uint32_t m_CurrentWidth = 0u;
        uint32_t m_CurrentHeight = 0u;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // 名前空間 NorvesLib::Core::Rendering

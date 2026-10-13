#pragma once

#include "IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    class ToneMappingPass;

    struct VignetteSettings
    {
        float Intensity = 0.3f;
        float Radius = 0.8f;
        float Softness = 0.5f;
        bool bEnabled = true;
    };

    class VignettePass : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit VignettePass(const VignetteSettings& settings = VignetteSettings{});
        ~VignettePass() override;

        const char* GetName() const override { return "VignettePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;
        // 監査済み: Execute は全画面のパスを積むだけで自分でバリアを張らず、出力は render pass の finalState
        // （宣言の最終の状態 ShaderResource）へ移る。準備が整ったフレームだけ、計画した区間で Parallel に記録する。
        bool DeclaresAllLayoutChanges() const override;
        RGRecordPolicy GetRecordPolicy() const override;
        // 区間ごとの記録の前に、render pass・framebuffer・パイプラインの作り直しを直列で済ませる。
        void PrepareParallelRecord(RenderGraphResources& resources, ViewRenderContext& context) override;

        void SetInputPass(const ToneMappingPass* inputPass) { m_InputPass = inputPass; }
        RGResourceHandle GetToneMappedColorHandle() const { return m_OutputHandle; }
        RGTextureHandle GetToneMappedColorTextureHandle() const { return m_OutputTextureHandle; }

        void SetEnabled(bool bEnabled) { m_Settings.bEnabled = bEnabled; }
        void SetIntensity(float intensity) { m_Settings.Intensity = intensity; }
        void SetRadius(float radius) { m_Settings.Radius = radius; }
        void SetSoftness(float softness) { m_Settings.Softness = softness; }
        const VignetteSettings& GetSettings() const { return m_Settings; }

    private:
        bool PrepareResources(uint32_t width,
                              uint32_t height,
                              const RHI::TexturePtr& outputTexture,
                              bool bUseRenderGraphInitialState);
        void ExecuteWithInput(ViewRenderContext& context,
                              const RHI::TexturePtr& inputTexture,
                              bool bRegisterLegacyBridge);
        bool EnqueueEmptyNativePass(ViewRenderContext& context) const;

        VignetteSettings m_Settings;

        RHI::TexturePtr m_OutputTexture;
        RGResourceHandle m_InputToneMappedHandle;
        RGTextureHandle m_OutputTextureHandle;
        RGResourceHandle m_OutputHandle;

        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_LinearSampler;
        // 共有の記述子セットのレイアウト（定数と記述子セットは Execute ごとにフレームの持ち主から借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        // 持ち主の無い文脈（RenderingCoordinator を通さない検査）で作った使い回さない定数と記述子セット。
        // 提出の完了を知る手段が無いので、パスを手放すまで持つ
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;

        RHI::IDevice* m_Device = nullptr;
        const ToneMappingPass* m_InputPass = nullptr;

        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        bool m_bLegacyInputFallbackActive = false;
        bool m_bRenderPassUsesRenderGraphInitialState = false;
        RHI::ITexture* m_FramebufferOutputTexture = nullptr;
    };

} // namespace NorvesLib::Core::Rendering

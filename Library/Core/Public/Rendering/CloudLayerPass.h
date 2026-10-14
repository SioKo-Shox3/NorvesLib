#pragma once

#include "IViewPass.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Math/Vector3.h"
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /**
     * @brief 空の画素に雲の層を描くパス
     *
     * SSR の後・フォグ（VolumetricsPass）の前に置く。シーンの雲の層（SceneProxy::CloudLayer）と空が有効なフレームだけ
     * グラフに入り、深度が遠方面の画素で、惑星の球殻にした雲の層を光線で進めて、散乱光（事前露出済み）と
     * 1 - 透過率をシーンの色へ前乗算のアルファで重ねる（空と太陽の円盤は透過率の分だけ残る）。
     * 密度は起動後、最初に雲を描くフレームで作る 3D ノイズ（基本の形の Perlin-Worley・Worley と細部の Worley）から求める。
     * 無効なフレームは何も宣言しないので、雲の無いシーンの描画は変わらない。
     */
    class CloudLayerPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~CloudLayerPass() override;

        const char* GetName() const override { return "CloudLayerPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

    private:
        bool EnsureNoiseTextures();
        bool PrepareResources(const RHI::TexturePtr& sceneColorTexture);
        void EnqueueEmptyNativePass(ViewRenderContext& context,
                                    const RHI::TexturePtr& sceneColorTexture) const;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_SceneDepthSampler;
        RHI::SamplerPtr m_SkyRadianceSampler;
        // 3D ノイズを 3 方向とも繰り返して線形に引くサンプラー
        RHI::SamplerPtr m_NoiseSampler;
        // 基本の形（R=Perlin-Worley、G・B・A=周波数の違う Worley の fbm）と細部（R・G・B=Worley の fbm）の 3D ノイズ
        RHI::TexturePtr m_ShapeNoiseTexture;
        RHI::TexturePtr m_DetailNoiseTexture;
        bool m_bNoiseGenerationFailed = false;
        // 共有の記述子セットのレイアウト（定数と記述子セットは Execute ごとにフレームの持ち主から借りる）
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        // 持ち主の無い文脈で作った使い回さない定数と記述子セット。提出の完了を知る手段が無いので、パスを手放すまで持つ
        Container::VariableArray<RHI::BufferPtr> m_UnownedBuffers;
        Container::VariableArray<RHI::DescriptorSetPtr> m_UnownedDescriptorSets;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        // このフレームの雲の層（空が無効なら bEnabled を偽にしてある）と空のパラメータ
        CloudLayerParameters m_CloudLayer;
        SkyAtmosphereParameters m_SkyParameters;
        // 太陽の地表の照度（lux、RGB）。空のパラメータが変わったときだけ求め直す
        SkyAtmosphereParameters m_SunIlluminanceSky;
        Math::Vector3 m_SunIlluminance = Math::Vector3::Zero;
        bool m_bSunIlluminanceValid = false;

        uint32_t m_CurrentWidth = 0;
        uint32_t m_CurrentHeight = 0;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // namespace NorvesLib::Core::Rendering

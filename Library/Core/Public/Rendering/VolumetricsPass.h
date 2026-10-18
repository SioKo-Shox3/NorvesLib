#pragma once

#include "IViewPass.h"
#include "Debug/DebugConfig.h"
#include "Rendering/FrameCaptureTypes.h"
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

#if NORVES_ENABLE_STATS
    /**
     * @brief 撮影の大気の書き出し（--atmosphere-dump）: 決めた視線の高さフォグと空気遠近を、VolumetricsPass と同じ式・同じ定数で評価する
     *
     * volumetrics.frag と共有の include（Common/VolumetricsCommon.glsl）を volumetrics_probe.frag で、カメラの位置から
     * 決めた視線ごとに評価し、事前露出の前の値を RGBA32F の 1 枚（横 = 視線、縦 = FrameCaptureAtmosphereProbeRows の値の種類）へ書く。
     * 1 枚はグラフの外へ出し（RenderGraphResourceNames::CaptureAtmosphereProbe）、撮影の読み出しが同じフレームから写す。
     * シーンの色へは書かない。視線（GetLastInfo().Rays）:
     *   Direction: 太陽の方位からの方位差 kAzimuthOffsetsDegrees × 仰角 kElevationsDegrees の向きに、距離 kDistancesMeters まで
     *   Ground: 方位差 kAzimuthOffsetsDegrees の向きに、カメラから GroundEyeHeightMeters 下の水平面の、水平距離 kDistancesMeters の点まで
     * 行（どれも事前露出の前。放射輝度は cd/m²）:
     *   0: 高さフォグの透過率・不透明度・空気遠近の RGB の平均の透過率・合成のアルファ
     *   1: 高さフォグの色（不透明度を掛ける前）、a = 空の放射輝度 LUT を使ったか
     *   2: 高さフォグの太陽の単一散乱（影つき）、a = 太陽の単一散乱が有効か
     *   3: 空気遠近の RGB の透過率、a = 空気遠近が有効か
     *   4: 空気遠近の散乱光
     *   5: 空の放射輝度 LUT のこの向きの値
     *   6: volumetrics.frag がシーンの色へ足す光（高さフォグ + 空気遠近 × 高さフォグの透過率）、a = 合成のアルファ
     */
    class AtmosphereProbePass : public IViewPass, public IRenderGraphPass
    {
    public:
        /// Ground の視線の地表を、カメラからこの高さ（m）だけ下の水平面とみなす（荒野の視点の目の高さ）
        static constexpr float GroundEyeHeightMeters = 1.7f;

        /** @brief 環境変数 NORVES_ATMOSPHERE_DUMP が 1 か（Game の --atmosphere-dump が描画の初期化の前に置く） */
        static bool IsRequestedByEnvironment();

        ~AtmosphereProbePass() override;

        const char* GetName() const override { return "AtmosphereProbePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief このフレームに書いた 1 枚の評価に使った値（書かなかったフレームは bValid が false） */
        const FrameCaptureAtmosphereInfo& GetLastInfo() const { return m_LastInfo; }

    private:
        /** @brief 太陽の方位から視線の表を作る */
        void BuildRays(const SkyAtmosphereParameters& sky);

        RHI::IDevice* m_Device = nullptr;
        bool m_bInitialized = false;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_PointSampler;
        RHI::SamplerPtr m_SkyRadianceSampler;
        RHI::SamplerPtr m_SkyTransmittanceSampler;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferTarget = nullptr;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_TargetHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        RGResourceHandle m_SkyAtmosphereTransmittanceHandle;
        RGResourceHandle m_CascadedShadowMapHandle;
        VolumetricFogParameters m_FogParameters;
        AerialPerspectiveParameters m_AerialPerspective;
        SkyAtmosphereParameters m_SkyParameters;
        FrameCaptureAtmosphereInfo m_LastInfo;
    };
#endif
} // namespace NorvesLib::Core::Rendering

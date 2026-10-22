#pragma once

#include "IViewPass.h"
#include "Rendering/SkyAtmosphere.h"
#include "Rendering/FrameCaptureTypes.h"
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
        /** @brief 空のパラメータが変わったときだけ太陽の地表の照度を求め直す */
        void UpdateSunIlluminance();
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

    /**
     * @brief 雲の 3D ノイズの texel（RGBA8。x が最も速く、z が最も遅く並ぶ）を作る
     *
     * CloudLayerPass が GPU へ上げる値と同じ関数で、CPU だけで決まる（同じ値になる）。outShape は一辺 64・outDetail は一辺 32。
     */
    void BuildCloudNoiseTexels(Container::VariableArray<uint8_t>& outShape, Container::VariableArray<uint8_t>& outDetail);

    /** @brief texel の並びの FNV-1a（64 bit）。CloudLayerPass のログ（CLOUD_NOISE）と雲の書き出しが同じ値を残す */
    uint64_t HashCloudNoiseTexels(const Container::VariableArray<uint8_t>& texels);

#if NORVES_ENABLE_STATS
    /**
     * @brief 撮影の雲の書き出し（--cloud-dump）: 決めた画素の視線の雲を、CloudLayerPass と同じ式・同じ定数で評価する
     *
     * cloud_layer.frag と共有の include（Common/CloudLayerCommon.glsl）を cloud_layer_probe.frag で、CloudLayerPass と同じ
     * 規則で詰めた定数（同じフレームのカメラ・時刻・フレームの番号）と、画面の決めた画素の中心（横 kRayColumns × 縦 kRayRows の
     * 格子。GetLastInfo().RayPixels）で評価し、RGBA32F の 1 枚（横 = 視線、縦 = FrameCaptureCloudProbeRows の値の種類）へ書く。
     * 歩の端数はその画素・そのフレームの雑音で、cloud_layer.frag がその画素で進めた視線と同じになる。1 枚はグラフの外へ出し
     * （RenderGraphResourceNames::CaptureCloudProbe）、撮影の読み出しが同じフレームから写す。シーンの色へは書かない。
     * 行（どれも事前露出の前。放射輝度は cd/m²、距離は m）:
     *   0: 雲の透過率（打ち切りの前）・cloud_layer.frag が重ねる割合（空でない画素・雲の無い視線は 0）・打ち切りの距離へ溶かす割合・進めた歩数
     *   1: 雲の散乱光、a = 光を返した所の距離の重み付きの平均
     *   2: cloud_layer.frag の雲の放射輝度（霞と溶かす割合を掛けた後）、a = 霞の向こうに残る割合
     *   3: 視線の向き（ワールド）、a = 歩の端数
     *   4: 環境光に使った上の空の平均の放射輝度、a = 雲の層を進めた区間の始め
     *   5: 地面の照り返しの放射輝度、a = 区間の終わり
     *   6: 視線の向きの空の放射輝度 LUT の値（雲の後ろの空）、a = その画素の深度
     *   7: 視線の光学的深さ・決めた歩数・1 歩の長さ、a = 雲の層を進めたか（1）+ 向きを求められたか（2）
     */
    class CloudProbePass : public IViewPass, public IRenderGraphPass
    {
    public:
        /// 視線を置く画面の格子（横・縦の数）
        static constexpr uint32_t kRayColumns = 16u;
        static constexpr uint32_t kRayRows = 9u;

        /** @brief 環境変数 NORVES_CLOUD_DUMP が 1 か（Game の --cloud-dump が描画の初期化の前に置く） */
        static bool IsRequestedByEnvironment();

        ~CloudProbePass() override;

        const char* GetName() const override { return "CloudProbePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief このフレームに書いた 1 枚の評価に使った値（書かなかったフレームは bValid が false） */
        const FrameCaptureCloudInfo& GetLastInfo() const { return m_LastInfo; }

    private:
        bool EnsureNoiseTextures();

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::SamplerPtr m_SceneDepthSampler;
        RHI::SamplerPtr m_SkyRadianceSampler;
        RHI::SamplerPtr m_NoiseSampler;
        // CloudLayerPass と同じ関数で作った 3D ノイズ（パスごとに持つ。値は同じ）
        RHI::TexturePtr m_ShapeNoiseTexture;
        RHI::TexturePtr m_DetailNoiseTexture;
        bool m_bNoiseGenerationFailed = false;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
        RHI::RenderPassPtr m_RenderPass;
        RHI::FramebufferPtr m_Framebuffer;
        RHI::ITexture* m_FramebufferTarget = nullptr;
        RHI::PipelinePtr m_Pipeline;

        RGResourceHandle m_TargetHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_SkyAtmosphereRadianceHandle;
        CloudLayerParameters m_CloudLayer;
        SkyAtmosphereParameters m_SkyParameters;
        SkyAtmosphereParameters m_SunIlluminanceSky;
        Math::Vector3 m_SunIlluminance = Math::Vector3::Zero;
        bool m_bSunIlluminanceValid = false;
        FrameCaptureCloudInfo m_LastInfo;
    };
#endif
} // namespace NorvesLib::Core::Rendering

// ラスタの動きぼけ。画面velocityにシャッター時間/フレーム長を掛け、SceneColorをその場でぼかす。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/SceneProxy.h"
#include "RHI/RHITypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief シャッターの間に動く長さの上限（画素）。tileの一辺もこの長さにする。 */
    inline constexpr float MotionBlurMaxLengthPixels = 32.0f;

    /** @brief 速度の最大を求めるtileの一辺（画素）。隣の3×3 tileで上限の長さまで届く。 */
    inline constexpr uint32_t MotionBlurTileSize = 32u;

    /**
     * @brief ラスタの動きぼけのシャッター
     *
     * 画面velocity（currentUV - previousUV）は前後の変換の間隔（フレーム長）の動きなので、
     * シャッター時間/フレーム長を掛けてシャッターの間の動きにする。シャッター区間はPTの連番の1フレームと
     * 同じく、前後の間隔の終わり側（現在の変換で終わる区間）に置く。シャッター時間が0なら働かない。
     */
    struct MotionBlurSettings
    {
        /** @brief シャッター時間（秒）。0は動きぼけなし。 */
        float ShutterDuration = 0.0f;
        /** @brief 前後の変換の間隔（秒）。 */
        float FrameDuration = 1.0f / 24.0f;
    };

    /** @brief シャッター時間/フレーム長（0〜1）。無効な値や0のシャッターは0。 */
    inline float ComputeMotionBlurShutterFraction(const MotionBlurSettings& settings)
    {
        if (!std::isfinite(settings.ShutterDuration) || !std::isfinite(settings.FrameDuration) ||
            settings.ShutterDuration <= 0.0f || settings.FrameDuration <= 0.0f)
        {
            return 0.0f;
        }
        return std::clamp(settings.ShutterDuration / settings.FrameDuration, 0.0f, 1.0f);
    }

    /**
     * @brief 画面velocityから、シャッターの間の動きでSceneColorをぼかすpass
     *
     * 各画素の動き（画素）は画面velocity × 画像の寸法 × シャッター時間/フレーム長。velocityの無い空の
     * 画素（深度1）は、無限遠の方向を前のカメラへ投影してカメラの動きから求める。
     * 1. tileごとに最も長い動きを求める。
     * 2. 各画素で、隣の3×3 tileの最も長い動きの向きと自分の動きの向きへ、シャッターの区間を等分した時刻の
     *    標本を取る。時刻τに中心の画素を覆う面は、現在の像で中心からτ × 動きだけ進んだ位置にある。
     *    最も長い動きの向きの標本の面がその時刻に中心を覆い、中心より手前なら前景として使う。そうでなければ
     *    自分の動きの向きの標本（同じ動きの面）を使い、どちらの面も中心を覆わない（手前の物体に隠れていた
     *    面が現れる）時刻は、奥の側の標本で埋める。
     * 3. 結果をSceneColorへ書き戻す。
     * シャッター時間が0、前のカメラが無い、検証表示中のいずれかなら何もしない。
     */
    class MotionBlurPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~MotionBlurPass() override;

        const char* GetName() const override { return "MotionBlurPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief シャッターを設定する（初期化中か、RenderThreadの外で描画が止まっている間に呼ぶ）。 */
        void SetSettings(const MotionBlurSettings& settings) { m_Settings = settings; }
        const MotionBlurSettings& GetSettings() const { return m_Settings; }

        /**
         * @brief sceneColor（RenderTarget状態）へ動きぼけを掛け、ShaderResource状態で返す
         *
         * sceneDepthはShaderResource状態の深度（0が手前、1が空）。R32_FLOATの色textureでもよい。
         * velocityはShaderResource状態の画面velocity（RG、currentUV - previousUV）。
         * 動きぼけが無効なときや資源を作れないときはfalseを返し、sceneColorは変えずに
         * ShaderResource状態へ移す。
         */
        bool Apply(ViewRenderContext& context,
                   const RHI::TexturePtr& sceneColor,
                   const RHI::TexturePtr& sceneDepth,
                   const RHI::TexturePtr& velocity);

    private:
        bool PrepareResources(const RHI::TexturePtr& sceneColor);

        MotionBlurSettings m_Settings;
        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_TileMaxShader;
        RHI::ShaderPtr m_GatherShader;
        RHI::ShaderPtr m_ResolveShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::SamplerPtr m_PointSampler;
        RHI::DescriptorSetPtr m_TileMaxDescriptorSet;
        RHI::DescriptorSetPtr m_GatherDescriptorSet;
        RHI::DescriptorSetPtr m_ResolveDescriptorSet;

        RHI::TexturePtr m_TileMaxTexture;
        RHI::RenderPassPtr m_TileMaxRenderPass;
        RHI::FramebufferPtr m_TileMaxFramebuffer;
        RHI::PipelinePtr m_TileMaxPipeline;
        RHI::TexturePtr m_GatherTexture;
        RHI::RenderPassPtr m_GatherRenderPass;
        RHI::FramebufferPtr m_GatherFramebuffer;
        RHI::PipelinePtr m_GatherPipeline;
        RHI::RenderPassPtr m_ResolveRenderPass;
        RHI::FramebufferPtr m_ResolveFramebuffer;
        RHI::PipelinePtr m_ResolvePipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;
        RGResourceHandle m_VelocityHandle;

        uint32_t m_CurrentWidth = 0u;
        uint32_t m_CurrentHeight = 0u;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // namespace NorvesLib::Core::Rendering

// ラスタの被写界深度。深度からPTと同じ薄レンズの式でCoCを求め、SceneColorをその場でぼかす。
#pragma once

#include "Rendering/IViewPass.h"
#include "Rendering/PathTracingCamera.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/SceneProxy.h"
#include "RHI/RHITypes.h"

#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 散らすCoCの半径の上限（入力画像の画素）。gatherの探索範囲もこれで決まる。 */
    inline constexpr float DepthOfFieldMaxCocRadiusPixels = 16.0f;

    /**
     * @brief ラスタの被写界深度の薄レンズの値
     *
     * 焦点距離は撮像面高24 mmとFOVから求め（PTと同じ）、CoCの直径（出力画素）は
     * CocScale × |z - FocusDistance| / z（zは視線方向の深度）。PTはピントを合わせた像面の倍率で
     * 画角を1 - f/ピント距離倍に狭めるため、ラスタも同じ倍率で最終画像を拡大する（FilmScale）。
     */
    struct DepthOfFieldLens
    {
        bool bEnabled = false;
        float FocalLength = 0.0f;
        float FocusDistance = 0.0f;
        float CocScale = 0.0f;
        float FilmScale = 1.0f;
    };

    /**
     * @brief カメラの絞り・ピント距離から薄レンズの値を作る
     *
     * ピント距離が0（ピンホール）・焦点距離以下、f値が正でない、透視投影でない、画像の高さが0の
     * いずれかなら無効（被写界深度を掛けない）。
     */
    inline DepthOfFieldLens BuildDepthOfFieldLens(const CameraProxy& camera, uint32_t imageHeight)
    {
        DepthOfFieldLens lens;
        const float focalLength = PathTracingCameraDetail::FocalLength(camera.FieldOfView);
        if (imageHeight == 0u || camera.Projection != ProjectionType::Perspective ||
            focalLength <= 0.0f || !std::isfinite(camera.FocusDistance) ||
            camera.FocusDistance <= focalLength || !std::isfinite(camera.Aperture) ||
            camera.Aperture <= 0.0f)
        {
            return lens;
        }
        const float diameter = focalLength / camera.Aperture;
        const float cocScale = diameter * focalLength / (camera.FocusDistance - focalLength) *
                               static_cast<float>(imageHeight) /
                               PathTracingCameraDetail::SensorHeight;
        if (!std::isfinite(cocScale) || cocScale <= 0.0f)
        {
            return lens;
        }
        lens.bEnabled = true;
        lens.FocalLength = focalLength;
        lens.FocusDistance = camera.FocusDistance;
        lens.CocScale = cocScale;
        lens.FilmScale = 1.0f - focalLength / camera.FocusDistance;
        return lens;
    }

    /** @brief 視線方向の深度zの点のCoCの直径（出力画素）。ComputePathTracingCocPixelsと同じ値になる。 */
    inline float ComputeDepthOfFieldCocPixels(const DepthOfFieldLens& lens, float viewDepth)
    {
        if (!lens.bEnabled || !std::isfinite(viewDepth) || viewDepth <= lens.FocalLength)
        {
            return 0.0f;
        }
        return lens.CocScale * std::abs(viewDepth - lens.FocusDistance) / viewDepth;
    }

    /**
     * @brief 深度からCoCを求め、前景と背景の2層のgatherでSceneColorをぼかすpass
     *
     * 背景（ピント面より奥）は各画素のCoCの円板で散らした寄与を集め、中心より奥の画素の広がりは
     * 中心のCoCまでに抑える（ピントの合った手前の物体へ奥のぼけが乗らない）。前景（ピント面より手前）は
     * 散らした円板の被覆の和を不透明度として背景の上に重ねる（前ボケの縁の半透明）。最後にPTと同じ
     * 倍率で拡大してSceneColorへ書き戻す。カメラのピント距離が0（ピンホール）か、検証表示中は何もしない。
     */
    class DepthOfFieldPass : public IViewPass, public IRenderGraphPass
    {
    public:
        ~DepthOfFieldPass() override;

        const char* GetName() const override { return "DepthOfFieldPass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;
        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /**
         * @brief sceneColor（RenderTarget状態）へ被写界深度を掛け、ShaderResource状態で返す
         *
         * sceneDepthはShaderResource状態の深度（0が手前、1が空）。R32_FLOATの色textureでもよい。
         * 被写界深度が無効なときや資源を作れないときはfalseを返し、sceneColorは変えずに
         * ShaderResource状態へ移す。
         */
        bool Apply(ViewRenderContext& context,
                   const RHI::TexturePtr& sceneColor,
                   const RHI::TexturePtr& sceneDepth);

    private:
        bool PrepareResources(const RHI::TexturePtr& sceneColor);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_GatherShader;
        RHI::ShaderPtr m_ResampleShader;
        RHI::BufferPtr m_ParamsBuffer;
        RHI::SamplerPtr m_PointSampler;
        RHI::SamplerPtr m_LinearSampler;
        RHI::DescriptorSetPtr m_GatherDescriptorSet;
        RHI::DescriptorSetPtr m_ResampleDescriptorSet;

        RHI::TexturePtr m_GatherTexture;
        RHI::RenderPassPtr m_GatherRenderPass;
        RHI::FramebufferPtr m_GatherFramebuffer;
        RHI::PipelinePtr m_GatherPipeline;
        RHI::RenderPassPtr m_ResampleRenderPass;
        RHI::FramebufferPtr m_ResampleFramebuffer;
        RHI::PipelinePtr m_ResamplePipeline;

        RGResourceHandle m_SceneColorHandle;
        RGResourceHandle m_SceneDepthHandle;

        uint32_t m_CurrentWidth = 0u;
        uint32_t m_CurrentHeight = 0u;
        RHI::Format m_CurrentFormat = RHI::Format::UNKNOWN;
        RHI::ITexture* m_FramebufferSceneColorTexture = nullptr;
    };
} // namespace NorvesLib::Core::Rendering

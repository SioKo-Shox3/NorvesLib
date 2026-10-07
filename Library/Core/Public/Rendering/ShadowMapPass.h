#pragma once

#include "IViewPass.h"
#include "SceneRenderer.h"
#include "DynamicUniformAllocator.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/ShadowMethod.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"
#include "Container/PointerTypes.h"
#include <cstdint>

using namespace NorvesLib::Core::Container;

namespace NorvesLib::Core::Rendering
{
    class SceneView;
    struct SkinnedRenderPathContractTestAccess;

    /**
     * @brief シャドウマップパス設定
     */
    struct ShadowMapPassSettings
    {
        /** @brief シャドウマップ解像度（正方形） */
        uint32_t Resolution = 2048;

        /** @brief 深度フォーマット */
        RHI::Format DepthFormat = RHI::Format::D32_FLOAT;

        /** @brief 正射影の半サイズ */
        float OrthoSize = 20.0f;

        /** @brief ライトカメラのニアプレーン */
        float NearPlane = 0.1f;

        /** @brief ライトカメラのファープレーン */
        float FarPlane = 50.0f;

        /**
         * @brief 方向光の影を受ける最大のビュー深度（m）
         *
         * CSMの4カスケードはカメラのnearからこの距離（カメラのfarの方が近ければfar）までを
         * 分割する。これより遠い面は影を受けず、最後のカスケードの奥の端で影を薄めて消す。
         */
        float MaxShadowDistance = 80.0f;

        /** @brief 点光源のキューブシャドウの1面の解像度（正方形） */
        uint32_t PointShadowResolution = 512;
    };

    /**
     * @brief シャドウマップパス（深度描画 - ライト視点）
     *
     * ディレクショナルライトの視点からシーンの深度のみを描画し、
     * シャドウマップテクスチャとして出力します。CSMの4カスケードへは影を落とす描画コマンドと、
     * 影を落とすMegaGeometry（LOD0。カスケードの影の地図にかかる物だけ）を描きます。
     *
     * GBufferPassの前に実行され、標準経路では RenderGraph named resource として公開した
     * シャドウマップをLightingPassが参照して影を計算します。
     * SharedResourceRegistry は legacy/fallback bridge の互換経路でのみ使用します。
     *
     * legacy bridge出力:
     * - "ShadowMap" : 4層の深度配列テクスチャ (D32_FLOAT)
     *
     * 影を落とす点光源（FramePacket::PointShadowsで選ばれた最大4灯）があるフレームだけ、
     * 各灯の6面へ光源からの線形距離（範囲で割った0〜1）を描いたキューブ配列の深度テクスチャを
     * RenderGraph named resource "PointShadowCubeMap" として公開する。点光源が無いフレームは
     * キューブへ何も描かず、公開もしない。
     */
    class ShadowMapPass : public IViewPass, public IRenderGraphPass
    {
    public:
        /**
         * @brief コンストラクタ
         * @param settings シャドウマップパス設定
         */
        explicit ShadowMapPass(const ShadowMapPassSettings &settings = ShadowMapPassSettings{});

        /**
         * @brief デストラクタ
         */
        ~ShadowMapPass() override;

        // ========================================
        // IViewPass実装
        // ========================================

        const char *GetName() const override { return "ShadowMapPass"; }

        bool Initialize(ViewRenderContext &context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext &context) override;
        void Execute(ViewRenderContext &context) override;
        void Declare(RenderGraphBuilder &builder) override;
        void Execute(RenderGraphResources &resources, ViewRenderContext &context) override;

        // ========================================
        // SceneView連携
        // ========================================

        /**
         * @brief 描画対象のSceneViewを設定します
         * @param sceneView SceneView
         */
        void SetSceneView(SceneView *sceneView) { m_SceneView = sceneView; }

        /**
         * @brief SceneRendererを設定します
         * @param renderer SceneRenderer
         */
        void SetSceneRenderer(SceneRenderer *renderer) { m_SceneRenderer = renderer; }

        /**
         * @brief legacy/fallback bridge としてSharedResourceRegistryへ登録するか
         *
         * 既定は互換のためtrue。production deferred pipelineでは RenderGraph named resource
         * を主経路にするためfalseにします。
         */
        void SetRegisterLegacyBridge(bool bRegister)
        {
            m_bRegisterLegacyBridge = bRegister;
        }

        // ========================================
        // シャドウマップアクセス
        // ========================================

        /**
         * @brief 方向光の影を受ける最大のビュー深度（m）を設定します
         * @param distance 0より大きい有限値。それ以外はCSMを無効にする
         */
        void SetMaxShadowDistance(float distance) { m_Settings.MaxShadowDistance = distance; }
        float GetMaxShadowDistance() const { return m_Settings.MaxShadowDistance; }

        /** @brief 太陽の影の方式。Vsm のとき、CSM の行列と同じ場所で太陽のクリップマップを毎フレーム作って公開する（描画は CSM のまま） */
        void SetShadowMethod(ShadowMethod method) { m_ShadowMethod = method; }
        ShadowMethod GetShadowMethod() const { return m_ShadowMethod; }

        RHI::ITexture* GetShadowMapTexture() const { return m_ShadowMapTexture.get(); }
        RGResourceHandle GetShadowMapHandle() const { return m_ShadowMapHandle; }
        RHI::ITexture* GetPointShadowCubeTexture() const { return m_PointShadowCubeTexture.get(); }
        RGResourceHandle GetPointShadowCubeHandle() const { return m_PointShadowCubeHandle; }

    private:
        friend struct SkinnedRenderPathContractTestAccess;

        bool TryPrepareSkinnedCommand(ViewRenderContext& context,
                                      const DrawCommand& source,
                                      DrawCommand& outCommand) const;

        /** @brief キューブシャドウの資源を初めて要るときに作ります（失敗したら以後は点光源の影を描かない） */
        bool EnsurePointShadowResources(const ViewRenderContext& context);

        /** @brief 選ばれた点光源ごとにキューブの6面を描きます */
        void ExecutePointShadows(ViewRenderContext& context);

        // 設定
        ShadowMapPassSettings m_Settings;

        // SceneView参照（外部所有）
        SceneView *m_SceneView = nullptr;
        SceneRenderer *m_SceneRenderer = nullptr;

        // シャドウマップ深度テクスチャ
        RHI::TexturePtr m_ShadowMapTexture;
        RHI::SamplerPtr m_ShadowSampler;
        RGResourceHandle m_ShadowMapHandle;

        // レンダーパス・フレームバッファ
        RHI::RenderPassPtr m_ShadowRenderPass;
        Container::VariableArray<RHI::FramebufferPtr> m_ShadowFramebuffers;

        RHI::PipelinePtr m_SkinnedShadowPipeline;
        // パイプライン・シェーダー
        RHI::ShaderPtr m_SkinnedShadowVertexShader;
        RHI::PipelinePtr m_ShadowPipeline;
        RHI::ShaderPtr m_ShadowVertexShader;
        RHI::ShaderPtr m_ShadowFragmentShader;

        // デバイス参照
        RHI::IDevice *m_Device = nullptr;

        bool m_bRegisterLegacyBridge = true;

        ShadowMethod m_ShadowMethod = ShadowMethod::Csm;
        // VSM_CLIPMAP / VSM_TEXEL を出したか（起動後に 1 回だけ出す）
        bool m_bLoggedVsmClipmap = false;

        // 最後に記録したCSMの分割の奥（m）。変わったときだけ分割を記録する。
        float m_LoggedCascadeSplitFar = -1.0f;
        // 最後に記録したカスケードごとのMegaGeometryの描画数と三角形数。変わったときだけ記録する。
        uint32_t m_LoggedCsmMegaDraws[4] = {~0u, ~0u, ~0u, ~0u};
        uint32_t m_LoggedCsmMegaTriangles[4] = {~0u, ~0u, ~0u, ~0u};
        // 最後に記録したカスケードごとのMegaGeometryの段（メッシュ名:段/三角形数）。変わったときだけ記録する。
        Container::String m_LoggedCsmMegaLevels;
        // 最後に記録した点光源のキューブの面ごとのMegaGeometryの段。変わったときだけ記録する。
        Container::String m_LoggedPointMegaLevels;

        // PerObject UBOアロケータ
        DynamicUniformAllocator m_UniformAllocator;

        // 点光源のキューブシャドウ（最初に影を落とす点光源が現れたフレームで作る）
        RHI::TexturePtr m_PointShadowCubeTexture;
        RGResourceHandle m_PointShadowCubeHandle;
        Container::VariableArray<RHI::FramebufferPtr> m_PointShadowFramebuffers;
        RHI::ShaderPtr m_PointShadowVertexShader;
        RHI::ShaderPtr m_SkinnedPointShadowVertexShader;
        RHI::ShaderPtr m_PointShadowFragmentShader;
        RHI::PipelinePtr m_PointShadowPipeline;
        RHI::PipelinePtr m_SkinnedPointShadowPipeline;
        // 面ごとのUBO（非スキンの描画は面ごとに1つを共有し、スキンの描画は描画ごとに取る）
        DynamicUniformAllocator m_PointShadowUniformAllocator;
        bool m_bPointShadowResourcesReady = false;
        bool m_bPointShadowResourcesFailed = false;
        // 最後に記録したキューブシャドウの灯の数。変わったときだけ記録する。
        uint32_t m_LoggedPointShadowLightCount = 0;
    };

} // namespace NorvesLib::Core::Rendering

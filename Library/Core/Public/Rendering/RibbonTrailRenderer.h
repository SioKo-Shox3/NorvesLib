#pragma once

#include "Rendering/RibbonTrailTypes.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class FrameResourceLane;
    class ShaderManager;

    /** @brief フレームの頂点を借りて、ワールド座標の帯を三角形ストリップとして描く。 */
    class RibbonTrailRenderer
    {
    public:
        /**
         * @brief シェーダーと加算・アルファのパイプラインを作る。
         * @param renderPass 色の的が 1 つと深度の的を持つ描画先。形式はこのパスから引き継ぐ。
         * 描画先のパスを作り直したときは、同じ描画先を渡して初期化し直す。
         */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager,
                        const RHI::RenderPassPtr& renderPass);

        /** @brief 資源の参照を手放す。GPU で使い終わった資源の回収はエンジンが行う。 */
        void Shutdown();

        bool IsInitialized() const;

        /**
         * @brief 開始済みのレンダーパスへ帯を記録する。ビューポートとシザーは呼び出し側で設定する。
         * @param view CopyShaderView が出力する 16 要素の行列。
         * @param projection CopyShaderProjection が出力する 16 要素の行列。
         * 空の帯は確保も束縛も行わず成功する。頂点・定数・記述子は frameResources から借りる。
         */
        bool Record(RHI::ICommandList* commandList, FrameResourceLane* frameResources,
                    const RibbonTrailScene& scene, const float (&view)[16],
                    const float (&projection)[16], float preExposure) const;

    private:
        RHI::ShaderPtr m_VertexShader;
        RHI::ShaderPtr m_FragmentShader;
        RHI::PipelinePtr m_AdditivePipeline;
        RHI::PipelinePtr m_AlphaPipeline;
        RHI::DescriptorSetLayoutPtr m_DescriptorSetLayout;
    };
} // 名前空間 NorvesLib::Core::Rendering

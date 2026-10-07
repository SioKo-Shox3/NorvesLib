#pragma once

// 太陽の仮想シャドウマップ（VSM）の、要るページへの印付け・物理ページの割り当て・消去の計算（毎フレームすべて作り直す。キャッシュは無い）。
// VirtualShadowMapPass がこの記録を RenderGraph の中で使い、GPU のテストは RenderGraph なしで直接呼ぶ。
//
// 3 つの区間（GPU のタイムスタンプの名前）:
//   VsmMark     : 要求のビット列を 0 にし、GBuffer.Depth の空でない画素から要るページに印を付ける（vsm_mark.comp）。
//   VsmAllocate : ページの表・統計を 0 にし、空きページの一覧を作り直し、要求のページへ物理ページを割り当てて、
//                 ページの表に「割り当て済み・dirty」を書く（vsm_allocate.comp の 3 段階）。
//   VsmClear    : dirty のページの物理ページを 1.0 のビットで埋める（vsm_clear.comp。間接 dispatch）。

#include "Rendering/FrameUseRing.h"
#include "Rendering/VirtualShadowMapClipmap.h"
#include "Rendering/VirtualShadowMapPass.h"
#include "RHI/IDescriptorSet.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class ICommandList;
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    class ShaderManager;

    namespace VirtualShadowMap
    {
        /**
         * @brief 印付けが隣のページへも印を付ける範囲のうち、texel に比例する分（texel。ページの境界からこの範囲の標本が、隣のページを読む）の既定
         *
         * 照明（Common/VirtualShadowMap.glsl）の標本の半径は max(物理の半影, 画素の大きさ, 段の 1 texel) で、画素の大きさと 1 texel の
         * 側は、段を選ぶ式（bias -0.5）では texel の 2.83 倍未満。標本の位置は法線の向きへ最大 1.5 texel ずれるので、合わせて 4.33 texel。
         * 余裕を持たせて 5 texel とする。物理の半影の側（上限 MAX_FILTER_RADIUS_METERS）は、ワールドの長さとして別に足す。
         */
        constexpr float DEFAULT_PCF_RADIUS_TEXELS = 5.0f;
        /**
         * @brief ブロッカーの探索と PCF の半径の上限（ワールドの長さ m。段に依らない）の既定
         *
         * 物理の半影の半幅（受け手と遮る物の深度の差 × 太陽の角半径の tan）は、これを超えるとここで抑える。3 cm は受け手と遮る物の深度の差
         * 約 6.4 m 分の半影。印付けは隣のページへの印の範囲にこれを足す（探索・PCF の標本が読むページに印が無いと、粗い段へ逃げて影が欠ける）。
         */
        constexpr float MAX_FILTER_RADIUS_METERS = 0.03f;
        /** @brief 太陽の角半径の tan（Common/SunShadowCsm.glsl の DIRECTIONAL_LIGHT_TAN_ANGULAR_RADIUS と同じ） */
        constexpr float SUN_TAN_ANGULAR_RADIUS = 0.00468f;
        /** @brief 間接 dispatch の x の上限（Vulkan が保証する maxComputeWorkGroupCount[0] の最小値。超える分は y へ広げる） */
        constexpr uint32_t GROUP_COUNT_X_LIMIT = 65535;
        /** @brief 消去するページの一覧の先頭の語: 0〜2 = 間接 dispatch の引数、3 = ページの数、4 以降 = 物理ページの番号 */
        constexpr uint32_t DIRTY_LIST_HEADER_WORDS = 4;
        constexpr uint32_t DIRTY_LIST_COUNT_WORD = 3;

        /** @brief 消去するページの一覧の大きさ（バイト）: 先頭の 4 語と、ページ数ぶんの物理ページの番号 */
        constexpr uint64_t DirtyListBytes(uint32_t pages)
        {
            return (static_cast<uint64_t>(pages) + DIRTY_LIST_HEADER_WORDS) * sizeof(uint32_t);
        }
    } // namespace VirtualShadowMap

    /** @brief 1 回の記録の入力。バッファはすべて UnorderedAccess の状態で渡し、同じ状態で戻る */
    struct VirtualShadowMapPagesDispatch
    {
        /** @brief GBuffer.Depth（無い、または Clipmap が使えないときは印付けをせず、要求は 0 のまま割り当てる） */
        RHI::TexturePtr Depth;
        /** @brief 今フレームのクリップマップ（無効なら印付けをしない） */
        const VirtualShadowMapClipmap* Clipmap = nullptr;
        /** @brief シェーダー向け（列優先）の逆ビュー射影行列 */
        float InverseViewProjection[16] = {};
        float CameraPosition[3] = {};
        /** @brief 垂直の画角（度）。段の選び方（画面上の 1 画素の大きさ）に使う */
        float FovYDegrees = 0.0f;
        /** @brief PCF の核の半径のうち、texel に比例する分（texel） */
        float PcfRadiusTexels = VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS;
        /** @brief ブロッカーの探索と PCF の半径の上限（ワールドの長さ m）。隣のページへの印の範囲に足す */
        float MaxFilterRadiusMeters = VirtualShadowMap::MAX_FILTER_RADIUS_METERS;
        /** @brief 物理ページの数（プール・空きページの一覧・消去の一覧の大きさと合っていること） */
        uint32_t PoolPages = 0;

        RHI::BufferPtr Pool;
        RHI::BufferPtr PageTable;
        RHI::BufferPtr RequestBits;
        RHI::BufferPtr FreeList;
        RHI::BufferPtr Stats;
        RHI::BufferPtr DirtyList;
    };

    /** @brief 印付け・割り当て・消去の 3 つの計算パイプラインと、フレームごとのディスクリプタを持つ */
    class VirtualShadowMapPages
    {
    public:
        VirtualShadowMapPages();
        ~VirtualShadowMapPages();

        /** @brief 3 つの計算シェーダーを読み、パイプラインを作る。失敗したら false（何も持たない） */
        bool Initialize(RHI::IDevice* device, ShaderManager* shaderManager);
        void Shutdown();
        bool IsReady() const { return m_Device != nullptr && m_MarkPipeline && m_AllocatePipeline && m_ClearPipeline; }

        /** @brief フレームの枠を選ぶ（frameSerial は 0 以外でフレームごとに増える） */
        void BeginFrame(uint32_t inFlightIndex, uint64_t frameSerial);

        /**
         * @brief 印付け → 割り当て → 消去を記録する。必要な資源が揃わなければ false を返し、何も記録しない
         *
         * 印付けは Depth があり Clipmap が有効で、段の数・ページ数が資源の大きさに収まるときだけ。そうでなければ
         * 要求は 0 のまま、割り当て・消去（0 ページ）だけを記録する。
         */
        bool Record(RHI::ICommandList* commandList, const VirtualShadowMapPagesDispatch& dispatch);

        /** @brief 直前の Record が印付けを記録したか */
        bool WasMarked() const { return m_bMarked; }

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        bool CreatePipeline(ShaderManager* shaderManager,
                            const char* shaderName,
                            const RHI::DescriptorSetDesc& layout,
                            RHI::ShaderPtr& outShader,
                            RHI::PipelinePtr& outPipeline);
        bool AcquireUse(FrameUseRing<Use>& ring, const RHI::DescriptorSetDesc& layout, Use*& outUse);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_MarkShader;
        RHI::ShaderPtr m_AllocateShader;
        RHI::ShaderPtr m_ClearShader;
        RHI::PipelinePtr m_MarkPipeline;
        RHI::PipelinePtr m_AllocatePipeline;
        RHI::PipelinePtr m_ClearPipeline;
        RHI::SamplerPtr m_PointSampler;
        FrameUseRing<Use> m_MarkUses;
        FrameUseRing<Use> m_AllocateUses;
        FrameUseRing<Use> m_ClearUses;
        bool m_bMarked = false;
    };

} // namespace NorvesLib::Core::Rendering

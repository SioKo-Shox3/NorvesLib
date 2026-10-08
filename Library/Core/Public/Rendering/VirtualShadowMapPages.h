#pragma once

// 太陽の仮想シャドウマップ（VSM）の、要るページへの印付け・物理ページの割り当て・消去の計算（ページの表を前フレームから引き継ぐキャッシュ付き）。
// VirtualShadowMapPass がこの記録を RenderGraph の中で使い、GPU のテストは RenderGraph なしで直接呼ぶ。
//
// 3 つの区間（GPU のタイムスタンプの名前）:
//   VsmMark     : 要求のビット列を 0 にし、GBuffer.Depth の空でない画素から要るページに印を付ける（vsm_mark.comp）。
//   VsmAllocate : 統計を 0 にし、ページの表を前フレームから引き継いで（範囲の外へ出たページ・長く要求の無いページを空きへ戻し、
//                 無効にするページに dirty を付け）、空きページの一覧を作り直し、まだ無い要求のページへ物理ページを割り当てて、
//                 ページの表に「割り当て済み・dirty」を書く（vsm_allocate.comp の 11 段階）。
//   VsmClear    : dirty のページの物理ページを 1.0 のビットで埋める（vsm_clear.comp。間接 dispatch）。
//
// キャッシュ: 前フレームから割り当て済みで今フレームも要求され、無効にされていないページは物理ページを保ち、消去も描画もしない（dirty にしない）。
// 要求の無いページは CACHE_CARRY_FRAMES フレーム持ち越し、空きが足りないときは古い順に戻す。段の範囲が動いて範囲の外へ出たページは空きへ戻す
// （トーラスの番地なので、残ったページの番地は変わらない）。太陽の向き・深度の原点が変わったときは全ページを、動いた投影物の矩形は
// その範囲のページだけを dirty にする。バッファ・段の設定が前フレームと違う、印付けをしなかった、キャッシュを使わない指定のフレームは、
// ページの表を 0 にして全部を割り当て直す。

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
         * 照明（Common/VirtualShadowMap.glsl）の標本の半径は max(物理の半影, 画素の大きさ × PCF_MIN_RADIUS_PIXELS, 段の 1 texel) で、
         * 画素の大きさと 1 texel の側は、段を選ぶ式（bias -0.5）では texel の 2.83 倍未満（係数 0.5 では 1.42 倍未満）。標本の位置は法線の
         * 向きへ最大 1.5 texel ずれるので、合わせて 4.33 texel 未満。余裕を持たせて 5 texel とする。物理の半影の側（上限
         * MAX_FILTER_RADIUS_METERS）は、ワールドの長さとして別に足す。
         */
        constexpr float DEFAULT_PCF_RADIUS_TEXELS = 5.0f;
        /**
         * @brief PCF の半径の下限のうち、画素の大きさに比例する分の係数（下限 = 画素の大きさ × この値。段の 1 texel より小さくはしない）
         *
         * 下限を画素の大きさで決めるのは、段の切り替わりで縁の幅が跳ばないようにするため。画素 1 つ分にすると、CSM の texel が画素より
         * 小さくなる中距離（各カスケードの奥）で縁が CSM より太くなる。半分にすると、起動画面の縁の帯は CSM の約半分になり、
         * ワールドに固定した点の揺れも小さいまま（`--shadow-probe` の測定）。
         */
        constexpr float PCF_MIN_RADIUS_PIXELS = 0.5f;
        /**
         * @brief ブロッカーの探索と PCF の半径の上限（ワールドの長さ m。段に依らない）の既定
         *
         * 物理の半影の半幅（受け手と遮る物の深度の差 × 太陽の角半径の tan）は、これを超えるとここで抑える。0.5 m は受け手と遮る物の深度の差
         * 約 107 m 分の半影で、影の最大の距離（既定 80 m）より遠い遮る物まで物理の半影のまま扱える。
         * 印付けは隣のページへの印の範囲にこれを足す（探索・PCF の標本が読むページに印が無いと、粗い段へ逃げて影が欠ける）。
         */
        constexpr float MAX_FILTER_RADIUS_METERS = 0.5f;
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
        /** @brief カメラの前方（ワールド。長さは問わない。0・非有限なら印付けをしない）。影の範囲を前方への距離で測るのに使う */
        float CameraForward[3] = {};
        /**
         * @brief 影の距離の範囲（カメラの前方への距離。照明の読み出しと同じ ResolveVirtualShadowMapViewRange の値）。
         *        ShadowFarMeters が ShadowNearMeters 以下なら、クリップマップの設定の [0, MaxShadowDistance]
         */
        float ShadowNearMeters = 0.0f;
        float ShadowFarMeters = 0.0f;
        /** @brief 垂直の画角（度）。段の選び方（画面上の 1 画素の大きさ）に使う */
        float FovYDegrees = 0.0f;
        /** @brief PCF の核の半径のうち、texel に比例する分（texel） */
        float PcfRadiusTexels = VirtualShadowMap::DEFAULT_PCF_RADIUS_TEXELS;
        /** @brief ブロッカーの探索と PCF の半径の上限（ワールドの長さ m）。隣のページへの印の範囲に足す */
        float MaxFilterRadiusMeters = VirtualShadowMap::MAX_FILTER_RADIUS_METERS;
        /** @brief 物理ページの数（プール・空きページの一覧・消去の一覧の大きさと合っていること） */
        uint32_t PoolPages = 0;
        /** @brief ページを次のフレームへ持ち越すか。false（--vsm-cache=off）なら毎フレーム表を 0 にして、すべて割り当て直して描き直す */
        bool bCacheEnabled = true;
        /**
         * @brief 無効にするライト空間の矩形（x, y = 最小、z, w = 最大。ワールドの境界を今フレームのクリップマップのライト空間へ移したもの）。
         *        矩形が覆う、範囲の中の割り当て済みのページを dirty にする。矩形の数は MAX_INVALIDATION_RECTS まで
         */
        const float* InvalidationRects = nullptr;
        uint32_t InvalidationRectCount = 0;
        /** @brief 全ページを無効にする（境界の無い投影物が変わった・矩形が多すぎるとき）。太陽の向き・深度の原点の変化は Pages が自分で見つける */
        bool bInvalidateAll = false;

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

        /** @brief 直前の Record が、前フレームのページの表を引き継いだか（false は表を 0 にして全部を割り当て直した） */
        bool WasCacheContinued() const { return m_bCacheContinued; }
        /** @brief 直前の Record が、太陽の向き・深度の原点の変化で全ページを無効にしたか */
        bool WasInvalidatedAll() const { return m_bInvalidatedAll; }
        /** @brief 次の Record で、前フレームのページの表を引き継がず、全部を割り当て直す（資源を作り直したとき・テスト） */
        void DiscardCache() { m_bCacheValid = false; }

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

        // キャッシュの状態（前フレームの記録が見た入力）。資源・段の設定が変わったときは引き継がない
        bool m_bCacheValid = false;
        bool m_bCacheContinued = false;
        bool m_bInvalidatedAll = false;
        const void* m_CachedPageTable = nullptr;
        const void* m_CachedPool = nullptr;
        uint32_t m_CachedPoolPages = 0;
        VirtualShadowMapClipmap m_PreviousClipmap;
    };

} // namespace NorvesLib::Core::Rendering

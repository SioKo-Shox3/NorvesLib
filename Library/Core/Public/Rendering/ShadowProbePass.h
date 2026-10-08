#pragma once

// 太陽の影の揺れと細かさを測る道具（--shadow-probe）。統計が有効な構成（Debug・RelWithDebInfo）だけで作る。
//
// 画面の格子（4 画素おき）の空でない画素の深度・法線から、ワールドの位置と法線を固定の標本点として保存し、
// 以後の毎フレーム、各標本点を今のカメラへ投影して、画面の中にあり線形の深度が一致するものだけを「見えている」とし、
// 太陽の可視度を照明と同じ関数（Common/SunShadowCsm.glsl）で求めて GPU で集計する（shadow_probe.comp）。
// 太陽と物が止まっていれば、ワールドに固定した点の可視度はカメラが動いても変わらないはずなので、
// フレーム間の変化がそのまま影の揺れになる。集計は数フレーム遅れで読み戻して CPU で足し、終了時に 1 行ログへ出す:
//   SHADOW_PROBE method=csm frames=<n> probes=<n> pairs=<n> mean_abs_delta=<f> changed_ratio=<f> flip_ratio=<f>
//                partial_ratio=<f> mean_texel_mm=<f>
// mean_abs_delta・changed_ratio・flip_ratio は pairs（前のフレームでも見えていた標本の組）に対する値、
// partial_ratio・mean_texel_mm は見えていた標本の延べ数に対する値。
//
// --shadow-method=vsm のときは、同じ標本点を VSM でも測り（Common/VirtualShadowMap.glsl。照明と同じ関数。CSM の描画は vsm でも残る）、
// csm の行と並べて次の行を出す（mean_texel_mm は VSM が使った段の texel の一辺）:
//   SHADOW_PROBE method=vsm frames=<n> probes=<n> pairs=<n> mean_abs_delta=<f> changed_ratio=<f> flip_ratio=<f>
//                partial_ratio=<f> mean_texel_mm=<f>
//   SHADOW_PROBE_AGREE both_definite=<n> agree=<n> ratio=<f> finer_ratio=<f>
//   SHADOW_PROBE_DETAIL method=vsm visible=<n> fallback_ratio=<f>
// both_definite は CSM と VSM の両方が 0.02 以下か 0.98 以上の標本の延べ数、agree はそのうち両方が同じ側の数、
// finer_ratio は VSM の texel が CSM の texel 以下の標本の割合（見えていた標本の延べ数に対する値）、
// fallback_ratio は VSM の PCF の標本（1 標本 16 点）のうち、自分の段のページが無く粗い段へ逃げた点の割合。
//
// 太陽が無い（夜）か --shadow-probe=point のときは、同じ標本点について、影を持つ最初の点光源（キューブの番号 0）の可視度を、キューブ
// （Common/PointShadow.glsl の SamplePointShadow）と点光源の VSM（Common/VirtualShadowMapPoint.glsl。照明と同じ関数）の両方で求め、
// 太陽と同じ項目の行を light=point を付けて出す（--point-shadow-method=vsm で点光源の VSM が使えるフレームだけ）:
//   SHADOW_PROBE light=point method=cube frames=<n> probes=<n> pairs=<n> mean_abs_delta=<f> changed_ratio=<f> flip_ratio=<f>
//                partial_ratio=<f> mean_texel_mm=<f>
//   SHADOW_PROBE light=point method=vsm frames=<n> probes=<n> pairs=<n> mean_abs_delta=<f> changed_ratio=<f> flip_ratio=<f>
//                partial_ratio=<f> mean_texel_mm=<f>
//   SHADOW_PROBE_AGREE light=point both_definite=<n> agree=<n> ratio=<f> finer_ratio=<f>
//   SHADOW_PROBE_DETAIL light=point method=vsm visible=<n> fallback_ratio=<f>
// mean_texel_mm はキューブは面の軸の距離 z で 2z ÷ キューブの一辺、VSM は使った段の texel の一辺。Range の外・光の当たらない向き
// （法線が光源と反対）の標本は数えない。--shadow-probe=point のときは太陽を測らない（csm の行は出ない）。
//
// 標本を固定するフレームは、決定的な撮影ではエポック（読み込み完了）の最初のフレーム。そうでない起動では、
// 読み込みの完了を知る手段が無いので、起動から FALLBACK_CAPTURE_EXECUTE_COUNT 回目の実行で固定する。

#include "Rendering/FrameUseRing.h"
#include "Rendering/IViewPass.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "Rendering/RenderGraph/RenderGraphTypes.h"
#include "RHI/RHITypes.h"

#include <cstdint>

namespace NorvesLib::RHI
{
    class IDevice;
}

namespace NorvesLib::Core::Rendering
{
    struct ViewRenderContext;

    namespace ShadowProbe
    {
        /** @brief 格子の間隔（画素）。1280x720 で 320x180 の標本になる */
        constexpr uint32_t GRID_STEP = 4;
        /** @brief 見えているとみなす線形の深度の一致の許容（画素に写っている点の深度に対する比） */
        constexpr float DEPTH_TOLERANCE = 0.01f;
        /** @brief 決定的な撮影でないとき、標本を固定する実行の回数（読み込みが落ち着くまでの目安） */
        constexpr uint64_t FALLBACK_CAPTURE_EXECUTE_COUNT = 300;
        /** @brief 統計の語の数とバイト数（shadow_probe.comp の stats と同じ並び） */
        constexpr uint32_t STATS_WORD_COUNT = 40;
        constexpr uint32_t STATS_BYTES = STATS_WORD_COUNT * sizeof(uint32_t);
        /** @brief |Δv| の和・texel の一辺の和の固定小数点の倍率（シェーダーの DELTA_SCALE・TEXEL_SCALE） */
        constexpr double DELTA_SCALE = 4096.0;
        constexpr double TEXEL_SCALE = 16.0;
        /** @brief 標本の担当のスレッドの数に対する分割（シェーダーのワークグループの一辺） */
        constexpr uint32_t GROUP_SIZE = 8;

        /** @brief 統計の語の添字（シェーダーの STAT_* と同じ並び） */
        enum StatWord : uint32_t
        {
            StatVisible = 0,
            StatPairs = 1,
            StatDeltaSum = 2,
            StatChanged = 3,
            StatFlip = 4,
            StatPartial = 5,
            StatTexelSum = 6,
            StatOutOfRange = 7,
            StatCaptured = 8,
            // --shadow-method=vsm のときだけ数える VSM の語（9 以降）
            StatVsmVisible = 9,
            StatVsmPairs = 10,
            StatVsmDeltaSum = 11,
            StatVsmChanged = 12,
            StatVsmFlip = 13,
            StatVsmPartial = 14,
            StatVsmTexelSum = 15,
            StatBothDefinite = 16,
            StatAgree = 17,
            StatFiner = 18,
            StatVsmFallbackSamples = 19,
            // 点光源（影を持つ最初の灯）をキューブと VSM で測る語（20 以降）
            StatPointCubeVisible = 20,
            StatPointCubePairs = 21,
            StatPointCubeDeltaSum = 22,
            StatPointCubeChanged = 23,
            StatPointCubeFlip = 24,
            StatPointCubePartial = 25,
            StatPointCubeTexelSum = 26,
            StatPointVsmVisible = 27,
            StatPointVsmPairs = 28,
            StatPointVsmDeltaSum = 29,
            StatPointVsmChanged = 30,
            StatPointVsmFlip = 31,
            StatPointVsmPartial = 32,
            StatPointVsmTexelSum = 33,
            StatPointBothDefinite = 34,
            StatPointAgree = 35,
            StatPointFiner = 36,
            StatPointVsmFallbackSamples = 37,
        };
        /** @brief VSM の PCF の標本の数（1 標本の点の数。Common/PoissonDisk16.glsl） */
        constexpr uint32_t VSM_PCF_POINTS = 16;

        /** @brief 画面の大きさから決まる格子 */
        struct Grid
        {
            uint32_t CountX = 0;
            uint32_t CountY = 0;
            uint32_t Count() const { return CountX * CountY; }
            bool IsValid() const { return CountX != 0 && CountY != 0; }
        };

        inline Grid ComputeGrid(uint32_t width, uint32_t height)
        {
            Grid grid;
            if (width != 0 && height != 0)
            {
                grid.CountX = (width + GRID_STEP - 1) / GRID_STEP;
                grid.CountY = (height + GRID_STEP - 1) / GRID_STEP;
            }
            return grid;
        }

        /** @brief 読み戻した 1 フレーム分の統計を足し上げた値と、ログの 1 行の値 */
        struct Totals
        {
            /** @brief 測ったフレームの数（読み戻せたもの） */
            uint64_t Frames = 0;
            /** @brief 固定した標本の数 */
            uint64_t Probes = 0;
            /** @brief 見えていた標本の延べ数 */
            uint64_t Visible = 0;
            /** @brief 前のフレームでも見えていた標本の組の数 */
            uint64_t Pairs = 0;
            uint64_t DeltaSumQ = 0;
            uint64_t Changed = 0;
            uint64_t Flip = 0;
            uint64_t Partial = 0;
            uint64_t TexelSumQ = 0;
            uint64_t OutOfRange = 0;
            /** @brief VSM を測ったフレームの数と、その統計（vsm の構成でだけ増える） */
            uint64_t VsmFrames = 0;
            uint64_t VsmVisible = 0;
            uint64_t VsmPairs = 0;
            uint64_t VsmDeltaSumQ = 0;
            uint64_t VsmChanged = 0;
            uint64_t VsmFlip = 0;
            uint64_t VsmPartial = 0;
            uint64_t VsmTexelSumQ = 0;
            uint64_t BothDefinite = 0;
            uint64_t Agree = 0;
            uint64_t Finer = 0;
            uint64_t VsmFallbackSamples = 0;
            /** @brief 点光源を測ったフレームの数と、その統計（太陽が無いか --shadow-probe=point のときだけ増える） */
            uint64_t PointFrames = 0;
            uint64_t PointCubeVisible = 0;
            uint64_t PointCubePairs = 0;
            uint64_t PointCubeDeltaSumQ = 0;
            uint64_t PointCubeChanged = 0;
            uint64_t PointCubeFlip = 0;
            uint64_t PointCubePartial = 0;
            uint64_t PointCubeTexelSumQ = 0;
            uint64_t PointVsmVisible = 0;
            uint64_t PointVsmPairs = 0;
            uint64_t PointVsmDeltaSumQ = 0;
            uint64_t PointVsmChanged = 0;
            uint64_t PointVsmFlip = 0;
            uint64_t PointVsmPartial = 0;
            uint64_t PointVsmTexelSumQ = 0;
            uint64_t PointBothDefinite = 0;
            uint64_t PointAgree = 0;
            uint64_t PointFiner = 0;
            uint64_t PointVsmFallbackSamples = 0;

            /** @brief 固定のフレームの統計（標本の数）を足す */
            void AddCaptureFrame(const uint32_t* words) { Probes = words[StatCaptured]; }

            /** @brief 点光源を測ったフレームの統計を足す */
            void AddPointFrame(const uint32_t* words)
            {
                ++PointFrames;
                PointCubeVisible += words[StatPointCubeVisible];
                PointCubePairs += words[StatPointCubePairs];
                PointCubeDeltaSumQ += words[StatPointCubeDeltaSum];
                PointCubeChanged += words[StatPointCubeChanged];
                PointCubeFlip += words[StatPointCubeFlip];
                PointCubePartial += words[StatPointCubePartial];
                PointCubeTexelSumQ += words[StatPointCubeTexelSum];
                PointVsmVisible += words[StatPointVsmVisible];
                PointVsmPairs += words[StatPointVsmPairs];
                PointVsmDeltaSumQ += words[StatPointVsmDeltaSum];
                PointVsmChanged += words[StatPointVsmChanged];
                PointVsmFlip += words[StatPointVsmFlip];
                PointVsmPartial += words[StatPointVsmPartial];
                PointVsmTexelSumQ += words[StatPointVsmTexelSum];
                PointBothDefinite += words[StatPointBothDefinite];
                PointAgree += words[StatPointAgree];
                PointFiner += words[StatPointFiner];
                PointVsmFallbackSamples += words[StatPointVsmFallbackSamples];
            }

            /** @brief 太陽を測ったフレームの統計を足す。bVsm は、そのフレームが太陽の VSM も測ったか */
            void AddMeasuredFrame(const uint32_t* words, bool bVsm = false)
            {
                ++Frames;
                if (bVsm)
                {
                    ++VsmFrames;
                    VsmVisible += words[StatVsmVisible];
                    VsmPairs += words[StatVsmPairs];
                    VsmDeltaSumQ += words[StatVsmDeltaSum];
                    VsmChanged += words[StatVsmChanged];
                    VsmFlip += words[StatVsmFlip];
                    VsmPartial += words[StatVsmPartial];
                    VsmTexelSumQ += words[StatVsmTexelSum];
                    BothDefinite += words[StatBothDefinite];
                    Agree += words[StatAgree];
                    Finer += words[StatFiner];
                    VsmFallbackSamples += words[StatVsmFallbackSamples];
                }
                Visible += words[StatVisible];
                Pairs += words[StatPairs];
                DeltaSumQ += words[StatDeltaSum];
                Changed += words[StatChanged];
                Flip += words[StatFlip];
                Partial += words[StatPartial];
                TexelSumQ += words[StatTexelSum];
                OutOfRange += words[StatOutOfRange];
            }

            double MeanAbsDelta() const { return Pairs ? static_cast<double>(DeltaSumQ) / DELTA_SCALE / static_cast<double>(Pairs) : 0.0; }
            double ChangedRatio() const { return Pairs ? static_cast<double>(Changed) / static_cast<double>(Pairs) : 0.0; }
            double FlipRatio() const { return Pairs ? static_cast<double>(Flip) / static_cast<double>(Pairs) : 0.0; }
            double PartialRatio() const { return Visible ? static_cast<double>(Partial) / static_cast<double>(Visible) : 0.0; }
            double MeanTexelMm() const { return Visible ? static_cast<double>(TexelSumQ) / TEXEL_SCALE / static_cast<double>(Visible) : 0.0; }
            double OutOfRangeRatio() const { return Visible ? static_cast<double>(OutOfRange) / static_cast<double>(Visible) : 0.0; }

            double VsmMeanAbsDelta() const { return VsmPairs ? static_cast<double>(VsmDeltaSumQ) / DELTA_SCALE / static_cast<double>(VsmPairs) : 0.0; }
            double VsmChangedRatio() const { return VsmPairs ? static_cast<double>(VsmChanged) / static_cast<double>(VsmPairs) : 0.0; }
            double VsmFlipRatio() const { return VsmPairs ? static_cast<double>(VsmFlip) / static_cast<double>(VsmPairs) : 0.0; }
            double VsmPartialRatio() const { return VsmVisible ? static_cast<double>(VsmPartial) / static_cast<double>(VsmVisible) : 0.0; }
            double VsmMeanTexelMm() const { return VsmVisible ? static_cast<double>(VsmTexelSumQ) / TEXEL_SCALE / static_cast<double>(VsmVisible) : 0.0; }
            /** @brief CSM と VSM が両方確定した標本のうち、同じ側だった割合 */
            double AgreeRatio() const { return BothDefinite ? static_cast<double>(Agree) / static_cast<double>(BothDefinite) : 0.0; }
            double FinerRatio() const { return VsmVisible ? static_cast<double>(Finer) / static_cast<double>(VsmVisible) : 0.0; }
            double PointCubeMeanAbsDelta() const { return PointCubePairs ? static_cast<double>(PointCubeDeltaSumQ) / DELTA_SCALE / static_cast<double>(PointCubePairs) : 0.0; }
            double PointCubeChangedRatio() const { return PointCubePairs ? static_cast<double>(PointCubeChanged) / static_cast<double>(PointCubePairs) : 0.0; }
            double PointCubeFlipRatio() const { return PointCubePairs ? static_cast<double>(PointCubeFlip) / static_cast<double>(PointCubePairs) : 0.0; }
            double PointCubePartialRatio() const { return PointCubeVisible ? static_cast<double>(PointCubePartial) / static_cast<double>(PointCubeVisible) : 0.0; }
            double PointCubeMeanTexelMm() const { return PointCubeVisible ? static_cast<double>(PointCubeTexelSumQ) / TEXEL_SCALE / static_cast<double>(PointCubeVisible) : 0.0; }
            double PointVsmMeanAbsDelta() const { return PointVsmPairs ? static_cast<double>(PointVsmDeltaSumQ) / DELTA_SCALE / static_cast<double>(PointVsmPairs) : 0.0; }
            double PointVsmChangedRatio() const { return PointVsmPairs ? static_cast<double>(PointVsmChanged) / static_cast<double>(PointVsmPairs) : 0.0; }
            double PointVsmFlipRatio() const { return PointVsmPairs ? static_cast<double>(PointVsmFlip) / static_cast<double>(PointVsmPairs) : 0.0; }
            double PointVsmPartialRatio() const { return PointVsmVisible ? static_cast<double>(PointVsmPartial) / static_cast<double>(PointVsmVisible) : 0.0; }
            double PointVsmMeanTexelMm() const { return PointVsmVisible ? static_cast<double>(PointVsmTexelSumQ) / TEXEL_SCALE / static_cast<double>(PointVsmVisible) : 0.0; }
            /** @brief キューブと点光源の VSM が両方確定した標本のうち、同じ側だった割合 */
            double PointAgreeRatio() const { return PointBothDefinite ? static_cast<double>(PointAgree) / static_cast<double>(PointBothDefinite) : 0.0; }
            double PointFinerRatio() const { return PointVsmVisible ? static_cast<double>(PointFiner) / static_cast<double>(PointVsmVisible) : 0.0; }
            /** @brief 点光源の VSM の PCF の標本の点のうち、粗い段へ逃げた割合 */
            double PointFallbackRatio() const
            {
                return PointVsmVisible ? static_cast<double>(PointVsmFallbackSamples) / (static_cast<double>(PointVsmVisible) * VSM_PCF_POINTS) : 0.0;
            }
            /** @brief VSM の PCF の標本の点のうち、粗い段へ逃げた割合 */
            double FallbackRatio() const
            {
                return VsmVisible ? static_cast<double>(VsmFallbackSamples) / (static_cast<double>(VsmVisible) * VSM_PCF_POINTS) : 0.0;
            }
        };
    } // namespace ShadowProbe

    /**
     * @brief 太陽の影の標本を測る RenderGraph のパス（照明の後に置く）
     *
     * 読むもの: GBuffer.Depth・GBuffer.Normal・ShadowMap（CSM の影の地図）・Scene.Color（照明の後に並べるための依存）。
     * vsm の構成では、VSM.PageTable・VSM.PhysicalPool も読む（VirtualShadowMapPass が公開したとき）。
     * 書くものは RenderGraph の資源に無い（標本・前の可視度・統計は、このパスが持つバッファ）。
     * CSM の行列・分割・影の地図の読み方は ViewRenderContext::PhysicalLighting（ShadowMapPass の公開値）から取る。
     */
    class ShadowProbePass final : public IViewPass, public IRenderGraphPass
    {
    public:
        ShadowProbePass();
        ~ShadowProbePass() override;

        const char* GetName() const override { return "ShadowProbePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        /** @brief 太陽ではなく点光源だけを測る（--shadow-probe=point）。Initialize の前に決める。既定は、太陽が使えるフレームは太陽、使えなければ点光源 */
        void SetPointOnly(bool bPointOnly) { m_bPointOnly = bPointOnly; }
        bool IsPointOnly() const { return m_bPointOnly; }

        /** @brief これまでに読み戻せた統計の合計 */
        const ShadowProbe::Totals& GetTotals() const { return m_Totals; }
        /** @brief 標本を固定したか */
        bool HasCapturedProbes() const { return m_bCaptured; }

        /** @brief 合計から SHADOW_PROBE の行を 1 回だけ出す（Shutdown が呼ぶ。出し終えていれば何もしない） */
        void LogSummary();

    private:
        struct Use
        {
            RHI::BufferPtr Uniform;
            /** @brief 太陽の VSM のスライスの表（GPUVsmSlice の配列。ホストが書く storage buffer） */
            RHI::BufferPtr Slices;
            RHI::DescriptorSetPtr DescriptorSet;
        };

        /** @brief 統計の読み戻し先（ホストが読めるバッファ）。数フレーム遅れて読む */
        struct StatsSlot
        {
            RHI::BufferPtr Buffer;
            const uint32_t* Mapped = nullptr;
            uint64_t ExecuteIndex = 0;
            bool bPending = false;
            bool bCapture = false;
            /** @brief この記録が太陽の VSM も測ったか */
            bool bVsm = false;
            /** @brief この記録が太陽を測ったか */
            bool bSun = false;
            /** @brief この記録が点光源（キューブと VSM）を測ったか */
            bool bPoint = false;
        };
        static constexpr uint32_t StatsSlotCount = 4;

        bool CreatePipeline(ViewRenderContext& context);
        bool EnsureBuffers(const ShadowProbe::Grid& grid);
        void HarvestSlot(StatsSlot& slot);
        bool AcquireUse(Use*& outUse);

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_Shader;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_PointSampler;
        FrameUseRing<Use> m_Uses;

        /** @brief 固定した標本（位置・法線）と前のフレームの可視度。GPU だけが使う */
        RHI::BufferPtr m_ProbeBuffer;
        RHI::BufferPtr m_StateBuffer;
        ShadowProbe::Grid m_Grid;
        StatsSlot m_Slots[StatsSlotCount];
        bool m_bStatsMapped = false;

        RGTextureHandle m_DepthHandle;
        RGTextureHandle m_NormalHandle;
        RGTextureHandle m_ShadowMapHandle;
        RGTextureHandle m_SceneColorHandle;
        /** @brief 太陽の VSM のページの表・物理ページのプール（公開されたときだけ有効） */
        RGResourceHandle m_VsmPageTableHandle;
        RGResourceHandle m_VsmPoolHandle;
        /** @brief 点光源のキューブシャドウ（影を落とす点光源があるフレームだけ公開される。読む宣言で、照明の後・書き込みの後に並べる） */
        RGTextureHandle m_PointCubeHandle;
        bool m_bDeclared = false;
        bool m_bPointOnly = false;

        uint64_t m_ExecuteCount = 0;
        bool m_bEpochSeen = false;
        bool m_bCaptured = false;
        bool m_bLogged = false;
        ShadowProbe::Totals m_Totals;
    };

} // namespace NorvesLib::Core::Rendering

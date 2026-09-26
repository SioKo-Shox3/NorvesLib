#pragma once

#include "IViewPass.h"
#include "Rendering/AutoExposure.h"
#include "Rendering/RenderGraph/IRenderGraphPass.h"
#include "RHI/RHITypes.h"
#include "Container/Containers.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 自動露出の最新の測定（RenderThread で更新する） */
    struct AutoExposureMeasurement
    {
        /** @brief 測定したフレームの番号（FramePacket の FrameNumber） */
        uint64_t FrameNumber = 0u;

        /** @brief ヒストグラムの画素の総数 */
        uint64_t PixelCount = 0u;

        /** @brief 外れを除いた log2 輝度の平均（cd/m²） */
        float AverageLog2Luminance = 0.0f;

        /** @brief そのフレームのヒストグラムから求めた目標の EV100 */
        float TargetEV100 = 0.0f;

        /** @brief 順応させた後の EV100 */
        float AdaptedEV100 = 0.0f;

        bool bValid = false;
    };

    /**
     * @brief 自動露出の測定パス（ポストプロセス）
     *
     * SceneColor の絶対輝度のヒストグラムを compute で作り、フレームスロットごとの読み戻しバッファへ写す。
     * 同じフレームスロットが次に回ってきたとき（スワップチェーンの待機で前の提出の完了が保証される）に
     * ヒストグラムを CPU で読み、外れを除いた平均から目標の EV100 を求めて順応させる。
     * 読み戻しの遅れはフレームスロットの数だけで、RenderThread の同期は変えない。
     *
     * 順応させた EV100 は TryGetAdaptedEV100() で取れる。露出の方式が Auto のカメラでは、SceneView が
     * 次のフレームの描画の最初にこの値からプリエクスポージャを決める。測定はそのフレームで実際に掛けた
     * プリエクスポージャで割って絶対輝度に戻すので、掛けた露出が次の目標へ跳ね返らない。
     * 1フレームに同じ View を複数の Viewport で描くときは、そのフレームの最初の Viewport だけを測る。
     *
     * 入力:
     * - "SSR.SceneColor"（無ければ "Scene.Color"）: HDR シーンカラー（プリエクスポージャ済み）
     */
    class AutoExposurePass : public IViewPass, public IRenderGraphPass
    {
    public:
        explicit AutoExposurePass(const AutoExposureSettings& settings = AutoExposureSettings{});
        ~AutoExposurePass() override;

        const char* GetName() const override { return "AutoExposurePass"; }

        bool Initialize(ViewRenderContext& context) override;
        void Shutdown() override;
        void Setup(ViewRenderContext& context) override;
        void Execute(ViewRenderContext& context) override;

        void Declare(RenderGraphBuilder& builder) override;
        void Execute(RenderGraphResources& resources, ViewRenderContext& context) override;

        void SetSettings(const AutoExposureSettings& settings) { m_Settings = settings; }

        /** @brief 露出補正（EV）だけを変える。次に読み戻す測定から効く */
        void SetExposureCompensation(float exposureCompensation)
        {
            m_Settings.ExposureCompensation = exposureCompensation;
        }

        /** @brief 順応させた EV100。まだ有効な測定が無いときは false */
        bool TryGetAdaptedEV100(float& outEV100) const
        {
            if (!m_Adaptation.bValid)
            {
                return false;
            }
            outEV100 = m_Adaptation.EV100;
            return true;
        }
        const AutoExposureSettings& GetSettings() const { return m_Settings; }

        /** @brief 最後に読み戻したヒストグラムから求めた測定 */
        const AutoExposureMeasurement& GetLatestMeasurement() const { return m_LatestMeasurement; }

    private:
        struct FrameSlot
        {
            RHI::BufferPtr ReadbackBuffer;
            RHI::BufferPtr ParamsBuffer;
            RHI::DescriptorSetPtr DescriptorSet;
            uint64_t FrameNumber = 0u;
            double TotalTime = 0.0;
            uint64_t PixelCount = 0u;
            bool bPending = false;
        };

        bool EnsurePipeline(ViewRenderContext& context);
        FrameSlot* EnsureFrameSlot(uint32_t frameIndex);
        void ConsumeCompletedSlot(FrameSlot& slot);
        void DisableAfterFailure(const char* reason);
        void TrackTransition(float previousEV100,
                             float targetEV100,
                             double totalTime,
                             float adaptationSeconds,
                             uint64_t frameNumber);

        AutoExposureSettings m_Settings;
        AutoExposureAdaptationState m_Adaptation;
        AutoExposureMeasurement m_LatestMeasurement;
        double m_LastAdaptationTime = 0.0;
        uint64_t m_ConsumedMeasurementCount = 0u;

        // 目標から大きく離れてから落ち着くまで（順応の途中）の記録。ログに出して振動と所要時間を確かめる
        struct TransitionRecord
        {
            bool bActive = false;
            double StartTime = 0.0;
            double LastLogTime = 0.0;
            // 順応に使った時間の和（1測定の上限で止めた後。描画が止まっていた時間を含まない）
            double AdaptedSeconds = 0.0;
            float StartEV100 = 0.0f;
            float Direction = 0.0f;
            float MaxOvershoot = 0.0f;
            float LastStep = 0.0f;
            uint32_t Reversals = 0u;
        };
        TransitionRecord m_Transition;

        RHI::IDevice* m_Device = nullptr;
        RHI::ShaderPtr m_ComputeShader;
        RHI::PipelinePtr m_Pipeline;
        RHI::SamplerPtr m_PointSampler;
        RHI::BufferPtr m_HistogramBuffer;
        RHI::ResourceState m_HistogramState = RHI::ResourceState::Undefined;
        Container::VariableArray<FrameSlot> m_FrameSlots;
        RGResourceHandle m_InputSceneColorHandle;
        bool m_bUnavailable = false;
    };

} // namespace NorvesLib::Core::Rendering

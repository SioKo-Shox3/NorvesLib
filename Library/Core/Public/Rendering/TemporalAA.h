// TAA のジッタ列（Halton(2,3)）と、ジッタを投影へ掛けるずらし量・履歴の混ぜ方の定数・履歴を使えるかの判定。
#pragma once

#include "Rendering/SceneProxy.h"
#include "Math/Matrix4x4.h"
#include "Math/MatrixUtils.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief ジッタ列の長さ。Halton(2,3) の1〜8番目を繰り返す。 */
    inline constexpr uint32_t TemporalAAJitterSampleCount = 8u;

    /** @brief 現在のフレームの色を混ぜる割合（残りは再投影した履歴）。 */
    inline constexpr float TemporalAACurrentFrameWeight = 0.1f;

    /** @brief 履歴をクリップする箱の半幅（近傍の色の標準偏差の何倍か、YCoCg）。 */
    inline constexpr float TemporalAAVarianceClipGamma = 1.0f;

    /**
     * @brief 書き戻すときのシャープ化の強さ（上下左右4画素の平均との差を足す割合）。
     *
     * 履歴を混ぜると細かな模様が柔らかくなるので、SceneColor へ書き戻す色だけを少し鋭くする
     * （履歴には鋭くする前の色を残す）。
     */
    inline constexpr float TemporalAASharpenStrength = 0.25f;

    /**
     * @brief 1つのフレームのジッタ
     *
     * Pixel は画素単位のずらし量（-0.5〜0.5）、Ndc は投影後の NDC でのずらし量（2 × 画素 / 寸法）。
     */
    struct TemporalAAJitter
    {
        float PixelX = 0.0f;
        float PixelY = 0.0f;
        float NdcX = 0.0f;
        float NdcY = 0.0f;
    };

    /**
     * @brief 基数 base の radical inverse（Halton 列の index 番目。index は1から数える）
     *
     * index を base 進で表した桁を小数点の右へ逆順に並べた値（0〜1）。base が2未満なら0。
     */
    inline float TemporalAAHalton(uint32_t index, uint32_t base)
    {
        if (base < 2u)
        {
            return 0.0f;
        }
        float fraction = 1.0f;
        float result = 0.0f;
        while (index > 0u)
        {
            fraction /= static_cast<float>(base);
            result += fraction * static_cast<float>(index % base);
            index /= base;
        }
        return result;
    }

    /**
     * @brief frameIndex 番目のフレームのジッタ
     *
     * Halton(2,3) の (frameIndex mod 8) + 1 番目から 0.5 を引いた画素のずらし量と、それを
     * width × height の画像の NDC へ直した量。寸法が0なら NDC のずらし量は0。
     */
    inline TemporalAAJitter ComputeTemporalAAJitter(uint64_t frameIndex, uint32_t width, uint32_t height)
    {
        const uint32_t sampleIndex = static_cast<uint32_t>(frameIndex % TemporalAAJitterSampleCount) + 1u;
        TemporalAAJitter jitter;
        jitter.PixelX = TemporalAAHalton(sampleIndex, 2u) - 0.5f;
        jitter.PixelY = TemporalAAHalton(sampleIndex, 3u) - 0.5f;
        jitter.NdcX = width > 0u ? 2.0f * jitter.PixelX / static_cast<float>(width) : 0.0f;
        jitter.NdcY = height > 0u ? 2.0f * jitter.PixelY / static_cast<float>(height) : 0.0f;
        return jitter;
    }

    /**
     * @brief 投影行列へ NDC のずらし量を掛ける
     *
     * 列ベクトル規約の投影 P の前に NDC の平行移動を掛け、クリップ座標の x・y へ w × ずらし量を足す。
     * 透視・正射影のどちらでも、除算後の NDC はどの深度でも同じ量だけずれ、z と w は変わらない。
     */
    inline Math::Matrix4x4 ApplyTemporalAAProjectionJitter(const Math::Matrix4x4& projection,
                                                           float ndcX,
                                                           float ndcY)
    {
        if (ndcX == 0.0f && ndcY == 0.0f)
        {
            return projection;
        }
        return Math::MatrixUtils::CreateTranslation(ndcX, ndcY, 0.0f) * projection;
    }

    /** @brief カメラへジッタを写す（投影を作るとき CameraViewConstants が掛ける）。 */
    inline void ApplyTemporalAAJitter(CameraProxy& camera, const TemporalAAJitter& jitter)
    {
        camera.ProjectionJitterNdcX = jitter.NdcX;
        camera.ProjectionJitterNdcY = jitter.NdcY;
    }

    /** @brief 履歴を使えるかの判定の結果（使えないときはその理由）。 */
    enum class TemporalAAHistoryDecision : uint8_t
    {
        Reuse,                 // 履歴を再投影して混ぜる
        NoHistory,             // 履歴が無い（最初のフレーム・捨てた後）
        ViewportChanged,       // 履歴を書いたのは別の Viewport
        FrameNotAdvanced,      // 履歴を書いたフレームより前か同じフレームを描く
        CameraChanged,         // カメラが切り替わった
        PreviousCameraMissing, // velocity の基準にするパケットの前のカメラが無い
        InvalidExposure        // 履歴か現在の露出が有限の正でない
    };

    /** @brief 履歴を使えるかを決めるための、今のフレームの値。 */
    struct TemporalAAHistoryQuery
    {
        // FramePacket のゲームのフレーム番号と、その時刻（アプリケーション開始からの秒）
        uint64_t FrameNumber = 0u;
        double TotalTime = 0.0;
        uint32_t ViewportId = 0u;
        uint64_t CameraId = 0u;
        float PreExposure = 0.0f;
        // パケットに前のカメラ（直前のゲームのフレームのカメラ）があるか
        bool bHasPreviousCamera = false;
    };

    /**
     * @brief TAA の履歴を書いたフレーム・時刻・Viewport・カメラ・露出を覚え、次に描くフレームで使えるかを決める
     *
     * パケットの velocity は直前のゲームのフレームからの動きだが、RenderThread は未描画のパケットを新しい
     * パケットで置き換えるので、履歴は2つ以上前のゲームのフレームのことがある（例: 10 を描いた後 11 を飛ばして
     * 12 を描く）。そのため履歴の再投影は、カメラの動きを履歴を書いたフレームのカメラ（GetCamera）から深度で
     * 求め直し、物体自身の動き（velocity からパケットの前のカメラによる動きを引いた分）だけを経過時間の比
     * （ComputeObjectMotionScale）で伸ばして足す。連続したフレームでは2つのカメラが同じで比は1なので、
     * velocity でそのまま再投影するのと同じになる。
     *
     * TAA を掛けない Viewport（同じフレームの2つ目以降）はこの状態に触れないので、1つの SceneView に
     * 2つの Viewport があっても、TAA を掛ける Viewport の履歴は途切れない。履歴を書いた Viewport を TAA 無しで
     * 描いたとき（NotifyViewportWithoutTemporalAA）は、その間の画像が履歴に入らないので捨てる。
     */
    class TemporalAAHistoryTracker
    {
    public:
        /** @brief query のフレームで履歴を使えるか（状態は変えない）。 */
        TemporalAAHistoryDecision Evaluate(const TemporalAAHistoryQuery& query) const
        {
            if (!m_bValid)
            {
                return TemporalAAHistoryDecision::NoHistory;
            }
            if (query.ViewportId != m_ViewportId)
            {
                return TemporalAAHistoryDecision::ViewportChanged;
            }
            if (query.FrameNumber <= m_FrameNumber)
            {
                return TemporalAAHistoryDecision::FrameNotAdvanced;
            }
            if (query.CameraId != m_CameraId)
            {
                return TemporalAAHistoryDecision::CameraChanged;
            }
            if (!query.bHasPreviousCamera)
            {
                return TemporalAAHistoryDecision::PreviousCameraMissing;
            }
            if (!IsPositiveFinite(m_PreExposure) || !IsPositiveFinite(query.PreExposure))
            {
                return TemporalAAHistoryDecision::InvalidExposure;
            }
            return TemporalAAHistoryDecision::Reuse;
        }

        /** @brief 履歴が直前のゲームのフレームのものか（パケットの前のカメラ・前の変換と同じフレームか）。 */
        bool IsContiguous(const TemporalAAHistoryQuery& query) const
        {
            return m_bValid && query.FrameNumber == m_FrameNumber + 1u;
        }

        /**
         * @brief 物体自身の動きを、パケットの1フレーム分から履歴を書いたフレームからの分へ伸ばす比
         *
         * 連続したフレームでは1。飛んだフレームでは（今の時刻 − 履歴の時刻）/ パケットの経過時間で、
         * 物体が履歴の時刻から同じ速さで動いたとみなす。経過時間が正の有限でなければ1。
         */
        float ComputeObjectMotionScale(const TemporalAAHistoryQuery& query, float packetDeltaTime) const
        {
            if (IsContiguous(query))
            {
                return 1.0f;
            }
            const double elapsed = query.TotalTime - m_TotalTime;
            const double scale = elapsed / static_cast<double>(packetDeltaTime);
            if (!(packetDeltaTime > 0.0f) || !(elapsed > 0.0) || !(scale > 0.0) || !(scale < 1.0e6))
            {
                return 1.0f;
            }
            return static_cast<float>(scale);
        }

        /** @brief query のフレームで履歴を書いたことを覚える。camera はジッタを外したそのフレームのカメラ。 */
        void Record(const TemporalAAHistoryQuery& query, const CameraProxy& camera)
        {
            m_bValid = true;
            m_FrameNumber = query.FrameNumber;
            m_TotalTime = query.TotalTime;
            m_ViewportId = query.ViewportId;
            m_CameraId = query.CameraId;
            m_PreExposure = query.PreExposure;
            m_Camera = camera;
            m_Camera.ProjectionJitterNdcX = 0.0f;
            m_Camera.ProjectionJitterNdcY = 0.0f;
        }

        /** @brief viewportId を TAA 無しで描いた。履歴を書いた Viewport なら履歴を捨てる。 */
        void NotifyViewportWithoutTemporalAA(uint32_t viewportId)
        {
            if (m_bValid && viewportId == m_ViewportId)
            {
                m_bValid = false;
            }
        }

        /** @brief 履歴を捨てる（次に働くフレームは現在の色だけを使う）。 */
        void Invalidate() { m_bValid = false; }

        bool IsValid() const { return m_bValid; }

        /** @brief 履歴を書いたフレームのプリエクスポージャ（露出の比を求めるのに使う）。 */
        float GetPreExposure() const { return m_PreExposure; }

        /** @brief 履歴を書いたフレームのカメラ（ジッタなし）。 */
        const CameraProxy& GetCamera() const { return m_Camera; }

    private:
        static bool IsPositiveFinite(float value)
        {
            // NaN は比較が偽になり、+∞ は上限で弾く。
            return value > 0.0f && value <= 3.4028235e38f;
        }

        bool m_bValid = false;
        uint64_t m_FrameNumber = 0u;
        double m_TotalTime = 0.0;
        uint32_t m_ViewportId = 0u;
        uint64_t m_CameraId = 0u;
        float m_PreExposure = 0.0f;
        CameraProxy m_Camera;
    };
} // namespace NorvesLib::Core::Rendering

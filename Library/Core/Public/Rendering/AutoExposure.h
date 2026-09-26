#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    /** @brief 輝度のヒストグラムの区間の数（compute の1グループの呼び出し数と同じ） */
    inline constexpr uint32_t AutoExposureHistogramBinCount = 256u;

    /** @brief ヒストグラムの下端（log2 cd/m²）。これより暗い画素と黒・非有限の画素は区間0へ入る */
    inline constexpr float AutoExposureHistogramMinLog2Luminance = -10.0f;

    /** @brief ヒストグラムの上端（log2 cd/m²）。これより明るい画素は最後の区間へ入る */
    inline constexpr float AutoExposureHistogramMaxLog2Luminance = 22.0f;

    /** @brief log2 輝度の1あたりの区間の数 */
    inline constexpr float AutoExposureHistogramBinsPerLog2 =
        static_cast<float>(AutoExposureHistogramBinCount) /
        (AutoExposureHistogramMaxLog2Luminance - AutoExposureHistogramMinLog2Luminance);

    /**
     * @brief 平均輝度から EV100 へ直す係数の log2
     *
     * 反射光式の露出計の式 EV100 = log2(L × S / K)（S = 100、K = 12.5）から log2(100 / 12.5) = 3。
     */
    inline constexpr float AutoExposureLuminanceToEV100Log2Offset = 3.0f;

    /**
     * @brief 自動露出の設定
     *
     * 「明るくなる向き」は画面が明るくなる順応（EV100 が下がる。暗い場所へ入ったとき）、
     * 「暗くなる向き」は画面が暗くなる順応（EV100 が上がる。明るい場所へ出たとき）。
     */
    struct AutoExposureSettings
    {
        /** @brief 平均から外す暗い側の画素の割合（0〜0.49） */
        float LowPercent = 0.10f;

        /** @brief 平均から外す明るい側の画素の割合（0〜0.49） */
        float HighPercent = 0.02f;

        /** @brief 露出補正（EV）。正の値で画面を明るくする（目標の EV100 を下げる） */
        float ExposureCompensation = 0.0f;

        /** @brief 目標の EV100 の下限 */
        float MinEV100 = -4.0f;

        /** @brief 目標の EV100 の上限 */
        float MaxEV100 = 18.0f;

        /**
         * @brief 明るくなる向きの順応の速さ（1/秒。差が e 分の1になるまでの時間の逆数）
         *
         * 2/秒で、夕方と昼の差（約5 EV）が 0.1 EV まで縮むのに約2秒かかる。
         */
        float SpeedBrighten = 2.0f;

        /** @brief 暗くなる向きの順応の速さ（1/秒） */
        float SpeedDarken = 3.0f;
    };

    /** @brief ヒストグラム1枚から求めた目標の露出 */
    struct AutoExposureHistogramResult
    {
        /** @brief 外れを除いた画素の log2 輝度の平均（cd/m²） */
        float AverageLog2Luminance = 0.0f;

        /** @brief 露出補正と下限・上限を掛けた目標の EV100 */
        float TargetEV100 = 0.0f;

        /** @brief ヒストグラムの画素の総数 */
        uint64_t TotalCount = 0u;

        /** @brief 平均に使った画素の数（外れを除いた後。端の区間は割合で数える） */
        double UsedCount = 0.0;

        /** @brief 画素が1つ以上あり、平均を求められたとき true */
        bool bValid = false;
    };

    /** @brief 順応の状態（前のフレームまでの露出） */
    struct AutoExposureAdaptationState
    {
        float EV100 = 0.0f;
        bool bValid = false;
    };

    namespace AutoExposureDetail
    {
        inline float SanitizeFinite(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }

        inline float SanitizePercent(float value, float fallback)
        {
            return std::clamp(SanitizeFinite(value, fallback), 0.0f, 0.49f);
        }

        inline float SanitizeSpeed(float value)
        {
            // 無限大の速さは目標へすぐ合わせる扱いにする（exp(-inf) = 0 と同じ結果）
            if (std::isnan(value) || value < 0.0f)
            {
                return 0.0f;
            }
            return value;
        }
    } // namespace AutoExposureDetail

    /**
     * @brief 絶対輝度（cd/m²）からヒストグラムの区間の番号を求める
     *
     * `Assets/Shaders/auto_exposure_histogram.comp` の LuminanceToBin と同じ規則。
     * 0以下・NaNは区間0、正の無限大は最後の区間へ入る。
     */
    inline uint32_t AutoExposureLuminanceToBin(float luminance)
    {
        if (!(luminance > 0.0f))
        {
            return 0u;
        }
        if (std::isinf(luminance))
        {
            return AutoExposureHistogramBinCount - 1u;
        }
        const float position = (std::log2(luminance) - AutoExposureHistogramMinLog2Luminance) *
                               AutoExposureHistogramBinsPerLog2;
        const float clamped = std::clamp(std::floor(position),
                                         0.0f,
                                         static_cast<float>(AutoExposureHistogramBinCount - 1u));
        return static_cast<uint32_t>(clamped);
    }

    /** @brief 区間の中央の log2 輝度（平均に使う代表値） */
    inline float AutoExposureBinCenterLog2Luminance(uint32_t bin)
    {
        return AutoExposureHistogramMinLog2Luminance +
               (static_cast<float>(bin) + 0.5f) / AutoExposureHistogramBinsPerLog2;
    }

    /** @brief log2 輝度の平均と露出補正・下限・上限から目標の EV100 を求める */
    inline float AutoExposureTargetEV100FromAverageLog2Luminance(float averageLog2Luminance,
                                                                 const AutoExposureSettings& settings)
    {
        float minEV100 = AutoExposureDetail::SanitizeFinite(settings.MinEV100, -4.0f);
        float maxEV100 = AutoExposureDetail::SanitizeFinite(settings.MaxEV100, 18.0f);
        if (minEV100 > maxEV100)
        {
            std::swap(minEV100, maxEV100);
        }
        const float compensation = AutoExposureDetail::SanitizeFinite(settings.ExposureCompensation, 0.0f);
        const float ev100 = averageLog2Luminance + AutoExposureLuminanceToEV100Log2Offset - compensation;
        return std::clamp(ev100, minEV100, maxEV100);
    }

    /**
     * @brief ヒストグラムから、暗い側・明るい側の外れを除いた log2 輝度の平均と目標の EV100 を求める
     *
     * 画素を暗い順に並べ、先頭の LowPercent と末尾の HighPercent を除いた範囲を平均する。
     * 境目にかかる区間は、範囲に入る画素の数だけ（端数を含めて）数える。
     * 各画素の log2 輝度には区間の中央の値を使う。
     */
    inline AutoExposureHistogramResult ComputeAutoExposureFromHistogram(const uint32_t* bins,
                                                                        uint32_t binCount,
                                                                        const AutoExposureSettings& settings)
    {
        AutoExposureHistogramResult result;
        if (bins == nullptr || binCount == 0u)
        {
            return result;
        }
        const uint32_t usedBinCount = std::min(binCount, AutoExposureHistogramBinCount);

        uint64_t totalCount = 0u;
        for (uint32_t bin = 0u; bin < usedBinCount; ++bin)
        {
            totalCount += bins[bin];
        }
        result.TotalCount = totalCount;
        if (totalCount == 0u)
        {
            return result;
        }

        const double total = static_cast<double>(totalCount);
        const double lowCut = total * AutoExposureDetail::SanitizePercent(settings.LowPercent, 0.10f);
        const double highCut = total * (1.0 - AutoExposureDetail::SanitizePercent(settings.HighPercent, 0.02f));

        double weightedSum = 0.0;
        double usedCount = 0.0;
        double cumulative = 0.0;
        for (uint32_t bin = 0u; bin < usedBinCount; ++bin)
        {
            const double count = static_cast<double>(bins[bin]);
            const double binStart = cumulative;
            const double binEnd = cumulative + count;
            cumulative = binEnd;
            const double overlap = std::min(binEnd, highCut) - std::max(binStart, lowCut);
            if (overlap <= 0.0)
            {
                continue;
            }
            weightedSum += overlap * static_cast<double>(AutoExposureBinCenterLog2Luminance(bin));
            usedCount += overlap;
        }

        if (usedCount <= 0.0)
        {
            return result;
        }

        result.UsedCount = usedCount;
        result.AverageLog2Luminance = static_cast<float>(weightedSum / usedCount);
        result.TargetEV100 = AutoExposureTargetEV100FromAverageLog2Luminance(result.AverageLog2Luminance, settings);
        result.bValid = true;
        return result;
    }

    /**
     * @brief EV100 から露出（シーンカラーに掛けるプリエクスポージャ）を求める
     *
     * CameraComponent の手動露出と同じ式 2^(-EV100) / 1.2（飽和に基づく感度の式）。
     * EV100 が非有限のときは 0 を返す（呼び出し側は手動の露出を使い続ける）。
     */
    inline float AutoExposurePreExposureFromEV100(float ev100)
    {
        if (!std::isfinite(ev100))
        {
            return 0.0f;
        }
        const double exposure = std::exp2(-static_cast<double>(ev100)) / 1.2;
        return static_cast<float>(std::clamp(exposure, 1.0e-6, 1.0e6));
    }

    /**
     * @brief 1ステップぶん EV100 を目標へ順応させる
     *
     * 差が指数的に縮む: next = target + (current - target) × exp(-speed × dt)。
     * 目標が今より低い（画面が明るくなる向き）ときは SpeedBrighten、高いときは SpeedDarken を使う。
     * dt が0以下・非有限のときは変えない。
     */
    inline float AdaptAutoExposureEV100(float currentEV100,
                                        float targetEV100,
                                        float deltaSeconds,
                                        const AutoExposureSettings& settings)
    {
        if (!std::isfinite(currentEV100))
        {
            return targetEV100;
        }
        if (!std::isfinite(targetEV100) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
        {
            return currentEV100;
        }
        const float speed = targetEV100 < currentEV100
                                ? AutoExposureDetail::SanitizeSpeed(settings.SpeedBrighten)
                                : AutoExposureDetail::SanitizeSpeed(settings.SpeedDarken);
        const double remaining = std::exp(-static_cast<double>(speed) * static_cast<double>(deltaSeconds));
        return static_cast<float>(static_cast<double>(targetEV100) +
                                  (static_cast<double>(currentEV100) - static_cast<double>(targetEV100)) * remaining);
    }

    /**
     * @brief 新しい測定で順応の状態を進める
     *
     * 測定が無効なら状態を変えない。最初の有効な測定では目標へそのまま合わせる。
     */
    inline void UpdateAutoExposureAdaptation(AutoExposureAdaptationState& state,
                                             const AutoExposureHistogramResult& measurement,
                                             float deltaSeconds,
                                             const AutoExposureSettings& settings)
    {
        if (!measurement.bValid || !std::isfinite(measurement.TargetEV100))
        {
            return;
        }
        if (!state.bValid)
        {
            state.EV100 = measurement.TargetEV100;
            state.bValid = true;
            return;
        }
        state.EV100 = AdaptAutoExposureEV100(state.EV100, measurement.TargetEV100, deltaSeconds, settings);
    }

    /** @brief 自動露出の最新の測定（RenderThread で更新し、統計のスナップショットで GameThread へ渡す） */
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

    /** @brief GPU から読み戻したヒストグラム（画素数の照合の前の中身） */
    struct AutoExposureHistogramReadback
    {
        /** @brief 記録したフレームの番号 */
        uint64_t FrameNumber = 0u;

        /** @brief 区間ごとの画素の数 */
        uint32_t Bins[AutoExposureHistogramBinCount] = {};

        bool bValid = false;
    };

} // namespace NorvesLib::Core::Rendering

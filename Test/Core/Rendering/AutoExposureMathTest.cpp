// 自動露出のヒストグラム→EV100 の計算と、タイムステップの順応を CPU の参照実装と照合する。
//
// ヒストグラムの参照: 画素の輝度を並べ、暗い側の LowPercent と明るい側の HighPercent を画素単位で落とし、
// 残りの log2 輝度を平均する（ヒストグラムを経由しない素朴な計算）。
// 順応の参照: dE/dt = -speed × (E - target) を細かい刻みの前進オイラー法で積分した値と、閉じた式の値。
#include "Rendering/AutoExposure.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>

using namespace NorvesLib::Core::Rendering;

namespace
{
    int GFailureCount = 0;

    void Check(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            std::printf("失敗: %s\n", message);
            ++GFailureCount;
        }
    }

    bool IsNear(double lhs, double rhs, double tolerance)
    {
        return std::fabs(lhs - rhs) <= tolerance;
    }

    // 再現できる擬似乱数（xorshift32）
    struct Random
    {
        uint32_t State = 0x12345678u;

        float NextUnit()
        {
            State ^= State << 13u;
            State ^= State >> 17u;
            State ^= State << 5u;
            return static_cast<float>(State >> 8u) / static_cast<float>(1u << 24u);
        }
    };

    constexpr uint32_t SampleCount = 1000u;

    struct Samples
    {
        float Luminance[SampleCount];
    };

    void BuildHistogram(const Samples& samples, uint32_t (&bins)[AutoExposureHistogramBinCount])
    {
        std::fill(std::begin(bins), std::end(bins), 0u);
        for (float luminance : samples.Luminance)
        {
            ++bins[AutoExposureLuminanceToBin(luminance)];
        }
    }

    // 素朴な参照: 画素を並べて外れを落とし、残りを平均する。
    // bUseBinCenter が true なら各画素の値を区間の中央へ丸め（ヒストグラムと同じ量子化）、false なら生の log2 輝度を使う。
    double ReferenceTrimmedMeanLog2(const Samples& samples, double lowPercent, double highPercent, bool bUseBinCenter)
    {
        double values[SampleCount];
        for (uint32_t index = 0u; index < SampleCount; ++index)
        {
            const float luminance = samples.Luminance[index];
            const uint32_t bin = AutoExposureLuminanceToBin(luminance);
            values[index] = bUseBinCenter ? static_cast<double>(AutoExposureBinCenterLog2Luminance(bin))
                                          : std::log2(static_cast<double>(luminance));
        }
        std::sort(std::begin(values), std::end(values));
        const uint32_t dropLow = static_cast<uint32_t>(std::lround(lowPercent * SampleCount));
        const uint32_t dropHigh = static_cast<uint32_t>(std::lround(highPercent * SampleCount));
        double sum = 0.0;
        uint32_t used = 0u;
        for (uint32_t index = dropLow; index < SampleCount - dropHigh; ++index)
        {
            sum += values[index];
            ++used;
        }
        return used > 0u ? sum / static_cast<double>(used) : 0.0;
    }

    // 範囲内の輝度で、暗部・中間・明るい外れ（空の太陽など）を混ぜた画素
    Samples MakeMixedScene(uint32_t seed)
    {
        Samples samples = {};
        Random random;
        random.State = seed;
        for (uint32_t index = 0u; index < SampleCount; ++index)
        {
            const float pick = random.NextUnit();
            float log2Luminance = 0.0f;
            if (pick < 0.15f)
            {
                log2Luminance = -6.0f + 4.0f * random.NextUnit(); // 影の奥
            }
            else if (pick < 0.97f)
            {
                log2Luminance = 8.0f + 5.0f * random.NextUnit(); // 日なたの地面・空
            }
            else
            {
                log2Luminance = 19.0f + 2.5f * random.NextUnit(); // 太陽の近く
            }
            samples.Luminance[index] = std::exp2(log2Luminance);
        }
        return samples;
    }

    void TestBinMapping()
    {
        Check(AutoExposureLuminanceToBin(0.0f) == 0u, "輝度0は区間0");
        Check(AutoExposureLuminanceToBin(-1.0f) == 0u, "負の輝度は区間0");
        Check(AutoExposureLuminanceToBin(std::numeric_limits<float>::quiet_NaN()) == 0u, "NaN は区間0");
        Check(AutoExposureLuminanceToBin(std::numeric_limits<float>::infinity()) == AutoExposureHistogramBinCount - 1u,
              "正の無限大は最後の区間");
        Check(AutoExposureLuminanceToBin(std::exp2(AutoExposureHistogramMinLog2Luminance - 3.0f)) == 0u,
              "下端より暗い輝度は区間0");
        Check(AutoExposureLuminanceToBin(std::exp2(AutoExposureHistogramMaxLog2Luminance + 3.0f)) ==
                  AutoExposureHistogramBinCount - 1u,
              "上端より明るい輝度は最後の区間");
        // 各区間の中央の輝度は、その区間へ戻る
        for (uint32_t bin = 0u; bin < AutoExposureHistogramBinCount; ++bin)
        {
            const float luminance = std::exp2(AutoExposureBinCenterLog2Luminance(bin));
            if (AutoExposureLuminanceToBin(luminance) != bin)
            {
                std::printf("失敗: 区間 %u の中央の輝度が別の区間 %u へ入る\n", bin, AutoExposureLuminanceToBin(luminance));
                ++GFailureCount;
            }
        }
        Check(IsNear(AutoExposureHistogramBinsPerLog2, 8.0, 1.0e-6), "log2 輝度1あたり8区間（32段を256区間）");
    }

    void TestHistogramMatchesReference()
    {
        AutoExposureSettings settings;
        settings.LowPercent = 0.10f;
        settings.HighPercent = 0.02f;
        settings.MinEV100 = -100.0f;
        settings.MaxEV100 = 100.0f;

        const uint32_t seeds[] = {0x12345678u, 0x9E3779B9u, 0x0BADF00Du};
        for (uint32_t seed : seeds)
        {
            const Samples samples = MakeMixedScene(seed);
            uint32_t bins[AutoExposureHistogramBinCount];
            BuildHistogram(samples, bins);
            const AutoExposureHistogramResult result =
                ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, settings);

            const double quantizedReference = ReferenceTrimmedMeanLog2(samples, 0.10, 0.02, true);
            const double rawReference = ReferenceTrimmedMeanLog2(samples, 0.10, 0.02, false);
            std::printf("seed=%08x avg_log2=%.6f 参照(区間中央)=%.6f 参照(生)=%.6f target_ev100=%.6f\n",
                        seed, result.AverageLog2Luminance, quantizedReference, rawReference, result.TargetEV100);

            Check(result.bValid, "混ぜた画素のヒストグラムは有効");
            Check(result.TotalCount == SampleCount, "ヒストグラムの総数は画素数");
            // 割合は float で持つので、画素数の端数に float の丸めが残る
            Check(IsNear(result.UsedCount, SampleCount * (1.0 - 0.10 - 0.02), 1.0e-3), "平均に使う画素は外れを除いた数");
            Check(IsNear(result.AverageLog2Luminance, quantizedReference, 1.0e-4),
                  "外れを除いた平均が、区間の中央へ丸めた素朴な参照と一致する");
            Check(IsNear(result.AverageLog2Luminance, rawReference, 0.5 / AutoExposureHistogramBinsPerLog2 + 1.0e-4),
                  "外れを除いた平均と生の log2 輝度の参照の差は区間の幅の半分以内");
            Check(IsNear(result.TargetEV100, result.AverageLog2Luminance + 3.0, 1.0e-5),
                  "目標の EV100 は log2(平均輝度 × 100 / 12.5)");

            // 外れを除かないと、太陽の近くの画素で平均が明るい側へ寄る
            AutoExposureSettings untrimmed = settings;
            untrimmed.LowPercent = 0.0f;
            untrimmed.HighPercent = 0.0f;
            const AutoExposureHistogramResult untrimmedResult =
                ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, untrimmed);
            Check(IsNear(untrimmedResult.AverageLog2Luminance, ReferenceTrimmedMeanLog2(samples, 0.0, 0.0, true), 1.0e-4),
                  "外れを除かない平均も素朴な参照と一致する");
        }
    }

    void TestFractionalCutInsideOneBin()
    {
        // 全画素が2つの区間にある。暗い区間 300 画素、明るい区間 700 画素。
        // 下位10%（100画素）は暗い区間から、上位2%（20画素）は明るい区間から落ちる。
        uint32_t bins[AutoExposureHistogramBinCount] = {};
        const uint32_t darkBin = 100u;
        const uint32_t brightBin = 180u;
        bins[darkBin] = 300u;
        bins[brightBin] = 700u;
        AutoExposureSettings settings;
        settings.MinEV100 = -100.0f;
        settings.MaxEV100 = 100.0f;
        const AutoExposureHistogramResult result =
            ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, settings);
        const double expected = (200.0 * AutoExposureBinCenterLog2Luminance(darkBin) +
                                 680.0 * AutoExposureBinCenterLog2Luminance(brightBin)) /
                                880.0;
        Check(result.bValid, "2区間のヒストグラムは有効");
        Check(IsNear(result.AverageLog2Luminance, expected, 1.0e-5), "境目の区間は範囲に入る画素の数だけ数える");
    }

    void TestUniformSceneAndSettings()
    {
        // 一様な 1000 cd/m² の画面。EV100 は log2(1000 × 100 / 12.5) ≈ 12.97。
        uint32_t bins[AutoExposureHistogramBinCount] = {};
        const float luminance = 1000.0f;
        const uint32_t bin = AutoExposureLuminanceToBin(luminance);
        bins[bin] = 4096u;

        AutoExposureSettings settings;
        const AutoExposureHistogramResult result =
            ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, settings);
        const double expectedEV100 = std::log2(1000.0 * 100.0 / 12.5);
        Check(result.bValid, "一様な画面は有効");
        Check(IsNear(result.TargetEV100, expectedEV100, 0.5 / AutoExposureHistogramBinsPerLog2 + 1.0e-4),
              "一様な画面の目標 EV100 は log2(L × 100 / 12.5) に区間の半分以内で一致");

        AutoExposureSettings brighter = settings;
        brighter.ExposureCompensation = 1.5f;
        const AutoExposureHistogramResult compensated =
            ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, brighter);
        Check(IsNear(compensated.TargetEV100, result.TargetEV100 - 1.5, 1.0e-5), "露出補正 +1.5 EV で目標の EV100 が1.5下がる");

        AutoExposureSettings clamped = settings;
        clamped.MaxEV100 = 10.0f;
        Check(IsNear(ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, clamped).TargetEV100, 10.0, 1.0e-6),
              "目標の EV100 は上限で止まる");
        clamped.MaxEV100 = 18.0f;
        clamped.MinEV100 = 15.0f;
        Check(IsNear(ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, clamped).TargetEV100, 15.0, 1.0e-6),
              "目標の EV100 は下限で止まる");
        clamped.MinEV100 = 11.0f;
        clamped.MaxEV100 = 9.0f;
        Check(IsNear(ComputeAutoExposureFromHistogram(bins, AutoExposureHistogramBinCount, clamped).TargetEV100, 11.0, 1.0e-6),
              "下限と上限が逆でも入れ替えて止める");

        uint32_t empty[AutoExposureHistogramBinCount] = {};
        Check(!ComputeAutoExposureFromHistogram(empty, AutoExposureHistogramBinCount, settings).bValid,
              "空のヒストグラムは無効");
        Check(!ComputeAutoExposureFromHistogram(nullptr, AutoExposureHistogramBinCount, settings).bValid,
              "ヒストグラムが無いときは無効");
    }

    // dE/dt = -speed × (E - target) を前進オイラー法で細かく積分する参照
    double IntegrateReference(double current, double target, double seconds, double speedBrighten, double speedDarken)
    {
        const uint32_t substeps = 200000u;
        const double h = seconds / static_cast<double>(substeps);
        for (uint32_t step = 0u; step < substeps; ++step)
        {
            const double speed = target < current ? speedBrighten : speedDarken;
            current += -speed * (current - target) * h;
        }
        return current;
    }

    void TestAdaptationMatchesReference()
    {
        AutoExposureSettings settings;
        settings.SpeedBrighten = 1.0f;
        settings.SpeedDarken = 3.0f;

        // 暗くなる向き（EV100 が上がる）: 可変のタイムステップを重ねても、閉じた式・積分の参照と一致する
        {
            const float deltas[] = {1.0f / 60.0f, 1.0f / 30.0f, 0.05f, 1.0f / 144.0f, 0.1f, 1.0f / 60.0f};
            float ev100 = 8.0f;
            const float target = 14.0f;
            double elapsed = 0.0;
            for (uint32_t repeat = 0u; repeat < 5u; ++repeat)
            {
                for (float delta : deltas)
                {
                    ev100 = AdaptAutoExposureEV100(ev100, target, delta, settings);
                    elapsed += delta;
                }
            }
            const double closedForm = 14.0 + (8.0 - 14.0) * std::exp(-3.0 * elapsed);
            const double integrated = IntegrateReference(8.0, 14.0, elapsed, 1.0, 3.0);
            std::printf("暗くなる向き elapsed=%.4f ev100=%.6f 閉じた式=%.6f 積分=%.6f\n", elapsed, ev100, closedForm, integrated);
            Check(IsNear(ev100, closedForm, 1.0e-4), "暗くなる向きの順応が閉じた式と一致する");
            Check(IsNear(ev100, integrated, 1.0e-3), "暗くなる向きの順応が微分方程式の積分と一致する");
        }

        // 明るくなる向き（EV100 が下がる）は SpeedBrighten で遅く追う
        {
            float ev100 = 14.0f;
            const float target = 8.0f;
            for (uint32_t frame = 0u; frame < 60u; ++frame)
            {
                ev100 = AdaptAutoExposureEV100(ev100, target, 1.0f / 60.0f, settings);
            }
            const double closedForm = 8.0 + (14.0 - 8.0) * std::exp(-1.0 * 1.0);
            const double integrated = IntegrateReference(14.0, 8.0, 1.0, 1.0, 3.0);
            std::printf("明るくなる向き 1秒後 ev100=%.6f 閉じた式=%.6f 積分=%.6f\n", ev100, closedForm, integrated);
            Check(IsNear(ev100, closedForm, 1.0e-4), "明るくなる向きの順応が閉じた式と一致する");
            Check(IsNear(ev100, integrated, 1.0e-3), "明るくなる向きの順応が微分方程式の積分と一致する");
        }

        // 同じ差でも、暗くなる向きのほうが速く縮む
        {
            const float up = AdaptAutoExposureEV100(10.0f, 12.0f, 0.1f, settings);
            const float down = AdaptAutoExposureEV100(10.0f, 8.0f, 0.1f, settings);
            Check((12.0f - up) < (down - 8.0f), "暗くなる向きは明るくなる向きより速い");
        }

        // タイムステップの分け方によらない（1ステップ 0.5 秒 = 30 ステップ × 1/60 秒）
        {
            const float single = AdaptAutoExposureEV100(6.0f, 12.0f, 0.5f, settings);
            float split = 6.0f;
            for (uint32_t step = 0u; step < 30u; ++step)
            {
                split = AdaptAutoExposureEV100(split, 12.0f, 1.0f / 60.0f, settings);
            }
            Check(IsNear(single, split, 1.0e-4), "同じ向きなら刻みの分け方によらない");
        }

        // 目標を越えず、振動しない
        {
            float ev100 = 0.0f;
            float previous = ev100;
            bool bMonotonic = true;
            for (uint32_t frame = 0u; frame < 600u; ++frame)
            {
                ev100 = AdaptAutoExposureEV100(ev100, 12.0f, 1.0f / 30.0f, settings);
                bMonotonic = bMonotonic && ev100 >= previous && ev100 <= 12.0f;
                previous = ev100;
            }
            Check(bMonotonic, "目標へ単調に近づき、越えない");
            Check(IsNear(ev100, 12.0, 1.0e-3), "20秒後には目標に落ち着く");
        }

        // 無効なタイムステップでは変えない
        Check(AdaptAutoExposureEV100(9.0f, 12.0f, 0.0f, settings) == 9.0f, "dt=0 では変えない");
        Check(AdaptAutoExposureEV100(9.0f, 12.0f, -0.1f, settings) == 9.0f, "負の dt では変えない");
        Check(AdaptAutoExposureEV100(9.0f, 12.0f, std::numeric_limits<float>::quiet_NaN(), settings) == 9.0f,
              "NaN の dt では変えない");
        Check(AdaptAutoExposureEV100(9.0f, std::numeric_limits<float>::quiet_NaN(), 0.1f, settings) == 9.0f,
              "NaN の目標では変えない");
    }

    void TestAdaptationState()
    {
        AutoExposureSettings settings;
        AutoExposureAdaptationState state;

        AutoExposureHistogramResult invalid;
        UpdateAutoExposureAdaptation(state, invalid, 0.1f, settings);
        Check(!state.bValid, "無効な測定では状態を作らない");

        AutoExposureHistogramResult first;
        first.bValid = true;
        first.TargetEV100 = 13.0f;
        UpdateAutoExposureAdaptation(state, first, 0.0f, settings);
        Check(state.bValid && state.EV100 == 13.0f, "最初の有効な測定では目標へそのまま合わせる");

        AutoExposureHistogramResult second;
        second.bValid = true;
        second.TargetEV100 = 7.0f;
        UpdateAutoExposureAdaptation(state, second, 0.25f, settings);
        const double expected = 7.0 + (13.0 - 7.0) * std::exp(-static_cast<double>(settings.SpeedBrighten) * 0.25);
        Check(IsNear(state.EV100, expected, 1.0e-4), "2回目からは順応の式で進む");

        const float before = state.EV100;
        UpdateAutoExposureAdaptation(state, invalid, 0.25f, settings);
        Check(state.EV100 == before, "無効な測定では前の露出を保つ");
    }

    void TestPreExposureFromEV100()
    {
        // 手動露出（CameraComponent）と同じ式 2^(-EV100) / 1.2
        for (float ev100 : {-4.0f, 0.0f, 9.9068906f, 14.6f, 18.0f})
        {
            const double expected = std::exp2(-static_cast<double>(ev100)) / 1.2;
            Check(IsNear(AutoExposurePreExposureFromEV100(ev100) / expected, 1.0, 1.0e-6),
                  "EV100 から露出への式が手動露出と同じ");
        }
        Check(AutoExposurePreExposureFromEV100(std::numeric_limits<float>::quiet_NaN()) == 0.0f,
              "NaN の EV100 では露出 0（手動の露出を使い続ける）");

        // 一様な輝度の画面では、目標の露出を掛けた値が 1 / (1.2 × 8) ≒ 0.104 になる（中間の灰）
        AutoExposureSettings settings;
        const float luminance = 3000.0f;
        const float averageLog2 = AutoExposureBinCenterLog2Luminance(AutoExposureLuminanceToBin(luminance));
        const float target = AutoExposureTargetEV100FromAverageLog2Luminance(averageLog2, settings);
        const double exposed = std::exp2(static_cast<double>(averageLog2)) *
                               static_cast<double>(AutoExposurePreExposureFromEV100(target));
        std::printf("一様な画面の露出後の値=%.5f\n", exposed);
        Check(IsNear(exposed, 1.0 / 9.6, 1.0e-4), "一様な画面は露出後に中間の灰（約0.104）になる");
    }

    // 閉ループの模擬: 露出は前のフレームまでに順応させた EV100 から決め、各フレームの SceneColor
    // （絶対輝度 × そのフレームの露出）のヒストグラムを、そのフレームの露出で割ってから数える。
    // 測定は delayFrames フレーム後に読み戻して順応へ使う（フレームスロットの読み戻しの遅れ）。
    struct ClosedLoopResult
    {
        double SettleSeconds = -1.0;
        double MaxOvershootEV = 0.0;
        uint32_t Reversals = 0u;
        float FinalEV100 = 0.0f;
        float FinalTargetEV100 = 0.0f;
    };

    ClosedLoopResult SimulateClosedLoop(float startScale,
                                        float stepScale,
                                        double stepTime,
                                        double frameSeconds,
                                        uint32_t delayFrames,
                                        const AutoExposureSettings& settings)
    {
        // 空・日向・日陰・暗部の混ざった画面（相対輝度）
        const float relative[] = {0.02f, 0.05f, 0.1f, 0.2f, 0.35f, 0.5f, 0.8f, 1.0f, 1.5f, 3.0f, 6.0f, 40.0f};
        constexpr uint32_t pixelsPerValue = 1000u;

        struct PendingMeasurement
        {
            uint32_t Bins[AutoExposureHistogramBinCount] = {};
            double Time = 0.0;
        };
        constexpr uint32_t maxDelay = 8u;
        PendingMeasurement ring[maxDelay] = {};
        const uint32_t delay = std::min(std::max(delayFrames, 1u), maxDelay);

        AutoExposureAdaptationState state;
        double lastAdaptationTime = 0.0;
        // 最初の測定までの手動露出
        const float manualEV100 = 12.0f;

        // 急変の後の露出の記録（落ち着くまでの時間と目標を越えた量を、最後の目標に対して後で求める）
        constexpr uint32_t maxHistory = 2048u;
        double historyTime[maxHistory] = {};
        float historyEV100[maxHistory] = {};
        uint32_t historyCount = 0u;

        ClosedLoopResult result;
        float lastStep = 0.0f;
        bool bAfterStep = false;
        double direction = 0.0;
        const double totalSeconds = stepTime + 6.0;
        const uint32_t frameCount = static_cast<uint32_t>(totalSeconds / frameSeconds);
        for (uint32_t frame = 0u; frame < frameCount; ++frame)
        {
            const double time = static_cast<double>(frame) * frameSeconds;
            PendingMeasurement& slot = ring[frame % delay];
            // このスロットの前の測定（delay フレーム前）を読み、順応させる
            if (frame >= delay)
            {
                const AutoExposureHistogramResult measured =
                    ComputeAutoExposureFromHistogram(slot.Bins, AutoExposureHistogramBinCount, settings);
                const bool bHadState = state.bValid;
                const float before = state.EV100;
                const float deltaSeconds = bHadState ? static_cast<float>(slot.Time - lastAdaptationTime) : 0.0f;
                UpdateAutoExposureAdaptation(state, measured, deltaSeconds, settings);
                lastAdaptationTime = slot.Time;
                if (bAfterStep && bHadState)
                {
                    const float step = state.EV100 - before;
                    if (std::fabs(step) >= 1.0e-4f)
                    {
                        if (lastStep != 0.0f && (step > 0.0f) != (lastStep > 0.0f))
                        {
                            ++result.Reversals;
                        }
                        lastStep = step;
                    }
                    if (historyCount < maxHistory)
                    {
                        historyTime[historyCount] = time;
                        historyEV100[historyCount] = state.EV100;
                        ++historyCount;
                    }
                }
                result.FinalTargetEV100 = measured.TargetEV100;
            }
            const float appliedEV100 = state.bValid ? state.EV100 : manualEV100;

            // このフレームの露出で描き、同じ露出で割って数える
            const float exposure = AutoExposurePreExposureFromEV100(appliedEV100);
            const bool bStepped = time >= stepTime;
            if (bStepped && !bAfterStep)
            {
                bAfterStep = true;
                direction = stepScale > startScale ? 1.0 : -1.0;
            }
            const float scale = bStepped ? stepScale : startScale;
            std::fill(std::begin(slot.Bins), std::end(slot.Bins), 0u);
            for (float value : relative)
            {
                const float sceneColor = value * scale * exposure;
                const float absolute = sceneColor * (1.0f / exposure);
                slot.Bins[AutoExposureLuminanceToBin(absolute)] += pixelsPerValue;
            }
            slot.Time = time;
        }
        result.FinalEV100 = state.EV100;

        // 最後に 0.1 EV より離れていた時刻の次の測定を、落ち着いた時刻とする
        double lastFarTime = stepTime;
        for (uint32_t index = 0u; index < historyCount; ++index)
        {
            const double distance = static_cast<double>(historyEV100[index]) - static_cast<double>(result.FinalTargetEV100);
            result.MaxOvershootEV = std::max(result.MaxOvershootEV, distance * direction);
            if (std::fabs(distance) >= 0.1)
            {
                lastFarTime = historyTime[index];
            }
        }
        result.SettleSeconds = lastFarTime + frameSeconds - stepTime;
        return result;
    }

    void TestClosedLoopSunStep()
    {
        const AutoExposureSettings settings;
        // 昼（明るい）→夕（約4.6 EV 暗い）と、その逆。30 fps・60 fps、読み戻しの遅れ 2〜3 フレーム。
        const float noon = 20000.0f;
        const float dusk = noon * static_cast<float>(std::exp2(-4.6));
        struct Case
        {
            const char* Name;
            float From;
            float To;
            double FrameSeconds;
            uint32_t Delay;
        };
        const Case cases[] = {
            {"昼→夕 30fps 遅れ3", noon, dusk, 1.0 / 30.0, 3u},
            {"昼→夕 60fps 遅れ2", noon, dusk, 1.0 / 60.0, 2u},
            {"夕→昼 30fps 遅れ3", dusk, noon, 1.0 / 30.0, 3u},
            {"夕→昼 60fps 遅れ2", dusk, noon, 1.0 / 60.0, 2u},
        };
        for (const Case& testCase : cases)
        {
            const ClosedLoopResult result =
                SimulateClosedLoop(testCase.From, testCase.To, 2.0, testCase.FrameSeconds, testCase.Delay, settings);
            std::printf("%s settle_s=%.3f max_overshoot_ev=%.4f reversals=%u final_ev100=%.3f target_ev100=%.3f\n",
                        testCase.Name,
                        result.SettleSeconds,
                        result.MaxOvershootEV,
                        result.Reversals,
                        result.FinalEV100,
                        result.FinalTargetEV100);
            Check(result.SettleSeconds >= 1.0 && result.SettleSeconds <= 3.0, "太陽の急変から1秒以上3秒以内で落ち着く");
            Check(result.MaxOvershootEV <= 1.0e-3, "目標を越えない");
            Check(result.Reversals == 0u, "順応の向きが反転しない（振動しない）");
            Check(IsNear(result.FinalEV100, result.FinalTargetEV100, 1.0e-3), "最後は目標に一致する");
        }
    }
} // namespace

int main()
{
    TestBinMapping();
    TestHistogramMatchesReference();
    TestFractionalCutInsideOneBin();
    TestUniformSceneAndSettings();
    TestAdaptationMatchesReference();
    TestAdaptationState();
    TestPreExposureFromEV100();
    TestClosedLoopSunStep();

    if (GFailureCount != 0)
    {
        std::printf("AutoExposureMathTest: 失敗 %d 件\n", GFailureCount);
        return 1;
    }
    std::printf("AutoExposureMathTest passed\n");
    return 0;
}

// 風の正規化、向き、長時間平均、振れ幅と決定性の契約を CPU で反証する。
#include "Rendering/Wind.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Rendering;
namespace Math = NorvesLib::Math;

namespace
{
    int g_FailureCount = 0;

    void Expect(bool bCondition, const char* message)
    {
        if (!bCondition)
        {
            ++g_FailureCount;
            std::cout << "失敗: " << message << "\n";
        }
    }

    struct WindField
    {
        const char* Name;
        float WindParameters::* Member;
        float BelowRange;
        float AboveRange;
    };

    constexpr WindField Fields[] = {
        {"DirectionYawDegrees", &WindParameters::DirectionYawDegrees, 0.0f, 0.0f},
        {"BaseSpeed", &WindParameters::BaseSpeed, -0.1f, 100.1f},
        {"GustStrength", &WindParameters::GustStrength, -0.1f, 1.5f},
        {"GustFrequencyHz", &WindParameters::GustFrequencyHz, 0.0f, 20.0f},
        {"GustScaleMeters", &WindParameters::GustScaleMeters, 0.01f, 10001.0f},
        {"Turbulence", &WindParameters::Turbulence, -0.1f, 1.1f},
    };

    WindParameters MakeDistinctParameters()
    {
        WindParameters p;
        p.bEnabled = true;
        p.DirectionYawDegrees = 123.0f;
        p.BaseSpeed = 7.5f;
        p.GustStrength = 0.625f;
        p.GustFrequencyHz = 0.25f;
        p.GustScaleMeters = 17.0f;
        p.Turbulence = 0.375f;
        return p;
    }

    void ExpectParameters(const WindParameters& actual, const WindParameters& expected,
                          const char* changedField, float input)
    {
        Expect(actual.bEnabled == expected.bEnabled, "正規化は有効フラグを保持する");
        for (const WindField& field : Fields)
        {
            if (actual.*(field.Member) != expected.*(field.Member))
            {
                ++g_FailureCount;
                std::cout << "失敗: Sanitize input_field=" << changedField << " input=" << input
                          << " output_field=" << field.Name << " actual=" << actual.*(field.Member)
                          << " expected=" << expected.*(field.Member) << "\n";
            }
        }
    }

    void TestSanitize()
    {
        const WindParameters defaults = MakeDefaultWindParameters();
        Expect(!defaults.bEnabled && defaults.DirectionYawDegrees == 0.0f && defaults.BaseSpeed == 3.0f &&
                   defaults.GustStrength == 0.4f && defaults.GustFrequencyHz == 0.125f &&
                   defaults.GustScaleMeters == 20.0f && defaults.Turbulence == 0.15f,
               "既定値は共通の仕様に一致する");
        const WindParameters baseline = MakeDistinctParameters();
        const float nonFinite[] = {std::numeric_limits<float>::quiet_NaN(),
                                   std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity()};
        int invalidCaseCount = 0;
        for (const WindField& field : Fields)
        {
            auto checkInvalid = [&](float value)
            {
                WindParameters input = baseline;
                WindParameters expected = baseline;
                input.*(field.Member) = value;
                expected.*(field.Member) = defaults.*(field.Member);
                ExpectParameters(SanitizeWindParameters(input), expected, field.Name, value);
                ++invalidCaseCount;
            };
            for (const float value : nonFinite)
            {
                checkInvalid(value);
            }
            if (field.Member != &WindParameters::DirectionYawDegrees)
            {
                checkInvalid(field.BelowRange);
                checkInvalid(field.AboveRange);
            }
        }

        const float wrappedYaws[] = {370.0f, -90.0f};
        for (const float yaw : wrappedYaws)
        {
            WindParameters input = baseline;
            WindParameters expected = baseline;
            input.DirectionYawDegrees = yaw;
            expected.DirectionYawDegrees = yaw == 370.0f ? 10.0f : 270.0f;
            ExpectParameters(SanitizeWindParameters(input), expected, "DirectionYawDegrees", yaw);
        }
        const float frequencies[] = {0.00001f, 1.0f / 4096.0f, 0.1234f, 0.125f, 10.0f};
        for (const float frequency : frequencies)
        {
            WindParameters input = baseline;
            input.GustFrequencyHz = frequency;
            const WindParameters actual = SanitizeWindParameters(input);
            WindParameters expected = baseline;
            const float rounded = std::round(frequency * 4096.0f) / 4096.0f;
            expected.GustFrequencyHz = rounded == 0.0f ? 1.0f / 4096.0f : rounded;
            ExpectParameters(actual, expected, "GustFrequencyHz", frequency);
            Expect(actual.GustFrequencyHz > 0.0f &&
                       std::floor(actual.GustFrequencyHz * 4096.0f) == actual.GustFrequencyHz * 4096.0f,
                   "正規化した周波数は正の 1/4096 Hz の整数倍になる");
        }
        WindParameters boundary = baseline;
        boundary.bEnabled = false;
        boundary.BaseSpeed = 0.0f;
        boundary.GustStrength = 0.0f;
        boundary.GustScaleMeters = 0.1f;
        boundary.Turbulence = 0.0f;
        ExpectParameters(SanitizeWindParameters(boundary), boundary, "lower_boundary", 0.0f);
        boundary.BaseSpeed = 100.0f;
        boundary.GustStrength = 1.0f;
        boundary.GustScaleMeters = 10000.0f;
        boundary.Turbulence = 1.0f;
        ExpectParameters(SanitizeWindParameters(boundary), boundary, "upper_boundary", 1.0f);
        std::cout << "Sanitize: invalid_cases=" << invalidCaseCount
                  << " yaw=(370->10,-90->270) frequency_quantum=" << 1.0f / 4096.0f << "\n";
    }

    void TestDirectionAndConstantWind()
    {
        constexpr float tolerance = 1.0e-6f;
        const float yaws[] = {0.0f, 37.0f, 90.0f, 137.0f, 180.0f, 270.0f, 359.0f};
        float maxLengthError = 0.0f;
        float maxVelocityError = 0.0f;
        for (const float yaw : yaws)
        {
            WindParameters p = MakeDistinctParameters();
            p.DirectionYawDegrees = yaw;
            p.GustStrength = 0.0f;
            p.Turbulence = 0.0f;
            float dirX = 0.0f;
            float dirZ = 0.0f;
            WindDirectionXZ(p, dirX, dirZ);
            const float lengthError = std::abs(std::sqrt(dirX * dirX + dirZ * dirZ) - 1.0f);
            maxLengthError = lengthError > maxLengthError ? lengthError : maxLengthError;
            Expect(std::isfinite(lengthError) && lengthError <= tolerance, "風向は単位ベクトルになる");
            if (yaw == 0.0f || yaw == 90.0f)
            {
                const float expectedX = yaw == 0.0f ? 1.0f : 0.0f;
                const float expectedZ = yaw == 0.0f ? 0.0f : 1.0f;
                Expect(std::abs(dirX - expectedX) <= tolerance && std::abs(dirZ - expectedZ) <= tolerance,
                       "yaw 0 は +X、yaw 90 は +Z に向く");
            }
            const Math::Vector3 velocity = EvaluateWind(p, Math::Vector3(-21.0f, 9.0f, 13.0f), 123.75f);
            const float errorX = std::abs(velocity.x - dirX * p.BaseSpeed);
            const float errorZ = std::abs(velocity.z - dirZ * p.BaseSpeed);
            const float error = errorX > errorZ ? errorX : errorZ;
            maxVelocityError = error > maxVelocityError ? error : maxVelocityError;
            Expect(std::isfinite(error) && error <= tolerance && velocity.y == 0.0f,
                   "突風と乱流がゼロなら風向と平均風速だけで速度が決まる");
        }
        std::cout << "Direction: max_length_error=" << maxLengthError << " tolerance=" << tolerance
                  << " max_velocity_error=" << maxVelocityError << "\n";
    }

    void TestLongTimeMean()
    {
        WindParameters p = MakeDistinctParameters();
        p.DirectionYawDegrees = 37.0f;
        p.GustStrength = 0.6f;
        p.Turbulence = 0.35f;
        p.GustFrequencyHz = 8.0f;
        constexpr int periodCount = 32768;
        constexpr int samplesPerPeriod = 32;
        constexpr int sampleCount = periodCount * samplesPerPeriod;
        // 値ノイズは周期的な波ではなく、1/f 秒ごとに隣のハッシュ値へ進む。
        // 一様な節点値を独立標本とみなすと、[-1,1] の平均の標準誤差は 1/sqrt(3N)。
        // 方向成分の相対誤差の 5σ は 0.6/sqrt(3*32768)*5 = 約0.957%。
        // 32点/区間の積分と float の丸めの余裕を含め、許容を1%とする。
        // 乱流は3.1倍の節点数を通るので、横成分にも同じ1%（BaseSpeed比）を使える。
        // 時計を途中で巻き戻さず、4096秒にわたる異なる32768区間を測る。
        constexpr double relativeTolerance = 0.01;
        const Math::Vector3 position(-31.25f, 2.0f, 76.5f);
        float dirX = 0.0f;
        float dirZ = 0.0f;
        WindDirectionXZ(p, dirX, dirZ);
        double forwardSum = 0.0;
        double sideSum = 0.0;
        for (int sample = 0; sample < sampleCount; ++sample)
        {
            const float time = (static_cast<float>(sample) + 0.5f) /
                               (p.GustFrequencyHz * static_cast<float>(samplesPerPeriod));
            const Math::Vector3 velocity = EvaluateWind(p, position, time);
            forwardSum += static_cast<double>(velocity.x) * dirX + static_cast<double>(velocity.z) * dirZ;
            sideSum += -static_cast<double>(velocity.x) * dirZ + static_cast<double>(velocity.z) * dirX;
        }
        const double forwardMean = forwardSum / sampleCount;
        const double sideMean = sideSum / sampleCount;
        const double relativeError = std::abs(forwardMean - p.BaseSpeed) / p.BaseSpeed;
        const double relativeSide = std::abs(sideMean) / p.BaseSpeed;
        Expect(std::isfinite(relativeError) && relativeError <= relativeTolerance,
               "長時間の風向成分の平均は BaseSpeed に一致する");
        Expect(std::isfinite(relativeSide) && relativeSide <= relativeTolerance,
               "長時間の直交成分の平均はほぼゼロになる");
        std::cout << "Mean: periods=" << periodCount << " samples=" << sampleCount
                  << " forward=" << forwardMean << " base_speed=" << p.BaseSpeed
                  << " relative_error=" << relativeError << " side=" << sideMean
                  << " relative_side=" << relativeSide << " relative_tolerance=" << relativeTolerance << "\n";
    }

    void TestBoundsAndDeterminism()
    {
        const float yaws[] = {0.0f, 37.0f, 90.0f, 213.0f};
        int sampleCount = 0;
        int boundFailureCount = 0;
        int deterministicFailureCount = 0;
        int disabledFailureCount = 0;
        for (int configuration = 0; configuration < 4; ++configuration)
        {
            WindParameters p = MakeDistinctParameters();
            p.DirectionYawDegrees = yaws[configuration];
            p.BaseSpeed = configuration == 1 ? 100.0f : (configuration == 2 ? 0.0f : 7.5f);
            p.GustStrength = configuration == 3 ? 0.0f : (configuration == 0 ? 0.625f : 1.0f);
            p.Turbulence = configuration == 3 ? 0.0f : (configuration == 0 ? 0.375f : 1.0f);
            float dirX = 0.0f;
            float dirZ = 0.0f;
            WindDirectionXZ(p, dirX, dirZ);
            const float lower = p.BaseSpeed * (1.0f - p.GustStrength);
            const float upper = p.BaseSpeed * (1.0f + p.GustStrength);
            const float sideBound = p.BaseSpeed * p.Turbulence;
            // 合成と射影の数回の演算の丸めだけを許す（最大風速でも約0.0002 m/s）。
            const float tolerance = 16.0f * std::numeric_limits<float>::epsilon() *
                                    (p.BaseSpeed > 1.0f ? p.BaseSpeed : 1.0f);
            float minForward = std::numeric_limits<float>::max();
            float maxForward = std::numeric_limits<float>::lowest();
            float maxAbsSide = 0.0f;
            for (int x = -8; x <= 8; ++x)
            {
                for (int z = -6; z <= 6; ++z)
                {
                    const Math::Vector3 position(x * 13.25f, 4.0f, z * 21.75f);
                    for (int sample = 0; sample < 128; ++sample)
                    {
                        const float time = sample * 31.9375f;
                        const Math::Vector3 first = EvaluateWind(p, position, time);
                        const Math::Vector3 second = EvaluateWind(p, position, time);
                        // Vector3 のアラインメント用領域は比べず、返された3成分のビットを比較する。
                        const float firstComponents[] = {first.x, first.y, first.z};
                        const float secondComponents[] = {second.x, second.y, second.z};
                        if (std::memcmp(firstComponents, secondComponents, sizeof(firstComponents)) != 0)
                        {
                            ++deterministicFailureCount;
                        }
                        const float forward = first.x * dirX + first.z * dirZ;
                        const float side = -first.x * dirZ + first.z * dirX;
                        if (!std::isfinite(forward) || !std::isfinite(side) || first.y != 0.0f ||
                            forward < lower - tolerance || forward > upper + tolerance ||
                            std::abs(side) > sideBound + tolerance)
                        {
                            ++boundFailureCount;
                        }
                        minForward = forward < minForward ? forward : minForward;
                        maxForward = forward > maxForward ? forward : maxForward;
                        maxAbsSide = std::abs(side) > maxAbsSide ? std::abs(side) : maxAbsSide;
                        WindParameters disabled = p;
                        disabled.bEnabled = false;
                        const Math::Vector3 zero = EvaluateWind(disabled, position, time);
                        if (zero.x != 0.0f || zero.y != 0.0f || zero.z != 0.0f)
                        {
                            ++disabledFailureCount;
                        }
                        ++sampleCount;
                    }
                }
            }
            std::cout << "Bounds: configuration=" << configuration << " forward_min=" << minForward
                      << " forward_max=" << maxForward << " expected_min=" << lower << " expected_max=" << upper
                      << " side_abs_max=" << maxAbsSide << " side_bound=" << sideBound
                      << " rounding_tolerance=" << tolerance << "\n";
        }
        Expect(boundFailureCount == 0, "すべての位置と時刻で突風と乱流が振れ幅を超えない");
        Expect(deterministicFailureCount == 0, "同じ位置と時刻の速度はビット単位で一致する");
        Expect(disabledFailureCount == 0, "無効な風はゼロベクトルを返す");
        Expect(WindClockSeconds(4096.0 + 1.5) == 1.5f, "風の時計は4096秒で折り返す");
        std::cout << "Samples: count=" << sampleCount << " bounds_failures=" << boundFailureCount
                  << " determinism_failures=" << deterministicFailureCount
                  << " disabled_failures=" << disabledFailureCount << " clock=" << WindClockSeconds(4097.5) << "\n";
    }
}

int main()
{
    TestSanitize();
    TestDirectionAndConstantWind();
    TestLongTimeMean();
    TestBoundsAndDeterminism();
    if (g_FailureCount != 0)
    {
        std::cout << "失敗数: " << g_FailureCount << "\n";
        return 1;
    }
    std::cout << "WindParametersTest: 風の CPU 契約を確認しました\n";
    return 0;
}

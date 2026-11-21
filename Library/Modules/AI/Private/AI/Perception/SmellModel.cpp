// 水平風を平滑化し、風下の煙と風上への拡散から鼻に届く濃さを求める。
#include "AI/Perception/SmellModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::AI::Perception
{
    namespace
    {
        bool IsFiniteVector(const Math::Vector3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        bool IsValidTuning(const SmellTuning& tuning)
        {
            return std::isfinite(tuning.DownwindLengthMeters) && tuning.DownwindLengthMeters > 0.0f &&
                   std::isfinite(tuning.UpwindLengthMeters) && tuning.UpwindLengthMeters > 0.0f &&
                   std::isfinite(tuning.UpwindFactor) && tuning.UpwindFactor >= 0.0f &&
                   std::isfinite(tuning.UpwindAxisRatio) && tuning.UpwindAxisRatio >= 0.0f &&
                   std::isfinite(tuning.CrossSigmaBaseMeters) && tuning.CrossSigmaBaseMeters > 0.0f &&
                   std::isfinite(tuning.CrossSigmaSpreadPerMeter) && tuning.CrossSigmaSpreadPerMeter >= 0.0f &&
                   std::isfinite(tuning.CalmWindSpeed) && tuning.CalmWindSpeed >= 0.0f &&
                   std::isfinite(tuning.FullWindSpeed) && tuning.FullWindSpeed > tuning.CalmWindSpeed &&
                   std::isfinite(tuning.CalmLengthMeters) && tuning.CalmLengthMeters > 0.0f &&
                   std::isfinite(tuning.WindFilterSeconds);
        }

        float ToFiniteFloat(double value)
        {
            constexpr double limit = std::numeric_limits<float>::max();
            return static_cast<float>(std::clamp(value, -limit, limit));
        }
    }

    void WindDirectionFilter::Reset()
    {
        Filtered = {};
        bInitialized = false;
    }

    Math::Vector3 WindDirectionFilter::Update(const Math::Vector3& wind, float deltaSeconds, float timeConstantSeconds)
    {
        if (!IsFiniteVector(wind) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f ||
            !std::isfinite(timeConstantSeconds))
        {
            return Filtered;
        }

        if (!bInitialized || timeConstantSeconds <= 0.0f)
        {
            Filtered = {wind.x, 0.0f, wind.z};
            bInitialized = true;
        }
        else
        {
            // 短い刻みでも 1 − exp(−dt/τ) の有効桁を保つ。
            const double alpha = -std::expm1(-static_cast<double>(deltaSeconds) / timeConstantSeconds);
            Filtered = {static_cast<float>(std::lerp<double>(Filtered.x, wind.x, alpha)), 0.0f,
                        static_cast<float>(std::lerp<double>(Filtered.z, wind.z, alpha))};
        }
        return Filtered;
    }

    SmellResult EvaluateSmell(const SmellQuery& query, const SmellTuning& tuning)
    {
        SmellResult result;
        if (!IsFiniteVector(query.Source) || !IsFiniteVector(query.Receiver) || !IsFiniteVector(query.FilteredWind) ||
            !std::isfinite(query.Strength) || query.Strength <= 0.0f || !IsValidTuning(tuning))
        {
            return result;
        }

        const double dx = static_cast<double>(query.Receiver.x) - query.Source.x;
        const double dz = static_cast<double>(query.Receiver.z) - query.Source.z;
        const double distance = std::hypot(dx, dz);
        const double isotropic = query.Strength * std::exp(-distance / tuning.CalmLengthMeters);
        const double windX = query.FilteredWind.x;
        const double windZ = query.FilteredWind.z;
        const double speed = std::hypot(windX, windZ);
        if (speed <= tuning.CalmWindSpeed)
        {
            result.Concentration = static_cast<float>(isotropic);
            return result;
        }

        const double windRange = static_cast<double>(tuning.FullWindSpeed) - tuning.CalmWindSpeed;
        const double t = std::min(1.0, (speed - tuning.CalmWindSpeed) / windRange);
        const double weight = t * t * (3.0 - 2.0 * t);
        const double along = (dx * windX + dz * windZ) / speed;
        // 風と直交する水平単位ベクトルへの射影で、風の軸からの距離を得る。
        const double cross = std::abs(dx * windZ - dz * windX) / speed;
        const double back = static_cast<double>(query.Strength) * tuning.UpwindFactor *
                            std::exp(-distance / tuning.UpwindLengthMeters);
        double directional;
        if (along >= 0.0)
        {
            const double sigma = tuning.CrossSigmaBaseMeters + tuning.CrossSigmaSpreadPerMeter * along;
            const double crossInSigmas = cross / sigma;
            const double plume = query.Strength * std::exp(-along / tuning.DownwindLengthMeters -
                                                          0.5 * crossInSigmas * crossInSigmas);
            directional = std::max(plume, back);
        }
        else
        {
            const double axisFraction = std::min(1.0, -along / distance);
            directional = back * std::lerp(1.0, static_cast<double>(tuning.UpwindAxisRatio), axisFraction);
        }

        result.Concentration = ToFiniteFloat(std::lerp(isotropic, directional, weight));
        result.AlongWindMeters = ToFiniteFloat(along);
        result.CrossWindMeters = ToFiniteFloat(cross);
        result.WindWeight = static_cast<float>(weight);
        return result;
    }
}

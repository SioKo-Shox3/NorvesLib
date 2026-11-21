// 音響経路の利得と騒音床から、聞こえた量と水平方向を求める。
#include "AI/Perception/HearingModel.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace NorvesLib::Modules::AI::Perception
{
    namespace
    {
        bool IsValidTuning(const HearingTuning& tuning)
        {
            return std::isfinite(tuning.WindCoefficient) &&
                   std::isfinite(tuning.WindReferenceSpeed) && tuning.WindReferenceSpeed > 0.0f &&
                   std::isfinite(tuning.WindFactorMin) && tuning.WindFactorMin > 0.0f &&
                   std::isfinite(tuning.WindFactorMax) && tuning.WindFactorMax >= tuning.WindFactorMin &&
                   std::isfinite(tuning.AudibleLevelDb) &&
                   std::isfinite(tuning.DirectionErrorBaseRadians) && tuning.DirectionErrorBaseRadians >= 0.0f &&
                   std::isfinite(tuning.DirectionErrorMinRadians) && tuning.DirectionErrorMinRadians >= 0.0f &&
                   std::isfinite(tuning.DirectionErrorMaxRadians) && tuning.DirectionErrorMaxRadians >= tuning.DirectionErrorMinRadians;
        }

        float SampleDirectionError01(uint32_t seed)
        {
            seed ^= seed >> 16;
            seed *= 0x7feb352du;
            seed ^= seed >> 15;
            seed *= 0x846ca68bu;
            seed ^= seed >> 16;
            // 上位 24 ビットだけを使い、float への丸めで 1 に達するのを防ぐ。
            return static_cast<float>(seed >> 8) * 0x1p-24f;
        }
    }

    HearingResult EvaluateHearing(const Core::Scene::AcousticTransmissionEvaluator& evaluator,
                                  const HearingQuery& query, const HearingTuning& tuning)
    {
        HearingResult result;
        if (!std::isfinite(query.LoudnessRadiusMeters) || query.LoudnessRadiusMeters <= 0.0f ||
            !std::isfinite(query.Wind.x) || !std::isfinite(query.Wind.y) || !std::isfinite(query.Wind.z) ||
            !IsValidTuning(tuning))
        {
            return result;
        }

        const float referenceDistance = evaluator.GetSettings().GetClass(query.SoundClass).ReferenceDistanceM;
        result.ThresholdDb = Core::Scene::ComputeHearingRadiusThresholdDb(referenceDistance, query.LoudnessRadiusMeters);

        Core::Scene::AcousticPathQuery pathQuery;
        pathQuery.Source = query.Source;
        pathQuery.Listener = query.Listener;
        pathQuery.SoundClass = query.SoundClass;
        pathQuery.SourceLevelDb = tuning.AudibleLevelDb - result.ThresholdDb;
        Core::Scene::AcousticPathResult path;
        result.QueryResult = evaluator.Evaluate(pathQuery, path);
        if (result.QueryResult != Core::Scene::EPhysicsSceneQueryResult::Success)
        {
            return result;
        }
        result.GainDb = path.GainDb;
        result.NoiseFloorDb = path.NoiseFloorDb;

        const double dx = static_cast<double>(query.Listener.x) - query.Source.x;
        const double dz = static_cast<double>(query.Listener.z) - query.Source.z;
        const double distanceXZ = std::hypot(dx, dz);
        const double directionX = distanceXZ > 0.0 ? dx / distanceXZ : 0.0;
        const double directionZ = distanceXZ > 0.0 ? dz / distanceXZ : 0.0;

        // 伝播方向への風速の射影が u cosφ に等しい。風下で正、風上で負になる。
        const double alongPath = query.Wind.x * directionX + query.Wind.z * directionZ;
        const double windFactor = 1.0 + tuning.WindCoefficient * alongPath / tuning.WindReferenceSpeed;
        result.WindFactor = static_cast<float>(std::clamp(windFactor, static_cast<double>(tuning.WindFactorMin),
                                                          static_cast<double>(tuning.WindFactorMax)));

        const double excessDb = static_cast<double>(result.GainDb) - result.ThresholdDb;
        const double levelDb = tuning.AudibleLevelDb + excessDb + 10.0 * std::log10(static_cast<double>(result.WindFactor));
        const double masking = 1.0 / (1.0 + std::pow(10.0, (result.NoiseFloorDb - levelDb) / 10.0));
        result.MaskingFactor = static_cast<float>(masking);
        const double amount = std::pow(10.0, excessDb / 10.0) * result.WindFactor * masking;
        result.Amount = static_cast<float>(std::min(amount, static_cast<double>(std::numeric_limits<float>::max())));
        result.Intensity = -std::expm1(-result.Amount);
        result.bAudible = result.Amount >= 1.0f;

        if (distanceXZ == 0.0)
        {
            result.DirectionUncertaintyRadians = tuning.DirectionErrorMaxRadians;
            return result;
        }
        const double uncertainty = tuning.DirectionErrorBaseRadians / std::sqrt(std::max(1.0e-6, static_cast<double>(result.Amount)));
        result.DirectionUncertaintyRadians = static_cast<float>(std::clamp(uncertainty, static_cast<double>(tuning.DirectionErrorMinRadians),
                                                                           static_cast<double>(tuning.DirectionErrorMaxRadians)));
        const double error = result.DirectionUncertaintyRadians * (2.0 * SampleDirectionError01(query.Seed) - 1.0);
        const double cosine = std::cos(error);
        const double sine = std::sin(error);
        // 伝播方向を反転して聞き手から音源へ向け、Y 軸まわりに誤差を加える。
        result.PerceivedDirection = {static_cast<float>(-directionX * cosine - directionZ * sine), 0.0f,
                                     static_cast<float>(directionX * sine - directionZ * cosine)};
        return result;
    }
}

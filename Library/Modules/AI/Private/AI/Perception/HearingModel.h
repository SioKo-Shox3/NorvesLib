// 評価器を所有する GameThread 専用。雨音で聞こえにくくなる効果は評価器の騒音床から取り込む。
#pragma once

#include "Scene/AcousticTransmission.h"
#include <cstdint>

namespace NorvesLib::Modules::AI::Perception
{
    /// 風による増減、可聴レベル、方向誤差の調整値。既定値はすべて仮の値。
    struct HearingTuning
    {
        float WindCoefficient = 0.3f;
        float WindReferenceSpeed = 5.0f;          ///< 風の強さを換算する基準風速（m/s、正値）。
        float WindFactorMin = 0.5f;
        float WindFactorMax = 1.6f;
        float AudibleLevelDb = 0.0f;              ///< 開けた地面で可聴半径ちょうどに届くレベル（dB）。
        float DirectionErrorBaseRadians = 0.35f;  ///< 聴覚量が 1 のときの方向の不確かさ（rad）。
        float DirectionErrorMinRadians = 0.05f;
        float DirectionErrorMaxRadians = 1.2f;
    };

    struct HearingQuery
    {
        Math::Vector3 Source;                   ///< 音源の位置（m）。
        Math::Vector3 Listener;                 ///< 聞き手の耳の位置（m）。
        float LoudnessRadiusMeters = 0.0f;      ///< 開けた地面でちょうど聞こえる半径（m）。
        Core::Scene::ESoundClass SoundClass = Core::Scene::ESoundClass::Footstep;
        Math::Vector3 Wind;                     ///< 呼び出し側が取得した聞き手の位置での風速（m/s）。
        uint32_t Seed = 0;                      ///< 方向誤差を決める固定シード。
    };

    struct HearingResult
    {
        Core::Scene::EPhysicsSceneQueryResult QueryResult = Core::Scene::EPhysicsSceneQueryResult::InvalidArgument;
        float Amount = 0.0f;                       ///< 感度を掛ける前の線形の聴覚量。
        float Intensity = 0.0f;                    ///< 聴覚量を 0 から 1 に圧縮した強度。
        bool bAudible = false;                     ///< 聴覚量が 1 以上のときに真。
        float GainDb = Core::Scene::kAcousticSilenceDb;
        float ThresholdDb = 0.0f;
        float NoiseFloorDb = Core::Scene::kAcousticSilenceDb;
        float WindFactor = 1.0f;
        float MaskingFactor = 1.0f;
        Math::Vector3 PerceivedDirection;          ///< 聞き手から感じた音源への水平単位ベクトル。水平距離 0 なら零。
        float DirectionUncertaintyRadians = 0.0f;  ///< 方向の誤差の上限（rad）。
    };

    /// 現在の音響経路、風、シードから聞こえ方を求める。入力不正や経路の評価失敗では聴覚量を零にする。
    HearingResult EvaluateHearing(const Core::Scene::AcousticTransmissionEvaluator& evaluator,
                                  const HearingQuery& query, const HearingTuning& tuning);
}

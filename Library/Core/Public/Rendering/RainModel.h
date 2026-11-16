#pragma once

#include "Container/String.h"
#include "Rendering/Wind.h"

#include <cstdint>

namespace NorvesLib::Core::Rendering
{
    inline constexpr uint32_t RainCurtainLayerCount = 3;
    inline constexpr const char* DefaultRainTuningPath = "Assets/Data/Environment/RainTuning.json";

    /**
     * @brief 近距離の雨、遠景のカーテン、霧の増分に共通の調整値。
     * 数値の既定値はすべて仮の値。
     */
    struct RainTuning
    {
        float NearBoxHalfWidthMeters = 8.0f;
        float NearBoxHeightMeters = 10.0f;
        float NearFallSpeedMinMetersPerSecond = 7.0f;
        float NearFallSpeedMaxMetersPerSecond = 9.0f;
        float NearFallSpeedVariation = 0.10f;
        float NearActiveFractionMax = 1.0f;
        float NearActiveFractionExponent = 0.7f;
        float NearOpacityMax = 0.35f;
        float NearOpacityExponent = 0.5f;
        float NearThicknessMinMeters = 0.0015f;
        float NearThicknessMaxMeters = 0.003f;
        float StreakShutterSeconds = 1.0f / 48.0f;
        float NearFadeZeroMeters = 0.5f;
        float NearFadeFullMeters = 1.5f;
        float NearColorScale[3] = {1.0f, 1.0f, 1.0f};
        float CurtainLayerDistanceMeters[RainCurtainLayerCount] = {18.0f, 40.0f, 90.0f};
        float CurtainLayerOpacityMax[RainCurtainLayerCount] = {0.25f, 0.20f, 0.15f};
        float CurtainOpacityExponent = 1.0f;
        float CurtainLayerScrollSpeed[RainCurtainLayerCount] = {1.0f, 0.6f, 0.35f};
        float CurtainShearFactor = 1.0f;
        float FogBoostMaxDensity = 0.02f;
        float FogBoostExponent = 1.5f;
    };

    /// 雨の調整値の既定値を返す。
    RainTuning MakeDefaultRainTuning();

    /// 非有限値・範囲外の欄と関係が崩れた組を、警告せず既定値へ戻す。
    RainTuning SanitizeRainTuning(const RainTuning& in);

    /** @brief 近距離の粒の割合、不透明度、太さ、落下速度と風による傾き。 */
    struct RainNearEvaluation
    {
        float ActiveFraction = 0.0f;
        float Opacity = 0.0f;
        float ThicknessMeters = 0.0f;
        float FallSpeedMetersPerSecond = 0.0f;
        float TiltRadians = 0.0f;
        float TiltDirectionYawDegrees = 0.0f;
    };

    /// 雨量 [0, 1] と風から近距離の雨を評価する。非有限の雨量は 0 とする。
    RainNearEvaluation EvaluateRainNear(float intensity, const WindParameters& wind, const RainTuning& tuning);

    /** @brief 遠景のカーテンの層ごとの不透明度。 */
    struct RainCurtainEvaluation
    {
        float LayerOpacity[RainCurtainLayerCount] = {};
    };

    /// 雨量 [0, 1] から遠景のカーテンを評価する。非有限の雨量は 0 とする。
    RainCurtainEvaluation EvaluateRainCurtain(float intensity, const RainTuning& tuning);

    /// 雨量 [0, 1] から霧の基準高度の密度に加える増分を返す。
    float MakeRainFogBoost(float intensity, const RainTuning& tuning);

    /// JSON を既定値に重ねて読み込む。解析失敗時は既定値を設定し、警告して false を返す。
    bool TryLoadRainTuningFromJsonText(const Container::String& text, RainTuning& outTuning);

    /// UTF-8 BOM を除いてファイルを読み込み、失敗時は警告して既定値を返す。
    RainTuning LoadRainTuningFile(const Container::String& utf8Path);
} // 名前空間 NorvesLib::Core::Rendering

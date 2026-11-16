#include "Rendering/RainModel.h"

#include "Logging/LogMacros.h"
#include "Platform/PlatformFile.h"
#include "Text/JsonDocument.h"

#include <cmath>

namespace NorvesLib::Core::Rendering
{
    namespace
    {
        // 値域と JSON の対応を共有し、純関数と読み込みで同じ検査を使う。
        struct RainField
        {
            const char* Section;
            const char* Key;
            float RainTuning::*Scalar;
            float (RainTuning::*Array)[RainCurtainLayerCount];
            float Minimum;
            float Maximum;

            uint32_t Count() const
            {
                return Array != nullptr ? RainCurtainLayerCount : 1u;
            }

            float* Values(RainTuning& tuning) const
            {
                return Array != nullptr ? tuning.*Array : &(tuning.*Scalar);
            }

            const float* Values(const RainTuning& tuning) const
            {
                return Array != nullptr ? tuning.*Array : &(tuning.*Scalar);
            }
        };

        constexpr RainField RainFields[] = {
            {"near", "boxHalfWidthMeters", &RainTuning::NearBoxHalfWidthMeters, nullptr, 0.5f, 64.0f},
            {"near", "boxHeightMeters", &RainTuning::NearBoxHeightMeters, nullptr, 0.5f, 64.0f},
            {"near", "fallSpeedMinMetersPerSecond", &RainTuning::NearFallSpeedMinMetersPerSecond, nullptr, 0.5f, 30.0f},
            {"near", "fallSpeedMaxMetersPerSecond", &RainTuning::NearFallSpeedMaxMetersPerSecond, nullptr, 0.5f, 30.0f},
            {"near", "fallSpeedVariation", &RainTuning::NearFallSpeedVariation, nullptr, 0.0f, 0.5f},
            {"near", "activeFractionMax", &RainTuning::NearActiveFractionMax, nullptr, 0.0f, 1.0f},
            {"near", "activeFractionExponent", &RainTuning::NearActiveFractionExponent, nullptr, 0.25f, 4.0f},
            {"near", "opacityMax", &RainTuning::NearOpacityMax, nullptr, 0.0f, 1.0f},
            {"near", "opacityExponent", &RainTuning::NearOpacityExponent, nullptr, 0.25f, 4.0f},
            {"near", "thicknessMinMeters", &RainTuning::NearThicknessMinMeters, nullptr, 0.0001f, 0.05f},
            {"near", "thicknessMaxMeters", &RainTuning::NearThicknessMaxMeters, nullptr, 0.0001f, 0.05f},
            {"near", "streakShutterSeconds", &RainTuning::StreakShutterSeconds, nullptr, 0.0f, 0.25f},
            {"near", "fadeZeroMeters", &RainTuning::NearFadeZeroMeters, nullptr, 0.0f, 10.0f},
            {"near", "fadeFullMeters", &RainTuning::NearFadeFullMeters, nullptr, 0.0f, 20.0f},
            {"near", "colorScale", nullptr, &RainTuning::NearColorScale, 0.0f, 16.0f},
            {"curtain", "layerDistanceMeters", nullptr, &RainTuning::CurtainLayerDistanceMeters, 1.0f, 2000.0f},
            {"curtain", "layerOpacityMax", nullptr, &RainTuning::CurtainLayerOpacityMax, 0.0f, 1.0f},
            {"curtain", "opacityExponent", &RainTuning::CurtainOpacityExponent, nullptr, 0.25f, 4.0f},
            {"curtain", "layerScrollSpeed", nullptr, &RainTuning::CurtainLayerScrollSpeed, 0.0f, 100.0f},
            {"curtain", "shearFactor", &RainTuning::CurtainShearFactor, nullptr, 0.0f, 4.0f},
            {"fog", "boostMaxDensity", &RainTuning::FogBoostMaxDensity, nullptr, 0.0f, 1.0f},
            {"fog", "boostExponent", &RainTuning::FogBoostExponent, nullptr, 0.25f, 4.0f},
        };

        bool IsInRange(double value, const RainField& field)
        {
            return std::isfinite(value) && value >= field.Minimum && value <= field.Maximum;
        }

        void WarnInvalidRange(const RainField& field, uint32_t index)
        {
            if (field.Array != nullptr)
            {
                NORVES_LOG_WARNING("RainModel", "%s.%s[%u] が非有限または範囲外のため既定値へ戻します",
                                   field.Section, field.Key, index);
            }
            else
            {
                NORVES_LOG_WARNING("RainModel", "%s.%s が非有限または範囲外のため既定値へ戻します",
                                   field.Section, field.Key);
            }
        }

        RainTuning SanitizeTuning(const RainTuning& in, bool bWarn)
        {
            const RainTuning defaults = MakeDefaultRainTuning();
            RainTuning result = in;
            for (const RainField& field : RainFields)
            {
                float* values = field.Values(result);
                const float* fallback = field.Values(defaults);
                for (uint32_t index = 0; index < field.Count(); ++index)
                {
                    if (!IsInRange(values[index], field))
                    {
                        values[index] = fallback[index];
                        if (bWarn)
                        {
                            WarnInvalidRange(field, index);
                        }
                    }
                }
            }

            if (result.NearFallSpeedMinMetersPerSecond > result.NearFallSpeedMaxMetersPerSecond)
            {
                result.NearFallSpeedMinMetersPerSecond = defaults.NearFallSpeedMinMetersPerSecond;
                result.NearFallSpeedMaxMetersPerSecond = defaults.NearFallSpeedMaxMetersPerSecond;
                if (bWarn)
                {
                    NORVES_LOG_WARNING("RainModel", "near.fallSpeedMinMetersPerSecond と near.fallSpeedMaxMetersPerSecond の大小関係が不正のため組を既定値へ戻します");
                }
            }
            if (result.NearThicknessMinMeters > result.NearThicknessMaxMeters)
            {
                result.NearThicknessMinMeters = defaults.NearThicknessMinMeters;
                result.NearThicknessMaxMeters = defaults.NearThicknessMaxMeters;
                if (bWarn)
                {
                    NORVES_LOG_WARNING("RainModel", "near.thicknessMinMeters と near.thicknessMaxMeters の大小関係が不正のため組を既定値へ戻します");
                }
            }
            if (result.NearFadeZeroMeters >= result.NearFadeFullMeters)
            {
                result.NearFadeZeroMeters = defaults.NearFadeZeroMeters;
                result.NearFadeFullMeters = defaults.NearFadeFullMeters;
                if (bWarn)
                {
                    NORVES_LOG_WARNING("RainModel", "near.fadeZeroMeters と near.fadeFullMeters の大小関係が不正のため組を既定値へ戻します");
                }
            }
            bool bDistancesIncreasing = true;
            for (uint32_t index = 1; index < RainCurtainLayerCount; ++index)
            {
                if (result.CurtainLayerDistanceMeters[index - 1] >= result.CurtainLayerDistanceMeters[index])
                {
                    bDistancesIncreasing = false;
                }
            }
            if (!bDistancesIncreasing)
            {
                for (uint32_t index = 0; index < RainCurtainLayerCount; ++index)
                {
                    result.CurtainLayerDistanceMeters[index] = defaults.CurtainLayerDistanceMeters[index];
                }
                if (bWarn)
                {
                    NORVES_LOG_WARNING("RainModel", "curtain.layerDistanceMeters が厳密な昇順でないため全層を既定値へ戻します");
                }
            }
            return result;
        }

        float SanitizeIntensity(float intensity)
        {
            if (!std::isfinite(intensity) || intensity <= 0.0f)
            {
                return 0.0f;
            }
            return intensity >= 1.0f ? 1.0f : intensity;
        }

        float IntensityCurve(float intensity, float exponent)
        {
            return intensity == 0.0f ? 0.0f : std::pow(intensity, exponent);
        }

        void LoadField(const JsonValue& value, const RainField& field, RainTuning& tuning)
        {
            const bool bArray = field.Array != nullptr;
            if ((!bArray && !value.IsNumber()) ||
                (bArray && (!value.IsArray() || value.GetArraySize() != field.Count())))
            {
                NORVES_LOG_WARNING("RainModel", "%s.%s の型または配列の長さが不正のため既定値を使います",
                                   field.Section, field.Key);
                return;
            }
            // 数でない要素を含む配列は、欄全体を既定値のままにする。
            if (bArray)
            {
                for (uint32_t index = 0; index < field.Count(); ++index)
                {
                    if (!value.GetArrayElement(index).IsNumber())
                    {
                        NORVES_LOG_WARNING("RainModel", "%s.%s[%u] が数でないため配列の既定値を使います",
                                           field.Section, field.Key, index);
                        return;
                    }
                }
            }
            float* values = field.Values(tuning);
            for (uint32_t index = 0; index < field.Count(); ++index)
            {
                const double number = bArray ? value.GetArrayElement(index).AsNumber() : value.AsNumber();
                // float に収まらない値も、変換する前に除く。
                if (IsInRange(number, field))
                {
                    values[index] = static_cast<float>(number);
                }
                else
                {
                    WarnInvalidRange(field, index);
                }
            }
        }

        void LoadSection(const JsonValue& section, const Container::String& sectionName, RainTuning& tuning)
        {
            if (!section.IsObject())
            {
                NORVES_LOG_WARNING("RainModel", "%s がオブジェクトでないため節の既定値を使います", sectionName.c_str());
                return;
            }
            for (size_t index = 0; index < section.GetObjectSize(); ++index)
            {
                const Container::String& key = section.GetMemberName(index);
                const RainField* pField = nullptr;
                for (const RainField& field : RainFields)
                {
                    if (sectionName == field.Section && key == field.Key)
                    {
                        pField = &field;
                        break;
                    }
                }
                if (pField == nullptr)
                {
                    NORVES_LOG_WARNING("RainModel", "未知の欄 %s.%s を無視します", sectionName.c_str(), key.c_str());
                    continue;
                }
                LoadField(section.GetMemberValue(index), *pField, tuning);
            }
        }
    } // 無名名前空間

    RainTuning MakeDefaultRainTuning()
    {
        return RainTuning{};
    }

    RainTuning SanitizeRainTuning(const RainTuning& in)
    {
        return SanitizeTuning(in, false);
    }

    RainNearEvaluation EvaluateRainNear(float intensity, const WindParameters& wind, const RainTuning& tuning)
    {
        const RainTuning safe = SanitizeRainTuning(tuning);
        const float amount = SanitizeIntensity(intensity);
        RainNearEvaluation result;
        result.ActiveFraction = safe.NearActiveFractionMax * IntensityCurve(amount, safe.NearActiveFractionExponent);
        result.Opacity = safe.NearOpacityMax * IntensityCurve(amount, safe.NearOpacityExponent);
        result.ThicknessMeters = safe.NearThicknessMinMeters +
            (safe.NearThicknessMaxMeters - safe.NearThicknessMinMeters) * amount;
        result.FallSpeedMetersPerSecond = safe.NearFallSpeedMinMetersPerSecond +
            (safe.NearFallSpeedMaxMetersPerSecond - safe.NearFallSpeedMinMetersPerSecond) * amount;
        const float windSpeed = std::isfinite(wind.BaseSpeed) && wind.BaseSpeed >= 0.0f ? wind.BaseSpeed : 0.0f;
        result.TiltRadians = std::atan2(windSpeed, result.FallSpeedMetersPerSecond);
        result.TiltDirectionYawDegrees = std::isfinite(wind.DirectionYawDegrees) ? wind.DirectionYawDegrees : 0.0f;
        return result;
    }

    RainCurtainEvaluation EvaluateRainCurtain(float intensity, const RainTuning& tuning)
    {
        const RainTuning safe = SanitizeRainTuning(tuning);
        const float curve = IntensityCurve(SanitizeIntensity(intensity), safe.CurtainOpacityExponent);
        RainCurtainEvaluation result;
        for (uint32_t index = 0; index < RainCurtainLayerCount; ++index)
        {
            result.LayerOpacity[index] = safe.CurtainLayerOpacityMax[index] * curve;
        }
        return result;
    }

    float MakeRainFogBoost(float intensity, const RainTuning& tuning)
    {
        const RainTuning safe = SanitizeRainTuning(tuning);
        return safe.FogBoostMaxDensity * IntensityCurve(SanitizeIntensity(intensity), safe.FogBoostExponent);
    }

    bool TryLoadRainTuningFromJsonText(const Container::String& text, RainTuning& outTuning)
    {
        outTuning = MakeDefaultRainTuning();
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            NORVES_LOG_WARNING("RainModel", "雨の調整値の JSON を解析できないため既定値を使います");
            return false;
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            NORVES_LOG_WARNING("RainModel", "雨の調整値の最上位がオブジェクトでないため既定値を使います");
            return false;
        }
        for (size_t index = 0; index < root.GetObjectSize(); ++index)
        {
            const Container::String& name = root.GetMemberName(index);
            if (name == "near" || name == "curtain" || name == "fog")
            {
                LoadSection(root.GetMemberValue(index), name, outTuning);
            }
            else
            {
                NORVES_LOG_WARNING("RainModel", "未知の節 %s を無視します", name.c_str());
            }
        }
        outTuning = SanitizeTuning(outTuning, true);
        return true;
    }

    RainTuning LoadRainTuningFile(const Container::String& utf8Path)
    {
        Container::VariableArray<uint8_t> bytes;
        if (!Platform::ReadFileBytes(utf8Path, bytes).bSuccess)
        {
            NORVES_LOG_WARNING("RainModel", "雨の調整値ファイルを読み込めないため既定値を使います: %s", utf8Path.c_str());
            return MakeDefaultRainTuning();
        }
        size_t offset = 0;
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            offset = 3;
        }
        Container::String text;
        if (bytes.size() > offset)
        {
            text.append(reinterpret_cast<const char*>(bytes.data() + offset), bytes.size() - offset);
        }
        RainTuning result;
        TryLoadRainTuningFromJsonText(text, result);
        return result;
    }
} // 名前空間 NorvesLib::Core::Rendering

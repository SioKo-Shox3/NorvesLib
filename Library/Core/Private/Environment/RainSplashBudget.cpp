// 雨の着水と樹冠からの滴りの予算を見積もり、調整値を読み込む。
#include "Environment/RainSplashBudget.h"

#include "Container/VariableArray.h"
#include "Logging/LogMacros.h"
#include "Platform/PlatformFile.h"
#include "Text/JsonDocument.h"

#include <cfloat>
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Environment
{
    namespace
    {
        // 設定値と JSON の欄で同じ値域を使う。
        struct BudgetField
        {
            const char* Key;
            float RainSplashBudgetSettings::*Member;
            double Maximum;
            bool bStrictlyPositive;
        };

        constexpr BudgetField BudgetFields[] = {
            {"maxSplashesPerSecond", &RainSplashBudgetSettings::MaxSplashesPerSecond, FLT_MAX, false},
            {"maxAliveSplashes", &RainSplashBudgetSettings::MaxAliveSplashes, FLT_MAX, false},
            {"grassDensityForMinScale", &RainSplashBudgetSettings::GrassDensityForMinScale, FLT_MAX, true},
            {"grassMinScale", &RainSplashBudgetSettings::GrassMinScale, 1.0, false},
            {"canopyMinClearanceMeters", &RainSplashBudgetSettings::CanopyMinClearanceMeters, FLT_MAX, false},
            {"canopyDripsPerSecondAtFullRain", &RainSplashBudgetSettings::CanopyDripsPerSecondAtFullRain, FLT_MAX, false},
        };

        bool IsValidFieldValue(double value, const BudgetField& field)
        {
            return std::isfinite(value) && (field.bStrictlyPositive ? value > 0.0 : value >= 0.0) &&
                value <= field.Maximum;
        }

        void WarnInvalidField(const BudgetField& field)
        {
            NORVES_LOG_WARNING("RainSplashBudget", "%s が非有限または範囲外のため無視します", field.Key);
        }

        double FiniteOrZero(float value)
        {
            return std::isfinite(value) ? static_cast<double>(value) : 0.0;
        }

        double ClampUnit(double value)
        {
            if (value <= 0.0)
            {
                return 0.0;
            }
            return value >= 1.0 ? 1.0 : value;
        }

        float ToProbability(double value)
        {
            const double bounded = ClampUnit(value);
            float probability = static_cast<float>(bounded);
            // 単精度への丸めで上限を超えないよう、必要ならゼロ側へ寄せる。
            if (static_cast<double>(probability) > bounded)
            {
                probability = std::nextafter(probability, 0.0f);
            }
            return probability;
        }
    } // 無名名前空間

    RainSplashBudgetSettings MakeDefaultRainSplashBudgetSettings()
    {
        return RainSplashBudgetSettings{};
    }

    RainSplashBudgetSettings SanitizeRainSplashBudgetSettings(const RainSplashBudgetSettings& in)
    {
        const RainSplashBudgetSettings defaults = MakeDefaultRainSplashBudgetSettings();
        RainSplashBudgetSettings result = in;
        for (const BudgetField& field : BudgetFields)
        {
            if (!IsValidFieldValue(result.*(field.Member), field))
            {
                result.*(field.Member) = defaults.*(field.Member);
            }
        }
        return result;
    }

    RainSplashBudgetResult ComputeRainSplashBudget(const RainSplashBudgetInput& input, const RainSplashBudgetSettings& settings)
    {
        const RainSplashBudgetSettings safe = SanitizeRainSplashBudgetSettings(settings);
        const double intensity = ClampUnit(FiniteOrZero(input.RainIntensity));
        const double particleCount = FiniteOrZero(input.ActiveParticleCount);
        const double fallSpeed = FiniteOrZero(input.FallSpeedMetersPerSecond);
        const double boxHeight = FiniteOrZero(input.BoxHeightMeters);
        const double lifetime = FiniteOrZero(input.SplashLifetimeSeconds);
        const double grassDensity = ClampUnit(FiniteOrZero(input.GrassDensity));

        // 有効な粒が箱の高さを落ちる間に 1 回当たると見積もる。
        const double hits = boxHeight > 0.0 ? particleCount * fallSpeed / boxHeight : 0.0;
        if (intensity == 0.0 || hits <= 0.0)
        {
            return {};
        }

        RainSplashBudgetResult result;
        // 単精度で表せない大きな見積りは、その最大値に収める。
        result.HitsPerSecond = static_cast<float>(hits > FLT_MAX ? FLT_MAX : hits);
        if (result.HitsPerSecond <= 0.0f)
        {
            return {};
        }

        // 同時に生きる数は、秒あたりの着水数と寿命の積で見積もる。
        double cap = safe.MaxSplashesPerSecond;
        if (lifetime > 0.0)
        {
            const double aliveCap = static_cast<double>(safe.MaxAliveSplashes) / lifetime;
            cap = aliveCap < cap ? aliveCap : cap;
        }

        // 草の密度に応じて、倍率を 1 から最小倍率まで線形に下げる。
        const double grassAmount = ClampUnit(grassDensity / static_cast<double>(safe.GrassDensityForMinScale));
        const double grassScale = 1.0 + (static_cast<double>(safe.GrassMinScale) - 1.0) * grassAmount;
        // 丸め前の当たり数が大きい場合も、確率が上限を超えないようにする。
        const double hitRate = hits > result.HitsPerSecond ? hits : result.HitsPerSecond;
        const double childRate = cap < hitRate ? cap / hitRate : 1.0;
        result.ChildProbability = ToProbability(childRate * grassScale);
        result.SplashesPerSecond = result.HitsPerSecond * result.ChildProbability;
        // 着水数の単精度への丸めでも上限を超えないよう確率を調整する。
        while (static_cast<double>(result.SplashesPerSecond) > cap && result.ChildProbability > 0.0f)
        {
            result.ChildProbability = std::nextafter(result.ChildProbability, 0.0f);
            result.SplashesPerSecond = result.HitsPerSecond * result.ChildProbability;
        }

        // 全ての当たりが樹冠下でも、滴りの数は雨量に比例した目安以下にする。
        const double dripRate = static_cast<double>(safe.CanopyDripsPerSecondAtFullRain) * intensity;
        result.DripProbability = ToProbability(dripRate / hitRate);
        return result;
    }

    bool TryLoadRainSplashBudgetFromJsonText(const Container::String& text, RainSplashBudgetSettings& outSettings)
    {
        outSettings = MakeDefaultRainSplashBudgetSettings();
        JsonDocument document;
        if (!JsonDocument::TryParse(text, document))
        {
            NORVES_LOG_WARNING("RainSplashBudget", "着水予算の JSON を解析できないため既定値を使います");
            return false;
        }
        const JsonValue root = document.GetRoot();
        if (!root.IsObject())
        {
            NORVES_LOG_WARNING("RainSplashBudget", "着水予算の最上位がオブジェクトでないため既定値を使います");
            return false;
        }
        for (size_t index = 0; index < root.GetObjectSize(); ++index)
        {
            const Container::String& key = root.GetMemberName(index);
            const BudgetField* pField = nullptr;
            for (const BudgetField& field : BudgetFields)
            {
                if (key == field.Key)
                {
                    pField = &field;
                    break;
                }
            }
            if (pField == nullptr)
            {
                NORVES_LOG_WARNING("RainSplashBudget", "未知の欄 %s を無視します", key.c_str());
                continue;
            }
            const JsonValue value = root.GetMemberValue(index);
            if (!value.IsNumber())
            {
                NORVES_LOG_WARNING("RainSplashBudget", "%s が数でないため無視します", key.c_str());
                continue;
            }
            const double number = value.AsNumber();
            if (!IsValidFieldValue(number, *pField))
            {
                WarnInvalidField(*pField);
                continue;
            }
            const float converted = static_cast<float>(number);
            // 密度の基準が単精度への変換で 0 になった場合も無効とする。
            if (!IsValidFieldValue(converted, *pField))
            {
                WarnInvalidField(*pField);
                continue;
            }
            outSettings.*(pField->Member) = converted;
        }
        return true;
    }

    RainSplashBudgetSettings LoadRainSplashBudgetFile(const Container::String& utf8Path)
    {
        Container::VariableArray<uint8_t> bytes;
        if (!Platform::ReadFileBytes(utf8Path, bytes).bSuccess)
        {
            NORVES_LOG_WARNING("RainSplashBudget", "着水予算ファイルを読み込めないため既定値を使います: %s", utf8Path.c_str());
            return MakeDefaultRainSplashBudgetSettings();
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
        RainSplashBudgetSettings result;
        TryLoadRainSplashBudgetFromJsonText(text, result);
        return result;
    }
} // 名前空間 NorvesLib::Core::Environment

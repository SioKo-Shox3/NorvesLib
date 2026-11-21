// 4 つの知覚モデルの調整値を既定値に重ねて読み込む。既定値と同梱 JSON の値はすべて仮の値。
#include "AI/Perception/PerceptionTuning.h"

#include "Asset/AssetFileReader.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <initializer_list>
#include <limits>

namespace NorvesLib::Modules::AI::Perception
{
    namespace
    {
        enum class Range { Finite, NonNegative, Positive, Unit, PositiveUnit, AtLeastOne, FieldOfView };

        struct Field
        {
            const char* Key;
            float& Output;
            Range Allowed;
        };

        struct Section
        {
            const char* Key;
            std::initializer_list<Field> Fields;
        };

        const char* GetRangeError(double value, Range allowed)
        {
            constexpr double limit = std::numeric_limits<float>::max();
            if (!std::isfinite(value) || value < -limit || value > limit)
            {
                return "float で表せる有限数を指定してください。";
            }
            // 正値が float への変換で零に丸まる場合も、正の下限を満たさない。
            const bool positive = static_cast<float>(value) > 0.0f;
            switch (allowed)
            {
            case Range::Finite:      return nullptr;
            case Range::NonNegative: return value >= 0.0 ? nullptr : "0 以上の値を指定してください。";
            case Range::Positive:    return positive ? nullptr : "正の値を指定してください。";
            case Range::Unit:        return value >= 0.0 && value <= 1.0 ? nullptr : "0 以上 1 以下の値を指定してください。";
            case Range::PositiveUnit: return positive && value <= 1.0 ? nullptr : "0 より大きく 1 以下の値を指定してください。";
            case Range::AtLeastOne:  return value >= 1.0 ? nullptr : "1 以上の値を指定してください。";
            case Range::FieldOfView: return positive && value <= 360.0 ? nullptr : "0 より大きく 360 以下の角度を指定してください。";
            }
            return "値の範囲が不明です。";
        }
    }

    PerceptionTuningLoadResult ParsePerceptionTuningJson(const Core::Container::String& json, PerceptionTuning& outTuning)
    {
        Core::JsonDocument document;
        Core::Container::String parseError;
        if (!Core::JsonDocument::TryParse(json, document, &parseError))
        {
            return {false, "知覚設定の JSON を解析できません。" + parseError};
        }
        const auto root = document.GetRoot();
        if (!root.IsObject())
        {
            return {false, "知覚設定のルートには JSON オブジェクトを指定してください。"};
        }
        const auto version = root.FindMember("version");
        if (version.IsValid() && (!version.IsNumber() || version.AsNumber() != 1.0))
        {
            return {false, "version には数値の 1 を指定してください。"};
        }

        PerceptionTuning next{};
        const Section sections[] = {
            {"vision", {
                {"fieldOfViewDegrees", next.Vision.FieldOfViewDegrees, Range::FieldOfView},
                {"fieldOfViewEdgeDegrees", next.Vision.FieldOfViewEdgeDegrees, Range::NonNegative},
                {"fadeStartMeters", next.Vision.FadeStartMeters, Range::Positive},
                {"maxDistanceMeters", next.Vision.MaxDistanceMeters, Range::Positive},
                {"terrainClearanceMeters", next.Vision.TerrainClearanceMeters, Range::NonNegative},
            }},
            {"grass", {
                {"opacityScale", next.Vision.GrassOpacityScale, Range::NonNegative},
                {"stepMeters", next.Vision.GrassStepMeters, Range::Positive},
            }},
            {"hearing", {
                {"windCoefficient", next.Hearing.WindCoefficient, Range::NonNegative},
                {"windReferenceSpeed", next.Hearing.WindReferenceSpeed, Range::Positive},
                {"windFactorMin", next.Hearing.WindFactorMin, Range::PositiveUnit},
                {"windFactorMax", next.Hearing.WindFactorMax, Range::AtLeastOne},
                {"audibleLevelDb", next.Hearing.AudibleLevelDb, Range::Finite},
                {"directionErrorBaseRadians", next.Hearing.DirectionErrorBaseRadians, Range::NonNegative},
                {"directionErrorMinRadians", next.Hearing.DirectionErrorMinRadians, Range::NonNegative},
                {"directionErrorMaxRadians", next.Hearing.DirectionErrorMaxRadians, Range::NonNegative},
            }},
            {"smell", {
                {"downwindLengthMeters", next.Smell.DownwindLengthMeters, Range::Positive},
                {"upwindLengthMeters", next.Smell.UpwindLengthMeters, Range::Positive},
                {"upwindFactor", next.Smell.UpwindFactor, Range::Unit},
                {"upwindAxisRatio", next.Smell.UpwindAxisRatio, Range::Unit},
                {"crossSigmaBaseMeters", next.Smell.CrossSigmaBaseMeters, Range::Positive},
                {"crossSigmaSpreadPerMeter", next.Smell.CrossSigmaSpreadPerMeter, Range::NonNegative},
                {"calmWindSpeed", next.Smell.CalmWindSpeed, Range::NonNegative},
                {"fullWindSpeed", next.Smell.FullWindSpeed, Range::Positive},
                {"calmLengthMeters", next.Smell.CalmLengthMeters, Range::Positive},
                {"windFilterSeconds", next.Smell.WindFilterSeconds, Range::Positive},
            }},
            {"awareness", {
                {"gain", next.Awareness.Gain, Range::NonNegative},
                {"decay", next.Awareness.Decay, Range::NonNegative},
                {"suspiciousThreshold", next.Awareness.SuspiciousThreshold, Range::PositiveUnit},
                {"alertThreshold", next.Awareness.AlertThreshold, Range::PositiveUnit},
                {"hysteresis", next.Awareness.Hysteresis, Range::NonNegative},
                {"reactionDelaySeconds", next.Awareness.ReactionDelaySeconds, Range::Positive},
                {"reactionJitterSeconds", next.Awareness.ReactionJitterSeconds, Range::NonNegative},
                {"memorySeconds", next.Awareness.MemorySeconds, Range::NonNegative},
            }},
        };
        for (const auto& section : sections)
        {
            const auto object = root.FindMember(section.Key);
            if (!object.IsValid())
            {
                continue;
            }
            if (!object.IsObject())
            {
                return {false, Core::Container::String(section.Key) + " にはオブジェクトを指定してください。"};
            }
            for (const auto& field : section.Fields)
            {
                const auto member = object.FindMember(field.Key);
                if (!member.IsValid())
                {
                    continue;
                }
                const char* error = member.IsNumber() ? GetRangeError(member.AsNumber(), field.Allowed)
                                                      : "数値を指定してください。";
                if (error)
                {
                    return {false, Core::Container::String(section.Key) + "." + field.Key + " には次の制約があります。" + error};
                }
                field.Output = static_cast<float>(member.AsNumber());
            }
        }

        if (next.Vision.FadeStartMeters >= next.Vision.MaxDistanceMeters)
        {
            return {false, "vision.fadeStartMeters は vision.maxDistanceMeters より小さくしてください。"};
        }
        if (next.Hearing.DirectionErrorMinRadians > next.Hearing.DirectionErrorMaxRadians)
        {
            return {false, "hearing.directionErrorMinRadians は hearing.directionErrorMaxRadians 以下にしてください。"};
        }
        if (next.Smell.CalmWindSpeed >= next.Smell.FullWindSpeed)
        {
            return {false, "smell.calmWindSpeed は smell.fullWindSpeed より小さくしてください。"};
        }
        if (next.Awareness.SuspiciousThreshold >= next.Awareness.AlertThreshold)
        {
            return {false, "awareness.suspiciousThreshold は awareness.alertThreshold より小さくしてください。"};
        }
        if (next.Awareness.Hysteresis >= next.Awareness.SuspiciousThreshold)
        {
            return {false, "awareness.hysteresis は awareness.suspiciousThreshold より小さくしてください。"};
        }
        outTuning = next;
        return {true, {}};
    }

    PerceptionTuningLoadResult LoadPerceptionTuningFromAssetFile(Core::Container::AnsiStringView path, PerceptionTuning& outTuning)
    {
        Core::Asset::AssetReadRequest request;
        request.InputPath = Core::Container::AnsiString(path);
        request.bAllowAbsolutePath = false;
        const auto asset = Core::Asset::AssetFileReader().Read(request);
        if (!asset.Succeeded())
        {
            return {false, request.InputPath + " の知覚設定を読み込めません。" + asset.Reason};
        }
        Core::Container::String json;
        if (!asset.Blob.IsEmpty())
        {
            json.append(reinterpret_cast<const char*>(asset.Blob.GetData()), asset.Blob.GetSize());
        }
        return ParsePerceptionTuningJson(json, outTuning);
    }
}

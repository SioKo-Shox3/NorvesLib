#include "Input/HapticsJson.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"
#include "Logging/LogMacros.h"
#include <cstring>
#include <initializer_list>
#include <limits>
#include <type_traits>
#include <utility>

namespace NorvesLib::Core::Input::HapticsJson
{
    namespace
    {
        bool Fail(HapticsJsonReport& report, const char* message)
        {
            report.Error = message;
            return false;
        }
        bool Fields(JsonValue object, std::initializer_list<const char*> allowed, HapticsJsonReport& report)
        {
            if (!object.IsObject())
            {
                return Fail(report,"振動設定値がobjectではありません");
            }
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const auto& name = object.GetMemberName(index);
                bool known = false;
                for (const auto* field : allowed)
                {
                    known = known || name == field;
                }
                if (!known)
                {
                    if (report.WarningCount < 16)
                    {
                        NORVES_LOG_WARNING("Input","未知の振動fieldを無視します: %s",name.empty() ? "" : name.c_str());
                    }
                    ++report.WarningCount;
                    continue;
                }
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (object.GetMemberName(previous) == name)
                    {
                        return Fail(report,"既知の振動fieldが重複しています");
                    }
                }
            }
            return true;
        }
        bool ValidName(const char* text, size_t size)
        {
            if (!text || size == 0 || size > MaximumNameBytes)
            {
                return false;
            }
            for (size_t index = 0; index < size; ++index)
            {
                if (static_cast<unsigned char>(text[index]) < 0x20)
                {
                    return false;
                }
            }
            return true;
        }
        bool ValidSettings(const HapticsSettings& settings)
        {
            return std::isfinite(settings.Strength) && settings.Strength >= 0 && settings.Strength <= 1 &&
                (settings.MixMode == EHapticsMixMode::Maximum || settings.MixMode == EHapticsMixMode::AddClamp);
        }
        bool Envelope(const Container::String& text, JsonDocument& document, HapticsJsonReport& report)
        {
            if (text.empty() || text.size() > MaximumTextBytes)
            {
                return Fail(report,"振動JSONが空か1MiB上限を超えています");
            }
            size_t depth = 0;
            bool quoted = false, escape = false;
            for (const auto ch : text)
            {
                if (quoted)
                {
                    if (escape)
                    {
                        escape = false;
                    }
                    else if (ch == '\\')
                    {
                        escape = true;
                    }
                    else if (ch == '"')
                    {
                        quoted = false;
                    }
                    continue;
                }
                if (ch == '"')
                {
                    quoted = true;
                }
                else if (ch == '{' || ch == '[')
                {
                    if (++depth > MaximumDepth)
                    {
                        return Fail(report,"振動JSONの入れ子が深すぎます");
                    }
                }
                else if (ch == '}' || ch == ']')
                {
                    if (depth == 0)
                    {
                        return Fail(report,"振動JSONの括弧が不正です");
                    }
                    --depth;
                }
            }
            if (quoted || depth != 0)
            {
                return Fail(report,"振動JSONが途中で終わっています");
            }
            const bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
                static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
            return bom ? JsonDocument::TryParse(text.substr(3),document,&report.Error) :
                JsonDocument::TryParse(text,document,&report.Error);
        }
        bool Curve(JsonValue array, double duration, Container::VariableArray<HapticsKeyframe>& out,
            HapticsJsonReport& report)
        {
            if (!array.IsArray() || array.GetArraySize() > MaximumKeysPerCurve)
            {
                return Fail(report,"振動curveが配列でないかkey上限を超えています");
            }
            for (size_t index = 0; index < array.GetArraySize(); ++index)
            {
                const auto key = array.GetArrayElement(index);
                if (!Fields(key,{"t","v"},report))
                {
                    return false;
                }
                const auto time = key.FindMember("t");
                const auto value = key.FindMember("v");
                if (!time.IsNumber() || !value.IsNumber() || !std::isfinite(time.AsNumber()) ||
                    !std::isfinite(value.AsNumber()) || time.AsNumber() < 0 || time.AsNumber() > duration ||
                    value.AsNumber() < 0 || value.AsNumber() > 1)
                {
                    return Fail(report,"振動keyの時刻/強さが不正です");
                }
                out.push_back({time.AsNumber(),static_cast<float>(value.AsNumber())});
            }
            if (!IsValidHapticsCurve({out.data(),out.size()},duration))
            {
                return Fail(report,"振動keyの時刻が昇順でないか重複しています");
            }
            return true;
        }
        bool Settings(JsonValue object, HapticsSettings& out, HapticsJsonReport& report)
        {
            if (!object.IsValid())
            {
                return true;
            }
            if (!Fields(object,{"enabled","strength","mix_mode"},report))
            {
                return false;
            }
            const auto enabled = object.FindMember("enabled");
            if (enabled.IsValid())
            {
                if (!enabled.IsBoolean())
                {
                    return Fail(report,"振動enabledがboolではありません");
                }
                out.Enabled = enabled.AsBool();
            }
            const auto strength = object.FindMember("strength");
            if (strength.IsValid())
            {
                if (!strength.IsNumber() || !std::isfinite(strength.AsNumber()) ||
                    strength.AsNumber() < 0 || strength.AsNumber() > 1)
                {
                    return Fail(report,"振動strengthは0..1が必要です");
                }
                out.Strength = static_cast<float>(strength.AsNumber());
            }
            const auto mode = object.FindMember("mix_mode");
            if (mode.IsValid())
            {
                if (!mode.IsString())
                {
                    return Fail(report,"振動mix_modeが文字列ではありません");
                }
                if (mode.AsString() == "maximum")
                {
                    out.MixMode = EHapticsMixMode::Maximum;
                }
                else if (mode.AsString() == "add_clamp")
                {
                    out.MixMode = EHapticsMixMode::AddClamp;
                }
                else
                {
                    return Fail(report,"未対応の振動mix_modeです");
                }
            }
            return true;
        }
        void WriteCurve(JsonWriter& writer, const char* name, const Container::VariableArray<HapticsKeyframe>& curve)
        {
            writer.BeginArray(name);
            for (const auto& key : curve)
            {
                writer.BeginObject();
                writer.WriteNumber("t",key.Time);
                writer.WriteNumber("v",key.Value);
                writer.EndObject();
            }
            writer.EndArray();
        }
    }
    bool IsValid(const HapticsConfiguration& configuration)
    {
        if (!ValidSettings(configuration.Settings) || configuration.Effects.size() > MaximumEffects)
        {
            return false;
        }
        for (size_t index = 0; index < configuration.Effects.size(); ++index)
        {
            const auto& effect = configuration.Effects[index];
            const auto name = effect.Id.GetView();
            if (!effect.Id.IsValid() || !ValidName(name.data(),name.size()) ||
                effect.Low.size() > MaximumKeysPerCurve || effect.High.size() > MaximumKeysPerCurve ||
                !IsValidHapticsEffect(effect.View()))
            {
                return false;
            }
            for (size_t previous = 0; previous < index; ++previous)
            {
                if (configuration.Effects[previous].Id == effect.Id)
                {
                    return false;
                }
            }
        }
        return true;
    }
    bool Parse(const Container::String& json, HapticsConfiguration& out, HapticsJsonReport* report)
    {
        static_assert(std::is_nothrow_move_assignable_v<HapticsConfiguration>);
        static_assert(std::is_nothrow_move_assignable_v<HapticsJsonReport>);
        HapticsJsonReport result;
        // jsonがreport.Errorを参照していても、読了前に入力を消さない。
        struct ReportPublication
        {
            HapticsJsonReport* Target;
            HapticsJsonReport& Value;
            ~ReportPublication() noexcept
            {
                if (Target)
                {
                    *Target = std::move(Value);
                }
            }
        } publication{report,result};
        JsonDocument document;
        if (!Envelope(json,document,result))
        {
            return false;
        }
        const auto root = document.GetRoot();
        if (!Fields(root,{"schema","effects","settings"},result))
        {
            return false;
        }
        const auto schema = root.FindMember("schema");
        const auto effects = root.FindMember("effects");
        if (!schema.IsString() || schema.AsString() != "haptics.v1" || !effects.IsArray() ||
            effects.GetArraySize() > MaximumEffects)
        {
            return Fail(result,"振動schemaまたはeffects配列が不正です");
        }
        HapticsConfiguration candidate;
        if (!Settings(root.FindMember("settings"),candidate.Settings,result))
        {
            return false;
        }
        for (size_t index = 0; index < effects.GetArraySize(); ++index)
        {
            const auto object = effects.GetArrayElement(index);
            if (!Fields(object,{"name","duration","loop","priority","low","high"},result))
            {
                return false;
            }
            const auto name = object.FindMember("name");
            const auto duration = object.FindMember("duration");
            if (!name.IsString() || !ValidName(name.AsString().data(),name.AsString().size()) ||
                !duration.IsNumber() || !std::isfinite(duration.AsNumber()) || duration.AsNumber() <= 0)
            {
                return Fail(result,"振動effectのname/durationが不正です");
            }
            HapticsEffectDefinition effect;
            effect.Duration = duration.AsNumber();
            const auto loop = object.FindMember("loop");
            if (loop.IsValid())
            {
                if (!loop.IsBoolean())
                {
                    return Fail(result,"振動loopがboolではありません");
                }
                effect.Loop = loop.AsBool();
            }
            const auto priority = object.FindMember("priority");
            if (priority.IsValid())
            {
                const double value = priority.AsNumber();
                if (!priority.IsNumber() || !std::isfinite(value) || std::trunc(value) != value ||
                    value < std::numeric_limits<int32_t>::min() || value > std::numeric_limits<int32_t>::max())
                {
                    return Fail(result,"振動priorityがint32ではありません");
                }
                effect.Priority = static_cast<int32_t>(value);
            }
            if (!Curve(object.FindMember("low"),effect.Duration,effect.Low,result) ||
                !Curve(object.FindMember("high"),effect.Duration,effect.High,result))
            {
                return false;
            }
            effect.Id = Identity(name.AsString());
            const auto interned = effect.Id.GetView();
            if (interned.size() != name.AsString().size() ||
                std::memcmp(interned.data(),name.AsString().data(),interned.size()) != 0)
            {
                return Fail(result,"振動effect名が既存Identityと衝突しています");
            }
            for (const auto& previous : candidate.Effects)
            {
                if (previous.Id == effect.Id)
                {
                    return Fail(result,"振動effect名またはIdentityが重複しています");
                }
            }
            candidate.Effects.push_back(std::move(effect));
        }
        if (!IsValid(candidate))
        {
            return Fail(result,"振動設定が検証条件を満たしません");
        }
        out = std::move(candidate);
        return true;
    }
    bool Write(const HapticsConfiguration& configuration, Container::String& out)
    {
        if (!IsValid(configuration))
        {
            return false;
        }
        JsonWriter writer(true);
        writer.BeginObject();
        writer.WriteString("schema","haptics.v1");
        writer.BeginObject("settings");
        writer.WriteBool("enabled",configuration.Settings.Enabled);
        writer.WriteNumber("strength",configuration.Settings.Strength);
        writer.WriteString("mix_mode",configuration.Settings.MixMode == EHapticsMixMode::Maximum ? "maximum" : "add_clamp");
        writer.EndObject();
        writer.BeginArray("effects");
        for (const auto& effect : configuration.Effects)
        {
            writer.BeginObject();
            writer.WriteString("name",effect.Id.ToString());
            writer.WriteNumber("duration",effect.Duration);
            writer.WriteBool("loop",effect.Loop);
            writer.WriteInt64("priority",effect.Priority);
            WriteCurve(writer,"low",effect.Low);
            WriteCurve(writer,"high",effect.High);
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
        if (!writer.IsComplete())
        {
            return false;
        }
        auto candidate = writer.ToString();
        if (candidate.size() > MaximumTextBytes)
        {
            return false;
        }
        out = std::move(candidate);
        return true;
    }
} // namespace NorvesLib::Core::Input::HapticsJson

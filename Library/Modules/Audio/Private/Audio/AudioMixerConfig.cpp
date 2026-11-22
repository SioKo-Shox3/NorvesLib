// ミキサー設定の各項目を検証し、すべて有効な場合だけ音量とダッキングの上書きを確定する。
#include "Audio/AudioMixerConfig.h"

#include "Text/JsonDocument.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        using Core::JsonValue;
        using Core::Container::String;
        using Reason = AudioMixerConfigReason;

        template <size_t N>
        void CheckKeys(JsonValue object, const String& path, const char* const (&keys)[N], AudioMixerLoadReport& report)
        {
            bool seen[N] = {};
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const auto& name = object.GetMemberName(index);
                size_t key = 0;
                while (key < N && name != keys[key])
                {
                    ++key;
                }
                if (key == N || seen[key])
                {
                    report.Diagnostics.push_back({key == N ? Reason::UnknownKey : Reason::InvalidValue,
                                                  path.empty() ? name : path + "." + name});
                }
                else
                {
                    seen[key] = true;
                }
            }
        }

        void ReadDb(JsonValue value, const String& path, float upperBound, float& outDb, AudioMixerLoadReport& report)
        {
            const double db = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(db) || db < -96.0 || db > upperBound)
            {
                report.Diagnostics.push_back({value.IsValid() ? Reason::InvalidValue : Reason::MissingField, path});
                return;
            }
            outDb = static_cast<float>(db);
        }

        void ReadBuses(JsonValue buses, AudioMixerConfig& config, AudioMixerLoadReport& report)
        {
            if (!buses.IsObject())
            {
                report.Diagnostics.push_back({Reason::InvalidValue, "buses"});
                return;
            }

            bool seen[kAudioBusCount] = {};
            for (size_t index = 0; index < buses.GetObjectSize(); ++index)
            {
                const auto& name = buses.GetMemberName(index);
                const String path = "buses." + name;
                AudioBusId bus;
                if (!TryParseAudioBusName(name, bus))
                {
                    report.Diagnostics.push_back({Reason::UnknownKey, path});
                    continue;
                }
                const auto busIndex = static_cast<size_t>(bus);
                if (seen[busIndex])
                {
                    report.Diagnostics.push_back({Reason::InvalidValue, path});
                }
                seen[busIndex] = true;

                const auto object = buses.GetMemberValue(index);
                if (!object.IsObject())
                {
                    report.Diagnostics.push_back({Reason::InvalidValue, path});
                    continue;
                }
                constexpr const char* keys[] = {"volume_db", "pause"};
                CheckKeys(object, path, keys, report);
                auto& settings = config.Buses[busIndex];
                if (const auto volume = object.FindMember("volume_db"); volume.IsValid())
                {
                    ReadDb(volume, path + ".volume_db", 12.0f, settings.VolumeDb, report);
                }
                if (const auto pause = object.FindMember("pause"); pause.IsValid())
                {
                    if (pause.IsBoolean())
                    {
                        settings.bPauseAffected = pause.AsBool();
                    }
                    else
                    {
                        report.Diagnostics.push_back({Reason::InvalidValue, path + ".pause"});
                    }
                }
            }
        }

        void ReadBusId(JsonValue value, const String& path, AudioBusId& outBus, AudioMixerLoadReport& report)
        {
            if (!value.IsString() || !TryParseAudioBusName(value.AsString(), outBus))
            {
                report.Diagnostics.push_back({value.IsValid() ? Reason::InvalidValue : Reason::MissingField, path});
            }
        }

        void ReadTime(JsonValue object, const char* key, const String& path, uint32_t& outMs, AudioMixerLoadReport& report)
        {
            const auto value = object.FindMember(key);
            if (!value.IsValid())
            {
                return;
            }
            const double ms = value.AsNumber();
            if (!value.IsIntegerLiteral() || !(ms >= 0 && ms <= 60000))
            {
                report.Diagnostics.push_back({Reason::InvalidValue, path + "." + key});
                return;
            }
            outMs = static_cast<uint32_t>(ms);
        }

        void ReadDuckRule(JsonValue object, const String& path, AudioDuckRule& rule, AudioMixerLoadReport& report)
        {
            constexpr const char* keys[] = {"trigger_bus", "trigger_tag", "target", "duck_db", "attack_ms", "hold_ms", "release_ms"};
            CheckKeys(object, path, keys, report);
            const auto bus = object.FindMember("trigger_bus");
            const auto tag = object.FindMember("trigger_tag");
            if (bus.IsValid() == tag.IsValid())
            {
                // 必須なのは二つのキーのいずれかなので、片方の欠落ではなく規則の道筋を示す。
                report.Diagnostics.push_back({bus.IsValid() ? Reason::InvalidValue : Reason::MissingField, path});
            }
            if (bus.IsValid())
            {
                ReadBusId(bus, path + ".trigger_bus", rule.TriggerBus, report);
            }
            if (tag.IsValid())
            {
                if (tag.IsString() && !tag.AsString().empty())
                {
                    rule.TriggerKind = AudioDuckTriggerKind::DuckTag;
                    rule.TriggerTag = Core::Identity(tag.AsString());
                }
                else
                {
                    report.Diagnostics.push_back({Reason::InvalidValue, path + ".trigger_tag"});
                }
            }
            ReadBusId(object.FindMember("target"), path + ".target", rule.Target, report);
            ReadDb(object.FindMember("duck_db"), path + ".duck_db", 0.0f, rule.DuckDb, report);
            ReadTime(object, "attack_ms", path, rule.AttackMs, report);
            ReadTime(object, "hold_ms", path, rule.HoldMs, report);
            ReadTime(object, "release_ms", path, rule.ReleaseMs, report);
        }

        void ReadDucking(JsonValue ducking, AudioMixerConfig& config, AudioMixerLoadReport& report)
        {
            if (!ducking.IsArray())
            {
                report.Diagnostics.push_back({Reason::InvalidValue, "ducking"});
                return;
            }
            config.DuckRules.clear();
            for (size_t index = 0; index < ducking.GetArraySize(); ++index)
            {
                char path[40];
                std::snprintf(path, sizeof(path), "ducking[%zu]", index);
                const auto object = ducking.GetArrayElement(index);
                if (!object.IsObject())
                {
                    report.Diagnostics.push_back({Reason::InvalidValue, path});
                    continue;
                }
                AudioDuckRule rule;
                ReadDuckRule(object, path, rule, report);
                config.DuckRules.push_back(std::move(rule));
            }
        }
    }

    bool ParseAudioMixerConfig(Core::Container::Span<const uint8_t> jsonBytes, AudioMixerConfig& inOutConfig,
                               AudioMixerLoadReport& outReport)
    {
        outReport = {};
        if (jsonBytes.data() && jsonBytes.size() >= 3 && jsonBytes[0] == 0xef && jsonBytes[1] == 0xbb && jsonBytes[2] == 0xbf)
        {
            jsonBytes = {jsonBytes.data() + 3, jsonBytes.size() - 3};
        }
        Core::JsonDocument document;
        if (!jsonBytes.data() || jsonBytes.empty() || !Core::JsonDocument::TryParseUtf8(jsonBytes, document))
        {
            outReport.Diagnostics.push_back({Reason::ParseError, "$"});
            return false;
        }
        const auto root = document.GetRoot();
        if (!root.IsObject())
        {
            outReport.Diagnostics.push_back({Reason::InvalidValue, "$"});
            return false;
        }
        constexpr const char* keys[] = {"version", "buses", "ducking"};
        CheckKeys(root, "", keys, outReport);
        const auto version = root.FindMember("version");
        if (!version.IsNumber())
        {
            outReport.Diagnostics.push_back({version.IsValid() ? Reason::InvalidValue : Reason::MissingField, "version"});
        }
        else if (version.AsNumber() != 1.0)
        {
            outReport.Diagnostics.push_back({Reason::UnsupportedVersion, "version"});
        }

        AudioMixerConfig candidate = inOutConfig;
        if (const auto buses = root.FindMember("buses"); buses.IsValid())
        {
            ReadBuses(buses, candidate, outReport);
        }
        if (const auto ducking = root.FindMember("ducking"); ducking.IsValid())
        {
            ReadDucking(ducking, candidate, outReport);
        }
        if (!outReport.Diagnostics.empty())
        {
            return false;
        }
        inOutConfig = std::move(candidate);
        outReport.bUsedDefaults = false;
        return true;
    }
}

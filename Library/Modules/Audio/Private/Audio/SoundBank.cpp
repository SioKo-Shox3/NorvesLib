#include "Audio/SoundBank.h"

#include "Audio/AudioClipResource.h"
#include "Text/JsonDocument.h"

#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Modules::Audio::Private
{
    namespace
    {
        using Core::JsonValue;
        using Core::Container::String;

        constexpr const char* DefinitionKeys[] = {
            "id", "clips", "volume_db", "volume_jitter_db", "pitch_jitter_semitones",
            "loop", "loop_begin_frame", "loop_length_frames", "priority", "max_instances",
            "limit_policy", "cooldown_ms", "dedupe_window_ms", "dedupe_radius",
            "fade_in_ms", "fade_out_ms", "spatial", "category", "noise_radius", "source_ref"};

        template <size_t N>
        bool CheckKeys(JsonValue object, const char* const (&allowed)[N], SoundBankDiagnostic& diagnostic)
        {
            for (size_t index = 0; index < object.GetObjectSize(); ++index)
            {
                const auto& name = object.GetMemberName(index);
                bool bKnown = false;
                for (const char* key : allowed)
                {
                    if (name == key)
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    diagnostic.Reason = SoundBankRejectReason::UnknownKey;
                    diagnostic.Detail = name;
                    return false;
                }
                // 同じキーの重複で、先頭の値だけが採用される曖昧さを避ける。
                for (size_t previous = 0; previous < index; ++previous)
                {
                    if (name == object.GetMemberName(previous))
                    {
                        diagnostic.Reason = SoundBankRejectReason::InvalidValue;
                        diagnostic.Detail = name;
                        return false;
                    }
                }
            }
            return true;
        }

        bool ReadDefinition(JsonValue object, const SoundBankClipResolver& resolveClip,
                            SoundDefinition& definition, SoundBankDiagnostic& diagnostic)
        {
            diagnostic.Reason = SoundBankRejectReason::InvalidValue;
            if (!object.IsObject())
            {
                diagnostic.Detail = TEXT("定義はオブジェクトである必要があります");
                return false;
            }
            if (!CheckKeys(object, DefinitionKeys, diagnostic))
            {
                return false;
            }
            const auto id = object.FindMember("id");
            const auto clips = object.FindMember("clips");
            if (!id.IsValid() || !clips.IsValid())
            {
                diagnostic.Reason = SoundBankRejectReason::MissingField;
                diagnostic.Detail = !id.IsValid() ? TEXT("id") : TEXT("clips");
                return false;
            }
            if (!id.IsString() || id.AsString().empty())
            {
                diagnostic.Detail = TEXT("id");
                return false;
            }
            if (!clips.IsArray() || clips.GetArraySize() == 0)
            {
                diagnostic.Detail = TEXT("clips");
                return false;
            }
            for (size_t index = 0; index < clips.GetArraySize(); ++index)
            {
                if (!clips.GetArrayElement(index).IsString())
                {
                    diagnostic.Detail = TEXT("clips");
                    return false;
                }
            }

            struct FloatField
            {
                const char* Key;
                float* Value;
                bool bNonnegative;
            };
            const FloatField floats[] = {
                {"volume_db", &definition.VolumeDb, false},
                {"volume_jitter_db", &definition.VolumeJitterDb, true},
                {"pitch_jitter_semitones", &definition.PitchJitterSemitones, true},
                {"dedupe_radius", &definition.DedupeRadius, true},
                {"noise_radius", &definition.NoiseRadius, true}};
            for (const auto& field : floats)
            {
                const auto value = object.FindMember(field.Key);
                if (!value.IsValid())
                {
                    continue;
                }
                const double number = value.AsNumber();
                const double maximum = (std::numeric_limits<float>::max)();
                if (!value.IsNumber() || !std::isfinite(number) || number > maximum || number < -maximum ||
                    (field.bNonnegative && number < 0.0))
                {
                    diagnostic.Detail = String(field.Key);
                    return false;
                }
                *field.Value = static_cast<float>(number);
            }

            uint32_t priority = definition.Priority;
            struct UIntField
            {
                const char* Key;
                uint32_t* Value;
                uint32_t Maximum;
            };
            const UIntField integers[] = {
                {"priority", &priority, UINT8_MAX},
                {"loop_begin_frame", &definition.LoopBeginFrame, UINT32_MAX},
                {"loop_length_frames", &definition.LoopLengthFrames, UINT32_MAX},
                {"max_instances", &definition.MaxInstances, UINT32_MAX},
                {"cooldown_ms", &definition.CooldownMs, UINT32_MAX},
                {"dedupe_window_ms", &definition.DedupeWindowMs, UINT32_MAX},
                {"fade_in_ms", &definition.FadeInMs, UINT32_MAX},
                {"fade_out_ms", &definition.FadeOutMs, UINT32_MAX}};
            for (const auto& field : integers)
            {
                const auto value = object.FindMember(field.Key);
                if (!value.IsValid())
                {
                    continue;
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || number < 0.0 ||
                    number > field.Maximum || std::floor(number) != number)
                {
                    diagnostic.Detail = String(field.Key);
                    return false;
                }
                *field.Value = static_cast<uint32_t>(number);
            }
            definition.Priority = static_cast<uint8_t>(priority);

            const auto loop = object.FindMember("loop");
            if (loop.IsValid())
            {
                if (!loop.IsBoolean())
                {
                    diagnostic.Detail = TEXT("loop");
                    return false;
                }
                definition.bLoop = loop.AsBool();
            }
            const auto policy = object.FindMember("limit_policy");
            if (policy.IsValid())
            {
                if (!policy.IsString() || (policy.AsString() != "stop_oldest" && policy.AsString() != "reject_new"))
                {
                    diagnostic.Detail = TEXT("limit_policy");
                    return false;
                }
                definition.LimitPolicy = policy.AsString() == "stop_oldest"
                    ? SoundInstanceLimitPolicy::StopOldest : SoundInstanceLimitPolicy::RejectNew;
            }
            const auto spatial = object.FindMember("spatial");
            if (spatial.IsValid())
            {
                if (!spatial.IsString() || (spatial.AsString() != "2d" && spatial.AsString() != "3d"))
                {
                    diagnostic.Detail = TEXT("spatial");
                    return false;
                }
                definition.Spatial = spatial.AsString() == "2d" ? SoundSpatialMode::TwoD : SoundSpatialMode::ThreeD;
            }
            const auto category = object.FindMember("category");
            if (category.IsValid())
            {
                if (!category.IsString() || (category.AsString() != "effect" && category.AsString() != "ambience"))
                {
                    diagnostic.Detail = TEXT("category");
                    return false;
                }
                definition.Category = category.AsString() == "effect" ? SoundCategory::Effect : SoundCategory::Ambience;
            }
            const auto source = object.FindMember("source_ref");
            if (source.IsValid())
            {
                if (!source.IsString())
                {
                    diagnostic.Detail = TEXT("source_ref");
                    return false;
                }
                definition.SourceRef = source.AsString();
            }

            definition.Id = Core::Identity(id.AsString());
            for (size_t index = 0; index < clips.GetArraySize(); ++index)
            {
                const auto& path = clips.GetArrayElement(index).AsString();
                auto clip = resolveClip.IsBound() ? resolveClip(path) : Core::Container::TSharedPtr<AudioClipResource>{};
                if (!clip || !clip->IsValid())
                {
                    diagnostic.Reason = SoundBankRejectReason::MissingClip;
                    diagnostic.Detail = path;
                    return false;
                }
                if (definition.Spatial == SoundSpatialMode::ThreeD && clip->GetFormat().ChannelCount == 2)
                {
                    diagnostic.Reason = SoundBankRejectReason::SpatialStereoClip;
                    diagnostic.Detail = path;
                    return false;
                }
                definition.Clips.push_back(std::move(clip));
            }
            return true;
        }

        SoundBankParseResult ReadBank(const Core::JsonDocument& document)
        {
            SoundBankParseResult result;
            SoundBankDiagnostic diagnostic;
            const auto root = document.GetRoot();
            if (!root.IsObject())
            {
                diagnostic.Reason = SoundBankRejectReason::InvalidValue;
                diagnostic.Detail = TEXT("バンクはオブジェクトである必要があります");
            }
            else
            {
                const auto version = root.FindMember("version");
                const auto sounds = root.FindMember("sounds");
                constexpr const char* RootKeys[] = {"version", "sounds"};
                if (!version.IsNumber() || version.AsNumber() != 1.0)
                {
                    diagnostic.Reason = SoundBankRejectReason::UnsupportedVersion;
                    diagnostic.Detail = TEXT("version");
                }
                else if (!CheckKeys(root, RootKeys, diagnostic))
                {
                    // キー検査が診断を設定する。
                }
                else if (!sounds.IsArray() || sounds.GetArraySize() > UINT32_MAX)
                {
                    diagnostic.Reason = sounds.IsValid() ? SoundBankRejectReason::InvalidValue : SoundBankRejectReason::MissingField;
                    diagnostic.Detail = TEXT("sounds");
                }
                else
                {
                    result.bBankAccepted = true;
                    return result;
                }
            }
            result.Report.Diagnostics.push_back(std::move(diagnostic));
            return result;
        }

        SoundBankParseResult ParseDocument(const Core::JsonDocument& document, const SoundBankClipResolver& resolveClip)
        {
            auto result = ReadBank(document);
            if (!result.bBankAccepted)
            {
                return result;
            }
            const auto sounds = document.GetRoot().FindMember("sounds");
            Core::Container::VariableArray<String> seenNames;
            for (size_t index = 0; index < sounds.GetArraySize(); ++index)
            {
                const auto object = sounds.GetArrayElement(index);
                SoundBankDiagnostic diagnostic;
                diagnostic.DefinitionIndex = static_cast<uint32_t>(index);
                const auto id = object.FindMember("id");
                bool bDuplicate = false;
                if (id.IsString())
                {
                    diagnostic.DefinitionName = id.AsString();
                    if (!id.AsString().empty())
                    {
                        for (const auto& name : seenNames)
                        {
                            if (name == id.AsString())
                            {
                                bDuplicate = true;
                                break;
                            }
                        }
                        // 先の定義が拒否されても、その id の再使用は重複として扱う。
                        if (!bDuplicate)
                        {
                            seenNames.push_back(id.AsString());
                        }
                    }
                }
                SoundDefinition definition;
                if (bDuplicate)
                {
                    diagnostic.Reason = SoundBankRejectReason::DuplicateId;
                    diagnostic.Detail = id.AsString();
                }
                else if (ReadDefinition(object, resolveClip, definition, diagnostic))
                {
                    result.Definitions.push_back(std::move(definition));
                    ++result.Report.LoadedDefinitionCount;
                    continue;
                }
                ++result.Report.RejectedDefinitionCount;
                result.Report.Diagnostics.push_back(std::move(diagnostic));
            }
            return result;
        }

        SoundBankParseResult ParseFailure(String detail)
        {
            SoundBankParseResult result;
            SoundBankDiagnostic diagnostic;
            diagnostic.Detail = std::move(detail);
            result.Report.Diagnostics.push_back(std::move(diagnostic));
            return result;
        }
    } // namespace

    SoundBankParseResult ParseSoundBank(const Core::Container::String& jsonText,
                                        const SoundBankClipResolver& resolveClip)
    {
        Core::JsonDocument document;
        String error;
        if (!Core::JsonDocument::TryParse(jsonText, document, &error))
        {
            return ParseFailure(std::move(error));
        }
        return ParseDocument(document, resolveClip);
    }

    SoundBankParseResult ParseSoundBank(Core::Container::Span<const uint8_t> jsonBytes,
                                        const SoundBankClipResolver& resolveClip)
    {
        if (!jsonBytes.data() || jsonBytes.empty())
        {
            return ParseFailure(TEXT("バンクのバイト列が空です"));
        }
        if (jsonBytes.size() >= 3 && jsonBytes[0] == 0xef && jsonBytes[1] == 0xbb && jsonBytes[2] == 0xbf)
        {
            jsonBytes = {jsonBytes.data() + 3, jsonBytes.size() - 3};
        }
        Core::JsonDocument document;
        String error;
        if (!Core::JsonDocument::TryParseUtf8(jsonBytes, document, &error))
        {
            return ParseFailure(std::move(error));
        }
        return ParseDocument(document, resolveClip);
    }
} // namespace NorvesLib::Modules::Audio::Private

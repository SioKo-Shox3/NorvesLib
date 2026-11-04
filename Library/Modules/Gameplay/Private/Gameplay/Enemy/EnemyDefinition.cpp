#include "Gameplay/Enemy/EnemyDefinition.h"

#include "Asset/AssetFileReader.h"
#include "Gameplay/Progression/GameplayStableId.h"
#include "Text/JsonDocument.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace NorvesLib::Modules::Gameplay::Enemy
{
    namespace
    {
        using String = Core::Container::String;
        using JsonValue = Core::JsonValue;

        bool IsJsonNumber(const char* current, const char* end)
        {
            if (current != end && *current == '-') ++current;
            if (current == end) return false;
            if (*current == '0') ++current;
            else
            {
                if (*current < '1' || *current > '9') return false;
                while (current != end && *current >= '0' && *current <= '9') ++current;
            }
            if (current != end && *current == '.')
            {
                ++current;
                if (current == end || *current < '0' || *current > '9') return false;
                while (current != end && *current >= '0' && *current <= '9') ++current;
            }
            if (current != end && (*current == 'e' || *current == 'E'))
            {
                ++current;
                if (current != end && (*current == '+' || *current == '-')) ++current;
                if (current == end || *current < '0' || *current > '9') return false;
                while (current != end && *current >= '0' && *current <= '9') ++current;
            }
            return current == end;
        }

        // JsonDocument が double の範囲外で解析を止める数値も、対象のキーを示して拒否する。
        bool TryParseDefinition(const String& text, Core::JsonDocument& document)
        {
            if (Core::JsonDocument::TryParse(text, document)) return true;

            String adjusted = text;
            bool bInString = false;
            bool bEscaped = false;
            bool bAdjusted = false;
            for (size_t i = 0; i < adjusted.size(); ++i)
            {
                const char ch = adjusted[i];
                if (bInString)
                {
                    if (bEscaped) bEscaped = false;
                    else if (ch == '\\') bEscaped = true;
                    else if (ch == '"') bInString = false;
                    continue;
                }
                if (ch == '"')
                {
                    bInString = true;
                    continue;
                }
                if (ch != '-' && (ch < '0' || ch > '9')) continue;

                size_t end = i + 1;
                while (end < adjusted.size())
                {
                    const char unit = adjusted[end];
                    if ((unit < '0' || unit > '9') && unit != '.' && unit != 'e' && unit != 'E' &&
                        unit != '+' && unit != '-') break;
                    ++end;
                }
                double number = 0.0;
                const char* begin = adjusted.data() + i;
                const char* last = adjusted.data() + end;
                const auto parsed = std::from_chars(begin, last, number, std::chars_format::general);
                if (parsed.ec == std::errc::result_out_of_range && parsed.ptr == last && IsJsonNumber(begin, last))
                {
                    adjusted.replace(i, end - i, "1e39");
                    end = i + 4;
                    bAdjusted = true;
                }
                i = end - 1;
            }
            return bAdjusted && Core::JsonDocument::TryParse(adjusted, document);
        }

        String ChildPath(const String& parent, const String& key)
        {
            return parent.empty() ? key : parent + "." + key;
        }

        String MemberPath(const String& parent, const String& key)
        {
            bool bSimple = !key.empty();
            for (size_t i = 0; i < key.size(); ++i)
            {
                const char ch = key[i];
                if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                      (ch >= '0' && ch <= '9') || ch == '_')) bSimple = false;
            }
            if (bSimple) return ChildPath(parent, key);

            String result = parent + "[\"";
            for (size_t i = 0; i < key.size(); ++i)
            {
                const unsigned char ch = static_cast<unsigned char>(key[i]);
                if (ch < 0x20)
                {
                    char escaped[7];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(ch));
                    result += escaped;
                }
                else
                {
                    if (ch == '\\' || ch == '"') result += '\\';
                    result += static_cast<char>(ch);
                }
            }
            return result + "\"]";
        }

        String IndexPath(const String& parent, size_t index)
        {
            char suffix[32];
            std::snprintf(suffix, sizeof(suffix), "[%zu]", index);
            return parent + suffix;
        }

        enum class NumberRange
        {
            Any,
            NonNegative,
            Positive,
            UnitInterval
        };

        class DefinitionReader
        {
        public:
            DefinitionReader(const String& sourceName, String& error) : m_SourceName(sourceName), m_Error(error) {}

            bool Read(JsonValue root, EnemyDefinition& definition)
            {
                const char* const keys[] = {"id", "archetype", "isFinal", "maxHealth", "staggerThreshold",
                    "animMachine", "animStates", "alertDelaySeconds", "attackIntervalSeconds", "repeatPenalty",
                    "attackProfiles", "patterns", "openingPatternId", "collapse", "defeat", "defeatResponses"};
                if (!CheckMembers(root, "", keys) ||
                    !ReadString(root, "id", "", definition.Id, true, true) ||
                    !ReadString(root, "archetype", "", definition.Archetype, false, true) ||
                    !ReadBool(root, "isFinal", "", definition.bIsFinal, false) ||
                    !ReadFloat(root, "maxHealth", "", definition.MaxHealth, true, NumberRange::Positive) ||
                    !ReadFloat(root, "staggerThreshold", "", definition.StaggerThreshold, true, NumberRange::NonNegative) ||
                    !ReadIdentity(root, "animMachine", "", definition.AnimMachine, true) ||
                    !ReadAnimStates(root.FindMember("animStates"), definition.AnimStates) ||
                    !ReadFloat(root, "alertDelaySeconds", "", definition.AlertDelaySeconds, false, NumberRange::NonNegative) ||
                    !ReadFloat(root, "attackIntervalSeconds", "", definition.AttackIntervalSeconds, false, NumberRange::NonNegative) ||
                    !ReadFloat(root, "repeatPenalty", "", definition.RepeatPenalty, false, NumberRange::UnitInterval) ||
                    !ReadProfiles(root.FindMember("attackProfiles"), definition) ||
                    !ReadPatterns(root.FindMember("patterns"), definition) ||
                    !ReadString(root, "openingPatternId", "", definition.OpeningPatternId, false, false) ||
                    !ReadCollapse(root.FindMember("collapse"), definition.Collapse) ||
                    !ReadDefeat(root.FindMember("defeat"), definition.DefeatHoldSeconds) ||
                    !ReadResponses(root.FindMember("defeatResponses"), definition.DefeatResponses))
                {
                    return false;
                }
                if (!definition.OpeningPatternId.empty() && FindPatternIndex(definition, definition.OpeningPatternId) < 0)
                {
                    return Fail("openingPatternId", "未定義のパターンを参照しています");
                }
                return true;
            }

        private:
            bool Fail(const String& path, const char* reason)
            {
                m_Error = m_SourceName + ": " + (path.empty() ? String("$") : path) + ": " + reason;
                return false;
            }

            template <size_t N>
            bool CheckMembers(JsonValue object, const String& path, const char* const (&allowed)[N])
            {
                if (!object.IsObject())
                {
                    return Fail(path, object.IsValid() ? "オブジェクトが必要です" : "必須のキーがありません");
                }
                for (size_t i = 0; i < object.GetObjectSize(); ++i)
                {
                    const auto& name = object.GetMemberName(i);
                    bool bKnown = false;
                    for (const char* candidate : allowed)
                    {
                        if (name == candidate)
                        {
                            bKnown = true;
                            break;
                        }
                    }
                    const String memberPath = MemberPath(path, name);
                    if (!bKnown)
                    {
                        return Fail(memberPath, "未知のキーです");
                    }
                    for (size_t j = 0; j < i; ++j)
                    {
                        if (object.GetMemberName(j) == name)
                        {
                            return Fail(memberPath, "キーが重複しています");
                        }
                    }
                }
                return true;
            }

            bool ReadString(JsonValue object, const char* key, const String& path, String& out,
                            bool required, bool stableId)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                if (!value.IsString() || value.AsString().empty() || value.AsString().find('\0') != String::npos)
                {
                    return Fail(fieldPath, "空でない文字列が必要です");
                }
                if (stableId && !IsValidGameplayStableId(value.AsString()))
                {
                    return Fail(fieldPath, "安定 ID の形式が不正です");
                }
                out = value.AsString();
                return true;
            }

            bool ReadIdentity(JsonValue object, const char* key, const String& path, Core::Identity& out, bool required)
            {
                String value;
                if (!ReadString(object, key, path, value, required, false))
                {
                    return false;
                }
                if (!value.empty())
                {
                    out = Core::Identity(value);
                }
                return true;
            }

            bool ReadBool(JsonValue object, const char* key, const String& path, bool& out, bool required)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                if (!value.IsBoolean())
                {
                    return Fail(fieldPath, "真偽値が必要です");
                }
                out = value.AsBool();
                return true;
            }

            bool ReadFloat(JsonValue object, const char* key, const String& path, float& out,
                           bool required, NumberRange range)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) ||
                    std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
                {
                    return Fail(fieldPath, "float の範囲に入る有限の数値が必要です");
                }
                const float converted = static_cast<float>(number);
                if ((range == NumberRange::NonNegative && number < 0.0) ||
                    (range == NumberRange::Positive && (number <= 0.0 || converted <= 0.0f)) ||
                    (range == NumberRange::UnitInterval && (number < 0.0 || number > 1.0)))
                {
                    return Fail(fieldPath, "数値が許容範囲外です");
                }
                out = converted;
                return true;
            }

            bool ReadUInt(JsonValue object, const char* key, const String& path, uint32_t& out, bool required)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || number < 0.0 ||
                    number > static_cast<double>(std::numeric_limits<uint32_t>::max()) || std::floor(number) != number)
                {
                    return Fail(fieldPath, "0 以上の 32 ビット整数が必要です");
                }
                out = static_cast<uint32_t>(number);
                return true;
            }

            bool CheckArray(JsonValue array, const String& path, bool required, bool nonEmpty)
            {
                if (!array.IsValid())
                {
                    return !required || Fail(path, "必須のキーがありません");
                }
                if (!array.IsArray() || (nonEmpty && array.GetArraySize() == 0))
                {
                    return Fail(path, nonEmpty ? "1 件以上の配列が必要です" : "配列が必要です");
                }
                return true;
            }

            bool ReadAnimStates(JsonValue states, EnemyAnimStates& out)
            {
                const char* const keys[] = {"idle", "alerted", "collapsed", "recover", "defeated"};
                const String path = "animStates";
                return CheckMembers(states, path, keys) &&
                    ReadIdentity(states, "idle", path, out.Idle, true) &&
                    ReadIdentity(states, "alerted", path, out.Alerted, false) &&
                    ReadIdentity(states, "collapsed", path, out.Collapsed, false) &&
                    ReadIdentity(states, "recover", path, out.Recover, false) &&
                    ReadIdentity(states, "defeated", path, out.Defeated, true);
            }

            bool ReadProfiles(JsonValue profiles, EnemyDefinition& definition)
            {
                const String arrayPath = "attackProfiles";
                if (!CheckArray(profiles, arrayPath, true, true)) return false;
                for (size_t i = 0; i < profiles.GetArraySize(); ++i)
                {
                    const JsonValue value = profiles.GetArrayElement(i);
                    const String path = IndexPath(arrayPath, i);
                    const char* const keys[] = {"id", "damage", "stagger", "maxDurationSeconds", "hitStopSeconds"};
                    EnemyAttackProfileDef profile;
                    if (!CheckMembers(value, path, keys) ||
                        !ReadString(value, "id", path, profile.Id, true, true) ||
                        !ReadFloat(value, "damage", path, profile.Damage, true, NumberRange::NonNegative) ||
                        !ReadFloat(value, "stagger", path, profile.Stagger, true, NumberRange::NonNegative) ||
                        !ReadFloat(value, "maxDurationSeconds", path, profile.MaxDurationSeconds, true, NumberRange::Positive) ||
                        !ReadFloat(value, "hitStopSeconds", path, profile.HitStopSeconds, false, NumberRange::NonNegative))
                    {
                        return false;
                    }
                    if (FindAttackProfile(definition, profile.Id) != nullptr)
                    {
                        return Fail(ChildPath(path, "id"), "ID が重複しています");
                    }
                    definition.AttackProfiles.push_back(std::move(profile));
                }
                return true;
            }

            bool ReadPhases(JsonValue phases, const String& arrayPath, EnemyAttackPattern& pattern)
            {
                if (!CheckArray(phases, arrayPath, true, true)) return false;
                for (size_t i = 0; i < phases.GetArraySize(); ++i)
                {
                    const JsonValue value = phases.GetArrayElement(i);
                    const String path = IndexPath(arrayPath, i);
                    const char* const keys[] = {"id", "seconds", "hitWindow"};
                    EnemyAttackPhase phase;
                    if (!CheckMembers(value, path, keys) ||
                        !ReadString(value, "id", path, phase.Id, true, false) ||
                        !ReadFloat(value, "seconds", path, phase.Seconds, true, NumberRange::Positive) ||
                        !ReadBool(value, "hitWindow", path, phase.bHitWindow, false))
                    {
                        return false;
                    }
                    pattern.Phases.push_back(std::move(phase));
                }
                return true;
            }

            bool ReadPatterns(JsonValue patterns, EnemyDefinition& definition)
            {
                const String arrayPath = "patterns";
                if (!CheckArray(patterns, arrayPath, true, true)) return false;
                for (size_t i = 0; i < patterns.GetArraySize(); ++i)
                {
                    const JsonValue value = patterns.GetArrayElement(i);
                    const String path = IndexPath(arrayPath, i);
                    const char* const keys[] = {"id", "animState", "attackProfileId", "minDistance", "maxDistance",
                        "weight", "cooldownSeconds", "phases"};
                    EnemyAttackPattern pattern;
                    if (!CheckMembers(value, path, keys) ||
                        !ReadString(value, "id", path, pattern.Id, true, false) ||
                        !ReadIdentity(value, "animState", path, pattern.AnimState, true) ||
                        !ReadString(value, "attackProfileId", path, pattern.AttackProfileId, true, false) ||
                        !ReadFloat(value, "minDistance", path, pattern.MinDistance, true, NumberRange::NonNegative) ||
                        !ReadFloat(value, "maxDistance", path, pattern.MaxDistance, true, NumberRange::NonNegative) ||
                        !ReadFloat(value, "weight", path, pattern.Weight, true, NumberRange::NonNegative) ||
                        !ReadFloat(value, "cooldownSeconds", path, pattern.CooldownSeconds, true, NumberRange::NonNegative) ||
                        !ReadPhases(value.FindMember("phases"), ChildPath(path, "phases"), pattern))
                    {
                        return false;
                    }
                    if (value.FindMember("minDistance").AsNumber() > value.FindMember("maxDistance").AsNumber())
                    {
                        return Fail(ChildPath(path, "maxDistance"), "最小距離より小さくできません");
                    }
                    if (FindPatternIndex(definition, pattern.Id) >= 0)
                    {
                        return Fail(ChildPath(path, "id"), "ID が重複しています");
                    }
                    if (FindAttackProfile(definition, pattern.AttackProfileId) == nullptr)
                    {
                        return Fail(ChildPath(path, "attackProfileId"), "未定義の攻撃プロファイルを参照しています");
                    }
                    definition.Patterns.push_back(std::move(pattern));
                }
                return true;
            }

            bool ReadCollapse(JsonValue value, EnemyCollapseRule& collapse)
            {
                if (!value.IsValid()) return true;
                const String path = "collapse";
                const char* const keys[] = {"condition", "healthFraction", "partId", "hitCount", "recoverSeconds"};
                String condition;
                if (!CheckMembers(value, path, keys) || !ReadString(value, "condition", path, condition, false, false))
                {
                    return false;
                }
                if (condition == "healthBelow") collapse.Condition = EEnemyCollapseCondition::HealthBelow;
                else if (condition == "partHits") collapse.Condition = EEnemyCollapseCondition::PartHits;
                else if (!condition.empty() && condition != "stagger") return Fail("collapse.condition", "未知の崩れ条件です");

                const bool bHealthBelow = collapse.Condition == EEnemyCollapseCondition::HealthBelow;
                const bool bPartHits = collapse.Condition == EEnemyCollapseCondition::PartHits;
                if (!ReadFloat(value, "healthFraction", path, collapse.HealthFraction, bHealthBelow, NumberRange::Any) ||
                    !ReadUInt(value, "partId", path, collapse.PartId, bPartHits) ||
                    !ReadUInt(value, "hitCount", path, collapse.HitCount, bPartHits) ||
                    !ReadFloat(value, "recoverSeconds", path, collapse.RecoverSeconds, false, NumberRange::Positive))
                {
                    return false;
                }
                if (bHealthBelow && (value.FindMember("healthFraction").AsNumber() <= 0.0 ||
                    value.FindMember("healthFraction").AsNumber() >= 1.0 || collapse.HealthFraction <= 0.0f ||
                    collapse.HealthFraction >= 1.0f))
                {
                    return Fail("collapse.healthFraction", "0 より大きく 1 より小さい値が必要です");
                }
                if (bPartHits && collapse.HitCount == 0)
                {
                    return Fail("collapse.hitCount", "1 以上の値が必要です");
                }
                return true;
            }

            bool ReadDefeat(JsonValue value, float& holdSeconds)
            {
                if (!value.IsValid()) return true;
                const char* const keys[] = {"holdSeconds"};
                return CheckMembers(value, "defeat", keys) &&
                    ReadFloat(value, "holdSeconds", "defeat", holdSeconds, false, NumberRange::NonNegative);
            }

            bool ReadResponses(JsonValue responses, Core::Container::VariableArray<String>& out)
            {
                const String path = "defeatResponses";
                if (!CheckArray(responses, path, false, false)) return false;
                if (!responses.IsValid()) return true;
                for (size_t i = 0; i < responses.GetArraySize(); ++i)
                {
                    const JsonValue value = responses.GetArrayElement(i);
                    if (!value.IsString() || !IsValidGameplayStableId(value.AsString()))
                    {
                        return Fail(IndexPath(path, i), "安定 ID の形式が不正です");
                    }
                    out.push_back(value.AsString());
                }
                return true;
            }

            const String& m_SourceName;
            String& m_Error;
        };
    }

    Core::Container::TSharedPtr<const Combat::AttackProfile> MakeAttackProfile(const EnemyAttackProfileDef& definition)
    {
        auto profile = Core::Container::MakeShared<Combat::AttackProfile>();
        profile->ProfileId = Core::Identity(definition.Id);
        profile->Damage = definition.Damage;
        profile->Stagger = definition.Stagger;
        profile->MaxDurationSeconds = definition.MaxDurationSeconds;
        profile->HitStopSecondsHint = definition.HitStopSeconds;
        return profile;
    }

    const EnemyAttackProfileDef* FindAttackProfile(const EnemyDefinition& definition, const String& id)
    {
        for (const auto& profile : definition.AttackProfiles)
        {
            if (profile.Id == id) return &profile;
        }
        return nullptr;
    }

    int32_t FindPatternIndex(const EnemyDefinition& definition, const String& id)
    {
        for (size_t i = 0; i < definition.Patterns.size(); ++i)
        {
            if (definition.Patterns[i].Id == id) return static_cast<int32_t>(i);
        }
        return -1;
    }

    bool LoadEnemyDefinitionFromJson(const String& text, const String& sourceName, EnemyDefinition& out, String& outError)
    {
        Core::JsonDocument document;
        if (!TryParseDefinition(text, document))
        {
            outError = sourceName + ": JSON を解析できません";
            return false;
        }
        EnemyDefinition definition;
        definition.SourceName = sourceName;
        if (!DefinitionReader(sourceName, outError).Read(document.GetRoot(), definition))
        {
            return false;
        }
        out = std::move(definition);
        outError = "";
        return true;
    }

    bool LoadEnemyDefinitionFromAsset(const String& assetPath, EnemyDefinition& out, String& outError)
    {
        const auto result = Core::Asset::AssetFileReader().Read(Core::Container::AnsiStringView(assetPath.data(), assetPath.size()));
        if (!result.Succeeded())
        {
            outError = assetPath + ": 資産を読み込めません";
            return false;
        }
        String text;
        if (result.Blob.GetSize() != 0)
        {
            text.append(reinterpret_cast<const char*>(result.Blob.GetData()), result.Blob.GetSize());
        }
        return LoadEnemyDefinitionFromJson(text, assetPath, out, outError);
    }
} // namespace NorvesLib::Modules::Gameplay::Enemy

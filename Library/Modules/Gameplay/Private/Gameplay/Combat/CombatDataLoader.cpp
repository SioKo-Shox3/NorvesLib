// 戦闘判定の JSON を検証し、資産から読み込む。
#include "Gameplay/Combat/CombatData.h"

#include "Asset/AssetFileReader.h"
#include "Text/JsonDocument.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <utility>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace NorvesLib::Modules::Gameplay::Combat
{
    namespace
    {
        using String = Core::Container::String;
        using JsonValue = Core::JsonValue;

        bool IsJsonNumber(const char* current, const char* end)
        {
            if (current != end && *current == '-')
            {
                ++current;
            }
            if (current == end)
            {
                return false;
            }
            if (*current == '0')
            {
                ++current;
            }
            else
            {
                if (*current < '1' || *current > '9')
                {
                    return false;
                }
                while (current != end && *current >= '0' && *current <= '9')
                {
                    ++current;
                }
            }
            if (current != end && *current == '.')
            {
                ++current;
                if (current == end || *current < '0' || *current > '9')
                {
                    return false;
                }
                while (current != end && *current >= '0' && *current <= '9')
                {
                    ++current;
                }
            }
            if (current != end && (*current == 'e' || *current == 'E'))
            {
                ++current;
                if (current != end && (*current == '+' || *current == '-'))
                {
                    ++current;
                }
                if (current == end || *current < '0' || *current > '9')
                {
                    return false;
                }
                while (current != end && *current >= '0' && *current <= '9')
                {
                    ++current;
                }
            }
            return current == end;
        }

        // double の範囲外の数値も、そのキーの位置を示して拒否できるようにする。
        bool TryParseDefinition(const String& text, Core::JsonDocument& document)
        {
            if (Core::JsonDocument::TryParse(text, document))
            {
                return true;
            }
            String adjusted = text;
            bool bInString = false;
            bool bEscaped = false;
            bool bAdjusted = false;
            for (size_t i = 0; i < adjusted.size(); ++i)
            {
                const char ch = adjusted[i];
                if (bInString)
                {
                    if (bEscaped)
                    {
                        bEscaped = false;
                    }
                    else if (ch == '\\')
                    {
                        bEscaped = true;
                    }
                    else if (ch == '"')
                    {
                        bInString = false;
                    }
                    continue;
                }
                if (ch == '"')
                {
                    bInString = true;
                    continue;
                }
                if (ch != '-' && (ch < '0' || ch > '9'))
                {
                    continue;
                }
                size_t end = i + 1;
                while (end < adjusted.size())
                {
                    const char unit = adjusted[end];
                    if ((unit < '0' || unit > '9') && unit != '.' && unit != 'e' && unit != 'E' &&
                        unit != '+' && unit != '-')
                    {
                        break;
                    }
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
                      (ch >= '0' && ch <= '9') || ch == '_'))
                {
                    bSimple = false;
                }
            }
            if (bSimple)
            {
                return ChildPath(parent, key);
            }
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
                    if (ch == '\\' || ch == '"')
                    {
                        result += '\\';
                    }
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
            Positive
        };

        class DefinitionReader
        {
        public:
            DefinitionReader(const String& sourceName, String& error) : m_SourceName(sourceName), m_Error(error) {}

            bool ReadFactions(JsonValue root, CombatFactionTable& table)
            {
                const char* const keys[] = {"factions"};
                if (!CheckMembers(root, "", keys))
                {
                    return false;
                }
                const JsonValue factions = root.FindMember("factions");
                if (!factions.IsObject())
                {
                    return Fail("factions", factions.IsValid() ? "オブジェクトが必要です" : "必須のキーがありません");
                }
                for (size_t i = 0; i < factions.GetObjectSize(); ++i)
                {
                    const String& name = factions.GetMemberName(i);
                    const String path = MemberPath("factions", name);
                    if (name.empty() || name.find('\0') != String::npos)
                    {
                        return Fail(path, "空でない名前が必要です");
                    }
                    for (size_t j = 0; j < i; ++j)
                    {
                        if (factions.GetMemberName(j) == name)
                        {
                            return Fail(path, "名前が重複しています");
                        }
                    }
                    uint32_t index = 0;
                    if (!ReadUnsignedValue(factions.GetMemberValue(i), path, index))
                    {
                        return false;
                    }
                    if (index > 31)
                    {
                        return Fail(path, "ビット番号は 0 から 31 の範囲が必要です");
                    }
                    const uint32_t bit = uint32_t{1} << index;
                    for (uint32_t used : table.Bits)
                    {
                        if (used == bit)
                        {
                            return Fail(path, "ビット番号が重複しています");
                        }
                    }
                    table.Names.push_back(name);
                    table.Bits.push_back(bit);
                }
                return true;
            }

            bool ReadAttack(JsonValue root, const CombatFactionTable& factions, CombatAttackDefinition& definition)
            {
                const char* const keys[] = {"id", "damage", "stagger", "hitStopSeconds", "rehit",
                    "rehitIntervalSeconds", "perVictim", "hitFactions", "hitboxes"};
                AttackProfile common;
                String rehit;
                String perVictim;
                if (!CheckMembers(root, "", keys) ||
                    !ReadString(root, "id", "", definition.Id, true) ||
                    !ReadFloat(root, "damage", "", common.Damage, true, NumberRange::NonNegative) ||
                    !ReadFloat(root, "stagger", "", common.Stagger, false, NumberRange::NonNegative) ||
                    !ReadFloat(root, "hitStopSeconds", "", common.HitStopSecondsHint, false, NumberRange::NonNegative) ||
                    !ReadString(root, "rehit", "", rehit, false) ||
                    !ReadString(root, "perVictim", "", perVictim, false))
                {
                    return false;
                }
                if (rehit == "oncePerPart")
                {
                    common.RehitPolicy = ERehitPolicy::OncePerPart;
                }
                else if (rehit == "interval")
                {
                    common.RehitPolicy = ERehitPolicy::Interval;
                }
                else if (!rehit.empty() && rehit != "oncePerVictim")
                {
                    return Fail("rehit", "未知の再ヒット方式です");
                }
                if (perVictim == "highestPriority")
                {
                    common.PerVictimPolicy = EPerVictimPolicy::HighestPriority;
                }
                else if (perVictim == "allParts")
                {
                    common.PerVictimPolicy = EPerVictimPolicy::AllParts;
                }
                else if (!perVictim.empty() && perVictim != "firstPart")
                {
                    return Fail("perVictim", "未知の部位選択方式です");
                }
                const bool bInterval = common.RehitPolicy == ERehitPolicy::Interval;
                if (!ReadFloat(root, "rehitIntervalSeconds", "", common.RehitIntervalSeconds,
                               bInterval, bInterval ? NumberRange::Positive : NumberRange::NonNegative) ||
                    !ReadHitFactions(root.FindMember("hitFactions"), factions, common.HitMask))
                {
                    return false;
                }
                const JsonValue hitboxes = root.FindMember("hitboxes");
                if (!CheckArray(hitboxes, "hitboxes", true, true))
                {
                    return false;
                }
                for (size_t i = 0; i < hitboxes.GetArraySize(); ++i)
                {
                    const JsonValue value = hitboxes.GetArrayElement(i);
                    const String path = IndexPath("hitboxes", i);
                    const char* const hitboxKeys[] = {"joint", "windowEvent", "maxDurationSeconds", "shape"};
                    CombatHitboxDefinition hitbox;
                    AttackProfile profile = common;
                    String joint;
                    String window;
                    if (!CheckMembers(value, path, hitboxKeys) ||
                        !ReadString(value, "joint", path, joint, false, true) ||
                        !ReadString(value, "windowEvent", path, window, true) ||
                        !ReadFloat(value, "maxDurationSeconds", path, profile.MaxDurationSeconds, false, NumberRange::NonNegative) ||
                        !ReadShape(value.FindMember("shape"), ChildPath(path, "shape"), false, hitbox.Shape))
                    {
                        return false;
                    }
                    String profileId = definition.Id;
                    if (hitboxes.GetArraySize() > 1)
                    {
                        char suffix[32];
                        std::snprintf(suffix, sizeof(suffix), "#%zu", i);
                        profileId += suffix;
                    }
                    hitbox.JointName = Core::Identity(joint);
                    profile.ProfileId = Core::Identity(profileId);
                    profile.WindowEventName = Core::Identity(window);
                    hitbox.Profile = Core::Container::MakeShared<AttackProfile>(std::move(profile));
                    definition.Hitboxes.push_back(std::move(hitbox));
                }
                return true;
            }

            bool ReadHurtboxes(JsonValue root, const CombatFactionTable& factions, CombatHurtboxDefinition& definition)
            {
                const char* const keys[] = {"id", "weakJoint", "parts"};
                if (!CheckMembers(root, "", keys) || !ReadString(root, "id", "", definition.Id, true))
                {
                    return false;
                }
                const JsonValue weakJoint = root.FindMember("weakJoint");
                if (weakJoint.IsValid())
                {
                    const char* const weakKeys[] = {"minVertexCount", "minWeightSum"};
                    if (!CheckMembers(weakJoint, "weakJoint", weakKeys) ||
                        !ReadUnsigned(weakJoint, "minVertexCount", "weakJoint", definition.WeakJoint.MinVertexCount, true) ||
                        !ReadFloat(weakJoint, "minWeightSum", "weakJoint", definition.WeakJoint.MinWeightSum, true, NumberRange::NonNegative))
                    {
                        return false;
                    }
                }
                const JsonValue parts = root.FindMember("parts");
                if (!CheckArray(parts, "parts", true, false))
                {
                    return false;
                }
                for (size_t i = 0; i < parts.GetArraySize(); ++i)
                {
                    const JsonValue value = parts.GetArrayElement(i);
                    const String path = IndexPath("parts", i);
                    const char* const partKeys[] = {"partId", "joint", "surface", "faction", "damageScale", "priority", "flags", "shape"};
                    HurtboxPart part;
                    String joint;
                    String surface;
                    String faction;
                    if (!CheckMembers(value, path, partKeys) ||
                        !ReadUnsigned(value, "partId", path, part.PartId, true) ||
                        !ReadString(value, "joint", path, joint, false, true) ||
                        !ReadString(value, "surface", path, surface, true) ||
                        !ReadString(value, "faction", path, faction, true) ||
                        !ReadFloat(value, "damageScale", path, part.DamageScale, false, NumberRange::NonNegative) ||
                        !ReadPriority(value, path, part.Priority) ||
                        !ReadFlags(value.FindMember("flags"), ChildPath(path, "flags"), part.Flags) ||
                        !ReadShape(value.FindMember("shape"), ChildPath(path, "shape"), true, part.Shape))
                    {
                        return false;
                    }
                    if (!factions.FindBit(faction, part.Faction))
                    {
                        return Fail(ChildPath(path, "faction"), "未知の faction です");
                    }
                    for (const HurtboxPart& previous : definition.Parts)
                    {
                        if (previous.PartId == part.PartId)
                        {
                            return Fail(ChildPath(path, "partId"), "部位 ID が重複しています");
                        }
                    }
                    part.JointName = Core::Identity(joint);
                    part.SurfaceName = Core::Identity(surface);
                    definition.Parts.push_back(std::move(part));
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
                    const String& name = object.GetMemberName(i);
                    const String memberPath = MemberPath(path, name);
                    bool bKnown = false;
                    for (const char* candidate : allowed)
                    {
                        if (name == candidate)
                        {
                            bKnown = true;
                            break;
                        }
                    }
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
                            bool required, bool allowEmpty = false)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                if (!value.IsString() || (!allowEmpty && value.AsString().empty()) ||
                    value.AsString().find('\0') != String::npos)
                {
                    return Fail(fieldPath, allowEmpty ? "文字列が必要です" : "空でない文字列が必要です");
                }
                out = value.AsString();
                return true;
            }

            bool ReadFloatValue(JsonValue value, const String& path, float& out, NumberRange range)
            {
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) ||
                    std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
                {
                    return Fail(path, "float の範囲に入る有限の数値が必要です");
                }
                const float converted = static_cast<float>(number);
                if ((range == NumberRange::NonNegative && number < 0.0) ||
                    (range == NumberRange::Positive && (number <= 0.0 || converted <= 0.0f)))
                {
                    return Fail(path, range == NumberRange::Positive ? "正の数が必要です" : "0 以上の数が必要です");
                }
                out = converted;
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
                return ReadFloatValue(value, fieldPath, out, range);
            }

            bool ReadUnsignedValue(JsonValue value, const String& path, uint32_t& out)
            {
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || number < 0.0 ||
                    number > static_cast<double>(std::numeric_limits<uint32_t>::max()) || std::floor(number) != number)
                {
                    return Fail(path, "0 以上の 32 ビット整数が必要です");
                }
                out = static_cast<uint32_t>(number);
                return true;
            }

            bool ReadUnsigned(JsonValue object, const char* key, const String& path, uint32_t& out, bool required)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return !required || Fail(fieldPath, "必須のキーがありません");
                }
                return ReadUnsignedValue(value, fieldPath, out);
            }

            bool ReadPriority(JsonValue object, const String& path, int32_t& out)
            {
                const JsonValue value = object.FindMember("priority");
                if (!value.IsValid())
                {
                    return true;
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || std::floor(number) != number ||
                    number < static_cast<double>(std::numeric_limits<int32_t>::min()) ||
                    number > static_cast<double>(std::numeric_limits<int32_t>::max()))
                {
                    return Fail(ChildPath(path, "priority"), "32 ビット整数が必要です");
                }
                out = static_cast<int32_t>(number);
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

            bool ReadVector(JsonValue object, const char* key, const String& path, Math::Vector3& out, NumberRange range)
            {
                const JsonValue value = object.FindMember(key);
                const String fieldPath = ChildPath(path, key);
                if (!value.IsValid())
                {
                    return Fail(fieldPath, "必須のキーがありません");
                }
                if (!value.IsArray() || value.GetArraySize() != 3)
                {
                    return Fail(fieldPath, "3 成分の配列が必要です");
                }
                float values[3] = {};
                for (size_t i = 0; i < 3; ++i)
                {
                    if (!ReadFloatValue(value.GetArrayElement(i), IndexPath(fieldPath, i), values[i], range))
                    {
                        return false;
                    }
                }
                out = Math::Vector3(values[0], values[1], values[2]);
                return true;
            }

            bool ReadShape(JsonValue shape, const String& path, bool allowBox, CombatShape& out)
            {
                if (!shape.IsObject())
                {
                    return Fail(path, shape.IsValid() ? "オブジェクトが必要です" : "必須のキーがありません");
                }
                String type;
                if (!ReadString(shape, "type", path, type, true))
                {
                    return false;
                }
                if (type == "sphere")
                {
                    const char* const keys[] = {"type", "center", "radius"};
                    out.Type = ECombatShapeType::Sphere;
                    return CheckMembers(shape, path, keys) &&
                        ReadVector(shape, "center", path, out.Center, NumberRange::Any) &&
                        ReadFloat(shape, "radius", path, out.Radius, true, NumberRange::Positive);
                }
                if (type == "capsule")
                {
                    const char* const keys[] = {"type", "pointA", "pointB", "radius"};
                    out.Type = ECombatShapeType::Capsule;
                    return CheckMembers(shape, path, keys) &&
                        ReadVector(shape, "pointA", path, out.PointA, NumberRange::Any) &&
                        ReadVector(shape, "pointB", path, out.PointB, NumberRange::Any) &&
                        ReadFloat(shape, "radius", path, out.Radius, true, NumberRange::Positive);
                }
                if (type == "box" && allowBox)
                {
                    const char* const keys[] = {"type", "center", "halfExtents"};
                    out.Type = ECombatShapeType::Box;
                    return CheckMembers(shape, path, keys) &&
                        ReadVector(shape, "center", path, out.Center, NumberRange::Any) &&
                        ReadVector(shape, "halfExtents", path, out.HalfExtents, NumberRange::Positive);
                }
                return Fail(ChildPath(path, "type"), "使用できない形状です");
            }

            bool ReadHitFactions(JsonValue values, const CombatFactionTable& factions, uint32_t& mask)
            {
                const String path = "hitFactions";
                if (!CheckArray(values, path, true, true))
                {
                    return false;
                }
                mask = 0;
                for (size_t i = 0; i < values.GetArraySize(); ++i)
                {
                    const JsonValue value = values.GetArrayElement(i);
                    const String itemPath = IndexPath(path, i);
                    if (!value.IsString() || value.AsString().empty() || value.AsString().find('\0') != String::npos)
                    {
                        return Fail(itemPath, "空でない faction 名が必要です");
                    }
                    uint32_t bit = 0;
                    if (!factions.FindBit(value.AsString(), bit))
                    {
                        return Fail(itemPath, "未知の faction です");
                    }
                    mask |= bit;
                }
                return true;
            }

            bool ReadFlags(JsonValue values, const String& path, EHurtboxFlags& out)
            {
                if (!CheckArray(values, path, false, false) || !values.IsValid())
                {
                    return !values.IsValid();
                }
                uint32_t flags = 0;
                for (size_t i = 0; i < values.GetArraySize(); ++i)
                {
                    const JsonValue value = values.GetArrayElement(i);
                    const String itemPath = IndexPath(path, i);
                    if (!value.IsString())
                    {
                        return Fail(itemPath, "フラグ名の文字列が必要です");
                    }
                    uint32_t flag = 0;
                    if (value.AsString() == "weak")
                    {
                        flag = static_cast<uint32_t>(EHurtboxFlags::Weak);
                    }
                    else if (value.AsString() == "anchorable")
                    {
                        flag = static_cast<uint32_t>(EHurtboxFlags::Anchorable);
                    }
                    else
                    {
                        return Fail(itemPath, "未知のフラグです");
                    }
                    if ((flags & flag) != 0)
                    {
                        return Fail(itemPath, "フラグが重複しています");
                    }
                    flags |= flag;
                }
                out = static_cast<EHurtboxFlags>(flags);
                return true;
            }

            const String& m_SourceName;
            String& m_Error;
        };

        bool ReadAsset(const String& assetPath, String& text, String& error)
        {
            const auto result = Core::Asset::AssetFileReader().Read(Core::Container::AnsiStringView(assetPath.data(), assetPath.size()));
            if (!result.Succeeded())
            {
                error = assetPath + ": $: 資産を読み込めません";
                return false;
            }
            if (result.Blob.GetSize() != 0)
            {
                text.append(reinterpret_cast<const char*>(result.Blob.GetData()), result.Blob.GetSize());
            }
            return true;
        }
    }

    bool CombatFactionTable::FindBit(const String& name, uint32_t& outBit) const
    {
        for (size_t i = 0; i < Names.size() && i < Bits.size(); ++i)
        {
            if (Names[i] == name)
            {
                outBit = Bits[i];
                return true;
            }
        }
        return false;
    }

    bool LoadCombatFactionsFromJson(const String& text, const String& sourceName, CombatFactionTable& out, String& outError)
    {
        Core::JsonDocument document;
        if (!TryParseDefinition(text, document))
        {
            outError = sourceName + ": $: JSON を解析できません";
            return false;
        }
        CombatFactionTable table;
        if (!DefinitionReader(sourceName, outError).ReadFactions(document.GetRoot(), table))
        {
            return false;
        }
        out = std::move(table);
        outError = "";
        return true;
    }

    bool LoadCombatAttackFromJson(const String& text, const String& sourceName, const CombatFactionTable& factions,
                                  CombatAttackDefinition& out, String& outError)
    {
        Core::JsonDocument document;
        if (!TryParseDefinition(text, document))
        {
            outError = sourceName + ": $: JSON を解析できません";
            return false;
        }
        CombatAttackDefinition definition;
        definition.SourceName = sourceName;
        if (!DefinitionReader(sourceName, outError).ReadAttack(document.GetRoot(), factions, definition))
        {
            return false;
        }
        out = std::move(definition);
        outError = "";
        return true;
    }

    bool LoadCombatHurtboxesFromJson(const String& text, const String& sourceName, const CombatFactionTable& factions,
                                     CombatHurtboxDefinition& out, String& outError)
    {
        Core::JsonDocument document;
        if (!TryParseDefinition(text, document))
        {
            outError = sourceName + ": $: JSON を解析できません";
            return false;
        }
        CombatHurtboxDefinition definition;
        definition.SourceName = sourceName;
        if (!DefinitionReader(sourceName, outError).ReadHurtboxes(document.GetRoot(), factions, definition))
        {
            return false;
        }
        out = std::move(definition);
        outError = "";
        return true;
    }

    bool LoadCombatFactionsFromAsset(const String& assetPath, CombatFactionTable& out, String& outError)
    {
        String text;
        return ReadAsset(assetPath, text, outError) && LoadCombatFactionsFromJson(text, assetPath, out, outError);
    }

    bool LoadCombatAttackFromAsset(const String& assetPath, const CombatFactionTable& factions,
                                   CombatAttackDefinition& out, String& outError)
    {
        String text;
        return ReadAsset(assetPath, text, outError) && LoadCombatAttackFromJson(text, assetPath, factions, out, outError);
    }

    bool LoadCombatHurtboxesFromAsset(const String& assetPath, const CombatFactionTable& factions,
                                      CombatHurtboxDefinition& out, String& outError)
    {
        String text;
        return ReadAsset(assetPath, text, outError) && LoadCombatHurtboxesFromJson(text, assetPath, factions, out, outError);
    }
} // namespace NorvesLib::Modules::Gameplay::Combat

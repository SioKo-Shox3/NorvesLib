// しがみつきの調整値を検証し、JSON 資産から読み込む。
#include "Gameplay/Grip/GripTuning.h"

#include "Asset/AssetFileReader.h"
#include "Text/JsonDocument.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#endif

namespace NorvesLib::Modules::Gameplay::Grip
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

        class TuningReader
        {
        public:
            TuningReader(const String& sourceName, String& error) : m_SourceName(sourceName), m_Error(error) {}

            bool Read(JsonValue root, GripTuning& tuning)
            {
                if (!CheckMembers(root) ||
                    !ReadFloat(root, "freeLoadG", tuning.FreeLoadG, true) ||
                    !ReadFloat(root, "panicLoadG", tuning.PanicLoadG, true) ||
                    !ReadFloat(root, "baseDrainPerSecond", tuning.BaseDrainPerSecond) ||
                    !ReadFloat(root, "loadDrainPerSecond", tuning.LoadDrainPerSecond) ||
                    !ReadFloat(root, "recoveryPerSecond", tuning.RecoveryPerSecond) ||
                    !ReadFloat(root, "smoothingSeconds", tuning.SmoothingSeconds) ||
                    !ReadFloat(root, "tensionWeight", tuning.TensionWeight) ||
                    !ReadFloat(root, "accelerationWeight", tuning.AccelerationWeight) ||
                    !ReadSubsteps(root, tuning.MaxSubsteps) ||
                    !ReadFloat(root, "maxLoadG", tuning.MaxLoadG) ||
                    !ReadFloat(root, "swingDampingPerSecond", tuning.SwingDampingPerSecond))
                {
                    return false;
                }
                if (root.FindMember("panicLoadG").AsNumber() <= root.FindMember("freeLoadG").AsNumber() ||
                    tuning.PanicLoadG <= tuning.FreeLoadG)
                {
                    return Fail("panicLoadG", "freeLoadG より大きい数が必要です");
                }
                if (tuning.TensionWeight <= 0.0f && tuning.AccelerationWeight <= 0.0f)
                {
                    return Fail("accelerationWeight", "tensionWeight と accelerationWeight の和は正である必要があります");
                }
                if (root.FindMember("maxLoadG").AsNumber() < root.FindMember("panicLoadG").AsNumber() ||
                    tuning.MaxLoadG < tuning.PanicLoadG)
                {
                    return Fail("maxLoadG", "panicLoadG 以上の数が必要です");
                }
                return true;
            }

        private:
            bool Fail(const String& path, const char* reason)
            {
                m_Error = m_SourceName + ": " + (path.empty() ? String("$") : path) + ": " + reason;
                return false;
            }

            bool CheckMembers(JsonValue object)
            {
                const char* const allowed[] = {"freeLoadG", "panicLoadG", "baseDrainPerSecond", "loadDrainPerSecond",
                    "recoveryPerSecond", "smoothingSeconds", "tensionWeight", "accelerationWeight", "maxSubsteps",
                    "maxLoadG", "swingDampingPerSecond"};
                if (!object.IsObject())
                {
                    return Fail("", "オブジェクトが必要です");
                }
                for (size_t i = 0; i < object.GetObjectSize(); ++i)
                {
                    const String& name = object.GetMemberName(i);
                    const String memberPath = MemberPath("", name);
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

            bool ReadFloat(JsonValue object, const char* key, float& out, bool bPositive = false)
            {
                const JsonValue value = object.FindMember(key);
                if (!value.IsValid())
                {
                    return Fail(key, "必須のキーがありません");
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) ||
                    std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
                {
                    return Fail(key, "float の範囲に入る有限の数値が必要です");
                }
                const float converted = static_cast<float>(number);
                if (number < 0.0 || (bPositive && (number <= 0.0 || converted <= 0.0f)))
                {
                    return Fail(key, bPositive ? "正の数が必要です" : "0 以上の数が必要です");
                }
                out = converted;
                return true;
            }

            bool ReadSubsteps(JsonValue object, uint32_t& out)
            {
                const char* key = "maxSubsteps";
                const JsonValue value = object.FindMember(key);
                if (!value.IsValid())
                {
                    return Fail(key, "必須のキーがありません");
                }
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || std::floor(number) != number ||
                    number < 1.0 || number > 8.0)
                {
                    return Fail(key, "1 から 8 の整数が必要です");
                }
                out = static_cast<uint32_t>(number);
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

    bool LoadGripTuningFromJson(const String& text, const String& sourceName, GripTuning& out, String& outError)
    {
        Core::JsonDocument document;
        if (!TryParseDefinition(text, document))
        {
            outError = sourceName + ": $: JSON を解析できません";
            return false;
        }
        GripTuning tuning;
        if (!TuningReader(sourceName, outError).Read(document.GetRoot(), tuning))
        {
            return false;
        }
        out = tuning;
        outError = "";
        return true;
    }

    bool LoadGripTuningFromAsset(const String& assetPath, GripTuning& out, String& outError)
    {
        String text;
        return ReadAsset(assetPath, text, outError) && LoadGripTuningFromJson(text, assetPath, out, outError);
    }
} // namespace NorvesLib::Modules::Gameplay::Grip

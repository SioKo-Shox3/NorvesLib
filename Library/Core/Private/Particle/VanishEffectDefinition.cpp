#include "Particle/VanishEffectDefinition.h"

#include "Particle/VfxEffectSerialization.h"
#include "Platform/PlatformFile.h"
#include "Text/JsonDocument.h"
#include "Text/JsonUnicodeScalar.h"
#include "Text/UnicodeText.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace NorvesLib::Core::Particle
{
    namespace
    {
        // 制御文字を表示用に逃がし、外部から渡された文字列でもエラーを一行に保つ。
        void AppendEscaped(Container::String& destination, const Container::String& source)
        {
            constexpr char hex[] = "0123456789abcdef";
            for (size_t index = 0u; index < source.size(); ++index)
            {
                const auto character = source[index];
                const uint32_t value = static_cast<uint32_t>(static_cast<unsigned char>(character));
                if (value < 0x20u || value == 0x7fu)
                {
                    destination += "\\x";
                    destination.push_back(hex[value >> 4u]);
                    destination.push_back(hex[value & 0x0fu]);
                }
                else
                {
                    destination.push_back(character);
                }
            }
        }

        void CopyString(const Container::String& source, Container::String& destination)
        {
            Container::String copy;
            copy.reserve(source.size());
            for (size_t index = 0u; index < source.size(); ++index)
            {
                copy.push_back(source[index]);
            }
            destination = std::move(copy);
        }

        bool SameString(const Container::String& first, const Container::String& second)
        {
            if (first.size() != second.size())
            {
                return false;
            }
            for (size_t index = 0u; index < first.size(); ++index)
            {
                if (first[index] != second[index])
                {
                    return false;
                }
            }
            return true;
        }

        bool SameLiteral(const Container::String& value, const char* literal)
        {
            size_t index = 0u;
            while (index < value.size() && literal[index] != '\0')
            {
                if (value[index] != literal[index])
                {
                    return false;
                }
                ++index;
            }
            return index == value.size() && literal[index] == '\0';
        }

        Container::String MemberPath(const Container::String& parent, const char* member)
        {
            Container::String path = parent;
            if (!path.empty())
            {
                path += ".";
            }
            path += member;
            return path;
        }

        Container::String MemberPath(const Container::String& parent, const Container::String& member)
        {
            Container::String path = parent;
            if (!path.empty())
            {
                path += ".";
            }
            AppendEscaped(path, member);
            return path;
        }

        Container::String IndexPath(const Container::String& parent, size_t index)
        {
            Container::String path = parent;
            path += "[";
            char digits[3u * sizeof(size_t) + 1u];
            size_t count = 0u;
            do
            {
                digits[count++] = static_cast<char>('0' + index % 10u);
                index /= 10u;
            } while (index != 0u);
            while (count != 0u)
            {
                path.push_back(digits[--count]);
            }
            path += "]";
            return path;
        }

        bool Fail(const Container::String& source, const Container::String& path,
            const char8_t* reason, Container::String& error)
        {
            error.clear();
            AppendEscaped(error, source);
            error += ": ";
            if (path.empty())
            {
                error += "$";
            }
            else
            {
                AppendEscaped(error, path);
            }
            error += ": ";
            error += reinterpret_cast<const char*>(reason);
            return false;
        }

        bool IsValidText(const Container::String& text)
        {
            return TextDetail::ForEachUnicodeScalar<Container::String::value_type>(
                {text.data(), text.size()}, [](uint32_t) {});
        }

        struct JsonPathFrame
        {
            Container::String Path;
            Container::String Member;
            size_t NextIndex = 0u;
            bool bObject = false;
            bool bExpectKey = false;
        };

        Container::String CurrentValuePath(const VariableArray<JsonPathFrame>& frames)
        {
            if (frames.empty())
            {
                return "";
            }
            const JsonPathFrame& frame = frames.back();
            return frame.bObject ? MemberPath(frame.Path, frame.Member) : IndexPath(frame.Path, frame.NextIndex);
        }

        void ConsumeValue(VariableArray<JsonPathFrame>& frames)
        {
            if (!frames.empty() && !frames.back().bObject)
            {
                ++frames.back().NextIndex;
            }
        }

        // パーサーが構文エラーで文書を返せないとき、直前までのキーと配列位置をたどる。
        Container::String FindParseErrorPath(const Container::String& text, const Container::String& parseError)
        {
            size_t digitStart = parseError.size();
            while (digitStart > 0u && parseError[digitStart - 1u] >= '0' && parseError[digitStart - 1u] <= '9')
            {
                --digitStart;
            }
            if (digitStart == parseError.size())
            {
                return "";
            }
            size_t position = 0u;
            for (size_t index = digitStart; index < parseError.size(); ++index)
            {
                const size_t digit = static_cast<size_t>(parseError[index] - '0');
                if (position > (text.size() - (digit <= text.size() ? digit : 0u)) / 10u || digit > text.size())
                {
                    return "";
                }
                position = position * 10u + digit;
            }
            if (position > text.size())
            {
                return "";
            }
            VariableArray<JsonPathFrame> frames;
            Container::String lastPath;
            size_t index = 0u;
            while (index < position)
            {
                const auto character = text[index];
                if (character == ' ' || character == '\t' || character == '\r' || character == '\n' || character == ':')
                {
                    ++index;
                    continue;
                }
                if (character == ',')
                {
                    if (!frames.empty() && frames.back().bObject)
                    {
                        frames.back().bExpectKey = true;
                    }
                    ++index;
                    continue;
                }
                if (character == '}' || character == ']')
                {
                    if (frames.empty())
                    {
                        break;
                    }
                    lastPath = frames.back().Path;
                    frames.pop_back();
                    ++index;
                    continue;
                }
                if (character == '{' || character == '[')
                {
                    Container::String path = CurrentValuePath(frames);
                    lastPath = path;
                    ConsumeValue(frames);
                    JsonPathFrame frame;
                    frame.Path = std::move(path);
                    frame.bObject = character == '{';
                    frame.bExpectKey = frame.bObject;
                    frames.push_back(std::move(frame));
                    ++index;
                    continue;
                }
                if (character == '"')
                {
                    const bool bKey = !frames.empty() && frames.back().bObject && frames.back().bExpectKey;
                    if (!bKey)
                    {
                        lastPath = CurrentValuePath(frames);
                        ConsumeValue(frames);
                    }
                    const size_t start = index++;
                    bool bEscaped = false;
                    bool bClosed = false;
                    while (index < position)
                    {
                        const auto unit = text[index++];
                        if (bEscaped)
                        {
                            bEscaped = false;
                        }
                        else if (unit == '\\')
                        {
                            bEscaped = true;
                        }
                        else if (unit == '"')
                        {
                            bClosed = true;
                            break;
                        }
                    }
                    if (bKey && bClosed)
                    {
                        Container::String token;
                        token.reserve(index - start);
                        for (size_t tokenIndex = start; tokenIndex < index; ++tokenIndex)
                        {
                            token.push_back(text[tokenIndex]);
                        }
                        JsonDocument keyDocument;
                        if (JsonDocument::TryParse(token, keyDocument) && keyDocument.GetRoot().IsString())
                        {
                            CopyString(keyDocument.GetRoot().AsString(), frames.back().Member);
                            frames.back().bExpectKey = false;
                            lastPath = MemberPath(frames.back().Path, frames.back().Member);
                        }
                    }
                    continue;
                }
                lastPath = CurrentValuePath(frames);
                ConsumeValue(frames);
                while (index < position && text[index] != ',' && text[index] != '}' && text[index] != ']' &&
                    text[index] != ' ' && text[index] != '\t' && text[index] != '\r' && text[index] != '\n')
                {
                    ++index;
                }
            }
            return lastPath;
        }

        bool CheckKeys(const JsonValue& object, const Container::String& source, const Container::String& path,
            const char* const* keys, size_t keyCount, Container::String& error)
        {
            if (!object.IsObject())
            {
                return Fail(source, path, u8"オブジェクトが必要です", error);
            }
            for (size_t index = 0u; index < object.GetObjectSize(); ++index)
            {
                const Container::String& name = object.GetMemberName(index);
                const Container::String memberPath = MemberPath(path, name);
                for (size_t previous = 0u; previous < index; ++previous)
                {
                    if (SameString(name, object.GetMemberName(previous)))
                    {
                        return Fail(source, memberPath, u8"同じキーを重複して指定できません", error);
                    }
                }
                bool bKnown = false;
                for (size_t keyIndex = 0u; keyIndex < keyCount; ++keyIndex)
                {
                    if (SameLiteral(name, keys[keyIndex]))
                    {
                        bKnown = true;
                        break;
                    }
                }
                if (!bKnown)
                {
                    return Fail(source, memberPath, u8"未知のキーは使えません", error);
                }
            }
            return true;
        }

        bool RequireMember(const JsonValue& object, const Container::String& source, const Container::String& path,
            const char* key, JsonValue& value, Container::String& error)
        {
            value = object.FindMember(key);
            if (!value.IsValid())
            {
                return Fail(source, MemberPath(path, key), u8"必須のキーがありません", error);
            }
            return true;
        }

        bool ReadString(const JsonValue& value, const Container::String& source, const Container::String& path,
            Container::String& result, Container::String& error)
        {
            if (!value.IsString())
            {
                return Fail(source, path, u8"文字列が必要です", error);
            }
            if (value.AsString().empty())
            {
                return Fail(source, path, u8"空の文字列は使えません", error);
            }
            CopyString(value.AsString(), result);
            return true;
        }

        bool ReadNumber(const JsonValue& value, const Container::String& source, const Container::String& path,
            double& result, Container::String& error)
        {
            if (!value.IsNumber())
            {
                return Fail(source, path, u8"数値が必要です", error);
            }
            result = value.AsNumber();
            if (!std::isfinite(result) || result < -static_cast<double>(FLT_MAX) || result > static_cast<double>(FLT_MAX))
            {
                return Fail(source, path, u8"有限の浮動小数点数で表せる値が必要です", error);
            }
            return true;
        }

        bool ReadTime(const JsonValue& value, const Container::String& source, const Container::String& path,
            double duration, double& result, Container::String& error)
        {
            if (!ReadNumber(value, source, path, result, error))
            {
                return false;
            }
            if (result < 0.0)
            {
                return Fail(source, path, u8"負の時刻は使えません", error);
            }
            if (result > duration)
            {
                return Fail(source, path, u8"duration を超える時刻は使えません", error);
            }
            return true;
        }

        bool ReadThreshold(const JsonValue& dissolve, const Container::String& source, double duration,
            VanishEffectDefinition& definition, Container::String& error)
        {
            const Container::String path = "dissolve";
            constexpr const char* keys[] = {"edgeEmissiveNits", "threshold"};
            if (!CheckKeys(dissolve, source, path, keys, 2u, error))
            {
                return false;
            }
            const JsonValue edge = dissolve.FindMember("edgeEmissiveNits");
            if (edge.IsValid())
            {
                const Container::String edgePath = MemberPath(path, "edgeEmissiveNits");
                double value = 0.0;
                if (!ReadNumber(edge, source, edgePath, value, error))
                {
                    return false;
                }
                if (value < 0.0)
                {
                    return Fail(source, edgePath, u8"0 以上の値が必要です", error);
                }
                definition.EdgeEmissiveNits = static_cast<float>(value);
            }
            JsonValue threshold;
            if (!RequireMember(dissolve, source, path, "threshold", threshold, error))
            {
                return false;
            }
            const Container::String thresholdPath = MemberPath(path, "threshold");
            if (!threshold.IsArray())
            {
                return Fail(source, thresholdPath, u8"配列が必要です", error);
            }
            if (threshold.GetArraySize() == 0u)
            {
                return Fail(source, thresholdPath, u8"キーを 1 個以上指定してください", error);
            }
            double previousTime = 0.0;
            for (size_t index = 0u; index < threshold.GetArraySize(); ++index)
            {
                const Container::String keyPath = IndexPath(thresholdPath, index);
                const JsonValue key = threshold.GetArrayElement(index);
                constexpr const char* keyNames[] = {"time", "value"};
                if (!CheckKeys(key, source, keyPath, keyNames, 2u, error))
                {
                    return false;
                }
                JsonValue timeJson;
                JsonValue valueJson;
                if (!RequireMember(key, source, keyPath, "time", timeJson, error) ||
                    !RequireMember(key, source, keyPath, "value", valueJson, error))
                {
                    return false;
                }
                const Container::String timePath = MemberPath(keyPath, "time");
                const Container::String valuePath = MemberPath(keyPath, "value");
                double time = 0.0;
                double value = 0.0;
                if (!ReadTime(timeJson, source, timePath, duration, time, error) ||
                    !ReadNumber(valueJson, source, valuePath, value, error))
                {
                    return false;
                }
                if (index != 0u && time < previousTime)
                {
                    return Fail(source, timePath, u8"時刻は昇順で指定してください", error);
                }
                if (value < 0.0 || value > 1.0)
                {
                    return Fail(source, valuePath, u8"0 以上 1 以下の値が必要です", error);
                }
                definition.Threshold.push_back({static_cast<float>(time), static_cast<float>(value)});
                previousTime = time;
            }
            return true;
        }

        bool ReadEmitterTrack(const JsonValue& json, const Container::String& source, const Container::String& path,
            double duration, VanishEmitterTrack& track, Container::String& error)
        {
            constexpr const char* names[] = {"emitter", "keys"};
            if (!CheckKeys(json, source, path, names, 2u, error))
            {
                return false;
            }
            JsonValue emitter;
            JsonValue keys;
            if (!RequireMember(json, source, path, "emitter", emitter, error) ||
                !RequireMember(json, source, path, "keys", keys, error) ||
                !ReadString(emitter, source, MemberPath(path, "emitter"), track.EmitterName, error))
            {
                return false;
            }
            const Container::String keysPath = MemberPath(path, "keys");
            if (!keys.IsArray())
            {
                return Fail(source, keysPath, u8"配列が必要です", error);
            }
            double previousTime = 0.0;
            for (size_t index = 0u; index < keys.GetArraySize(); ++index)
            {
                const Container::String keyPath = IndexPath(keysPath, index);
                const JsonValue key = keys.GetArrayElement(index);
                constexpr const char* keyNames[] = {"time", "enabled", "rate"};
                if (!CheckKeys(key, source, keyPath, keyNames, 3u, error))
                {
                    return false;
                }
                JsonValue timeJson;
                JsonValue enabled;
                JsonValue rateJson;
                if (!RequireMember(key, source, keyPath, "time", timeJson, error) ||
                    !RequireMember(key, source, keyPath, "enabled", enabled, error) ||
                    !RequireMember(key, source, keyPath, "rate", rateJson, error))
                {
                    return false;
                }
                const Container::String timePath = MemberPath(keyPath, "time");
                const Container::String ratePath = MemberPath(keyPath, "rate");
                double time = 0.0;
                double rate = 0.0;
                if (!ReadTime(timeJson, source, timePath, duration, time, error))
                {
                    return false;
                }
                if (index != 0u && time < previousTime)
                {
                    return Fail(source, timePath, u8"時刻は昇順で指定してください", error);
                }
                if (!enabled.IsBoolean())
                {
                    return Fail(source, MemberPath(keyPath, "enabled"), u8"真偽値が必要です", error);
                }
                if (!ReadNumber(rateJson, source, ratePath, rate, error))
                {
                    return false;
                }
                if (rate < 0.0)
                {
                    return Fail(source, ratePath, u8"0 以上の値が必要です", error);
                }
                track.Keys.push_back({static_cast<float>(time), enabled.AsBool(), static_cast<float>(rate)});
                previousTime = time;
            }
            return true;
        }

        struct ParsedBurst
        {
            VanishBurst Burst;
            double Time = 0.0;
        };

        bool ReadBurst(const JsonValue& json, const Container::String& source, const Container::String& path,
            double duration, ParsedBurst& parsed, Container::String& error)
        {
            constexpr const char* names[] = {"time", "emitter", "count", "velocity"};
            if (!CheckKeys(json, source, path, names, 4u, error))
            {
                return false;
            }
            JsonValue timeJson;
            JsonValue emitter;
            JsonValue count;
            if (!RequireMember(json, source, path, "time", timeJson, error) ||
                !RequireMember(json, source, path, "emitter", emitter, error) ||
                !RequireMember(json, source, path, "count", count, error) ||
                !ReadTime(timeJson, source, MemberPath(path, "time"), duration, parsed.Time, error) ||
                !ReadString(emitter, source, MemberPath(path, "emitter"), parsed.Burst.EmitterName, error))
            {
                return false;
            }
            const Container::String countPath = MemberPath(path, "count");
            if (!count.IsIntegerLiteral())
            {
                return Fail(source, countPath, u8"整数が必要です", error);
            }
            if (count.AsNumber() < 1.0 || count.AsNumber() > 65536.0)
            {
                return Fail(source, countPath, u8"1 以上 65536 以下の値が必要です", error);
            }
            parsed.Burst.TimeSeconds = static_cast<float>(parsed.Time);
            parsed.Burst.Count = static_cast<uint32_t>(count.AsNumber());
            const JsonValue velocity = json.FindMember("velocity");
            if (velocity.IsValid())
            {
                const Container::String velocityPath = MemberPath(path, "velocity");
                if (!velocity.IsArray() || velocity.GetArraySize() != 3u)
                {
                    return Fail(source, velocityPath, u8"3 個の数値からなる配列が必要です", error);
                }
                float components[3] = {};
                for (size_t index = 0u; index < 3u; ++index)
                {
                    double value = 0.0;
                    if (!ReadNumber(velocity.GetArrayElement(index), source, IndexPath(velocityPath, index), value, error))
                    {
                        return false;
                    }
                    components[index] = static_cast<float>(value);
                }
                parsed.Burst.Velocity = Math::Vector3(components[0], components[1], components[2]);
            }
            return true;
        }

        bool FindEmitter(const VfxEffectAsset& asset, const Container::String& name, uint32_t& result)
        {
            for (size_t index = 0u; index < asset.Emitters.size() && index < UINT32_MAX; ++index)
            {
                if (SameString(asset.Emitters[index].Name, name))
                {
                    result = static_cast<uint32_t>(index);
                    return true;
                }
            }
            return false;
        }
    } // namespace

    float EvaluateVanishThreshold(const VanishEffectDefinition& definition, double timeSeconds)
    {
        if (definition.Threshold.empty())
        {
            return 0.0f;
        }
        const auto& keys = definition.Threshold;
        if (!(timeSeconds >= static_cast<double>(keys.front().TimeSeconds)))
        {
            return keys.front().Value;
        }
        size_t index = 0u;
        while (index + 1u < keys.size() && timeSeconds >= static_cast<double>(keys[index + 1u].TimeSeconds))
        {
            ++index;
        }
        if (index + 1u == keys.size())
        {
            return keys[index].Value;
        }
        const double start = keys[index].TimeSeconds;
        const double end = keys[index + 1u].TimeSeconds;
        const double fraction = (timeSeconds - start) / (end - start);
        return static_cast<float>(keys[index].Value +
            (static_cast<double>(keys[index + 1u].Value) - keys[index].Value) * fraction);
    }

    void EvaluateVanishEmitter(const VanishEmitterTrack& track, double timeSeconds, bool& outEnabled, float& outSpawnRate)
    {
        outEnabled = false;
        outSpawnRate = 0.0f;
        if (track.Keys.empty() || !(timeSeconds >= static_cast<double>(track.Keys.front().TimeSeconds)))
        {
            return;
        }
        size_t index = 0u;
        while (index + 1u < track.Keys.size() && timeSeconds >= static_cast<double>(track.Keys[index + 1u].TimeSeconds))
        {
            ++index;
        }
        const VanishEmitterKey& previous = track.Keys[index];
        outEnabled = previous.bEnabled;
        outSpawnRate = previous.SpawnRate;
        if (index + 1u < track.Keys.size() && previous.bEnabled && track.Keys[index + 1u].bEnabled)
        {
            const VanishEmitterKey& next = track.Keys[index + 1u];
            const double fraction = (timeSeconds - previous.TimeSeconds) /
                (static_cast<double>(next.TimeSeconds) - previous.TimeSeconds);
            outSpawnRate = static_cast<float>(previous.SpawnRate +
                (static_cast<double>(next.SpawnRate) - previous.SpawnRate) * fraction);
        }
    }

    bool LoadVanishEffectFromJson(const Container::String& text, const Container::String& sourceName,
        VanishEffectDefinition& outDefinition, Container::String& outError)
    {
        JsonDocument document;
        Container::String parseError;
        if (!JsonDocument::TryParse(text, document, &parseError))
        {
            return Fail(sourceName, FindParseErrorPath(text, parseError), u8"JSON を解析できません", outError);
        }
        const JsonValue root = document.GetRoot();
        constexpr const char* names[] = {"format", "version", "name", "effect", "duration", "dissolve", "emitters", "bursts"};
        if (!CheckKeys(root, sourceName, "", names, 8u, outError))
        {
            return false;
        }
        JsonValue format;
        JsonValue version;
        JsonValue name;
        JsonValue effect;
        JsonValue durationJson;
        JsonValue dissolve;
        if (!RequireMember(root, sourceName, "", "format", format, outError) ||
            !RequireMember(root, sourceName, "", "version", version, outError) ||
            !RequireMember(root, sourceName, "", "name", name, outError) ||
            !RequireMember(root, sourceName, "", "effect", effect, outError) ||
            !RequireMember(root, sourceName, "", "duration", durationJson, outError) ||
            !RequireMember(root, sourceName, "", "dissolve", dissolve, outError))
        {
            return false;
        }
        if (!format.IsString() || !SameLiteral(format.AsString(), "norves.vanish"))
        {
            return Fail(sourceName, "format", u8"norves.vanish を指定してください", outError);
        }
        if (!version.IsIntegerLiteral() || version.AsNumber() != 1.0)
        {
            return Fail(sourceName, "version", u8"整数の 1 を指定してください", outError);
        }
        VanishEffectDefinition definition;
        double duration = 0.0;
        if (!ReadString(name, sourceName, "name", definition.Name, outError) ||
            !ReadString(effect, sourceName, "effect", definition.EffectPath, outError) ||
            !ReadNumber(durationJson, sourceName, "duration", duration, outError))
        {
            return false;
        }
        definition.DurationSeconds = static_cast<float>(duration);
        if (duration <= 0.0 || definition.DurationSeconds <= 0.0f)
        {
            return Fail(sourceName, "duration", u8"正の浮動小数点数で表せる値が必要です", outError);
        }
        if (!ReadThreshold(dissolve, sourceName, duration, definition, outError))
        {
            return false;
        }
        const JsonValue emitters = root.FindMember("emitters");
        if (emitters.IsValid())
        {
            const Container::String emittersPath = "emitters";
            if (!emitters.IsArray())
            {
                return Fail(sourceName, emittersPath, u8"配列が必要です", outError);
            }
            for (size_t index = 0u; index < emitters.GetArraySize(); ++index)
            {
                const Container::String trackPath = IndexPath(emittersPath, index);
                VanishEmitterTrack track;
                if (!ReadEmitterTrack(emitters.GetArrayElement(index), sourceName, trackPath, duration, track, outError))
                {
                    return false;
                }
                for (const VanishEmitterTrack& previous : definition.Emitters)
                {
                    if (SameString(previous.EmitterName, track.EmitterName))
                    {
                        return Fail(sourceName, MemberPath(trackPath, "emitter"),
                            u8"同じエミッタの割り付けを重複して指定できません", outError);
                    }
                }
                definition.Emitters.push_back(std::move(track));
            }
        }
        const JsonValue bursts = root.FindMember("bursts");
        if (bursts.IsValid())
        {
            const Container::String burstsPath = "bursts";
            if (!bursts.IsArray())
            {
                return Fail(sourceName, burstsPath, u8"配列が必要です", outError);
            }
            VariableArray<ParsedBurst> parsedBursts;
            for (size_t index = 0u; index < bursts.GetArraySize(); ++index)
            {
                ParsedBurst parsed;
                parsed.Burst.SourceIndex = index;
                if (!ReadBurst(bursts.GetArrayElement(index), sourceName, IndexPath(burstsPath, index),
                    duration, parsed, outError))
                {
                    return false;
                }
                parsedBursts.push_back(std::move(parsed));
            }
            std::sort(parsedBursts.begin(), parsedBursts.end(), [](const ParsedBurst& first, const ParsedBurst& second)
                {
                    return first.Time < second.Time ||
                        (first.Time == second.Time && first.Burst.SourceIndex < second.Burst.SourceIndex);
                });
            for (ParsedBurst& parsed : parsedBursts)
            {
                definition.Bursts.push_back(std::move(parsed.Burst));
            }
        }
        outDefinition = std::move(definition);
        outError.clear();
        return true;
    }

    bool ResolveVanishEffectEmitters(VanishEffectDefinition& definition, const VfxEffectAsset& asset,
        const Container::String& sourceName, Container::String& outError)
    {
        VariableArray<uint32_t> emitterIndices;
        VariableArray<uint32_t> burstIndices;
        for (size_t index = 0u; index < definition.Emitters.size(); ++index)
        {
            const VanishEmitterTrack& track = definition.Emitters[index];
            const Container::String path = MemberPath(IndexPath("emitters", index), "emitter");
            for (size_t previous = 0u; previous < index; ++previous)
            {
                if (SameString(track.EmitterName, definition.Emitters[previous].EmitterName))
                {
                    return Fail(sourceName, path, u8"同じエミッタの割り付けを重複して指定できません", outError);
                }
            }
            uint32_t resolved = UINT32_MAX;
            if (!FindEmitter(asset, track.EmitterName, resolved))
            {
                return Fail(sourceName, path, u8"参照先 VFX にエミッタが存在しません", outError);
            }
            emitterIndices.push_back(resolved);
        }
        for (size_t index = 0u; index < definition.Bursts.size(); ++index)
        {
            const VanishBurst& burst = definition.Bursts[index];
            const size_t sourceIndex = burst.SourceIndex == static_cast<size_t>(-1) ? index : burst.SourceIndex;
            uint32_t resolved = UINT32_MAX;
            if (!FindEmitter(asset, burst.EmitterName, resolved))
            {
                return Fail(sourceName, MemberPath(IndexPath("bursts", sourceIndex), "emitter"),
                    u8"参照先 VFX にエミッタが存在しません", outError);
            }
            burstIndices.push_back(resolved);
        }
        for (size_t index = 0u; index < emitterIndices.size(); ++index)
        {
            definition.Emitters[index].EmitterIndex = emitterIndices[index];
        }
        for (size_t index = 0u; index < burstIndices.size(); ++index)
        {
            definition.Bursts[index].EmitterIndex = burstIndices[index];
        }
        outError.clear();
        return true;
    }

    bool LoadVanishEffectFile(const Container::String& path, VanishEffectDefinition& outDefinition,
        VfxEffectAsset& outEffect, Container::String& outError)
    {
        VariableArray<uint8_t> bytes;
        const auto read = Platform::ReadFileBytes(path, bytes);
        if (!read.bSuccess)
        {
            Fail(path, "", u8"ファイルを読み込めません", outError);
            if (!read.Message.empty() && IsValidText(read.Message))
            {
                outError += " (";
                AppendEscaped(outError, read.Message);
                outError += ")";
            }
            return false;
        }
        const size_t skip = bytes.size() >= 3u && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf ? 3u : 0u;
        Container::String text;
        text.reserve(bytes.size() - skip);
        const uint8_t* data = bytes.empty() ? nullptr : bytes.data() + skip;
        if (!TextDetail::ForEachUnicodeScalar<uint8_t>({data, bytes.size() - skip}, [&](uint32_t scalar)
            {
                const auto encoded = TextDetail::EncodeJsonUnicodeScalar<Container::String::value_type>(scalar);
                text.append(encoded.Units, encoded.Count);
            }))
        {
            return Fail(path, "", u8"JSON が有効な UTF-8 ではありません", outError);
        }
        VanishEffectDefinition definition;
        if (!LoadVanishEffectFromJson(text, path, definition, outError))
        {
            return false;
        }
        VfxEffectAsset effect;
        Container::String effectError;
        if (!LoadVfxEffectFile(definition.EffectPath, effect, effectError))
        {
            Fail(path, "effect", u8"参照先 VFX を読み込めません", outError);
            outError += " (";
            AppendEscaped(outError, definition.EffectPath);
            if (!effectError.empty() && IsValidText(effectError))
            {
                outError += ": ";
                AppendEscaped(outError, effectError);
            }
            outError += ")";
            return false;
        }
        if (!ResolveVanishEffectEmitters(definition, effect, path, outError))
        {
            return false;
        }
        outDefinition = std::move(definition);
        outEffect = std::move(effect);
        outError.clear();
        return true;
    }
} // namespace NorvesLib::Core::Particle

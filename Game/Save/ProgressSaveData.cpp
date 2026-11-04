#include "ProgressSaveData.h"

#include "Core/Public/Save/GameStateStore.h"
#include "Core/Public/Text/JsonDocument.h"
#include "Core/Public/Text/JsonWriter.h"
#include <charconv>
#include <cmath>
#include <utility>

namespace Game::Save
{
    namespace CoreSave = NorvesLib::Core::Save;
    using NorvesLib::Core::JsonDocument;
    using NorvesLib::Core::JsonType;
    using NorvesLib::Core::JsonValue;
    using NorvesLib::Core::JsonWriter;

    namespace
    {
        constexpr int64_t ExactIntegerLimit = 9007199254740992LL;

        bool Fail(const Container::String& field, Container::String* pOutError)
        {
            if (pOutError)
            {
                *pOutError = field;
                pOutError->append(": 保存データの型または値が不正です");
            }
            return false;
        }

        bool CheckType(JsonValue value, JsonType type, const Container::String& field,
                       Container::String* pOutError)
        {
            return !value.IsValid() || value.GetType() == type || Fail(field, pOutError);
        }

        bool ReadString(JsonValue value, Container::String& out, const Container::String& field,
                        Container::String* pOutError)
        {
            if (!CheckType(value, JsonType::String, field, pOutError))
            {
                return false;
            }
            if (value.IsValid())
            {
                out = value.AsString();
            }
            return true;
        }

        bool ReadReal(JsonValue value, double& out, const Container::String& field,
                      Container::String* pOutError)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (!value.IsNumber() || !std::isfinite(value.AsNumber()))
            {
                return Fail(field, pOutError);
            }
            out = value.AsNumber();
            return true;
        }

        bool ReadInt(JsonValue value, int64_t& out, const Container::String& field,
                     Container::String* pOutError)
        {
            if (!value.IsValid())
            {
                return true;
            }
            if (value.IsString())
            {
                const auto& text = value.AsString();
                int64_t candidate = 0;
                const auto result = std::from_chars(text.data(), text.data() + text.size(), candidate);
                if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
                {
                    return Fail(field, pOutError);
                }
                out = candidate;
                return true;
            }
            // double に丸められる整数は文字列でのみ受け取る。
            if (!value.IsNumber())
            {
                return Fail(field, pOutError);
            }
            const double number = value.AsNumber();
            if (!std::isfinite(number) || std::trunc(number) != number ||
                number <= -static_cast<double>(ExactIntegerLimit) ||
                number >= static_cast<double>(ExactIntegerLimit))
            {
                return Fail(field, pOutError);
            }
            out = static_cast<int64_t>(number);
            return true;
        }

        void WriteInt(JsonWriter& writer, const char* key, int64_t value)
        {
            if (value > -ExactIntegerLimit && value < ExactIntegerLimit)
            {
                writer.WriteInt64(key, value);
            }
            else
            {
                char buffer[32] = {};
                const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
                Container::String text;
                text.append(buffer, static_cast<size_t>(result.ptr - buffer));
                writer.WriteString(key, text);
            }
        }

        const char* KindName(WaypointKind kind)
        {
            switch (kind)
            {
            case WaypointKind::Bed: return "bed";
            case WaypointKind::Checkpoint: return "checkpoint";
            case WaypointKind::Poi: return "poi";
            }
            return nullptr;
        }

        bool WriteWaypoint(JsonWriter& writer, const WaypointRef& waypoint)
        {
            const char* kind = KindName(waypoint.Kind);
            if (!kind)
            {
                return false;
            }
            writer.WriteString("id", waypoint.Id);
            writer.WriteString("kind", kind);
            return true;
        }

        bool ReadWaypoint(JsonValue value, WaypointRef& out, const Container::String& field,
                          Container::String* pOutError)
        {
            if (value.IsString())
            {
                out.Id = value.AsString();
                out.Kind = WaypointKind::Checkpoint;
                return true;
            }
            if (!CheckType(value, JsonType::Object, field, pOutError))
            {
                return false;
            }
            if (!ReadString(value.FindMember("id"), out.Id, field + ".id", pOutError))
            {
                return false;
            }
            const JsonValue kind = value.FindMember("kind");
            if (!kind.IsValid())
            {
                return true;
            }
            if (kind.IsString())
            {
                if (kind.AsString() == "bed")
                {
                    out.Kind = WaypointKind::Bed;
                    return true;
                }
                if (kind.AsString() == "checkpoint")
                {
                    out.Kind = WaypointKind::Checkpoint;
                    return true;
                }
                if (kind.AsString() == "poi")
                {
                    out.Kind = WaypointKind::Poi;
                    return true;
                }
            }
            return Fail(field + ".kind", pOutError);
        }

        int HexDigit(char ch)
        {
            if (ch >= '0' && ch <= '9')
            {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f')
            {
                return ch - 'a' + 10;
            }
            return -1;
        }

        bool ReadMapMask(JsonValue value, Container::VariableArray<uint8_t>& out, Container::String* pOutError)
        {
            if (!CheckType(value, JsonType::String, "progress.mapMask", pOutError))
            {
                return false;
            }
            if (!value.IsValid())
            {
                return true;
            }
            const auto& text = value.AsString();
            if (text.size() % 2 != 0)
            {
                return Fail("progress.mapMask", pOutError);
            }
            out.reserve(text.size() / 2);
            for (size_t index = 0; index < text.size(); index += 2)
            {
                const int high = HexDigit(text[index]);
                const int low = HexDigit(text[index + 1]);
                if (high < 0 || low < 0)
                {
                    return Fail("progress.mapMask", pOutError);
                }
                out.push_back(static_cast<uint8_t>((high << 4) | low));
            }
            return true;
        }

        template <typename T, typename Reader>
        bool ReadMap(JsonValue value, Container::Map<Container::String, T>& out,
                     const Container::String& field, Container::String* pOutError, Reader read)
        {
            if (!CheckType(value, JsonType::Object, field, pOutError))
            {
                return false;
            }
            for (size_t index = 0; index < value.GetObjectSize(); ++index)
            {
                const auto& name = value.GetMemberName(index);
                const auto memberField = field + "." + name;
                T entry{};
                if (!read(value.GetMemberValue(index), entry, memberField, pOutError))
                {
                    return false;
                }
                if (!out.emplace(name, entry).second)
                {
                    return Fail(memberField, pOutError);
                }
            }
            return true;
        }

        bool ReadBool(JsonValue value, bool& out, const Container::String& field,
                      Container::String* pOutError)
        {
            if (!value.IsBoolean())
            {
                return Fail(field, pOutError);
            }
            out = value.AsBool();
            return true;
        }

        bool ReadPlayer(JsonValue player, ProgressSaveData::PlayerData& out, Container::String* pOutError)
        {
            if (!CheckType(player, JsonType::Object, "player", pOutError))
            {
                return false;
            }
            const JsonValue pos = player.FindMember("pos");
            if (pos.IsValid())
            {
                if (!pos.IsArray() || pos.GetArraySize() != 3)
                {
                    return Fail("player.pos", pOutError);
                }
                for (size_t index = 0; index < 3; ++index)
                {
                    if (!ReadReal(pos.GetArrayElement(index), out.Pos[index], "player.pos[]", pOutError))
                    {
                        return false;
                    }
                }
            }
            return ReadReal(player.FindMember("yaw"), out.Yaw, "player.yaw", pOutError) &&
                   ReadWaypoint(player.FindMember("waypoint"), out.Waypoint, "player.waypoint", pOutError);
        }

        bool ReadProgress(JsonValue progress, ProgressSaveData::ProgressData& out, Container::String* pOutError)
        {
            if (!CheckType(progress, JsonType::Object, "progress", pOutError))
            {
                return false;
            }
            const JsonValue defeated = progress.FindMember("defeated");
            const JsonValue waypoints = progress.FindMember("unlockedWaypoints");
            if (!CheckType(defeated, JsonType::Array, "progress.defeated", pOutError) ||
                !CheckType(waypoints, JsonType::Array, "progress.unlockedWaypoints", pOutError))
            {
                return false;
            }
            for (size_t index = 0; index < defeated.GetArraySize(); ++index)
            {
                Container::String id;
                if (!ReadString(defeated.GetArrayElement(index), id, "progress.defeated[]", pOutError))
                {
                    return false;
                }
                out.Defeated.push_back(std::move(id));
            }
            for (size_t index = 0; index < waypoints.GetArraySize(); ++index)
            {
                WaypointRef waypoint;
                if (!ReadWaypoint(waypoints.GetArrayElement(index), waypoint, "progress.unlockedWaypoints[]", pOutError))
                {
                    return false;
                }
                out.UnlockedWaypoints.push_back(std::move(waypoint));
            }
            return ReadMapMask(progress.FindMember("mapMask"), out.MapMask, pOutError) &&
                   ReadMap(progress.FindMember("flags"), out.Flags, "progress.flags", pOutError, ReadBool) &&
                   ReadMap(progress.FindMember("ints"), out.Ints, "progress.ints", pOutError, ReadInt) &&
                   ReadMap(progress.FindMember("reals"), out.Reals, "progress.reals", pOutError, ReadReal);
        }
    } // namespace

    Container::String ToJson(const ProgressSaveData& data)
    {
        JsonWriter writer;
        writer.BeginObject();
        writer.WriteUInt64("formatVersion", CoreSave::SavePayloadFormatVersion);
        writer.WriteString("appVersion", data.AppVersion);
        WriteInt(writer, "savedAtUnix", data.SavedAtUnix);
        writer.WriteNumber("playSeconds", data.PlaySeconds);
        writer.BeginObject("player");
        writer.BeginArray("pos");
        for (double coordinate : data.Player.Pos)
        {
            writer.WriteNumberElement(coordinate);
        }
        writer.EndArray();
        writer.WriteNumber("yaw", data.Player.Yaw);
        writer.BeginObject("waypoint");
        if (!WriteWaypoint(writer, data.Player.Waypoint))
        {
            return {};
        }
        writer.EndObject();
        writer.EndObject();
        writer.BeginObject("world");
        writer.WriteNumber("timeOfDay", data.World.TimeOfDay);
        WriteInt(writer, "day", data.World.Day);
        writer.WriteString("weather", data.World.Weather);
        writer.EndObject();
        writer.BeginObject("progress");
        writer.BeginArray("defeated");
        for (const auto& id : data.Progress.Defeated)
        {
            writer.WriteStringElement(id);
        }
        writer.EndArray();
        writer.BeginArray("unlockedWaypoints");
        for (const auto& waypoint : data.Progress.UnlockedWaypoints)
        {
            writer.BeginObject();
            if (!WriteWaypoint(writer, waypoint))
            {
                return {};
            }
            writer.EndObject();
        }
        writer.EndArray();
        constexpr char HexDigits[] = "0123456789abcdef";
        Container::String mask;
        for (uint8_t byte : data.Progress.MapMask)
        {
            mask.push_back(HexDigits[byte >> 4]);
            mask.push_back(HexDigits[byte & 0x0f]);
        }
        writer.WriteString("mapMask", mask);
        writer.BeginObject("flags");
        for (const auto& [name, value] : data.Progress.Flags)
        {
            if (name.find('\0') != Container::String::npos)
            {
                return {};
            }
            writer.WriteBool(name.c_str(), value);
        }
        writer.EndObject();
        writer.BeginObject("ints");
        for (const auto& [name, value] : data.Progress.Ints)
        {
            if (name.find('\0') != Container::String::npos)
            {
                return {};
            }
            WriteInt(writer, name.c_str(), value);
        }
        writer.EndObject();
        writer.BeginObject("reals");
        for (const auto& [name, value] : data.Progress.Reals)
        {
            if (name.find('\0') != Container::String::npos)
            {
                return {};
            }
            writer.WriteNumber(name.c_str(), value);
        }
        writer.EndObject();
        writer.EndObject();
        writer.EndObject();
        return writer.IsComplete() ? writer.ToString() : Container::String{};
    }

    bool FromJson(Container::Span<const uint8_t> payloadUtf8, ProgressSaveData& out, Container::String* pOutError)
    {
        Container::VariableArray<uint8_t> migrated;
        const auto result = CoreSave::MigrateSavePayload(payloadUtf8, migrated, pOutError);
        if (result == CoreSave::SaveMigrationResult::RejectedNewerVersion || result == CoreSave::SaveMigrationResult::Invalid)
        {
            // 版の型が違う場合も、他の欄と同様に対象の名前を返す。
            if (result == CoreSave::SaveMigrationResult::Invalid && pOutError)
            {
                JsonDocument original;
                if (JsonDocument::TryParseUtf8(payloadUtf8, original) && original.GetRoot().IsObject())
                {
                    const JsonValue version = original.GetRoot().FindMember("formatVersion");
                    if (!version.IsIntegerLiteral() || version.AsNumber() < 1.0)
                    {
                        return Fail("formatVersion", pOutError);
                    }
                }
            }
            return false;
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(migrated, document, pOutError))
        {
            return false;
        }
        const JsonValue root = document.GetRoot();
        const JsonValue world = root.FindMember("world");
        ProgressSaveData candidate;
        if (!ReadString(root.FindMember("appVersion"), candidate.AppVersion, "appVersion", pOutError) ||
            !ReadInt(root.FindMember("savedAtUnix"), candidate.SavedAtUnix, "savedAtUnix", pOutError) ||
            !ReadReal(root.FindMember("playSeconds"), candidate.PlaySeconds, "playSeconds", pOutError) ||
            !ReadPlayer(root.FindMember("player"), candidate.Player, pOutError) ||
            !CheckType(world, JsonType::Object, "world", pOutError) ||
            !ReadReal(world.FindMember("timeOfDay"), candidate.World.TimeOfDay, "world.timeOfDay", pOutError) ||
            !ReadInt(world.FindMember("day"), candidate.World.Day, "world.day", pOutError) ||
            !ReadString(world.FindMember("weather"), candidate.World.Weather, "world.weather", pOutError) ||
            !ReadProgress(root.FindMember("progress"), candidate.Progress, pOutError))
        {
            return false;
        }
        out = std::move(candidate);
        return true;
    }

    void CaptureStateStore(const CoreSave::GameStateStore& store, ProgressSaveData& out)
    {
        Container::VariableArray<CoreSave::GameStateEntry> entries;
        store.CollectPersistent(entries);
        out.Progress.Flags.clear();
        out.Progress.Ints.clear();
        out.Progress.Reals.clear();
        for (const auto& entry : entries)
        {
            switch (entry.Type)
            {
            case CoreSave::GameStateValueType::Bool:
                out.Progress.Flags[entry.Name] = entry.BoolValue;
                break;
            case CoreSave::GameStateValueType::Int64:
                out.Progress.Ints[entry.Name] = entry.IntValue;
                break;
            case CoreSave::GameStateValueType::Double:
                out.Progress.Reals[entry.Name] = entry.DoubleValue;
                break;
            }
        }
    }

    void ApplyToStateStore(const ProgressSaveData& data, CoreSave::GameStateStore& store)
    {
        store.ResetScope(CoreSave::GameStateScope::Persistent);
        for (const auto& [name, value] : data.Progress.Flags)
        {
            store.ApplyPersistentBool(name, value);
        }
        for (const auto& [name, value] : data.Progress.Ints)
        {
            store.ApplyPersistentInt64(name, value);
        }
        for (const auto& [name, value] : data.Progress.Reals)
        {
            store.ApplyPersistentDouble(name, value);
        }
    }
} // namespace Game::Save

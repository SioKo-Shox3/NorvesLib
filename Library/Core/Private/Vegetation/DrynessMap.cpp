#include "Vegetation/DrynessMap.h"

#include "Container/UnorderedSet.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include "Text/JsonWriter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NorvesLib::Core::Vegetation
{
    namespace
    {
        enum SourceField : size_t
        {
            IdField,
            ShapeField,
            CenterXField,
            CenterZField,
            RadiusAField,
            RadiusBField,
            YawField,
            FalloffField,
            TargetField,
            LevelField,
            SecondsField,
            RemainingField,
            SourceFieldCount
        };

        constexpr const char* SourceFieldNames[SourceFieldCount] =
        {
            "id", "shape", "centerX", "centerZ", "radiusA", "radiusB", "yaw",
            "falloff", "target", "level", "seconds", "remaining"
        };

        /** @brief ID を保持できる正の整数値かを返す。 */
        bool IsValidIdNumber(double value)
        {
            return std::isfinite(value) && value >= 1.0 &&
                value <= static_cast<double>(std::numeric_limits<DrynessSourceId>::max()) &&
                std::trunc(value) == value;
        }

        /** @brief 浮動小数点へ戻しても半径が正であるかを返す。 */
        bool IsPositiveRadius(double value)
        {
            return value > 0.0 &&
                (value > static_cast<double>(std::numeric_limits<float>::max()) ||
                 static_cast<float>(value) > 0.0f);
        }

        /** @brief 枯れ度が有限かつ有効な範囲に収まるかを返す。 */
        bool IsValidLevel(float level)
        {
            return std::isfinite(level) && level >= 0.0f && level <= 1.0f;
        }

        /** @brief 空でも内部に終端文字を含んでもいない鍵かを返す。 */
        bool IsValidKey(const Container::String& key)
        {
            if (key.empty())
            {
                return false;
            }
            for (size_t index = 0; index < key.size(); ++index)
            {
                if (key[index] == '\0')
                {
                    return false;
                }
            }
            return true;
        }

        /** @brief 源の値を鍵付きの保存値へ写す。 */
        DrynessKeyedState CaptureKeyedState(const Container::String& key, const DrynessSource& source)
        {
            return {key, source.CurrentLevel, source.TargetLevel, source.RemainingSeconds};
        }

        /** @brief 保存値を源へ写し、残り秒数を新しい遷移秒数とする。 */
        void ApplyKeyedState(DrynessSource& source, const DrynessKeyedState& state)
        {
            source.CurrentLevel = state.CurrentLevel;
            source.TargetLevel = state.TargetLevel;
            source.RemainingSeconds = state.RemainingSeconds;
            source.TransitionSeconds = state.RemainingSeconds;
        }

        /** @brief 源の中心から焦点までの距離の二乗を求める。 */
        double DistanceSquared(const DrynessSource& source, float focusX, float focusZ)
        {
            const double dx = static_cast<double>(source.CenterX) - static_cast<double>(focusX);
            const double dz = static_cast<double>(source.CenterZ) - static_cast<double>(focusZ);
            return dx * dx + dz * dz;
        }
    } // namespace

    const char* GetDrynessRestoreErrorName(DrynessRestoreError error)
    {
        switch (error)
        {
        case DrynessRestoreError::None: return "None";
        case DrynessRestoreError::NotObject: return "NotObject";
        case DrynessRestoreError::UnsupportedVersion: return "UnsupportedVersion";
        case DrynessRestoreError::MissingField: return "MissingField";
        case DrynessRestoreError::WrongType: return "WrongType";
        case DrynessRestoreError::UnknownShape: return "UnknownShape";
        case DrynessRestoreError::NonFinite: return "NonFinite";
        case DrynessRestoreError::NonPositiveRadius: return "NonPositiveRadius";
        case DrynessRestoreError::OutOfRange: return "OutOfRange";
        case DrynessRestoreError::DuplicateId: return "DuplicateId";
        case DrynessRestoreError::InvalidId: return "InvalidId";
        }
        return "Unknown";
    }

    DrynessSourceId DrynessMap::AddCircle(float centerX, float centerZ, float radius, float falloff, float level)
    {
        const DrynessSourceId id = AddEllipse(centerX, centerZ, radius, radius, 0.0f, falloff, level);
        if (id != InvalidDrynessSourceId)
        {
            m_Sources.back().Shape = DrynessShape::Circle;
        }
        return id;
    }

    DrynessSourceId DrynessMap::AddEllipse(float centerX, float centerZ, float radiusA, float radiusB, float yaw,
                                           float falloff, float level)
    {
        if (!std::isfinite(centerX) || !std::isfinite(centerZ) ||
            !std::isfinite(radiusA) || radiusA <= 0.0f || !std::isfinite(radiusB) || radiusB <= 0.0f ||
            !std::isfinite(yaw) || !std::isfinite(falloff) || falloff < 0.0f || !IsValidLevel(level) ||
            m_NextId == std::numeric_limits<DrynessSourceId>::max())
        {
            return InvalidDrynessSourceId;
        }

        DrynessSource source;
        source.Id = m_NextId;
        source.Shape = DrynessShape::Ellipse;
        source.CenterX = centerX;
        source.CenterZ = centerZ;
        source.RadiusA = radiusA;
        source.RadiusB = radiusB;
        source.Yaw = yaw;
        source.Falloff = falloff;
        source.TargetLevel = level;
        source.CurrentLevel = level;
        m_Sources.push_back(source);
        ++m_NextId;
        return source.Id;
    }

    bool DrynessMap::SetTarget(DrynessSourceId id, float level, float seconds)
    {
        if (!IsValidLevel(level) || !std::isfinite(seconds) || seconds < 0.0f)
        {
            return false;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                source.TargetLevel = level;
                source.TransitionSeconds = seconds;
                source.RemainingSeconds = seconds;
                if (seconds == 0.0f)
                {
                    source.CurrentLevel = level;
                }
                return true;
            }
        }
        return false;
    }

    bool DrynessMap::SetSourceCenter(DrynessSourceId id, float centerX, float centerZ)
    {
        if (!std::isfinite(centerX) || !std::isfinite(centerZ))
        {
            return false;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                source.CenterX = centerX;
                source.CenterZ = centerZ;
                return true;
            }
        }
        return false;
    }

    bool DrynessMap::BindKey(DrynessSourceId id, const Container::String& key)
    {
        if (!IsValidKey(key))
        {
            LOG_WARNING("DRYNESS_KEY_REJECTED reason=InvalidKey 鍵が空か内部に終端文字を含んでいます");
            return false;
        }
        auto source = std::find_if(m_Sources.begin(), m_Sources.end(),
            [id](const DrynessSource& candidate)
            {
                return candidate.Id == id;
            });
        if (source == m_Sources.end())
        {
            return false;
        }
        for (const KeyEntry& entry : m_Keys)
        {
            if (entry.LiveId == id)
            {
                return false;
            }
        }

        const float defaultLevel = source->CurrentLevel;
        for (KeyEntry& entry : m_Keys)
        {
            if (entry.Key != key)
            {
                continue;
            }
            if (entry.LiveId != InvalidDrynessSourceId)
            {
                if (!entry.bReleased)
                {
                    LOG_WARNING("DRYNESS_KEY_REJECTED reason=DuplicateKey key=%s 同じ鍵の源が既にあります", key.c_str());
                    return false;
                }
                const DrynessSource* previous = Find(entry.LiveId);
                ApplyKeyedState(*source, CaptureKeyedState(key, *previous));
                // 除去によって源の参照は無効になり得るので、この後は鍵の表だけを更新する。
                Remove(entry.LiveId);
            }
            else if (entry.bHasHeld)
            {
                ApplyKeyedState(*source, entry.Held);
            }
            entry.LiveId = id;
            entry.DefaultLevel = defaultLevel;
            entry.Held = {};
            entry.bHasHeld = false;
            entry.bReleased = false;
            entry.bPendingRestore = false;
            return true;
        }

        KeyEntry entry;
        entry.Key = key;
        entry.LiveId = id;
        entry.DefaultLevel = defaultLevel;
        m_Keys.push_back(std::move(entry));
        return true;
    }

    bool DrynessMap::Release(DrynessSourceId id)
    {
        const DrynessSource* source = Find(id);
        if (source == nullptr)
        {
            return false;
        }
        if (source->RemainingSeconds > 0.0f)
        {
            for (KeyEntry& entry : m_Keys)
            {
                if (entry.LiveId == id)
                {
                    entry.bReleased = true;
                    return true;
                }
            }
        }
        return Remove(id);
    }

    DrynessSourceId DrynessMap::FindByKey(const Container::String& key) const
    {
        for (const KeyEntry& entry : m_Keys)
        {
            if (entry.Key == key)
            {
                return entry.LiveId;
            }
        }
        return InvalidDrynessSourceId;
    }

    bool DrynessMap::SetTargetByKey(const Container::String& key, float level, float seconds)
    {
        return SetTarget(FindByKey(key), level, seconds);
    }

    void DrynessMap::CaptureKeyedStates(Container::VariableArray<DrynessKeyedState>& out) const
    {
        out.clear();
        for (const KeyEntry& entry : m_Keys)
        {
            const DrynessSource* source = Find(entry.LiveId);
            if (source != nullptr)
            {
                out.push_back(CaptureKeyedState(entry.Key, *source));
            }
            else if (entry.bHasHeld)
            {
                out.push_back(entry.Held);
            }
        }
        std::sort(out.begin(), out.end(), [](const DrynessKeyedState& left, const DrynessKeyedState& right)
            {
                return left.Key < right.Key;
            });
    }

    void DrynessMap::RestoreKeyedStates(Container::Span<const DrynessKeyedState> states)
    {
        Container::VariableArray<DrynessKeyedState> validStates;
        validStates.reserve(states.size());
        for (size_t index = 0; index < states.size(); ++index)
        {
            const DrynessKeyedState& state = states[index];
            if (!IsValidKey(state.Key) || !IsValidLevel(state.CurrentLevel) || !IsValidLevel(state.TargetLevel) ||
                !std::isfinite(state.RemainingSeconds) || state.RemainingSeconds < 0.0f)
            {
                LOG_WARNING("DRYNESS_RESTORE_SKIPPED reason=InvalidEntry index=%zu 鍵または枯れ度の値が不正です", index);
                continue;
            }
            const auto duplicate = std::find_if(validStates.begin(), validStates.end(),
                [&state](const DrynessKeyedState& candidate)
                {
                    return candidate.Key == state.Key;
                });
            if (duplicate != validStates.end())
            {
                LOG_WARNING("DRYNESS_RESTORE_SKIPPED reason=DuplicateKey key=%s 同じ鍵の値が既にあります", state.Key.c_str());
                continue;
            }
            validStates.push_back(state);
        }

        for (KeyEntry& entry : m_Keys)
        {
            if (entry.bReleased)
            {
                Remove(entry.LiveId);
            }
        }
        for (auto entry = m_Keys.begin(); entry != m_Keys.end();)
        {
            if (entry->LiveId == InvalidDrynessSourceId)
            {
                entry = m_Keys.erase(entry);
                continue;
            }
            const auto state = std::find_if(validStates.begin(), validStates.end(),
                [&entry](const DrynessKeyedState& candidate)
                {
                    return candidate.Key == entry->Key;
                });
            for (DrynessSource& source : m_Sources)
            {
                if (source.Id == entry->LiveId)
                {
                    if (state != validStates.end())
                    {
                        ApplyKeyedState(source, *state);
                    }
                    else
                    {
                        source.CurrentLevel = entry->DefaultLevel;
                        source.TargetLevel = entry->DefaultLevel;
                        source.RemainingSeconds = 0.0f;
                        source.TransitionSeconds = 0.0f;
                    }
                    break;
                }
            }
            ++entry;
        }
        for (const DrynessKeyedState& state : validStates)
        {
            if (FindByKey(state.Key) == InvalidDrynessSourceId)
            {
                KeyEntry entry;
                entry.Key = state.Key;
                entry.Held = state;
                entry.bHasHeld = true;
                entry.bPendingRestore = true;
                m_Keys.push_back(std::move(entry));
            }
        }
    }

    bool DrynessMap::Remove(DrynessSourceId id)
    {
        for (auto it = m_Sources.begin(); it != m_Sources.end(); ++it)
        {
            if (it->Id == id)
            {
                for (KeyEntry& entry : m_Keys)
                {
                    if (entry.LiveId == id)
                    {
                        entry.Held = CaptureKeyedState(entry.Key, *it);
                        entry.bHasHeld = true;
                        entry.LiveId = InvalidDrynessSourceId;
                        entry.bReleased = false;
                        entry.bPendingRestore = false;
                        break;
                    }
                }
                m_Sources.erase(it);
                return true;
            }
        }
        return false;
    }

    void DrynessMap::Clear()
    {
        m_Sources.clear();
        m_Keys.clear();
    }

    void DrynessMap::Tick(float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f)
        {
            return;
        }

        for (DrynessSource& source : m_Sources)
        {
            if (source.RemainingSeconds > 0.0f)
            {
                if (deltaSeconds >= source.RemainingSeconds)
                {
                    source.CurrentLevel = source.TargetLevel;
                    source.RemainingSeconds = 0.0f;
                }
                else
                {
                    source.CurrentLevel += (source.TargetLevel - source.CurrentLevel) *
                        (deltaSeconds / source.RemainingSeconds);
                    source.RemainingSeconds -= deltaSeconds;
                }
            }
        }
        for (KeyEntry& entry : m_Keys)
        {
            if (entry.bReleased)
            {
                const DrynessSource* source = Find(entry.LiveId);
                if (source != nullptr && source->RemainingSeconds == 0.0f)
                {
                    Remove(entry.LiveId);
                }
            }
        }
        for (auto entry = m_Keys.begin(); entry != m_Keys.end();)
        {
            if (entry->bPendingRestore && entry->LiveId == InvalidDrynessSourceId)
            {
                LOG_WARNING("DRYNESS_RESTORE_UNKNOWN_KEY key=%s 結ばれる源が無い鍵の枯れ度を読み飛ばします", entry->Key.c_str());
                entry = m_Keys.erase(entry);
            }
            else
            {
                ++entry;
            }
        }
    }

    float DrynessMap::Sample(float x, float z) const
    {
        if (!std::isfinite(x) || !std::isfinite(z))
        {
            return 0.0f;
        }

        float result = 0.0f;
        for (const DrynessSource& source : m_Sources)
        {
            float dx = x - source.CenterX;
            float dz = z - source.CenterZ;
            const float cosine = std::cos(source.Yaw);
            const float sine = std::sin(source.Yaw);
            float lx = dx * cosine + dz * sine;
            float lz = -dx * sine + dz * cosine;
            // float の二乗によるあふれや消失を避けて長さを求める。
            float dist = std::hypot(lx, lz);
            float distanceScale = 1.0f;
            if (!std::isfinite(dist))
            {
                // 差や回転後の長さがあふれる場合は 1/4 に縮めて評価する。
                dx = x * 0.25f - source.CenterX * 0.25f;
                dz = z * 0.25f - source.CenterZ * 0.25f;
                lx = dx * cosine + dz * sine;
                lz = -dx * sine + dz * cosine;
                dist = std::hypot(lx, lz);
                distanceScale = 4.0f;
            }
            const float normalizedX = (lx / source.RadiusA) * distanceScale;
            const float normalizedZ = (lz / source.RadiusB) * distanceScale;
            const float rho = std::hypot(normalizedX, normalizedZ);

            float attenuation = 1.0f;
            if (rho > 1.0f)
            {
                if (source.Falloff == 0.0f)
                {
                    attenuation = 0.0f;
                }
                else
                {
                    const float edgeDistance = dist * (1.0f - 1.0f / rho);
                    const float t = std::clamp((edgeDistance / source.Falloff) * distanceScale, 0.0f, 1.0f);
                    attenuation = 1.0f - t * t * (3.0f - 2.0f * t);
                }
            }
            result = std::max(result, source.CurrentLevel * attenuation);
        }
        return result;
    }

    void DrynessMap::Serialize(JsonWriter& writer, const char* key) const
    {
        if (key == nullptr)
        {
            writer.BeginObject();
        }
        else
        {
            writer.BeginObject(key);
        }

        writer.WriteNumber("version", 1.0);
        writer.WriteUInt64("nextId", m_NextId);
        writer.BeginArray("sources");
        for (const DrynessSource& source : m_Sources)
        {
            writer.BeginObject();
            writer.WriteUInt64("id", source.Id);
            writer.WriteString("shape", source.Shape == DrynessShape::Circle ? "circle" : "ellipse");
            writer.WriteNumber("centerX", static_cast<double>(source.CenterX));
            writer.WriteNumber("centerZ", static_cast<double>(source.CenterZ));
            writer.WriteNumber("radiusA", static_cast<double>(source.RadiusA));
            writer.WriteNumber("radiusB", static_cast<double>(source.RadiusB));
            writer.WriteNumber("yaw", static_cast<double>(source.Yaw));
            writer.WriteNumber("falloff", static_cast<double>(source.Falloff));
            writer.WriteNumber("target", static_cast<double>(source.TargetLevel));
            writer.WriteNumber("level", static_cast<double>(source.CurrentLevel));
            writer.WriteNumber("seconds", static_cast<double>(source.TransitionSeconds));
            writer.WriteNumber("remaining", static_cast<double>(source.RemainingSeconds));
            writer.EndObject();
        }
        writer.EndArray();
        writer.EndObject();
    }

    DrynessRestoreResult DrynessMap::Restore(const JsonValue& value)
    {
        if (!value.IsObject())
        {
            return {DrynessRestoreError::NotObject};
        }

        const JsonValue version = value.FindMember("version");
        if (!version.IsValid())
        {
            return {DrynessRestoreError::MissingField};
        }
        if (!version.IsNumber())
        {
            return {DrynessRestoreError::WrongType};
        }
        if (version.AsNumber() != 1.0)
        {
            return {DrynessRestoreError::UnsupportedVersion};
        }

        const JsonValue nextIdValue = value.FindMember("nextId");
        const JsonValue sourceValues = value.FindMember("sources");
        if (!nextIdValue.IsValid() || !sourceValues.IsValid())
        {
            return {DrynessRestoreError::MissingField};
        }
        if (!nextIdValue.IsNumber() || !sourceValues.IsArray())
        {
            return {DrynessRestoreError::WrongType};
        }

        const double nextId = nextIdValue.AsNumber();
        const bool bValidNextId = IsValidIdNumber(nextId);
        Container::VariableArray<DrynessSource> sources;
        Container::UnorderedSet<DrynessSourceId> ids;
        const size_t sourceCount = sourceValues.GetArraySize();
        sources.reserve(sourceCount);
        ids.reserve(sourceCount);

        for (size_t sourceIndex = 0; sourceIndex < sourceCount; ++sourceIndex)
        {
            const JsonValue sourceValue = sourceValues.GetArrayElement(sourceIndex);
            JsonValue fields[SourceFieldCount];
            for (size_t fieldIndex = 0; fieldIndex < SourceFieldCount; ++fieldIndex)
            {
                fields[fieldIndex] = sourceValue.FindMember(SourceFieldNames[fieldIndex]);
                if (!fields[fieldIndex].IsValid())
                {
                    return {DrynessRestoreError::MissingField, sourceIndex};
                }
            }
            for (size_t fieldIndex = 0; fieldIndex < SourceFieldCount; ++fieldIndex)
            {
                if ((fieldIndex == ShapeField && !fields[fieldIndex].IsString()) ||
                    (fieldIndex != ShapeField && !fields[fieldIndex].IsNumber()))
                {
                    return {DrynessRestoreError::WrongType, sourceIndex};
                }
            }

            DrynessShape shape;
            if (fields[ShapeField].AsString() == "circle")
            {
                shape = DrynessShape::Circle;
            }
            else if (fields[ShapeField].AsString() == "ellipse")
            {
                shape = DrynessShape::Ellipse;
            }
            else
            {
                return {DrynessRestoreError::UnknownShape, sourceIndex};
            }

            double numbers[SourceFieldCount] = {};
            for (size_t fieldIndex = 0; fieldIndex < SourceFieldCount; ++fieldIndex)
            {
                if (fieldIndex != ShapeField)
                {
                    numbers[fieldIndex] = fields[fieldIndex].AsNumber();
                    if (!std::isfinite(numbers[fieldIndex]))
                    {
                        return {DrynessRestoreError::NonFinite, sourceIndex};
                    }
                }
            }
            if (!IsPositiveRadius(numbers[RadiusAField]) || !IsPositiveRadius(numbers[RadiusBField]))
            {
                return {DrynessRestoreError::NonPositiveRadius, sourceIndex};
            }
            if (numbers[FalloffField] < 0.0 ||
                numbers[LevelField] < 0.0 || numbers[LevelField] > 1.0 ||
                numbers[TargetField] < 0.0 || numbers[TargetField] > 1.0 ||
                numbers[SecondsField] < 0.0 || numbers[RemainingField] < 0.0 ||
                numbers[RemainingField] > numbers[SecondsField] ||
                (shape == DrynessShape::Circle &&
                 (numbers[RadiusBField] != numbers[RadiusAField] || numbers[YawField] != 0.0)))
            {
                return {DrynessRestoreError::OutOfRange, sourceIndex};
            }
            for (size_t fieldIndex = CenterXField; fieldIndex < SourceFieldCount; ++fieldIndex)
            {
                if (std::abs(numbers[fieldIndex]) > static_cast<double>(std::numeric_limits<float>::max()))
                {
                    return {DrynessRestoreError::OutOfRange, sourceIndex};
                }
            }

            const double idNumber = numbers[IdField];
            if (!IsValidIdNumber(idNumber) || idNumber >= nextId)
            {
                return {DrynessRestoreError::InvalidId, sourceIndex};
            }
            const DrynessSourceId id = static_cast<DrynessSourceId>(idNumber);
            if (!ids.insert(id).second)
            {
                return {DrynessRestoreError::DuplicateId, sourceIndex};
            }

            DrynessSource source;
            source.Id = id;
            source.Shape = shape;
            source.CenterX = static_cast<float>(numbers[CenterXField]);
            source.CenterZ = static_cast<float>(numbers[CenterZField]);
            source.RadiusA = static_cast<float>(numbers[RadiusAField]);
            source.RadiusB = static_cast<float>(numbers[RadiusBField]);
            source.Yaw = static_cast<float>(numbers[YawField]);
            source.Falloff = static_cast<float>(numbers[FalloffField]);
            source.TargetLevel = static_cast<float>(numbers[TargetField]);
            source.CurrentLevel = static_cast<float>(numbers[LevelField]);
            source.TransitionSeconds = static_cast<float>(numbers[SecondsField]);
            source.RemainingSeconds = static_cast<float>(numbers[RemainingField]);
            sources.push_back(source);
        }

        if (!bValidNextId)
        {
            return {DrynessRestoreError::InvalidId};
        }
        m_Sources = std::move(sources);
        m_Keys.clear();
        m_NextId = static_cast<DrynessSourceId>(nextId);
        return {};
    }

    const DrynessSource* DrynessMap::Find(DrynessSourceId id) const
    {
        for (const DrynessSource& source : m_Sources)
        {
            if (source.Id == id)
            {
                return &source;
            }
        }
        return nullptr;
    }

    Container::Span<const DrynessSource> DrynessMap::GetSources() const
    {
        return Container::Span<const DrynessSource>(m_Sources.data(), m_Sources.size());
    }

    size_t DrynessMap::GetSourceCount() const
    {
        return m_Sources.size();
    }

    void DrynessMap::SetGpuSourceLimit(uint32_t limit)
    {
        m_GpuSourceLimit = limit;
    }

    uint32_t DrynessMap::GetGpuSourceLimit() const
    {
        return m_GpuSourceLimit;
    }

    void DrynessMap::CollectGpuSources(float focusX, float focusZ, Container::VariableArray<DrynessSource>& out) const
    {
        out.clear();
        const size_t count = std::min(m_Sources.size(), static_cast<size_t>(m_GpuSourceLimit));
        if (count == 0)
        {
            return;
        }

        out.resize(count);
        std::partial_sort_copy(m_Sources.begin(), m_Sources.end(), out.begin(), out.end(),
            [focusX, focusZ](const DrynessSource& lhs, const DrynessSource& rhs)
            {
                const double leftDistance = DistanceSquared(lhs, focusX, focusZ);
                const double rightDistance = DistanceSquared(rhs, focusX, focusZ);
                if (leftDistance < rightDistance)
                {
                    return true;
                }
                if (rightDistance < leftDistance)
                {
                    return false;
                }
                return lhs.Id < rhs.Id;
            });
    }
} // namespace NorvesLib::Core::Vegetation

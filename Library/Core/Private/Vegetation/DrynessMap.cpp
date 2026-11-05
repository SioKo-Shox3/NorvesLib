#include "Vegetation/DrynessMap.h"

#include "Container/UnorderedSet.h"
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

    bool DrynessMap::Remove(DrynessSourceId id)
    {
        for (auto it = m_Sources.begin(); it != m_Sources.end(); ++it)
        {
            if (it->Id == id)
            {
                m_Sources.erase(it);
                return true;
            }
        }
        return false;
    }

    void DrynessMap::Clear()
    {
        m_Sources.clear();
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

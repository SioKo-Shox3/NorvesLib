#include "Animation/ClipMetadata.h"
#include "Logging/LogMacros.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <bit>
#include <cmath>
namespace NorvesLib::Core::Animation
{
    namespace
    {
        bool Fail(ClipMetadataReport& r, ClipMetadataError error, const TCHAR* detail)
        {
            r.Error = error;
            r.Detail = Container::String(detail);
            return false;
        }
        bool Number(JsonValue v, float& out, double minimum = -3.402823466e38, double maximum = 3.402823466e38)
        {
            const double n = v.AsNumber();
            if (!v.IsNumber() || !std::isfinite(n) || n < minimum || n > maximum)
                return false;
            out = float(n);
            return true;
        }
        bool Unique(JsonValue object)
        {
            if (!object.IsObject())
                return false;
            for (size_t i = 0; i < object.GetObjectSize(); ++i)
                for (size_t j = 0; j < i; ++j)
                    if (object.GetMemberName(i) == object.GetMemberName(j))
                        return false;
            return true;
        }
        bool ValidName(Identity name)
        {
            const auto view = name.GetView();
            return name.IsValid() && !view.empty() && view.size() <= 1024 &&
                   std::find(view.begin(), view.end(), TCHAR{}) == view.end();
        }
        bool Name(JsonValue v, Identity& out)
        {
            if (!v.IsString() || v.AsString().empty() || v.AsString().size() > 1024)
                return false;
            out = Identity(v.AsString());
            return ValidName(out);
        }
        bool AtTime(JsonValue value, float duration, float& out)
        {
            if (value.HasMember("t") && value.HasMember("phase"))
                return false;
            if (value.HasMember("t"))
                return Number(value.FindMember("t"), out, 0, duration);
            float phase = 0;
            if (!Number(value.FindMember("phase"), phase, 0, 1) || phase < 0 || phase > 1)
                return false;
            out = phase * duration;
            return true;
        }
        void Unknown(JsonValue value, Container::Span<const TCHAR* const> keys, bool sidecar,
                     ClipMetadataReport& report)
        {
            if (!sidecar)
                return;
            for (size_t i = 0; i < value.GetObjectSize(); ++i)
            {
                const auto& name = value.GetMemberName(i);
                bool known = false;
                for (const auto* key : keys)
                    if (name == Container::String(key))
                    {
                        known = true;
                        break;
                    }
                if (!known)
                    ++report.UnknownKeys;
            }
        }
    } // namespace
    bool SameClipMetadata(const ClipMetadata& a, const ClipMetadata& b) noexcept
    {
        const auto bits = [](float v) { return std::bit_cast<uint32_t>(v); };
        if (a.Events.size() != b.Events.size() || a.Markers.size() != b.Markers.size() ||
            a.Loop.bEnabled != b.Loop.bEnabled || bits(a.Loop.Start) != bits(b.Loop.Start) ||
            bits(a.Loop.End) != bits(b.Loop.End) || a.Root.Mode != b.Root.Mode || a.Root.Joint != b.Root.Joint ||
            a.Root.bX != b.Root.bX || a.Root.bZ != b.Root.bZ || a.Root.bYaw != b.Root.bYaw ||
            bits(a.Root.NominalSpeed) != bits(b.Root.NominalSpeed) || bits(a.GroundOffset) != bits(b.GroundOffset))
            return false;
        for (size_t i = 0; i < a.Events.size(); ++i)
        {
            const auto& x = a.Events[i];
            const auto& y = b.Events[i];
            if (x.Name.GetView() != y.Name.GetView() || bits(x.Time) != bits(y.Time) ||
                bits(x.EndTime) != bits(y.EndTime) || bits(x.MinWeight) != bits(y.MinWeight) ||
                bits(x.Value) != bits(y.Value) || x.IntValue != y.IntValue)
                return false;
        }
        for (size_t i = 0; i < a.Markers.size(); ++i)
            if (a.Markers[i].Name.GetView() != b.Markers[i].Name.GetView() ||
                bits(a.Markers[i].Time) != bits(b.Markers[i].Time))
                return false;
        return true;
    }
    bool ApplyClipMetadataOverride(const ClipMetadata& base, const Container::String& json, float duration,
                                   ClipMetadata& out, ClipMetadataReport& report)
    {
        if (json.size() > 1048576)
        {
            report = {};
            return Fail(report, ClipMetadataError::InvalidSchema, _T("metadata_size"));
        }
        JsonDocument doc;
        if (!JsonDocument::TryParse(json, doc))
        {
            report = {};
            return Fail(report, ClipMetadataError::InvalidJson, _T("metadata_json"));
        }
        return ApplyClipMetadataValueOverride(base, doc.GetRoot(), duration, out, report);
    }
    bool ApplyClipMetadataValueOverride(const ClipMetadata& base, const JsonValue& root, float duration,
                                        ClipMetadata& out, ClipMetadataReport& report)
    {
        ClipMetadata parsed;
        if (!ParseClipMetadataValue(root, duration, true, parsed, report))
            return false;
        auto candidate = base;
        if (root.HasMember("events"))
            candidate.Events = std::move(parsed.Events);
        if (root.HasMember("markers"))
            candidate.Markers = std::move(parsed.Markers);
        if (root.HasMember("loop"))
            candidate.Loop = parsed.Loop;
        if (root.HasMember("rootMotion"))
        {
            const auto settings = root.FindMember("rootMotion");
            if (settings.HasMember("mode"))
                candidate.Root.Mode = parsed.Root.Mode;
            if (settings.HasMember("joint"))
                candidate.Root.Joint = parsed.Root.Joint;
            if (settings.HasMember("nominalSpeed"))
                candidate.Root.NominalSpeed = parsed.Root.NominalSpeed;
            if (settings.HasMember("x"))
                candidate.Root.bX = parsed.Root.bX;
            if (settings.HasMember("z"))
                candidate.Root.bZ = parsed.Root.bZ;
            if (settings.HasMember("yaw"))
                candidate.Root.bYaw = parsed.Root.bYaw;
        }
        if (root.HasMember("groundOffset"))
            candidate.GroundOffset = parsed.GroundOffset;
        if (!ValidateClipMetadata(candidate, duration, report))
            return false;
        out = std::move(candidate);
        return true;
    }
    bool RemapClipMetadataTime(const ClipMetadata& source, float duration, double start, double end, double scale,
                               ClipMetadata& out, ClipMetadataReport& report)
    {
        if (!ValidateClipMetadata(source, duration, report))
            return false;
        if (!std::isfinite(start) || !std::isfinite(end) || !std::isfinite(scale) || start < 0 || end < start ||
            end > duration || scale <= 0 || !std::isfinite((end - start) * scale) ||
            (end - start) * scale > 3.402823466e38)
            return Fail(report, ClipMetadataError::InvalidTime, _T("metadata_time_map"));
        auto candidate = source;
        candidate.Events.clear();
        candidate.Markers.clear();
        for (auto event : source.Events)
        {
            if (event.EndTime < 0)
            {
                if (event.Time < start || event.Time > end)
                    continue;
            }
            else
            {
                if (event.EndTime <= start || event.Time >= end)
                    continue;
                event.Time = float((std::max(double(event.Time), start) - start) * scale);
                event.EndTime = float((std::min(double(event.EndTime), end) - start) * scale);
            }
            if (event.EndTime < 0)
                event.Time = float((double(event.Time) - start) * scale);
            candidate.Events.push_back(event);
        }
        for (auto marker : source.Markers)
        {
            if (marker.Time < start || marker.Time >= end)
                continue;
            marker.Time = float((double(marker.Time) - start) * scale);
            candidate.Markers.push_back(marker);
        }
        candidate.Loop = {};
        if (source.Loop.bEnabled)
        {
            const double a = std::max(start, double(source.Loop.Start)), b = std::min(end, double(source.Loop.End));
            if (b > a)
                candidate.Loop = {true, float((a - start) * scale), float((b - start) * scale)};
        }
        if (candidate.Root.NominalSpeed >= 0)
        {
            const float mapped = float(double(candidate.Root.NominalSpeed) / scale);
            if (candidate.Root.NominalSpeed > 0 && mapped == 0)
                return Fail(report, ClipMetadataError::InvalidRoot, _T("metadata_speed_underflow"));
            candidate.Root.NominalSpeed = mapped;
        }
        if (!ValidateClipMetadata(candidate, float((end - start) * scale), report))
            return false;
        out = std::move(candidate);
        return true;
    }
    bool ValidateClipMetadata(const ClipMetadata& m, float duration, ClipMetadataReport& r)
    {
        r.Error = ClipMetadataError::None;
        r.Detail.clear();
        if (!std::isfinite(duration) || duration < 0 || m.Events.size() > 4096 || m.Markers.size() > 256)
            return Fail(r, ClipMetadataError::InvalidSchema, _T("metadata_limits"));
        float previous = -1;
        for (const auto& e : m.Events)
        {
            if (!ValidName(e.Name))
                return Fail(r, ClipMetadataError::InvalidName, _T("event_name"));
            if (!std::isfinite(e.Time) || e.Time < 0 || e.Time > duration || e.Time < previous ||
                !std::isfinite(e.EndTime) || (e.EndTime >= 0 && (e.EndTime <= e.Time || e.EndTime > duration)))
                return Fail(r, ClipMetadataError::InvalidTime, _T("event_time"));
            if (!std::isfinite(e.MinWeight) || e.MinWeight < 0 || e.MinWeight > 1 || !std::isfinite(e.Value))
                return Fail(r, ClipMetadataError::InvalidWeight, _T("event_weight"));
            previous = e.Time;
        }
        previous = -1;
        for (size_t i = 0; i < m.Markers.size(); ++i)
        {
            const auto& marker = m.Markers[i];
            if (!ValidName(marker.Name))
                return Fail(r, ClipMetadataError::InvalidName, _T("marker_name"));
            if (!std::isfinite(marker.Time) || marker.Time < 0 || marker.Time >= duration || marker.Time <= previous)
                return Fail(r, ClipMetadataError::InvalidTime, _T("marker_time"));
            for (size_t j = 0; j < i; ++j)
                if (m.Markers[j].Name == marker.Name)
                    return Fail(r, ClipMetadataError::InvalidName, _T("duplicate_marker"));
            previous = marker.Time;
        }
        if (!std::isfinite(m.Loop.Start) || !std::isfinite(m.Loop.End) ||
            (m.Loop.bEnabled && (m.Loop.Start < 0 || m.Loop.End <= m.Loop.Start || m.Loop.End > duration)))
            return Fail(r, ClipMetadataError::InvalidLoop, _T("loop_range"));
        if (uint8_t(m.Root.Mode) > uint8_t(RootMotionMode::Extract) || !std::isfinite(m.Root.NominalSpeed) ||
            m.Root.NominalSpeed < -1 || !std::isfinite(m.GroundOffset))
            return Fail(r, ClipMetadataError::InvalidRoot, _T("root_settings"));
        return true;
    }
    bool ParseClipMetadataValue(const JsonValue& value, float duration, bool sidecar, ClipMetadata& out,
                                ClipMetadataReport& report)
    {
        out = {};
        report = {};
        ClipMetadata m;
        if (!Unique(value))
            return Fail(report, ClipMetadataError::InvalidSchema, _T("metadata_object"));
        if (value.HasMember("version") &&
            (!value.FindMember("version").IsIntegerLiteral() || value.FindMember("version").AsInt64() != 1))
            return Fail(report, ClipMetadataError::InvalidSchema, _T("metadata_version"));
        const TCHAR* keys[] = {_T("version"), _T("events"),     _T("markers"),
                               _T("loop"),    _T("rootMotion"), _T("groundOffset")};
        Unknown(value, keys, sidecar, report);
        auto events = value.FindMember("events");
        if (events.IsValid())
        {
            if (!events.IsArray() || events.GetArraySize() > 4096)
                return Fail(report, ClipMetadataError::InvalidSchema, _T("events"));
            for (size_t i = 0; i < events.GetArraySize(); ++i)
            {
                auto v = events.GetArrayElement(i);
                AnimEvent e;
                if (!Unique(v) || !Name(v.FindMember("name"), e.Name))
                    return Fail(report, ClipMetadataError::InvalidName, _T("event_name"));
                if (!AtTime(v, duration, e.Time))
                    return Fail(report, ClipMetadataError::InvalidTime, _T("event_time"));
                if (v.HasMember("end") && !Number(v.FindMember("end"), e.EndTime, -3.402823466e38, duration))
                    return Fail(report, ClipMetadataError::InvalidTime, _T("event_end"));
                if (v.HasMember("minWeight") && !Number(v.FindMember("minWeight"), e.MinWeight, 0, 1))
                    return Fail(report, ClipMetadataError::InvalidWeight, _T("event_weight"));
                if (v.HasMember("value") && !Number(v.FindMember("value"), e.Value))
                    return Fail(report, ClipMetadataError::InvalidSchema, _T("event_value"));
                if (v.HasMember("intValue"))
                {
                    auto n = v.FindMember("intValue");
                    if (!n.IsIntegerLiteral() || n.AsNumber() < INT32_MIN || n.AsNumber() > INT32_MAX)
                        return Fail(report, ClipMetadataError::InvalidSchema, _T("event_int"));
                    e.IntValue = int32_t(n.AsInt64());
                }
                const TCHAR* eventKeys[] = {_T("name"),      _T("t"),     _T("phase"),   _T("end"),
                                            _T("minWeight"), _T("value"), _T("intValue")};
                Unknown(v, eventKeys, sidecar, report);
                m.Events.push_back(e);
            }
        }
        auto markers = value.FindMember("markers");
        if (markers.IsValid())
        {
            if (!markers.IsArray() || markers.GetArraySize() > 256)
                return Fail(report, ClipMetadataError::InvalidSchema, _T("markers"));
            for (size_t i = 0; i < markers.GetArraySize(); ++i)
            {
                auto v = markers.GetArrayElement(i);
                PhaseMarker marker;
                if (!Unique(v) || !Name(v.FindMember("name"), marker.Name))
                    return Fail(report, ClipMetadataError::InvalidName, _T("marker_name"));
                if (!AtTime(v, duration, marker.Time))
                    return Fail(report, ClipMetadataError::InvalidTime, _T("marker_time"));
                const TCHAR* markerKeys[] = {_T("name"), _T("t"), _T("phase")};
                Unknown(v, markerKeys, sidecar, report);
                m.Markers.push_back(marker);
            }
        }
        auto loop = value.FindMember("loop");
        if (loop.IsValid() && !loop.IsNull())
        {
            if (!Unique(loop) || !Number(loop.FindMember("start"), m.Loop.Start, 0, duration) ||
                !Number(loop.FindMember("end"), m.Loop.End, 0, duration))
                return Fail(report, ClipMetadataError::InvalidLoop, _T("loop_range"));
            m.Loop.bEnabled = true;
            const TCHAR* loopKeys[] = {_T("start"), _T("end")};
            Unknown(loop, loopKeys, sidecar, report);
        }
        auto root = value.FindMember("rootMotion");
        if (root.IsValid())
        {
            if (!Unique(root))
                return Fail(report, ClipMetadataError::InvalidRoot, _T("root_object"));
            auto mode = root.FindMember("mode");
            if (mode.IsValid() && !mode.IsString())
                return Fail(report, ClipMetadataError::InvalidRoot, _T("root_mode"));
            if (!mode.IsValid())
            {
            }
            else if (mode.AsString() == Container::String(_T("extract")))
                m.Root.Mode = RootMotionMode::Extract;
            else if (mode.AsString() == Container::String(_T("inPlace")))
                m.Root.Mode = RootMotionMode::InPlace;
            else if (mode.AsString() != Container::String(_T("none")))
                return Fail(report, ClipMetadataError::InvalidRoot, _T("root_mode"));
            if (root.HasMember("joint"))
            {
                auto joint = root.FindMember("joint");
                if (!joint.IsIntegerLiteral() || joint.AsNumber() < 0 || joint.AsNumber() >= UINT32_MAX)
                    return Fail(report, ClipMetadataError::InvalidRoot, _T("root_joint"));
                m.Root.Joint = joint.AsUInt32();
            }
            if (root.HasMember("nominalSpeed") && !Number(root.FindMember("nominalSpeed"), m.Root.NominalSpeed, -1))
                return Fail(report, ClipMetadataError::InvalidRoot, _T("nominal_speed"));
            const TCHAR* rootKeys[] = {_T("mode"), _T("joint"), _T("nominalSpeed"), _T("x"), _T("z"), _T("yaw")};
            Unknown(root, rootKeys, sidecar, report);
            const char* names[] = {"x", "z", "yaw"};
            bool* flags[] = {&m.Root.bX, &m.Root.bZ, &m.Root.bYaw};
            for (size_t i = 0; i < 3; ++i)
                if (root.HasMember(names[i]))
                {
                    auto flag = root.FindMember(names[i]);
                    if (!flag.IsBoolean())
                        return Fail(report, ClipMetadataError::InvalidRoot, _T("root_axis"));
                    *flags[i] = flag.AsBool();
                }
        }
        if (value.HasMember("groundOffset") && !Number(value.FindMember("groundOffset"), m.GroundOffset))
            return Fail(report, ClipMetadataError::InvalidSchema, _T("ground_offset"));
        if (!ValidateClipMetadata(m, duration, report))
            return false;
        if (sidecar && report.UnknownKeys)
            NORVES_LOG_WARNING("Animation", "animmetaの未知キーを無視しました: count=%u", report.UnknownKeys);
        out = std::move(m);
        return true;
    }
    bool ParseClipMetadata(const Container::String& json, float duration, bool sidecar, ClipMetadata& out,
                           ClipMetadataReport& report)
    {
        if (json.size() > 1048576)
        {
            out = {};
            report = {};
            return Fail(report, ClipMetadataError::InvalidSchema, _T("metadata_size"));
        }
        JsonDocument doc;
        if (!JsonDocument::TryParse(json, doc))
        {
            out = {};
            report = {};
            return Fail(report, ClipMetadataError::InvalidJson, _T("metadata_json"));
        }
        return ParseClipMetadataValue(doc.GetRoot(), duration, sidecar, out, report);
    }
} // namespace NorvesLib::Core::Animation

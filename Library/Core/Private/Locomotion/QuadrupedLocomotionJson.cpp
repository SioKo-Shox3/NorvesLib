#include "Locomotion/QuadrupedLocomotionJson.h"
#include "Locomotion/QuadrupedLocomotionModel.h"
#include "Text/JsonDocument.h"
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
namespace NorvesLib::Core::Locomotion
{
    namespace
    {
        bool AsciiName(const Container::String& value, const char* name)
        {
            const size_t length=std::strlen(name);
            if(value.size()!=length) return false;
            for(size_t i=0;i<length;++i)
                if(value[i]!=static_cast<Container::String::value_type>(name[i])) return false;
            return true;
        }
        bool Fields(JsonValue object, std::initializer_list<const char*> names)
        {
            if (!object.IsObject() || object.GetObjectSize() != names.size())
                return false;
            for (const char* name : names)
            {
                unsigned count = 0;
                for (size_t i = 0; i < object.GetObjectSize(); ++i)
                    if (AsciiName(object.GetMemberName(i), name))
                        ++count;
                if (count != 1)
                    return false;
            }
            return true;
        }
        struct Field
        {
            const char* Name;
            float* Value;
        };
        bool Numbers(JsonValue object, std::initializer_list<Field> fields)
        {
            if (!object.IsObject() || object.GetObjectSize() != fields.size())
                return false;
            for (auto field : fields)
            {
                unsigned count = 0;
                for (size_t i = 0; i < object.GetObjectSize(); ++i)
                    if (AsciiName(object.GetMemberName(i), field.Name))
                        ++count;
                if (count != 1)
                    return false;
                const auto value = object.FindMember(field.Name);
                const double number = value.AsNumber();
                if (!value.IsNumber() || !std::isfinite(number) || number < 0 ||
                    number > std::numeric_limits<float>::max())
                    return false;
                // floatへ丸める前に区間を判定し、微小な範囲外を境界値へ吸収しない。
                if ((std::strcmp(field.Name, "airControl") == 0 || std::strcmp(field.Name, "landingSpeedLoss") == 0 ||
                     std::strcmp(field.Name, "minScale") == 0) &&
                    number > 1)
                    return false;
                *field.Value = static_cast<float>(number);
            }
            return true;
        }
    } // namespace
    bool ParseQuadrupedLocomotionJson(Container::Span<const uint8_t> bytes, QuadrupedLocomotionParams& out,
                                      Container::String& error)
    {
        error.clear();
        if (!bytes.data() || bytes.empty() || bytes.size() > 64 * 1024)
        {
            error = TEXT("移動設定が空か64KiB上限を超えています");
            return false;
        }
        // JSON解析器へ渡す前に深さを制限する。括弧・文字列の正当性は解析器が検証する。
        unsigned depth = 0;
        bool quoted = false, escaped = false;
        for (uint8_t ch : bytes)
        {
            if (quoted)
            {
                if (escaped)
                    escaped = false;
                else if (ch == '\\')
                    escaped = true;
                else if (ch == '"')
                    quoted = false;
            }
            else if (ch == '"')
                quoted = true;
            else if (ch == '{' || ch == '[')
            {
                if (++depth > 16)
                {
                    error = TEXT("移動設定の入れ子が深すぎます");
                    return false;
                }
            }
            else if (ch == '}' || ch == ']')
            {
                if (depth == 0)
                {
                    error = TEXT("移動設定の括弧が不正です");
                    return false;
                }
                --depth;
            }
        }
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
            bytes = {bytes.data() + 3, bytes.size() - 3};
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(bytes, document, &error))
            return false;
        const auto root = document.GetRoot();
        if (!Fields(root, {"schema", "gaits", "pivot", "slope", "lean", "jump", "smoothing", "turnSlowdown"}) ||
            !root.FindMember("schema").IsString() || !AsciiName(root.FindMember("schema").AsString(), "locomotion.v1"))
        {
            error = TEXT("移動設定のschemaまたは最上位fieldが不正です");
            return false;
        }
        QuadrupedLocomotionParams p;
        const auto gaits = root.FindMember("gaits");
        if (!gaits.IsArray() || gaits.GetArraySize() != 4)
        {
            error = TEXT("移動設定のgaitsは4要素の配列が必要です");
            return false;
        }
        for (size_t i = 0; i < 4; ++i)
        {
            auto& g = p.Gaits[i];
            if (!Numbers(gaits.GetArrayElement(i), {{"speedMin", &g.SpeedMin},
                                                    {"speedMax", &g.SpeedMax},
                                                    {"upThreshold", &g.UpThreshold},
                                                    {"downThreshold", &g.DownThreshold},
                                                    {"minDwell", &g.MinDwell},
                                                    {"accel", &g.Acceleration},
                                                    {"decel", &g.Deceleration},
                                                    {"brake", &g.Brake},
                                                    {"turnRateCap", &g.TurnRateCap},
                                                    {"lateralAccel", &g.LateralAcceleration},
                                                    {"strideLength", &g.StrideLength}}))
            {
                error = TEXT("歩様設定のfieldまたは数値が不正です");
                return false;
            }
        }
        const auto slowdown = root.FindMember("turnSlowdown");
        if (!slowdown.IsNumber() || !std::isfinite(slowdown.AsNumber()) || slowdown.AsNumber() < 0 ||
            slowdown.AsNumber() > 1)
        {
            error = TEXT("旋回減速係数は0から1が必要です");
            return false;
        }
        p.TurnSlowdown = static_cast<float>(slowdown.AsNumber());
        if (!Numbers(root.FindMember("pivot"), {{"speed", &p.PivotSpeed},
                                                {"enterAngle", &p.PivotEnterAngle},
                                                {"exitAngle", &p.PivotExitAngle},
                                                {"rate", &p.PivotRate}}) ||
            !Numbers(root.FindMember("slope"), {{"limit", &p.SlopeLimit},
                                                {"uphillK", &p.UphillCoefficient},
                                                {"downhillBoost", &p.DownhillBoost},
                                                {"minScale", &p.MinimumSlopeScale}}) ||
            !Numbers(root.FindMember("lean"),
                     {{"gain", &p.LeanGain}, {"maxBank", &p.MaximumBank}, {"halfLife", &p.LeanHalfLife}}) ||
            !Numbers(root.FindMember("jump"), {{"heightMin", &p.JumpHeightMin},
                                               {"heightMax", &p.JumpHeightMax},
                                               {"coyote", &p.CoyoteTime},
                                               {"buffer", &p.JumpBuffer},
                                               {"airControl", &p.AirControl},
                                               {"airTurnRate", &p.AirTurnRate},
                                               {"landingSpeedLoss", &p.LandingSpeedLoss},
                                               {"landingRecovery", &p.LandingRecovery}}) ||
            !Numbers(root.FindMember("smoothing"), {{"targetHalfLife", &p.TargetHalfLife}}) ||
            !QuadrupedLocomotionModel::IsValidParams(p))
        {
            error = TEXT("移動設定のfield・範囲または歩様の境界条件が不正です");
            return false;
        }
        const auto jump = root.FindMember("jump");
        if (jump.FindMember("heightMin").AsNumber() > jump.FindMember("heightMax").AsNumber())
        {
            error = TEXT("最大ジャンプ高さが最小値より小さくなっています");
            return false;
        }
        out = p;
        return true;
    }
} // namespace NorvesLib::Core::Locomotion

#include "DogMovementAutopilotScript.h"
#include "Locomotion/QuadrupedLocomotionModel.h"
#include "Text/JsonDocument.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>

namespace Game::GameModes::DogMovementAutopilot
{
    using namespace NorvesLib::Core;
    namespace
    {
        constexpr const char* StageNames[] = {
            "step_climb", "step_block", "slope_climb", "slope_block", "platform", "gallop",
            "turn", "jump", "camera_low", "camera_sprint", "camera_wall", "hitstop"};
        static_assert(sizeof(StageNames) / sizeof(StageNames[0]) == static_cast<size_t>(AutopilotStage::Count));

        bool NameEquals(const Container::String& value, const char* name)
        {
            const size_t length = std::strlen(name);
            if (value.size() != length)
            {
                return false;
            }
            for (size_t i = 0; i < length; ++i)
            {
                if (value[i] != static_cast<Container::String::value_type>(name[i]))
                {
                    return false;
                }
            }
            return true;
        }

        bool IsName(JsonValue value, const char* name)
        {
            return value.IsString() && NameEquals(value.AsString(), name);
        }

        // 個数と各名前の出現数を照合し、未知のフィールドと重複キーをともに拒否する。
        bool Fields(JsonValue object, std::initializer_list<const char*> names)
        {
            if (!object.IsObject() || object.GetObjectSize() != names.size())
            {
                return false;
            }
            for (const char* name : names)
            {
                unsigned count = 0;
                for (size_t i = 0; i < object.GetObjectSize(); ++i)
                {
                    if (NameEquals(object.GetMemberName(i), name))
                    {
                        ++count;
                    }
                }
                if (count != 1)
                {
                    return false;
                }
            }
            return true;
        }

        bool Number(JsonValue value, double low, double high, float& out)
        {
            const double number = value.AsNumber();
            if (!value.IsNumber() || !std::isfinite(number) || number < low || number > high)
            {
                return false;
            }
            out = static_cast<float>(number);
            return true;
        }

        bool Fail(Container::String& error, const Container::String::value_type* reason)
        {
            error = reason;
            return false;
        }

        bool ParseInput(JsonValue value, double duration, double& previous, AutopilotInput& out,
                        Container::String& error)
        {
            // 時刻の比較はfloatへの丸め前に行う。同時刻の入力は列の順を保持する。
            const auto time = value.FindMember("t");
            if (!Number(time, 0, duration, out.Time))
            {
                return Fail(error, TEXT("out_of_range"));
            }
            if (time.AsNumber() < previous)
            {
                return Fail(error, TEXT("time_reversed"));
            }
            previous = time.AsNumber();
            if (value.HasMember("look"))
            {
                const auto look = value.FindMember("look");
                if (!Fields(value, {"t", "look"}))
                {
                    return Fail(error, TEXT("invalid_fields"));
                }
                if (!look.IsArray() || look.GetArraySize() != 2 ||
                    !Number(look.GetArrayElement(0), -2000, 2000, out.LookX) ||
                    !Number(look.GetArrayElement(1), -2000, 2000, out.LookY))
                {
                    return Fail(error, TEXT("out_of_range"));
                }
                out.Kind = AutopilotInputKind::Look;
                return true;
            }
            if (!Fields(value, {"t", "key", "action"}))
            {
                return Fail(error, TEXT("invalid_fields"));
            }
            const auto action = value.FindMember("action");
            if (!IsName(action, "press") && !IsName(action, "release"))
            {
                return Fail(error, TEXT("unknown_action"));
            }
            out.Action = IsName(action, "press") ? Input::InputAction::Pressed : Input::InputAction::Released;
            const auto key = value.FindMember("key");
            if (IsName(key, "MouseLeft") || IsName(key, "MouseRight"))
            {
                out.Kind = AutopilotInputKind::MouseButton;
                out.Button = IsName(key, "MouseLeft") ? Input::MouseButton::Left : Input::MouseButton::Right;
                return true;
            }
            constexpr const char* names[] = {"W", "A", "S", "D", "Space", "LeftShift", "E"};
            constexpr Input::KeyCode codes[] = {Input::KeyCode::W, Input::KeyCode::A, Input::KeyCode::S,
                Input::KeyCode::D, Input::KeyCode::Space, Input::KeyCode::LeftShift, Input::KeyCode::E};
            for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i)
            {
                if (IsName(key, names[i]))
                {
                    out.Key = codes[i];
                    return true;
                }
            }
            return Fail(error, TEXT("unknown_key"));
        }

        float FiniteResult(double value)
        {
            return std::isfinite(value) && value >= 0 && value <= std::numeric_limits<float>::max()
                ? static_cast<float>(value) : 0.f;
        }
    } // namespace

    const char* StageName(AutopilotStage stage)
    {
        const auto index = static_cast<size_t>(stage);
        return index < static_cast<size_t>(AutopilotStage::Count) ? StageNames[index] : "";
    }

    bool ParseStageName(const Container::String& name, AutopilotStage& out)
    {
        for (size_t i = 0; i < static_cast<size_t>(AutopilotStage::Count); ++i)
        {
            if (NameEquals(name, StageNames[i]))
            {
                out = static_cast<AutopilotStage>(i);
                return true;
            }
        }
        return false;
    }

    bool ParseAutopilotScript(Container::Span<const uint8_t> bytes, AutopilotScript& out, Container::String& error)
    {
        error.clear();
        if (!bytes.data() || bytes.empty())
        {
            return Fail(error, TEXT("empty_script"));
        }
        if (bytes.size() >= 3 && bytes[0] == 0xef && bytes[1] == 0xbb && bytes[2] == 0xbf)
        {
            bytes = {bytes.data() + 3, bytes.size() - 3};
        }
        // 正しい台本の最大の深さは5。未知の入れ子で解析器の再帰スタックを消費させない。
        unsigned depth = 0;
        bool bQuoted = false, bEscaped = false;
        for (uint8_t ch : bytes)
        {
            if (bQuoted)
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
                    bQuoted = false;
                }
            }
            else if (ch == '"')
            {
                bQuoted = true;
            }
            else if (ch == '{' || ch == '[')
            {
                if (++depth > 8)
                {
                    return Fail(error, TEXT("invalid_fields"));
                }
            }
            else if (ch == '}' || ch == ']')
            {
                if (depth == 0)
                {
                    return Fail(error, TEXT("invalid_json"));
                }
                --depth;
            }
        }
        JsonDocument document;
        if (!JsonDocument::TryParseUtf8(bytes, document))
        {
            return Fail(error, TEXT("invalid_json"));
        }
        const auto root = document.GetRoot();
        if (!Fields(root, {"schema", "segments"}) || !IsName(root.FindMember("schema"), "dog_autopilot.v1"))
        {
            return Fail(error, TEXT("invalid_schema_or_fields"));
        }
        const auto segments = root.FindMember("segments");
        if (!segments.IsArray() || segments.GetArraySize() != static_cast<size_t>(AutopilotStage::Count))
        {
            return Fail(error, TEXT("stage_count"));
        }
        AutopilotScript parsed;
        bool bSeen[static_cast<size_t>(AutopilotStage::Count)] = {};
        for (size_t i = 0; i < segments.GetArraySize(); ++i)
        {
            const auto value = segments.GetArrayElement(i);
            if (!Fields(value, {"stage", "start", "yawDeg", "duration", "inputs"}))
            {
                return Fail(error, TEXT("invalid_fields"));
            }
            AutopilotSegment segment;
            const auto stage = value.FindMember("stage");
            if (!stage.IsString() || !ParseStageName(stage.AsString(), segment.Stage))
            {
                return Fail(error, TEXT("unknown_stage"));
            }
            auto& bUsed = bSeen[static_cast<size_t>(segment.Stage)];
            if (bUsed)
            {
                return Fail(error, TEXT("duplicate_stage"));
            }
            bUsed = true;
            const auto start = value.FindMember("start");
            const auto duration = value.FindMember("duration");
            constexpr double maximum = std::numeric_limits<float>::max();
            if (!start.IsArray() || start.GetArraySize() != 3 ||
                !Number(start.GetArrayElement(0), -100, 100, segment.Start.x) ||
                !Number(start.GetArrayElement(1), -100, 100, segment.Start.y) ||
                !Number(start.GetArrayElement(2), -100, 100, segment.Start.z) ||
                !Number(value.FindMember("yawDeg"), -maximum, maximum, segment.YawDegrees) ||
                !Number(duration, 0, 30, segment.Duration) || duration.AsNumber() <= 0 || segment.Duration <= 0)
            {
                return Fail(error, TEXT("out_of_range"));
            }
            const auto inputs = value.FindMember("inputs");
            if (!inputs.IsArray())
            {
                return Fail(error, TEXT("invalid_fields"));
            }
            double previous = 0;
            for (size_t j = 0; j < inputs.GetArraySize(); ++j)
            {
                AutopilotInput input;
                if (!ParseInput(inputs.GetArrayElement(j), duration.AsNumber(), previous, input, error))
                {
                    return false;
                }
                segment.Inputs.push_back(input);
            }
            parsed.Segments.push_back(std::move(segment));
        }
        out = std::move(parsed);
        return true;
    }

    float ExpectedTurnRadius(float speed, float turnRateCap, float lateralAcceleration)
    {
        if (!std::isfinite(speed) || !std::isfinite(turnRateCap) || !std::isfinite(lateralAcceleration) ||
            speed <= 0 || turnRateCap <= 0 || lateralAcceleration <= 0)
        {
            return 0;
        }
        const double v = speed;
        return FiniteResult(std::max(v / turnRateCap, v * v / lateralAcceleration));
    }

    float ExpectedGallopTime(const Locomotion::QuadrupedLocomotionParams& params)
    {
        if (!Locomotion::QuadrupedLocomotionModel::IsValidParams(params))
        {
            return 0;
        }
        const double target = params.Gaits[3].SpeedMax;
        double time = 2 * static_cast<double>(params.TargetHalfLife) / 1.6783469900166605;
        double speed = 0;
        for (unsigned i = 0; i < 3; ++i)
        {
            const auto& gait = params.Gaits[i];
            if (gait.UpThreshold >= target)
            {
                return 0;
            }
            const double interval = std::max(static_cast<double>(gait.MinDwell),
                std::max(0.0, gait.UpThreshold - speed) / gait.Acceleration);
            time += interval;
            speed = std::min(target, speed + gait.Acceleration * interval);
        }
        return FiniteResult(time);
    }

    float ExpectedJumpDistance(const Locomotion::QuadrupedLocomotionParams& params,
                               float takeoffHorizontalSpeed, float gravity)
    {
        if (!Locomotion::QuadrupedLocomotionModel::IsValidParams(params) ||
            !std::isfinite(takeoffHorizontalSpeed) || takeoffHorizontalSpeed < 0 ||
            !std::isfinite(gravity) || gravity <= 0)
        {
            return 0;
        }
        const double speed = takeoffHorizontalSpeed;
        const double fraction = std::clamp(speed / params.Gaits[3].SpeedMax, 0.0, 1.0);
        const double height = params.JumpHeightMin + (params.JumpHeightMax - params.JumpHeightMin) * fraction;
        return FiniteResult(speed * 2 * std::sqrt(2 * height / gravity));
    }
} // namespace Game::GameModes::DogMovementAutopilot

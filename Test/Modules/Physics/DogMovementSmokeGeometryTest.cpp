#include "Game/GameModes/DogMovementSmoke/DogMovementSmokeGeometry.h"
#include "Game/GameModes/DogMovementSmoke/DogMovementAutopilotScript.h"
#include "Physics/CharacterMoveSettings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Rendering;
namespace
{
    namespace Autopilot = Game::GameModes::DogMovementAutopilot;

    Container::VariableArray<uint8_t> SmallScript(const char* firstInputs,
                                                const char* duration = "2", const char* start = "[1,0,2]")
    {
        Container::VariableArray<uint8_t> bytes;
        const auto append = [&](const char* text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text);
            bytes.insert(bytes.end(), begin, begin + std::strlen(text));
        };
        append("{\"schema\":\"dog_autopilot.v1\",\"segments\":[");
        for (unsigned i = 0; i < static_cast<unsigned>(Autopilot::AutopilotStage::Count); ++i)
        {
            char segment[2048];
            const int length = std::snprintf(segment, sizeof(segment),
                "%s{\"stage\":\"%s\",\"start\":%s,\"yawDeg\":23,\"duration\":%s,\"inputs\":%s}",
                i == 0 ? "" : ",", Autopilot::StageName(static_cast<Autopilot::AutopilotStage>(i)),
                i == 0 ? start : "[0,0,0]", i == 0 ? duration : "2", i == 0 ? firstInputs : "[]");
            assert(length > 0 && static_cast<size_t>(length) < sizeof(segment));
            append(segment);
        }
        append("]}");
        return bytes;
    }

    unsigned Check(const Container::VariableArray<Mesh3DVertex>& vertices,
                   const Container::VariableArray<uint32_t>& indices)
    {
        assert(indices.size() % 3 == 0);
        for (const auto& v : vertices)
        {
            for (float value : v.Position)
                assert(std::isfinite(value));
            for (float value : v.Normal)
                assert(std::isfinite(value));
            const float n = v.Normal[0] * v.Normal[0] + v.Normal[1] * v.Normal[1] + v.Normal[2] * v.Normal[2];
            assert(std::fabs(n - 1) < 1e-5f);
        }
        unsigned degenerate = 0;
        const auto position = [&](uint32_t i) {
            assert(i < vertices.size());
            const auto& v = vertices[i];
            return Math::Vector3(v.Position[0], v.Position[1], v.Position[2]);
        };
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const auto a = position(indices[i]), b = position(indices[i + 1]), c = position(indices[i + 2]);
            const auto cross = Math::Vector3::Cross(b - a, c - a);
            if (cross.LengthSquared() < 1e-14f)
            {
                ++degenerate;
                continue;
            }
            const auto& v = vertices[indices[i]];
            const Math::Vector3 normal(v.Normal[0], v.Normal[1], v.Normal[2]);
            // GBufferのClockwise表面。通常の右手系外積とは反対向き。
            assert(Math::Vector3::Dot(cross, normal) < 0);
        }
        return degenerate;
    }
} // namespace
int main()
{
    Container::VariableArray<Mesh3DVertex> vertices;
    Container::VariableArray<uint32_t> indices;
    Game::GameModes::DogMovementSmokeGeometry::Capsule(vertices, indices);
    assert(vertices.size() == 450 && indices.size() == 2448);
    assert(Check(vertices, indices) == 48);
    float low = 1, high = -1;
    for (const auto& v : vertices)
    {
        assert(std::fabs(v.Position[0]) <= .300001f && std::fabs(v.Position[2]) <= .300001f);
        low = std::fmin(low, v.Position[1]);
        high = std::fmax(high, v.Position[1]);
    }
    assert(std::fabs(low) < 1e-6f && std::fabs(high - .8f) < 1e-6f);
    Game::GameModes::DogMovementSmokeGeometry::WallBox(vertices, indices);
    assert(vertices.size() == 24 && indices.size() == 36);
    assert(Check(vertices, indices) == 0);
    for (const auto& v : vertices)
        assert(std::fabs(v.Position[0]) <= 4.00001f && std::fabs(v.Position[1]) <= 1.50001f &&
               std::fabs(v.Position[2]) <= .10001f);
    // 見本の全段階・開始位置と入力の値を検査する。
    namespace Autopilot = Game::GameModes::DogMovementAutopilot;
    Container::VariableArray<uint8_t> sample;
    FILE* file = nullptr;
    assert(fopen_s(&file, NORVES_SOURCE_ROOT "/Assets/Gameplay/DogMovementAutopilot.json", "rb") == 0 && file);
    uint8_t buffer[4096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) != 0)
    {
        sample.insert(sample.end(), buffer, buffer + count);
    }
    assert(std::ferror(file) == 0);
    std::fclose(file);
    Autopilot::AutopilotScript script;
    Container::String error;
    assert(Autopilot::ParseAutopilotScript({sample.data(), sample.size()}, script, error));
    assert(error.empty() && script.Segments.size() == 12);
    const size_t inputCounts[] = {2, 2, 2, 2, 0, 4, 4, 4, 1, 4, 0, 2};
    const float durations[] = {2.5f, 2.5f, 2.5f, 2.5f, 4, 7, 2.5f, 3.5f, 2, 4, 2, 2};
    for (unsigned i = 0; i < 12; ++i)
    {
        const auto stage = static_cast<Autopilot::AutopilotStage>(i);
        const auto& segment = script.Segments[i];
        assert(segment.Stage == stage && segment.Inputs.size() == inputCounts[i]);
        assert(segment.Start == Autopilot::StageStart(stage).ToVector3());
        assert(segment.YawDegrees == Autopilot::StageYawDegrees(stage) && segment.Duration == durations[i]);
    }
    assert(script.Segments[0].Inputs[0].Key == Input::KeyCode::W);
    assert(script.Segments[0].Inputs[1].Action == Input::InputAction::Released);
    assert(script.Segments[5].Inputs[1].Key == Input::KeyCode::LeftShift);
    assert(script.Segments[7].Inputs[1].Key == Input::KeyCode::Space);
    assert(script.Segments[8].Inputs[0].Kind == Autopilot::AutopilotInputKind::Look);
    assert(script.Segments[8].Inputs[0].Time == .25f);
    assert(script.Segments[11].Inputs[0].Kind == Autopilot::AutopilotInputKind::MouseButton);
    assert(script.Segments[11].Inputs[0].Button == Input::MouseButton::Left);

    // 小さい正しい列で同時刻、全キーの対応、視点移動と解放を確かめる。
    auto scriptBytes = SmallScript(R"([{"t":0,"key":"W","action":"press"},
        {"t":0,"key":"A","action":"press"},{"t":0,"key":"S","action":"press"},
        {"t":0,"key":"D","action":"press"},{"t":0,"key":"Space","action":"press"},
        {"t":0,"key":"LeftShift","action":"press"},{"t":0,"key":"E","action":"press"},
        {"t":0,"key":"MouseLeft","action":"press"},{"t":0,"key":"MouseRight","action":"release"},
        {"t":0.5,"look":[-2,3]},{"t":2,"key":"W","action":"release"}])");
    assert(Autopilot::ParseAutopilotScript({scriptBytes.data(), scriptBytes.size()}, script, error));
    assert(script.Segments.size() == 12 && script.Segments[0].Inputs.size() == 11 && error.empty());
    const auto& first = script.Segments[0];
    assert(first.Start == Math::Vector3(1, 0, 2) && first.YawDegrees == 23 && first.Duration == 2);
    const Input::KeyCode codes[] = {Input::KeyCode::W, Input::KeyCode::A, Input::KeyCode::S,
        Input::KeyCode::D, Input::KeyCode::Space, Input::KeyCode::LeftShift, Input::KeyCode::E};
    for (unsigned i = 0; i < 7; ++i)
    {
        assert(first.Inputs[i].Kind == Autopilot::AutopilotInputKind::Key && first.Inputs[i].Key == codes[i]);
        assert(first.Inputs[i].Action == Input::InputAction::Pressed && first.Inputs[i].Time == 0);
    }
    assert(first.Inputs[8].Button == Input::MouseButton::Right &&
           first.Inputs[8].Action == Input::InputAction::Released);
    assert(first.Inputs[9].Time == .5f && first.Inputs[9].LookX == -2 && first.Inputs[9].LookY == 3);
    assert(first.Inputs[10].Time == 2 && first.Inputs[10].Action == Input::InputAction::Released);
    const uint8_t bom[] = {0xef, 0xbb, 0xbf};
    scriptBytes.insert(scriptBytes.begin(), bom, bom + 3);
    assert(Autopilot::ParseAutopilotScript({scriptBytes.data(), scriptBytes.size()}, script, error));
    assert(script.Segments[0].Inputs.size() == 11 && error.empty());

    const auto reject = [&](const Container::VariableArray<uint8_t>& bytes, const char* expected) {
        assert(!Autopilot::ParseAutopilotScript({bytes.data(), bytes.size()}, script, error));
        assert(error.size() == std::strlen(expected));
        for (size_t i = 0; i < error.size(); ++i)
        {
            assert(error[i] == static_cast<Container::String::value_type>(expected[i]));
        }
        // 解析失敗で呼出側が保持する台本を上書きしない。
        assert(script.Segments.size() == 12 && script.Segments[0].Inputs.size() == 11);
        assert(script.Segments[0].Start == Math::Vector3(1, 0, 2) && script.Segments[0].YawDegrees == 23);
    };
    reject(SmallScript(R"([{"t":1,"key":"W","action":"press"},
                          {"t":0.5,"key":"W","action":"release"}])"), "time_reversed");
    reject(SmallScript(R"([{"t":0,"key":"Q","action":"press"}])"), "unknown_key");
    reject(SmallScript(R"([{"t":2.01,"key":"W","action":"press"}])"), "out_of_range");
    reject(SmallScript(R"([{"t":-0.1,"key":"W","action":"press"}])"), "out_of_range");
    reject(SmallScript("[]", "0"), "out_of_range");
    reject(SmallScript("[]", "30.01"), "out_of_range");
    reject(SmallScript("[]", "2", "[100.01,0,0]"), "out_of_range");
    reject(SmallScript("[]", "2", "[0,-100.01,0]"), "out_of_range");

    assert(std::fabs(Autopilot::ExpectedTurnRadius(2, 1, 8) - 2) < 1e-6f);
    assert(std::fabs(Autopilot::ExpectedTurnRadius(4, 4, 2) - 8) < 1e-6f);
    const Modules::Physics::CharacterMoveSettings settings;
    assert(Autopilot::LowStep.TopY() < settings.StepHeight && Autopilot::HighStep.TopY() > settings.StepHeight);
    assert(Autopilot::LowSlope.AngleDegrees < settings.MaximumSlopeDegrees && Autopilot::HighSlope.AngleDegrees > settings.MaximumSlopeDegrees);
    std::cout << "DogMovementSmokeGeometryTest passed\n";
    return 0;
}

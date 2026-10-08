#include "Input/HapticsJson.h"
#include "Game/Input/GameHapticsSettings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <iostream>
#include <initializer_list>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    constexpr const char* Good = R"({"schema":"haptics.v1","settings":{"enabled":true,"strength":0.5,"mix_mode":"add_clamp"},"effects":[{"name":"Hit","duration":1,"loop":true,"priority":7,"low":[{"t":0,"v":0.8},{"t":1,"v":0}],"high":[]}]})";
    Container::String Wrap(const char* effect)
    {
        Container::String text = "{\"schema\":\"haptics.v1\",\"effects\":[";
        text.append(effect);
        text.append("]}");
        return text;
    }
    void Reject(const Container::String& json)
    {
        HapticsConfiguration value;
        assert(HapticsJson::Parse(Good,value));
        Container::String before;
        assert(HapticsJson::Write(value,before));
        HapticsJsonReport report;
        assert(!HapticsJson::Parse(json,value,&report));
        assert(!report.Error.empty());
        Container::String after;
        assert(HapticsJson::Write(value,after) && before == after);
    }
    void RoundtripAndInvalid()
    {
        HapticsConfiguration value;
        HapticsJsonReport report;
        assert(HapticsJson::Parse(Good,value,&report));
        report.Error = Good;
        assert(HapticsJson::Parse(report.Error,value,&report));
        assert(report.WarningCount == 0 && report.Error.empty());
        assert(value.Effects.size() == 1 && value.Effects[0].Id == "Hit"_id);
        assert(value.Settings.MixMode == EHapticsMixMode::AddClamp && value.Settings.Strength == .5f);
        assert(value.Effects[0].Loop && value.Effects[0].Priority == 7);
        HapticsOutput original;
        assert(EvaluateHapticsEffect(value.Effects[0].View(),.5,original));
        Container::String json;
        assert(HapticsJson::Write(value,json));
        HapticsConfiguration roundtrip;
        assert(HapticsJson::Parse(json,roundtrip));
        HapticsOutput restored;
        assert(EvaluateHapticsEffect(roundtrip.Effects[0].View(),.5,restored));
        assert(restored.Low == original.Low && restored.High == original.High);
        Container::String bom("\xEF\xBB\xBF");
        bom.append(Good);
        assert(HapticsJson::Parse(bom,roundtrip));
        constexpr const char* unknown = R"({"schema":"haptics.v1","future":1,"settings":{"future":false},"effects":[{"name":"Hit","duration":1,"extra":null,"low":[{"t":0,"v":0.5,"extra":0}],"high":[]}]})";
        assert(HapticsJson::Parse(unknown,roundtrip,&report) && report.WarningCount == 4);
        assert(HapticsJson::Parse(Good,roundtrip,&report) && report.WarningCount == 0);
        for (const char* invalid : {
            "", "[]", "{}", R"({"schema":"haptics.v2","effects":[]})",
            R"({"schema":"haptics.v1","schema":"haptics.v1","effects":[]})",
            R"({"schema":"haptics.v1","effects":null})",
            R"({"schema":"haptics.v1","settings":null,"effects":[]})",
            R"({"schema":"haptics.v1","settings":{"enabled":1},"effects":[]})",
            R"({"schema":"haptics.v1","settings":{"strength":1.00000001},"effects":[]})",
            R"({"schema":"haptics.v1","settings":{"strength":-0.1},"effects":[]})",
            R"({"schema":"haptics.v1","settings":{"mix_mode":"other"},"effects":[]})",
            R"({"schema":"haptics.v1","effects":[]} trailing)"})
        {
            Reject(invalid);
        }
        for (const char* invalid : {
            R"({"name":"","duration":1,"low":[],"high":[]})",
            R"({"name":"A\u0000B","duration":1,"low":[],"high":[]})",
            R"({"name":"Hit","duration":0,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1e999,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"loop":0,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"priority":1.5,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"priority":2147483648,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"priority":-2147483649,"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"low":[],"low":[],"high":[]})",
            R"({"name":"Hit","duration":1,"high":[]})",
            R"({"name":"Hit","duration":1,"low":[{"t":0,"v":1.00000001}],"high":[]})",
            R"({"name":"Hit","duration":1,"low":[],"high":[{"t":0,"v":-0.00001}]})",
            R"({"name":"Hit","duration":1,"low":[{"t":0,"t":0,"v":0.5}],"high":[]})",
            R"({"name":"Hit","duration":1,"low":[{"t":0,"v":0.5},{"t":0,"v":0.2}],"high":[]})",
            R"({"name":"Hit","duration":1,"low":[{"t":1.1,"v":0.5}],"high":[]})",
            R"({"name":"Hit","duration":1,"low":[{"t":-0.1,"v":0.5}],"high":[]})"})
        {
            Reject(Wrap(invalid));
        }
        Reject(Wrap(R"({"name":"A","duration":1,"low":[],"high":[]},{"name":"A","duration":1,"low":[],"high":[]})"));
        Container::String sentinel = "preserve";
        value.Settings.Strength = std::numeric_limits<float>::quiet_NaN();
        assert(!HapticsJson::Write(value,sentinel) && sentinel == "preserve");
        value.Settings.Strength = 1;
        value.Effects[0].Low[0].Time = -1;
        assert(!HapticsJson::Write(value,sentinel) && sentinel == "preserve");
    }
    void Limits()
    {
        Container::String huge(HapticsJson::MaximumTextBytes + 1,' ');
        Reject(huge);
        Container::String deep;
        for (size_t index = 0; index < HapticsJson::MaximumDepth + 1; ++index)
        {
            deep.push_back('[');
        }
        for (size_t index = 0; index < HapticsJson::MaximumDepth + 1; ++index)
        {
            deep.push_back(']');
        }
        Reject(deep);
        Container::String longName = "{\"name\":\"";
        for (size_t index = 0; index < HapticsJson::MaximumNameBytes + 1; ++index)
        {
            longName.push_back('A');
        }
        longName.append("\",\"duration\":1,\"low\":[],\"high\":[]}");
        Reject(Wrap(longName.c_str()));
        Container::String manyKeys = "{\"name\":\"A\",\"duration\":1024,\"low\":[";
        for (size_t index = 0; index < HapticsJson::MaximumKeysPerCurve + 1; ++index)
        {
            if (index != 0)
            {
                manyKeys.push_back(',');
            }
            char text[64]{};
            std::snprintf(text,sizeof(text),"{\"t\":%zu,\"v\":0}",index);
            manyKeys.append(text);
        }
        manyKeys.append("],\"high\":[]}");
        Reject(Wrap(manyKeys.c_str()));
        Container::String manyEffects;
        for (size_t index = 0; index < HapticsJson::MaximumEffects + 1; ++index)
        {
            if (index != 0)
            {
                manyEffects.push_back(',');
            }
            char text[128]{};
            std::snprintf(text,sizeof(text),"{\"name\":\"E%zu\",\"duration\":1,\"low\":[],\"high\":[]}",index);
            manyEffects.append(text);
        }
        Reject(Wrap(manyEffects.c_str()));
    }
    void GameApplicationAndAtomicSettings()
    {
        HapticsService service;
        Container::String error;
        assert(Game::Input::ApplyGameHapticsJson(service,Good,error) && error.empty());
        assert(service.Play("Hit"_id,0));
        const auto voices = service.GetActiveVoiceCount();
        assert(!Game::Input::ApplyGameHapticsJson(service,"{}",error));
        assert(!error.empty() && service.GetActiveVoiceCount() == voices);
        assert(service.GetEffects()[0].Id == "Hit"_id && service.GetSettings().Strength == .5f);
        HapticsSettings invalid;
        invalid.Strength = 2;
        assert(!service.Configure({},invalid));
        assert(service.GetActiveVoiceCount() == voices && service.GetEffects().size() == 1);
        Asset::AssetFileReader missing("NonexistentHapticsFixture-982741");
        assert(!Game::Input::InitializeGameHaptics(service,missing,error));
        assert(service.GetActiveVoiceCount() == voices && service.GetEffects().size() == 1);
        // ソースassetはCoreのcompiled asset rootから読み込む。
        assert(Game::Input::InitializeGameHaptics(service,error));
        assert(service.GetActiveVoiceCount() == 0 && service.GetEffects().size() == 3);
        assert(service.GetSettings().Strength == .5f && service.GetSettings().Enabled);
        assert(service.Play("Footstep"_id,0));
        assert(service.Play("Hit"_id,0));
        assert(service.Play("BiteHold"_id,0));
    }
}
int main()
{
    RoundtripAndInvalid();
    Limits();
    GameApplicationAndAtomicSettings();
    std::cout << "HapticsJsonTest passed\n";
    return 0;
}

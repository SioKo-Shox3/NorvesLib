#include "Input/HapticsEnvelopeMath.h"
#include "Input/HapticsOutputState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>
#include <initializer_list>

using namespace NorvesLib::Core::Input;
namespace
{
    void Equal(const HapticsOutput& value, float low, float high)
    {
        assert(std::abs(value.Low - low) < 0.00001f);
        assert(std::abs(value.High - high) < 0.00001f);
        assert(IsValidHapticsOutput(value));
    }
    void EnvelopeCases()
    {
        HapticsKeyframe low[]{{0,0}, {1,1}, {2,0}};
        HapticsKeyframe high[]{{.25,.4f}, {1.75,.8f}};
        HapticsEffectView effect{2,false,0,low,high};
        HapticsOutput output{.11f,.22f};
        assert(IsValidHapticsEffect(effect));
        assert(EvaluateHapticsEffect(effect,0,output));
        Equal(output,0,.4f);
        assert(EvaluateHapticsEffect(effect,.5,output));
        Equal(output,.5f,.4f+.4f/6);
        assert(EvaluateHapticsEffect(effect,1,output));
        Equal(output,1,.6f);
        assert(EvaluateHapticsEffect(effect,1.9,output));
        Equal(output,.1f,.8f);
        for (double elapsed : {2.0, 3.0, std::numeric_limits<double>::max()})
        {
            assert(EvaluateHapticsEffect(effect,elapsed,output));
            Equal(output,0,0);
        }
        effect.Loop = true;
        for (double elapsed : {2.0, 4.0, std::numeric_limits<double>::max()})
        {
            assert(EvaluateHapticsEffect(effect,elapsed,output));
            Equal(output,0,.4f);
        }
        assert(EvaluateHapticsEffect(effect,2.5,output));
        Equal(output,.5f,.4f+.4f/6);
        const float beforeLow = output.Low;
        const float beforeHigh = output.High;
        for (double invalid : {-1.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        {
            assert(!EvaluateHapticsEffect(effect,invalid,output));
            assert(output.Low == beforeLow && output.High == beforeHigh);
        }
        for (double invalid : {0.0, -1.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        {
            effect.Duration = invalid;
            assert(!EvaluateHapticsEffect(effect,0,output));
            assert(output.Low == beforeLow && output.High == beforeHigh);
        }
        effect.Duration = 2;
        HapticsKeyframe bad[2]{{0,.5f},{1,.5f}};
        effect.Low = bad;
        for (double invalid : {-1.0, 3.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()})
        {
            bad[0].Time = invalid;
            assert(!EvaluateHapticsEffect(effect,0,output));
        }
        bad[0].Time = 1;
        assert(!IsValidHapticsEffect(effect));
        bad[0].Time = 1.5;
        assert(!IsValidHapticsEffect(effect));
        bad[0].Time = 0;
        for (float invalid : {-.1f, 1.1f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            bad[0].Value = invalid;
            assert(!EvaluateHapticsEffect(effect,0,output));
        }
        effect.Low = {};
        effect.High = {};
        assert(EvaluateHapticsEffect(effect,1,output));
        Equal(output,0,0);
        HapticsKeyframe single[]{{0,.7f}};
        effect = {std::numeric_limits<double>::denorm_min(),true,0,single,{}};
        assert(EvaluateHapticsEffect(effect,std::numeric_limits<double>::max(),output));
        Equal(output,.7f,0);
    }
    void MixCases()
    {
        HapticsMixSample samples[]{
            {{.8f,.2f},-10,true}, {{.3f,.6f},-10,true},
            {{1,1},-11,true}, {{1,1},100,false}};
        HapticsOutput output;
        assert(MixHapticsSamples(samples,EHapticsMixMode::Maximum,1,output));
        Equal(output,.8f,.6f);
        assert(MixHapticsSamples(samples,EHapticsMixMode::AddClamp,.5f,output));
        Equal(output,.5f,.4f);
        samples[2] = {{0,0},0,true};
        assert(MixHapticsSamples(samples,EHapticsMixMode::Maximum,1,output));
        Equal(output,0,0);
        samples[2].Active = false;
        assert(MixHapticsSamples(samples,EHapticsMixMode::Maximum,0,output));
        Equal(output,0,0);
        for (auto& sample : samples)
        {
            sample.Active = false;
        }
        assert(MixHapticsSamples(samples,EHapticsMixMode::AddClamp,1,output));
        Equal(output,0,0);
        assert(MixHapticsSamples({},EHapticsMixMode::Maximum,1,output));
        Equal(output,0,0);
        output = {.25f,.75f};
        for (float strength : {-.1f, 1.1f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            assert(!MixHapticsSamples(samples,EHapticsMixMode::Maximum,strength,output));
            Equal(output,.25f,.75f);
        }
        assert(!MixHapticsSamples(samples,static_cast<EHapticsMixMode>(255),1,output));
        samples[3].Output.High = std::numeric_limits<float>::quiet_NaN();
        assert(!MixHapticsSamples(samples,EHapticsMixMode::Maximum,1,output));
        Equal(output,.25f,.75f);
        HapticsMixSample smallContributions[1025]{};
        smallContributions[0].Output = {.5f,.5f};
        for (size_t index = 1; index < 1025; ++index)
        {
            smallContributions[index].Output = {.00000002f,.00000002f};
        }
        assert(MixHapticsSamples(smallContributions,EHapticsMixMode::AddClamp,1,output));
        const float accumulated = static_cast<float>(.5 + 1024.0 * static_cast<double>(.00000002f));
        assert(output.Low == accumulated && output.High == accumulated);
        HapticsMixSample extremes[]{{{.1f,.2f},INT32_MIN,true},{{.2f,.3f},INT32_MAX,true}};
        assert(MixHapticsSamples(extremes,EHapticsMixMode::Maximum,1,output));
        Equal(output,.2f,.3f);
    }
    void OutputCases()
    {
        HapticsOutputState state;
        assert(!state.HasAcknowledgedOutput() && !state.MayBeActive());
        assert(state.NeedsSend({}));
        assert(state.RecordAttempt({},true));
        assert(!state.NeedsSend({}));
        assert(!state.NeedsSend({.001f,.001f}));
        assert(state.NeedsSend({1.0f/255.0f,0}));
        assert(state.NeedsSend({0,1.0f/255.0f}));
        assert(state.RecordAttempt({.001f,.7f},true));
        assert(!state.NeedsSend({.001f,.7f}));
        // 一方のmotorだけ0になる場合も微小差分を理由に停止を省略しない。
        assert(state.NeedsSend({0,.7f}));
        assert(state.RecordAttempt({.7f,.001f},true));
        assert(state.NeedsSend({.7f,0}));
        assert(state.RecordAttempt({.001f,.001f},true));
        assert(state.NeedsSend({}));
        assert(state.RecordAttempt({},false));
        assert(state.MayBeActive() && state.IsUncertain());
        Equal(state.GetAcknowledgedOutput(),.001f,.001f);
        assert(state.NeedsSend({}));
        assert(state.RecordAttempt({},true));
        assert(!state.MayBeActive() && !state.IsUncertain() && !state.NeedsSend({}));
        assert(state.RecordAttempt({.9f,.8f},false));
        Equal(state.GetAcknowledgedOutput(),0,0);
        assert(state.MayBeActive() && state.IsUncertain());
        assert(state.NeedsSend({.9f,.8f}) && state.NeedsSend({}));
        const HapticsOutput invalid{-1,0};
        assert(!state.RecordAttempt(invalid,true) && !state.NeedsSend(invalid));
        assert(state.MayBeActive() && state.IsUncertain());
        Equal(state.GetAcknowledgedOutput(),0,0);
        assert(state.RecordAttempt({},true));
        assert(state.RecordAttempt({},false));
        assert(!state.MayBeActive() && state.IsUncertain() && state.NeedsSend({}));
    }
}
int main()
{
    EnvelopeCases();
    MixCases();
    OutputCases();
    std::cout << "HapticsMixerTest passed\n";
    return 0;
}

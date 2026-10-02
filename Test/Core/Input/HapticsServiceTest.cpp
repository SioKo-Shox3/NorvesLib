#include "Input/HapticsService.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    class Sink final : public IHapticsOutput
    {
    public:
        bool IsConnected(uint8_t slot) const noexcept override
        {
            ++Queries;
            return Connected[slot];
        }
        bool SetVibration(uint8_t slot, float low, float high) noexcept override
        {
            ++Writes[slot];
            Requested[slot] = {low,high};
            if (Owner)
            {
                assert(!Owner->StopAll());
                assert(!Owner->SetFocused(false));
                assert(!Owner->SetPaused(true));
                assert(!Owner->SetSettings({}));
                assert(!Owner->Configure({}));
                assert(Owner->Play("Base"_id,0) == 0);
                assert(!Owner->Update(0,*this));
                assert(!Owner->FlushOutputs(*this));
            }
            if (Fail[slot])
            {
                return false;
            }
            Applied[slot] = {low,high};
            return true;
        }
        bool Connected[GamepadSlotCount]{true,false,false,false};
        bool Fail[GamepadSlotCount]{};
        int Writes[GamepadSlotCount]{};
        mutable int Queries = 0;
        HapticsOutput Requested[GamepadSlotCount]{};
        HapticsOutput Applied[GamepadSlotCount]{};
        HapticsService* Owner = nullptr;
    };
    HapticsEffectDefinition Effect(Identity id, bool loop = false)
    {
        HapticsEffectDefinition effect;
        effect.Id = id;
        effect.Duration = 1;
        effect.Loop = loop;
        effect.Low.push_back({0,.8f});
        effect.High.push_back({0,.4f});
        return effect;
    }
    void Equal(const HapticsOutput& output, float low, float high)
    {
        assert(std::abs(output.Low-low)<.00001f && std::abs(output.High-high)<.00001f);
    }
    void OwnershipAndTiming()
    {
        HapticsService service;
        Sink sink;
        HapticsEffectDefinition definitions[]{Effect("Base"_id)};
        assert(service.Configure(definitions));
        definitions[0].Low[0].Value = 0;
        const auto handle = service.Play("Base"_id,0);
        assert(handle != 0 && service.GetActiveVoiceCount() == 1);
        // Play直後は前frameの大きいdtを遡らず、時刻0を評価する。
        assert(service.Update(100,sink));
        Equal(sink.Applied[0],.4f,.2f);
        const int writes = sink.Writes[0];
        assert(service.Update(.25,sink));
        assert(service.FlushOutputs(sink) && sink.Writes[0] == writes);
        const int queries = sink.Queries;
        assert(!service.Update(-1,sink));
        assert(!service.Update(std::numeric_limits<double>::quiet_NaN(),sink));
        assert(sink.Queries == queries && sink.Writes[0] == writes);
        assert(service.Update(.75,sink));
        assert(service.GetActiveVoiceCount() == 0 && !service.Stop(handle));
        Equal(sink.Applied[0],0,0);
        assert(sink.Writes[0] == writes + 1);
        assert(service.Update(1,sink) && sink.Writes[0] == writes + 1);
        assert(service.Play("Missing"_id,0) == 0 && service.Play("Base"_id,4) == 0);
        assert(service.Play("Base"_id,0,-1) == 0);
        assert(service.Play("Base"_id,0,std::numeric_limits<float>::infinity()) == 0);
        const auto old = service.Play("Base"_id,0,.5f);
        assert(old != 0 && old != handle);
        assert(service.FlushOutputs(sink));
        Equal(sink.Applied[0],.2f,.1f);
        assert(service.Stop(old) && !service.Stop(old));
        assert(service.FlushOutputs(sink));
        Equal(sink.Applied[0],0,0);
    }
    void ConfigurationAndControls()
    {
        HapticsService service;
        Sink sink;
        HapticsEffectDefinition definitions[]{Effect("Base"_id,true)};
        assert(service.Configure(definitions));
        auto handle = service.Play("Base"_id,0);
        assert(service.Update(0,sink));
        auto invalid = definitions[0];
        invalid.Duration = 0;
        assert(!service.Configure({&invalid,1}));
        assert(service.GetActiveVoiceCount() == 1);
        HapticsEffectDefinition duplicate[]{definitions[0],definitions[0]};
        assert(!service.Configure(duplicate));
        assert(service.Stop(handle));
        HapticsService other;
        assert(other.Configure(definitions));
        const auto foreign = other.Play("Base"_id,0);
        handle = service.Play("Base"_id,0);
        assert(foreign != handle && !service.Stop(foreign));
        HapticsSettings settings;
        settings.Strength = 1;
        settings.MixMode = EHapticsMixMode::AddClamp;
        assert(service.SetSettings(settings));
        assert(service.Play("Base"_id,0));
        assert(service.Update(0,sink));
        Equal(sink.Applied[0],1,.8f);
        settings.Strength = -1;
        assert(!service.SetSettings(settings));
        assert(service.GetSettings().Strength == 1);
        assert(service.SetPaused(true) && service.GetActiveVoiceCount() == 0);
        assert(service.Play("Base"_id,0) == 0 && service.FlushOutputs(sink));
        Equal(sink.Applied[0],0,0);
        assert(service.SetPaused(false) && service.Update(1,sink));
        Equal(sink.Applied[0],0,0);
        assert(service.Play("Base"_id,0) && service.FlushOutputs(sink));
        assert(service.SetFocused(false) && service.FlushOutputs(sink));
        Equal(sink.Applied[0],0,0);
        assert(service.Play("Base"_id,0) == 0);
        assert(service.SetFocused(true));
        settings = service.GetSettings();
        settings.Enabled = false;
        assert(service.SetSettings(settings));
        assert(service.Play("Base"_id,0) == 0);
        settings.Enabled = true;
        assert(service.SetSettings(settings));
        for (size_t index = 0; index < HapticsService::MaximumVoices; ++index)
        {
            assert(service.Play("Base"_id,0));
        }
        assert(service.Play("Base"_id,0) == 0 && service.GetActiveVoiceCount() == HapticsService::MaximumVoices);
        assert(service.StopAll());
        handle = service.Play("Base"_id,0);
        const auto& owned = service.GetEffects();
        assert(service.Configure({owned.data(),owned.size()}));
        assert(!service.Stop(handle) && service.GetActiveVoiceCount() == 0);
        assert(service.Configure({}) && service.GetEffects().empty());
        assert(service.Play("Base"_id,0) == 0);
    }
    void FailureDisconnectAndReentry()
    {
        HapticsService service;
        Sink sink;
        HapticsEffectDefinition definitions[]{Effect("Base"_id,true)};
        assert(service.Configure(definitions));
        sink.Fail[0] = true;
        assert(service.Play("Base"_id,0));
        assert(!service.Update(0,sink));
        assert(service.StopAll());
        assert(!service.FlushOutputs(sink));
        Equal(sink.Requested[0],0,0);
        sink.Fail[0] = false;
        assert(service.FlushOutputs(sink));
        const int stoppedWrites = sink.Writes[0];
        assert(service.FlushOutputs(sink) && sink.Writes[0] == stoppedWrites);
        sink.Owner = &service;
        const auto handle = service.Play("Base"_id,0);
        assert(service.Update(0,sink));
        assert(service.GetActiveVoiceCount() == 1);
        sink.Connected[0] = false;
        assert(service.Update(.1,sink));
        assert(service.GetActiveVoiceCount() == 0 && !service.Stop(handle));
        Equal(sink.Applied[0],0,0);
        sink.Connected[0] = true;
        assert(service.Update(0,sink));
        Equal(sink.Applied[0],0,0);
    }
}
int main()
{
    OwnershipAndTiming();
    ConfigurationAndControls();
    FailureDisconnectAndReentry();
    std::cout << "HapticsServiceTest passed\n";
    return 0;
}

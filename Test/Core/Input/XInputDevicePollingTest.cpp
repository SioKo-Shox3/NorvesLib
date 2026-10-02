// 差替APIと実InputStateを使い、poll順序/再試行/配送modeをSDK非依存で試験する。
#include "Core/Private/Platform/Windows/XInputPollingState.h"
#include "Input/InputState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>
#include <initializer_list>
using namespace NorvesLib::Core::Input;
namespace
{
    class Api final : public IXInputApi
    {
    public:
        XInputReadResult Results[GamepadSlotCount]{};
        XInputRawGamepadState Values[GamepadSlotCount]{};
        int Calls[GamepadSlotCount]{};
        int Total = 0;
        XInputReadResult ReadState(uint8_t slot, XInputRawGamepadState& raw) noexcept override
        {
            assert(slot < GamepadSlotCount);
            ++Calls[slot]; ++Total;
            raw = Values[slot]; // Errorのrawも埋め、読取側が失敗値を無視することを検証する。
            return Results[slot];
        }
        void Connect(uint8_t slot)
        {
            Results[slot] = {EXInputReadStatus::Connected, 0};
            Values[slot].Buttons = static_cast<uint16_t>(GamepadButton::A);
            Values[slot].LeftX = 32767;
            Values[slot].PacketNumber = 91;
        }
    };
    class Sink final : public IGamepadSampleSink
    {
    public:
        InputState State;
        int Calls[GamepadSlotCount]{};
        int Accepted[GamepadSlotCount]{};
        int Reject[GamepadSlotCount]{};
        EGamepadSampleMode Modes[GamepadSlotCount]{};
        bool SubmitGamepadSample(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode) override
        {
            assert(slot < GamepadSlotCount);
            ++Calls[slot];
            Modes[slot] = mode;
            if (Reject[slot] > 0)
            {
                --Reject[slot];
                return false;
            }
            assert(State.SetGamepadState(slot, state, mode));
            ++Accepted[slot];
            return true;
        }
    };
    void SparseProbeAndClock()
    {
        Api api; Sink sink; XInputPollingState polling(api);
        assert(!polling.Poll(0, sink) && api.Total == 0);
        polling.Initialize();
        for (double invalid : {-1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()})
        {
            assert(!polling.Poll(invalid, sink) && api.Total == 0);
        }
        for (int i = 0; i < 4; ++i)
        {
            const auto total = api.Total;
            assert(polling.Poll(static_cast<double>(i) / 100, sink));
            assert(api.Total == total + 1 && api.Calls[i] == 1);
            assert(!polling.IsConnected(static_cast<uint8_t>(i)) && polling.HasReadResult(static_cast<uint8_t>(i)));
        }
        assert(polling.Poll(0.5, sink) && api.Total == 4);
        assert(polling.Poll(1, sink) && api.Calls[0] == 2 && api.Total == 5);
        assert(polling.Poll(1.005, sink) && api.Total == 5);
        assert(polling.Poll(1.02, sink) && api.Calls[1] == 2 && api.Total == 6);
        assert(polling.Poll(1.03, sink) && api.Calls[2] == 2 && api.Total == 7);
        assert(polling.Poll(1.04, sink) && api.Calls[3] == 2 && api.Total == 8);
        assert(!polling.Poll(1.03, sink) && api.Total == 8);
        assert(polling.Poll(100000, sink) && api.Total == 9); // catch-upで全slotを連打しない。
        assert(!polling.IsConnected(255) && !polling.HasReadResult(255));
        assert(polling.GetLastReadResult(255).Status == EXInputReadStatus::Disconnected);
        polling.Shutdown();
        assert(!polling.IsInitialized() && !polling.HasReadResult(0));
        assert(!polling.Poll(100001, sink) && api.Total == 9);
        polling.Initialize();
        assert(polling.Poll(0, sink) && api.Total == 10); // 新しいrunのclockへ戻せる。
    }
    void LiveFocusAndReconnect()
    {
        Api api; Sink sink; XInputPollingState polling(api);
        api.Connect(0); polling.Initialize();
        assert(polling.Poll(0, sink) && polling.IsConnected(0));
        assert(sink.Modes[0] == EGamepadSampleMode::Baseline && !sink.State.IsGamepadButtonPressed(0, GamepadButton::A));
        polling.Initialize(); // 再Initializeは接続/初回基準を壊さない。
        assert(polling.Poll(0.1, sink));
        assert(api.Calls[0] == 2 && api.Calls[1] == 1 && sink.Modes[0] == EGamepadSampleMode::Live);
        assert(sink.State.GetGamepadSampleSerial(0) == 2 && sink.State.GetLastGamepadSample(0).PacketNumber == 91);
        polling.SetFocused(false);
        sink.State.ReleaseAll(); // owner側のfocus Cancelとは独立した境界。
        assert(polling.Poll(0.2, sink));
        assert(sink.Modes[0] == EGamepadSampleMode::Background && sink.State.GetGamepadState(0).Buttons == 0);
        assert(sink.State.GetGamepadAxis(0, GamepadAxis::LeftX) == 0 && sink.State.GetLastGamepadSample(0).Axes[0] == 1);
        polling.SetFocused(true);
        assert(polling.Poll(0.3, sink));
        assert(sink.Modes[0] == EGamepadSampleMode::Baseline && !sink.State.IsGamepadButtonPressed(0, GamepadButton::A));
        assert(polling.Poll(0.4, sink) && sink.Modes[0] == EGamepadSampleMode::Live);
        api.Values[0].Buttons = 0;
        assert(polling.Poll(0.5, sink));
        api.Values[0].Buttons = static_cast<uint16_t>(GamepadButton::A);
        assert(polling.Poll(0.6, sink) && sink.State.IsGamepadButtonPressed(0, GamepadButton::A));
        api.Results[0] = {EXInputReadStatus::Disconnected, 7};
        assert(polling.Poll(0.7, sink) && !polling.IsConnected(0));
        assert(!sink.State.GetGamepadState(0).Connected && sink.Modes[0] == EGamepadSampleMode::Live);
        const int reads = api.Calls[0];
        api.Connect(0);
        assert(polling.Poll(0.8, sink) && api.Calls[0] == reads);
        assert(polling.Poll(1.8, sink) && api.Calls[0] == reads + 1); // 同frameで再読取しない。
        assert(polling.IsConnected(0) && sink.Modes[0] == EGamepadSampleMode::Baseline);
        assert(!sink.State.IsGamepadButtonPressed(0, GamepadButton::A));
        assert(polling.Poll(1.81, sink) && sink.Modes[0] == EGamepadSampleMode::Live);
    }
    void ErrorsRemainVisible()
    {
        Api api; Sink sink; XInputPollingState polling(api);
        api.Connect(0); polling.Initialize();
        assert(polling.Poll(0, sink));
        api.Results[0] = {EXInputReadStatus::Error, 77};
        assert(!polling.Poll(0.1, sink));
        assert(!polling.IsConnected(0) && !sink.State.GetGamepadState(0).Connected);
        assert(sink.State.GetLastGamepadSample(0).Buttons == 0); // Errorのrawを採用しない。
        assert(polling.GetLastReadResult(0).Status == EXInputReadStatus::Error && polling.GetLastReadResult(0).NativeError == 77);
        const int reads = api.Calls[0];
        assert(!polling.Poll(0.2, sink) && api.Calls[0] == reads); // 新しいAPI呼出しがなくても未回復errorを報告する。
        assert(!polling.Poll(0.3, sink) && api.Calls[0] == reads); // slot3の初期probeまで進める。
        api.Results[0] = {EXInputReadStatus::Disconnected, 0};
        assert(polling.Poll(1.2, sink) && api.Calls[0] == reads + 1);
        polling.Shutdown(); polling.Initialize();
        api.Results[0] = {static_cast<EXInputReadStatus>(255), 81};
        assert(!polling.Poll(0, sink));
        assert(polling.GetLastReadResult(0).Status == EXInputReadStatus::Error);
        assert(!sink.State.GetGamepadState(0).Connected);
    }
    void RejectedSamplesRetry()
    {
        Api api; Sink sink; XInputPollingState polling(api);
        api.Connect(0); sink.Reject[0] = 1; polling.Initialize();
        assert(!polling.Poll(0, sink) && !polling.IsConnected(0));
        assert(sink.Modes[0] == EGamepadSampleMode::Baseline && sink.State.GetGamepadSampleSerial(0) == 0);
        assert(polling.Poll(0.01, sink) && api.Calls[0] == 2);
        assert(polling.IsConnected(0) && sink.Modes[0] == EGamepadSampleMode::Baseline);
        assert(polling.Poll(0.02, sink) && sink.Modes[0] == EGamepadSampleMode::Live);
        api.Values[0].Buttons = static_cast<uint16_t>(GamepadButton::B);
        sink.Reject[0] = 1;
        assert(!polling.Poll(0.03, sink));
        assert(sink.State.GetGamepadState(0).Buttons == static_cast<uint16_t>(GamepadButton::A));
        assert(polling.Poll(0.04, sink));
        assert(sink.State.GetGamepadState(0).Buttons == static_cast<uint16_t>(GamepadButton::B));
        api.Results[0] = {EXInputReadStatus::Disconnected, 0}; sink.Reject[0] = 1;
        assert(!polling.Poll(0.05, sink) && polling.IsConnected(0));
        assert(polling.Poll(0.06, sink) && !polling.IsConnected(0));
        api.Connect(0);
        assert(polling.Poll(1.07, sink) && polling.IsConnected(0) && sink.Modes[0] == EGamepadSampleMode::Baseline);
        polling.SetFocused(false); polling.Shutdown(); polling.Initialize();
        assert(!polling.IsFocused());
        assert(polling.Poll(0, sink) && sink.Modes[0] == EGamepadSampleMode::Background);
    }
    void HugeClockDoesNotSpin()
    {
        Api api; Sink sink; XInputPollingState polling(api);
        polling.Initialize();
        const double maximum = std::numeric_limits<double>::max();
        for (int i = 0; i < 4; ++i)
        {
            assert(polling.Poll(maximum, sink) && api.Total == i + 1);
        }
        assert(polling.Poll(maximum, sink) && api.Total == 4);
    }
}
int main()
{
    SparseProbeAndClock();
    LiveFocusAndReconnect();
    ErrorsRemainVisible();
    RejectedSamplesRetry();
    HugeClockDoesNotSpin();
    std::puts("XInputDevicePollingTest passed");
    return 0;
}

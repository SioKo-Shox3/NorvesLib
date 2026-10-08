#include "Core/Private/Platform/Windows/XInputVibrationState.h"
#include "Input/IInputDevice.h"
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
    class Api final : public IXInputApi
    {
    public:
        XInputReadResult ReadState(uint8_t, XInputRawGamepadState&) noexcept override
        {
            return {};
        }
        XInputWriteResult WriteVibration(uint8_t slot, const XInputMotorState& motors) noexcept override
        {
            assert(slot < GamepadSlotCount && Count < 128);
            Slots[Count] = slot;
            Values[Count++] = motors;
            return {!Fail[slot], Fail[slot] ? uint32_t{5} : uint32_t{0}};
        }
        bool Fail[GamepadSlotCount]{};
        uint8_t Slots[128]{};
        XInputMotorState Values[128]{};
        int Count = 0;
    };
    class LegacyDevice final : public IInputDevice
    {
    public:
        bool Initialize() override { return true; }
        void PollEvents(InputSystem&) override {}
        void Shutdown() override
        {
            ++Calls;
            if (Fail)
            {
                throw 7;
            }
        }
        bool Fail = true;
        int Calls = 0;
    };
    void BasicAndValidation()
    {
        Api api;
        XInputVibrationState state(api);
        assert(!state.SetVibration(0,1,1) && api.Count == 0);
        assert(state.Initialize());
        assert(state.Initialize() && api.Count == 0);
        for (float invalid : {-1.0f, 1.1f, std::numeric_limits<float>::infinity(),
            std::numeric_limits<float>::quiet_NaN()})
        {
            assert(!state.SetVibration(0,invalid,0));
            assert(!state.SetVibration(0,0,invalid));
        }
        assert(!state.SetVibration(4,0,0) && !state.SetVibration(255,0,0));
        assert(api.Count == 0 && !state.HasPendingStop());
        for (uint8_t slot = 0; slot < GamepadSlotCount; ++slot)
        {
            assert(state.SetVibration(slot,1,.5f));
            assert(api.Slots[slot] == slot);
            assert(api.Values[slot].Low == 65535 && api.Values[slot].High == 32768);
            assert(state.MayBeActive(slot));
        }
        assert(!state.MayBeActive(4));
        assert(state.SetFocused(false));
        assert(api.Count == 8);
        for (int index = 4; index < 8; ++index)
        {
            assert(api.Slots[index] == index - 4);
            assert(api.Values[index].Low == 0 && api.Values[index].High == 0);
        }
        assert(!state.SetVibration(0,1,0) && api.Count == 8);
        assert(state.SetFocused(false) && api.Count == 8);
        assert(state.Shutdown() && state.Shutdown());
        assert(api.Count == 8);
        assert(state.Initialize());
        assert(!state.SetVibration(0,1,0));
        assert(state.SetFocused(true));
        assert(state.SetVibration(2,0,.0001f));
        assert(api.Values[8].High == 7);
        assert(state.Shutdown());
        assert(api.Count == 10 && api.Slots[9] == 2 && api.Values[9].High == 0);
    }
    void FailedOutputAndShutdown()
    {
        Api api;
        XInputVibrationState state(api);
        assert(state.Initialize());
        api.Fail[0] = true;
        assert(!state.SetVibration(0,.7f,.3f));
        assert(state.MayBeActive(0) && state.HasPendingStop());
        assert(!state.GetLastWriteResult(0).Accepted && state.GetLastWriteResult(0).NativeError == 5);
        // 不確実なnonzeroを重ねず、先にzeroを試みる。
        assert(!state.SetVibration(0,.8f,.4f));
        assert(api.Count == 2 && api.Values[1].Low == 0 && api.Values[1].High == 0);
        assert(state.SetVibration(1,1,1));
        assert(!state.Shutdown());
        assert(api.Count == 5 && api.Slots[3] == 0 && api.Slots[4] == 1);
        assert(state.HasPendingStop() && state.MayBeActive(0) && !state.MayBeActive(1));
        assert(!state.Initialize());
        assert(api.Count == 6 && api.Values[5].Low == 0);
        assert(!state.SetVibration(1,1,1) && api.Count == 6);
        api.Fail[0] = false;
        assert(state.Shutdown());
        assert(api.Count == 7 && !state.HasPendingStop() && !state.MayBeActive(0));
        assert(state.Initialize());
        assert(state.SetVibration(0,.4f,.2f));
        api.Fail[0] = true;
        assert(!state.SetFocused(false));
        assert(!state.SetVibration(0,1,1));
        const int before = api.Count;
        assert(!state.RetryPendingStops() && api.Count == before + 1);
        api.Fail[0] = false;
        assert(state.RetryPendingStops());
        assert(!state.MayBeActive(0) && !state.HasPendingStop());
        assert(state.SetFocused(true));
        assert(state.SetVibration(0,1,1));
        assert(state.Shutdown());
    }
    void PendingZeroThenNewOutput()
    {
        Api api;
        XInputVibrationState state(api);
        assert(state.Initialize());
        api.Fail[2] = true;
        assert(!state.SetVibration(2,1,1));
        api.Fail[2] = false;
        assert(state.SetVibration(2,.5f,.5f));
        assert(api.Count == 3);
        assert(api.Values[1].Low == 0 && api.Values[1].High == 0);
        assert(api.Values[2].Low == 32768 && api.Values[2].High == 32768);
        assert(state.MayBeActive(2) && !state.HasPendingStop());
        api.Fail[2] = true;
        assert(!state.SetVibration(2,0,0));
        assert(state.MayBeActive(2));
        api.Fail[2] = false;
        assert(state.SetVibration(2,0,0));
        assert(api.Count == 5 && !state.MayBeActive(2) && !state.HasPendingStop());
        assert(state.Shutdown() && api.Count == 5);

        LegacyDevice legacy;
        assert(!legacy.SetVibration(0,1,1));
        assert(!legacy.TryShutdown() && legacy.Calls == 1);
        legacy.Fail = false;
        assert(legacy.TryShutdown() && legacy.Calls == 2);
    }
}
int main()
{
    BasicAndValidation();
    FailedOutputAndShutdown();
    PendingZeroThenNewOutput();
    std::cout << "XInputVibrationStateTest passed\n";
    return 0;
}

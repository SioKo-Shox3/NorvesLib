#include "XInputDevice.h"
#include "Input/InputSystem.h"
#include <chrono>

namespace NorvesLib::Core::Input
{
    namespace
    {
        class InputSystemGamepadSink final : public IGamepadSampleSink
        {
        public:
            explicit InputSystemGamepadSink(InputSystem& system) : m_System(system)
            {
            }
            bool SubmitGamepadSample(uint8_t slot, const GamepadState& state, EGamepadSampleMode mode) override
            {
                return m_System.InjectGamepadState(slot, state, mode);
            }
        private:
            InputSystem& m_System;
        };
    }

    void XInputDevice::PollEvents(InputSystem& system)
    {
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        (void)PollEvents(system, seconds);
    }

    bool XInputDevice::PollEvents(InputSystem& system, double unscaledTimeSeconds)
    {
        InputSystemGamepadSink sink(system);
        return m_State.Poll(unscaledTimeSeconds, sink);
    }
} // namespace NorvesLib::Core::Input

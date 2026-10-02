// 捕捉した物理sourceを明示出力設定へ変換する値境界を試験する。
#include "Game/Input/GameInputRebindTypes.h"
#include "Input/InputActionRuntime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorvesLib::Core::Input;
using namespace Game::Input;
namespace
{
    void DirectionsAndOutput()
    {
        InputCapturedControl captured{{EInputBindingSource::GamepadAxis, 1, 2}, InputModifierCtrl, -1};
        GameInputRebindOutput output;
        output.Component = EInputAxisComponent::Y;
        output.Scale = 2;
        InputBinding binding;
        assert(BuildGameInputRebindBinding(captured, output, EInputMappingValueType::Axis2D, EInputAxisOutput::Normalized, binding));
        assert(binding.Source.Kind == EInputBindingSource::GamepadAxis && binding.Source.Code == 1 && binding.Source.Slot == 2);
        assert(binding.RequiredModifiers == InputModifierCtrl && binding.Component == EInputAxisComponent::Y);
        assert(binding.Scale == 2 && binding.Invert);
        output.bInvert = true;
        assert(BuildGameInputRebindBinding(captured, output, EInputMappingValueType::Axis2D, EInputAxisOutput::Normalized, binding));
        assert(!binding.Invert);
        output.bAlignCapturedDirection = false;
        assert(BuildGameInputRebindBinding(captured, output, EInputMappingValueType::Axis2D, EInputAxisOutput::Normalized, binding));
        assert(binding.Invert);
        captured = {{EInputBindingSource::MouseDelta, 0, 0}, 0, 1};
        output = {};
        assert(BuildGameInputRebindBinding(captured, output, EInputMappingValueType::Axis1D, EInputAxisOutput::FrameDelta, binding));
        assert(!binding.Invert && binding.Scale == 1);
        captured = {{EInputBindingSource::MouseWheel, 1, 0}, InputModifierShift, -1};
        assert(BuildGameInputRebindBinding(captured, output, EInputMappingValueType::Button, EInputAxisOutput::Normalized, binding));
        assert(binding.Invert && binding.Source.Code == 1 && binding.RequiredModifiers == InputModifierShift);
    }
    void InvalidPreservesOutput()
    {
        InputCapturedControl captured{{EInputBindingSource::Key, static_cast<uint16_t>(KeyCode::A), 0}, 0, 1};
        InputBinding binding;
        binding.Scale = 42;
        GameInputRebindOutput output;
        auto fails = [&](InputCapturedControl control, GameInputRebindOutput settings,
            EInputMappingValueType type = EInputMappingValueType::Button,
            EInputAxisOutput axis = EInputAxisOutput::Normalized)
        {
            assert(!BuildGameInputRebindBinding(control, settings, type, axis, binding));
            assert(binding.Scale == 42 && binding.Source.Code == 0);
        };
        auto bad = captured;
        bad.Direction = 0; fails(bad, output);
        bad.Direction = 2; fails(bad, output);
        bad.Direction = -1; fails(bad, output);
        bad = captured; bad.Source.Code = static_cast<uint16_t>(KeyCode::Escape); fails(bad, output);
        bad = captured; bad.Source.Code = 65535; fails(bad, output);
        bad = captured; bad.RequiredModifiers = 0x80; fails(bad, output);
        auto invalid = output;
        invalid.Scale = std::numeric_limits<float>::infinity(); fails(captured, invalid);
        invalid = output; invalid.ButtonThreshold = std::numeric_limits<float>::quiet_NaN(); fails(captured, invalid);
        invalid = output; invalid.Component = EInputAxisComponent::Y; fails(captured, invalid);
        fails(captured, output, EInputMappingValueType::Button, EInputAxisOutput::FrameDelta);
        captured.Source = {EInputBindingSource::MouseDelta, 0, 0};
        fails(captured, output, EInputMappingValueType::Axis1D);
    }
    void NegativeGestureActivatesButton()
    {
        InputBinding binding;
        InputCapturedControl captured{{EInputBindingSource::GamepadAxis, 0, 0}, 0, -1};
        assert(BuildGameInputRebindBinding(captured, {}, EInputMappingValueType::Button, EInputAxisOutput::Normalized, binding));
        InputActionRuntime runtime;
        assert(runtime.Configure({}));
        assert(runtime.BeginFrame(0));
        InputState raw;
        InputArmedState armed;
        GamepadState pad;
        pad.Connected = true;
        pad.Axes[0] = -1;
        assert(raw.SetGamepadState(0, pad));
        assert(runtime.SyncButtons({&binding, 1}, raw, armed));
        assert(runtime.GetButton().Held);
        pad.Axes[0] = 0;
        assert(raw.SetGamepadState(0, pad));
        assert(runtime.SyncButtons({&binding, 1}, raw, armed));
        assert(!runtime.GetButton().Held);
    }
}
int main()
{
    DirectionsAndOutput();
    InvalidPreservesOutput();
    NegativeGestureActivatesButton();
    std::puts("GameInputRebindTypesTest passed");
    return 0;
}

#pragma once

#include "Core/Public/Input/InputRebindCaptureState.h"

namespace Game::Input
{
    // 選択slotの出力先/倍率はUI側が明示する。Sourceと修飾は捕捉結果から決まる。
    struct GameInputRebindOutput
    {
        NorvesLib::Core::Input::EInputAxisComponent Component = NorvesLib::Core::Input::EInputAxisComponent::X;
        float Scale = 1;
        bool bInvert = false;
        float ButtonThreshold = 0.5f;
        // 負方向の捕捉操作を出力の正方向として扱う。出力側Invertとはxorで合成する。
        bool bAlignCapturedDirection = true;
    };
    enum class EGameInputRebindApplyResult : uint8_t
    {
        Pending, Applied, Cancelled, Stale, Invalid
    };

    // 純粋な値変換。失敗時はresultを変更しない。
    inline bool BuildGameInputRebindBinding(
        const NorvesLib::Core::Input::InputCapturedControl& control,
        const GameInputRebindOutput& output,
        NorvesLib::Core::Input::EInputMappingValueType type,
        NorvesLib::Core::Input::EInputAxisOutput axisOutput,
        NorvesLib::Core::Input::InputBinding& result)
    {
        namespace Input = NorvesLib::Core::Input;
        if (control.Direction != 1 && control.Direction != -1)
        {
            return false;
        }
        if (control.Source.Kind == Input::EInputBindingSource::Key &&
            control.Source.Code == static_cast<uint16_t>(Input::KeyCode::Escape))
        {
            return false;
        }
        const bool bSignedSource = control.Source.Kind == Input::EInputBindingSource::GamepadAxis ||
            Input::IsDisplacementSource(control.Source.Kind);
        if (!bSignedSource && control.Direction != 1)
        {
            return false;
        }
        Input::InputBinding binding;
        binding.Source = control.Source;
        binding.RequiredModifiers = control.RequiredModifiers;
        binding.Component = output.Component;
        binding.Scale = output.Scale;
        binding.Invert = output.bInvert != (output.bAlignCapturedDirection && control.Direction < 0);
        binding.ButtonThreshold = output.ButtonThreshold;
        if (!Input::IsValidInputBinding(binding, type, axisOutput))
        {
            return false;
        }
        result = binding;
        return true;
    }
} // namespace Game::Input

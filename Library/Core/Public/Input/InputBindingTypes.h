#pragma once

#include "Input/InputTypes.h"
#include "Input/GamepadTypes.h"
#include <cmath>
#include <cstdint>

namespace NorvesLib::Core::Input
{
    enum class EInputMappingValueType : uint8_t { Button, Axis1D, Axis2D };
    enum class EInputBindingSource : uint8_t { Key, MouseButton, MouseDelta, MouseWheel, GamepadButton, GamepadAxis, GamepadTrigger };
    enum class EInputAxisComponent : uint8_t { X, Y };
    enum class EInputAxisOutput : uint8_t { Normalized, FrameDelta };
    using InputModifierMask = uint8_t;
    inline constexpr InputModifierMask InputModifierShift=1, InputModifierCtrl=2, InputModifierAlt=4;
    inline constexpr InputModifierMask AllInputModifiers = InputModifierShift | InputModifierCtrl | InputModifierAlt;

    // MouseDeltaのcodeはX=0/Y=1、MouseWheelはvertical=0/horizontal=1。
    // Gamepad以外のslotは0。codeの型変換前に範囲を検証する。
    struct InputPhysicalSource
    {
        EInputBindingSource Kind = EInputBindingSource::Key;
        uint16_t Code = 0;
        uint8_t Slot = 0;
    };

    inline constexpr bool IsGamepadSource(EInputBindingSource kind)
    {
        return kind == EInputBindingSource::GamepadButton || kind == EInputBindingSource::GamepadAxis ||
            kind == EInputBindingSource::GamepadTrigger;
    }
    inline constexpr bool IsDisplacementSource(EInputBindingSource kind)
    {
        return kind == EInputBindingSource::MouseDelta || kind == EInputBindingSource::MouseWheel;
    }
    inline constexpr bool IsValidPhysicalSource(const InputPhysicalSource& source)
    {
        if (IsGamepadSource(source.Kind) ? source.Slot >= GamepadSlotCount : source.Slot != 0) return false;
        switch (source.Kind)
        {
        case EInputBindingSource::Key:
            return source.Code > static_cast<uint16_t>(KeyCode::None) && source.Code < static_cast<uint16_t>(KeyCode::Count);
        case EInputBindingSource::MouseButton: return source.Code < static_cast<uint16_t>(MouseButton::Count);
        case EInputBindingSource::MouseDelta:
        case EInputBindingSource::MouseWheel: return source.Code < 2;
        case EInputBindingSource::GamepadButton: return IsValidGamepadButton(source.Code);
        case EInputBindingSource::GamepadAxis: return source.Code < static_cast<uint16_t>(GamepadAxis::Count);
        case EInputBindingSource::GamepadTrigger: return source.Code < static_cast<uint16_t>(GamepadTrigger::Count);
        default: return false;
        }
    }

    struct InputBinding
    {
        InputPhysicalSource Source;
        EInputAxisComponent Component = EInputAxisComponent::X;
        float Scale = 1.0f;
        bool Invert = false;
        InputModifierMask RequiredModifiers = 0;
        float ButtonThreshold = 0.5f;
    };

    inline bool IsValidInputBinding(const InputBinding& binding, EInputMappingValueType type, EInputAxisOutput output)
    {
        if (type != EInputMappingValueType::Button && type != EInputMappingValueType::Axis1D && type != EInputMappingValueType::Axis2D) return false;
        if (output != EInputAxisOutput::Normalized && output != EInputAxisOutput::FrameDelta) return false;
        if (!IsValidPhysicalSource(binding.Source) || !std::isfinite(binding.Scale) ||
            (binding.RequiredModifiers & ~AllInputModifiers) != 0 ||
            !std::isfinite(binding.ButtonThreshold) || binding.ButtonThreshold < 0 || binding.ButtonThreshold > 1) return false;
        if (binding.Component != EInputAxisComponent::X && binding.Component != EInputAxisComponent::Y) return false;
        if (type != EInputMappingValueType::Axis2D && binding.Component != EInputAxisComponent::X) return false;
        if (type == EInputMappingValueType::Button) return output == EInputAxisOutput::Normalized;
        // 相対変位を正規化入力値へ暗黙clampしない。変位sourceはFrameDelta軸へ接続する。
        return output == EInputAxisOutput::FrameDelta || !IsDisplacementSource(binding.Source.Kind);
    }
} // namespace NorvesLib::Core::Input

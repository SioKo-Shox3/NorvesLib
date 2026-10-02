#pragma once

#include "Input/InputBindingTypes.h"
#include "Input/InputAxisMath.h"
#include "Input/InputButtonState.h"
#include "Container/Span.h"

namespace NorvesLib::Core::Input
{
    struct InputActionSettings
    {
        EInputMappingValueType Type = EInputMappingValueType::Button;
        EInputAxisOutput Output = EInputAxisOutput::Normalized;
        InputAxisResponse AxisResponse;
        InputButtonTiming ButtonTiming;
        float MouseSensitivity = 1.0f;
        float RateSensitivity = 1.0f;
    };

    inline bool IsValidInputActionSettings(const InputActionSettings& settings)
    {
        if (settings.Type != EInputMappingValueType::Button && settings.Type != EInputMappingValueType::Axis1D &&
            settings.Type != EInputMappingValueType::Axis2D) return false;
        if (settings.Output != EInputAxisOutput::Normalized && settings.Output != EInputAxisOutput::FrameDelta) return false;
        if (settings.Type == EInputMappingValueType::Button && settings.Output != EInputAxisOutput::Normalized) return false;
        return IsValidAxisResponse(settings.AxisResponse) && IsValidInputButtonTiming(settings.ButtonTiming) &&
            std::isfinite(settings.MouseSensitivity) && settings.MouseSensitivity >= 0 &&
            std::isfinite(settings.RateSensitivity) && settings.RateSensitivity >= 0;
    }

    inline bool IsValidInputActionBindings(const InputActionSettings& settings, Container::Span<const InputBinding> bindings)
    {
        if (!IsValidInputActionSettings(settings)) return false;
        for (const auto& binding : bindings)
            if (!IsValidInputBinding(binding, settings.Type, settings.Output)) return false;
        return true;
    }
} // namespace NorvesLib::Core::Input

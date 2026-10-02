#pragma once
#include "Input/InputBindingTypes.h"
#include "Container/Span.h"
#include <cstring>
#include <limits>

namespace NorvesLib::Core::Input
{
    struct InputCodeName { uint16_t Code; const char* Name; };
    namespace Detail
    {
        inline constexpr InputCodeName KeyNames[] = {
            {static_cast<uint16_t>(KeyCode::A), "A"},
            {static_cast<uint16_t>(KeyCode::B), "B"},
            {static_cast<uint16_t>(KeyCode::C), "C"},
            {static_cast<uint16_t>(KeyCode::D), "D"},
            {static_cast<uint16_t>(KeyCode::E), "E"},
            {static_cast<uint16_t>(KeyCode::F), "F"},
            {static_cast<uint16_t>(KeyCode::G), "G"},
            {static_cast<uint16_t>(KeyCode::H), "H"},
            {static_cast<uint16_t>(KeyCode::I), "I"},
            {static_cast<uint16_t>(KeyCode::J), "J"},
            {static_cast<uint16_t>(KeyCode::K), "K"},
            {static_cast<uint16_t>(KeyCode::L), "L"},
            {static_cast<uint16_t>(KeyCode::M), "M"},
            {static_cast<uint16_t>(KeyCode::N), "N"},
            {static_cast<uint16_t>(KeyCode::O), "O"},
            {static_cast<uint16_t>(KeyCode::P), "P"},
            {static_cast<uint16_t>(KeyCode::Q), "Q"},
            {static_cast<uint16_t>(KeyCode::R), "R"},
            {static_cast<uint16_t>(KeyCode::S), "S"},
            {static_cast<uint16_t>(KeyCode::T), "T"},
            {static_cast<uint16_t>(KeyCode::U), "U"},
            {static_cast<uint16_t>(KeyCode::V), "V"},
            {static_cast<uint16_t>(KeyCode::W), "W"},
            {static_cast<uint16_t>(KeyCode::X), "X"},
            {static_cast<uint16_t>(KeyCode::Y), "Y"},
            {static_cast<uint16_t>(KeyCode::Z), "Z"},
            {static_cast<uint16_t>(KeyCode::Num0), "Num0"},
            {static_cast<uint16_t>(KeyCode::Num1), "Num1"},
            {static_cast<uint16_t>(KeyCode::Num2), "Num2"},
            {static_cast<uint16_t>(KeyCode::Num3), "Num3"},
            {static_cast<uint16_t>(KeyCode::Num4), "Num4"},
            {static_cast<uint16_t>(KeyCode::Num5), "Num5"},
            {static_cast<uint16_t>(KeyCode::Num6), "Num6"},
            {static_cast<uint16_t>(KeyCode::Num7), "Num7"},
            {static_cast<uint16_t>(KeyCode::Num8), "Num8"},
            {static_cast<uint16_t>(KeyCode::Num9), "Num9"},
            {static_cast<uint16_t>(KeyCode::F1), "F1"},
            {static_cast<uint16_t>(KeyCode::F2), "F2"},
            {static_cast<uint16_t>(KeyCode::F3), "F3"},
            {static_cast<uint16_t>(KeyCode::F4), "F4"},
            {static_cast<uint16_t>(KeyCode::F5), "F5"},
            {static_cast<uint16_t>(KeyCode::F6), "F6"},
            {static_cast<uint16_t>(KeyCode::F7), "F7"},
            {static_cast<uint16_t>(KeyCode::F8), "F8"},
            {static_cast<uint16_t>(KeyCode::F9), "F9"},
            {static_cast<uint16_t>(KeyCode::F10), "F10"},
            {static_cast<uint16_t>(KeyCode::F11), "F11"},
            {static_cast<uint16_t>(KeyCode::F12), "F12"},
            {static_cast<uint16_t>(KeyCode::LeftShift), "LeftShift"},
            {static_cast<uint16_t>(KeyCode::RightShift), "RightShift"},
            {static_cast<uint16_t>(KeyCode::LeftCtrl), "LeftCtrl"},
            {static_cast<uint16_t>(KeyCode::RightCtrl), "RightCtrl"},
            {static_cast<uint16_t>(KeyCode::LeftAlt), "LeftAlt"},
            {static_cast<uint16_t>(KeyCode::RightAlt), "RightAlt"},
            {static_cast<uint16_t>(KeyCode::Space), "Space"},
            {static_cast<uint16_t>(KeyCode::Escape), "Escape"},
            {static_cast<uint16_t>(KeyCode::Tab), "Tab"},
            {static_cast<uint16_t>(KeyCode::Enter), "Enter"},
            {static_cast<uint16_t>(KeyCode::Backspace), "Backspace"},
            {static_cast<uint16_t>(KeyCode::Delete), "Delete"},
            {static_cast<uint16_t>(KeyCode::Insert), "Insert"},
            {static_cast<uint16_t>(KeyCode::Home), "Home"},
            {static_cast<uint16_t>(KeyCode::End), "End"},
            {static_cast<uint16_t>(KeyCode::PageUp), "PageUp"},
            {static_cast<uint16_t>(KeyCode::PageDown), "PageDown"},
            {static_cast<uint16_t>(KeyCode::Up), "Up"},
            {static_cast<uint16_t>(KeyCode::Down), "Down"},
            {static_cast<uint16_t>(KeyCode::Left), "Left"},
            {static_cast<uint16_t>(KeyCode::Right), "Right"},
            {static_cast<uint16_t>(KeyCode::Numpad0), "Numpad0"},
            {static_cast<uint16_t>(KeyCode::Numpad1), "Numpad1"},
            {static_cast<uint16_t>(KeyCode::Numpad2), "Numpad2"},
            {static_cast<uint16_t>(KeyCode::Numpad3), "Numpad3"},
            {static_cast<uint16_t>(KeyCode::Numpad4), "Numpad4"},
            {static_cast<uint16_t>(KeyCode::Numpad5), "Numpad5"},
            {static_cast<uint16_t>(KeyCode::Numpad6), "Numpad6"},
            {static_cast<uint16_t>(KeyCode::Numpad7), "Numpad7"},
            {static_cast<uint16_t>(KeyCode::Numpad8), "Numpad8"},
            {static_cast<uint16_t>(KeyCode::Numpad9), "Numpad9"},
            {static_cast<uint16_t>(KeyCode::NumpadAdd), "NumpadAdd"},
            {static_cast<uint16_t>(KeyCode::NumpadSubtract), "NumpadSubtract"},
            {static_cast<uint16_t>(KeyCode::NumpadMultiply), "NumpadMultiply"},
            {static_cast<uint16_t>(KeyCode::NumpadDivide), "NumpadDivide"},
            {static_cast<uint16_t>(KeyCode::NumpadDecimal), "NumpadDecimal"},
            {static_cast<uint16_t>(KeyCode::NumpadEnter), "NumpadEnter"},
            {static_cast<uint16_t>(KeyCode::CapsLock), "CapsLock"},
            {static_cast<uint16_t>(KeyCode::NumLock), "NumLock"},
            {static_cast<uint16_t>(KeyCode::ScrollLock), "ScrollLock"},
            {static_cast<uint16_t>(KeyCode::PrintScreen), "PrintScreen"},
            {static_cast<uint16_t>(KeyCode::Pause), "Pause"},
            {static_cast<uint16_t>(KeyCode::Semicolon), "Semicolon"},
            {static_cast<uint16_t>(KeyCode::Equal), "Equal"},
            {static_cast<uint16_t>(KeyCode::Comma), "Comma"},
            {static_cast<uint16_t>(KeyCode::Minus), "Minus"},
            {static_cast<uint16_t>(KeyCode::Period), "Period"},
            {static_cast<uint16_t>(KeyCode::Slash), "Slash"},
            {static_cast<uint16_t>(KeyCode::GraveAccent), "GraveAccent"},
            {static_cast<uint16_t>(KeyCode::LeftBracket), "LeftBracket"},
            {static_cast<uint16_t>(KeyCode::Backslash), "Backslash"},
            {static_cast<uint16_t>(KeyCode::RightBracket), "RightBracket"},
            {static_cast<uint16_t>(KeyCode::Apostrophe), "Apostrophe"},
        };
        inline constexpr InputCodeName MouseButtonNames[] = {{static_cast<uint16_t>(MouseButton::Left),"left"},{static_cast<uint16_t>(MouseButton::Right),"right"},{static_cast<uint16_t>(MouseButton::Middle),"middle"},{static_cast<uint16_t>(MouseButton::X1),"x1"},{static_cast<uint16_t>(MouseButton::X2),"x2"}};
        inline constexpr InputCodeName MouseDeltaNames[] = {{0,"x"},{1,"y"}};
        inline constexpr InputCodeName MouseWheelNames[] = {{0,"vertical"},{1,"horizontal"}};
        inline constexpr InputCodeName PadAxisNames[] = {{static_cast<uint16_t>(GamepadAxis::LeftX),"left_x"},{static_cast<uint16_t>(GamepadAxis::LeftY),"left_y"},{static_cast<uint16_t>(GamepadAxis::RightX),"right_x"},{static_cast<uint16_t>(GamepadAxis::RightY),"right_y"}};
        inline constexpr InputCodeName PadTriggerNames[] = {{static_cast<uint16_t>(GamepadTrigger::Left),"left"},{static_cast<uint16_t>(GamepadTrigger::Right),"right"}};
        inline constexpr InputCodeName PadButtonNames[] = {
            {static_cast<uint16_t>(GamepadButton::DpadUp),"dpad_up"},
            {static_cast<uint16_t>(GamepadButton::DpadDown),"dpad_down"},
            {static_cast<uint16_t>(GamepadButton::DpadLeft),"dpad_left"},
            {static_cast<uint16_t>(GamepadButton::DpadRight),"dpad_right"},
            {static_cast<uint16_t>(GamepadButton::Start),"start"},
            {static_cast<uint16_t>(GamepadButton::Back),"back"},
            {static_cast<uint16_t>(GamepadButton::LeftThumb),"left_thumb"},
            {static_cast<uint16_t>(GamepadButton::RightThumb),"right_thumb"},
            {static_cast<uint16_t>(GamepadButton::LeftShoulder),"left_shoulder"},
            {static_cast<uint16_t>(GamepadButton::RightShoulder),"right_shoulder"},
            {static_cast<uint16_t>(GamepadButton::A),"a"},
            {static_cast<uint16_t>(GamepadButton::B),"b"},
            {static_cast<uint16_t>(GamepadButton::X),"x"},
            {static_cast<uint16_t>(GamepadButton::Y),"y"}
        };
    }
    inline bool MatchesInputName(const char* text, size_t size, const char* expected)
    {
        return text && expected && std::strlen(expected)==size && std::memcmp(text,expected,size)==0;
    }
    // 返却span/文字列は静的テーブルを借用し、process中有効。設定/Identityを所有しない。
    inline Container::Span<const InputCodeName> GetInputCodeNames(EInputBindingSource source)
    {
        switch(source)
        {
        case EInputBindingSource::Key:return Detail::KeyNames;
        case EInputBindingSource::MouseButton:return Detail::MouseButtonNames;
        case EInputBindingSource::MouseDelta:return Detail::MouseDeltaNames;
        case EInputBindingSource::MouseWheel:return Detail::MouseWheelNames;
        case EInputBindingSource::GamepadButton:return Detail::PadButtonNames;
        case EInputBindingSource::GamepadAxis:return Detail::PadAxisNames;
        case EInputBindingSource::GamepadTrigger:return Detail::PadTriggerNames;
        default:return {};
        }
    }
    inline const char* GetInputCodeName(EInputBindingSource source, uint16_t code)
    {
        for(const auto& entry:GetInputCodeNames(source)) if(entry.Code==code) return entry.Name;
        return nullptr;
    }
    inline bool TryParseInputCodeName(EInputBindingSource source, const char* text, size_t size, uint16_t& outCode)
    {
        for(const auto& entry:GetInputCodeNames(source))
            if(MatchesInputName(text,size,entry.Name)) { outCode=entry.Code;return true; }
        return false;
    }
    inline const char* GetInputSourceName(EInputBindingSource source)
    {
        switch(source)
        {
        case EInputBindingSource::Key:return "key";
        case EInputBindingSource::MouseButton:return "mouse_button";
        case EInputBindingSource::MouseDelta:return "mouse_delta";
        case EInputBindingSource::MouseWheel:return "mouse_wheel";
        case EInputBindingSource::GamepadButton:return "gamepad_button";
        case EInputBindingSource::GamepadAxis:return "gamepad_axis";
        case EInputBindingSource::GamepadTrigger:return "gamepad_trigger";
        default:return nullptr;
        }
    }
    inline bool TryParseInputSourceName(const char* text, size_t size, EInputBindingSource& outSource)
    {
        for(uint8_t i=0;i<=static_cast<uint8_t>(EInputBindingSource::GamepadTrigger);++i)
        {
            const auto source=static_cast<EInputBindingSource>(i);
            if(MatchesInputName(text,size,GetInputSourceName(source))) { outSource=source;return true; }
        }
        return false;
    }
    // 意味範囲をdoubleで確認し、丸め後も範囲を維持する。false時outは非変更。
    inline bool TryParseInputFloatNumber(double number,float& out,
        double minimum=-static_cast<double>(std::numeric_limits<float>::max()),
        double maximum=static_cast<double>(std::numeric_limits<float>::max()),
        bool minimumExclusive=false,bool maximumExclusive=false)
    {
        if(!std::isfinite(number) || !std::isfinite(minimum) || !std::isfinite(maximum) || minimum>maximum ||
            std::fabs(number)>std::numeric_limits<float>::max()) return false;
        const auto inRange=[=](double value)
        {
            return value>=minimum && value<=maximum &&
                (!minimumExclusive || value!=minimum) && (!maximumExclusive || value!=maximum);
        };
        if(!inRange(number)) return false;
        const float narrowed=static_cast<float>(number);
        if(!inRange(narrowed)) return false;
        out=narrowed;return true;
    }
    // JSON doubleを狭いenumへ入れる前に検証。false時は出力を変更しない。
    inline bool TryParseInputCodeNumber(EInputBindingSource source, double number, uint16_t& outCode)
    {
        if(!std::isfinite(number) || number<0 || number>65535 || std::trunc(number)!=number) return false;
        const auto code=static_cast<uint16_t>(number);
        if(!IsValidPhysicalSource({source,code,0})) return false;
        outCode=code;return true;
    }
    inline bool TryParseInputSlotNumber(double number, uint8_t& outSlot)
    {
        if(!std::isfinite(number) || number<0 || number>=GamepadSlotCount || std::trunc(number)!=number) return false;
        outSlot=static_cast<uint8_t>(number);return true;
    }
}

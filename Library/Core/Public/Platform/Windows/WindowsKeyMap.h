#pragma once

#include "Input/InputTypes.h"
#include <Windows.h>

namespace NorvesLib::Core::Platform
{

    /**
     * @brief Windowsの仮想キーコードをNorvesLib KeyCodeに変換
     * @param vkCode Windows仮想キーコード (VK_*)
     * @return 対応するKeyCode。対応なしの場合はKeyCode::None
     */
    inline Input::KeyCode TranslateWindowsKeyCode(WPARAM vkCode)
    {
        // アルファベット (VK_A=0x41 ~ VK_Z=0x5A)
        if (vkCode >= 'A' && vkCode <= 'Z')
        {
            return static_cast<Input::KeyCode>(
                static_cast<uint16_t>(Input::KeyCode::A) + (vkCode - 'A'));
        }

        // 数字 (VK_0=0x30 ~ VK_9=0x39)
        if (vkCode >= '0' && vkCode <= '9')
        {
            return static_cast<Input::KeyCode>(
                static_cast<uint16_t>(Input::KeyCode::Num0) + (vkCode - '0'));
        }

        // ファンクションキー (VK_F1=0x70 ~ VK_F12=0x7B)
        if (vkCode >= VK_F1 && vkCode <= VK_F12)
        {
            return static_cast<Input::KeyCode>(
                static_cast<uint16_t>(Input::KeyCode::F1) + (vkCode - VK_F1));
        }

        // テンキー (VK_NUMPAD0=0x60 ~ VK_NUMPAD9=0x69)
        if (vkCode >= VK_NUMPAD0 && vkCode <= VK_NUMPAD9)
        {
            return static_cast<Input::KeyCode>(
                static_cast<uint16_t>(Input::KeyCode::Numpad0) + (vkCode - VK_NUMPAD0));
        }

        // 個別マッピング
        switch (vkCode)
        {
        case VK_LSHIFT:
            return Input::KeyCode::LeftShift;
        case VK_RSHIFT:
            return Input::KeyCode::RightShift;
        case VK_LCONTROL:
            return Input::KeyCode::LeftCtrl;
        case VK_RCONTROL:
            return Input::KeyCode::RightCtrl;
        case VK_LMENU:
            return Input::KeyCode::LeftAlt;
        case VK_RMENU:
            return Input::KeyCode::RightAlt;
        case VK_SHIFT:
            return Input::KeyCode::LeftShift; // 左右区別なし→Left
        case VK_CONTROL:
            return Input::KeyCode::LeftCtrl;
        case VK_MENU:
            return Input::KeyCode::LeftAlt;

        case VK_SPACE:
            return Input::KeyCode::Space;
        case VK_ESCAPE:
            return Input::KeyCode::Escape;
        case VK_TAB:
            return Input::KeyCode::Tab;
        case VK_RETURN:
            return Input::KeyCode::Enter;
        case VK_BACK:
            return Input::KeyCode::Backspace;
        case VK_DELETE:
            return Input::KeyCode::Delete;
        case VK_INSERT:
            return Input::KeyCode::Insert;
        case VK_HOME:
            return Input::KeyCode::Home;
        case VK_END:
            return Input::KeyCode::End;
        case VK_PRIOR:
            return Input::KeyCode::PageUp;
        case VK_NEXT:
            return Input::KeyCode::PageDown;

        case VK_UP:
            return Input::KeyCode::Up;
        case VK_DOWN:
            return Input::KeyCode::Down;
        case VK_LEFT:
            return Input::KeyCode::Left;
        case VK_RIGHT:
            return Input::KeyCode::Right;

        case VK_ADD:
            return Input::KeyCode::NumpadAdd;
        case VK_SUBTRACT:
            return Input::KeyCode::NumpadSubtract;
        case VK_MULTIPLY:
            return Input::KeyCode::NumpadMultiply;
        case VK_DIVIDE:
            return Input::KeyCode::NumpadDivide;
        case VK_DECIMAL:
            return Input::KeyCode::NumpadDecimal;

        case VK_CAPITAL:
            return Input::KeyCode::CapsLock;
        case VK_NUMLOCK:
            return Input::KeyCode::NumLock;
        case VK_SCROLL:
            return Input::KeyCode::ScrollLock;
        case VK_SNAPSHOT:
            return Input::KeyCode::PrintScreen;
        case VK_PAUSE:
            return Input::KeyCode::Pause;

        case VK_OEM_1:
            return Input::KeyCode::Semicolon;
        case VK_OEM_PLUS:
            return Input::KeyCode::Equal;
        case VK_OEM_COMMA:
            return Input::KeyCode::Comma;
        case VK_OEM_MINUS:
            return Input::KeyCode::Minus;
        case VK_OEM_PERIOD:
            return Input::KeyCode::Period;
        case VK_OEM_2:
            return Input::KeyCode::Slash;
        case VK_OEM_3:
            return Input::KeyCode::GraveAccent;
        case VK_OEM_4:
            return Input::KeyCode::LeftBracket;
        case VK_OEM_5:
            return Input::KeyCode::Backslash;
        case VK_OEM_6:
            return Input::KeyCode::RightBracket;
        case VK_OEM_7:
            return Input::KeyCode::Apostrophe;

        default:
            return Input::KeyCode::None;
        }
    }

} // namespace NorvesLib::Core::Platform

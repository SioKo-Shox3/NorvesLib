#pragma once

#include "Input/InputTypes.h"

namespace NorvesLib::Core::Input
{
    // F1はdebug overlay有効時だけ予約する。OS/Router/Mapperを所有しない値状態。
    class InputDebugOverlayState
    {
    public:
        void SetEnabled(bool enabled)
        {
            m_bEnabled = enabled;
            if (!enabled)
            {
                m_bRequested = false;
                m_bHotkeyDown = false;
            }
        }
        bool IsEnabled() const
        {
            return m_bEnabled;
        }
        bool IsActive() const
        {
            return m_bApplied;
        }
        // 解除を要求してからApplyするまでの入力もGame側へ漏らさない。
        bool IsMasking() const
        {
            return m_bApplied || m_bRequested;
        }
        bool OnKey(const KeyEvent& event)
        {
            if (!m_bEnabled || event.Code != KeyCode::F1)
            {
                return false;
            }
            if (event.Action == InputAction::Released)
            {
                m_bHotkeyDown = false;
            }
            else if (event.Action == InputAction::Pressed)
            {
                if (!m_bHotkeyDown)
                {
                    m_bRequested = !m_bRequested;
                }
                m_bHotkeyDown = true;
            }
            else if (event.Action == InputAction::Repeat)
            {
                m_bHotkeyDown = true;
            }
            return true;
        }
        bool ApplyRequested()
        {
            if (m_bApplied == m_bRequested)
            {
                return false;
            }
            m_bApplied = m_bRequested;
            return true;
        }
        void ResetKeys()
        {
            m_bHotkeyDown = false;
        }
        // Runの終了/再開ではmoduleのenableだけ維持する。
        void Reset()
        {
            m_bApplied = false;
            m_bRequested = false;
            ResetKeys();
        }
    private:
        bool m_bEnabled = false;
        bool m_bRequested = false;
        bool m_bApplied = false;
        bool m_bHotkeyDown = false;
    };
} // namespace NorvesLib::Core::Input

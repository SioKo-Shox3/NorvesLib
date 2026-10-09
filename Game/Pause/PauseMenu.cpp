#include "Pause/PauseMenu.h"

namespace Game::Pause
{

    namespace
    {
        using NorvesLib::Core::Input::GamepadAxis;
        using NorvesLib::Core::Input::GamepadButton;
        using NorvesLib::Core::Input::InputAction;
        using NorvesLib::Core::Input::KeyCode;

        /// スティックを倒したとみなす値と、戻したとみなす値（ヒステリシス）
        constexpr float kStickPress = 0.6f;
        constexpr float kStickRelease = 0.35f;
    } // namespace

    void PauseMenu::SetEnabled(bool bEnabled)
    {
        m_bEnabled = bEnabled;
        if (!bEnabled)
        {
            Close();
        }
    }

    void PauseMenu::Open()
    {
        if (!m_bEnabled || m_bOpen)
        {
            return;
        }
        m_bOpen = true;
        m_Selected = static_cast<int>(Item::Resume);
        m_ActivateIndex = -1;
        m_StickUpHeld = false;
        m_StickDownHeld = false;
    }

    void PauseMenu::Close()
    {
        m_bOpen = false;
        m_ActivateIndex = -1;
        m_StickUpHeld = false;
        m_StickDownHeld = false;
    }

    void PauseMenu::SetSelected(int index)
    {
        if (index >= 0 && index < kItemCount)
        {
            m_Selected = index;
        }
    }

    void PauseMenu::RequestActivate(int index)
    {
        if (m_bOpen && index >= 0 && index < kItemCount)
        {
            m_ActivateIndex = index;
        }
    }

    bool PauseMenu::ConsumeActivation(Item &item)
    {
        if (m_ActivateIndex < 0)
        {
            return false;
        }
        item = static_cast<Item>(m_ActivateIndex);
        m_ActivateIndex = -1;
        return true;
    }

    void PauseMenu::Move(int delta)
    {
        m_Selected = (m_Selected + delta + kItemCount) % kItemCount;
    }

    void PauseMenu::Activate()
    {
        // 選択の移動はその場で適用しているので、「下、Enter」は移動後の項目を決定する
        m_ActivateIndex = m_Selected;
    }

    bool PauseMenu::OnKey(const NorvesLib::Core::Input::KeyEvent &event)
    {
        const bool bPressed = event.Action == InputAction::Pressed;
        const bool bPressedOrRepeat = bPressed || event.Action == InputAction::Repeat;

        if (!m_bOpen)
        {
            if (m_bEnabled && event.Code == KeyCode::Escape && bPressed)
            {
                Open();
                return true;
            }
            return false;
        }

        // 離した入力は下位へ通す（開く前に押していた Alt などの押下状態を、シーン側が解除できるように）。
        if (event.Action == InputAction::Released)
        {
            return false;
        }

        switch (event.Code)
        {
        case KeyCode::Escape:
            if (bPressed)
            {
                Close();
            }
            break;
        case KeyCode::Up:
        case KeyCode::W:
            if (bPressedOrRepeat)
            {
                Move(-1);
            }
            break;
        case KeyCode::Down:
        case KeyCode::S:
            if (bPressedOrRepeat)
            {
                Move(1);
            }
            break;
        case KeyCode::Enter:
        case KeyCode::NumpadEnter:
        case KeyCode::Space:
            if (bPressed)
            {
                Activate();
            }
            break;
        default:
            break;
        }
        // 開いている間は、F1〜F5 や Alt を含むすべてのキーをシーンへ渡さない。
        return true;
    }

    bool PauseMenu::OnMouseButton(const NorvesLib::Core::Input::MouseButtonEvent &event)
    {
        return m_bOpen && event.Action != InputAction::Released;
    }

    bool PauseMenu::OnMouseMove(const NorvesLib::Core::Input::MouseMoveEvent &)
    {
        return m_bOpen;
    }

    bool PauseMenu::OnMouseScroll(const NorvesLib::Core::Input::MouseScrollEvent &)
    {
        return m_bOpen;
    }

    bool PauseMenu::OnMouseRawMove(const NorvesLib::Core::Input::MouseRawMoveEvent &)
    {
        return m_bOpen;
    }

    bool PauseMenu::OnGamepadButton(const NorvesLib::Core::Input::GamepadButtonEvent &event)
    {
        const bool bPressed = event.Action == InputAction::Pressed;

        if (!m_bOpen)
        {
            if (m_bEnabled && event.Button == GamepadButton::Start && bPressed)
            {
                Open();
                return true;
            }
            return false;
        }

        if (event.Action == InputAction::Released)
        {
            return false;
        }

        if (bPressed)
        {
            switch (event.Button)
            {
            case GamepadButton::Start:
            case GamepadButton::B:
                Close();
                break;
            case GamepadButton::DpadUp:
                Move(-1);
                break;
            case GamepadButton::DpadDown:
                Move(1);
                break;
            case GamepadButton::A:
                Activate();
                break;
            default:
                break;
            }
        }
        return true;
    }

    bool PauseMenu::OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent &event)
    {
        if (!m_bOpen || event.Mode != NorvesLib::Core::Input::EGamepadSampleMode::Live || !event.State.Connected)
        {
            return false;
        }
        // 左スティック。Y は上が正。倒した瞬間に1回だけ移動にする。
        // サンプルは下位（入力の写像）の状態の更新にも要るので、消費しない。
        const float y = event.State.Axes[static_cast<uint8_t>(GamepadAxis::LeftY)];
        if (!m_StickUpHeld && y >= kStickPress)
        {
            m_StickUpHeld = true;
            Move(-1);
        }
        else if (m_StickUpHeld && y < kStickRelease)
        {
            m_StickUpHeld = false;
        }
        if (!m_StickDownHeld && -y >= kStickPress)
        {
            m_StickDownHeld = true;
            Move(1);
        }
        else if (m_StickDownHeld && -y < kStickRelease)
        {
            m_StickDownHeld = false;
        }
        return false;
    }

    void PauseMenu::OnInputReset()
    {
        m_StickUpHeld = false;
        m_StickDownHeld = false;
    }

} // namespace Game::Pause

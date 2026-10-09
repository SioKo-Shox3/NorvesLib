#include "GameModes/Launcher/LauncherMenu.h"

namespace Game::GameModes
{

    namespace
    {
        using NorvesLib::Core::Input::GamepadButton;
        using NorvesLib::Core::Input::GamepadAxis;
        using NorvesLib::Core::Input::InputAction;
        using NorvesLib::Core::Input::KeyCode;

        /// スティックを倒したとみなす値と、戻したとみなす値（ヒステリシス）
        constexpr float kStickPress = 0.6f;
        constexpr float kStickRelease = 0.35f;
    } // namespace

    void LauncherMenu::Reset(int itemCount)
    {
        m_ItemCount = itemCount < 1 ? 1 : itemCount;
        m_Selected = 0;
        m_ReturnCard = 0;
        m_ActivateIndex = -1;
        m_PendingCount = 0;
        for (bool &held : m_StickHeld)
        {
            held = false;
        }
    }

    void LauncherMenu::SetSelected(int index)
    {
        if (index < 0 || index >= m_ItemCount)
        {
            return;
        }
        m_Selected = index;
        if (index < GetQuitIndex())
        {
            m_ReturnCard = index;
        }
    }

    void LauncherMenu::PushStep(Step step)
    {
        if (m_PendingCount < kMaxPendingSteps)
        {
            m_Pending[m_PendingCount++] = step;
        }
    }

    void LauncherMenu::PushDirection(Direction direction)
    {
        switch (direction)
        {
        case Direction::Up:
            PushStep(Step::Up);
            break;
        case Direction::Down:
            PushStep(Step::Down);
            break;
        case Direction::Left:
            PushStep(Step::Left);
            break;
        case Direction::Right:
            PushStep(Step::Right);
            break;
        }
    }

    void LauncherMenu::PushActivate()
    {
        PushStep(Step::Activate);
    }

    void LauncherMenu::ApplyNavigation(int columns)
    {
        if (columns < 1)
        {
            columns = 1;
        }
        const int quit = GetQuitIndex();
        const int cardCount = quit;
        for (int index = 0; index < m_PendingCount; ++index)
        {
            switch (m_Pending[index])
            {
            case Step::Left:
                if (m_Selected > 0 && m_Selected < quit)
                {
                    --m_Selected;
                }
                break;
            case Step::Right:
                if (m_Selected < cardCount - 1)
                {
                    ++m_Selected;
                }
                break;
            case Step::Up:
                if (m_Selected == quit)
                {
                    if (cardCount > 0)
                    {
                        m_Selected = m_ReturnCard < cardCount ? m_ReturnCard : cardCount - 1;
                    }
                }
                else if (m_Selected >= columns)
                {
                    m_Selected -= columns;
                }
                break;
            case Step::Down:
                if (m_Selected < quit)
                {
                    if (m_Selected + columns < cardCount)
                    {
                        m_Selected += columns;
                    }
                    else
                    {
                        m_ReturnCard = m_Selected;
                        m_Selected = quit;
                    }
                }
                break;
            case Step::Activate:
                // ここまでの移動を適用した後の選択を決定する
                RequestActivate(m_Selected);
                break;
            }
            if (m_Selected < quit)
            {
                m_ReturnCard = m_Selected;
            }
        }
        m_PendingCount = 0;
    }

    void LauncherMenu::RequestActivate(int index)
    {
        if (index >= 0 && index < m_ItemCount)
        {
            m_ActivateIndex = index;
        }
    }

    bool LauncherMenu::ConsumeActivation(int &index)
    {
        if (m_ActivateIndex < 0)
        {
            return false;
        }
        index = m_ActivateIndex;
        m_ActivateIndex = -1;
        return true;
    }

    bool LauncherMenu::OnKey(const NorvesLib::Core::Input::KeyEvent &event)
    {
        const bool bPressed = event.Action == InputAction::Pressed;
        const bool bPressedOrRepeat = bPressed || event.Action == InputAction::Repeat;

        switch (event.Code)
        {
        case KeyCode::Up:
        case KeyCode::W:
            if (bPressedOrRepeat)
            {
                PushDirection(Direction::Up);
            }
            return true;
        case KeyCode::Down:
        case KeyCode::S:
            if (bPressedOrRepeat)
            {
                PushDirection(Direction::Down);
            }
            return true;
        case KeyCode::Left:
        case KeyCode::A:
            if (bPressedOrRepeat)
            {
                PushDirection(Direction::Left);
            }
            return true;
        case KeyCode::Right:
        case KeyCode::D:
            if (bPressedOrRepeat)
            {
                PushDirection(Direction::Right);
            }
            return true;
        case KeyCode::Enter:
        case KeyCode::NumpadEnter:
        case KeyCode::Space:
            if (bPressed)
            {
                PushActivate();
            }
            return true;
        default:
            return false;
        }
    }

    bool LauncherMenu::OnGamepadButton(const NorvesLib::Core::Input::GamepadButtonEvent &event)
    {
        if (event.Action != InputAction::Pressed)
        {
            return false;
        }
        switch (event.Button)
        {
        case GamepadButton::DpadUp:
            PushDirection(Direction::Up);
            return true;
        case GamepadButton::DpadDown:
            PushDirection(Direction::Down);
            return true;
        case GamepadButton::DpadLeft:
            PushDirection(Direction::Left);
            return true;
        case GamepadButton::DpadRight:
            PushDirection(Direction::Right);
            return true;
        case GamepadButton::A:
            PushActivate();
            return true;
        default:
            return false;
        }
    }

    bool LauncherMenu::OnGamepadSample(const NorvesLib::Core::Input::GamepadSampleEvent &event)
    {
        if (event.Mode != NorvesLib::Core::Input::EGamepadSampleMode::Live || !event.State.Connected)
        {
            return false;
        }
        // 左スティック。Y は上が正。倒した瞬間に1回だけ方向の入力にする。
        const float x = event.State.Axes[static_cast<uint8_t>(GamepadAxis::LeftX)];
        const float y = event.State.Axes[static_cast<uint8_t>(GamepadAxis::LeftY)];
        const float values[4] = {y, -y, -x, x}; // 上、下、左、右の順
        const Direction directions[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
        for (int index = 0; index < 4; ++index)
        {
            if (!m_StickHeld[index] && values[index] >= kStickPress)
            {
                m_StickHeld[index] = true;
                PushDirection(directions[index]);
            }
            else if (m_StickHeld[index] && values[index] < kStickRelease)
            {
                m_StickHeld[index] = false;
            }
        }
        return false;
    }

    void LauncherMenu::OnInputReset()
    {
        m_PendingCount = 0;
        for (bool &held : m_StickHeld)
        {
            held = false;
        }
    }

} // namespace Game::GameModes

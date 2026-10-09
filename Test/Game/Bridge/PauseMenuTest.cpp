#include "Pause/PauseMenu.h"

#include <iostream>

// ポーズメニューの入力の契約の検査。開く条件（Esc・Start・無効なとき）、開いている間にシーンへ入力を渡さないこと
// （F1〜F5・Alt の押下を含む。離した入力だけは通す）、閉じる経路、移動後の項目の決定を確かめる。

namespace
{
    using Game::Pause::PauseMenu;
    using NorvesLib::Core::Input::GamepadButton;
    using NorvesLib::Core::Input::GamepadButtonEvent;
    using NorvesLib::Core::Input::InputAction;
    using NorvesLib::Core::Input::KeyCode;
    using NorvesLib::Core::Input::KeyEvent;
    using NorvesLib::Core::Input::MouseButtonEvent;

    bool Check(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "PauseMenuTest failed: " << message << "\n";
        }
        return condition;
    }

    KeyEvent KeyAction(KeyCode code, InputAction action)
    {
        KeyEvent event;
        event.Code = code;
        event.Action = action;
        return event;
    }

    KeyEvent Press(KeyCode code)
    {
        return KeyAction(code, InputAction::Pressed);
    }

    GamepadButtonEvent PadPress(GamepadButton button)
    {
        GamepadButtonEvent event;
        event.Button = button;
        event.Action = InputAction::Pressed;
        return event;
    }

    bool TestEscapeOpensAndCloses()
    {
        PauseMenu menu;
        menu.SetEnabled(true);
        bool bPassed = Check(!menu.OnKey(Press(KeyCode::F2)), "closed menu passes other keys to the scene");
        bPassed = Check(!menu.IsOpen(), "starts closed") && bPassed;
        bPassed = Check(menu.OnKey(Press(KeyCode::Escape)), "Escape is consumed when opening") && bPassed;
        bPassed = Check(menu.IsOpen(), "Escape opens the menu") && bPassed;
        bPassed = Check(menu.OnKey(Press(KeyCode::Escape)), "Escape is consumed when closing") && bPassed;
        bPassed = Check(!menu.IsOpen(), "second Escape closes the menu") && bPassed;
        return bPassed;
    }

    bool TestDisabledMenuDoesNotOpen()
    {
        PauseMenu menu;
        menu.SetEnabled(false);
        bool bPassed = Check(!menu.OnKey(Press(KeyCode::Escape)), "disabled menu passes Escape through");
        bPassed = Check(!menu.OnGamepadButton(PadPress(GamepadButton::Start)), "disabled menu passes Start through") &&
                  bPassed;
        bPassed = Check(!menu.IsOpen(), "disabled menu stays closed") && bPassed;
        menu.SetEnabled(true);
        menu.OnKey(Press(KeyCode::Escape));
        menu.SetEnabled(false);
        bPassed = Check(!menu.IsOpen(), "disabling closes an open menu") && bPassed;
        return bPassed;
    }

    bool TestOpenMenuBlocksSceneInput()
    {
        PauseMenu menu;
        menu.SetEnabled(true);
        menu.OnKey(Press(KeyCode::Escape));
        bool bPassed = true;
        for (KeyCode code : {KeyCode::F1, KeyCode::F2, KeyCode::F3, KeyCode::F4, KeyCode::F5, KeyCode::LeftAlt,
                             KeyCode::W, KeyCode::Q})
        {
            // W は移動に使うが、シーンへは渡さない（メニューが消費する）。
            bPassed = Check(menu.OnKey(Press(code)), "open menu consumes key presses") && bPassed;
        }
        bPassed = Check(!menu.OnKey(KeyAction(KeyCode::LeftAlt, InputAction::Released)),
                        "open menu passes releases so the scene can clear held state") &&
                  bPassed;
        MouseButtonEvent click;
        click.Action = InputAction::Pressed;
        bPassed = Check(menu.OnMouseButton(click), "open menu consumes mouse presses") && bPassed;
        click.Action = InputAction::Released;
        bPassed = Check(!menu.OnMouseButton(click), "open menu passes mouse releases") && bPassed;
        bPassed = Check(menu.OnMouseMove({}) && menu.OnMouseScroll({}) && menu.OnMouseRawMove({}),
                        "open menu consumes mouse motion and scroll") &&
                  bPassed;
        menu.OnKey(Press(KeyCode::Escape));
        bPassed = Check(!menu.OnMouseMove({}) && !menu.OnKey(Press(KeyCode::F2)),
                        "closed menu passes input to the scene again") &&
                  bPassed;
        return bPassed;
    }

    bool TestGamepadStartAndB()
    {
        PauseMenu menu;
        menu.SetEnabled(true);
        bool bPassed = Check(menu.OnGamepadButton(PadPress(GamepadButton::Start)) && menu.IsOpen(), "Start opens");
        bPassed = Check(menu.OnGamepadButton(PadPress(GamepadButton::Start)) && !menu.IsOpen(), "Start closes") &&
                  bPassed;
        menu.OnGamepadButton(PadPress(GamepadButton::Start));
        bPassed = Check(menu.OnGamepadButton(PadPress(GamepadButton::B)) && !menu.IsOpen(), "B closes") && bPassed;
        return bPassed;
    }

    bool TestMoveThenActivate()
    {
        PauseMenu menu;
        menu.SetEnabled(true);
        menu.OnKey(Press(KeyCode::Escape));
        bool bPassed = Check(menu.GetSelected() == static_cast<int>(PauseMenu::Item::Resume),
                             "opens on the first item (continue)");
        menu.OnKey(Press(KeyCode::Down));
        menu.OnKey(Press(KeyCode::Enter));
        PauseMenu::Item item = PauseMenu::Item::Resume;
        bPassed = Check(menu.ConsumeActivation(item), "Down+Enter requests an activation") && bPassed;
        bPassed = Check(item == PauseMenu::Item::ReturnToLauncher, "Down+Enter activates the moved-to item") &&
                  bPassed;
        bPassed = Check(!menu.ConsumeActivation(item), "activation is consumed once") && bPassed;

        // 先頭から上へは末尾へ回る。
        menu.SetSelected(0);
        menu.OnGamepadButton(PadPress(GamepadButton::DpadUp));
        menu.OnGamepadButton(PadPress(GamepadButton::A));
        bPassed = Check(menu.ConsumeActivation(item) && item == PauseMenu::Item::Quit, "Up wraps to the last item") &&
                  bPassed;
        return bPassed;
    }

    bool TestReopenStartsOnContinue()
    {
        PauseMenu menu;
        menu.SetEnabled(true);
        menu.Open();
        menu.OnKey(Press(KeyCode::Down));
        menu.OnKey(Press(KeyCode::Enter));
        menu.Close();
        PauseMenu::Item item = PauseMenu::Item::Resume;
        bool bPassed = Check(!menu.ConsumeActivation(item), "closing drops a pending activation");
        menu.Open();
        bPassed = Check(menu.GetSelected() == static_cast<int>(PauseMenu::Item::Resume),
                        "reopening starts on the first item") &&
                  bPassed;
        return bPassed;
    }
} // namespace

int main()
{
    bool bPassed = TestEscapeOpensAndCloses();
    bPassed = TestDisabledMenuDoesNotOpen() && bPassed;
    bPassed = TestOpenMenuBlocksSceneInput() && bPassed;
    bPassed = TestGamepadStartAndB() && bPassed;
    bPassed = TestMoveThenActivate() && bPassed;
    bPassed = TestReopenStartsOnContinue() && bPassed;
    if (!bPassed)
    {
        return 1;
    }
    std::cout << "PauseMenuTest passed\n";
    return 0;
}

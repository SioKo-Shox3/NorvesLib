#include "GameModes/Launcher/LauncherMenu.h"

#include <iostream>

// ランチャーの入力順の検査。同じフレームに積んだ「移動、決定」は、移動した後の項目を決定する。

namespace
{
    using Game::GameModes::LauncherMenu;
    using NorvesLib::Core::Input::GamepadButton;
    using NorvesLib::Core::Input::GamepadButtonEvent;
    using NorvesLib::Core::Input::InputAction;
    using NorvesLib::Core::Input::KeyCode;
    using NorvesLib::Core::Input::KeyEvent;

    bool Check(bool condition, const char *message)
    {
        if (!condition)
        {
            std::cerr << "LauncherMenuTest failed: " << message << "\n";
        }
        return condition;
    }

    KeyEvent Press(KeyCode code)
    {
        KeyEvent event;
        event.Code = code;
        event.Action = InputAction::Pressed;
        return event;
    }

    GamepadButtonEvent PadPress(GamepadButton button)
    {
        GamepadButtonEvent event;
        event.Button = button;
        event.Action = InputAction::Pressed;
        return event;
    }

    // カード 3 枚 + 終了の 4 項目、カードは 1 列。
    constexpr int kItemCount = 4;
    constexpr int kColumns = 1;

    bool TestKeyboardMoveThenEnter()
    {
        LauncherMenu menu;
        menu.Reset(kItemCount);
        menu.OnKey(Press(KeyCode::Down));
        menu.OnKey(Press(KeyCode::Enter));
        menu.ApplyNavigation(kColumns);
        int activated = -1;
        bool bPassed = Check(menu.ConsumeActivation(activated), "Down+Enter requests an activation");
        bPassed = Check(activated == 1, "Down+Enter activates the moved-to item, not the old one") && bPassed;
        return bPassed;
    }

    bool TestEnterThenMoveActivatesOldItem()
    {
        LauncherMenu menu;
        menu.Reset(kItemCount);
        menu.OnKey(Press(KeyCode::Enter));
        menu.OnKey(Press(KeyCode::Down));
        menu.ApplyNavigation(kColumns);
        int activated = -1;
        bool bPassed = Check(menu.ConsumeActivation(activated), "Enter+Down requests an activation");
        bPassed = Check(activated == 0, "Enter+Down activates the item selected before the move") && bPassed;
        bPassed = Check(menu.GetSelected() == 1, "Enter+Down still moves the selection afterwards") && bPassed;
        return bPassed;
    }

    bool TestGamepadMoveToQuitThenA()
    {
        LauncherMenu menu;
        menu.Reset(kItemCount);
        for (int index = 0; index < kItemCount; ++index)
        {
            menu.OnGamepadButton(PadPress(GamepadButton::DpadDown));
        }
        menu.OnGamepadButton(PadPress(GamepadButton::A));
        menu.ApplyNavigation(kColumns);
        int activated = -1;
        bool bPassed = Check(menu.ConsumeActivation(activated), "DpadDown*N+A requests an activation");
        bPassed = Check(activated == menu.GetQuitIndex(), "DpadDown*N+A activates the quit item") && bPassed;
        return bPassed;
    }

    bool TestActivationWaitsForNavigation()
    {
        LauncherMenu menu;
        menu.Reset(kItemCount);
        menu.OnKey(Press(KeyCode::Enter));
        int activated = -1;
        bool bPassed = Check(!menu.ConsumeActivation(activated), "Enter alone is not resolved before ApplyNavigation");
        menu.ApplyNavigation(kColumns);
        bPassed = Check(menu.ConsumeActivation(activated) && activated == 0, "Enter resolves to the current item") && bPassed;
        return bPassed;
    }
} // namespace

int main()
{
    bool bPassed = TestKeyboardMoveThenEnter();
    bPassed = TestEnterThenMoveActivatesOldItem() && bPassed;
    bPassed = TestGamepadMoveToQuitThenA() && bPassed;
    bPassed = TestActivationWaitsForNavigation() && bPassed;
    if (!bPassed)
    {
        return 1;
    }
    std::cout << "LauncherMenuTest passed\n";
    return 0;
}

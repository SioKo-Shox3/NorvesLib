#include "Input/InputArmedState.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace NorvesLib::Core::Input;

int main()
{
    InputState raw;
    InputArmedState armed;
    const GamepadButton buttons[] = {GamepadButton::DpadUp,GamepadButton::DpadDown,GamepadButton::DpadLeft,GamepadButton::DpadRight,
        GamepadButton::Start,GamepadButton::Back,GamepadButton::LeftThumb,GamepadButton::RightThumb,
        GamepadButton::LeftShoulder,GamepadButton::RightShoulder,GamepadButton::A,GamepadButton::B,GamepadButton::X,GamepadButton::Y};
    for(uint8_t slot=0;slot<GamepadSlotCount;++slot)
    {
        assert(!raw.GetGamepadState(slot).Connected);
        for(auto button:buttons)
        {
            GamepadState value;
            value.Connected=true;value.Buttons=static_cast<uint16_t>(button);value.PacketNumber=17;
            value.Axes[0]=-1;value.Axes[1]=1;value.Triggers[1]=1;
            raw.BeginFrame();
            assert(raw.SetGamepadState(slot,value));
            assert(raw.IsGamepadButtonDown(slot,button) && raw.IsGamepadButtonPressed(slot,button));
            assert(raw.GetGamepadAxis(slot,GamepadAxis::LeftX)==-1 && raw.GetGamepadTrigger(slot,GamepadTrigger::Right)==1);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            armed.OnGamepadButton({slot,button,InputAction::Repeat},raw);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            armed.OnGamepadButton({slot,button,InputAction::Pressed},raw);
            assert(armed.IsGamepadButtonArmed(slot,button,raw));
            const auto serial=raw.GetGamepadButtonReleaseSerial(slot,button);
            assert(raw.SetGamepadState(slot,value));
            assert(raw.GetGamepadButtonReleaseSerial(slot,button)==serial);
            raw.BeginFrame();
            assert(raw.GetPreviousGamepadState(slot).Buttons==value.Buttons);
            assert(!raw.IsGamepadButtonPressed(slot,button));
            auto released=value;released.Buttons=0;
            assert(raw.SetGamepadState(slot,released));
            assert(raw.SetGamepadState(slot,value)); // UIが両edgeを消費しても旧armedは失効
            assert(raw.IsGamepadButtonPressed(slot,button) && raw.IsGamepadButtonReleased(slot,button));
            assert(raw.GetGamepadButtonReleaseSerial(slot,button)==serial+1);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            armed.Reconcile(raw);
            armed.OnGamepadButton({slot,button,InputAction::Repeat},raw);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            armed.OnGamepadButton({slot,button,InputAction::Pressed},raw);
            assert(armed.IsGamepadButtonArmed(slot,button,raw));
            assert(raw.SetGamepadState(slot,{})); // 切断はPressedを取り消してReleaseだけ残す
            assert(!raw.IsGamepadButtonDown(slot,button) && !raw.IsGamepadButtonPressed(slot,button));
            assert(raw.IsGamepadButtonReleased(slot,button));
            assert(raw.GetGamepadButtonReleaseSerial(slot,button)==serial+2);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            assert(raw.GetGamepadAxis(slot,GamepadAxis::LeftX)==0);
            assert(raw.SetGamepadState(slot,value));
            armed.OnGamepadButton({slot,button,InputAction::Pressed},raw);
            assert(armed.IsGamepadButtonArmed(slot,button,raw));
            armed.ResetGamepad(slot);
            assert(!armed.IsGamepadButtonArmed(slot,button,raw));
            raw.ReleaseAll();
            const auto neutral=raw.GetGamepadState(slot);
            assert(neutral.Connected && neutral.PacketNumber==17 && neutral.Buttons==0);
            assert(neutral.Axes[0]==0 && neutral.Triggers[1]==0);
            assert(!raw.IsGamepadButtonPressed(slot,button) && raw.IsGamepadButtonReleased(slot,button));
            assert(raw.GetGamepadButtonReleaseSerial(slot,button)==serial+3);
            raw.ReleaseAll();raw.BeginFrame();
            assert(raw.GetGamepadButtonReleaseSerial(slot,button)==serial+3);
            assert(!raw.IsGamepadButtonReleased(slot,button));
        }
    }
    // slotを跨いで更新/解除世代が混ざらない。
    raw=InputState{};armed.Reset();
    GamepadState held;held.Connected=true;held.Buttons=static_cast<uint16_t>(GamepadButton::A);held.PacketNumber=9;
    assert(raw.SetGamepadState(0,held));
    for(uint8_t slot=1;slot<GamepadSlotCount;++slot)
        assert(!raw.GetGamepadState(slot).Connected && raw.GetGamepadButtonReleaseSerial(slot,GamepadButton::A)==0);
    raw.SetKeyState(KeyCode::A,true);armed.OnKey({KeyCode::A,InputAction::Pressed},raw);
    armed.OnGamepadButton({0,GamepadButton::A,InputAction::Pressed},raw);
    armed.ResetGamepad(0);
    assert(armed.IsKeyArmed(KeyCode::A,raw));

    // 非finite/範囲外/予約mask/非接続非neutralは一切適用しない。
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(int invalid=0;invalid<5;++invalid)
    {
        auto bad=held;bad.PacketNumber=99;
        if(invalid==0)bad.Axes[0]=nan;
        if(invalid==1)bad.Triggers[0]=-0.1f;
        if(invalid==2)bad.Axes[0]=1.1f;
        if(invalid==3)bad.Buttons=0x0400;
        if(invalid==4)bad.Connected=false;
        assert(!raw.SetGamepadState(0,bad));
        assert(raw.GetGamepadState(0).PacketNumber==9 && raw.GetGamepadState(0).Buttons==held.Buttons);
        assert(raw.GetGamepadButtonReleaseSerial(0,GamepadButton::A)==0 && raw.IsGamepadButtonPressed(0,GamepadButton::A));
    }
    for(uint16_t invalid=GamepadSlotCount;invalid<256;++invalid)
    {
        const auto slot=static_cast<uint8_t>(invalid);
        assert(!raw.SetGamepadState(slot,held));
        assert(!raw.GetGamepadState(slot).Connected && !raw.GetPreviousGamepadState(slot).Connected);
        assert(!raw.IsGamepadButtonDown(slot,GamepadButton::A) && !raw.IsGamepadButtonPressed(slot,GamepadButton::A));
        assert(!raw.IsGamepadButtonReleased(slot,GamepadButton::A) && raw.GetGamepadButtonReleaseSerial(slot,GamepadButton::A)==0);
        assert(raw.GetGamepadAxis(slot,GamepadAxis::LeftX)==0 && raw.GetGamepadTrigger(slot,GamepadTrigger::Left)==0);
        armed.OnGamepadButton({slot,GamepadButton::A,InputAction::Pressed},raw);
        armed.ResetGamepad(slot);
        assert(!armed.IsGamepadButtonArmed(slot,GamepadButton::A,raw));
    }
    for(uint16_t invalid : {uint16_t(0),uint16_t(3),uint16_t(0x0400),uint16_t(0x0800),uint16_t(0xFFFF)})
    {
        const auto button=static_cast<GamepadButton>(invalid);
        assert(!raw.IsGamepadButtonDown(0,button) && !raw.IsGamepadButtonPressed(0,button) && !raw.IsGamepadButtonReleased(0,button));
        assert(raw.GetGamepadButtonReleaseSerial(0,button)==0);
        armed.OnGamepadButton({0,button,InputAction::Pressed},raw);
        assert(!armed.IsGamepadButtonArmed(0,button,raw));
    }
    assert(raw.GetGamepadAxis(0,static_cast<GamepadAxis>(255))==0);
    assert(raw.GetGamepadTrigger(0,static_cast<GamepadTrigger>(255))==0);
    raw.BeginFrame();
    auto release=held;release.Buttons=0;
    assert(raw.SetGamepadState(0,release));
    assert(raw.SetGamepadState(0,held));
    assert(raw.SetGamepadState(0,release)); // 同frame短tapの両edgeを記録
    assert(raw.IsGamepadButtonPressed(0,GamepadButton::A) && raw.IsGamepadButtonReleased(0,GamepadButton::A));
    assert(!raw.IsGamepadButtonDown(0,GamepadButton::A));
    raw.BeginFrame();
    assert(!raw.IsGamepadButtonPressed(0,GamepadButton::A) && !raw.IsGamepadButtonReleased(0,GamepadButton::A));
    {
        InputState sampled;
        for(uint8_t slot=0;slot<GamepadSlotCount;++slot)
        {
            assert(sampled.GetGamepadSampleSerial(slot)==0 && !sampled.GetLastGamepadSample(slot).Connected);
            GamepadState sample;sample.Connected=true;sample.Buttons=static_cast<uint16_t>(GamepadButton::A);
            sample.Axes[0]=0.75f;sample.Triggers[1]=0.6f;sample.PacketNumber=42u+slot;
            assert(sampled.SetGamepadState(slot,sample));assert(sampled.GetGamepadSampleSerial(slot)==1);
            sampled.ReleaseAll();sampled.ReleaseAll();sampled.BeginFrame();
            assert(sampled.GetGamepadState(slot).Buttons==0 && sampled.GetGamepadAxis(slot,GamepadAxis::LeftX)==0);
            auto last=sampled.GetLastGamepadSample(slot);
            assert(last.Connected && last.Buttons==sample.Buttons && last.Axes[0]==0.75f && last.Triggers[1]==0.6f && last.PacketNumber==42u+slot);
            assert(sampled.GetGamepadSampleSerial(slot)==1);
            last.Buttons=0;assert(sampled.GetLastGamepadSample(slot).Buttons==sample.Buttons); // 値copy。
            auto invalid=sample;invalid.Axes[1]=std::numeric_limits<float>::quiet_NaN();
            assert(!sampled.SetGamepadState(slot,invalid));assert(sampled.GetGamepadSampleSerial(slot)==1 && sampled.GetLastGamepadSample(slot).Axes[1]==0);
            assert(sampled.SetGamepadState(slot,sample));assert(sampled.SetGamepadState(slot,sample));
            assert(sampled.GetGamepadSampleSerial(slot)==3); // packet同値も実sampleとして数える。
            assert(sampled.SetGamepadState(slot,{}));assert(sampled.GetGamepadSampleSerial(slot)==4 && !sampled.GetLastGamepadSample(slot).Connected);
        }
        for(uint16_t slot=GamepadSlotCount;slot<256;++slot)
        {
            assert(sampled.GetGamepadSampleSerial(static_cast<uint8_t>(slot))==0);
            assert(!sampled.GetLastGamepadSample(static_cast<uint8_t>(slot)).Connected);
        }
    }
    std::cout << "GamepadInputStateTest passed\n";
    return 0;
}

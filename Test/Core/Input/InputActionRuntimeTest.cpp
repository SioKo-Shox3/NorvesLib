#include "Input/InputActionRuntime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
using namespace NorvesLib::Core::Input;
namespace
{
    InputBinding Key(KeyCode key, float scale=1)
    {
        InputBinding b;
        b.Source={EInputBindingSource::Key,static_cast<uint16_t>(key),0};
        b.Scale=scale;
        return b;
    }
    void SetKey(InputState& raw, InputArmedState& armed, KeyCode key, bool down)
    {
        raw.SetKeyState(key,down);
        armed.OnKey({key,down?InputAction::Pressed:InputAction::Released},raw);
    }
    bool Near(float a, float b) { return std::abs(a-b)<0.0001f; }
}
int main()
{
    InputState raw;
    InputArmedState armed;
    InputActionRuntime button;
    InputBinding keys[]={Key(KeyCode::W),Key(KeyCode::Space)};
    assert(!button.BeginFrame(0));
    assert(!button.Update(keys,raw,armed,0,0));
    assert(button.Configure({}));
    assert(button.BeginFrame(0));
    // UIが押下を消費したキーは正本だけdownでも発火しない。
    raw.SetKeyState(KeyCode::W,true);
    assert(button.SyncButtons(keys,raw,armed));
    assert(!button.GetButton().Held);
    armed.OnKey({KeyCode::W,InputAction::Pressed},raw);
    assert(button.SyncButtons(keys,raw,armed));
    assert(button.GetButton().Pressed && button.GetButton().Held);
    assert(button.HasPendingFixedPress());
    SetKey(raw,armed,KeyCode::Space,true);
    assert(button.SyncButtons(keys,raw,armed));
    SetKey(raw,armed,KeyCode::W,false);
    assert(button.SyncButtons(keys,raw,armed));
    assert(button.GetButton().Held && !button.GetButton().Released);
    SetKey(raw,armed,KeyCode::Space,false);
    assert(button.SyncButtons(keys,raw,armed));
    assert(button.GetButton().Pressed && button.GetButton().Released && button.GetButton().Tap);
    assert(!button.GetButton().Held);
    assert(button.BeginFrame(0.1));
    assert(!button.GetButton().Pressed && button.ConsumeFixedPress() && !button.ConsumeFixedPress());
    SetKey(raw,armed,KeyCode::W,true);
    assert(button.SyncButtons(keys,raw,armed));
    assert(button.Update(keys,raw,armed,0.7,0.6));
    assert(button.GetButton().Hold && button.GetButton().HoldStarted);
    button.Cancel();
    assert(button.GetButton().Released && !button.GetButton().Held && !button.GetButton().Tap);
    assert(!button.HasPendingFixedPress());
    armed.Reset();
    assert(button.SyncButtons(keys,raw,armed));
    assert(!button.GetButton().Held);
    // 同frameにUIが解除・再押下を消費しても古い許可で復活しない。
    SetKey(raw,armed,KeyCode::W,false);SetKey(raw,armed,KeyCode::W,true);
    assert(button.SyncButtons(keys,raw,armed));
    raw.SetKeyState(KeyCode::W,false);raw.SetKeyState(KeyCode::W,true);
    assert(button.SyncButtons(keys,raw,armed));
    assert(!button.GetButton().Held);
    raw.ReleaseAll();armed.Reset();button.Cancel();

    // wheelのボタン化は即時impulse。持続bindingがdownなら余分な解除を作らない。
    InputBinding impulses[]={Key(KeyCode::W),{}};
    impulses[1].Source={EInputBindingSource::MouseWheel,0,0};
    assert(button.BeginFrame(1));
    assert(button.AccumulateRelative(EInputBindingSource::MouseWheel,0,1,impulses,raw,armed));
    assert(button.GetButton().Pressed && button.GetButton().Released && !button.GetButton().Held);
    assert(button.ConsumeFixedPress());
    assert(button.BeginFrame(2));
    SetKey(raw,armed,KeyCode::W,true);
    assert(button.SyncButtons(impulses,raw,armed));
    assert(button.AccumulateRelative(EInputBindingSource::MouseWheel,0,1,impulses,raw,armed));
    assert(button.GetButton().Held && !button.GetButton().Released);
    raw.ReleaseAll();armed.Reset();button.Cancel();

    InputActionRuntime axis;
    InputActionSettings settings;
    settings.Type=EInputMappingValueType::Axis2D;
    InputBinding movement[]={Key(KeyCode::D),Key(KeyCode::W),Key(KeyCode::A,-1)};
    movement[1].Component=EInputAxisComponent::Y;
    assert(axis.Configure(settings));assert(axis.BeginFrame(0));
    SetKey(raw,armed,KeyCode::D,true);SetKey(raw,armed,KeyCode::W,true);
    assert(axis.Update(movement,raw,armed,0,0));
    assert(Near(axis.GetAxis().x,std::sqrt(0.5f)) && Near(axis.GetAxis().y,std::sqrt(0.5f)));
    SetKey(raw,armed,KeyCode::A,true);
    assert(axis.Update(movement,raw,armed,0,0));
    assert(axis.GetAxis().x==0 && axis.GetAxis().y==1);
    raw.ReleaseAll();armed.Reset();
    // 正規化laneの集約後に曲線を一度適用する。
    GamepadState pad;pad.Connected=true;pad.Axes[0]=0.25f;pad.Axes[1]=0.5f;pad.Triggers[0]=0.75f;
    assert(raw.SetGamepadState(0,pad));
    InputBinding analog[2];
    analog[0].Source={EInputBindingSource::GamepadAxis,0,0};analog[1]=analog[0];
    settings.Type=EInputMappingValueType::Axis1D;settings.AxisResponse.Curve=EInputResponseCurve::Power;settings.AxisResponse.Gamma=2;
    assert(axis.Configure(settings));assert(axis.Update(analog,raw,armed,0,0));
    assert(Near(axis.GetAxis().x,0.25f));
    analog[1].Invert=true;assert(axis.Update(analog,raw,armed,0,0));assert(axis.GetAxis().x==0);
    analog[1].Invert=false;
    InputBinding trigger;trigger.Source={EInputBindingSource::GamepadTrigger,0,0};
    trigger.ButtonThreshold=0.75f;
    assert(button.Configure({}));assert(button.SyncButtons({&trigger,1},raw,armed));assert(button.GetButton().Held);
    trigger.ButtonThreshold=0.8f;assert(button.SyncButtons({&trigger,1},raw,armed));assert(!button.GetButton().Held);
    InputBinding padButton;padButton.Source={EInputBindingSource::GamepadButton,static_cast<uint16_t>(GamepadButton::A),0};
    pad.Buttons=static_cast<uint16_t>(GamepadButton::A);assert(raw.SetGamepadState(0,pad));
    assert(button.SyncButtons({&padButton,1},raw,armed));assert(!button.GetButton().Held);
    armed.OnGamepadButton({0,GamepadButton::A,InputAction::Pressed},raw);
    assert(button.SyncButtons({&padButton,1},raw,armed));assert(button.GetButton().Held);
    button.Cancel();

    // curve前のfloat丸めで半径1を内側へ落としたり、微小入力を消さない。
    InputActionRuntime precision;
    auto precisionSettings=settings;
    precisionSettings.Type=EInputMappingValueType::Axis2D;
    precisionSettings.AxisResponse.Gamma=std::numeric_limits<float>::max();
    assert(precision.Configure(precisionSettings));
    SetKey(raw,armed,KeyCode::D,true);SetKey(raw,armed,KeyCode::W,true);
    assert(precision.Update(movement,raw,armed,0,0));
    assert(Near(precision.GetAxis().x,std::sqrt(0.5f)) && Near(precision.GetAxis().y,std::sqrt(0.5f)));
    raw.ReleaseAll();armed.Reset();
    pad.Axes[0]=1e-23f;assert(raw.SetGamepadState(0,pad));
    precisionSettings.Type=EInputMappingValueType::Axis1D;precisionSettings.AxisResponse.Gamma=0.01f;
    assert(precision.Configure(precisionSettings));
    InputBinding tiny=analog[0];tiny.Scale=1e-23f;
    assert(precision.Update({&tiny,1},raw,armed,0,0));
    assert(Near(precision.GetAxis().x,static_cast<float>(std::pow(static_cast<double>(pad.Axes[0])*tiny.Scale,0.01f))));
    pad.Axes[0]=0.25f;assert(raw.SetGamepadState(0,pad));

    // 相対変位はevent時のmodifier許可で蓄積し、curve/dtを掛けない。
    settings={};settings.Type=EInputMappingValueType::Axis2D;settings.Output=EInputAxisOutput::FrameDelta;
    settings.MouseSensitivity=2;settings.RateSensitivity=4;
    settings.AxisResponse.Curve=EInputResponseCurve::Power;settings.AxisResponse.Gamma=2;
    InputBinding look[3];
    look[0].Source={EInputBindingSource::MouseDelta,0,0};look[0].RequiredModifiers=InputModifierShift;
    look[1].Source={EInputBindingSource::MouseWheel,1,0};look[1].Component=EInputAxisComponent::Y;look[1].Invert=true;
    look[2].Source={EInputBindingSource::GamepadAxis,0,0};
    assert(axis.Configure(settings));assert(axis.BeginFrame(1));
    raw.SetKeyState(KeyCode::LeftShift,true);
    assert(axis.AccumulateRelative(EInputBindingSource::MouseDelta,0,10,look,raw,armed));
    armed.OnKey({KeyCode::LeftShift,InputAction::Pressed},raw);
    assert(axis.AccumulateRelative(EInputBindingSource::MouseDelta,0,3,look,raw,armed));
    SetKey(raw,armed,KeyCode::LeftShift,false);
    assert(axis.AccumulateRelative(EInputBindingSource::MouseWheel,1,2,look,raw,armed));
    assert(axis.Update(look,raw,armed,1,0.5));
    assert(Near(axis.GetAxis().x,6.125f) && axis.GetAxis().y==-4);
    assert(axis.Update(look,raw,armed,1,0.5));assert(Near(axis.GetAxis().x,6.125f));
    // 無効値は時刻・結果・蓄積を部分更新しない。
    const float nan=std::numeric_limits<float>::quiet_NaN();
    auto invalid=settings;invalid.MouseSensitivity=nan;
    assert(!axis.Configure(invalid));assert(axis.GetTime()==1);
    assert(!axis.BeginFrame(0));assert(!axis.BeginFrame(nan));
    assert(!axis.Update(look,raw,armed,2,-1));assert(axis.GetTime()==1);
    assert(!axis.AccumulateRelative(EInputBindingSource::MouseDelta,2,1,look,raw,armed));
    assert(!axis.AccumulateRelative(EInputBindingSource::MouseDelta,0,nan,look,raw,armed));
    assert(Near(axis.GetAxis().x,6.125f));
    axis.Cancel();assert(axis.GetAxis().x==0 && axis.GetAxis().y==0);
    assert(axis.Update(look,raw,armed,1,0));assert(axis.GetAxis().x==0);

    // 同じ総mouse変位と1秒のstick入力ならframe数によらず同じ総量。
    settings.AxisResponse={};settings.MouseSensitivity=0.5f;settings.RateSensitivity=8;
    look[0].RequiredModifiers=0;
    for(int frames:{30,60,144})
    {
        InputActionRuntime sampled;assert(sampled.Configure(settings));
        double total=0;
        for(int frame=0;frame<frames;++frame)
        {
            const double t=static_cast<double>(frame)/frames;
            assert(sampled.BeginFrame(t));
            assert(sampled.AccumulateRelative(EInputBindingSource::MouseDelta,0,120.0f/frames,look,raw,armed));
            assert(sampled.Update(look,raw,armed,t,1.0/frames));
            total+=sampled.GetAxis().x;
        }
        assert(std::abs(total-62)<0.0001);
    }
    // mouse buttonと修飾の両方が配送済みでなければ許可しない。
    InputBinding modifiedMouse;
    modifiedMouse.Source={EInputBindingSource::MouseButton,static_cast<uint16_t>(MouseButton::Left),0};
    modifiedMouse.RequiredModifiers=InputModifierCtrl|InputModifierAlt;
    InputActionRuntime mouseAction;assert(mouseAction.Configure({}));
    raw.SetMouseButtonState(MouseButton::Left,true);
    armed.OnMouseButton({MouseButton::Left,InputAction::Pressed},raw);
    SetKey(raw,armed,KeyCode::LeftCtrl,true);
    assert(mouseAction.SyncButtons({&modifiedMouse,1},raw,armed));assert(!mouseAction.GetButton().Held);
    SetKey(raw,armed,KeyCode::RightAlt,true);
    assert(mouseAction.SyncButtons({&modifiedMouse,1},raw,armed));assert(mouseAction.GetButton().Held);
    auto invalidBinding=modifiedMouse;invalidBinding.Scale=nan;
    assert(!mouseAction.SyncButtons({&invalidBinding,1},raw,armed));assert(mouseAction.GetButton().Held);
    assert(!mouseAction.Update({&invalidBinding,1},raw,armed,1,1));assert(mouseAction.GetTime()==0);
    assert(mouseAction.HasPendingFixedPress());
    assert(!mouseAction.Configure(invalid));assert(mouseAction.GetButton().Held && mouseAction.HasPendingFixedPress());
    SetKey(raw,armed,KeyCode::RightAlt,false);
    assert(mouseAction.SyncButtons({&modifiedMouse,1},raw,armed));assert(!mouseAction.GetButton().Held);
    // 2DのY計算だけoverflowしても先に求めたXを公開しない。
    InputBinding overflowing[2];
    overflowing[0].Source={EInputBindingSource::MouseDelta,0,0};
    overflowing[1].Source={EInputBindingSource::MouseDelta,1,0};overflowing[1].Component=EInputAxisComponent::Y;overflowing[1].Scale=2;
    InputActionRuntime atomicAxis;settings.MouseSensitivity=1;assert(atomicAxis.Configure(settings));
    assert(atomicAxis.AccumulateRelative(EInputBindingSource::MouseDelta,0,2,overflowing,raw,armed));
    assert(atomicAxis.Update(overflowing,raw,armed,0,0));assert(atomicAxis.GetAxis().x==2);
    assert(atomicAxis.AccumulateRelative(EInputBindingSource::MouseDelta,0,3,overflowing,raw,armed));
    assert(atomicAxis.AccumulateRelative(EInputBindingSource::MouseDelta,1,std::numeric_limits<float>::max(),overflowing,raw,armed));
    assert(!atomicAxis.Update(overflowing,raw,armed,1,0));assert(atomicAxis.GetAxis().x==2 && atomicAxis.GetTime()==0);
    // float範囲を超える変位累積もdoubleで相殺後に評価できる。
    settings.Type=EInputMappingValueType::Axis1D;settings.MouseSensitivity=1;settings.RateSensitivity=0;
    InputBinding huge;huge.Source={EInputBindingSource::MouseDelta,0,0};huge.Scale=2;
    InputActionRuntime wide;assert(wide.Configure(settings));assert(wide.BeginFrame(0));
    const float max=std::numeric_limits<float>::max();
    assert(wide.AccumulateRelative(EInputBindingSource::MouseDelta,0,max,{&huge,1},raw,armed));
    assert(!wide.Update({&huge,1},raw,armed,1,0));assert(wide.GetTime()==0 && wide.GetAxis().x==0);
    assert(wide.AccumulateRelative(EInputBindingSource::MouseDelta,0,-max,{&huge,1},raw,armed));
    assert(wide.Update({&huge,1},raw,armed,1,0));assert(wide.GetAxis().x==0);
    // 呼出後にspanの元配列がなくてもruntime自体の操作は安全。
    { InputBinding local=Key(KeyCode::W);assert(button.SyncButtons({&local,1},raw,armed)); }
    button.Cancel();assert(button.BeginFrame(3));
    std::cout << "InputActionRuntimeTest passed\n";
    return 0;
}

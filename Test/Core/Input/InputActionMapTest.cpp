#include "Input/InputMapper.h"
#include "Input/InputSystem.h"
#include "Input/InputRouter.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <limits>
#include <iostream>
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace
{
    InputBinding Key(KeyCode code, EInputAxisComponent component=EInputAxisComponent::X, float scale=1)
    {
        InputBinding binding;
        binding.Source={EInputBindingSource::Key,static_cast<uint16_t>(code),0};
        binding.Component=component;binding.Scale=scale;
        return binding;
    }
    InputBinding Source(EInputBindingSource kind, uint16_t code, EInputAxisComponent component=EInputAxisComponent::X)
    {
        InputBinding binding;binding.Source={kind,code,0};binding.Component=component;return binding;
    }
    InputBindingSet Definitions()
    {
        InputBindingSet definitions;
        assert(definitions.AddContext("Gameplay"_id,ECursorMode::Locked));
        assert(definitions.AddContext("Menu"_id,ECursorMode::Normal));
        InputActionDefinition action;action.Id="Jump"_id;
        action.Bindings={Key(KeyCode::W),Key(KeyCode::Space),
            Source(EInputBindingSource::GamepadButton,static_cast<uint16_t>(GamepadButton::A)),
            Source(EInputBindingSource::GamepadButton,static_cast<uint16_t>(GamepadButton::B))};
        assert(definitions.AddAction("Gameplay"_id,action));
        action={};action.Id="Move"_id;action.Settings.Type=EInputMappingValueType::Axis2D;
        action.Bindings={Key(KeyCode::D),Key(KeyCode::A,EInputAxisComponent::X,-1),
            Key(KeyCode::W,EInputAxisComponent::Y),Key(KeyCode::S,EInputAxisComponent::Y,-1),
            Source(EInputBindingSource::GamepadAxis,0),Source(EInputBindingSource::GamepadAxis,1,EInputAxisComponent::Y)};
        assert(definitions.AddAction("Gameplay"_id,action));
        action={};action.Id="Look"_id;action.Settings.Type=EInputMappingValueType::Axis2D;
        action.Settings.Output=EInputAxisOutput::FrameDelta;action.Settings.MouseSensitivity=0.25f;
        action.Bindings={Source(EInputBindingSource::MouseDelta,0),Source(EInputBindingSource::MouseDelta,1,EInputAxisComponent::Y),
            Source(EInputBindingSource::MouseWheel,0,EInputAxisComponent::Y)};
        assert(definitions.AddAction("Gameplay"_id,action));
        action={};action.Id="Confirm"_id;action.Bindings={Key(KeyCode::Enter)};
        assert(definitions.AddAction("Menu"_id,action));
        action={};action.Id="Click"_id;
        auto click=Source(EInputBindingSource::MouseButton,static_cast<uint16_t>(MouseButton::Left));
        click.RequiredModifiers=InputModifierShift;action.Bindings={click};
        assert(definitions.AddAction("Gameplay"_id,action));
        action={};action.Id="ModifiedLook"_id;action.Settings.Type=EInputMappingValueType::Axis1D;
        action.Settings.Output=EInputAxisOutput::FrameDelta;
        auto relative=Source(EInputBindingSource::MouseDelta,0);relative.RequiredModifiers=InputModifierCtrl;
        action.Bindings={relative};assert(definitions.AddAction("Gameplay"_id,action));
        return definitions;
    }
    class Overlay final : public IInputController
    {
    public:
        bool Consume=false;
        bool OnKey(const KeyEvent&) override { return Consume; }
        bool OnMouseRawMove(const MouseRawMoveEvent&) override { return Consume; }
        bool OnGamepadButton(const GamepadButtonEvent&) override { return Consume; }
        const char* DebugName() const override { return "MapperTestOverlay"; }
    };
    void Begin(InputSystem& system, InputMapper& mapper, double time)
    {
        system.BeginFrame();assert(mapper.BeginFrame(time));
    }
}
int main()
{
    InputSystem system;InputRouter router;Overlay ui;
    system.SetRouter(&router);router.RegisterController(&ui,InputRouter::PriorityOverlay);
    InputMapper mapper(system.GetState());
    auto definitions=Definitions();assert(mapper.Configure(definitions));definitions.Clear();
    assert(!mapper.GetAction("Jump"_id).Valid);
    assert(!mapper.PushContext("Unknown"_id));assert(!mapper.PopContext());
    assert(mapper.PushContext("Gameplay"_id));assert(!mapper.PushContext("Gameplay"_id));
    assert(mapper.GetAction("Jump"_id).Valid && !mapper.GetAction("Jump"_id).Active);
    assert(mapper.GetCursorMode()==ECursorMode::Normal);
    mapper.Attach(router);mapper.Attach(router);
    assert(mapper.GetCursorMode()==ECursorMode::Locked && mapper.GetRequestedCursorMode()==ECursorMode::Locked);
    Begin(system,mapper,0);
    ui.Consume=true;system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(mapper.Update(0,0));assert(!mapper.GetAction("Jump"_id).Button.Held);
    ui.Consume=false;system.InjectKeyEvent(KeyCode::W,InputAction::Repeat);
    assert(mapper.Update(0,0));assert(!mapper.GetAction("Jump"_id).Button.Held);
    system.InjectKeyEvent(KeyCode::W,InputAction::Released);
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    system.InjectKeyEvent(KeyCode::W,InputAction::Released);
    auto jump=mapper.GetAction("Jump"_id);
    assert(jump.Active && jump.Button.Pressed && jump.Button.Released && jump.Button.Tap && !jump.Button.Held);
    Begin(system,mapper,0.1);
    assert(!mapper.GetAction("Jump"_id).Button.Pressed && mapper.ConsumeFixedPress("Jump"_id));
    assert(!mapper.ConsumeFixedPress("Jump"_id));
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    system.InjectKeyEvent(KeyCode::W,InputAction::Released);
    assert(mapper.GetAction("Jump"_id).Button.Held && !mapper.GetAction("Jump"_id).Button.Released);
    assert(mapper.Update(0.7,0.6));assert(mapper.GetAction("Jump"_id).Button.Hold);
    const auto saved=mapper.GetAction("Jump"_id);
    auto replacement=Definitions();
    assert(!mapper.ConfigureWithContext(replacement,"Missing"_id));
    assert(!mapper.ConfigureWithContext(replacement,{}));
    assert(mapper.GetActiveContext()=="Gameplay"_id && mapper.GetAction("Jump"_id).Button.Held);
    assert(mapper.PushContext("Menu"_id));
    assert(!mapper.GetAction("Jump"_id).Valid && mapper.GetCursorMode()==ECursorMode::Normal);
    jump=mapper.GetAction("Gameplay"_id,"Jump"_id);
    assert(!jump.Active && jump.Button.Released && !jump.Button.Held && !jump.Button.Tap);
    assert(!mapper.ConsumeFixedPress("Jump"_id));
    system.InjectKeyEvent(KeyCode::Enter,InputAction::Pressed);
    assert(mapper.GetAction("Confirm"_id).Button.Held);
    assert(mapper.Update(0.8,0.1));assert(mapper.PopContext());
    assert(mapper.GetActiveContext()=="Gameplay"_id);
    assert(mapper.Update(0.8,0));assert(!mapper.GetAction("Jump"_id).Button.Held);
    assert(saved.Button.Held && saved.Button.Hold); // 返却値は独立snapshot。
    system.InjectKeyEvent(KeyCode::Space,InputAction::Released);
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    assert(mapper.GetAction("Jump"_id).Button.Held);
    // UIがreleaseとrepressを両方消費しても旧armedが復活しない。
    ui.Consume=true;
    system.InjectKeyEvent(KeyCode::Space,InputAction::Released);
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    assert(mapper.Update(0.9,0.1));assert(!mapper.GetAction("Jump"_id).Button.Held);
    ui.Consume=false;
    system.ReleaseAll();Begin(system,mapper,1);
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    mapper.SetFocused(false);
    assert(mapper.GetRequestedCursorMode()==ECursorMode::Locked);
    assert(!mapper.GetAction("Jump"_id).Active && mapper.GetAction("Jump"_id).Button.Released);
    assert(!mapper.ConsumeFixedPress("Jump"_id) && mapper.GetCursorMode()==ECursorMode::Normal);
    system.ReleaseAll();system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    mapper.SetFocused(true);assert(mapper.Update(1,0));assert(!mapper.GetAction("Jump"_id).Button.Held);
    system.InjectKeyEvent(KeyCode::W,InputAction::Released);system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(mapper.GetAction("Jump"_id).Button.Held);
    assert(!mapper.BeginFrame(0));assert(!mapper.Update(2,-1));
    assert(!mapper.BeginFrame(std::numeric_limits<double>::quiet_NaN()));
    assert(mapper.GetAction("Jump"_id).Button.Held);
    system.ReleaseAll();Begin(system,mapper,2);
    assert(!mapper.GetAction("Jump"_id).Button.Released);
    ui.Consume=true;assert(system.InjectRawMouseDelta(100,100));
    ui.Consume=false;assert(system.InjectRawMouseDelta(4,-8));
    system.InjectMouseMove(100,100);system.InjectMouseMove(200,200);
    system.InjectMouseScroll(2);
    assert(mapper.Update(2,0.5));
    auto look=mapper.GetAction("Look"_id);
    assert(look.Axis.x==1 && look.Axis.y==-1.5f);
    Begin(system,mapper,3);assert(mapper.Update(3,1));assert(mapper.GetAction("Look"_id).Axis.x==0);
    // 同一snapshotのPad持替えはaggregate buttonを一度も離さない。
    GamepadState pad;pad.Connected=true;pad.Buttons=static_cast<uint16_t>(GamepadButton::A);
    assert(system.InjectGamepadState(0,pad));assert(mapper.GetAction("Jump"_id).Button.Held);
    Begin(system,mapper,4);
    pad.Buttons=static_cast<uint16_t>(GamepadButton::B);assert(system.InjectGamepadState(0,pad));
    jump=mapper.GetAction("Jump"_id);assert(jump.Button.Held && !jump.Button.Released && !jump.Button.Pressed);
    assert(system.InjectGamepadState(0,{}));
    jump=mapper.GetAction("Jump"_id);assert(jump.Button.Released && !jump.Button.Tap && !jump.Button.Held);
    assert(!mapper.ConsumeFixedPress("Jump"_id));
    // 別bindingのkeyboardがheldならPad切断だけで解除しない。
    Begin(system,mapper,5);system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(system.InjectGamepadState(0,pad));
    Begin(system,mapper,6);assert(system.InjectGamepadState(0,{}));
    jump=mapper.GetAction("Jump"_id);assert(jump.Button.Held && !jump.Button.Released && !jump.Button.Pressed);
    system.ReleaseAll();Begin(system,mapper,7);
    pad.Buttons=0;pad.Axes[0]=0.5f;assert(system.InjectGamepadState(0,pad));
    assert(mapper.Update(7,1));assert(mapper.GetAction("Move"_id).Axis.x==0.5f);
    mapper.Detach();assert(!mapper.GetAction("Move"_id).Active && mapper.GetCursorMode()==ECursorMode::Normal && mapper.GetRequestedCursorMode()==ECursorMode::Normal);
    assert(mapper.Update(7,0));assert(mapper.GetAction("Move"_id).Axis.x==0);
    system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);assert(!mapper.GetAction("Jump"_id).Button.Held);
    mapper.Attach(router);system.ReleaseAll();Begin(system,mapper,8);
    system.InjectMouseButton(MouseButton::Left,InputAction::Pressed,0,0);
    assert(!mapper.GetAction("Click"_id).Button.Held);
    system.InjectKeyEvent(KeyCode::LeftShift,InputAction::Pressed);
    assert(mapper.GetAction("Click"_id).Button.Held);
    system.InjectKeyEvent(KeyCode::LeftShift,InputAction::Released);
    assert(mapper.GetAction("Click"_id).Button.Released);
    ui.Consume=true;system.InjectKeyEvent(KeyCode::LeftCtrl,InputAction::Pressed);
    ui.Consume=false;assert(system.InjectRawMouseDelta(3,0));
    assert(mapper.Update(8,0));assert(mapper.GetAction("ModifiedLook"_id).Axis.x==0);
    system.InjectKeyEvent(KeyCode::LeftCtrl,InputAction::Released);
    system.InjectKeyEvent(KeyCode::LeftCtrl,InputAction::Pressed);
    assert(system.InjectRawMouseDelta(2,0));
    system.InjectKeyEvent(KeyCode::LeftCtrl,InputAction::Released);
    assert(mapper.Update(8,0));assert(mapper.GetAction("ModifiedLook"_id).Axis.x==2);
    system.ReleaseAll();Begin(system,mapper,8);
    {
        InputRouter other;
        mapper.Attach(other);system.SetRouter(nullptr);
        system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
        router.DispatchKey({KeyCode::W,InputAction::Pressed});
        assert(!mapper.GetAction("Jump"_id).Button.Held);
        other.DispatchKey({KeyCode::W,InputAction::Pressed});
        assert(mapper.GetAction("Jump"_id).Button.Held);
        mapper.Attach(router);
        other.DispatchKey({KeyCode::W,InputAction::Pressed});
        assert(!mapper.GetAction("Jump"_id).Button.Held);
        system.SetRouter(&router);
    }
    system.ReleaseAll();Begin(system,mapper,8);
    // 個別actionのoverflowはCancelし、他actionの正常な評価は維持する。
    definitions=Definitions();auto overflow=*definitions.FindAction("Gameplay"_id,"Look"_id);
    overflow.Settings.MouseSensitivity=std::numeric_limits<float>::max();
    assert(definitions.ReplaceAction("Gameplay"_id,overflow));assert(mapper.Configure(definitions));
    assert(!mapper.GetActiveContext().IsValid());assert(mapper.PushContext("Gameplay"_id));
    assert(system.InjectRawMouseDelta(2,0));system.InjectKeyEvent(KeyCode::W,InputAction::Pressed);
    assert(!mapper.Update(8,0));assert(mapper.GetAction("Look"_id).Axis.x==0);
    assert(mapper.GetAction("Jump"_id).Button.Held && mapper.ConsumeFixedPress("Jump"_id));
    assert(mapper.Update(8,0));
    mapper.ClearContexts();assert(!mapper.GetAction("Jump"_id).Valid && !mapper.PopContext());
    assert(mapper.ConfigureWithContext(replacement,"Menu"_id));
    assert(mapper.GetActiveContext()=="Menu"_id && mapper.GetAction("Confirm"_id).Valid);
    assert(mapper.Configure(replacement));assert(!mapper.GetActiveContext().IsValid());
    mapper.Detach();mapper.Detach();router.UnregisterController(&ui);system.SetRouter(nullptr);
    // destructorは借用Routerより先に解除する。
    { InputMapper scoped(system.GetState());scoped.Attach(router); }
    router.DispatchKey({KeyCode::W,InputAction::Pressed});
    {
        InputSystem source;InputRouter route;source.SetRouter(&route);InputMapper target(source.GetState());target.Attach(route);
        auto original=Definitions();assert(target.ConfigureWithContext(original,"Gameplay"_id));assert(target.PushContext("Menu"_id));
        source.InjectKeyEvent(KeyCode::Enter,InputAction::Pressed);
        assert(target.GetAction("Confirm"_id).Button.Held);
        InputBindingSet missing;assert(missing.AddContext("Menu"_id,ECursorMode::Normal));
        assert(!target.ConfigurePreservingContexts(missing));
        assert(target.GetActiveContext()=="Menu"_id && target.GetAction("Confirm"_id).Button.Held);
        // 定義の配列順が変わってもstackをindexでなくIdentityで引き直す。
        InputBindingSet reordered;
        for(auto it=original.GetContexts().rbegin();it!=original.GetContexts().rend();++it)
        {
            assert(reordered.AddContext(it->Id,it->CursorMode));
            for(const auto& action:it->Actions) assert(reordered.AddAction(it->Id,action));
        }
        assert(target.ConfigurePreservingContexts(reordered));
        assert(target.GetActiveContext()=="Menu"_id && !target.GetAction("Confirm"_id).Button.Held && !target.ConsumeFixedPress("Confirm"_id));
        assert(target.PopContext() && target.GetActiveContext()=="Gameplay"_id);
        assert(reordered.SetBindings("Gameplay"_id,"Jump"_id,{Key(KeyCode::Q)}));
        source.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);assert(target.GetAction("Jump"_id).Button.Held);
        assert(target.ConfigurePreservingContexts(reordered));assert(!target.GetAction("Jump"_id).Button.Held);
        source.InjectKeyEvent(KeyCode::Q,InputAction::Pressed);assert(target.GetAction("Jump"_id).Button.Held);
        target.SetFocused(false);assert(target.ConfigurePreservingContexts(original));
        assert(!target.IsFocused() && target.GetRequestedCursorMode()==ECursorMode::Locked);
        target.ClearContexts();assert(target.ConfigurePreservingContexts(original));assert(!target.GetActiveContext().IsValid());
        target.Detach();source.SetRouter(nullptr);
    }
    {
        InputSystem source;
        InputRouter route;
        source.SetRouter(&route);
        InputMapper target(source.GetState());
        target.Attach(route);
        assert(target.ConfigureWithContext(Definitions(), "Gameplay"_id));
        GamepadState held;
        held.Connected = true;
        held.Buttons = static_cast<uint16_t>(GamepadButton::A);
        held.Axes[0] = 1;
        assert(source.InjectGamepadState(0, held));
        assert(target.GetAction("Jump"_id).Button.Held);
        target.SetFocused(false);
        source.ReleaseAll();
        assert(source.InjectGamepadState(0, held, EGamepadSampleMode::Background));
        assert(target.Update(0, 0.01));
        assert(!target.GetAction("Jump"_id).Active && !target.GetAction("Jump"_id).Button.Held);
        assert(!target.GetAction("Jump"_id).Button.Tap && target.GetAction("Move"_id).Axis.x == 0);
        assert(source.GetState().GetLastGamepadSample(0).Buttons == held.Buttons);
        target.SetFocused(true);
        assert(source.InjectGamepadState(0, held, EGamepadSampleMode::Baseline));
        assert(target.Update(0, 0.01));
        assert(!target.GetAction("Jump"_id).Button.Held && !target.ConsumeFixedPress("Jump"_id));
        assert(target.GetAction("Move"_id).Axis.x == 1); // analogは復帰後の連続値として評価する。
        assert(source.InjectGamepadState(0, held));
        assert(target.Update(0, 0.01) && !target.GetAction("Jump"_id).Button.Held);
        auto released = held;
        released.Buttons = 0;
        assert(source.InjectGamepadState(0, released));
        assert(source.InjectGamepadState(0, held));
        assert(target.GetAction("Jump"_id).Button.Held && target.ConsumeFixedPress("Jump"_id));
        target.Detach();
        source.SetRouter(nullptr);
    }
    {
        InputSystem source;
        InputRouter routes;
        source.SetRouter(&routes);
        InputMapper target(source.GetState());
        auto definitions = Definitions();
        InputActionDefinition trigger;
        trigger.Id = "TriggerJump"_id;
        trigger.Bindings = {Source(EInputBindingSource::GamepadTrigger, 0)};
        assert(definitions.AddAction("Gameplay"_id, trigger));
        assert(target.ConfigureWithContext(definitions, "Gameplay"_id));
        target.Attach(routes);
        assert(target.SetFixedButtonEventCapture("Jump"_id, true));
        assert(target.SetFixedButtonEventCapture("TriggerJump"_id, true));
        assert(!target.SetFixedButtonEventCapture("Move"_id, true));
        Begin(source, target, 0);
        for (int i = 0; i < 2; ++i)
        {
            source.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
            source.InjectKeyEvent(KeyCode::W, InputAction::Released);
        }
        Begin(source, target, .01);
        Begin(source, target, .02);
        InputButtonEvent event;
        for (int i = 0; i < 4; ++i)
        {
            assert(target.ConsumeFixedButtonEvent("Jump"_id, event));
            assert(event.Type == (i % 2 == 0 ? EInputButtonEventType::Pressed : EInputButtonEventType::Released));
        }
        assert(!target.ConsumeFixedButtonEvent("Jump"_id, event));
        GamepadState pad;
        pad.Connected = true;
        assert(source.InjectGamepadState(0, pad, EGamepadSampleMode::Baseline));
        pad.Triggers[0] = 1;
        assert(source.InjectGamepadState(0, pad));
        pad.Triggers[0] = 0;
        assert(source.InjectGamepadState(0, pad));
        assert(target.ConsumeFixedButtonEvent("TriggerJump"_id, event) && event.Type == EInputButtonEventType::Pressed);
        assert(target.ConsumeFixedButtonEvent("TriggerJump"_id, event) &&
               event.Type == EInputButtonEventType::Released);
        source.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        assert(target.ConfigurePreservingContexts(definitions));
        assert(!target.ConsumeFixedButtonEvent("Jump"_id, event));
        source.InjectKeyEvent(KeyCode::W, InputAction::Released);
        source.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        source.InjectKeyEvent(KeyCode::W, InputAction::Released);
        assert(target.ConsumeFixedButtonEvent("Jump"_id, event) && event.Type == EInputButtonEventType::Pressed);
        assert(target.ConsumeFixedButtonEvent("Jump"_id, event) && event.Type == EInputButtonEventType::Released);
        source.InjectKeyEvent(KeyCode::W, InputAction::Pressed);
        const auto generation = target.GetCancellationGeneration();
        target.SetFocused(false);
        assert(target.GetCancellationGeneration() != generation);
        assert(!target.ConsumeFixedButtonEvent("Jump"_id, event));
        target.SetFocused(true);
        assert(!target.ConsumeFixedButtonEvent("Jump"_id, event));
        const auto beforeReconfigure = target.GetCancellationGeneration();
        assert(target.ConfigurePreservingContexts(definitions));
        assert(target.GetCancellationGeneration() != beforeReconfigure);
        const auto beforeReset = target.GetCancellationGeneration();
        target.CancelAll();
        assert(target.GetCancellationGeneration() != beforeReset);
        assert(target.SetFixedButtonEventCapture("Jump"_id, false));
        target.Detach();
        source.SetRouter(nullptr);
    }
    std::cout << "InputActionMapTest passed\n";
    return 0;
}

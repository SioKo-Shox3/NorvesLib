#include "Engine/Engine.h"
#include "Engine/ApplicationProcessor.h"
#include "Application/IWindow.h"
#include "Application/ApplicationHandlerBase.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
namespace NorvesLib::Core::Engine
{
    struct ApplicationInputFocusTestAccess
    {
        static void Connect(ApplicationProcessor& p,Container::TSharedPtr<IWindow> w) { p.ConnectInputWindow(std::move(w)); }
        static void Disconnect(ApplicationProcessor& p) { p.DisconnectInputWindow(); }
        static void Flush(ApplicationProcessor& p) { p.DispatchInputFocusEvents(); }
        static bool Pump(ApplicationProcessor& p) { return p.ProcessPlatformMessages(); }
    };
}
using namespace NorvesLib;
using namespace NorvesLib::Core;
using namespace NorvesLib::Core::Input;
using namespace NorvesLib::Core::literals;
namespace Eng=NorvesLib::Core::Engine;
namespace
{
    class FocusWindow final : public IWindow
    {
    public:
        bool Focused=true;
        bool RawEnabled=false;
        int RawEnables=0,RawDisables=0;
        bool SetRawMouseEnabled(bool enabled) noexcept override
        {
            RawEnabled=enabled;if(enabled) ++RawEnables;else ++RawDisables;return true;
        }
        bool IsRawMouseEnabled() const noexcept override { return RawEnabled; }
        void Emit(bool value) { Focused=value;NotifyInputFocusChanged(value); }
        bool Create(const Container::String&,int,int) override { return true; }
        void Destroy() override {}
        void Show() override {}
        void Hide() override {}
        void SetTitle(const Container::String&) override {}
        void Resize(int,int) override {}
        bool IsActive() const override { return true; }
        bool IsInputFocused() const override { return Focused; }
        Platform::NativeWindowHandle GetNativeHandle() const override { return {}; }
    };
    class FocusObserver final : public IInputController
    {
    public:
        int Resets=0;
        bool Consume=false;
        Container::VariableArray<bool> Focus;
        const char* DebugName() const override { return "FocusObserver"; }
        bool OnKey(const KeyEvent&) override { return Consume; }
        void OnInputReset() override { ++Resets; }
        void OnInputFocusChanged(bool value) override { Focus.push_back(value); }
    };
    class FocusHandler final : public Application::ApplicationHandlerBase
    {
    public:
        Container::VariableArray<bool> Events;
        Eng::ApplicationProcessor* Processor=nullptr;
        FocusWindow* Window=nullptr;
        bool* InPump=nullptr;
        bool Reenter=false,Reconnect=false,Throw=false,DestroyPlatform=false;
        void Record(bool value)
        {
            assert(!InPump || !*InPump);
            Events.push_back(value);
            if(Throw) { Throw=false;throw 42; }
            if(DestroyPlatform)
            {
                DestroyPlatform=false;InPump=nullptr;Eng::GEngine->SetPlatformApp({});
            }
            if(Reenter)
            {
                Reenter=false;const auto count=Events.size();
                Window->Emit(!Window->Focused);Window->Emit(!Window->Focused);
                Eng::ApplicationInputFocusTestAccess::Flush(*Processor);
                assert(Events.size()==count); // 再入は次batchまで遅延する。
            }
            if(Reconnect)
            {
                Reconnect=false;
                Eng::ApplicationInputFocusTestAccess::Connect(*Processor,Eng::GEngine->GetMainWindowShared());
            }
        }
        void OnFocusGained() override { Record(true); }
        void OnFocusLost() override { Record(false); }
    };
    class FocusPlatform final : public IApplication
    {
    public:
        FocusWindow* Window=nullptr;
        FocusHandler* Handler=nullptr;
        bool InPump=false;
        bool Initialize(const Container::VariableArray<Container::String>&) override { return true; }
        void Shutdown() override {}
        IWindow* GetMainWindow() override { return Window; }
        void RegisterWindow(Container::TSharedPtr<IWindow>) override {}
        void UnregisterWindow(Container::TSharedPtr<IWindow>) override {}
        void PumpMessages() override
        {
            InPump=true;const auto count=Handler->Events.size();
            Window->Emit(false);Window->Emit(true);
            assert(Handler->Events.size()==count);
            InPump=false;
        }
        bool IsExitRequested() const override { return false; }
        int GetExitCode() const override { return 0; }
    };
}
int main()
{
    auto* previous=Eng::GEngine;
    auto engine=Container::MakeUnique<Eng::Engine>();Eng::GEngine=engine.get();
    auto window=Container::MakeShared<FocusWindow>();engine->SetMainWindow(window);
    Eng::ApplicationProcessor processor;
    auto handler=Container::MakeShared<FocusHandler>();handler->Processor=&processor;handler->Window=window.get();engine->SetApplicationHandler(handler);
    FocusObserver observer;engine->GetInputRouter().RegisterController(&observer,InputRouter::PriorityOverlay);
    auto& system=engine->GetInputSystem();auto& mapper=engine->GetInputMapper();
    InputBindingSet definitions;assert(definitions.AddContext("Gameplay"_id,ECursorMode::Locked));
    InputActionDefinition action;action.Id="Jump"_id;
    InputBinding key;key.Source={EInputBindingSource::Key,static_cast<uint16_t>(KeyCode::Space),0};action.Bindings.push_back(key);
    assert(definitions.AddAction("Gameplay"_id,action));assert(mapper.ConfigureWithContext(definitions,"Gameplay"_id));
    Eng::ApplicationInputFocusTestAccess::Connect(processor,window);
    assert(window->OnInputFocusChanged().GetSize()==1 && handler->Events.empty());
    assert(window->RawEnabled && window->RawEnables==1);
    Eng::ApplicationInputFocusTestAccess::Flush(processor);
    assert(handler->Events.size()==1 && handler->Events[0] && observer.Focus.size()==1);
    system.InjectKeyEvent(KeyCode::Space,InputAction::Pressed);
    assert(system.GetState().IsKeyDown(KeyCode::Space) && mapper.GetAction("Jump"_id).Button.Held);
    observer.Consume=true;
    window->Emit(false);window->Emit(false);
    assert(observer.Resets==1 && observer.Focus.size()==2 && !observer.Focus[1]);
    assert(!system.GetState().IsKeyDown(KeyCode::Space) && !mapper.IsFocused() && mapper.GetCursorMode()==ECursorMode::Normal);
    assert(handler->Events.size()==1);
    window->Emit(true);assert(mapper.IsFocused() && !mapper.GetAction("Jump"_id).Button.Held);
    Eng::ApplicationInputFocusTestAccess::Flush(processor);
    assert(handler->Events.size()==3 && !handler->Events[1] && handler->Events[2]);
    auto platform=Container::MakeUnique<FocusPlatform>();platform->Window=window.get();platform->Handler=handler.get();
    handler->InPump=&platform->InPump;engine->SetPlatformApp(std::move(platform));
    assert(Eng::ApplicationInputFocusTestAccess::Pump(processor));
    assert(handler->Events.size()==5 && !handler->Events[3] && handler->Events[4]);
    handler->Reenter=true;window->Emit(false);Eng::ApplicationInputFocusTestAccess::Flush(processor);
    assert(handler->Events.size()==6);
    Eng::ApplicationInputFocusTestAccess::Flush(processor);assert(handler->Events.size()==8);
    // 同じwindowへの再購読でも旧batchの後続通知を新しい購読へ持ち越さない。
    handler->Reconnect=true;window->Emit(true);window->Emit(false);
    Eng::ApplicationInputFocusTestAccess::Flush(processor);assert(handler->Events.size()==9);
    Eng::ApplicationInputFocusTestAccess::Flush(processor);assert(handler->Events.size()==10 && !handler->Events.back());
    handler->Throw=true;window->Emit(true);
    bool caught=false;try { Eng::ApplicationInputFocusTestAccess::Flush(processor); } catch(int value) { caught=value==42; }
    assert(caught);window->Emit(false);Eng::ApplicationInputFocusTestAccess::Flush(processor);assert(!handler->Events.back());
    // handlerがplatformを破棄しても、Pump後の借用pointerを再利用しない。
    handler->DestroyPlatform=true;
    assert(Eng::ApplicationInputFocusTestAccess::Pump(processor));
    assert(!engine->GetPlatformApp());
    const auto oldCount=handler->Events.size();
    auto second=Container::MakeShared<FocusWindow>();engine->SetMainWindow(second);
    Eng::ApplicationInputFocusTestAccess::Connect(processor,second);
    assert(window->OnInputFocusChanged().IsEmpty() && second->OnInputFocusChanged().GetSize()==1);
    assert(!window->RawEnabled && second->RawEnabled);
    window->Emit(false);assert(mapper.IsFocused());
    Eng::ApplicationInputFocusTestAccess::Flush(processor);assert(handler->Events.size()==oldCount+1);
    Eng::ApplicationInputFocusTestAccess::Disconnect(processor);
    assert(second->OnInputFocusChanged().IsEmpty() && !second->RawEnabled);second->Emit(false);assert(mapper.IsFocused());
    {
        Eng::ApplicationProcessor temporary;
        Eng::ApplicationInputFocusTestAccess::Connect(temporary,second);
        assert(second->OnInputFocusChanged().GetSize()==1);
    }
    assert(second->OnInputFocusChanged().IsEmpty() && !second->RawEnabled);
    engine->GetInputRouter().UnregisterController(&observer);
    engine->SetApplicationHandler({});engine->SetPlatformApp({});engine->SetMainWindow({});engine.reset();Eng::GEngine=previous;
    std::cout << "InputFocusPipelineTest passed\n";
    return 0;
}
